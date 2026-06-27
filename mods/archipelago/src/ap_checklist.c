#include "game.h"
#include "os.h"
#include "scene.h"
#include "text.h"
#include "code_patch/code_patch.h"
#include "hoshi/func.h"
#include "hsd.h"

#include "main.h"
#include "ap_checklist.h"

// Mod-owned clear data for the AP checklist (the synthetic 4th checklist mode).
// Oversized past the 0xF4 GameClearData so any read into the per-mode "records
// tail" the three real modes carry stays in-bounds. BSS-zeroed; grid_mapping is
// filled into a full 120-entry permutation in APChecklist_OnBoot.
static u8 ap_clear_storage[0x140];
#define AP_CLEAR ((GameClearData *)ap_clear_storage)

// Minor-scene id the AP tab is installed as (assigned by Hoshi_InstallMinorScene;
// -1 until installed, which disables the tab insertion in the cycle below).
static int g_ap_minor_id = -1;

// Set while APChecklist_MinorLoad drives Checklist_Init for the AP tab: the build
// borrows City Trial's visual template (a valid mode), so while this is set the
// clear-checker accessor below returns AP_CLEAR for the CITYTRIAL mode the build
// runs under, making every clear-data-driven part reflect the AP checklist.
static int g_ap_build_active = 0;

// Raised while the current checklist session was entered from a run (a mode's
// *_MinorExit routing into the checklist, not menu navigation). Set by
// APChecklist_SetNextMinor when it sees a checklist tab requested from a gameplay
// major; consumed by the exit path to chain into the AP tab and cleared on exit.
// Confines the post-run AP chain to runs only - manual menu browsing is untouched.
static int g_ap_postrun = 0;

// A custom check pairs an AP clear_kind with a predicate. The evaluator
// (APChecklist_OnFrameStart) completes the cell the first frame the predicate
// holds via ClearChecker_SetNewUnlock(AP_CHECKLIST_MODE, clear_kind), which
// check_detection's SetNewUnlock replacement records into the AP sent_checks slot.

typedef struct APCheck
{
    int clear_kind;
    const char *label;       // objective text shown when the cell is selected
    int (*is_complete)(void);
} APCheck;

// Phase 1 stubs: observable, deterministic placeholders that exercise the
// pipeline end to end. Replaced by the real custom objectives later.
static int Check_Booted(void)            { return ap_save->boot_num >= 1; }
static int Check_ReceivedAnyItem(void)   { return ap_save->item_received_count >= 1; }
static int Check_ReceivedFiveItems(void) { return ap_save->item_received_count >= 5; }

static const APCheck ap_checks[] = {
    { 0, "Boot the game",   Check_Booted },
    { 1, "Receive an item", Check_ReceivedAnyItem },
    { 2, "Receive 5 items", Check_ReceivedFiveItems },
};

#define AP_CHECK_NUM ((int)(sizeof(ap_checks) / sizeof(ap_checks[0])))

// Already recorded as sent this save? Out-of-range cells report "done".
static int APChecklist_IsRecorded(int clear_kind)
{
    if (clear_kind < 0 || clear_kind >= CLEAR_KIND_NUM)
        return 1;
    return (ap_save->sent_checks_ap[clear_kind >> 6] >> (clear_kind & 63)) & 1ULL;
}

// REPLACEFUNC for gmGetClearcheckerTypeP (0x800076a0): adds AP_CHECKLIST_MODE.
// Modes 0/1/2 return the same GameData-embedded blocks as vanilla; the AP mode
// returns the mod block; anything else returns NULL (matching vanilla's
// out-of-range NULL return, without the assert).
static GameClearData *APChecklist_GetClearcheckerTypeP(GameMode mode)
{
    GameData *gd = Gm_GetGameData();
    switch (mode)
    {
    case GMMODE_AIRRIDE:    return &gd->airride_clear.clear;
    case GMMODE_TOPRIDE:    return &gd->topride_clear.clear;
    case GMMODE_CITYTRIAL:  return g_ap_build_active ? AP_CLEAR : &gd->city_clear.clear;
    case AP_CHECKLIST_MODE: return AP_CLEAR;
    default:                return NULL;
    }
}

// REPLACEFUNC for Checklist_GetRewardNum (0x80049c20): the AP checklist has no
// native rewards (its cells only ever host cross-mode placements), so report 0.
// This gates every reward loop in the render path off for the AP mode and dodges
// the vanilla mode>=3 assert. Real modes return their vanilla counts.
static u8 APChecklist_GetRewardNum(GameMode mode)
{
    static const u8 counts[GMMODE_NUM] = {
        REWARD_COUNT_AIRRIDE, REWARD_COUNT_TOPRIDE, REWARD_COUNT_CITYTRIAL,
    };
    return mode < GMMODE_NUM ? counts[mode] : 0;
}

// REPLACEFUNC for Checklist_GetClearKindFromRewardIndex (0x80049c84). The AP
// checklist has no native rewards, so return 0 for AP_CHECKLIST_MODE instead of
// tripping the vanilla mode>=3 assert. Checklist_ProcessUnlock's first new-unlock
// scan calls this for every is_new cell regardless of mode; returning 0 keeps that
// scan inert on the AP tab so its is_new -> unlock animation can run. Real modes
// reproduce vanilla exactly (reward_tables[mode][index].clear_kind).
static u8 APChecklist_GetClearKindFromRewardIndex(GameMode mode, u8 reward_index)
{
    if ((unsigned)mode >= GMMODE_NUM)
        return 0;
    return stc_reward_table_ptrs[mode][reward_index].clear_kind;
}

// The checklist UI struct (chk): MainMenu_GetData()[0xed0] -> root GObj ->
// user_data (+0x2c). Mirrors how Gm_GetClearChecker (0x8017cf14) derives it.
// NULL before the grid GObj exists.
static u8 *APChecklist_GetUI(void)
{
    u8 *mgr = (u8 *)MainMenu_GetData();
    u8 *root = *(u8 **)(mgr + 0xed0);
    if (!root)
        return NULL;
    return *(u8 **)(root + 0x2c);
}

// Custom cell labels (SIS text). The checklist shows a cell's objective text from
// stc_sis_data[0][clear_kind + 4] (Checklist_Update -> Text_StorePremadeText).
// Checklist_Init loaded City Trial's SisClrChkCT into slot 0, so after the build
// APChecklist_InitSis repoints slot 0 at an AP-owned pointer array: CT's header
// entries (0..3) kept, the rest blank, each check's label composed in and slotted
// at clear_kind + 4. The CT tab reloads slot 0 from the archive on its own cb_Load,
// so its labels stay intact.

#define AP_SIS_PTR_NUM (CLEAR_KIND_NUM + 4)  // covers index clear_kind + 4 for all kinds
#define AP_SIS_HEADER_NUM 4                  // entries 0..3 are CT's title/legend

static void *ap_sis_ptrs[AP_SIS_PTR_NUM];
static u8 ap_sis_blank[24];
static u8 ap_sis_label[AP_CHECK_NUM][128];

// Compose a SIS-format text entry from a C string (vanilla UI-text formatting).
static void APChecklist_ComposeSis(u8 *buf, const char *str)
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

// Redirect the checklist's SIS slot 0 to AP-owned entries. Run after
// Checklist_Init has loaded SisClrChkCT into slot 0.
static void APChecklist_InitSis(void)
{
    void **loaded = (void **)stc_sis_data[0];
    if (!loaded)
        return;

    for (int i = 0; i < AP_SIS_HEADER_NUM; i++)
        ap_sis_ptrs[i] = loaded[i];

    APChecklist_ComposeSis(ap_sis_blank, "");
    for (int i = AP_SIS_HEADER_NUM; i < AP_SIS_PTR_NUM; i++)
        ap_sis_ptrs[i] = ap_sis_blank;

    for (int i = 0; i < AP_CHECK_NUM; i++)
    {
        int idx = ap_checks[i].clear_kind + 4;
        if (!ap_checks[i].label || idx < AP_SIS_HEADER_NUM || idx >= AP_SIS_PTR_NUM)
            continue;
        APChecklist_ComposeSis(ap_sis_label[i], ap_checks[i].label);
        ap_sis_ptrs[idx] = ap_sis_label[i];
    }

    stc_sis_data[0] = (SISData *)ap_sis_ptrs;
}

// Per-scene art for the AP tab, loaded from ApChecklistTex.dat (this mod's assets/,
// staged to the FST root) into the reclaimable per-scene heap: two _HSD_ImageDesc
// publics the banner/emblem swaps below point the checklist's TObjs at. Reloaded
// each MinorLoad and NULL'd on failure; the swaps skip on NULL, keeping vanilla art.
#define AP_TEX_FILE "ApChecklistTex"
#define AP_BANNER_SYMBOL "apBannerImg"
#define AP_EMBLEM_SYMBOL "apEmblemImg"

// Valid only for the current AP-tab scene (per-scene heap); reloaded each MinorLoad.
static _HSD_ImageDesc *ap_logo_imagedesc;   // banner watermark (RGB5A3 248x128)
static _HSD_ImageDesc *ap_emblem_imagedesc; // tab emblem (I4 64x64)

static void APChecklist_LoadTextures(void)
{
    HSD_Archive *arc = NULL;
    ap_logo_imagedesc = NULL;   // drop the prior scene's (now reclaimed) descriptors
    ap_emblem_imagedesc = NULL;

    Gm_LoadGameFile(&arc, AP_TEX_FILE);
    if (arc == NULL)
    {
        OSReport("[APChecklist] %s.dat not found - AP tab art disabled\n", AP_TEX_FILE);
        return;
    }
    ap_logo_imagedesc = Archive_GetPublicAddress(arc, AP_BANNER_SYMBOL);
    ap_emblem_imagedesc = Archive_GetPublicAddress(arc, AP_EMBLEM_SYMBOL);
    if (ap_logo_imagedesc == NULL || ap_emblem_imagedesc == NULL)
        OSReport("[APChecklist] %s.dat missing texture symbols (banner=%d emblem=%d)\n",
                 AP_TEX_FILE, ap_logo_imagedesc != NULL, ap_emblem_imagedesc != NULL);
}

// True if any cell of a clear-data block has a pending unlock animation: completed
// (is_new) but not yet shown (is_unlocked not raised). The vanilla gate
// (ClearChecker_CheckForNewUnlocks) scans the same condition over a mode's cells.
static int ClearData_HasPendingUnlock(const GameClearData *cd)
{
    if (!cd)
        return 0;
    for (int k = 0; k < CLEAR_KIND_NUM; k++)
        if (cd->clear[k].is_new && !cd->clear[k].is_unlocked)
            return 1;
    return 0;
}

// True if an AP check is completed (is_new) but its flip hasn't been shown yet
// (is_unlocked not raised). Drives Checklist_Init's `fresh` flag and the post-run
// chain. Unlike the vanilla gate this is not cache-gated: the AP tab should present a
// genuinely-new check whenever entered (e.g. the boot check, or manual navigation).
static int APChecklist_HasPendingUnlock(void)
{
    return ClearData_HasPendingUnlock(AP_CLEAR);
}

// REPLACEFUNC for ClearChecker_CheckForNewUnlocks (0x8004a1a4): the per-mode gate each
// mode's *_MinorExit consults to decide whether to route into the checklist scene
// after a run. Reproduces vanilla (cache-stale scan of the mode's cells) and also
// reports a pending AP-checklist unlock, so a round that completes only an AP custom
// check - with no coinciding vanilla cell - still brings up the post-run checklist.
static int APChecklist_CheckForNewUnlocks(GameMode mode)
{
    GameClearData *cd = gmGetClearcheckerTypeP(mode);
    int vanilla = (!Checklist_IsCacheValid() && ClearData_HasPendingUnlock(cd)) ? 1 : 0;
    return vanilla || APChecklist_HasPendingUnlock();
}

// REPLACEFUNC for Scene_SetNextMinor (0x800088c8), vanilla a one-line store of the
// requested minor id to GameData+0x7d8. The post-run gate above can route a run into
// the played mode's checklist tab (AR/TR/CT = 32/33/34) when only an AP check went
// new, leaving that tab with nothing to animate. When the request comes from a
// gameplay major (i.e. a post-run transition, not menu navigation) and the played
// mode has no vanilla pending unlock while the AP tab does, retarget straight to the
// AP tab so its unlock animates without a detour through an empty played-mode tab.
// The post-run session is flagged either way so the exit path can chain to the AP tab
// when the played mode did have its own unlocks to show first.
static void APChecklist_SetNextMinor(int minor)
{
    if (g_ap_minor_id >= 0 &&
        minor >= MNRKIND_AIRRIDECHECKLIST && minor <= MNRKIND_CITYCHECKLIST &&
        Scene_GetCurrentMajor() != MJRKIND_MENU)
    {
        g_ap_postrun = 1;
        GameMode mode = (GameMode)(minor - MNRKIND_AIRRIDECHECKLIST);
        GameClearData *cd = gmGetClearcheckerTypeP(mode);
        int mode_pending = (!Checklist_IsCacheValid() && ClearData_HasPendingUnlock(cd)) ? 1 : 0;
        if (!mode_pending && APChecklist_HasPendingUnlock())
            minor = g_ap_minor_id;
    }
    ((u8 *)Gm_GetGameData())[0x7d8] = (u8)minor;
}

// The AP tab is a 4th checklist minor scene; this is its cb_Load. It builds a mode-3
// checklist by running Checklist_Init under City Trial's visual template (a valid
// mode: no mode>=3 assert, no archetype-slot collision) while g_ap_build_active
// redirects the clear-data accessor to AP_CLEAR, so the columns, completion counter,
// and cell layout come from AP data. The UI mode byte (chk+0x14) is then flipped to
// AP_CHECKLIST_MODE so the per-frame path also reads AP_CLEAR. Mirrors
// Checklist_MinorLoad (0x8004a768).
static void APChecklist_MinorLoad(void)
{
    Checklist_PrepMenuData();

    // Checklist_Init's `fresh` flag selects the entry state: 1 starts the new-unlock
    // presentation (state 0 -> the flip-and-sparkle animation), 0 jumps straight to
    // browsing (state 4). Vanilla sets it coming out of a run; the AP tab is never a
    // post-run scene, so we drive it from our own pending-unlock state - presenting
    // "as if after a run" exactly when an AP check is newly completed.
    int fresh = APChecklist_HasPendingUnlock() ? 1 : 0;

    g_ap_build_active = 1;
    Checklist_Init(GMMODE_CITYTRIAL, fresh);
    g_ap_build_active = 0;

    APChecklist_InitSis();

    // After the scene is built (so its setup can't reset the per-scene heap and wipe
    // the load): pull the AP banner/emblem art into that heap for this tab's lifetime.
    APChecklist_LoadTextures();

    u8 *chk = APChecklist_GetUI();
    if (chk)
        chk[0x14] = AP_CHECKLIST_MODE;

    if (Scene_GetCurrentMajor() == MJRKIND_MENU)
        loadMainMenuMusic();
}

// REPLACEFUNC for Checklist_MinorThink (0x8004a648), shared by every checklist
// tab. Reimplements the vanilla tab cycle with the AP tab folded into the
// rotation: AR -> TR -> CT -> AP -> AR. Falls back to the vanilla 3-tab wrap if
// the AP minor failed to install (g_ap_minor_id < 0).
static void APChecklist_MinorThink(void)
{
    int phase = Gm_GetClearChecker(); // chk+0x15
    int minor = Scene_GetCurrentMinor();
    int ap = g_ap_minor_id;

    switch (phase)
    {
    case 11: // exit to main menu
        // Post-run only: if an AP unlock is still unviewed, detour to the AP tab so it
        // animates before leaving (it raises is_unlocked once shown, so the next exit
        // press falls through). Lets the played mode animate its own unlocks first.
        if (g_ap_postrun && ap >= 0 && minor != ap && APChecklist_HasPendingUnlock())
        {
            Scene_SetNextMinor(ap);
            Scene_ExitMinor();
            break;
        }
        g_ap_postrun = 0;
        Scene_SetNextMinor(-1);
        Scene_ExitMinor();
        break;

    case 12: // next tab
    {
        SFX_PlayFullVolume(0x1000A);
        int next = minor + 1;
        if (ap >= 0 && minor == MNRKIND_CITYCHECKLIST)
            next = ap;
        else if (ap >= 0 && minor == ap)
            next = MNRKIND_AIRRIDECHECKLIST;
        else if (next > MNRKIND_CITYCHECKLIST)
            next = MNRKIND_AIRRIDECHECKLIST; // vanilla wrap
        Scene_SetNextMinor(next);
        Scene_ExitMinor();
        break;
    }

    case 13: // previous tab
    {
        SFX_PlayFullVolume(0x1000A);
        int prev = minor - 1;
        if (ap >= 0 && minor == MNRKIND_AIRRIDECHECKLIST)
            prev = ap;
        else if (ap >= 0 && minor == ap)
            prev = MNRKIND_CITYCHECKLIST;
        else if (prev < MNRKIND_AIRRIDECHECKLIST)
            prev = MNRKIND_CITYCHECKLIST; // vanilla wrap
        Scene_SetNextMinor(prev);
        Scene_ExitMinor();
        break;
    }

    case 14: // switch to the records screen (no AP records: no-op on the AP tab)
        if (minor == ap)
            break;
        g_ap_postrun = 0; // leaving the checklist; don't carry the post-run chain
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

// Lay out the AP checklist's clear data. grid_mapping must be a full bijection over
// all 120 clear_kinds: Checklist_Update reverse-scans it to map a cursor position
// back to a clear_kind, and an unmapped position trips the "Clearchecker Number 120"
// assert. Vanilla builds this at save-init (Checklist_InitGridMapping); the AP mode
// has none, so use the identity map (clear_kind k at position k). is_visible gates
// which cells draw, so only the defined checks (kinds 0..N-1) appear, rest blank.
static void APChecklist_InitClearData(void)
{
    for (int k = 0; k < CLEAR_KIND_NUM; k++)
    {
        AP_CLEAR->grid_mapping[k] = (u8)k;
        *((u8 *)&AP_CLEAR->clear[k]) = 0;
    }
    for (int i = 0; i < AP_CHECK_NUM; i++)
    {
        int ck = ap_checks[i].clear_kind;
        if (ck < 0 || ck >= CLEAR_KIND_NUM)
            continue;
        AP_CLEAR->clear[ck].is_visible = 1;
    }
}

// Register the AP tab as a new minor scene by cloning the City Trial checklist
// descriptor and overriding its cb_Load. Reachable via Scene_SetNextMinor once
// folded into the cycle above.
static void APChecklist_InstallMinor(void)
{
    MinorSceneDesc *descs = Hoshi_GetMinorScenes();
    MinorSceneDesc ap_desc = descs[MNRKIND_CITYCHECKLIST];
    ap_desc.cb_Load = APChecklist_MinorLoad;
    g_ap_minor_id = (int)(s8)Hoshi_InstallMinorScene(&ap_desc);
    OSReport("[APChecklist] Installed AP tab as minor scene id=%d\n", g_ap_minor_id);
}

// Blue theme recolor: the AP tab borrows City Trial's green tint (in the background
// scene's material diffuses at MainMenuData + 0xED0), which the menu's anim pass
// re-applies every frame - so this runs each frame from OnFrameEnd, rotating each
// diffuse (R,G,B) -> (B,R,G), gated on green-dominant so blue results don't re-rotate.
#define MMD_BASE 0x80558788     // MainMenu_GetData() (0x801311e0) return value

// The built scene GObjs whose materials carry the per-mode tint: the background
// scene (+0xED0) plus the small marker/counter models in the +0x1100 range. The
// frame border (+0xEE4) is texture-colored (white material) and not in this set.
static const int g_recolor_slots[] = { 0xED0, 0x1104, 0x110C, 0x1114 };
#define AP_RECOLOR_SLOT_NUM ((int)(sizeof(g_recolor_slots) / sizeof(g_recolor_slots[0])))

// The per-mode banner is a single 248x128 quad at MainMenuData + 0xEE4 that backs
// the grid and scrolls via a TObj AOBJ; we repoint it at the AP watermark. Its 248
// width is unique in the scene, which is how RetargetBannerJObj finds it.
#define MMD_FRAME_SLOT 0xEE4
#define AP_BANNER_TEX_W 248

// The tab emblem (top-right, between the L/R arrows) is a per-mode silhouette: a
// single 40x40 I4 quad, unique in the background scene and distinct from the 72x72
// circle layers. We repoint just that quad at the AP emblem; circle and tint stay.
#define AP_EMBLEM_TEX_W 40
#define AP_EMBLEM_TEX_FMT 0  // I4

// Rotate a green-dominant GXColor (RGBA at HSD_Material + 0x4): (R,G,B)->(B,R,G).
static void APChecklist_RotateDiffuse(u8 *mat)
{
    u8 r = mat[4], g = mat[5], b = mat[6];
    if (g > r && g >= b) // green-dominant only
    {
        mat[4] = b;
        mat[5] = r;
        mat[6] = g;
    }
}

// Blue-tint one JOBJ's dobjs (rotate each diffuse green->blue) and, in the same
// pass, swap the mode emblem's silhouette quad (the unique 40x40 I4 texture) to the
// AP logo. The emblem lives in the recolored background scene, so this rides the
// existing recolor walk; the 40x40/I4 signature is unique there, so nothing else
// is touched and the circle layers around it are left alone.
static void APChecklist_ProcessJObj(u8 *j)
{
    for (u8 *d = *(u8 **)(j + 0x18); d; d = *(u8 **)(d + 0x04)) // JOBJ.dobj, DOBJ.next
    {
        u8 *m = *(u8 **)(d + 0x08); // DOBJ.mobj
        if (!m)
            continue;
        u8 *mat = *(u8 **)(m + 0x0C); // MOBJ.mat
        if (mat)
            APChecklist_RotateDiffuse(mat);
        if (!ap_emblem_imagedesc)
            continue; // textures not loaded; recolor only, leave the vanilla emblem
        for (u8 *t = *(u8 **)(m + 0x08); t; t = *(u8 **)(t + 0x08)) // MOBJ.tobj, TOBJ.next
        {
            u8 *img = *(u8 **)(t + 0x58); // TOBJ.imagedesc
            if (!img || img == (u8 *)ap_emblem_imagedesc)
                continue;
            if (*(u16 *)(img + 0x04) != AP_EMBLEM_TEX_W ||
                *(u32 *)(img + 0x08) != AP_EMBLEM_TEX_FMT)
                continue;
            // The vanilla emblem is a texture flipbook (TObj.aobj + imagetbl): its
            // anim pass rewrites imagedesc every tick, fighting our swap. Clear both
            // so our descriptor is the only thing bound; the material tint still animates.
            *(void **)(t + 0x58) = ap_emblem_imagedesc;
            *(void **)(t + 0x64) = 0; // TOBJ.aobj
            *(void **)(t + 0x68) = 0; // TOBJ.imagetbl
        }
    }
}

// Walk a JOBJ subtree (child + sibling). JOBJ.child=+0x10, sibling=+0x08.
static void APChecklist_RecolorJObj(u8 *j, int depth)
{
    if (!j || depth > 32)
        return;
    APChecklist_ProcessJObj(j);
    APChecklist_RecolorJObj(*(u8 **)(j + 0x10), depth + 1);
    APChecklist_RecolorJObj(*(u8 **)(j + 0x08), depth + 1);
}

// Recolor one built scene GObj: its root's own dobjs plus its child subtree, but
// not the root's sibling (which would leave this scene).
static void APChecklist_RecolorGObj(u8 *gobj)
{
    if (!gobj)
        return;
    u8 *jroot = *(u8 **)(gobj + 0x28); // GObj hsd_object -> scene JOBJ root
    if (!jroot)
        return;
    APChecklist_ProcessJObj(jroot);
    APChecklist_RecolorJObj(*(u8 **)(jroot + 0x10), 0);
}

// Retarget the banner quad on one JOBJ: a TObj pointing at the 248-wide banner
// texture (or our descriptor, once swapped) is repointed at the AP watermark. The
// JOBJ scale and the quad's scroll are untouched, so the panel keeps its footprint
// and animation. The material diffuse is forced white so the texture samples
// neutrally. Offsets: MOBJ.tobj=+0x08; TOBJ.next=+0x08, imagedesc=+0x58;
// _HSD_ImageDesc.width=+0x04; HSD_Material+0x4 = diffuse.
static void APChecklist_RetargetBannerJObj(u8 *j)
{
    if (!ap_logo_imagedesc)
        return; // textures not loaded; leave the vanilla banner art in place

    for (u8 *d = *(u8 **)(j + 0x18); d; d = *(u8 **)(d + 0x04))
    {
        u8 *m = *(u8 **)(d + 0x08);
        if (!m)
            continue;
        u8 *mat = *(u8 **)(m + 0x0C);
        for (u8 *t = *(u8 **)(m + 0x08); t; t = *(u8 **)(t + 0x08))
        {
            u8 *img = *(u8 **)(t + 0x58);
            if (!img)
                continue;
            int already = (img == (u8 *)ap_logo_imagedesc);
            if (!already && *(u16 *)(img + 0x04) != AP_BANNER_TEX_W)
                continue;
            if (!already)
                *(void **)(t + 0x58) = ap_logo_imagedesc;
            if (mat)
            {
                mat[4] = 0xFF; // diffuse R
                mat[5] = 0xFF; // diffuse G
                mat[6] = 0xFF; // diffuse B (alpha untouched, keeps the quad's blend)
            }
        }
    }
}

// Walk the banner GObj's JOBJ tree, retargeting its scrolling quad to the AP logo.
static void APChecklist_RetargetBanner(u8 *gobj)
{
    if (!gobj)
        return;
    u8 *jroot = *(u8 **)(gobj + 0x28);
    for (u8 *stack[40], **sp = stack, *j = jroot; ; )
    {
        while (j)
        {
            APChecklist_RetargetBannerJObj(j);
            if (sp < stack + 40)
                *sp++ = *(u8 **)(j + 0x08); // defer sibling
            j = *(u8 **)(j + 0x10);          // descend child
        }
        if (sp == stack)
            break;
        j = *--sp;
    }
}

// Recolor the AP tab's scene to blue and retarget its scrolling banner artwork.
// No-op unless the AP tab is the current minor scene. Idempotent per frame, so
// safe to run every frame (the menu's anim pass re-applies the green tint each
// frame).
static void APChecklist_RecolorScene(void)
{
    if (g_ap_minor_id < 0 || Scene_GetCurrentMinor() != g_ap_minor_id)
        return;

    for (int i = 0; i < AP_RECOLOR_SLOT_NUM; i++)
        APChecklist_RecolorGObj(*(u8 **)(MMD_BASE + g_recolor_slots[i]));

    APChecklist_RetargetBanner(*(u8 **)(MMD_BASE + MMD_FRAME_SLOT));

    // GX caches texel data in TMEM, so the swapped banner/emblem render from a stale
    // cache without a per-frame invalidate while the AP tab is up.
    GXInvalidateTexAll();
}

void APChecklist_OnFrameEnd(void)
{
    // Tint the AP tab blue. Runs after the menu's material-animation pass has
    // re-applied the green tint this frame (cheap no-op when off the AP tab).
    APChecklist_RecolorScene();
}

void APChecklist_OnFrameStart(void)
{
    // Wait for the save to load and the textbox API to resolve (RecordCheck
    // enqueues a textbox). game_ready is set at the end of OnSaveLoaded.
    if (!ap_data || !ap_data->game_ready)
        return;

    for (int i = 0; i < AP_CHECK_NUM; i++)
    {
        const APCheck *c = &ap_checks[i];
        int ck = c->clear_kind;
        if (ck < 0 || ck >= CLEAR_KIND_NUM)
            continue;

        if (!APChecklist_IsRecorded(ck))
        {
            // Not yet sent: complete it the first frame the predicate holds.
            if (!c->is_complete || !c->is_complete())
                continue;

            // Drive the unlock like a vanilla checklist. ClearChecker_SetNewUnlock
            // routes through check_detection's replacement (RecordCheck: sets
            // sent_checks_ap, fires the "Check sent" textbox, re-evaluates goals); during
            // a gamemode the unlock cache is invalid, so it also sets clear[].is_new and
            // plays the unlock SFX, and Checklist_ProcessUnlock animates the cell on the
            // next checklist entry.
            ClearChecker_SetNewUnlock(AP_CHECKLIST_MODE, (u8)ck);

            // A check satisfied outside any gamemode (e.g. "Boot the game") hits the
            // cache-valid short-circuit, so SetNewUnlock leaves is_new unset. Seed it here
            // so the same entry animation fires as if the cell cleared during a run.
            AP_CLEAR->clear[ck].is_new = 1;
            AP_CLEAR->clear[ck].is_visible = 1;
        }
        else
        {
            // Already sent. Keep it rendered. A still-pending is_new (completed this
            // session, not yet viewed) is left for Checklist_ProcessUnlock to animate -
            // it raises is_unlocked itself once shown. Only force is_unlocked when no
            // animation is pending, so a check recorded on a prior boot (AP_CLEAR is
            // BSS-zeroed at boot) shows complete with no replay.
            AP_CLEAR->clear[ck].is_visible = 1;
            if (!AP_CLEAR->clear[ck].is_new)
                AP_CLEAR->clear[ck].is_unlocked = 1;
        }
    }
}

void APChecklist_OnBoot(void)
{
    CODEPATCH_REPLACEFUNC(gmGetClearcheckerTypeP, APChecklist_GetClearcheckerTypeP);
    CODEPATCH_REPLACEFUNC(Checklist_GetRewardNum, APChecklist_GetRewardNum);
    CODEPATCH_REPLACEFUNC(Checklist_GetClearKindFromRewardIndex, APChecklist_GetClearKindFromRewardIndex);
    CODEPATCH_REPLACEFUNC(Checklist_MinorThink, APChecklist_MinorThink);
    CODEPATCH_REPLACEFUNC(ClearChecker_CheckForNewUnlocks, APChecklist_CheckForNewUnlocks);
    CODEPATCH_REPLACEFUNC(Scene_SetNextMinor, APChecklist_SetNextMinor);

    APChecklist_InitClearData();
    APChecklist_InstallMinor();

    OSReport("[APChecklist] Hooks installed (mode %d, %d custom checks)\n",
             AP_CHECKLIST_MODE, AP_CHECK_NUM);
}
