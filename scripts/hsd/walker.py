# SPDX-License-Identifier: GPL-3.0-only
# Contains code derived from HSDLib (https://github.com/Ploaj/HSDLib),
# Copyright (c) 2021 Ploaj, used under the MIT License.
# See THIRD_PARTY_NOTICES.md at the repo root for the full MIT license text.
"""Type-aware reachability walker over HSD trees.

Starts from a root offset of a known type (typically JOBJDesc) and
follows every reloc'd pointer field, recording each reached offset with
its type and computed size. The pointer fields and struct sizes come
from `schema.SCHEMA`; a `visit_*` method here adds whatever that type
needs beyond a plain field list - a size read out of the record, an
inline array, an embedded record container.

Sizing strategy:
- Types in SCHEMA use their declared size.
- ImageDesc-pointed image data is sized from width x height x bpp,
  rounded up to the GX format's tile padding.
- TlutDesc-pointed palette data is sized from n_entries x 2.
- Display lists, vertex arrays, VtxDescList terminator scans, Spline
  length/segment arrays, and ParticleGroup embedded generators use a
  neighbor-offset heuristic (extend until the next reachable start).
  These are small enough that the slop is negligible.

Blob pseudo-types (`image_blob`, `anim_buffer`, `dl_blob`, ...) carry no
schema entry: their size is whatever the caller passed as a hint, or the
neighbor heuristic when no hint is known.

Animation is walked in full: HSD_AnimJoint, HSD_MatAnimJoint (-> MatAnim
-> TexAnim), HSD_ShapeAnimJoint, and the ROBJAnimJoint / WOBJAnim /
LightAnimPointer chains, reached from ModelGroup's three anim arrays,
KAR_grModelMotion, HSD_Light+0x04, and the KAR_grSubAnimNode slots hung
off grData. Every branch bottoms out at an AOBJ (FOBJDesc keyframe
buffers sized from dataLength) or, for texture animation, at the
ImageDesc / TlutDesc frame buffers. HSD_FigaTree keyframe containers are
walked too; they appear as standalone anim publics rather than hanging
off a model tree. The FOBJ/FOBJDesc track-type byte is context-dependent
(Fog/Joint/Mat/Tex/Light/Shape) and is left uninterpreted here.

KAR stage-model roots (KAR_grModel) descend into the MainModel and
SkyboxModel geometry trees, the SkyboxModel's ModelMotion animation
joints, and the MainModel's ModelBounding spatial-culling metadata.

Use `Walker(arc).walk(root_off)` to get an `OrderedDict[off] = (type, size)`.
`merge_intervals` collapses adjacent (start, end) ranges and `carve_ranges`
concatenates the reachable bytes into a new data section (with the reloc
table rebuilt), the two steps a minimal carved archive needs.
"""

import struct
from collections import OrderedDict, namedtuple

from .archive import Archive, u16, u32
from .gx import image_size
from .schema import SCHEMA, array_length, resolved_fields


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

    def walk(self, root, root_type="JOBJDesc"):
        self.work.append((root, root_type, None))
        while self.work:
            off, typ, hint = self.work.pop(0)
            if off == 0 or off in self.visited or off >= len(self.arc.data):
                continue
            spec = SCHEMA.get(typ)
            size = spec.size if spec else hint
            if spec:
                self._follow_fields(off, typ)
            handler = getattr(self, f"visit_{typ}", None)
            if handler is not None:
                computed = handler(off)
                if computed is not None:
                    size = computed
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

    def _follow_fields(self, off, typ):
        for fd in resolved_fields(self.arc, typ, off):
            if fd.kind == "array":
                self.follow_array(off, fd.off, fd.type)
            elif fd.kind == "run":
                self.follow_ptr_run(off, fd.off, fd.type)
            elif fd.kind in ("count", "records", "buffer"):
                count = (u16 if fd.cnt_w == 2 else u32)(self.arc.data, off + fd.cnt_off)
                if fd.kind == "count":
                    self.follow_count_array(off, fd.off, count, fd.type, fd.stride)
                elif fd.kind == "records":
                    self.follow_records(off, fd.off, count, fd.type)
                else:
                    self.record_buffer(off, fd.off, count * fd.stride, fd.type)
            else:
                self.follow(off, fd.off, fd.type)

    def follow(self, src, slot, target_type, size_hint=None):
        if (src + slot) not in self.arc.reloc_set:
            return
        tgt = u32(self.arc.data, src + slot)
        if tgt == 0:
            return
        self.work.append((tgt, target_type, size_hint))

    def follow_array(self, src, slot, elem_type):
        """Follow a NULL-terminated pointer array (HSDNullPointerArrayAccessor).
        The slot at src+slot points to a sequence of u32 pointers, each
        followed as `elem_type`."""
        if (src + slot) not in self.arc.reloc_set:
            return
        arr = u32(self.arc.data, src + slot)
        if arr == 0:
            return
        n = array_length(self.arc, arr)
        for i in range(n):
            self.follow(arr + i * 4, 0x00, elem_type)
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

    def follow_records(self, src, slot, count, elem_type):
        """Walk `count` fixed-size records of `elem_type` packed back to back
        in one allocation (HSDLib's embedded-accessor arrays: collision joints,
        range splines, ...). The records tile the buffer exactly, so no
        separate container entry is recorded."""
        if (src + slot) not in self.arc.reloc_set:
            return
        base = u32(self.arc.data, src + slot)
        stride = SCHEMA[elem_type].size
        for i in range(max(0, count)):
            rec = base + i * stride
            if rec + stride > len(self.arc.data):
                break
            self.work.append((rec, elem_type, None))

    def record_buffer(self, src, slot, n_bytes, name):
        """Record a raw blob of known length at src+slot. Unlike follow(),
        this keeps a relocated slot whose value is 0 - a genuine pointer to
        data offset 0, which stage vertex buffers and view-region index
        arrays do use."""
        if (src + slot) not in self.arc.reloc_set:
            return
        tgt = u32(self.arc.data, src + slot)
        if tgt not in self.visited:
            self.visited[tgt] = (name, max(0, n_bytes))

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

    def visit_ImageDesc(self, off):
        w = u16(self.arc.data, off + 4)
        h = u16(self.arc.data, off + 6)
        fmt = u32(self.arc.data, off + 8)
        mip = u32(self.arc.data, off + 0xC) != 0
        self.follow(off, 0x00, "image_blob", image_size(w, h, fmt, mip))

    def visit_TlutDesc(self, off):
        n = u16(self.arc.data, off + 0x0C)
        self.follow(off, 0x00, "palette_blob", n * 2)

    # HSD_IOBJ (HSDLib HSD_IOBJ.cs): a standalone image object, essentially
    # an ImageDesc without the wrapping TObj.
    def visit_IOBJDesc(self, off):
        w = u16(self.arc.data, off + 0x00)
        h = u16(self.arc.data, off + 0x02)
        fmt = u32(self.arc.data, off + 0x04)
        self.follow(off, 0x08, "image_blob", image_size(w, h, fmt))

    def visit_VtxDescList(self, off):
        # 0x18-byte entries terminated by attr=0xFF; follow each entry's
        # vertex pointer (entry+0x14).
        cur = off
        while cur + 0x18 <= len(self.arc.data):
            if u32(self.arc.data, cur) == 0xFF:
                break
            self.follow(cur, 0x14, "vertex_blob")
            cur += 0x18
        return cur + 0x18 - off

    # HSD_FOBJDesc keyframe data at +0x10, sized from the +0x04 dataLength
    # (raw byte count of the packed bit-stream).
    def visit_FOBJDesc(self, off):
        self.follow(off, 0x10, "anim_buffer", u32(self.arc.data, off + 0x04))

    # HSD_FigaTree (HSDLib HSD_FigaTree.cs): a standalone keyframe container
    # (joint-animation "AJ" storage; KAR names these publics `*_cmpatree`).
    # 0x0C -> a byte count table (one track-count per node, 0xFF terminator)
    # and 0x10 -> a blob of TrackCount embedded HSD_Track records (0x0C each).
    # Each track's keyframe buffer hangs off track+0x08, sized by the
    # DataLength u16 at track+0x00.
    def visit_FigaTree(self, off):
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
                self.follow(tk, 0x08, "anim_buffer", u16(self.arc.data, tk + 0x00))
            self.follow(off, 0x10, "figatree_blob", track_count * 0x0C)

    # HSD_Envelope (HSDLib HSD_Envelope.cs): variable-length array of 8-byte
    # entries (JOBJ* + float weight), ending at the first non-reloc'd slot.
    # Size resolves via the next-reachable-start heuristic.
    def visit_Envelope(self, off):
        i = 0
        while off + i * 8 + 8 <= len(self.arc.data):
            entry = off + i * 8
            if entry not in self.arc.reloc_set:
                break
            self.follow(entry, 0x00, "JOBJDesc")
            i += 1

    # HSD_ShapeSet (HSDLib HSD_ShapeSet.cs): POBJ vertex-morph animation.
    # 0x08 VertexAttributes / 0x14 NormalAttributes are GX_Attribute
    # descriptor lists in the same on-disc form as a POBJ VtxDescList.
    # 0x0C VertexIndices / 0x18 NormalIndices are per-shape pointer tables,
    # each entry an int16 index array; the table length is delimited by the
    # reloc set (the stored ShapeCount does not always match the table).
    def visit_ShapeSet(self, off):
        self.follow(off, 0x08, "VtxDescList")
        self.follow_ptr_run(off, 0x0C, "shapeset_blob")
        self.follow(off, 0x14, "VtxDescList")
        self.follow_ptr_run(off, 0x18, "shapeset_blob")

    # HSD_Spline (HSDLib HSD_Spline.cs): three reloc'd arrays - Points
    # (0x08, one HSD_Vector3 per PointCount), Lengths (0x10, float per
    # point) and SegPolys (0x14, 0x14-byte records). Points is sized from
    # PointCount; the other two resolve via next-reachable-start.
    def visit_Spline(self, off):
        n_points = u16(self.arc.data, off + 0x02)
        self.follow(off, 0x08, "spline_blob", n_points * 12)
        self.follow(off, 0x10, "spline_blob")
        self.follow(off, 0x14, "spline_blob")

    # KAR_grModelBounding (HSDLib KAR_GrModelBounding.cs): the MainModel's
    # spatial-culling metadata - four count+container pairs. Each container
    # is a single allocation of `count` inlined fixed-size records (an
    # embedded accessor array, no per-record relocs). ViewRegion (0x20) and
    # DynamicBoundingBox (0x24) records each carry an internal u16-index
    # array at +0x00 (entry count at record+0x04); StaticBoundingBox (0x18)
    # records are pure floats; the trailing Indices container is a raw u16
    # array.
    def visit_ModelBounding(self, off):
        self._follow_bound_container(off, 0x00, off + 0x04, 0x20, indexed=True)
        self._follow_bound_container(off, 0x08, off + 0x0C, 0x24, indexed=True)
        self._follow_bound_container(off, 0x10, off + 0x14, 0x18, indexed=False)
        self.record_buffer(off, 0x18, u16(self.arc.data, off + 0x1C) * 2, "bounding_blob")

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
                self.record_buffer(rec, 0x00, n * 2, "bounding_blob")
        if cont not in self.visited:
            self.visited[cont] = (f"BoundContainer[{stride:#x}]", count * stride)

    # KAR_grCollisionTree +0x54: one bit per collidable triangle, count at
    # +0x58 (HSDLib writes ceil(count / 8) bytes).
    def visit_grCollisionTree(self, off):
        n_bits = u16(self.arc.data, off + 0x58)
        self.record_buffer(off, 0x54, (n_bits + 7) // 8, "bit_table")

    def visit_cstring(self, off):
        end = self.arc.data.find(b"\0", off)
        return (end - off + 1) if end >= 0 else 1
