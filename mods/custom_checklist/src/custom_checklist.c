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

// Mod-owned checklist tabs alongside the three vanilla ones, folded into the L/R tab
// rotation. Each tab is a synthetic checklist mode (>= GMMODE_NUM) backed by a mod-owned
// GameClearData served through gmGetClearcheckerTypeP. With nothing registered the
// installed REPLACEFUNCs reproduce vanilla.

// Hard ceiling on registered tabs: bounds the registry, the save slots, and the tab ring.
#define CC_MAX_CHECKLISTS 16

// Per-tab state, BSS-zeroed and live for the session. clear_storage is a union with the real
// modes' blocks so a read into the "records tail" they carry past GameClearData stays
// in-bounds - a custom tab has no records, but engine code doesn't know that.
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
} CCList;

#define CC_BIT_TEST(w, k) (((w)[(k) >> 6] >> ((k) & 63)) & 1ULL)
#define CC_BIT_SET(w, k)  ((w)[(k) >> 6] |= 1ULL << ((k) & 63))

static CCList g_lists[CC_MAX_CHECKLISTS];
static int g_count = 0;

#define CC_CLEAR(i) (&g_lists[i].clear_storage.clear)

// Framework persistence for tabs that leave is_recorded/record_complete NULL, keyed by
// tab-name hash so bits survive tabs being added, removed, or reordered.
typedef struct CCSave
{
    struct
    {
        u32 name_hash;     // 0 = empty slot
        u64 recorded[2];   // completed clear_kinds (2 u64 words cover 0..119)
    } slots[CC_MAX_CHECKLISTS];

    // Grid layout seed; each tab mixes it with its name hash for its own permutation, so
    // tabs that claim no slot get a layout too. 0 = not yet generated.
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

// Index of the tab being built by CC_MinorLoad (-1 otherwise). While set, the clear-data
// accessor redirects CITYTRIAL to that tab's block so Checklist_Init populates from it.
static int g_build_active = -1;

// Raised while the checklist session was entered from a run (not menu navigation).
// Drives the post-run chain into a custom tab; cleared on exit.
static int g_postrun = 0;

static int CC_FindListByMinor(int minor)
{
    for (int i = 0; i < g_count; i++)
        if (g_lists[i].minor_id == minor)
            return i;
    return -1;
}

// True if any cell is completed (is_new) but not yet shown (is_unlocked) - i.e. has
// a pending unlock animation. Mirrors ClearChecker_CheckForNewUnlocks over the cells.
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

// REPLACEFUNC for gmGetClearcheckerTypeP (0x800076a0). Modes >= GMMODE_NUM return the
// registered tab's block; unknown modes return NULL, without vanilla's assert.
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

// Vanilla per-mode reward counts {AR, TR, CT}. Custom tabs host no native rewards.
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

// The checklist screen's UI state, mirroring Gm_GetClearChecker (0x8017cf14). NULL before
// the grid GObj exists.
static ClearCheckerUI *CC_GetUI(void)
{
    GOBJ *root = Gm_GetMenuData()->clearchecker.bg_gobj;
    return root ? (ClearCheckerUI *)root->userdata : NULL;
}

// Cell objective text comes from stc_sis_data[0][clear_kind + 4]. After the build, slot 0
// is repointed at this array: CT's header entries 0..3, the rest blank, each check's label
// at clear_kind + 4. Only one custom tab is on screen, so the buffers are shared.

#define CC_SIS_HEADER_NUM 4                       // entries 0..3 are CT's title/legend
#define CC_SIS_PTR_NUM (CC_CLEAR_KIND_NUM + 4)    // covers index clear_kind + 4
#define CC_SIS_LABEL_MAX 128

static void *g_sis_ptrs[CC_SIS_PTR_NUM];
static u8 g_sis_blank[24];
static u8 g_sis_label[CC_CLEAR_KIND_NUM][CC_SIS_LABEL_MAX];

// A label longer than this wraps onto a second line. The cell's text box holds exactly two
// lines, and a line the engine considers too wide for the box is squeezed narrower rather
// than wrapped, so the break has to be authored. Vanilla keeps single lines up to 37
// characters but writes the overwhelming majority of its entries as two lines of ~25; this
// wraps sooner so every line stays in that range, where the glyphs render at full size.
#define CC_SIS_WRAP 30

// Index of the space to turn into the line break, or -1 for none - either because the label
// fits on one line or because it carries its own '\n', which always wins. Otherwise the space
// nearest the middle, so the two lines come out balanced.
//
// A label whose break matters should place it: the midpoint rule splits on width alone and
// will part a name from a trailing number ("SINGLE RACE / 8 Finish in 1st!"), where vanilla
// breaks after the whole designation.
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

// Compose a SIS-format text entry from a C string, matching the shape of the vanilla
// checklist's objective entries: glyphs, word separators, an optional line break where the
// text wraps ('\n' in the label, else the automatic one), and the terminator - no trailing
// break. Those entries carry no
// align/fit/kerning/color/scale opcodes of their own (the checklist UI's Text object
// supplies all of it), so pushing any here renders the cell text differently from the three
// vanilla tabs - a scale opcode shrinks it outright.
static void CC_ComposeSis(u8 *buf, const char *str)
{
    u8 *p = buf;
    int wrap = CC_WrapIndex(str);

    // 1 trailer byte follows and a glyph costs 2, so stop 3 short rather than run off the
    // fixed-size entry when a consumer supplies a long label.
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

// Redirect SIS slot 0 to this tab's entries. Run after Checklist_Init loads
// SisClrChkCT into slot 0.
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

// Tab banner/emblem image descriptors, loaded into the reclaimable per-scene heap: valid
// only for the current tab's scene, NULL'd on failure (the swaps skip on NULL).
static _HSD_ImageDesc *g_logo_imagedesc;   // banner watermark (RGB5A3 248x128)
static _HSD_ImageDesc *g_emblem_imagedesc; // tab emblem (I4 40x40)

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
// *_MinorExit consults to route into the checklist after a run. Vanilla result OR any
// custom-tab pending, so a run completing only a custom check still routes there.
static int CC_CheckForNewUnlocks(GameMode mode)
{
    GameClearData *cd = gmGetClearcheckerTypeP(mode);
    int vanilla = (!Checklist_IsCacheValid() && ClearData_HasPendingUnlock(cd)) ? 1 : 0;
    return vanilla || (CC_FirstPending() >= 0);
}

// REPLACEFUNC for Scene_SetNextMinor (0x800088c8), vanilla a store of the minor id to
// GameData.minor_next. On a post-run transition into a checklist tab, retarget to a pending
// custom tab when the played mode has nothing of its own to animate; g_postrun is flagged
// either way so the exit path can chain to custom tabs afterwards.
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

// Shared cb_Load for every custom tab: runs Checklist_Init under City Trial's visual
// template (a valid mode, so no assert and no archetype-slot collision) with g_build_active
// redirecting the clear data to this tab, then flips the UI mode to the synthetic one.
static void CC_MinorLoad(void)
{
    int idx = CC_FindListByMinor(Scene_GetCurrentMinor());
    if (idx < 0)
    {
        // Should not happen (cb_Load only runs for installed custom minors); build a
        // plain CT screen so the scene isn't left half-initialized.
        Checklist_PrepMenuData();
        Checklist_Init(GMMODE_CITYTRIAL, 0);
        return;
    }

    Checklist_PrepMenuData();

    // 1 starts the new-unlock presentation (flip-and-sparkle), 0 jumps straight to browsing.
    // A custom tab is never itself a post-run scene, so drive it from its own pending state.
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

// The ordered tab ring: AR, TR, CT, then each installed custom tab in registry order.
// Returns the count; ring must hold at least 3 + CC_MAX_CHECKLISTS.
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
// Reimplements the vanilla tab cycle with the custom tabs folded in; with no tabs
// registered the ring is just AR/TR/CT and this matches vanilla.
static void CC_MinorThink(void)
{
    ClearCheckerPhase phase = (ClearCheckerPhase)Gm_GetClearChecker();
    int minor = Scene_GetCurrentMinor();

    switch (phase)
    {
    case CLEARCHECKER_PHASE_EXIT:
        // Post-run only: detour to a tab with an unviewed unlock so it animates before
        // leaving; it raises is_unlocked once shown, so the next exit press falls through.
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

// The per-mode banner is a single 248x128 quad on the frame GObj; its 248 width is unique
// in the scene, which is how the walk finds it.
#define CC_BANNER_TEX_W 248

// The tab emblem (top-right, between the L/R arrows) is a single 40x40 I4 quad, unique in
// the background scene (the circle layers are 72x72).
#define CC_EMBLEM_TEX_W 40
#define CC_EMBLEM_TEX_FMT 0  // I4

// Theme color for the tab on screen, set per frame in CC_RecolorScene.
static u8 g_cur_theme_r, g_cur_theme_g, g_cur_theme_b;

// Retint one material diffuse onto the tab's theme color, preserving the material's
// [min, green] brightness range. The green-dominance gate selects only the borrowed CT tint
// materials and makes the pass idempotent.
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

// Retint one JOBJ's dobjs and, in the same pass, swap the mode emblem's quad to the tab
// emblem - it lives in the recolored background scene, so it rides the recolor walk.
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
            // The vanilla emblem is a texture flipbook whose anim pass rewrites imagedesc
            // every tick; clear both so this descriptor is the only binding.
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

// Recolor one built scene GObj: its root's own dobjs plus its child subtree, but not the
// root's sibling (which would leave this scene).
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

// Retarget the banner quad on one JOBJ: a TObj on the 248-wide texture is repointed at the
// tab watermark and its diffuse forced white so the texture samples neutrally. JOBJ scale
// and quad scroll are untouched.
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

// Walk the banner GObj's JOBJ tree, retargeting its scrolling quad to the tab logo.
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

// Recolor the current custom tab's scene to its theme and retarget its scrolling banner.
// No-op unless a custom tab is the current minor scene.
static void CC_RecolorScene(void)
{
    int idx = CC_FindListByMinor(Scene_GetCurrentMinor());
    if (idx < 0)
        return;

    g_cur_theme_r = g_lists[idx].desc.theme_r;
    g_cur_theme_g = g_lists[idx].desc.theme_g;
    g_cur_theme_b = g_lists[idx].desc.theme_b;

    // The background scene and the marker/counter GObjs carry the per-mode tint in their
    // material diffuses; the frame GObj is texture-colored, so it only takes the banner swap.
    ScMenuCommon *mm = Gm_GetMenuData();
    CC_RecolorGObj(mm->clearchecker.bg_gobj);
    CC_RecolorGObj(mm->clearchecker.cross_gobj);
    CC_RecolorGObj(mm->clearchecker.prize1_gobj);
    CC_RecolorGObj(mm->clearchecker.prize2_gobj);

    CC_RetargetBanner(mm->clearchecker.frame_gobj);

    // TMEM caches texels, so the swapped banner/emblem need a per-frame invalidate.
    GXInvalidateTexAll();
}

// Private xorshift32 for the grid layout: regenerating a layout from a saved seed needs a
// stream depending on nothing but that seed, which the shared HSD_Randi state can't give.
// State must be nonzero.
static u32 CC_Rand32(u32 *state)
{
    u32 x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

// Ensure the save carries a grid-layout seed; returns 0 while the save is unavailable.
// Generated once per save file and persisted, so layouts are stable across boots (the clear
// storage itself is BSS, so 4 bytes of seed are saved instead of 120 of layout).
static int CC_EnsureLayoutSeed(void)
{
    if (!g_save)
        return 0;
    if (g_save->layout_seed)
        return 1;

    // OSGetTime is the console RTC, so it differs between save files; avalanche it, since
    // xorshift32 seeded with near-identical states produces visibly similar first draws.
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

// Shuffle a tab's grid_mapping into a random-but-stable permutation, from the save seed
// mixed with the tab's name hash so tabs neither share a layout nor reshuffle each other.
// Writes grid_mapping only - clear[] completion state is live by the time this runs - but
// drops the reveals, which are positional and so stale under a new layout. Fisher-Yates
// keeps the result a full bijection over 0..119, which Checklist_Update's reverse scan
// needs. No meta-cell pre-placement: Fill100ClearKind returns 0xFF for custom tabs.
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
}

// Apply the saved layout once per tab per session. Lazy because a consumer registers from
// its own OnSaveLoaded, which can run before this mod's has set g_save; until then the tab
// keeps CC_InitClearData's identity mapping, itself a valid bijection.
static void CC_EnsureLayout(int idx)
{
    if (g_lists[idx].layout_done)
        return;
    if (!CC_EnsureLayoutSeed())
        return;
    CC_ApplyLayout(idx);
    g_lists[idx].layout_done = 1;
}

// Lay out a tab's clear data. Every cell starts hidden - the board reveals outward from
// completions - and grid_mapping must be a full bijection over all 120 clear_kinds or
// Checklist_Update's reverse scan trips the "Clearchecker Number 120" assert; identity is
// the fallback until CC_EnsureLayout can shuffle.
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

// Reveal a completed cell's four orthogonal neighbours - the expansion
// Checklist_ProcessUnlock performs as it animates an unlock. The framework repeats it for
// cells that come back already complete (a prior boot, or a consumer back-filling its own
// recorded state), which the engine never animates and so never reveals around.
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

// Register a tab as a new minor scene by cloning the City Trial checklist descriptor and
// overriding its cb_Load. Returns the installed id (-1 on failure).
static int CC_InstallMinor(void)
{
    MinorSceneDesc *descs = Hoshi_GetMinorScenes();
    MinorSceneDesc d = descs[MNRKIND_CITYCHECKLIST];
    d.cb_Load = CC_MinorLoad;
    return (int)(s8)Hoshi_InstallMinorScene(&d);
}

// Vanilla checklist "objective completed" cue.
#define CC_UNLOCK_SFX 0x10008

// Play the completion cue for a freshly-completed check. Suppressed when the unlock cache is
// valid (in menus the flip-and-sparkle animates on tab entry instead), and gated on the
// engine's one-frame cooldown so a record path through ClearChecker_SetNewUnlock can't
// double-play.
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

// Resolve (and lazily claim) the save slot for a framework-persisted tab: match by name
// hash, else claim an empty one. Returns -1 until the save loads (slots == max tabs, so
// there is always room).
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

// Framework-default is_recorded: read the tab's saved bitmask. Unresolved slot reports
// not-recorded, so the check re-evaluates next frame.
static int CC_DefaultIsRecorded(int i, int clear_kind)
{
    int s = CC_ResolveSaveSlot(i);
    if (s < 0)
        return 0;
    return (g_save->slots[s].recorded[clear_kind >> 6] >> (clear_kind & 63)) & 1ULL;
}

// Framework-default record_complete: set the saved bit. No-op if the slot can't be
// resolved (the check stays pending rather than being lost). The card is not written
// here - checks complete mid-run and Hoshi_WriteSave is a synchronous whole-file
// rewrite; the bit rides along with the game's own main-menu-entry save.
static void CC_DefaultRecord(int i, int clear_kind)
{
    int s = CC_ResolveSaveSlot(i);
    if (s < 0)
        return;
    g_save->slots[s].recorded[clear_kind >> 6] |= (1ULL << (clear_kind & 63));
}

// Per-frame pass over every tab: complete any check whose predicate now holds, and restore
// the board state of the ones already recorded.
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
                // Not yet recorded: complete it the first frame the predicate holds.
                if (!chk->is_complete || !chk->is_complete())
                    continue;
                if (L->fw_persist)
                    CC_DefaultRecord(i, ck);
                else
                    L->desc.record_complete(ck);
                // Optional mod cue, fired once on first completion whichever side persists.
                if (L->desc.on_complete)
                    L->desc.on_complete(ck);
                // A check satisfied outside any gamemode never gets is_new from the engine,
                // so seed it; the flip-and-sparkle runs on next entry.
                cd->clear[ck].is_new = 1;
                CC_PlayUnlockSfx();
            }
            else if (!cd->clear[ck].is_new)
            {
                // Settled complete. A pending is_new is left alone for
                // Checklist_ProcessUnlock to animate (it raises is_unlocked and reveals the
                // neighbours itself), so forcing is_unlocked only once none is pending makes
                // a prior-boot completion show complete with no replay.
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

    // The mod owns recorded state only if it provides BOTH callbacks; with either omitted
    // the framework persists the tab itself (keyed by name hash, which must then be set).
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
    L->desc = *desc; // copy the descriptor (pointers it holds must stay valid)
    L->mode = GMMODE_NUM + idx;
    L->fw_persist = fw_persist;
    // Hashed for every tab: mod-persisted tabs still need a stable per-tab layout key. A NULL
    // name hashes to a fixed constant, so unnamed tabs share a layout stream.
    L->name_hash = CC_HashName(desc->name);
    L->save_slot = -1;
    L->layout_done = 0;

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

static const CustomChecklistAPI g_api = {
    .Register = CC_Register,
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

// Drop cached per-save state so it re-resolves against whatever save is now current: a
// different file has a different layout seed and different slot assignments.
static void CC_InvalidateSaveBindings(void)
{
    for (int i = 0; i < g_count; i++)
    {
        g_lists[i].save_slot = -1;
        g_lists[i].layout_done = 0;
    }
}

// Framework save: the per-tab recorded bitmask (framework-persisted tabs only) plus the
// grid-layout seed, which every tab uses.
static void OnSaveInit(void)
{
    g_save = (CCSave *)mod_desc.save_ptr;
    memset(g_save, 0, sizeof(*g_save));
    // Zeroed layout_seed means the next CC_EnsureLayout mints a fresh one.
    CC_InvalidateSaveBindings();
}

static void OnSaveLoaded(void)
{
    g_save = (CCSave *)mod_desc.save_ptr;
    // save_slot and the grid layout are resolved lazily on first access, so saved bits and
    // layouts bind by tab name regardless of registration order.
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
