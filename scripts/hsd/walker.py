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
- Display lists, vertex arrays, VtxDescList terminator scans, Spline
  length/segment arrays, and ParticleGroup embedded generators use a
  neighbor-offset heuristic (extend until the next reachable start).
  These are small enough that the slop is negligible.

Animation scope:
- The animation-joint trees are fully walked: HSD_AnimJoint,
  HSD_MatAnimJoint (-> HSD_MatAnim -> HSD_TexAnim), and
  HSD_ShapeAnimJoint (-> HSD_ShapeAnim -> HSD_AOBJDesc), plus the
  HSD_ROBJAnimJoint / HSD_WOBJAnim / HSD_LightAnimPointer chains. They
  are reached from the HSD_JOBJDesc (ModelGroup) anim-array slots,
  KAR_grModelMotion, HSD_Light+0x04, and the KAR_grSubAnimNode slots
  hung off grData (stage sub-animations). The set is finite: every
  branch bottoms out at an HSD_AOBJ (whose FOBJDesc keyframe buffers
  are sized from dataLength) or, for texture animations, at the
  ImageDesc / TlutDesc frame buffers reused from the model sizer.
- HSD_FigaTree keyframe containers are walked too (count table +
  embedded HSD_Track array, each track's buffer sized from its
  DataLength). These appear as standalone anim publics rather than
  hanging off the model tree.
- The FOBJ/FOBJDesc track-type byte is printed raw -- its enum
  (Fog/Joint/Mat/Tex/Light/Shape track) is context-dependent on which
  slot the AOBJ hangs from.

KAR stage-model roots (KAR_grModel) are also a valid root type: the
walk descends into the MainModel and SkyboxModel JOBJ geometry trees,
the SkyboxModel's ModelMotion animation joints, and the MainModel's
ModelBounding spatial-culling metadata (four embedded-record
containers -- view regions, dynamic/static bounding boxes, indices --
plus the per-record u16 index arrays). The only slots still sized as
leaves are KAR_grModel's two unidentified trailing model pointers,
which HSDLib leaves unmapped.

Use `Walker(arc).walk(root_off)` to get an `OrderedDict[off] = (type, size)`.
`merge_intervals` collapses adjacent (start, end) ranges and `carve_ranges`
concatenates the reachable bytes into a new data section (with the reloc
table rebuilt), the two steps a minimal carved archive needs.
"""

import struct
from collections import OrderedDict, namedtuple

from .archive import Archive, u16, u32
from .gx import image_size  # noqa: F401  (re-exported via hsd/__init__.py)


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


CarveResult = namedtuple("CarveResult", "data remap relocs dropped intervals")


def carve_ranges(arc, visited, prefix, base_relocs=(), skip_relocs=(), source=None):
    """Concatenate the reachable byte ranges in `visited` after `prefix`,
    preserving each range's 32-byte (GX cache-line) alignment, and rebuild
    the reloc table into carved coordinates.

    visited:     OrderedDict[off] = (type, size) from Walker.walk().
    prefix:      bytearray the caller has pre-populated (descriptor / pp
                 slot / name string); the ranges land after it.
    base_relocs: reloc sources the caller synthesizes inside `prefix`
                 (ModelSection / descriptor pointer slots), seeded into
                 the result's reloc list.
    skip_relocs: source offsets the caller rewrites itself (e.g. a
                 repointed ImageDesc) - left out of the translated set.
    source:      data section to copy bytes from (defaults to arc.data;
                 pass a modified copy, e.g. scaled geometry).

    Returns CarveResult(data, remap, relocs, dropped, intervals):
      data      bytearray = prefix + padded, concatenated kept ranges.
      remap     old data offset -> new data offset (kept bytes only).
      relocs    list(base_relocs) + every translated in-range reloc source.
      dropped   count of relocs whose target fell outside the kept ranges
                (their source dword is zeroed - only happens to dangling
                pointers in slop bytes that merging pulled into a range).
      intervals the merged (start, end) source ranges that were kept.
    """
    src = arc.data if source is None else source
    intervals = merge_intervals([(off, off + sz) for off, (_, sz) in visited.items()])

    new_data = bytearray(prefix)
    remap = {}
    cursor = len(new_data)
    for s, e in intervals:
        pad = ((s & 31) - (cursor & 31)) & 31
        if pad:
            new_data.extend(b"\0" * pad)
            cursor += pad
        for o in range(s, e):
            remap[o] = cursor + (o - s)
        new_data.extend(src[s:e])
        cursor += e - s

    relocs = list(base_relocs)
    dropped = 0
    skip = set(skip_relocs)
    for reloc_src in arc.relocs:
        if reloc_src not in remap or reloc_src in skip:
            continue
        new_src = remap[reloc_src]
        tgt = u32(src, reloc_src)
        if tgt in remap:
            struct.pack_into(">I", new_data, new_src, remap[tgt])
            relocs.append(new_src)
        else:
            struct.pack_into(">I", new_data, new_src, 0)
            dropped += 1
    return CarveResult(new_data, remap, relocs, dropped, intervals)


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

    def follow_count_array(self, src, slot, count, elem_type, stride=4):
        """Follow a count-delimited contiguous pointer array (HSDArrayAccessor).
        Unlike follow_array, the length is not self-terminating: HSDArrayAccessor
        stores no count on disc, so the caller passes `count` read from a sibling
        field on the parent (e.g. TexAnim.ImageCount). Each `stride`-byte entry's
        first word is a reloc'd pointer followed as `elem_type`."""
        if (src + slot) not in self.arc.reloc_set:
            return
        arr = u32(self.arc.data, src + slot)
        if arr == 0 or count <= 0:
            return
        for i in range(count):
            self.follow(arr + i * stride, 0x00, elem_type)
        if arr not in self.visited:
            self.visited[arr] = (f"Array<{elem_type}>", count * stride)

    def follow_ptr_run(self, src, slot, elem_type):
        """Follow a contiguous run of reloc'd 4-byte pointer slots whose length
        is delimited by the reloc set rather than a stored count. Each slot is
        followed as `elem_type`; the run ends at the first slot not in the
        reloc table. Used for tables (e.g. ShapeSet index tables) whose on-disc
        count field is unreliable."""
        if (src + slot) not in self.arc.reloc_set:
            return
        tbl = u32(self.arc.data, src + slot)
        if tbl == 0:
            return
        n = 0
        while (tbl + n * 4) in self.arc.reloc_set:
            self.follow(tbl + n * 4, 0x00, elem_type)
            n += 1
        if tbl not in self.visited:
            self.visited[tbl] = (f"PtrTable<{elem_type}>", n * 4)

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
        # 0x14=PEDesc; 0x10 is unused.
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

    # HSD_ROBJ layout (HSDLib HSD_ROBJ.cs, 0xC bytes): 0x00=next,
    # 0x04=flags, 0x08=ref (union by REFTYPE in top nibble).
    _ROBJ_REFTYPE_JOBJ = 0x10000000  # bits 28-30 == 1

    def visit_RObjDesc(self, off, _):
        self.follow(off, 0x00, "RObjDesc")  # next RObj in list
        flags = u32(self.arc.data, off + 0x04)
        if (flags & 0x70000000) == self._ROBJ_REFTYPE_JOBJ:
            self.follow(off, 0x08, "JOBJDesc")
        # Other REFTYPEs (EXP, LIMIT, BYTECODE, IKHINT) point at types
        # we don't need to size for asset analysis; leave them unfollowed.
        return 0xC

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
        # 0x04/0x08/0x0C are NULL-terminated pointer arrays of joint /
        # material / shape animation trees, one entry per model in the group.
        self.follow_array(off, 0x04, "AnimJoint")
        self.follow_array(off, 0x08, "MatAnimJoint")
        self.follow_array(off, 0x0C, "ShapeAnimJoint")
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

    # --- Animation joint trees ---------------------------------------------
    # Reached from ModelGroup (three NULL-ptr arrays), KAR_grModelMotion, and
    # HSD_Light. All branches bottom out at an already-handled AOBJ (keyframe
    # buffers) or, for texture animation, at ImageDesc / TlutDesc frames.

    # HSD_AnimJoint (HSDLib HSD_AnimJoint.cs, 0x14): a Child/Next tree mirroring
    # the JOBJ skeleton; each node carries one AOBJ of transform tracks.
    def visit_AnimJoint(self, off, _):
        self.follow(off, 0x00, "AnimJoint")  # child
        self.follow(off, 0x04, "AnimJoint")  # next
        self.follow(off, 0x08, "AOBJ")
        return 0x14

    # HSD_MatAnimJoint (HSDLib HSD_MatAnimJoint.cs, 0x0C): Child/Next tree whose
    # nodes each own a HSD_MatAnim list.
    def visit_MatAnimJoint(self, off, _):
        self.follow(off, 0x00, "MatAnimJoint")  # child
        self.follow(off, 0x04, "MatAnimJoint")  # next
        self.follow(off, 0x08, "MatAnim")
        return 0x0C

    # HSD_MatAnim (HSDLib HSD_MatAnim.cs, 0x10): per-material list node; an AOBJ
    # of material-color tracks plus an optional texture-animation list.
    def visit_MatAnim(self, off, _):
        self.follow(off, 0x00, "MatAnim")  # next
        self.follow(off, 0x04, "AOBJ")
        self.follow(off, 0x08, "TexAnim")
        return 0x10

    # HSD_TexAnim (HSDLib HSD_TexAnim.cs, 0x18): per-texmap list node. The
    # ImageBuffers / TlutBuffers are count-delimited arrays (counts at 0x14 /
    # 0x16) of pointers to the frame ImageDesc / TlutDesc allocations -- the
    # texture-animation frames a wrapper-level carve would otherwise drop.
    def visit_TexAnim(self, off, _):
        self.follow(off, 0x00, "TexAnim")  # next
        self.follow(off, 0x08, "AOBJ")
        img_count = u16(self.arc.data, off + 0x14)
        tlut_count = u16(self.arc.data, off + 0x16)
        self.follow_count_array(off, 0x0C, img_count, "ImageDesc")
        self.follow_count_array(off, 0x10, tlut_count, "TlutDesc")
        return 0x18

    # HSD_ShapeAnimJoint (HSDLib HSD_ShapeAnimJoint.cs, 0x0C): Child/Next tree of
    # HSD_ShapeAnim lists (vertex-morph animation).
    def visit_ShapeAnimJoint(self, off, _):
        self.follow(off, 0x00, "ShapeAnimJoint")  # child
        self.follow(off, 0x04, "ShapeAnimJoint")  # next
        self.follow(off, 0x08, "ShapeAnim")
        return 0x0C

    # HSD_ShapeAnim (HSDLib HSD_ShapeAnim.cs, 0x08): list node wrapping an
    # HSD_AOBJDesc chain.
    def visit_ShapeAnim(self, off, _):
        self.follow(off, 0x00, "ShapeAnim")  # next
        self.follow(off, 0x04, "AOBJDesc")
        return 0x08

    # HSD_AOBJDesc (HSDLib HSD_AOBJ.cs, 0x08): list node wrapping one AOBJ.
    def visit_AOBJDesc(self, off, _):
        self.follow(off, 0x00, "AOBJDesc")  # next
        self.follow(off, 0x04, "AOBJ")
        return 0x08

    # HSD_ROBJAnimJoint (HSDLib HSD_ROBJAnimJoint.cs, 0x08): list node wrapping
    # one AOBJ, used for WObj (light/camera target) animation.
    def visit_ROBJAnimJoint(self, off, _):
        self.follow(off, 0x00, "ROBJAnimJoint")  # next
        self.follow(off, 0x04, "AOBJ")
        return 0x08

    # HSD_WOBJAnim (HSDLib HSD_WOBJ.cs, 0x08): pairs a value AOBJ with an
    # ROBJAnimJoint reference chain.
    def visit_WOBJAnim(self, off, _):
        self.follow(off, 0x00, "AOBJ")
        self.follow(off, 0x04, "ROBJAnimJoint")
        return 0x08

    # HSD_LightAnimPointer (HSDLib HSD_LOBJ.cs, 0x10): list node holding the
    # light's color AOBJ plus position / interest WObj animations.
    def visit_LightAnimPointer(self, off, _):
        self.follow(off, 0x00, "LightAnimPointer")  # next
        self.follow(off, 0x04, "AOBJ")
        self.follow(off, 0x08, "WOBJAnim")
        self.follow(off, 0x0C, "WOBJAnim")
        return 0x10

    # HSD_FigaTree (HSDLib HSD_FigaTree.cs, 0x14): a standalone keyframe
    # container (joint-animation "AJ" storage). 0x0C -> a byte count table
    # (one track-count per node, 0xFF terminator) and 0x10 -> a blob of
    # TrackCount embedded HSD_Track records (0x0C each). Each track's keyframe
    # buffer hangs off track+0x08, sized by the DataLength u16 at track+0x00.
    def visit_FigaTree(self, off, _):
        track_count = 0
        if (off + 0x0C) in self.arc.reloc_set:
            tbl = u32(self.arc.data, off + 0x0C)
            n = 0
            while tbl + n < len(self.arc.data) and self.arc.data[tbl + n] != 0xFF:
                track_count += self.arc.data[tbl + n]
                n += 1
            tbl_size = (n + 1 + 3) & ~3  # nodes + terminator, padded to 4
            self.follow(off, 0x0C, "figatree_blob", tbl_size)
        if (off + 0x10) in self.arc.reloc_set:
            tracks = u32(self.arc.data, off + 0x10)
            for i in range(track_count):
                tk = tracks + i * 0x0C
                data_len = u16(self.arc.data, tk + 0x00)
                self.follow(tk, 0x08, "anim_buffer", data_len)
            self.follow(off, 0x10, "figatree_blob", track_count * 0x0C)
        return 0x14

    def visit_figatree_blob(self, off, hint):
        return hint

    # KAR_grSubAnimNode (HSDLib KAR_grSubAnimNode.cs, 0x18): the grData-side
    # sub-animation table -- six KAR_grSubAnim slots (SuperJump, Rail, ...),
    # each a count-delimited array of HSD_AnimJoint. Reached via grData+0x24,
    # not from a model root; walkable with --root-type grSubAnimNode.
    def visit_grSubAnimNode(self, off, _):
        for slot in (0x00, 0x04, 0x08, 0x0C, 0x10, 0x14):
            self.follow(off, slot, "grSubAnim")
        return 0x18

    # KAR_grSubAnim (0x08): 0x00 -> HSDFixedLengthPointerArrayAccessor of
    # HSD_AnimJoint, length in the int32 Count at 0x04.
    def visit_grSubAnim(self, off, _):
        count = u32(self.arc.data, off + 0x04)
        self.follow_count_array(off, 0x00, count, "AnimJoint")
        return 0x08

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

    # HSD_ShapeSet (HSDLib HSD_ShapeSet.cs, 0x1C): POBJ vertex-morph animation.
    # 0x08 VertexAttributes / 0x14 NormalAttributes are GX_Attribute descriptor
    # lists in the same on-disc form as a POBJ VtxDescList (0x18 stride, vertex
    # buffer pointer at +0x14, GX_VA_NULL terminator). 0x0C VertexIndices /
    # 0x18 NormalIndices are per-shape pointer tables, each entry an int16
    # index array; the table length is delimited by the reloc set (the stored
    # ShapeCount does not always match the on-disc table).
    def visit_ShapeSet(self, off, _):
        self.follow(off, 0x08, "VtxDescList")  # VertexAttributes
        self.follow_ptr_run(off, 0x0C, "shapeset_blob")  # VertexIndices tables
        self.follow(off, 0x14, "VtxDescList")  # NormalAttributes
        self.follow_ptr_run(off, 0x18, "shapeset_blob")  # NormalIndices tables
        return 0x1C

    def visit_shapeset_blob(self, off, hint):
        return hint

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

    # HSD_ParticleGroup (HSDLib HSD_ParticleGroup.cs): a 0x0C header, a
    # GeneratorCount-entry table of byte-offsets at 0x0C, then the generator
    # blocks those offsets delimit -- all embedded in this one allocation
    # (byte ranges, not relocated pointers). The generators run to the end
    # of the allocation, so the full size resolves via the next-reachable-
    # start heuristic rather than the header+table alone.
    def visit_ParticleGroup(self, off, _):
        return None

    # KAR stage-model roots (HSDLib AirRide/Gr/Model). KAR_grModel is the
    # top-level model descriptor: 0x00 -> MainModel, 0x04 -> SkyboxModel.
    # 0x08/0x0C exist but are unidentified in HSDLib and do not hold model
    # pointers, so they are left unfollowed rather than dereferenced.
    def visit_grModel(self, off, _):
        self.follow(off, 0x00, "MainModel")
        self.follow(off, 0x04, "SkyBoxModel")
        return 0x10

    # KAR_grMainModel (0x14): the main stage model -- a JOBJ RootNode,
    # jobj/dobj/pobj counts, and a ModelBounding. The bounding record owns
    # a view-region / bounding-box / index pointer web that is spatial
    # metadata rather than model geometry, so it is sized as a leaf and not
    # descended into (same boundary as the animation joints).
    def visit_MainModel(self, off, _):
        self.follow(off, 0x00, "JOBJDesc")  # RootNode
        self.follow(off, 0x10, "ModelBounding")
        return 0x14

    # KAR_grModelBounding (HSDLib KAR_GrModelBounding.cs, 0x20): the MainModel's
    # spatial-culling metadata -- four count+container pairs. Each container is
    # a single allocation of `count` inlined fixed-size records (an embedded
    # accessor array, no per-record relocs). ViewRegion (0x20) and
    # DynamicBoundingBox (0x24) records each carry an internal u16-index array
    # at +0x00 (entry count at record+0x04); StaticBoundingBox (0x18) records
    # are pure floats; the trailing Indices container is a raw u16 array.
    def visit_ModelBounding(self, off, _):
        self._follow_bound_container(off, 0x00, off + 0x04, 0x20, indexed=True)
        self._follow_bound_container(off, 0x08, off + 0x0C, 0x24, indexed=True)
        self._follow_bound_container(off, 0x10, off + 0x14, 0x18, indexed=False)
        n_idx = u16(self.arc.data, off + 0x1C)
        self._record_bound_indices(off, 0x18, n_idx * 2)
        return 0x20

    def _follow_bound_container(self, src, slot, count_off, stride, indexed):
        count = u16(self.arc.data, count_off)
        if (src + slot) not in self.arc.reloc_set:
            return
        cont = u32(self.arc.data, src + slot)
        if cont == 0 or count <= 0:
            return
        if indexed:
            for i in range(count):
                rec = cont + i * stride
                n = u16(self.arc.data, rec + 0x04)  # per-record index count
                self._record_bound_indices(rec, 0x00, n * 2)
        if cont not in self.visited:
            self.visited[cont] = (f"BoundContainer[{stride:#x}]", count * stride)

    def _record_bound_indices(self, src, slot, n_bytes):
        """Record a bounding u16-index array (a leaf) at src+slot, sized
        exactly from its count. A reloc'd slot always names a real offset --
        including 0, a genuine pointer to the start of data that the first
        view region uses -- so this records it directly rather than via
        follow(), whose null-guard would otherwise drop the offset-0 case."""
        if (src + slot) not in self.arc.reloc_set:
            return
        tgt = u32(self.arc.data, src + slot)
        if tgt not in self.visited:
            self.visited[tgt] = ("bounding_blob", n_bytes)

    # KAR_grSkyBoxModel (0x08): the skybox model -- a JOBJ root and a
    # KAR_grModelMotion, which pairs the model's joint-transform animation
    # (HSD_AnimJoint) with its material/texture animation (HSD_MatAnimJoint).
    def visit_SkyBoxModel(self, off, _):
        self.follow(off, 0x00, "JOBJDesc")  # JOBJRoot
        self.follow(off, 0x04, "ModelMotion")
        return 0x08

    def visit_ModelMotion(self, off, _):
        self.follow(off, 0x00, "AnimJoint")
        self.follow(off, 0x04, "MatAnimJoint")
        return 0x14

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
        # 0x04 -> NULL-terminated array of HSD_LightAnimPointer (color/
        # position/interest animation for this light).
        self.follow_array(off, 0x04, "LightAnimPointer")
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

    # HSD_Spline (HSDLib HSD_Spline.cs, 0x18): a curve reached via the JOBJ
    # SPLINE flag. Three reloc'd arrays hang off it -- Points (0x08, one
    # HSD_Vector3 per PointCount), Lengths (0x10, float per point), and
    # SegPolys (0x14, 0x14-byte records). Points is sized from PointCount;
    # the other two resolve via the next-reachable-start heuristic.
    def visit_Spline(self, off, _):
        n_points = u16(self.arc.data, off + 0x02)
        self.follow(off, 0x08, "spline_blob", n_points * 12)  # Points (Vec3[])
        self.follow(off, 0x10, "spline_blob")  # Lengths (float[])
        self.follow(off, 0x14, "spline_blob")  # SegPolys (0x14-byte records)
        return 0x18

    def visit_spline_blob(self, off, hint):
        return hint

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

    def visit_lobj_attn_blob(self, off, hint):
        return hint  # 0x0C/0x14 by LObj type
