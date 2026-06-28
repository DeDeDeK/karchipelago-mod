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

// Defined at the bottom; forward-declared for the save lifecycle callbacks.
extern ModDesc mod_desc;

// Custom Checklist framework: mod-owned checklist tabs alongside the three vanilla
// ones (Air Ride / Top Ride / City Trial), folded into the L/R tab rotation. Each
// tab is a synthetic checklist mode (>= GMMODE_NUM) backed by a mod-owned
// GameClearData served through gmGetClearcheckerTypeP. With nothing registered the
// installed REPLACEFUNCs reproduce vanilla, so a build with no consumer is inert.

// Bounds the fixed-size registry (g_lists), the persisted save (CCSave.slots), and
// the tab-cycle ring - there is no dynamic allocation, so this is a hard ceiling on
// registered tabs. Generous headroom; each slot costs ~0x140 BSS + 20 save bytes.
#define CC_MAX_CHECKLISTS 16

// Per-tab state. clear_storage is oversized past the 0xF4 GameClearData struct so a
// read into the per-mode "records tail" the real modes carry stays in-bounds. BSS-
// zeroed, persists for the session (one tab's flags must survive while another is up).
typedef struct CCList
{
    CustomChecklistDesc desc;   // copied at Register (pointers must stay valid)
    u8  clear_storage[0x140];   // GameClearData for this tab
    int minor_id;               // installed minor-scene id (-1 if install failed)
    int mode;                   // GMMODE_NUM + registry index
    int fw_persist;             // 1 if the framework owns this tab's recorded state
    u32 name_hash;              // stable key into the save (fw_persist tabs)
    int save_slot;              // resolved CCSave slot, -1 until first access
} CCList;

static CCList g_lists[CC_MAX_CHECKLISTS];
static int g_count = 0;

#define CC_CLEAR(i) ((GameClearData *)g_lists[i].clear_storage)

// Framework-managed persistence for tabs that leave is_recorded/record_complete NULL:
// a completed-clear_kind bitmask in the mod's own hoshi save, keyed by a hash of the
// tab name (not registry index) so bits survive tabs being added/removed/reordered.
typedef struct CCSave
{
    struct
    {
        u32 name_hash;     // 0 = empty slot
        u64 recorded[2];   // completed clear_kinds (2 u64 words cover 0..119)
    } slots[CC_MAX_CHECKLISTS];
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

// Index of the tab being built by CC_MinorLoad (-1 otherwise). While set, the
// clear-data accessor redirects the CITYTRIAL slot to that tab's block so
// Checklist_Init populates from the tab instead of City Trial.
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

// REPLACEFUNC for gmGetClearcheckerTypeP (0x800076a0). Modes 0/1/2 return the
// GameData blocks (CITYTRIAL redirected to the tab under construction while
// g_build_active is set); modes >= GMMODE_NUM return the registered tab's block;
// unknown modes return NULL (matching vanilla's out-of-range, without the assert).
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

// REPLACEFUNC for Checklist_GetClearKindFromRewardIndex (0x80049c84): 0 for custom
// tabs (no rewards) keeps Checklist_ProcessUnlock's new-unlock scan inert so the cell
// animation can run, and dodges the mode>=3 assert. Real modes reproduce vanilla.
static u8 CC_GetClearKindFromRewardIndex(GameMode mode, u8 reward_index)
{
    if ((unsigned)mode >= GMMODE_NUM)
        return 0;
    return stc_reward_table_ptrs[mode][reward_index].clear_kind;
}

// MainMenu_GetData()[0xed0] -> root GObj -> user_data (+0x2c), mirroring
// Gm_GetClearChecker (0x8017cf14). NULL before the grid GObj exists.
static u8 *CC_GetUI(void)
{
    u8 *mgr = (u8 *)MainMenu_GetData();
    u8 *root = *(u8 **)(mgr + 0xed0);
    if (!root)
        return NULL;
    return *(u8 **)(root + 0x2c);
}

// Cell objective text comes from stc_sis_data[0][clear_kind + 4]. Checklist_Init
// loads City Trial's SisClrChkCT into slot 0; after the build we repoint slot 0 at our
// own pointer array (CT header entries 0..3 kept, rest blank, each label slotted at
// clear_kind + 4). Only one custom tab is on screen at a time, so a single shared
// buffer set is recomposed per tab build.

#define CC_SIS_HEADER_NUM 4                       // entries 0..3 are CT's title/legend
#define CC_SIS_PTR_NUM (CC_CLEAR_KIND_NUM + 4)    // covers index clear_kind + 4
#define CC_SIS_LABEL_MAX 128

static void *g_sis_ptrs[CC_SIS_PTR_NUM];
static u8 g_sis_blank[24];
static u8 g_sis_label[CC_CLEAR_KIND_NUM][CC_SIS_LABEL_MAX];

// Compose a SIS-format text entry from a C string (vanilla UI-text formatting).
static void CC_ComposeSis(u8 *buf, const char *str)
{
    u8 *p = buf;

    *p++ = 0x12; // ALIGN_LEFT
    *p++ = 0x18; // FIT_ON
    *p++ = 0x16; // KERNING_ON
    *p++ = 0x0c;
    *p++ = 0xbb;
    *p++ = 0xbb;
    *p++ = 0xbb; // COLOR gray
    *p++ = 0x0e;
    *p++ = 0x00;
    *p++ = 0xb3;
    *p++ = 0x00;
    *p++ = 0xb3; // SCALE ~0.70

    for (; *str; str++)
    {
        if (*str == ' ')
        {
            *p++ = 0x1a; // SIS space command
        }
        else
        {
            int cmd = Text_CharToCommand(*str);
            if (cmd != -1)
            {
                *p++ = (u8)((cmd >> 8) & 0xFF);
                *p++ = (u8)(cmd & 0xFF);
            }
        }
    }

    *p++ = 0x03;
    *p++ = 0x0f;
    *p++ = 0x0d;
    *p++ = 0x17;
    *p++ = 0x19;
    *p++ = 0x13;
    *p++ = 0x00; // TERMINATE
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

// The tab's banner/emblem image descriptors, loaded into the reclaimable per-scene
// heap. Valid only for the current tab's scene; NULL'd on failure (swaps skip on NULL,
// keeping the borrowed CT art).
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

// REPLACEFUNC for ClearChecker_CheckForNewUnlocks (0x8004a1a4), the per-mode gate
// each mode's *_MinorExit consults to route into the checklist after a run. Vanilla
// result OR any custom-tab pending, so a run that completes only a custom check (with
// no coinciding vanilla cell) still brings up the post-run checklist.
static int CC_CheckForNewUnlocks(GameMode mode)
{
    GameClearData *cd = gmGetClearcheckerTypeP(mode);
    int vanilla = (!Checklist_IsCacheValid() && ClearData_HasPendingUnlock(cd)) ? 1 : 0;
    return vanilla || (CC_FirstPending() >= 0);
}

// REPLACEFUNC for Scene_SetNextMinor (0x800088c8), vanilla a one-line store of the
// minor id to GameData+0x7d8. On a post-run transition (from a gameplay major) into
// the played mode's checklist tab (AR/TR/CT = 32/33/34), if that mode has no vanilla
// pending unlock but a custom tab does, retarget straight to the custom tab so its
// unlock animates without a detour through an empty tab. g_postrun is flagged either
// way so the exit path can still chain to a custom tab after the played mode animates.
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
    ((u8 *)Gm_GetGameData())[0x7d8] = (u8)minor;
}

// Shared cb_Load for every custom tab. Builds the tab by running Checklist_Init under
// City Trial's visual template (a valid mode: no mode>=3 assert, no archetype-slot
// collision) while g_build_active redirects the clear-data accessor to this tab's
// block, then flips the UI mode byte (chk+0x14) to the tab's synthetic mode.
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

    // fresh selects Checklist_Init's entry state: 1 starts the new-unlock presentation
    // (flip-and-sparkle), 0 jumps straight to browsing. A custom tab is never itself a
    // post-run scene, so drive it from this tab's own pending-unlock state - presenting
    // "as if after a run" exactly when one of its checks is freshly completed.
    int fresh = CC_ListHasPendingUnlock(idx) ? 1 : 0;

    g_build_active = idx;
    Checklist_Init(GMMODE_CITYTRIAL, fresh);
    g_build_active = -1;

    CC_InitSisForList(idx);

    // After the build (so its setup can't reset the per-scene heap under the load),
    // pull the tab's banner/emblem art into that heap.
    CC_LoadTexturesForList(idx);

    u8 *chk = CC_GetUI();
    if (chk)
        chk[0x14] = (u8)g_lists[idx].mode;

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

// REPLACEFUNC for Checklist_MinorThink (0x8004a648), shared by every checklist tab.
// Reimplements the vanilla tab cycle with the custom tabs folded in; with no tabs
// registered the ring is just AR/TR/CT and this matches vanilla.
static void CC_MinorThink(void)
{
    int phase = Gm_GetClearChecker(); // chk+0x15
    int minor = Scene_GetCurrentMinor();

    switch (phase)
    {
    case 11: // exit to main menu
        // Post-run only: if a custom unlock is still unviewed, detour to that tab so it
        // animates before leaving (it raises is_unlocked once shown, so the next exit
        // press falls through). Lets the played mode animate first.
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

    case 12: // next tab
    {
        SFX_PlayFullVolume(0x1000A);
        int next = CC_RingStep(minor, +1);
        if (next < 0)
            next = MNRKIND_AIRRIDECHECKLIST;
        Scene_SetNextMinor(next);
        Scene_ExitMinor();
        break;
    }

    case 13: // previous tab
    {
        SFX_PlayFullVolume(0x1000A);
        int prev = CC_RingStep(minor, -1);
        if (prev < 0)
            prev = MNRKIND_CITYCHECKLIST;
        Scene_SetNextMinor(prev);
        Scene_ExitMinor();
        break;
    }

    case 14: // switch to the records screen (custom tabs have none: no-op)
        if (CC_FindListByMinor(minor) >= 0)
            break;
        g_postrun = 0; // leaving the checklist; don't carry the post-run chain
        MainMenu_ClearSoundTestSongThunk();
        if (minor == MNRKIND_AIRRIDECHECKLIST)
            Scene_SetNextMinor(28);
        else if (minor == MNRKIND_TOPRIDECHECKLIST)
            Scene_SetNextMinor(29);
        else
            Scene_SetNextMinor(30); // CT
        Scene_ExitMinor();
        break;

    default:
        break;
    }
}

// MainMenu_GetData() (0x801311e0) return value. The built scene GObjs whose materials
// carry the per-mode tint hang off fixed offsets from it.
#define MMD_BASE 0x80558788

// The background scene (+0xED0) and the marker/counter models (+0x1100 range) carry the
// per-mode tint in their material diffuses. The frame border (+0xEE4) is texture-colored
// (white material), not in this set.
static const int g_recolor_slots[] = { 0xED0, 0x1104, 0x110C, 0x1114 };
#define CC_RECOLOR_SLOT_NUM ((int)(sizeof(g_recolor_slots) / sizeof(g_recolor_slots[0])))

// The per-mode banner is a single 248x128 quad at MainMenuData + 0xEE4, retargeted to
// the tab watermark. Its 248 width is unique in the scene, which is how the walk finds it.
#define MMD_FRAME_SLOT 0xEE4
#define CC_BANNER_TEX_W 248

// The tab emblem (top-right, between the L/R arrows) is a single 40x40 I4 quad, unique
// in the background scene (distinct from the 72x72 circle layers), retargeted to the
// tab emblem.
#define CC_EMBLEM_TEX_W 40
#define CC_EMBLEM_TEX_FMT 0  // I4

// Theme color for the tab on screen, set per frame in CC_RecolorScene.
static u8 g_cur_theme_r, g_cur_theme_g, g_cur_theme_b;

// Retint one diffuse (GXColor RGBA at HSD_Material + 0x4) onto the tab's theme color.
// Gated on green-dominance: the borrowed CT tint materials are green-dominant, so this
// selects only them (not the purple cell tiles or other UI) and is idempotent (the
// result is no longer green-dominant). The brightness range [min, green] is preserved
// and redistributed onto the theme hue; a zero theme leaves the green.
static void CC_RemapDiffuse(u8 *mat)
{
    u8 r = mat[4], g = mat[5], b = mat[6];
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
    mat[4] = (u8)(m + span * g_cur_theme_r / tmax);
    mat[5] = (u8)(m + span * g_cur_theme_g / tmax);
    mat[6] = (u8)(m + span * g_cur_theme_b / tmax);
}

// Retint one JOBJ's dobjs and, in the same pass, swap the mode emblem's quad (the
// unique 40x40 I4 texture) to the tab emblem. The emblem lives in the recolored
// background scene, so this rides the recolor walk; the 40x40/I4 signature is unique
// there, so nothing else is touched.
static void CC_ProcessJObj(u8 *j)
{
    for (u8 *dj = *(u8 **)(j + 0x18); dj; dj = *(u8 **)(dj + 0x04)) // JOBJ.dobj, DOBJ.next
    {
        u8 *mo = *(u8 **)(dj + 0x08); // DOBJ.mobj
        if (!mo)
            continue;
        u8 *mat = *(u8 **)(mo + 0x0C); // MOBJ.mat
        if (mat)
            CC_RemapDiffuse(mat);
        if (!g_emblem_imagedesc)
            continue; // textures not loaded; recolor only, leave the vanilla emblem
        for (u8 *t = *(u8 **)(mo + 0x08); t; t = *(u8 **)(t + 0x08)) // MOBJ.tobj, TOBJ.next
        {
            u8 *img = *(u8 **)(t + 0x58); // TOBJ.imagedesc
            if (!img || img == (u8 *)g_emblem_imagedesc)
                continue;
            if (*(u16 *)(img + 0x04) != CC_EMBLEM_TEX_W ||
                *(u32 *)(img + 0x08) != CC_EMBLEM_TEX_FMT)
                continue;
            // The vanilla emblem is a texture flipbook (TObj.aobj + imagetbl) whose anim
            // pass rewrites imagedesc every tick; clear both so our descriptor is the only
            // binding. The material tint still animates.
            *(void **)(t + 0x58) = g_emblem_imagedesc;
            *(void **)(t + 0x64) = 0; // TOBJ.aobj
            *(void **)(t + 0x68) = 0; // TOBJ.imagetbl
        }
    }
}

// Walk a JOBJ subtree (child + sibling). JOBJ.child=+0x10, sibling=+0x08.
static void CC_RecolorJObj(u8 *j, int depth)
{
    if (!j || depth > 32)
        return;
    CC_ProcessJObj(j);
    CC_RecolorJObj(*(u8 **)(j + 0x10), depth + 1);
    CC_RecolorJObj(*(u8 **)(j + 0x08), depth + 1);
}

// Recolor one built scene GObj: its root's own dobjs plus its child subtree, but not the
// root's sibling (which would leave this scene).
static void CC_RecolorGObj(u8 *gobj)
{
    if (!gobj)
        return;
    u8 *jroot = *(u8 **)(gobj + 0x28); // GObj hsd_object -> scene JOBJ root
    if (!jroot)
        return;
    CC_ProcessJObj(jroot);
    CC_RecolorJObj(*(u8 **)(jroot + 0x10), 0);
}

// Retarget the banner quad on one JOBJ: a TObj pointing at the 248-wide banner texture
// (or our descriptor, once swapped) is repointed at the tab watermark, and its material
// diffuse forced white so the texture samples neutrally. JOBJ scale and quad scroll are
// untouched.
static void CC_RetargetBannerJObj(u8 *j)
{
    if (!g_logo_imagedesc)
        return; // textures not loaded; leave the vanilla banner art in place

    for (u8 *dj = *(u8 **)(j + 0x18); dj; dj = *(u8 **)(dj + 0x04))
    {
        u8 *mo = *(u8 **)(dj + 0x08);
        if (!mo)
            continue;
        u8 *mat = *(u8 **)(mo + 0x0C);
        for (u8 *t = *(u8 **)(mo + 0x08); t; t = *(u8 **)(t + 0x08))
        {
            u8 *img = *(u8 **)(t + 0x58);
            if (!img)
                continue;
            int already = (img == (u8 *)g_logo_imagedesc);
            if (!already && *(u16 *)(img + 0x04) != CC_BANNER_TEX_W)
                continue;
            if (!already)
                *(void **)(t + 0x58) = g_logo_imagedesc;
            if (mat)
            {
                mat[4] = 0xFF; // diffuse R
                mat[5] = 0xFF; // diffuse G
                mat[6] = 0xFF; // diffuse B (alpha untouched, keeps the quad's blend)
            }
        }
    }
}

// Walk the banner GObj's JOBJ tree, retargeting its scrolling quad to the tab logo.
static void CC_RetargetBanner(u8 *gobj)
{
    if (!gobj)
        return;
    u8 *jroot = *(u8 **)(gobj + 0x28);
    for (u8 *stack[40], **sp = stack, *j = jroot; ; )
    {
        while (j)
        {
            CC_RetargetBannerJObj(j);
            if (sp < stack + 40)
                *sp++ = *(u8 **)(j + 0x08); // defer sibling
            j = *(u8 **)(j + 0x10);          // descend child
        }
        if (sp == stack)
            break;
        j = *--sp;
    }
}

// Recolor the current custom tab's scene to its theme and retarget its scrolling banner.
// No-op unless a custom tab is the current minor scene. Idempotent per frame (the menu's
// anim pass re-applies the green tint each frame).
static void CC_RecolorScene(void)
{
    int idx = CC_FindListByMinor(Scene_GetCurrentMinor());
    if (idx < 0)
        return;

    g_cur_theme_r = g_lists[idx].desc.theme_r;
    g_cur_theme_g = g_lists[idx].desc.theme_g;
    g_cur_theme_b = g_lists[idx].desc.theme_b;

    for (int i = 0; i < CC_RECOLOR_SLOT_NUM; i++)
        CC_RecolorGObj(*(u8 **)(MMD_BASE + g_recolor_slots[i]));

    CC_RetargetBanner(*(u8 **)(MMD_BASE + MMD_FRAME_SLOT));

    // GX caches texels in TMEM, so the swapped banner/emblem need a per-frame invalidate
    // to render from fresh data while the tab is up.
    GXInvalidateTexAll();
}

// Lay out a tab's clear data. grid_mapping must be a full identity bijection over all
// 120 clear_kinds: Checklist_Update reverse-scans it to map a cursor position back to a
// clear_kind, and an unmapped position trips the "Clearchecker Number 120" assert.
// is_visible gates which cells draw, so only defined checks appear.
static void CC_InitClearData(int idx)
{
    GameClearData *cd = CC_CLEAR(idx);
    const CustomChecklistDesc *d = &g_lists[idx].desc;
    for (int k = 0; k < CC_CLEAR_KIND_NUM; k++)
    {
        cd->grid_mapping[k] = (u8)k;
        *((u8 *)&cd->clear[k]) = 0;
    }
    for (int c = 0; c < d->check_num; c++)
    {
        int ck = d->checks[c].clear_kind;
        if (ck < 0 || ck >= CC_CLEAR_KIND_NUM)
            continue;
        cd->clear[ck].is_visible = 1;
    }
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

// Vanilla checklist "objective completed" cue (ClearChecker_SetNewUnlock plays it on a
// fresh in-run completion).
#define CC_UNLOCK_SFX 0x10008

// Play the completion cue for a freshly-completed check, so every tab gets vanilla's
// mid-run "ding". Suppressed when the unlock cache is valid (in menus, where the flip-
// and-sparkle animates on tab entry instead). Shares the engine's one-frame SFX cooldown
// (stc_clearchecker_sfx_last_frame), so a tab whose record_complete also routes through
// ClearChecker_SetNewUnlock never double-plays.
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

// Resolve (and lazily claim) the save slot for a framework-persisted tab: match an
// existing slot by name hash, else claim an empty one. Returns -1 if the save is not
// loaded yet (there is always room: slots == max tabs).
static int CC_ResolveSaveSlot(int i)
{
    if (!g_save)
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

// Framework-default record_complete: set the saved bit and persist. No-op if the slot
// can't be resolved (the check stays pending rather than being lost).
static void CC_DefaultRecord(int i, int clear_kind)
{
    int s = CC_ResolveSaveSlot(i);
    if (s < 0)
        return;
    g_save->slots[s].recorded[clear_kind >> 6] |= (1ULL << (clear_kind & 63));
    Hoshi_WriteSave();
}

// For each registered tab: complete any not-yet-recorded check whose predicate now holds
// (record it, seed the cell's is_new/is_visible so the animation runs on next tab entry,
// play the cue), and re-assert is_visible / raise is_unlocked for already-recorded checks
// so a prior-boot completion shows complete with no replay.
static void CC_Evaluate(void)
{
    for (int i = 0; i < g_count; i++)
    {
        CCList *L = &g_lists[i];
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
                // Optional mod cue, fired once on first completion. Works for framework-
                // persisted tabs too, which have no record_complete to notify from.
                if (L->desc.on_complete)
                    L->desc.on_complete(ck);
                // A check satisfied outside any gamemode (e.g. "Boot the game") never gets
                // is_new from the engine, so seed it; the flip-and-sparkle runs on next entry.
                cd->clear[ck].is_new = 1;
                cd->clear[ck].is_visible = 1;
                CC_PlayUnlockSfx();
            }
            else
            {
                // Already recorded: keep it rendered. A still-pending is_new (completed
                // this session, not yet viewed) is left for Checklist_ProcessUnlock to
                // animate; only force is_unlocked when none is pending, so a prior-boot
                // completion (block is BSS-zeroed) shows complete with no replay.
                cd->clear[ck].is_visible = 1;
                if (!cd->clear[ck].is_new)
                    cd->clear[ck].is_unlocked = 1;
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
    L->name_hash = fw_persist ? CC_HashName(desc->name) : 0;
    L->save_slot = -1;

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
    // Install the REPLACEFUNCs unconditionally; with no tabs registered they reproduce
    // vanilla, so the mod is inert until a consumer registers a tab (via the exported
    // API, typically in OnSaveLoaded).
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

// Framework save (per-tab recorded bitmask for framework-persisted tabs). Tabs that
// supply their own persistence callbacks never touch it.
static void OnSaveInit(void)
{
    g_save = (CCSave *)mod_desc.save_ptr;
    memset(g_save, 0, sizeof(*g_save));
}

static void OnSaveLoaded(void)
{
    g_save = (CCSave *)mod_desc.save_ptr;
    // save_slot is resolved lazily on first access, so saved bits bind by tab name
    // regardless of registration order.
}

static void OnFrameStart(void)
{
    CC_Evaluate();
}

static void OnFrameEnd(void)
{
    // Re-apply the current tab's tint after the menu's per-frame material animation sets
    // the green; no-op unless a custom tab is on screen.
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
