#include <string.h>

#include "game.h"
#include "os.h"
#include "obj.h"
#include "hud.h"
#include "rider.h"
#include "item.h"
#include "inline.h"
#include "code_patch/code_patch.h"
#include "hoshi/mod.h"

#include "custom_items_api.h"

#include "main.h"
#include "ap_check_detect.h"
#include "ap_star_pieces.h"
#include "textbox_api.h"

// CustomItemDesc.name of each sphere, indexed by APStarPieceKind. The name is the
// handle that binds a drop-in .dat to this code, the same convention
// AP_STAR_MACHINE_NAME uses for the machine.
static const char *const piece_names[APSTARPIECE_NUM] = {
    "AP Sphere Rose",
    "AP Sphere Green",
    "AP Sphere Violet",
    "AP Sphere Tan",
    "AP Sphere Blue",
    "AP Sphere Yellow",
};

// Match progress (0..1) windows the n-th delivery step falls in. Vanilla spreads
// three pieces over 15-30 / 25-50 / 50-80 percent of a round; these six divide the
// same span so a full set lands inside one round. A round with fewer spheres
// unlocked uses the first rows, keeping the deliveries early.
static const u8 piece_progress_range[APSTARPIECE_NUM][2] = {
    { 10, 20 }, { 20, 32 }, { 32, 45 }, { 45, 58 }, { 58, 70 }, { 70, 85 },
};

static const CustomItemsAPI *ci_api;
static int pickup_registered;

static u32 piece_hash[APSTARPIECE_NUM]; // 0 until the registry has been scanned
static int piece_kind[APSTARPIECE_NUM]; // ItemKind assigned this round, -1 if none
static int pieces_matched = -1;         // -1 before the first scan, then the match count

static u8 piece_mask[5];      // per-player collected spheres, cleared each round
static u8 assembled_mask;     // per-player assembly this round, cleared each round
static u8 ever_assembled;     // sticky for the boot; the checklist predicate reads it

// This round's delivery schedule, mirroring LegendaryPieceData's shape: a spawn
// order, one progress threshold per step, and a one-tick request flag the carrier
// box path reads back. Only the spheres unlocked this round are in it.
static struct
{
    u8 enabled;
    u8 next;
    u8 num;
    u8 req_spawn;
    u8 order[APSTARPIECE_NUM];
    float progress[APSTARPIECE_NUM];
} sched;

// Step whose carrier claim has been reported, +1. A claim repeats every tick until
// a carrier actually spawns, so the report is held to one per step.
static u8 claim_logged;

// The collection tracker, built the way the vanilla one is: an icon element per
// collected piece, hung on the anchors of the legendary HUD's position model and
// packed left to right in collection order. It is a second row rather than a
// share of the vanilla one, since that row's six anchors are already spoken for by
// Hydra and Dragoon, so the AP row sits one icon height below it.
#define AP_PIECE_HUD_KIND    0x3b
#define AP_PIECE_HUD_ROW_DY  -3.4f

static JOBJSet **icon_sets;
static Vec3 anchor_pos[APSTARPIECE_NUM];
static int anchors_valid;

static struct
{
    GOBJ *icon[APSTARPIECE_NUM];
    u8 count;
    u8 shown_mask;
} piece_hud[5];

// Players whose mount is owed. Collection lands inside Machine_OnTouchItem, and
// the mount tears down the machine that call is running on, so it waits for the
// frame boundary - the same deferral the vanilla assembly gets from running its
// mount out of the cinematic's own proc rather than the pickup arm.
static u8 pending_mount_mask;

static int IsPieceUnlocked(int piece)
{
    return (ap_save->ap_star_piece_unlocked_mask & (1u << piece)) != 0;
}

int ApStarPieces_UnlockPiece(int piece)
{
    if (piece < 0 || piece >= APSTARPIECE_NUM)
        return 0;

    ap_save->ap_star_piece_unlocked_mask |= (u8)(1 << piece);
    OSReport("[ApStarPieces] %s unlocked (mask = %s)\n", piece_names[piece],
             MaskBits(ap_save->ap_star_piece_unlocked_mask, APSTARPIECE_NUM));
    tb_api->EnqueueColoredNoun("Unlocked Item: ", piece_names[piece], tb_api->ItemColor, NULL);
    return 1;
}

static int PieceSlotForHash(u32 id_hash)
{
    for (int i = 0; i < APSTARPIECE_NUM; i++)
    {
        if (piece_hash[i] != 0 && piece_hash[i] == id_hash)
            return i;
    }
    return -1;
}

// Match each sphere .dat to its slot by display name. The hashes are stable
// across scenes, so a complete set is scanned for once; a short one is rescanned
// and reported only when the count moves.
static void ResolvePieces(void)
{
    if (ci_api == NULL || pieces_matched == APSTARPIECE_NUM)
        return;

    for (int i = 0; i < ci_api->GetCount(); i++)
    {
        const char *name = ci_api->GetName(i);
        if (name == NULL)
            continue;
        for (int p = 0; p < APSTARPIECE_NUM; p++)
        {
            if (piece_hash[p] == 0 && strcmp(name, piece_names[p]) == 0)
            {
                piece_hash[p] = ci_api->GetIdHash(i);
                break;
            }
        }
    }

    int found = 0;
    for (int p = 0; p < APSTARPIECE_NUM; p++)
        found += (piece_hash[p] != 0);
    if (found == pieces_matched)
        return;

    pieces_matched = found;
    if (found != APSTARPIECE_NUM)
        OSReport("[ApStarPieces] Only %d of %d sphere items found in items/\n",
                 found, APSTARPIECE_NUM);
}

// Anchor positions come off the position model's descriptor rather than an
// instance of it: the six anchors are children of the root with no rotation and
// unit scale, so a world position is the root's translation plus the anchor's.
// That also keeps the AP row from needing a position-model element of its own.
static void ReadAnchors(void)
{
    anchors_valid = 0;

    Game3dData *g3d = Gm_Get3dData();
    if (g3d == NULL || g3d->legendary_hud_pos == NULL || g3d->legendary_hud_pos[0] == NULL)
        return;

    JOBJDesc *root = g3d->legendary_hud_pos[0]->jobj;
    if (root == NULL)
        return;

    JOBJDesc *anchor = root->child;
    for (int i = 0; i < APSTARPIECE_NUM; i++)
    {
        if (anchor == NULL)
            return;
        anchor_pos[i].X = root->position.X + anchor->position.X;
        anchor_pos[i].Y = root->position.Y + anchor->position.Y + AP_PIECE_HUD_ROW_DY;
        anchor_pos[i].Z = root->position.Z + anchor->position.Z;
        anchor = anchor->next;
    }
    anchors_valid = 1;
}

static int ViewForPly(int ply)
{
    Game3dData *g3d = Gm_Get3dData();
    if (g3d == NULL)
        return 0;
    for (int v = 0; v < 4; v++)
    {
        if (g3d->plyview_lookup[v] == (s8)ply)
            return v;
    }
    return 0;
}

// One icon element per collected sphere, created the way the vanilla piece icons
// are: a player HUD element on the pause-HUD p_link, positioned at its anchor.
static void ShowPieceIcon(int ply, int piece)
{
    int slot = piece_hud[ply].count;
    if (slot >= APSTARPIECE_NUM || piece_hud[ply].icon[slot] != NULL)
        return;
    if (icon_sets[piece] == NULL || icon_sets[piece]->jobj == NULL)
        return;

    GOBJ *g = HUD_CreateElement(ply, icon_sets[piece]->jobj);
    if (g == NULL)
        return;
    GObj_SetPLink(g, GAMEPLINK_PAUSEHUD, 0);
    HUD_AddElementData(g, AP_PIECE_HUD_KIND, ply, ViewForPly(ply));

    JOBJ *j = g->hsd_object;
    j->trans = anchor_pos[slot];
    JObj_SetMtxDirtySub(j);

    piece_hud[ply].icon[slot] = g;
    piece_hud[ply].count = (u8)(slot + 1);
}

static void ClearPieceIcons(int ply)
{
    for (int i = 0; i < APSTARPIECE_NUM; i++)
    {
        if (piece_hud[ply].icon[i] != NULL)
            GObj_Destroy(piece_hud[ply].icon[i]);
        piece_hud[ply].icon[i] = NULL;
    }
    piece_hud[ply].count = 0;
    piece_hud[ply].shown_mask = 0;
}

// The vanilla tracker updates by diffing the piece mask against a cached copy
// once a frame rather than reacting to the pickup itself; this does the same, so
// no GObj is created from inside the collision call that collected the sphere.
static void UpdatePieceHud(int ply)
{
    u8 mask = piece_mask[ply];
    if (mask == piece_hud[ply].shown_mask)
        return;

    if (mask == 0)
    {
        ClearPieceIcons(ply);
        return;
    }
    if (!anchors_valid || icon_sets == NULL)
    {
        piece_hud[ply].shown_mask = mask;
        return;
    }
    for (int p = 0; p < APSTARPIECE_NUM; p++)
    {
        u8 bit = (u8)(1 << p);
        if ((mask & bit) && !(piece_hud[ply].shown_mask & bit))
            ShowPieceIcon(ply, p);
    }
    piece_hud[ply].shown_mask = mask;
}

// Put the assembling player on the star. This is the tail of the vanilla
// assembly - the cinematic that normally runs ahead of it drives presentation
// only, and its rider mount is a two-way branch over Hydra and Dragoon.
static void MountStar(int ply)
{
    int kind = ApStarMachineKind();
    if (kind < 0 || cm_api == NULL)
    {
        OSReport("[ApStarPieces] Player %d not mounted: no %s registered\n",
                 ply + 1, AP_STAR_MACHINE_NAME);
        return;
    }

    int is_bike = 0;
    int class_index = cm_api->ClassIndexFromKind(kind, &is_bike);
    GOBJ *rg = Ply_GetRiderGObj(ply);
    if (class_index < 0 || rg == NULL)
    {
        OSReport("[ApStarPieces] Player %d not mounted: no rider or class slot\n", ply + 1);
        return;
    }

    // A recreate would tear the machine down and rebuild the same one, costing the
    // player their patches for nothing.
    if (cm_api->KindFromClassIndex(Ply_GetMachineIsBike(ply), Ply_GetMachineKind(ply)) == kind)
    {
        OSReport("[ApStarPieces] Player %d is already riding the %s\n",
                 ply + 1, AP_STAR_MACHINE_NAME);
        return;
    }

    RiderData *rd = rg->userdata;
    rd->starting_machine_idx = (MachineKind)kind;
    Rider_RespawnFullRecreate(rd, is_bike, (u8)class_index, 0, 0, 1, 0, 0);
    OSReport("[ApStarPieces] Player %d mounted the %s (kind %d, star slot %d)\n",
             ply + 1, AP_STAR_MACHINE_NAME, kind, class_index);
}

static void Assemble(int ply)
{
    pending_mount_mask |= (u8)(1 << ply);
    assembled_mask |= (u8)(1 << ply);
    piece_mask[ply] = 0;

    // The count-4 rung is the pair of sounds the vanilla cinematic plays when a
    // machine completes, not a fourth piece.
    Ply_OnLegendaryPieceCollect(ply, 4);

    if (!ever_assembled)
    {
        ever_assembled = 1;
        APCheckDetect_Observe(APCK_ASSEMBLE_AP_STAR);
    }
    OSReport("[ApStarPieces] Player %d assembled the %s\n", ply + 1, AP_STAR_MACHINE_NAME);
}

static void OnPickup(u32 id_hash, const char *name, int player)
{
    (void)name;
    if (player < 0 || player >= 5)
        return;

    int slot = PieceSlotForHash(id_hash);
    if (slot < 0)
        return;

    u8 bit = (u8)(1 << slot);
    if (piece_mask[player] & bit)
        return;
    piece_mask[player] |= bit;

    // Ply_OnLegendaryPieceCollect's ladder runs 1-2-3 over a three-piece set;
    // six pieces climb the same three rungs two at a time.
    int count = Popcount64(piece_mask[player]);
    if (count >= APSTARPIECE_NUM)
    {
        Assemble(player);
        return;
    }
    Ply_OnLegendaryPieceCollect(player, (count + 1) / 2);
}

// REPLACECALL on the bl in CityItemSpawn_UpdateAndCheckToSpawn. Vanilla returns 2
// when one of its own pieces wants the next carrier box and 3 when neither does;
// the AP set only claims a carrier vanilla passed on.
static int CheckToSpawn(float progress)
{
    int ret = CityItemSpawn_CheckToSpawnLegendaryPiece(progress);

    sched.req_spawn = 0;
    if (ret != 3 || !sched.enabled || sched.next >= sched.num)
        return ret;
    if (progress <= sched.progress[sched.next])
        return ret;

    sched.req_spawn = 1;
    if (claim_logged != (u8)(sched.next + 1))
    {
        claim_logged = (u8)(sched.next + 1);
        OSReport("[ApStarPieces] Carrier claimed for sphere %d at %d%% of the round\n",
                 sched.order[sched.next], (int)(progress * 100.0f));
    }
    return 2;
}

// REPLACECALL on the bl in CityItemSpawn_Think, which has just spawned the red
// carrier box. Writing forced_item is all it takes to make that box hold a piece.
static void SpawnPiece(int spawner, int p2, int p3)
{
    if (!sched.req_spawn)
    {
        CityItemSpawn_SpawnLegendaryPiece(spawner, p2, p3);
        return;
    }

    sched.req_spawn = 0;
    if (sched.next >= sched.num)
        return;

    int piece = sched.order[sched.next];
    int kind = piece_kind[piece];
    if (kind >= 0)
        LegendaryPiece_MarkAsSpawned(spawner, kind);
    sched.next++;
    LegendaryPiece_ClearSpawnRequest(spawner);
    OSReport("[ApStarPieces] Sphere %d loaded into a carrier box (kind %d)\n", piece, kind);
}

void ApStarPieces_OnBoot(void)
{
    for (int i = 0; i < APSTARPIECE_NUM; i++)
        piece_kind[i] = -1;

    CODEPATCH_REPLACECALL(0x800ea7e0, CheckToSpawn);  // bl CityItemSpawn_CheckToSpawnLegendaryPiece
    CODEPATCH_REPLACECALL(0x800eb27c, SpawnPiece);    // bl CityItemSpawn_SpawnLegendaryPiece
    OSReport("[ApStarPieces] Spawn hooks installed\n");
}

static void ImportRegistry(void)
{
    if (ci_api == NULL)
        ci_api = (const CustomItemsAPI *)Hoshi_ImportMod(
            (char *)CUSTOM_ITEMS_MOD_NAME, CUSTOM_ITEMS_API_MAJOR, CUSTOM_ITEMS_API_MINOR);
    if (ci_api == NULL)
        return;

    ResolvePieces();
    if (!pickup_registered && ci_api->AddPickupHandler != NULL)
    {
        ci_api->AddPickupHandler(OnPickup);
        pickup_registered = 1;
    }
}

void ApStarPieces_On3DLoadStart(void)
{
    ImportRegistry();
    if (ci_api == NULL)
        return;

    // Held out of the registry entirely until its own item arrives, so a locked
    // sphere has no ItemKind and cannot be spawned by any path.
    for (int i = 0; i < APSTARPIECE_NUM; i++)
    {
        if (piece_hash[i] != 0)
            ci_api->SetEnabled(piece_hash[i], IsPieceUnlocked(i));
    }
}

void ApStarPieces_On3DLoadEnd(void)
{
    // Every HUD GObj and the icon archive itself live in the scene heap, so the
    // handles from the previous round are already gone by now.
    for (int i = 0; i < 5; i++)
    {
        piece_mask[i] = 0;
        for (int p = 0; p < APSTARPIECE_NUM; p++)
            piece_hud[i].icon[p] = NULL;
        piece_hud[i].count = 0;
    }
    icon_sets = NULL;
    anchors_valid = 0;
    pending_mount_mask = 0;
    assembled_mask = 0;
    sched.enabled = 0;
    sched.next = 0;
    sched.num = 0;
    sched.req_spawn = 0;
    claim_logged = 0;
    for (int i = 0; i < APSTARPIECE_NUM; i++)
        piece_kind[i] = -1;

    if (ci_api == NULL || !Gm_IsInCity() || Gm_GetCityMode() != CITYMODE_TRIAL)
        return;

    // The kinds are assigned by custom_items at CityItemSpawn_Init, so they are
    // only valid from here on and only for this scene. A locked sphere was never
    // registered, so it has no kind and takes no delivery step - the set is only
    // completable once all six of its items are in.
    int num = 0;
    for (int i = 0; i < APSTARPIECE_NUM; i++)
    {
        if (piece_hash[i] == 0 || !IsPieceUnlocked(i))
            continue;
        piece_kind[i] = ci_api->GetAssignedKind(piece_hash[i]);
        if (piece_kind[i] >= 0)
            sched.order[num++] = (u8)i;
    }
    if (num == 0)
        return;
    sched.num = (u8)num;

    // Shuffle the delivery order, as vanilla rotates which of a machine's three
    // parts comes first, and draw each step's threshold from its window.
    for (int i = num - 1; i > 0; i--)
    {
        int j = HSD_Randi(i + 1);
        u8 t = sched.order[i];
        sched.order[i] = sched.order[j];
        sched.order[j] = t;
    }
    for (int i = 0; i < num; i++)
    {
        int lo = piece_progress_range[i][0];
        int hi = piece_progress_range[i][1];
        sched.progress[i] = 0.01f * (float)(lo + HSD_Randi(hi - lo));
    }
    sched.enabled = 1;

    HSD_Archive *icons = NULL;
    Gm_LoadGameFile(&icons, "ApPieceIcons");
    if (icons != NULL)
        icon_sets = Archive_GetPublicAddress(icons, "apPieceIcons_scene_models");
    ReadAnchors();

    OSReport("[ApStarPieces] %d of %d spheres armed, first at %d%% of the round, tracker %s\n",
             num, APSTARPIECE_NUM, (int)(sched.progress[0] * 100.0f),
             (icon_sets && anchors_valid) ? "ready" : "unavailable");
}

void ApStarPieces_OnFrameStart(void)
{
    for (int ply = 0; ply < 5; ply++)
    {
        if (pending_mount_mask & (1 << ply))
        {
            pending_mount_mask &= (u8)~(1 << ply);
            MountStar(ply);
        }
        UpdatePieceHud(ply);
    }
}

int ApStarPieces_WasAssembled(void)
{
    return ever_assembled;
}

int ApStarPieces_AssembledThisRound(int ply)
{
    if (ply < 0 || ply >= 5)
        return 0;
    return (assembled_mask >> ply) & 1;
}
