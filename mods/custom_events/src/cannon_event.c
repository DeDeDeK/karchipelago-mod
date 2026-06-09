// Cannon event — WIP investigation toward spawning a working cannon yakumono
// in City Trial. See cannon_event.h for the high-level approach.

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "obj.h"
#include "inline.h"
#include "stage.h"
#include "yakumono.h"

#include "cannon_event.h"

#ifndef CANNON_SPAWN_ENABLED
#define CANNON_SPAWN_ENABLED 1
#endif

// Disabled by default. Loading GrMachine2Model.dat (1.6MB) in CT exhausts
// heap 1 — the JObj instantiation needs ~1KB and only ~30 bytes were left
// after the load. CT's memory budget can't accommodate the full model
// archive.
#ifndef CANNON_LOAD_ENABLED
#define CANNON_LOAD_ENABLED 0
#endif

#define CANNON_DESC_ID 48

// CT path: hijack a spare data_array slot for a zeroed-param ghost spawn.
// CT's data_count is 33 with slots 31, 32 unused.
#define CT_HIJACK_DATA_IDX 31

// Machine Passage path: data_array[1] is the live cannon param block populated
// by GrMachine2.dat. We spawn a *second* cannon using the same slot — both
// yakumono will share the param read-only and have separate ydata, giving us a
// clean ydata to dump alongside the param.
#define MP_CANNON_DATA_IDX 1
// Gm_GetCurrentGrKind reads r13[0x7f8] which actually stores StageKind /
// AirRideCourse (Machine Passage = 6), NOT the per-grkind hook table index
// (which is 5 for MP). The doc's "GroundKind = 5" refers to the internal
// dispatch table, not this runtime accessor. CT happens to be 9 in both.
#define MACHINE_PASSAGE_GRKIND 6

static u8 s_zero_cannon_param[256] __attribute__((aligned(4))) = {0};

// Cache loaded archives — Archive_LoadFile is expensive and we only need to
// do it once per session. Order matters: model must load first so the
// stage archive's extern (the cannon animjoint) resolves automatically when
// HSD_ArchiveParse walks the global archive list.
static HSD_Archive *s_grmachine2_model_archive = NULL;
static HSD_Archive *s_grmachine2_archive = NULL;

static int ReadCount(void)
{
    int *p = Yaku_GetCountPtr();
    return p ? *p : -1;
}

// Tag a 4-byte value with a hint about what it likely is, to make the dump
// readable at a glance:
//   "ROM"  = pointer into 0x80000000..0x80600000 (game code/data)
//   "HEAP" = pointer into 0x80800000..0x81800000 (HSD heap, MEM1 dynamic)
//   "f"    = looks like a small float (1.0, 0.0, common transform values)
//   "0"    = exact zero
//   "?"    = anything else (small ints, packed flags, etc.)
static const char *Tag(u32 v)
{
    if (v == 0)
        return "0";
    if (v >= 0x80000000 && v < 0x80600000)
        return "ROM";
    if (v >= 0x80800000 && v < 0x81800000)
        return "HEAP";
    int exp = (v >> 23) & 0xff;
    if (exp >= 0x3e && exp <= 0x42 && (v & 0x80000000) == 0)
        return "f+";
    if (exp >= 0x3e && exp <= 0x42 && (v & 0x80000000))
        return "f-";
    return "?";
}

static int IsLikelyPointer(u32 v)
{
    return (v >= 0x80000000 && v < 0x80600000) ||
           (v >= 0x80800000 && v < 0x81800000);
}

// Dump `nbytes` of memory at `addr`, in 16-byte rows (4 words per row) with tags.
static void DumpRange(const char *prefix, const void *addr, int nbytes)
{
    const u32 *raw = (const u32 *)addr;
    for (int off = 0; off < nbytes; off += 16)
    {
        u32 a = raw[(off + 0) / 4];
        u32 b = raw[(off + 4) / 4];
        u32 c = raw[(off + 8) / 4];
        u32 d = raw[(off + 12) / 4];
        OSReport("%s +0x%03x: %08x[%s] %08x[%s] %08x[%s] %08x[%s]\n",
                 prefix, off, a, Tag(a), b, Tag(b), c, Tag(c), d, Tag(d));
    }
}

// Dump a param block, then for each pointer-looking word in the first
// `nbytes_chase` bytes, dump 32 bytes at the target.
static void DumpParamWithChase(const char *tag, const void *param,
                               int nbytes_dump, int nbytes_chase)
{
    OSReport("[CannonEvent] === %s param @ %p ===\n", tag, param);
    DumpRange("[CannonEvent]", param, nbytes_dump);

    const u32 *raw = (const u32 *)param;
    int nwords = nbytes_chase / 4;
    for (int i = 0; i < nwords; i++)
    {
        u32 v = raw[i];
        if (IsLikelyPointer(v))
        {
            OSReport("[CannonEvent] -- chase param+0x%02x = %08x --\n", i * 4, v);
            DumpRange("[CannonEvent]   ", (const void *)v, 32);
        }
    }
}

static void DumpYData(const char *tag, YakumonoData *yd)
{
    OSReport("[CannonEvent] === %s ydata @ %p (full 0x180) ===\n", tag, yd);
    DumpRange("[CannonEvent]", yd, 0x180);
}

// City Trial: hijack a spare slot, point it at a zeroed param, spawn the
// cannon, restore the slot. Confirms framework plumbing is stage-agnostic;
// resulting ydata is the "ghost" baseline (no JObj, no model, no audio).
static void DumpZeroParamCT(void)
{
    GrObj *grobj = *stc_grobj;
    if (!grobj || !grobj->gr_data || !grobj->gr_data->yakumono)
    {
        OSReport("[CannonEvent] spawn-CT: no grobj/grdata/yakumono — skipping\n");
        return;
    }

    YakumonoTable *table = grobj->gr_data->yakumono;
    if (CT_HIJACK_DATA_IDX >= table->data_count)
    {
        OSReport("[CannonEvent] spawn-CT: slot %d out of range (data_count=%d)\n",
                 CT_HIJACK_DATA_IDX, table->data_count);
        return;
    }

    int count_before = ReadCount();
    void *original_param = table->data_array[CT_HIJACK_DATA_IDX];
    table->data_array[CT_HIJACK_DATA_IDX] = s_zero_cannon_param;

    GOBJ *yaku_gobj = GrYaku_Create(CANNON_DESC_ID, CT_HIJACK_DATA_IDX);
    if (!yaku_gobj)
    {
        table->data_array[CT_HIJACK_DATA_IDX] = original_param;
        OSReport("[CannonEvent] spawn-CT: GrYaku_Create returned NULL\n");
        return;
    }
    GrYakuCannon_TailInit(yaku_gobj);
    table->data_array[CT_HIJACK_DATA_IDX] = original_param;

    int count_after = ReadCount();
    YakumonoData *yd = Yaku_GetData(yaku_gobj);
    OSReport("[CannonEvent] spawn-CT(zeroed): count %d→%d gobj=%p yd=%p\n",
             count_before, count_after, yaku_gobj, yd);
    if (yd)
        DumpYData("CT-zeroed", yd);
}

// Machine Passage: data_array[1] is already populated by vanilla. Dump the
// real param block (with pointer chasing), then spawn a second cannon from
// the same slot — ydata is fresh and isolated, the param is shared read-only.
static void DumpHealthyMP(void)
{
    GrObj *grobj = *stc_grobj;
    if (!grobj || !grobj->gr_data || !grobj->gr_data->yakumono)
    {
        OSReport("[CannonEvent] spawn-MP: no grobj/grdata/yakumono — skipping\n");
        return;
    }

    YakumonoTable *table = grobj->gr_data->yakumono;
    OSReport("[CannonEvent] spawn-MP: data_count=%d entry_count=%d\n",
             table->data_count, table->entry_count);

    if (MP_CANNON_DATA_IDX >= table->data_count)
    {
        OSReport("[CannonEvent] spawn-MP: cannon slot %d out of range (data_count=%d)\n",
                 MP_CANNON_DATA_IDX, table->data_count);
        return;
    }

    void *cannon_param = table->data_array[MP_CANNON_DATA_IDX];
    if (!cannon_param)
    {
        OSReport("[CannonEvent] spawn-MP: data_array[%d] is NULL\n", MP_CANNON_DATA_IDX);
        return;
    }

    DumpParamWithChase("MP-real", cannon_param, /*nbytes_dump=*/0x80, /*nbytes_chase=*/0x40);

    // Walk the GAMEPLINK_YAKUMONO=8 GObj list and dump every yakumono whose
    // desc_id (ydata+0x04) == 48. This finds the *vanilla* cannons spawned by
    // grInitYakumono, before our duplicate spawn, so we can compare a real
    // working cannon's ydata against ours.
    GOBJ *head = (*stc_gobj_lookup)[8];
    int vidx = 0;
    for (GOBJ *g = head; g; g = g->next)
    {
        YakumonoData *vyd = Yaku_GetData(g);
        if (!vyd)
            continue;
        if (vyd->desc_id != CANNON_DESC_ID)
            continue;
        OSReport("[CannonEvent] spawn-MP: vanilla cannon #%d gobj=%p yd=%p\n", vidx++, g, vyd);
        DumpYData("MP-vanilla", vyd);
    }

    int count_before = ReadCount();
    GOBJ *yaku_gobj = GrYaku_Create(CANNON_DESC_ID, MP_CANNON_DATA_IDX);
    if (!yaku_gobj)
    {
        OSReport("[CannonEvent] spawn-MP: GrYaku_Create returned NULL\n");
        return;
    }
    GrYakuCannon_TailInit(yaku_gobj);

    int count_after = ReadCount();
    YakumonoData *yd = Yaku_GetData(yaku_gobj);
    OSReport("[CannonEvent] spawn-MP(real): count %d→%d gobj=%p yd=%p\n",
             count_before, count_after, yaku_gobj, yd);
    if (yd)
        DumpYData("MP-real", yd);
}

static HSD_Archive *LoadArchive(const char *name, HSD_Archive **cache)
{
    if (*cache)
        return *cache;

    *cache = Archive_LoadFile((char *)name);
    if (!*cache)
    {
        OSReport("[CannonEvent] load: Archive_LoadFile(%s) returned NULL\n", name);
        return NULL;
    }

    OSReport("[CannonEvent] load: %s @ %p  (file_size=0x%x data=%p nb_public=%d nb_extern=%d)\n",
             name, *cache,
             (*cache)->header.file_size, (*cache)->data,
             (*cache)->header.nb_public, (*cache)->header.nb_extern);

    return *cache;
}

static void DumpPublicSymbols(const char *tag, HSD_Archive *arch, int max)
{
    int n = arch->header.nb_public;
    int shown = (n < max) ? n : max;
    OSReport("[CannonEvent] load: %s: %d public symbols (showing %d):\n", tag, n, shown);
    for (int i = 0; i < shown; i++)
    {
        HSD_ArchivePublicInfo *info = &arch->public_info[i];
        char *name = arch->symbols + info->symbol;
        void *data = arch->data + info->offset;
        OSReport("[CannonEvent] load:   [%d] +%08x → %p  %s\n",
                 i, info->offset, data, name);
    }
    if (shown < n)
        OSReport("[CannonEvent] load:   ... %d more truncated\n", n - shown);
}

static void DumpExternSymbols(const char *tag, HSD_Archive *arch)
{
    int n = arch->header.nb_extern;
    OSReport("[CannonEvent] load: %s: %d extern symbols:\n", tag, n);
    for (int i = 0; i < n; i++)
    {
        HSD_ArchiveExternInfo *info = &arch->extern_info[i];
        char *name = arch->symbols + info->symbol;
        // After parse, the extern's slot inside data should be patched to
        // the resolved address. Read it to check if resolution succeeded.
        u32 *slot = (u32 *)(arch->data + info->offset);
        OSReport("[CannonEvent] load:   [%d] +%08x slot=%p  *slot=%08x  %s\n",
                 i, info->offset, slot, *slot, name);
    }
}

static void CrossLoadCT(void)
{
    if (!Gm_IsInCity())
        return;

    // Load model archive FIRST so its public symbols are in the global list
    // when we parse the stage archive's externs.
    HSD_Archive *model = LoadArchive("GrMachine2Model.dat", &s_grmachine2_model_archive);
    if (!model)
        return;
    DumpPublicSymbols("Model.dat", model, 30);
    DumpExternSymbols("Model.dat", model);

    HSD_Archive *stage = LoadArchive("GrMachine2.dat", &s_grmachine2_archive);
    if (!stage)
        return;
    DumpPublicSymbols("Stage.dat", stage, 10);
    DumpExternSymbols("Stage.dat", stage);

    // Resolve grDataMachine2 by name and inspect its yakumono table.
    GrData *grdata = (GrData *)Archive_GetPublicAddress(stage, "grDataMachine2");
    if (!grdata)
    {
        OSReport("[CannonEvent] load: grDataMachine2 not found\n");
        return;
    }
    OSReport("[CannonEvent] load: grDataMachine2 = %p\n", grdata);
    OSReport("[CannonEvent] load:   flags=0x%x  stage_node=%p  model_section=%p  motion=%p  spline=%p  pos_data=%p  yakumono=%p\n",
             grdata->flags, grdata->stage_node, grdata->model_section, grdata->motion,
             grdata->spline, grdata->pos_data, grdata->yakumono);

    if (!grdata->yakumono)
    {
        OSReport("[CannonEvent] load: yakumono table is NULL — skipping cannon param dump\n");
        return;
    }
    YakumonoTable *yt = grdata->yakumono;
    OSReport("[CannonEvent] load: yakumono table: data_array=%p data_count=%d entries=%p entry_count=%d\n",
             yt->data_array, yt->data_count, yt->entries, yt->entry_count);

    if (yt->data_count > 1 && yt->data_array && yt->data_array[1])
    {
        void *cannon_param = yt->data_array[1];
        OSReport("[CannonEvent] load: cross-loaded cannon param @ %p (compare to MP runtime 0x80aa2d98):\n",
                 cannon_param);
        DumpRange("[CannonEvent]", cannon_param, 0x80);
    }

    // Inspect the model archive's two public symbols to identify their
    // struct types. grModelMachine2 is most likely a JObjSet or HSD_SObjDesc;
    // dumping the first 0x40 bytes lets us pattern-match against known layouts.
    void *grmodel = Archive_GetPublicAddress(model, "grModelMachine2");
    void *grmotion = Archive_GetPublicAddress(model, "grModelMotionMachine2");
    OSReport("[CannonEvent] load: grModelMachine2 = %p, grModelMotionMachine2 = %p\n",
             grmodel, grmotion);

    if (grmodel)
    {
        OSReport("[CannonEvent] load: === grModelMachine2 first 0x40 bytes ===\n");
        DumpRange("[CannonEvent]", grmodel, 0x40);

        // The first 3 pointers in grModelMachine2 are very close to grModelMachine2
        // itself (8-36 bytes earlier). That's the HSD_SObjDesc indirect-pointer
        // pattern: each field is a pointer to a NULL-terminated array of pointers
        // to the actual data. Dump the first ~16 bytes at each indirection target
        // to see what kind of pointer-array we get.
        u32 *raw = (u32 *)grmodel;
        for (int i = 0; i < 6; i++)
        {
            u32 v = raw[i];
            if (v >= 0x80000000 && v < 0x81800000)
            {
                OSReport("[CannonEvent] load:   grModel+0x%02x = %08x → contents:\n", i * 4, v);
                DumpRange("[CannonEvent]    ", (const void *)v, 16);
                // For HSD_SObjDesc, each field is JObjSet** (etc) — the contents
                // we just dumped should be a NULL-terminated pointer array. Chase
                // the first entry to see if it points at a real JObjSet.
                u32 inner = *(u32 *)v;
                if (inner >= 0x80000000 && inner < 0x81800000)
                {
                    OSReport("[CannonEvent] load:     **chase = %08x → first 0x20:\n", inner);
                    DumpRange("[CannonEvent]      ", (const void *)inner, 0x20);
                }
            }
        }
    }
    if (grmotion)
    {
        OSReport("[CannonEvent] load: === grModelMotionMachine2 first 0x40 bytes ===\n");
        DumpRange("[CannonEvent]", grmotion, 0x40);
    }

    // Also dump grdata->stage_node — this is what GrData+0x4 ("stage_node")
    // points at; might be a top-level scene-graph node or a struct that
    // bridges grdata and grmodel.
    if (grdata->stage_node)
    {
        OSReport("[CannonEvent] load: === grdata->stage_node @ %p first 0x40 bytes ===\n",
                 grdata->stage_node);
        DumpRange("[CannonEvent]", grdata->stage_node, 0x40);
    }
}

void CannonEvent_On3DLoadEnd(void)
{
#if CANNON_SPAWN_ENABLED
    {
        GroundKind grkind = Gm_GetCurrentGrKind();
        if (Gm_IsInCity())
            DumpZeroParamCT();
        else if (grkind == MACHINE_PASSAGE_GRKIND)
            DumpHealthyMP();
    }
#endif

#if CANNON_LOAD_ENABLED
    CrossLoadCT();
#endif
}

void CannonEvent_TryRender(int set_index)
{
#if !CANNON_LOAD_ENABLED
    (void)set_index;
    return;
#else
    if (!s_grmachine2_model_archive || !s_grmachine2_archive)
    {
        OSReport("[CannonEvent] render: archives not loaded yet\n");
        return;
    }

    JOBJSet **grmodel = (JOBJSet **)Archive_GetPublicAddress(
        s_grmachine2_model_archive, "grModelMachine2");
    if (!grmodel)
    {
        OSReport("[CannonEvent] render: grModelMachine2 not found\n");
        return;
    }

    JOBJSet *set = grmodel[set_index];
    if (!set)
    {
        OSReport("[CannonEvent] render: grmodel[%d] is NULL\n", set_index);
        return;
    }

    OSReport("[CannonEvent] render: trying grmodel[%d] = %p, set->jobj = %p\n",
             set_index, set, set->jobj);

    // Pass is_add_anim=0 so the inline doesn't dereference set->animjoint —
    // those fields are small ints (counts) in this struct, not real pointers.
    GOBJ *g = JObj_LoadSet_SetPri(
        /*is_hidden=*/0, set, /*anim_id=*/0, /*frame=*/0.0f,
        /*p_link=*/14 /* GAMEPLINK_USER */, /*gx_link=*/0,
        /*is_add_anim=*/0, /*cb=*/NULL, /*pri=*/0);

    if (!g)
    {
        OSReport("[CannonEvent] render: JObj_LoadSet_SetPri returned NULL\n");
        return;
    }
    OSReport("[CannonEvent] render: spawned GOBJ=%p hsd_object(JOBJ)=%p\n",
             g, g->hsd_object);
#endif
}
