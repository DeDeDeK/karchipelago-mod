#include <string.h>

#include "game.h"
#include "os.h"
#include "obj.h"
#include "rider.h"
#include "item.h"
#include "inline.h"
#include "code_patch/code_patch.h"
#include "hoshi/mod.h"

#include "custom_items_api.h"

#include "ap_star.h"
#include "ap_star_pieces.h"
#include "ap_star_piece_hud.h"

// CustomItemDesc.name of each sphere, indexed by APStarPieceKind. The name is what
// binds a drop-in .dat to this code.
static const char *const piece_names[APSTARPIECE_NUM] = {
    "AP Sphere Rose",
    "AP Sphere Green",
    "AP Sphere Violet",
    "AP Sphere Tan",
    "AP Sphere Blue",
    "AP Sphere Yellow",
};

// Progress window, in percent of the round, that the n-th delivery step draws its
// threshold from. A round with fewer spheres in play uses the first rows, keeping
// the deliveries early.
static const u8 piece_progress_range[APSTARPIECE_NUM][2] = {
    { 10, 20 }, { 20, 32 }, { 32, 45 }, { 45, 58 }, { 58, 70 }, { 70, 85 },
};

#define AP_STAR_PIECE_ALL ((1u << APSTARPIECE_NUM) - 1)

#define AP_STAR_HANDLER_MAX 4

static const CustomItemsAPI *ci_api;

static u32 piece_gate = AP_STAR_PIECE_ALL; // one bit per APStarPieceKind
static ApStarAssembleFn assemble_handlers[AP_STAR_HANDLER_MAX];

static u32 piece_hash[APSTARPIECE_NUM]; // 0 for a sphere with no archive
static int piece_kind[APSTARPIECE_NUM] = { -1, -1, -1, -1, -1, -1 }; // this round's ItemKind

static int round_live;         // a City Trial round, attract demo excluded, is loaded
static u8 piece_mask[PLY_NUM]; // per-player collected spheres
static u8 assembled_mask;      // per-player assembly this round

// This round's delivery schedule, mirroring LegendaryPieceData's shape: a spawn
// order, one progress threshold per step, and a one-tick request flag the carrier
// box path reads back. Only the spheres in play this round are in it.
static struct
{
    u8 next;
    u8 num;
    u8 req_spawn;
    u8 order[APSTARPIECE_NUM];
    float progress[APSTARPIECE_NUM];
} sched;

const char *ApStarPieces_GetName(int piece)
{
    if (piece < 0 || piece >= APSTARPIECE_NUM)
        return "Unknown Sphere";
    return piece_names[piece];
}

void ApStarPieces_SetGate(u32 mask)
{
    mask &= AP_STAR_PIECE_ALL;
    if (mask == piece_gate)
        return;

    piece_gate = mask;
    OSReport("[ApStarPieces] Sphere gate set (mask = %s)\n", MaskBits(mask, APSTARPIECE_NUM));
}

void ApStarPieces_AddAssembleHandler(ApStarAssembleFn fn)
{
    if (fn == NULL)
        return;
    for (int i = 0; i < AP_STAR_HANDLER_MAX; i++)
    {
        if (assemble_handlers[i] == fn)
            return;
    }
    for (int i = 0; i < AP_STAR_HANDLER_MAX; i++)
    {
        if (assemble_handlers[i] == NULL)
        {
            assemble_handlers[i] = fn;
            return;
        }
    }
    OSReport("[ApStarPieces] Assemble handler list full\n");
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

// Match each sphere .dat to its slot by display name. custom_items discovers its
// archives at boot, so one scan sees every one there will be.
static void ResolvePieces(void)
{
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
    if (found != APSTARPIECE_NUM)
        OSReport("[ApStarPieces] Only %d of %d sphere items found in items/\n",
                 found, APSTARPIECE_NUM);
}

static void Assemble(int ply)
{
    assembled_mask |= (u8)(1 << ply);
    piece_mask[ply] = 0;

    // The cinematic owns the mount and the completion sounds; with none, both are owed
    // here. Rung 4 is the pair of sounds a machine completes on, not a fourth piece.
    // Re-mounting a player already riding the star costs them their patches, as vanilla
    // does on a duplicate Hydra set.
    if (!ApStar_StartAssembly(ply))
    {
        if (!ApStar_Mount(ply))
            OSReport("[ApStarPieces] Player %d not mounted: no %s registered\n",
                     ply + 1, AP_STAR_MACHINE_NAME);
        Ply_OnLegendaryPieceCollect(ply, 4);
    }

    for (int i = 0; i < AP_STAR_HANDLER_MAX; i++)
    {
        if (assemble_handlers[i] != NULL)
            assemble_handlers[i](ply);
    }
    OSReport("[ApStarPieces] Player %d assembled the %s\n", ply + 1, AP_STAR_MACHINE_NAME);
}

static void Collect(int player, int slot)
{
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

static void OnPickup(u32 id_hash, const char *name, int player)
{
    (void)name;
    int slot = PieceSlotForHash(id_hash);
    if (slot >= 0)
        Collect(player, slot);
}

// The kind the candidate roll produces when its list is empty: the array is seeded to
// -1 and 0x37 is added unconditionally. Vanilla never throws it because the quota is
// zero when no piece is held; adding spheres to the quota makes it reachable, so it is
// read here as "the rider holds no vanilla piece".
#define AP_DROP_NO_VANILLA (ITKIND_HYDRA1 - 1)

// The rider's spheres that have an ItemKind this round. A sphere collected through
// CollectPiece while its gate was shut has none, so nothing can throw it - and putting
// it in the pool would throw AP_DROP_NO_VANILLA, an ordinary Gordo, and leave the bit
// set so the quota never drains.
static u8 DroppableSpheres(int ply)
{
    u8 mask = 0;
    for (int i = 0; i < APSTARPIECE_NUM; i++)
    {
        if ((piece_mask[ply] & (1 << i)) && piece_kind[i] >= 0)
            mask |= (u8)(1 << i);
    }
    return mask;
}

static int SphereSlotForKind(int kind)
{
    if (kind < 0)
        return -1;
    for (int i = 0; i < APSTARPIECE_NUM; i++)
    {
        if (piece_kind[i] == kind)
            return i;
    }
    return -1;
}

// REPLACECALL on the bl Ply_GetDragoonCollection in Rider_DropPatches (0x8019d330). The
// sum of the two collection counts is the rider's legendary-drop quota, and
// allups_dropped is capped against it - so without the spheres in that sum a rider
// holding nothing else queues no drop and Rider_TickDropAllUp is never dispatched.
static int DropQuotaDragoon(int ply)
{
    return Ply_GetDragoonCollection(ply) + Popcount64(DroppableSpheres(ply));
}

// HOOKCREATE on the stw of the kind about to be thrown, in Rider_TickDropAllUp
// (0x8019d55c). r0 holds that kind and r30 the RiderData. Vanilla has already rolled
// uniformly over the pieces in its two masks; re-rolling over those plus the rider's
// droppable spheres keeps every piece that can be thrown equally likely to come out.
static int PickDropKind(int kind, RiderData *rd)
{
    int ply = rd->ply;
    u8 mask = DroppableSpheres(ply);
    int spheres = Popcount64(mask);
    if (spheres == 0)
    {
        // The quota and the masks drain together, so this only reads as "the rider holds
        // nothing" if they ever desync. Returning -1 takes vanilla's own bail rather than
        // letting AP_DROP_NO_VANILLA reach the throw.
        return kind == AP_DROP_NO_VANILLA ? -1 : kind;
    }

    int vanilla = 0;
    if (kind != AP_DROP_NO_VANILLA)
        vanilla = Popcount64(Ply_GetHydraPieceMask(ply) & 7) +
                  Popcount64(Ply_GetDragoonPieceMask(ply) & 7);
    if (HSD_Randi(vanilla + spheres) < vanilla)
        return kind;

    int pick = HSD_Randi(spheres);
    for (int i = 0; i < APSTARPIECE_NUM; i++)
    {
        if (!(mask & (1 << i)))
            continue;
        if (pick-- == 0)
            return piece_kind[i];
    }
    return kind;
}

// REPLACECALL on the bl Ply_DecrementItemCollectNum in Rider_TickDropAllUp (0x8019d55c).
// item_collect is 0x45 entries indexed by ItemKind and the callee bounds-checks neither
// end, so a sphere's kind would write past PlayerStats. The pickup counted the clamped
// base kind, so the drop takes it back off the same slot.
static void DropDecrementCollect(int ply, ItemKind kind)
{
    if (SphereSlotForKind(kind) >= 0)
        kind = ITKIND_HYDRA1;
    Ply_DecrementItemCollectNum(ply, kind);
}

// HOOKCONDITIONALCREATE on the lwz that reloads the thrown kind for the mask clear, in
// Rider_TickDropAllUp (0x8019d55c); r30 holds the RiderData. Both vanilla branches XOR a
// mask by 1 << (kind - 0x37) or 1 << (kind - 0x3a); a sphere kind runs both off the end,
// so it clears its own bit and exits past them to the cooldown reset.
static int DropClearSphere(RiderData *rd)
{
    int slot = SphereSlotForKind(rd->drop_piece_kind);
    if (slot < 0)
        return 0;

    piece_mask[rd->ply] &= (u8)~(1 << slot);
    OSReport("[ApStarPieces] Player %d dropped %s\n", rd->ply + 1, piece_names[slot]);
    return 1;
}

CODEPATCH_HOOKCREATE(0x8019d868,
    "mr 3, 0\n\t"
    "mr 4, 30\n\t",
    PickDropKind,
    "mr 0, 3\n\t",
    0
)

CODEPATCH_HOOKCONDITIONALCREATE(0x8019d8f8,
    "mr 3, 30\n\t",
    DropClearSphere,
    "",
    0,           // not a sphere: run the lwz and take vanilla's mask branch
    0x8019d950   // sphere: skip both branches, resume at the cooldown reset
)

// REPLACECALL on the bl in CityItemSpawn_UpdateAndCheckToSpawn (0x800ea6e0). Vanilla
// returns 2 when one of its own pieces wants the next carrier box and 3 when neither
// does; the AP set only claims a carrier vanilla passed on.
static int CheckToSpawn(float progress)
{
    int ret = CityItemSpawn_CheckToSpawnLegendaryPiece(progress);

    sched.req_spawn = 0;
    if (ret != 3 || sched.next >= sched.num)
        return ret;
    if (progress <= sched.progress[sched.next])
        return ret;

    sched.req_spawn = 1;
    return 2;
}

// REPLACECALL on the bl in CityItemSpawn_Think (0x800eb108), which has just spawned the
// red carrier box. Writing forced_item is all it takes to make that box hold a piece.
static void SpawnPiece(GOBJ *box, int area, int p3)
{
    if (!sched.req_spawn)
    {
        CityItemSpawn_SpawnLegendaryPiece(box, area, p3);
        return;
    }

    sched.req_spawn = 0;
    if (sched.next >= sched.num)
        return;

    int piece = sched.order[sched.next];
    int kind = piece_kind[piece];
    LegendaryPiece_MarkAsSpawned(box, kind);
    sched.next++;
    LegendaryPiece_ClearSpawnRequest(box);
    OSReport("[ApStarPieces] %s loaded into a carrier box (kind %d)\n",
             piece_names[piece], kind);
}

void ApStarPieces_OnBoot(void)
{
    CODEPATCH_REPLACECALL(0x800ea7e0, CheckToSpawn);  // bl CityItemSpawn_CheckToSpawnLegendaryPiece
    CODEPATCH_REPLACECALL(0x800eb27c, SpawnPiece);    // bl CityItemSpawn_SpawnLegendaryPiece
    CODEPATCH_REPLACECALL(0x8019d4bc, DropQuotaDragoon);      // bl Ply_GetDragoonCollection in Rider_DropPatches
    CODEPATCH_REPLACECALL(0x8019d8d4, DropDecrementCollect);  // bl Ply_DecrementItemCollectNum in Rider_TickDropAllUp
    CODEPATCH_HOOKAPPLY(0x8019d868);  // sphere into the drop candidate roll
    CODEPATCH_HOOKAPPLY(0x8019d8f8);  // sphere out of the collected mask
    OSReport("[ApStarPieces] Spawn and drop hooks installed\n");
}

void ApStarPieces_On3DLoadStart(void)
{
    // Tried once, past every mod's OnBoot: a build without custom_items would warn on
    // every 3D scene.
    static int tried;
    if (!tried)
    {
        tried = 1;
        ci_api = (const CustomItemsAPI *)Hoshi_ImportMod(
            (char *)CUSTOM_ITEMS_MOD_NAME, CUSTOM_ITEMS_API_MAJOR, CUSTOM_ITEMS_API_MINOR);
        if (ci_api != NULL)
        {
            ci_api->AddPickupHandler(OnPickup);
            ResolvePieces();
        }
    }
    if (ci_api == NULL)
        return;

    // A closed sphere is held out of the registry entirely, so it has no ItemKind and
    // no path can spawn it. The attract demo is held out the same way - it is a real
    // City Trial round with a CPU in every slot.
    int demo = Gm_IsAutoDemo();
    for (int i = 0; i < APSTARPIECE_NUM; i++)
    {
        if (piece_hash[i] != 0)
            ci_api->SetEnabled(piece_hash[i], !demo && (piece_gate & (1u << i)));
    }
}

// The HUD GObjs, the icon archive and the item kinds all belong to the scene the heap
// reset just freed.
void ApStarPieces_OnSceneChange(void)
{
    round_live = 0;
    memset(piece_mask, 0, sizeof(piece_mask));
    assembled_mask = 0;
    memset(&sched, 0, sizeof(sched));
    for (int i = 0; i < APSTARPIECE_NUM; i++)
        piece_kind[i] = -1;
    ApStarPieceHud_OnSceneChange();
}

void ApStarPieces_On3DLoadEnd(void)
{
    if (Gm_IsAutoDemo() || !Gm_IsInCity() || Gm_GetCityMode() != CITYMODE_TRIAL)
        return;

    // Built whether or not a sphere is armed: CollectPiece lands one regardless of the gate.
    round_live = 1;
    ApStarPieceHud_Load();
    if (ci_api == NULL)
        return;

    // custom_items assigns the kinds in CityItemSpawn_Init, so they are valid only
    // from here and only for this scene. A closed sphere has no kind and takes no
    // delivery step.
    int num = 0;
    for (int i = 0; i < APSTARPIECE_NUM; i++)
    {
        if (piece_hash[i] == 0)
            continue;
        piece_kind[i] = ci_api->GetAssignedKind(piece_hash[i]);
        if (piece_kind[i] >= 0)
            sched.order[num++] = (u8)i;
    }
    sched.num = (u8)num;
    if (num == 0)
    {
        OSReport("[ApStarPieces] 0 of %d spheres armed\n", APSTARPIECE_NUM);
        return;
    }

    // Shuffle, as vanilla rotates which of a machine's three parts comes first.
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
    OSReport("[ApStarPieces] %d of %d spheres armed, first at %d%% of the round\n",
             num, APSTARPIECE_NUM, (int)(sched.progress[0] * 100.0f));
}

void ApStarPieces_OnFrameStart(void)
{
    for (int ply = 0; ply < PLY_NUM; ply++)
        ApStarPieceHud_Update(ply, piece_mask[ply]);
}

// Far enough ahead that the rider drives into the sphere rather than spawning on it.
#define AP_PIECE_SPAWN_FORWARD 10.0f

int ApStarPieces_SpawnPiece(int piece, int ply)
{
    if (piece < 0 || piece >= APSTARPIECE_NUM || ply < 0 || ply >= PLY_NUM || !round_live)
        return 0;

    // The registry is only written at CityItemSpawn_Init, so opening a gate mid-round
    // is not enough to spawn its sphere.
    int kind = piece_kind[piece];
    if (kind < 0)
    {
        OSReport("[ApStarPieces] %s is not registered this round\n", piece_names[piece]);
        return 0;
    }

    GOBJ *mg = Ply_GetMachineGObj(ply);
    if (mg == NULL)
    {
        OSReport("[ApStarPieces] Player %d has no machine to spawn in front of\n", ply + 1);
        return 0;
    }

    MachineData *md = mg->userdata;
    Vec3 pos;
    pos.X = md->pos.X + AP_PIECE_SPAWN_FORWARD * md->forward.X;
    pos.Y = md->pos.Y + AP_PIECE_SPAWN_FORWARD * md->forward.Y;
    pos.Z = md->pos.Z + AP_PIECE_SPAWN_FORWARD * md->forward.Z;

    ItemDesc desc;
    Item_InitDesc(&desc, (ItemKind)kind, 1.0f, 0, &pos, &md->up, &md->forward,
                  -1, -1, 1, 3, -1, -1);
    CityItem_Create(&desc);
    OSReport("[ApStarPieces] Spawned %s for player %d (kind %d)\n",
             piece_names[piece], ply + 1, kind);
    return 1;
}

int ApStarPieces_CollectPiece(int piece, int ply)
{
    if (piece < 0 || piece >= APSTARPIECE_NUM || ply < 0 || ply >= PLY_NUM || !round_live)
        return 0;
    if (Ply_GetRiderGObj(ply) == NULL)
        return 0;

    Collect(ply, piece);
    return 1;
}

int ApStarPieces_Assemble(int ply)
{
    if (ply < 0 || ply >= PLY_NUM || !round_live)
        return 0;
    if (ApStar_MachineKind() < 0 || Ply_GetRiderGObj(ply) == NULL)
        return 0;

    Assemble(ply);
    return 1;
}

int ApStarPieces_AssembledThisRound(int ply)
{
    if (ply < 0 || ply >= PLY_NUM)
        return 0;
    return (assembled_mask >> ply) & 1;
}
