#include <string.h>

#include "game.h"
#include "os.h"
#include "scene.h"
#include "text.h"
#include "code_patch/code_patch.h"
#include "hoshi/func.h"
#include "hoshi/mod.h"
#include "hsd.h"

#include "custom_checklist_api.h"

// Defined at the bottom; forward-declared for the save callbacks.
extern ModDesc mod_desc;

// Bounds the registry, the save slots, and the tab ring.
#define CC_MAX_CHECKLISTS 16

// Union with the real modes' blocks so a read into the "records tail" they carry
// past GameClearData stays in-bounds; engine code doesn't know a tab has none.
typedef union CCClearStorage
{
    GameClearData clear;
    AirRideClearData airride;
    TopRideClearData topride;
    CityTrialClearData city;
} CCClearStorage;

typedef struct CCList
{
    CustomChecklistDesc desc;   // copied at Register (pointers must stay valid)
    CCClearStorage clear_storage;
    u64 revealed[2];            // clear_kinds whose neighbours have already been revealed
    int minor_id;               // installed minor-scene id (-1 if install failed)
    int mode;                   // GMMODE_NUM + registry index
    int fw_persist;             // 1 if the framework owns this tab's recorded state
    u32 name_hash;              // stable tab identity: save key (fw_persist) and grid-layout key (all tabs)
    int save_slot;              // resolved CCSave slot, -1 until first access
    int layout_done;            // 1 once the saved grid layout has been applied this session
    int reveal_all;             // 1 once RevealAll latched the tab open for the session
} CCList;

#define CC_BIT_TEST(w, k) (((w)[(k) >> 6] >> ((k) & 63)) & 1ULL)
#define CC_BIT_SET(w, k)  ((w)[(k) >> 6] |= 1ULL << ((k) & 63))

static CCList g_lists[CC_MAX_CHECKLISTS];
static int g_count = 0;

#define CC_CLEAR(i) (&g_lists[i].clear_storage.clear)

// Persistence for tabs that leave is_recorded/record_complete NULL, keyed by
// tab-name hash so bits survive tabs being added, removed, or reordered.
typedef struct CCSave
{
    struct
    {
        u32 name_hash;     // 0 = empty slot
        u64 recorded[2];   // completed clear_kinds (2 u64 words cover 0..119)
    } slots[CC_MAX_CHECKLISTS];

    // Each tab mixes this with its name hash for its own permutation, so tabs that
    // claim no slot get a layout too. 0 = not yet generated.
    u32 layout_seed;
} CCSave;

static CCSave *g_save = NULL;

// FNV-1a hash of a tab name; never returns 0 (0 marks an empty save slot).
static u32 CC_HashName(const char *s)
{
    u32 h = 2166136261u;
    for (; s && *s; s++)
    {
        h ^= (u8)*s;
        h *= 16777619u;
    }
    return h ? h : 1u;
}

// Tab being built by CC_MinorLoad (-1 otherwise). While set, the clear-data accessor
// redirects CITYTRIAL to that tab's block so Checklist_Init populates from it.
static int g_build_active = -1;

// Raised while the checklist session was entered from a run, not menu navigation.
static int g_postrun = 0;

static int CC_FindListByMinor(int minor)
{
    for (int i = 0; i < g_count; i++)
        if (g_lists[i].minor_id == minor)
            return i;
    return -1;
}

// True if any cell is completed (is_new) but not yet shown (is_unlocked).
static int ClearData_HasPendingUnlock(const GameClearData *cd)
{
    if (!cd)
        return 0;
    for (int k = 0; k < CC_CLEAR_KIND_NUM; k++)
        if (cd->clear[k].is_new && !cd->clear[k].is_unlocked)
            return 1;
    return 0;
}

static int CC_ListHasPendingUnlock(int idx)
{
    return ClearData_HasPendingUnlock(CC_CLEAR(idx));
}

// First registered tab with a pending unlock (optionally excluding one minor), or -1.
static int CC_FirstPendingExcluding(int exclude_minor)
{
    for (int i = 0; i < g_count; i++)
        if (g_lists[i].minor_id != exclude_minor && CC_ListHasPendingUnlock(i))
            return i;
    return -1;
}

static int CC_FirstPending(void)
{
    return CC_FirstPendingExcluding(-1);
}

// REPLACEFUNC for gmGetClearcheckerTypeP (0x800076a0). Unknown modes return NULL
// instead of tripping vanilla's mode >= 3 assert.
static GameClearData *CC_GetClearcheckerTypeP(GameMode mode)
{
    GameData *gd = Gm_GetGameData();
    switch (mode)
    {
    case GMMODE_AIRRIDE:   return &gd->airride_clear.clear;
    case GMMODE_TOPRIDE:   return &gd->topride_clear.clear;
    case GMMODE_CITYTRIAL: return g_build_active >= 0 ? CC_CLEAR(g_build_active)
                                                      : &gd->city_clear.clear;
    default:
    {
        int idx = (int)mode - GMMODE_NUM;
        if (idx >= 0 && idx < g_count)
            return CC_CLEAR(idx);
        return NULL;
    }
    }
}

// Vanilla per-mode reward counts; custom tabs host no native rewards.
#define CC_REWARD_COUNT_AIRRIDE   46
#define CC_REWARD_COUNT_TOPRIDE   33
#define CC_REWARD_COUNT_CITYTRIAL 44

// REPLACEFUNC for Checklist_GetRewardNum (0x80049c20): 0 for custom tabs gates the
// reward loops off and dodges the vanilla mode>=3 assert.
static u8 CC_GetRewardNum(GameMode mode)
{
    static const u8 counts[GMMODE_NUM] = {
        CC_REWARD_COUNT_AIRRIDE, CC_REWARD_COUNT_TOPRIDE, CC_REWARD_COUNT_CITYTRIAL,
    };
    return mode < GMMODE_NUM ? counts[mode] : 0;
}

// REPLACEFUNC for Checklist_GetClearKindFromRewardIndex (0x80049c84): 0 for custom tabs
// keeps Checklist_ProcessUnlock's new-unlock scan inert so the cell animation can run.
static u8 CC_GetClearKindFromRewardIndex(GameMode mode, u8 reward_index)
{
    if ((unsigned)mode >= GMMODE_NUM)
        return 0;
    return stc_reward_table_ptrs[mode][reward_index].clear_kind;
}

// Mirrors Gm_GetClearChecker (0x8017cf14). NULL before the grid GObj exists.
static ClearCheckerUI *CC_GetUI(void)
{
    GOBJ *root = Gm_GetMenuData()->clearchecker.bg_gobj;
    return root ? (ClearCheckerUI *)root->userdata : NULL;
}

// Cell objective text comes from stc_sis_data[0][clear_kind + 4]. Only one custom
// tab is on screen at a time, so these buffers are shared.

#define CC_SIS_HEADER_NUM 4                       // entries 0..3 are CT's title/legend
#define CC_SIS_PTR_NUM (CC_CLEAR_KIND_NUM + 4)    // covers index clear_kind + 4
// Wider than the 128 bytes every vanilla objective entry fits in: a custom tab's labels
// restate an Archipelago location name, and the longest of those spend ~130.
#define CC_SIS_LABEL_MAX 160

static void *g_sis_ptrs[CC_SIS_PTR_NUM];
static u8 g_sis_blank[24];
static u8 g_sis_label[CC_CLEAR_KIND_NUM][CC_SIS_LABEL_MAX];

// A longer label wraps onto a second line. The cell box holds exactly two lines and
// the engine squeezes an over-wide line rather than breaking it, so breaks are authored.
#define CC_SIS_WRAP 30

// Index of the space to turn into the line break, or -1 when the label fits on one
// line or carries its own '\n'. Otherwise the space nearest the middle.
static int CC_WrapIndex(const char *str)
{
    int len = 0;
    while (str[len])
    {
        if (str[len] == '\n')
            return -1;
        len++;
    }
    if (len <= CC_SIS_WRAP)
        return -1;

    int mid = len / 2;
    int best = -1;
    for (int i = 0; i < len; i++)
    {
        if (str[i] != ' ')
            continue;
        if (best < 0 || (i < mid ? mid - i : i - mid) < (best < mid ? mid - best : best - mid))
            best = i;
    }
    return best;
}

// Compose a SIS-format text entry shaped like the vanilla objective entries: glyphs,
// word separators, an optional line break, terminator. They carry no align/fit/kerning/
// color/scale opcodes - the checklist UI's Text object supplies all of that - and one
// pushed here would render the cell unlike the three vanilla tabs.
static void CC_ComposeSis(u8 *buf, const char *str)
{
    u8 *p = buf;
    int wrap = CC_WrapIndex(str);

    // 1 trailer byte follows and a glyph costs 2, so stop 3 short of the entry's end.
    u8 *limit = buf + CC_SIS_LABEL_MAX - 3;
    for (int i = 0; str[i] && p < limit; i++)
    {
        if (i == wrap || str[i] == '\n')
        {
            *p++ = TEXTCMD_LINEBREAK;
        }
        else if (str[i] == ' ')
        {
            *p++ = TEXTCMD_SPACE;
        }
        else
        {
            int cmd = Text_CharToCommand(str[i]);
            if (cmd != -1)
            {
                *p++ = (u8)((cmd >> 8) & 0xFF);
                *p++ = (u8)(cmd & 0xFF);
            }
        }
    }

    *p++ = TEXTCMD_TERMINATE;
}

// Redirect SIS slot 0 to this tab's entries: CT's header entries 0..3, the rest blank,
// each check's label at clear_kind + 4. Runs after Checklist_Init fills slot 0.
static void CC_InitSisForList(int idx)
{
    void **loaded = (void **)stc_sis_data[0];
    if (!loaded)
        return;

    for (int i = 0; i < CC_SIS_HEADER_NUM; i++)
        g_sis_ptrs[i] = loaded[i];

    CC_ComposeSis(g_sis_blank, "");
    for (int i = CC_SIS_HEADER_NUM; i < CC_SIS_PTR_NUM; i++)
        g_sis_ptrs[i] = g_sis_blank;

    const CustomChecklistDesc *d = &g_lists[idx].desc;
    int n = d->check_num;
    if (n > CC_CLEAR_KIND_NUM)
        n = CC_CLEAR_KIND_NUM; // label-buffer bound
    for (int c = 0; c < n; c++)
    {
        int sis_idx = d->checks[c].clear_kind + 4;
        if (!d->checks[c].label || sis_idx < CC_SIS_HEADER_NUM || sis_idx >= CC_SIS_PTR_NUM)
            continue;
        CC_ComposeSis(g_sis_label[c], d->checks[c].label);
        g_sis_ptrs[sis_idx] = g_sis_label[c];
    }

    stc_sis_data[0] = (SISData *)g_sis_ptrs;
}

// Loaded into the reclaimable per-scene heap, so valid only for the current tab's
// scene; NULL'd on failure and the swaps skip on NULL.
static _HSD_ImageDesc *g_logo_imagedesc;   // banner watermark (RGB5A3 248x128)
static _HSD_ImageDesc *g_emblem_imagedesc; // tab emblem, any size

static void CC_LoadTexturesForList(int idx)
{
    const CustomChecklistDesc *d = &g_lists[idx].desc;
    g_logo_imagedesc = NULL;   // drop the prior scene's (now reclaimed) descriptors
    g_emblem_imagedesc = NULL;
    if (!d->tex_file)
        return;

    HSD_Archive *arc = NULL;
    Gm_LoadGameFile(&arc, (char *)d->tex_file);
    if (arc == NULL)
    {
        OSReport("[CustomChecklist] %s.dat not found - %s tab art disabled\n",
                 d->tex_file, d->name);
        return;
    }
    if (d->banner_symbol)
        g_logo_imagedesc = Archive_GetPublicAddress(arc, (char *)d->banner_symbol);
    if (d->emblem_symbol)
        g_emblem_imagedesc = Archive_GetPublicAddress(arc, (char *)d->emblem_symbol);
    if (g_logo_imagedesc == NULL || g_emblem_imagedesc == NULL)
        OSReport("[CustomChecklist] %s.dat missing texture symbols (banner=%d emblem=%d)\n",
                 d->tex_file, g_logo_imagedesc != NULL, g_emblem_imagedesc != NULL);
}

// REPLACEFUNC for ClearChecker_CheckForNewUnlocks (0x8004a1a4), the gate each mode's
// *_MinorExit consults. OR-ing in the custom tabs routes a run that completed only a
// custom check into the checklist.
static int CC_CheckForNewUnlocks(GameMode mode)
{
    GameClearData *cd = gmGetClearcheckerTypeP(mode);
    int vanilla = (!Checklist_IsCacheValid() && ClearData_HasPendingUnlock(cd)) ? 1 : 0;
    return vanilla || (CC_FirstPending() >= 0);
}

// REPLACEFUNC for Scene_SetNextMinor (0x800088c8), vanilla a store of the minor id to
// GameData.minor_next. A post-run transition retargets to a pending custom tab when the
// played mode has nothing of its own to animate.
static void CC_SetNextMinor(int minor)
{
    if (g_count > 0 &&
        minor >= MNRKIND_AIRRIDECHECKLIST && minor <= MNRKIND_CITYCHECKLIST &&
        Scene_GetCurrentMajor() != MJRKIND_MENU)
    {
        g_postrun = 1;
        GameMode mode = (GameMode)(minor - MNRKIND_AIRRIDECHECKLIST);
        GameClearData *cd = gmGetClearcheckerTypeP(mode);
        int mode_pending = (!Checklist_IsCacheValid() && ClearData_HasPendingUnlock(cd)) ? 1 : 0;
        if (!mode_pending)
        {
            int idx = CC_FirstPending();
            if (idx >= 0)
                minor = g_lists[idx].minor_id;
        }
    }
    Gm_GetGameData()->minor_next = (MinorKind)minor;
}

// Shared cb_Load: Checklist_Init runs under City Trial's visual template - a valid mode,
// so no assert and no archetype-slot collision - with the clear data redirected here.
static void CC_MinorLoad(void)
{
    int idx = CC_FindListByMinor(Scene_GetCurrentMinor());
    if (idx < 0)
    {
        // Unreachable in practice; build a plain CT screen rather than leave the
        // scene half-initialized.
        Checklist_PrepMenuData();
        Checklist_Init(GMMODE_CITYTRIAL, 0);
        return;
    }

    Checklist_PrepMenuData();

    // 1 starts the new-unlock presentation, 0 jumps straight to browsing. A custom tab
    // is never itself a post-run scene, so drive it from its own pending state.
    int fresh = CC_ListHasPendingUnlock(idx) ? 1 : 0;

    g_build_active = idx;
    Checklist_Init(GMMODE_CITYTRIAL, fresh);
    g_build_active = -1;

    CC_InitSisForList(idx);

    // After the build, so its setup can't reset the per-scene heap under the load.
    CC_LoadTexturesForList(idx);

    ClearCheckerUI *chk = CC_GetUI();
    if (chk)
        chk->mode = (GameMode)g_lists[idx].mode;

    if (Scene_GetCurrentMajor() == MJRKIND_MENU)
        loadMainMenuMusic();
}

// AR, TR, CT, then each installed custom tab in registry order. Returns the count;
// ring must hold at least 3 + CC_MAX_CHECKLISTS.
static int CC_TabRing(int *ring)
{
    int n = 0;
    ring[n++] = MNRKIND_AIRRIDECHECKLIST;
    ring[n++] = MNRKIND_TOPRIDECHECKLIST;
    ring[n++] = MNRKIND_CITYCHECKLIST;
    for (int i = 0; i < g_count; i++)
        if (g_lists[i].minor_id >= 0)
            ring[n++] = g_lists[i].minor_id;
    return n;
}

// Step one tab forward (dir +1) or back (dir -1) in the ring, with wrap. Returns -1 if
// `minor` is not on the ring.
static int CC_RingStep(int minor, int dir)
{
    int ring[3 + CC_MAX_CHECKLISTS];
    int n = CC_TabRing(ring);
    for (int i = 0; i < n; i++)
        if (ring[i] == minor)
            return ring[(i + dir + n) % n];
    return -1;
}

// Vanilla checklist tab-switch cue.
#define CC_TAB_SFX 0x1000A

// REPLACEFUNC for Checklist_MinorThink (0x8004a648), shared by every checklist tab.
// With no tabs registered the ring is just AR/TR/CT and this matches vanilla.
static void CC_MinorThink(void)
{
    ClearCheckerPhase phase = (ClearCheckerPhase)Gm_GetClearChecker();
    int minor = Scene_GetCurrentMinor();

    switch (phase)
    {
    case CLEARCHECKER_PHASE_EXIT:
        // Detour to a tab with an unviewed unlock so it animates before leaving; it
        // raises is_unlocked once shown, so the next exit press falls through.
        if (g_postrun)
        {
            int idx = CC_FirstPendingExcluding(minor);
            if (idx >= 0)
            {
                Scene_SetNextMinor(g_lists[idx].minor_id);
                Scene_ExitMinor();
                break;
            }
        }
        g_postrun = 0;
        Scene_SetNextMinor(-1);
        Scene_ExitMinor();
        break;

    case CLEARCHECKER_PHASE_NEXTTAB:
    {
        SFX_PlayFullVolume(CC_TAB_SFX);
        int next = CC_RingStep(minor, +1);
        if (next < 0)
            next = MNRKIND_AIRRIDECHECKLIST;
        Scene_SetNextMinor(next);
        Scene_ExitMinor();
        break;
    }

    case CLEARCHECKER_PHASE_PREVTAB:
    {
        SFX_PlayFullVolume(CC_TAB_SFX);
        int prev = CC_RingStep(minor, -1);
        if (prev < 0)
            prev = MNRKIND_CITYCHECKLIST;
        Scene_SetNextMinor(prev);
        Scene_ExitMinor();
        break;
    }

    case CLEARCHECKER_PHASE_ENDING:
        // A custom tab reports no rewards, so it can never raise this phase.
        if (CC_FindListByMinor(minor) >= 0)
            break;
        g_postrun = 0; // leaving the checklist; don't carry the post-run chain
        MainMenu_ClearSoundTestSongThunk();
        if (minor == MNRKIND_AIRRIDECHECKLIST)
            Scene_SetNextMinor(MNRKIND_AIRRIDEENDING);
        else if (minor == MNRKIND_TOPRIDECHECKLIST)
            Scene_SetNextMinor(MNRKIND_TOPRIDEENDING);
        else
            Scene_SetNextMinor(MNRKIND_CITYENDING);
        Scene_ExitMinor();
        break;

    default:
        break;
    }
}

// The per-mode banner quad on the frame GObj; its 248 width is unique in the scene.
#define CC_BANNER_TEX_W 248

// The tab emblem quad; its 40x40 I4 signature is unique in the background scene.
#define CC_EMBLEM_TEX_W 40
#define CC_EMBLEM_TEX_FMT 0  // I4

// Theme color for the tab on screen, set per frame in CC_RecolorScene.
static u8 g_cur_theme_r, g_cur_theme_g, g_cur_theme_b;

// Retint one material diffuse onto the theme color, preserving the material's
// [min, green] brightness range. The green-dominance gate selects only the borrowed CT
// tint materials and makes the pass idempotent.
static void CC_RemapDiffuse(HSD_Material *mat)
{
    u8 r = mat->diffuse.r, g = mat->diffuse.g, b = mat->diffuse.b;
    if (!(g > r && g >= b)) // green-dominant per-mode tint only
        return;

    int tmax = g_cur_theme_r;
    if (g_cur_theme_g > tmax) tmax = g_cur_theme_g;
    if (g_cur_theme_b > tmax) tmax = g_cur_theme_b;
    if (tmax == 0)
        return; // theme unset: keep City Trial's green

    int d = g;                     // dominant (green is the max under the gate)
    int m = r < b ? r : b;         // min of the three channels
    int span = d - m;
    mat->diffuse.r = (u8)(m + span * g_cur_theme_r / tmax);
    mat->diffuse.g = (u8)(m + span * g_cur_theme_g / tmax);
    mat->diffuse.b = (u8)(m + span * g_cur_theme_b / tmax);
}

// Retint one JOBJ's dobjs and swap the mode emblem's quad in the same pass - the emblem
// lives in the recolored background scene, so it rides this walk.
static void CC_ProcessJObj(JOBJ *j)
{
    for (DOBJ *dj = j->dobj; dj; dj = dj->next)
    {
        MOBJ *mo = dj->mobj;
        if (!mo)
            continue;
        if (mo->mat)
            CC_RemapDiffuse(mo->mat);
        if (!g_emblem_imagedesc)
            continue; // textures not loaded; recolor only, leave the vanilla emblem
        for (TOBJ *t = mo->tobj; t; t = t->next)
        {
            _HSD_ImageDesc *img = t->imagedesc;
            if (!img || img == g_emblem_imagedesc)
                continue;
            if (img->width != CC_EMBLEM_TEX_W || img->format != CC_EMBLEM_TEX_FMT)
                continue;
            // The vanilla emblem is a flipbook whose anim pass rewrites imagedesc every
            // tick; clearing aobj/imagetbl leaves this descriptor the only binding.
            t->imagedesc = g_emblem_imagedesc;
            t->aobj = NULL;
            t->imagetbl = NULL;
        }
    }
}

// Walk a JOBJ subtree (child + sibling).
static void CC_RecolorJObj(JOBJ *j, int depth)
{
    if (!j || depth > 32)
        return;
    CC_ProcessJObj(j);
    CC_RecolorJObj(j->child, depth + 1);
    CC_RecolorJObj(j->sibling, depth + 1);
}

// The root's own dobjs plus its child subtree, but not its sibling, which would leave
// this scene.
static void CC_RecolorGObj(GOBJ *gobj)
{
    if (!gobj)
        return;
    JOBJ *jroot = (JOBJ *)gobj->hsd_object;
    if (!jroot)
        return;
    CC_ProcessJObj(jroot);
    CC_RecolorJObj(jroot->child, 0);
}

// A TObj on the 248-wide texture is repointed at the tab watermark and its diffuse
// forced white so the texture samples neutrally. JOBJ scale and quad scroll are untouched.
static void CC_RetargetBannerJObj(JOBJ *j)
{
    if (!g_logo_imagedesc)
        return; // textures not loaded; leave the vanilla banner art in place

    for (DOBJ *dj = j->dobj; dj; dj = dj->next)
    {
        MOBJ *mo = dj->mobj;
        if (!mo)
            continue;
        for (TOBJ *t = mo->tobj; t; t = t->next)
        {
            _HSD_ImageDesc *img = t->imagedesc;
            if (!img)
                continue;
            int already = (img == g_logo_imagedesc);
            if (!already && img->width != CC_BANNER_TEX_W)
                continue;
            if (!already)
                t->imagedesc = g_logo_imagedesc;
            if (mo->mat)
            {
                // Alpha untouched, keeping the quad's blend.
                mo->mat->diffuse.r = 0xFF;
                mo->mat->diffuse.g = 0xFF;
                mo->mat->diffuse.b = 0xFF;
            }
        }
    }
}

static void CC_RetargetBanner(GOBJ *gobj)
{
    if (!gobj)
        return;
    for (JOBJ *stack[40], **sp = stack, *j = (JOBJ *)gobj->hsd_object; ; )
    {
        while (j)
        {
            CC_RetargetBannerJObj(j);
            if (sp < stack + 40)
                *sp++ = j->sibling; // defer sibling
            j = j->child;           // descend child
        }
        if (sp == stack)
            break;
        j = *--sp;
    }
}

// No-op unless a custom tab is the current minor scene.
static void CC_RecolorScene(void)
{
    int idx = CC_FindListByMinor(Scene_GetCurrentMinor());
    if (idx < 0)
        return;

    g_cur_theme_r = g_lists[idx].desc.theme_r;
    g_cur_theme_g = g_lists[idx].desc.theme_g;
    g_cur_theme_b = g_lists[idx].desc.theme_b;

    // The background scene and marker GObjs carry the per-mode tint in their material
    // diffuses; the frame GObj is texture-colored, so it only takes the banner swap.
    ScMenuCommon *mm = Gm_GetMenuData();
    CC_RecolorGObj(mm->clearchecker.bg_gobj);
    CC_RecolorGObj(mm->clearchecker.cross_gobj);
    CC_RecolorGObj(mm->clearchecker.prize1_gobj);
    CC_RecolorGObj(mm->clearchecker.prize2_gobj);

    CC_RetargetBanner(mm->clearchecker.frame_gobj);

    // TMEM caches texels, so the swapped banner/emblem need a per-frame invalidate.
    GXInvalidateTexAll();
}

// Private xorshift32: regenerating a layout from a saved seed needs a stream depending on
// nothing but that seed, which the shared HSD_Randi state can't give. State must be nonzero.
static u32 CC_Rand32(u32 *state)
{
    u32 x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

// Returns 0 while the save is unavailable. The seed is minted once per save file, so
// layouts are stable across boots at 4 saved bytes instead of 120 of layout.
static int CC_EnsureLayoutSeed(void)
{
    if (!g_save)
        return 0;
    if (g_save->layout_seed)
        return 1;

    // The RTC differs between save files; avalanche it, since xorshift32 seeded with
    // near-identical states produces visibly similar first draws.
    u64 t = OSGetTime();
    u32 s = (u32)t ^ (u32)(t >> 32);
    s ^= s >> 16;
    s *= 2246822519u;
    s ^= s >> 13;
    s *= 3266489917u;
    s ^= s >> 16;

    g_save->layout_seed = s ? s : 1u;
    OSReport("[CustomChecklist] Grid layout seed initialized (0x%08x)\n", g_save->layout_seed);
    return 1;
}

// Show every cell the tab defines a check for. Cells with no check behind them stay
// hidden - a revealed empty box reads as an objective that can never be completed.
static void CC_RevealChecks(int idx)
{
    GameClearData *cd = CC_CLEAR(idx);
    const CustomChecklistDesc *d = &g_lists[idx].desc;

    for (int c = 0; c < d->check_num; c++)
    {
        int ck = d->checks[c].clear_kind;
        if (ck >= 0 && ck < CC_CLEAR_KIND_NUM)
            cd->clear[ck].is_visible = 1;
    }
}

// Shuffle grid_mapping from the save seed mixed with the tab's name hash, so tabs neither
// share a layout nor reshuffle each other. clear[] completion state is live by now and is
// left alone; the reveals are positional and so stale under a new layout. No meta-cell
// pre-placement: Fill100ClearKind returns 0xFF for custom tabs.
static void CC_ApplyLayout(int idx)
{
    GameClearData *cd = CC_CLEAR(idx);

    u32 st = (g_save->layout_seed ^ g_lists[idx].name_hash) * 2654435761u;
    st ^= st >> 15;
    if (!st)
        st = 1u;

    for (int k = 0; k < CC_CLEAR_KIND_NUM; k++)
        cd->grid_mapping[k] = (u8)k;

    for (int k = CC_CLEAR_KIND_NUM - 1; k > 0; k--)
    {
        u32 j = CC_Rand32(&st) % (u32)(k + 1);
        u8 tmp = cd->grid_mapping[k];
        cd->grid_mapping[k] = cd->grid_mapping[j];
        cd->grid_mapping[j] = tmp;
    }

    for (int k = 0; k < CC_CLEAR_KIND_NUM; k++)
        cd->clear[k].is_visible = 0;
    g_lists[idx].revealed[0] = 0;
    g_lists[idx].revealed[1] = 0;

    // The latch outlives the wipe: a consumer can call RevealAll from its OnSaveLoaded,
    // before the shuffle this session's first frame runs.
    if (g_lists[idx].reveal_all)
        CC_RevealChecks(idx);
}

// Once per tab per session, lazily: a consumer registers from its own OnSaveLoaded, which
// can run before this mod's sets g_save. Until then the tab keeps the identity mapping.
static void CC_EnsureLayout(int idx)
{
    if (g_lists[idx].layout_done)
        return;
    if (!CC_EnsureLayoutSeed())
        return;
    CC_ApplyLayout(idx);
    g_lists[idx].layout_done = 1;
}

// Every cell starts hidden - the board reveals outward from completions - and
// grid_mapping must be a full bijection over all 120 clear_kinds or Checklist_Update's
// reverse scan trips the "Clearchecker Number 120" assert. Identity until the shuffle.
static void CC_InitClearData(int idx)
{
    GameClearData *cd = CC_CLEAR(idx);
    for (int k = 0; k < CC_CLEAR_KIND_NUM; k++)
    {
        cd->grid_mapping[k] = (u8)k;
        memset(&cd->clear[k], 0, sizeof(cd->clear[k]));
    }
    g_lists[idx].revealed[0] = 0;
    g_lists[idx].revealed[1] = 0;
}

// Show the cell occupying a physical grid slot, resolved back through the tab's
// grid_mapping permutation.
static void CC_RevealSlot(GameClearData *cd, int slot)
{
    for (int k = 0; k < CC_CLEAR_KIND_NUM; k++)
    {
        if (cd->grid_mapping[k] == (u8)slot)
        {
            cd->clear[k].is_visible = 1;
            return;
        }
    }
}

// The expansion Checklist_ProcessUnlock performs as it animates an unlock, repeated here
// for cells that arrive already complete, which the engine never animates.
static void CC_RevealNeighbors(GameClearData *cd, int clear_kind)
{
    int slot = cd->grid_mapping[clear_kind];
    int col = slot % CHECKLIST_GRID_COLS;
    int row = slot / CHECKLIST_GRID_COLS;

    if (col > 0)
        CC_RevealSlot(cd, slot - 1);
    if (col < CHECKLIST_GRID_COLS - 1)
        CC_RevealSlot(cd, slot + 1);
    if (row > 0)
        CC_RevealSlot(cd, slot - CHECKLIST_GRID_COLS);
    if (row < CHECKLIST_GRID_ROWS - 1)
        CC_RevealSlot(cd, slot + CHECKLIST_GRID_COLS);
}

// Clone the City Trial checklist descriptor with our cb_Load. Returns the installed
// minor id, or -1 on failure.
static int CC_InstallMinor(void)
{
    MinorSceneDesc *descs = Hoshi_GetMinorScenes();
    MinorSceneDesc d = descs[MNRKIND_CITYCHECKLIST];
    d.cb_Load = CC_MinorLoad;
    return (int)(s8)Hoshi_InstallMinorScene(&d);
}

// Vanilla checklist "objective completed" cue.
#define CC_UNLOCK_SFX 0x10008

// Suppressed when the unlock cache is valid (in menus the flip-and-sparkle animates on
// tab entry instead), and gated on the engine's one-frame cooldown so a record path
// through ClearChecker_SetNewUnlock can't double-play.
static void CC_PlayUnlockSfx(void)
{
    if (Checklist_IsCacheValid())
        return;
    int frame = ClearChecker_GetFrameIndex();
    if (*stc_clearchecker_sfx_last_frame != frame)
    {
        SFX_PlayFullVolume(CC_UNLOCK_SFX);
        *stc_clearchecker_sfx_last_frame = frame;
    }
}

// Match by name hash, else claim an empty slot. Returns -1 until the save loads; there
// are as many slots as tabs, so there is always room.
static int CC_ResolveSaveSlot(int i)
{
    if (!g_save)
        return -1;
    // name_hash is set for every tab (it doubles as the layout key), so this guard is what
    // keeps a mod-persisted tab from claiming a slot it never reads.
    if (!g_lists[i].fw_persist)
        return -1;
    if (g_lists[i].save_slot >= 0)
        return g_lists[i].save_slot;

    u32 h = g_lists[i].name_hash;
    int empty = -1;
    for (int s = 0; s < CC_MAX_CHECKLISTS; s++)
    {
        if (g_save->slots[s].name_hash == h)
        {
            g_lists[i].save_slot = s;
            return s;
        }
        if (empty < 0 && g_save->slots[s].name_hash == 0)
            empty = s;
    }
    if (empty < 0)
        return -1;
    g_save->slots[empty].name_hash = h;
    g_lists[i].save_slot = empty;
    return empty;
}

// An unresolved slot reports not-recorded, so the check re-evaluates next frame.
static int CC_DefaultIsRecorded(int i, int clear_kind)
{
    int s = CC_ResolveSaveSlot(i);
    if (s < 0)
        return 0;
    return (g_save->slots[s].recorded[clear_kind >> 6] >> (clear_kind & 63)) & 1ULL;
}

// No-op if the slot can't be resolved, leaving the check pending rather than lost. The
// card is not written here - Hoshi_WriteSave is a synchronous whole-file rewrite and
// checks complete mid-run, so the bit rides along with the game's own saves.
static void CC_DefaultRecord(int i, int clear_kind)
{
    int s = CC_ResolveSaveSlot(i);
    if (s < 0)
        return;
    g_save->slots[s].recorded[clear_kind >> 6] |= (1ULL << (clear_kind & 63));
}

// Complete any check whose predicate now holds, and restore the board state of the ones
// already recorded.
static void CC_Evaluate(void)
{
    for (int i = 0; i < g_count; i++)
    {
        CCList *L = &g_lists[i];

        // Before the is_ready gate: deferring it would show the identity fallback and then
        // visibly reshuffle the moment the tab became ready.
        CC_EnsureLayout(i);

        if (L->desc.is_ready && !L->desc.is_ready())
            continue;

        GameClearData *cd = CC_CLEAR(i);
        for (int c = 0; c < L->desc.check_num; c++)
        {
            const CustomCheck *chk = &L->desc.checks[c];
            int ck = chk->clear_kind;
            if (ck < 0 || ck >= CC_CLEAR_KIND_NUM)
                continue;

            int recorded = L->fw_persist ? CC_DefaultIsRecorded(i, ck)
                                         : L->desc.is_recorded(ck);
            if (!recorded)
            {
                if (!chk->is_complete || !chk->is_complete(ck))
                    continue;
                if (L->fw_persist)
                    CC_DefaultRecord(i, ck);
                else
                    L->desc.record_complete(ck);
                if (L->desc.on_complete)
                    L->desc.on_complete(ck);
                // A check satisfied outside any gamemode never gets is_new from the engine,
                // so seed it; the flip-and-sparkle runs on next entry.
                cd->clear[ck].is_new = 1;
                CC_PlayUnlockSfx();
            }
            else if (!cd->clear[ck].is_new)
            {
                // A pending is_new is left to Checklist_ProcessUnlock, which raises
                // is_unlocked and reveals the neighbours itself; forcing it only once none
                // is pending shows a prior-boot completion complete with no replay.
                cd->clear[ck].is_unlocked = 1;
                if (!CC_BIT_TEST(L->revealed, ck))
                {
                    CC_BIT_SET(L->revealed, ck);
                    CC_RevealNeighbors(cd, ck);
                }
            }
        }
    }
}

static int CC_Register(const CustomChecklistDesc *desc)
{
    if (!desc || !desc->checks || desc->check_num <= 0)
    {
        OSReport("[CustomChecklist] Register rejected: invalid descriptor\n");
        return -1;
    }
    if (g_count >= CC_MAX_CHECKLISTS)
    {
        OSReport("[CustomChecklist] Register rejected: registry full (max %d)\n",
                 CC_MAX_CHECKLISTS);
        return -1;
    }

    // The mod owns recorded state only if it provides both callbacks; with either omitted
    // the framework persists the tab itself, keyed by name hash.
    int has_recorded = desc->is_recorded != NULL;
    int has_record   = desc->record_complete != NULL;
    int fw_persist = !(has_recorded && has_record);
    if (has_recorded != has_record)
        OSReport("[CustomChecklist] '%s': only one persistence callback provided; using framework persistence\n",
                 desc->name ? desc->name : "?");
    if (fw_persist && !desc->name)
    {
        OSReport("[CustomChecklist] Register rejected: framework persistence needs a name\n");
        return -1;
    }

    int idx = g_count;
    CCList *L = &g_lists[idx];
    L->desc = *desc; // pointers it holds must stay valid
    L->mode = GMMODE_NUM + idx;
    L->fw_persist = fw_persist;
    // Hashed for every tab: mod-persisted tabs still need a layout key. A NULL name
    // hashes to a fixed constant, so unnamed tabs share a layout stream.
    L->name_hash = CC_HashName(desc->name);
    L->save_slot = -1;
    L->layout_done = 0;
    L->reveal_all = 0;

    CC_InitClearData(idx);

    L->minor_id = CC_InstallMinor();
    if (L->minor_id < 0)
    {
        OSReport("[CustomChecklist] Register failed: minor-scene install for '%s'\n",
                 desc->name ? desc->name : "?");
        return -1;
    }

    g_count++;
    OSReport("[CustomChecklist] Registered '%s' as mode %d (minor scene %d, %d checks, %s persistence)\n",
             desc->name ? desc->name : "?", L->mode, L->minor_id, desc->check_num,
             fw_persist ? "framework" : "mod");
    return L->mode;
}

// Latched rather than applied and forgotten: the tab's is_visible bits live in RAM, and
// the layout shuffle drops them once per session at a moment the caller cannot see.
static void CC_RevealAll(int mode)
{
    int idx = mode - GMMODE_NUM;
    if (idx < 0 || idx >= g_count)
        return;

    g_lists[idx].reveal_all = 1;
    CC_RevealChecks(idx);
    OSReport("[CustomChecklist] '%s': revealed %d cells\n",
             g_lists[idx].desc.name ? g_lists[idx].desc.name : "?",
             g_lists[idx].desc.check_num);
}

static const CustomChecklistAPI g_api = {
    .Register = CC_Register,
    .RevealAll = CC_RevealAll,
};

static void OnBoot(void)
{
    // Installed unconditionally; with no tabs registered they reproduce vanilla.
    CODEPATCH_REPLACEFUNC(gmGetClearcheckerTypeP, CC_GetClearcheckerTypeP);
    CODEPATCH_REPLACEFUNC(Checklist_GetRewardNum, CC_GetRewardNum);
    CODEPATCH_REPLACEFUNC(Checklist_GetClearKindFromRewardIndex, CC_GetClearKindFromRewardIndex);
    CODEPATCH_REPLACEFUNC(Checklist_MinorThink, CC_MinorThink);
    CODEPATCH_REPLACEFUNC(ClearChecker_CheckForNewUnlocks, CC_CheckForNewUnlocks);
    CODEPATCH_REPLACEFUNC(Scene_SetNextMinor, CC_SetNextMinor);

    Hoshi_ExportMod((void *)&g_api);

    OSReport("[CustomChecklist] Hooks installed, API exported (v%d.%d)\n",
             CUSTOM_CHECKLIST_API_MAJOR, CUSTOM_CHECKLIST_API_MINOR);
}

// A different save file has a different layout seed and slot assignments, so cached
// per-save state is dropped and re-resolved.
static void CC_InvalidateSaveBindings(void)
{
    for (int i = 0; i < g_count; i++)
    {
        g_lists[i].save_slot = -1;
        g_lists[i].layout_done = 0;
    }
}

static void OnSaveInit(void)
{
    g_save = (CCSave *)mod_desc.save_ptr;
    memset(g_save, 0, sizeof(*g_save));
    // A zeroed layout_seed makes the next CC_EnsureLayout mint a fresh one.
    CC_InvalidateSaveBindings();
}

static void OnSaveLoaded(void)
{
    g_save = (CCSave *)mod_desc.save_ptr;
    CC_InvalidateSaveBindings();
}

static void OnFrameStart(void)
{
    CC_Evaluate();
}

static void OnFrameEnd(void)
{
    // Re-apply the tab tint after the menu's per-frame material animation sets the green.
    CC_RecolorScene();
}

ModDesc mod_desc = {
    .name = "custom_checklist",
    .author = "DeDeDK",
    .version.major = CUSTOM_CHECKLIST_API_MAJOR,
    .version.minor = CUSTOM_CHECKLIST_API_MINOR,
    .save_size = sizeof(CCSave),
    .OnBoot = OnBoot,
    .OnSaveInit = OnSaveInit,
    .OnSaveLoaded = OnSaveLoaded,
    .OnFrameStart = OnFrameStart,
    .OnFrameEnd = OnFrameEnd,
};
