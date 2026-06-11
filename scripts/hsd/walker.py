# SPDX-License-Identifier: GPL-3.0-only
# Contains code derived from HSDLib (https://github.com/Ploaj/HSDLib),
# Copyright (c) 2021 Ploaj, used under the MIT License.
# See THIRD_PARTY_NOTICES.md at the repo root for the full MIT license text.
"""Type-aware reachability walker over HSD model trees.

Starts from a root offset of a known type (typically JOBJDesc) and
follows every reloc'd pointer field, recording each reached offset
with its type and computed size.

Sizing strategy:
- Known fixed-size HSD types use their hardcoded struct size.
- ImageDesc-pointed image data is sized from width x height x bpp,
  rounded up to the GX format's tile padding.
- TlutDesc-pointed palette data is sized from n_entries x 2.
- Display lists, vertex arrays, and VtxDescList terminator scans use
  a neighbor-offset heuristic (extend until the next reachable start).
  These are small enough that the slop is negligible.

Animation scope:
- Shallow AOBJ / FOBJDesc / FOBJ walking is supported (struct fields
  + packed keyframe buffers sized from FOBJDesc.dataLength). Wired
  in at FogAnim+0x04 today. The track-type byte is intentionally
  printed raw -- its enum (Fog/Joint/Mat/Tex/Light/Shape track) is
  context-dependent on which slot the AOBJ hangs from.
- HSD_AnimJoint / HSD_MatAnimJoint / HSD_ShapeAnimJoint tree
  accessors (hung off JOBJDesc / ModelGroup) and the LightAnimPointer
  / WOBJAnim chains are intentionally NOT walked. Those would
  cascade into the whole anim system; we stop at the AOBJ leaf.

Use `Walker(arc).walk(root_off)` to get an `OrderedDict[off] = (type, size)`.
`merge_intervals` collapses adjacent (start, end) ranges, useful for
emitting a minimal carved archive.
"""

from collections import OrderedDict

from .archive import Archive, u16, u32


# GX texture format -> (block_w, block_h, bpp)
GX_FORMATS = {
    0: (8, 8, 4),  # GX_TF_I4
    1: (8, 4, 8),  # GX_TF_I8
    2: (8, 4, 8),  # GX_TF_IA4
    3: (4, 4, 16),  # GX_TF_IA8
    4: (4, 4, 16),  # GX_TF_RGB565
    5: (4, 4, 16),  # GX_TF_RGB5A3
    6: (4, 4, 32),  # GX_TF_RGBA8
    8: (8, 8, 4),  # GX_TF_C4
    9: (8, 4, 8),  # GX_TF_C8
    10: (4, 4, 16),  # GX_TF_C14X2
    14: (8, 8, 4),  # GX_TF_CMPR
}


def image_size(width, height, fmt, mipmap=False):
    """Bytes for a GX texture, padded up to the format's block size.
    Mipmaps add ~33% for the geometric pyramid; rounded to 1.4x for slack."""
    bw, bh, bpp = GX_FORMATS.get(fmt, (4, 4, 16))
    pw = ((width + bw - 1) // bw) * bw
    ph = ((height + bh - 1) // bh) * bh
    base = pw * ph * bpp // 8
    return int(base * 1.4) if mipmap else base


def merge_intervals(intervals, gap=4):
    """Sort and merge overlapping/adjacent (start, end) intervals.
    `gap` controls how close two intervals must be to fuse - the HSD
    loader doesn't care about uninvolved bytes inside a kept range, so
    fusing across a 4-byte gap is safe and trims reloc-table size."""
    if not intervals:
        return []
    intervals = sorted(intervals)
    merged = [list(intervals[0])]
    for start, end in intervals[1:]:
        last = merged[-1]
        if start <= last[1] + gap:
            last[1] = max(last[1], end)
        else:
            merged.append([start, end])
    return [(s, e) for s, e in merged]


class Walker:
    """visited[offset] = (type_name, size_in_bytes)."""

    def __init__(self, arc: Archive):
        self.arc = arc
        self.visited = OrderedDict()
        self.work = []

    def follow(self, src, slot, target_type, size_hint=None):
        if (src + slot) not in self.arc.reloc_set:
            return
        tgt = u32(self.arc.data, src + slot)
        if tgt == 0:
            return
        self.work.append((tgt, target_type, size_hint))

    def follow_array(self, src, slot, elem_type):
        """Follow a NULL-terminated pointer array (HSDNullPointerArrayAccessor).
        The slot at src+slot points to a sequence of u32 pointers; each
        non-NULL entry is in the reloc table and is followed as `elem_type`.
        The array ends at the first entry that is not relocated AND zero."""
        if (src + slot) not in self.arc.reloc_set:
            return
        arr = u32(self.arc.data, src + slot)
        if arr == 0:
            return
        n = 0
        while True:
            entry = arr + n * 4
            if entry + 4 > len(self.arc.data):
                break
            in_reloc = entry in self.arc.reloc_set
            val = u32(self.arc.data, entry)
            if not in_reloc:
                # Terminator (or junk past end-of-array): stop.
                break
            if val == 0:
                # NULL entry inside the reloc set is unusual but valid.
                n += 1
                continue
            self.work.append((val, elem_type, None))
            n += 1
        # Record the array itself so the sizer accounts for its footprint
        # (n entries + terminator slot).
        if arr not in self.visited:
            self.visited[arr] = (f"NullPtrArray<{elem_type}>", (n + 1) * 4)

    def walk(self, root, root_type="JOBJDesc"):
        self.work.append((root, root_type, None))
        while self.work:
            off, typ, hint = self.work.pop(0)
            if off == 0 or off in self.visited:
                continue
            handler = getattr(self, f"visit_{typ}", None)
            size = handler(off, hint) if handler else hint
            self.visited[off] = (typ, size)

        # Resolve unknown sizes via the next-reachable-start heuristic.
        sorted_offs = sorted(self.visited.keys())
        for i, off in enumerate(sorted_offs):
            typ, sz = self.visited[off]
            if sz is None:
                next_o = (
                    sorted_offs[i + 1]
                    if i + 1 < len(sorted_offs)
                    else len(self.arc.data)
                )
                self.visited[off] = (typ, next_o - off)
        return self.visited

    # --- Type handlers -------------------------------------------------

    # JOBJ flag bits we route on. Stage data sometimes uses the SPLINE
    # and PTCL bits to repurpose the +0x10 union slot (which is normally
    # a DObj*) -- treating those as DObj would chase garbage pointers.
    _JOBJ_FLAG_PTCL = 1 << 5
    _JOBJ_FLAG_SPLINE = 1 << 14

    def visit_JOBJDesc(self, off, _):
        self.follow(off, 0x00, "cstring")
        self.follow(off, 0x08, "JOBJDesc")
        self.follow(off, 0x0C, "JOBJDesc")
        flags = u32(self.arc.data, off + 0x04)
        if flags & self._JOBJ_FLAG_SPLINE:
            self.follow(off, 0x10, "Spline")
        elif flags & self._JOBJ_FLAG_PTCL:
            self.follow(off, 0x10, "ParticleJoint")
        else:
            self.follow(off, 0x10, "DObjDesc")
        self.follow(off, 0x38, "Mtx")
        self.follow(off, 0x3C, "RObjDesc")
        return 0x40

    def visit_DObjDesc(self, off, _):
        self.follow(off, 0x00, "cstring")
        self.follow(off, 0x04, "DObjDesc")
        self.follow(off, 0x08, "MObjDesc")
        self.follow(off, 0x0C, "POBJDesc")
        return 0x10

    def visit_MObjDesc(self, off, _):
        # MObj layout (HSDLib HSD_MOBJ.cs): 0x08=TObj, 0x0C=Material,
        # 0x14=PEDesc. Offset 0x10 is unused; older versions of this
        # walker mislabeled it as PEDesc and called +0x14 "LightTable".
        self.follow(off, 0x00, "cstring")
        self.follow(off, 0x08, "TObjDesc")
        self.follow(off, 0x0C, "MaterialDesc")
        self.follow(off, 0x14, "PEDesc")
        return 0x18

    def visit_TObjDesc(self, off, _):
        self.follow(off, 0x00, "cstring")
        self.follow(off, 0x04, "TObjDesc")
        self.follow(off, 0x4C, "ImageDesc")
        self.follow(off, 0x50, "TlutDesc")
        self.follow(off, 0x54, "TexLODDesc")
        self.follow(off, 0x58, "TObjTev")
        return 0x5C

    def visit_ImageDesc(self, off, _):
        w = u16(self.arc.data, off + 4)
        h = u16(self.arc.data, off + 6)
        fmt = u32(self.arc.data, off + 8)
        mip = u32(self.arc.data, off + 0xC) != 0
        sz = image_size(w, h, fmt, mip)
        self.follow(off, 0x00, "image_blob", sz)
        return 0x18

    def visit_TlutDesc(self, off, _):
        n = u16(self.arc.data, off + 0x0C)
        self.follow(off, 0x00, "palette_blob", n * 2)
        return 0x10

    # POBJ flag bits we route on (HSDLib HSD_POBJ.cs POBJ_FLAG enum).
    _POBJ_FLAG_SHAPEANIM = 1 << 12
    _POBJ_FLAG_ENVELOPE = 1 << 13

    def visit_POBJDesc(self, off, _):
        self.follow(off, 0x00, "cstring")
        self.follow(off, 0x04, "POBJDesc")
        self.follow(off, 0x08, "VtxDescList")
        self.follow(off, 0x10, "dl_blob")
        # POBJ +0x14 is a flag-tagged union (HSDLib HSD_POBJ.cs):
        #   SHAPEANIM -> HSD_ShapeSet
        #   ENVELOPE  -> NullPtrArray<HSD_Envelope>
        #   else      -> HSD_JOBJ (SingleBoundJOBJ)
        flags = u16(self.arc.data, off + 0x0C)
        if flags & self._POBJ_FLAG_SHAPEANIM:
            self.follow(off, 0x14, "ShapeSet")
        elif flags & self._POBJ_FLAG_ENVELOPE:
            self.follow_array(off, 0x14, "Envelope")
        else:
            self.follow(off, 0x14, "JOBJDesc")
        return 0x18

    def visit_VtxDescList(self, off, _):
        # 0x18-byte entries terminated by attr=0xFF; follow each entry's
        # vertex pointer (entry+0x14).
        cur = off
        while cur + 0x18 <= len(self.arc.data):
            attr = u32(self.arc.data, cur)
            if attr == 0xFF:
                break
            self.follow(cur, 0x14, "vertex_blob")
            cur += 0x18
        return cur + 0x18 - off

    # HSD_ROBJ layout (HSDLib HSD_ROBJ.cs): 0x00=next, 0x04=flags,
    # 0x08=ref (union by REFTYPE in top nibble). Older walker followed
    # +0x0C as JOBJDesc, which is past the 0xC-byte struct entirely.
    _ROBJ_REFTYPE_JOBJ = 0x10000000  # bits 28-30 == 1

    def visit_RObjDesc(self, off, _):
        self.follow(off, 0x00, "RObjDesc")  # next RObj in list
        flags = u32(self.arc.data, off + 0x04)
        if (flags & 0x70000000) == self._ROBJ_REFTYPE_JOBJ:
            self.follow(off, 0x08, "JOBJDesc")
        # Other REFTYPEs (EXP, LIMIT, BYTECODE, IKHINT) point at types
        # we don't need to size for asset analysis; leave them unfollowed.
        return 0xC

    # --- Scene / camera --------------------------------------------------

    # HSD_SOBJ (HSDLib HSD_SOBJ.cs): a 0x10 record with three pointer-array
    # slots and an inline FogAnim. KAR archives use this for UI/HUD scenes
    # (IfAll11.dat) and for camera/light groups attached to cutscenes.
    def visit_SOBJ(self, off, _):
        self.follow_array(off, 0x00, "ModelGroup")  # JOBJDescs**
        self.follow_array(off, 0x04, "Camera")  # Cameras**
        self.follow_array(off, 0x08, "Light")  # Lights**
        self.follow(off, 0x0C, "FogAnim")  # inline FogAnim*
        return 0x10

    # HSD_JOBJDesc (the 0x10 wrapper, not HSD_JOBJ - name collides with our
    # existing JOBJDesc which is really HSD_JOBJ). Holds a root joint plus
    # three animation-joint chains we don't size.
    def visit_ModelGroup(self, off, _):
        self.follow(off, 0x00, "JOBJDesc")  # RootJoint -> HSD_JOBJ
        # 0x04 JointAnimations, 0x08 MaterialAnimations, 0x0C ShapeAnimations
        # are HSDNullPointerArrayAccessor<HSD_AnimJoint/MatAnimJoint/ShapeAnim>.
        # Animation joints are intentionally out of scope (see module docstring).
        return 0x10

    # HSD_Camera / COBJ (HSDLib HSD_COBJ.cs, 0x40 bytes). eye/target are
    # WObj transforms; the rest is projection/viewport scalars we don't
    # follow.
    def visit_Camera(self, off, _):
        self.follow(off, 0x00, "cstring")  # ClassName
        self.follow(off, 0x18, "WObjDesc")  # eye
        self.follow(off, 0x1C, "WObjDesc")  # target
        return 0x40

    # HSD_FogAnim (HSDLib HSD_SOBJ.cs, 0x08): wraps a FogDesc + an AOBJ.
    def visit_FogAnim(self, off, _):
        self.follow(off, 0x00, "FogDesc")
        self.follow(off, 0x04, "AOBJ")
        return 0x08

    # --- Animation (shallow) --------------------------------------------

    # HSD_AOBJ (HSDLib HSD_AOBJ.cs, 0x10): animation object. Holds a list
    # of FOBJDesc tracks and an optional JOBJ object reference. The
    # AnimJoint trees that hang off JOBJDesc/ModelGroup are NOT walked
    # here -- this handler exists only so the AOBJ slots already pointed
    # at by other walked structs (e.g. FogAnim+0x04) get their FOBJDesc
    # chain and keyframe buffers sized.
    def visit_AOBJ(self, off, _):
        self.follow(off, 0x08, "FOBJDesc")  # track list head
        self.follow(off, 0x0C, "JOBJDesc")  # object reference (often NULL)
        return 0x10

    # HSD_FOBJDesc (HSDLib HSD_FOBJDesc.cs, 0x14): one animation track.
    # Linked list via +0x00; keyframe data at +0x10 sized from +0x04
    # dataLength (raw byte count of the packed bit-stream).
    def visit_FOBJDesc(self, off, _):
        self.follow(off, 0x00, "FOBJDesc")  # next track
        data_len = u32(self.arc.data, off + 0x04)
        self.follow(off, 0x10, "anim_buffer", data_len)
        return 0x14

    # HSD_FOBJ (HSDLib HSD_FOBJ.cs, 0x08): transient/un-Desc form of a
    # track. No dataLength field; buffer size resolves via the
    # next-reachable-start heuristic. Only used if an archive holds raw
    # FOBJs (FigaTree-style storage); FOBJDesc is the on-disk norm.
    def visit_FOBJ(self, off, _):
        self.follow(off, 0x04, "anim_buffer")
        return 0x08

    def visit_anim_buffer(self, off, hint):
        return hint

    # --- Envelope / ShapeSet (POBJ +0x14 branches) ----------------------

    # HSD_Envelope (HSDLib HSD_Envelope.cs): variable-length array of
    # 8-byte entries (JOBJ* + float weight). The end is at the next
    # reachable allocation; size resolves via the next-reachable-start
    # heuristic.
    def visit_Envelope(self, off, _):
        # Each 8-byte entry: 0x00 JOBJ ref, 0x04 weight float.
        i = 0
        while True:
            entry = off + i * 8
            if entry + 8 > len(self.arc.data):
                break
            # Bail when neither field is in the reloc set (envelope ended).
            if entry not in self.arc.reloc_set:
                if u32(self.arc.data, entry) == 0:
                    break
                # weight-only entry shouldn't exist, but be safe
                break
            self.follow(entry, 0x00, "JOBJDesc")
            i += 1
        # Size resolves via next-reachable-start heuristic.
        return None

    # HSD_ShapeSet (HSDLib HSD_ShapeSet.cs). Fixed-ish 0x1C header with
    # two attribute/index pairs (vertex + normal). The attribute and index
    # tables are variable-length; size resolves via the heuristic.
    def visit_ShapeSet(self, off, _):
        self.follow(off, 0x08, "shapeset_blob")  # VertexAttributes
        self.follow(off, 0x0C, "shapeset_blob")  # VertexIndices table
        self.follow(off, 0x14, "shapeset_blob")  # NormalAttributes
        self.follow(off, 0x18, "shapeset_blob")  # NormalIndices table
        return 0x1C

    def visit_shapeset_blob(self, off, hint):
        return hint

    # --- Standalone media -----------------------------------------------

    # HSD_IOBJ (HSDLib HSD_IOBJ.cs, 0x0C): standalone image object,
    # essentially an ImageDesc without the wrapping TObj. Same image
    # sizing rule applies.
    def visit_IOBJDesc(self, off, _):
        w = u16(self.arc.data, off + 0x00)
        h = u16(self.arc.data, off + 0x02)
        fmt = u32(self.arc.data, off + 0x04)
        sz = image_size(w, h, fmt)
        self.follow(off, 0x08, "image_blob", sz)
        return 0x0C

    # HSD_ParticleGroup (HSDLib HSD_ParticleGroup.cs): a header followed
    # by an array of byte-offsets that delimit embedded generator data.
    # The whole record is variable-length and the generator data are
    # not relocated pointers but byte ranges inside the same allocation.
    def visit_ParticleGroup(self, off, _):
        count = u32(self.arc.data, off + 0x08)
        # Header (0x0C) + count generator-offset entries.
        return 0x0C + max(0, count) * 4

    # --- Lights & world-space helpers ----------------------------------

    # KAR-side wrappers around HSD lights (HSDLib AirRide/Gr/Data/
    # KAR_grLightGroup.cs). LightGroup holds three LightNode pointers
    # (Global, Group1, Group2); LightNode is a fixed array of four
    # HSD_Light pointers; HSD_Light wraps an LObj + an anim pointer.
    def visit_LightGroup(self, off, _):
        self.follow(off, 0x00, "LightNode")
        self.follow(off, 0x04, "LightNode")
        self.follow(off, 0x08, "LightNode")
        return 0x0C

    def visit_LightNode(self, off, _):
        for slot in (0x00, 0x04, 0x08, 0x0C):
            self.follow(off, slot, "Light")
        return 0x10

    def visit_Light(self, off, _):
        self.follow(off, 0x00, "LObjDesc")
        # 0x04 is an anim pointer chain we don't size today.
        return 0x08

    def visit_LObjDesc(self, off, _):
        self.follow(off, 0x04, "LObjDesc")  # next LObj in linked list
        self.follow(off, 0x10, "WObjDesc")  # Position
        self.follow(off, 0x14, "WObjDesc")  # Interest
        # 0x18 -> attenuation block; type depends on flag bits 0-1
        # (AMBIENT/INFINITE/POINT/SPOT). Treat as opaque so the sizer
        # falls back to next-reachable-start.
        self.follow(off, 0x18, "lobj_attn_blob")
        return 0x1C

    def visit_WObjDesc(self, off, _):
        self.follow(off, 0x00, "cstring")
        self.follow(off, 0x10, "RObjDesc")
        return 0x14

    def visit_FogDesc(self, off, _):
        self.follow(off, 0x04, "FogAdjDesc")
        return 0x18

    def visit_FogAdjDesc(self, off, _):
        return 0x44

    def visit_Spline(self, off, _):
        return 0x18

    def visit_ParticleJoint(self, off, _):
        return 0x08

    def visit_Mtx(self, off, _):
        return 0x30  # 4x3 floats inv_world

    def visit_MaterialDesc(self, off, _):
        return 0x14

    def visit_TexLODDesc(self, off, _):
        return 0x10

    def visit_PEDesc(self, off, _):
        return 0xC

    def visit_TObjTev(self, off, _):
        return 0x20

    def visit_cstring(self, off, _):
        end = self.arc.data.find(b"\0", off)
        return (end - off + 1) if end >= 0 else 1

    def visit_image_blob(self, off, hint):
        return hint

    def visit_palette_blob(self, off, hint):
        return hint

    def visit_dl_blob(self, off, hint):
        return hint  # heuristic

    def visit_vertex_blob(self, off, hint):
        return hint  # heuristic

    def visit_envelope_or_joint(self, off, hint):
        return hint

    def visit_lobj_attn_blob(self, off, hint):
        return hint  # 0x0C/0x14 by LObj type
