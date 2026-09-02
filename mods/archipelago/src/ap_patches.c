#include <string.h>

#include "game.h"
#include "os.h"
#include "obj.h"
#include "hsd.h"
#include "item.h"
#include "machine.h"
#include "particle.h"
#include "inline.h"
#include "code_patch/code_patch.h"
#include "hoshi/mod.h"

#include "custom_items_api.h"

#include "main.h"
#include "ap_item_handler.h"
#include "ap_patches.h"

// Percent of City Trial box spawns that come up as an AP Box. Matches red's share of
// the city's own 9-entry chance table (14 of 71), so it lands like a fourth color.
#define AP_BOX_PERCENT 16

// Yaw offset in degrees applied to the nth item out of a breaking box, from the
// table Box_OutcomeLogic reads at 0x80489f48. A zero offset skips the rotation.
static const float box_slot_yaw[4] = { 0.0f, 180.0f, 90.0f, -90.0f };

// The constant Box_OutcomeLogic itself uses, kept in place of MTXDegToRad so the
// scatter below reproduces the vanilla outcome.
#define BOX_DEG_TO_RAD 0.0174533f
#define BOX_SINGLE_PITCH 1.5708f // the fixed launch pitch a one-item box uses

static const CustomItemsAPI *ci_api;
static int pickup_registered;

static u32 patch_hash, box_hash; // 0 until the registry has been scanned
static int items_matched = -1;   // -1 before the first scan, then the match count

static int patch_kind = -1;      // ItemKind assigned this round, -1 if none
static int box_kind = -1;

static int round_armed;
static int boxes_rolled;

int ApPatches_CollectedCount(void)
{
    int n = 0;
    for (int w = 0; w < AP_PATCH_WORDS; w++)
        n += Popcount64(ap_save->ap_patch_collected[w]);
    return n;
}

int ApPatches_GetCount(void)
{
    int count = (int)ap_save->options.ap_patches;
    return count > AP_PATCH_MAX ? AP_PATCH_MAX : count;
}

int ApPatches_Remaining(void)
{
    int left = ApPatches_GetCount() - ApPatches_CollectedCount();
    return left > 0 ? left : 0;
}

// Claim the lowest clear bit below ap_patches. The patches are interchangeable
// in logic, so which pickup maps to which index does not matter, only the count.
// Returning 0 is an ordinary outcome - two AP boxes on the field clamp against the
// same remaining count - so it stays silent.
static int Claim(void)
{
    int count = ApPatches_GetCount();

    for (int i = 0; i < count; i++)
    {
        u64 bit = 1ULL << (i & 63);
        int w = i >> 6;
        if (ap_save->ap_patch_collected[w] & bit)
            continue;
        ap_save->ap_patch_collected[w] |= bit;
        ap_data->ap_patch_checks[w] |= bit;
        // No card write: Hoshi_WriteSave stalls the frame and this fires mid-round,
        // so the bits ride in the save block until the game's own save point.
        OSReport("[APPatches] AP Patch %d collected (%d of %d)\n",
                 i + 1, ApPatches_CollectedCount(), count);
        return 1;
    }
    return 0;
}

// An instance of one of our two kinds. The custom_items behavior clamp has already
// rewritten ItemData.kind to the base kind by the time any seam runs, so only the
// itData pointer - which the clamp leaves alone - still names the custom kind.
static int IsApKind(ItemData *id, int kind)
{
    if (id == NULL || kind < 0)
        return 0;
    itCommonDataAll *all = *stc_it_common_data;
    if (all == NULL || all->itData == NULL)
        return 0;
    return id->itData == &all->itData[kind];
}

// Size off the stage's own chance table with the three colors collapsed, for the
// one case the gated picker leaves unanswered: it found nothing eligible, returned
// -1 and wrote neither out-param.
static int RollBoxSize(void)
{
    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    if (info == NULL || info->item_desc == NULL || info->item_desc->box_spawn_chances == NULL)
        return 0;

    const u8 *chances = (const u8 *)info->item_desc->box_spawn_chances;
    int weight[3] = { 0, 0, 0 };
    int total = 0;
    for (int i = 0; i < 9; i++)
    {
        weight[i % 3] += chances[i];
        total += chances[i];
    }
    if (total == 0)
        return 0;

    int roll = HSD_Randi(total);
    for (int size = 0; size < 2; size++)
    {
        roll -= weight[size];
        if (roll < 0)
            return size;
    }
    return 2;
}

// REPLACECALL on the bl GrBoxGeneratorDetermine at 0x800eb20c, the one call site
// CityItemSpawn_Think reaches when its tick came up an item box. The picker's
// return is the box's ItemKind, so an AP box is one more outcome of the vanilla
// roll - it inherits the fall timer, the field's item cap and the spawn-rate
// scaling, and it keeps the color and size the roll landed on.
static int DetermineBox(int *box_color, int *box_size)
{
    int kind = GrBoxGeneratorDetermine(box_color, box_size);

    if (!round_armed || ApPatches_Remaining() <= 0)
        return kind;
    if (HSD_Randi(100) >= AP_BOX_PERCENT)
        return kind;

    // No gate ever sees the AP box, so it still lands on the tick where box
    // gating has left no vanilla color eligible - carrying its own color and size.
    if (kind < 0)
    {
        *box_color = BOXKIND_BLUE;
        *box_size = RollBoxSize();
    }

    boxes_rolled++;
    return box_kind;
}

// Contents of a broken AP Box. Box_OutcomeLogic caps a custom kind at one item -
// forced_item by design, the pool roll because its 1 / 2 / 4 count only applies
// to vanilla patch kinds - so the whole outcome is reproduced here instead.
static void BreakApBox(ItemData *id)
{
    JOBJ *j = GObj_GetJObjIndex(id->item_gobj, 1);
    Vec3 pos;
    JObj_GetWorldPosition(j, NULL, &pos);

    memset(id->child_gobjs, 0, sizeof(id->child_gobjs));
    if (patch_kind < 0)
        return;

    int count = (id->x40 == 1) ? 2 : (id->x40 == 2) ? 4 : 1;
    int remaining = ApPatches_Remaining();
    if (count > remaining)
        count = remaining;
    if (count <= 0)
        return;

    ItemCommonParam *p = *stc_item_param;
    for (int i = 0; i < count; i++)
    {
        if (!CityItem_CanSpawnNMore(1))
            break;

        Vec3 dir = id->forward;
        if (box_slot_yaw[i] != 0.0f)
        {
            float spread = (float)HSD_Randi((int)p->box_spawn_yaw_range);
            if (HSD_Randi(2))
                spread = -spread;
            Vec3_RotateAboutUnitAxis(&dir, &id->up,
                                     BOX_DEG_TO_RAD * (spread + box_slot_yaw[i]));
        }

        float horiz = p->box_spawn_offset_min_h +
                      (float)HSD_Randi((int)(p->box_spawn_offset_max_h - p->box_spawn_offset_min_h));
        GOBJ *child;
        if (count == 1)
        {
            child = Box_SpawnContents((ItemKind)patch_kind, 2, &pos, &dir, 0,
                                      horiz, BOX_SINGLE_PITCH);
        }
        else
        {
            float pitch = BOX_DEG_TO_RAD *
                          (p->box_spawn_offset_min_v +
                           (float)HSD_Randi((int)(p->box_spawn_offset_max_v - p->box_spawn_offset_min_v)));
            child = Box_SpawnContents((ItemKind)patch_kind, 2, &pos, &dir, 1,
                                      horiz, pitch);
        }

        id->child_gobjs[i] = child;
        if (child != NULL)
            ((ItemData *)child->userdata)->parent_gobj = id->item_gobj;
    }
}

// REPLACECALL on the bl in Box_Break. Anything that is not an AP Box is handed
// straight to the vanilla outcome.
static void OutcomeLogic(ItemData *id)
{
    if (IsApKind(id, box_kind))
        BreakApBox(id);
    else
        Box_OutcomeLogic(id);
}

// Box_SpawnImpactEffect picks its burst off the clamped kind, so an AP Box draws
// the vanilla blue pair. Each is recolored into a copy of its generator descriptor
// that psGeneratorDesc points at for the length of the spawn: Ptcl_Alloc stores
// descriptor + 0x3c in the generator instance, so the burst reads the copy for its
// whole life while every other box still allocates off the vanilla one.
#define AP_PTCL_BANK      5     // yakumono
#define AP_PTCL_DESC_SIZE 0x88  // stride of the six box descriptors in the bank
#define AP_PTCL_COLOR     0x3c  // PTCL_OP_COLOR, RGBA operand at +2
#define AP_PTCL_COLOR2    0x48  // PTCL_OP_COLOR2, RGBA operand at +2

// psInitDataBanks biases psGeneratorDesc[bank] by the bank's base id and stores
// base + n as the count, so both tables are indexed by the whole effect id.
static const int ap_burst_ef[2] = { 50000, 50001 }; // hit, break

// The box's own six faces, matching the atlas its texture is authored from.
static const u8 ap_face_color[][3] = {
    { 201, 118, 130 }, { 117, 194, 117 }, { 202, 148, 194 },
    { 217, 160, 125 }, { 118, 126, 189 }, { 238, 227, 145 },
};
#define AP_FACE_NUM (int)(sizeof(ap_face_color) / sizeof(ap_face_color[0]))

static u8 ptcl_desc[2][AP_FACE_NUM][AP_PTCL_DESC_SIZE];
static int ptcl_state; // 0 not built for this round, 1 ready, -1 unavailable
static int ptcl_color;

// Force one color operand to the tint's hue, keeping its own value so the bright
// primary stays bright and the dark secondary stays dark.
static void RecolorOperand(u8 *rgb, const u8 *tint)
{
    int v = rgb[0] > rgb[1] ? rgb[0] : rgb[1];
    if (rgb[2] > v)
        v = rgb[2];
    for (int i = 0; i < 3; i++)
        rgb[i] = (u8)((tint[i] * v) / 255);
}

// One recolored copy of each burst per face color. psInitDataBanks rebuilds the
// bank tables on every scene load, so the source is re-read each round, and the
// two opcodes are checked rather than assumed.
static void BuildBursts(void)
{
    ptcl_state = -1;
    for (int g = 0; g < 2; g++)
    {
        int ef = ap_burst_ef[g];
        if ((u32)ef >= psGeneratorCount[AP_PTCL_BANK])
            return;

        const u8 *src = psGeneratorDesc[AP_PTCL_BANK][ef];
        if (src == NULL || src[AP_PTCL_COLOR] != (PTCL_OP_COLOR | 0xf) ||
            src[AP_PTCL_COLOR2] != (PTCL_OP_COLOR2 | 0xf))
        {
            OSReport("[APPatches] Box burst %d is not the expected program\n", ef);
            return;
        }
        for (int c = 0; c < AP_FACE_NUM; c++)
        {
            u8 *dst = ptcl_desc[g][c];
            memcpy(dst, src, AP_PTCL_DESC_SIZE);
            RecolorOperand(dst + AP_PTCL_COLOR + 2, ap_face_color[c]);
            RecolorOperand(dst + AP_PTCL_COLOR2 + 2, ap_face_color[c]);
        }
    }
    ptcl_state = 1;
}

// REPLACECALL on both bl Box_SpawnImpactEffect sites. An AP Box swaps its
// recolored descriptor in for the length of the spawn and takes the next face
// color, so a box that is hit twice and broken throws three of its own colors.
static int SpawnImpactEffect(GOBJ *gobj, int is_break)
{
    ItemData *id = gobj != NULL ? (ItemData *)gobj->userdata : NULL;
    int g = is_break ? 1 : 0;

    if (!IsApKind(id, box_kind))
        return Box_SpawnImpactEffect(gobj, is_break);

    if (ptcl_state == 0)
        BuildBursts();
    if (ptcl_state != 1)
        return Box_SpawnImpactEffect(gobj, is_break);

    u8 **slot = &psGeneratorDesc[AP_PTCL_BANK][ap_burst_ef[g]];
    u8 *saved = *slot;
    *slot = ptcl_desc[g][ptcl_color];
    ptcl_color = (ptcl_color + 1) % AP_FACE_NUM;

    int ret = Box_SpawnImpactEffect(gobj, is_break);
    *slot = saved;
    return ret;
}

// Ply_IncrementItemCollectNum is the single producer of PlayerStats.item_collect[].
// Returning 1 skips the call, keeping both AP kinds out of every counter it feeds -
// the per-kind slot, the lifetime total, and the first-20-seconds and Tac aggregates.
static int SuppressItemCollect(ItemData *id)
{
    return IsApKind(id, patch_kind) || IsApKind(id, box_kind);
}

// 0x801db91c: lwz r4, 28(r21) - the call's own kind argument, reloaded from r21
// (ItemData) on the accept path. Accept falls through to 0x801db920, which
// re-materializes r3 and r5; reject jumps past the call.
CODEPATCH_HOOKCONDITIONALCREATE(0x801db91c, "mr 3, 21\n\t", SuppressItemCollect, "", 0, 0x801db92c)

static void OnPickup(u32 id_hash, const char *name, int player)
{
    (void)name;
    if (id_hash != patch_hash || patch_hash == 0)
        return;
    if (player < 0 || player >= 5)
        return;
    Claim();
}

// Match both drop-ins to their hashes by display name, the same lazy resolve the
// AP Star spheres use: mod load order follows FST order, so an export is not
// available until its owner's OnBoot has run.
static void ResolveItems(void)
{
    if (ci_api == NULL || items_matched == 2)
        return;

    for (int i = 0; i < ci_api->GetCount(); i++)
    {
        const char *name = ci_api->GetName(i);
        if (name == NULL)
            continue;
        if (patch_hash == 0 && strcmp(name, AP_PATCH_ITEM_NAME) == 0)
            patch_hash = ci_api->GetIdHash(i);
        else if (box_hash == 0 && strcmp(name, AP_BOX_ITEM_NAME) == 0)
            box_hash = ci_api->GetIdHash(i);
    }

    int found = (patch_hash != 0) + (box_hash != 0);
    if (found == items_matched)
        return;
    items_matched = found;
    if (found != 2)
        OSReport("[APPatches] Only %d of 2 AP item(s) found in items/ (%s, %s)\n",
                 found, patch_hash ? "patch" : "no patch", box_hash ? "box" : "no box");
}

void ApPatches_OnBoot(void)
{
    CODEPATCH_REPLACECALL(0x80258384, OutcomeLogic);  // bl Box_OutcomeLogic in Box_Break
    CODEPATCH_REPLACECALL(0x80258344, SpawnImpactEffect);  // bl Box_SpawnImpactEffect in Box_Break
    CODEPATCH_REPLACECALL(0x802575f0, SpawnImpactEffect);  // ... and in Box_OnTakeDamage
    CODEPATCH_REPLACECALL(0x800eb20c, DetermineBox);  // bl GrBoxGeneratorDetermine in CityItemSpawn_Think
    CODEPATCH_HOOKAPPLY(0x801db91c);                  // item_collect suppression
    OSReport("[APPatches] Hooks installed\n");
}

void ApPatches_On3DLoadStart(void)
{
    // The kinds are per-scene, and this fires for every 3D scene where On3DLoadEnd
    // does not (Top Ride), so clearing here is what keeps them from going stale.
    patch_kind = -1;
    box_kind = -1;
    round_armed = 0;
    boxes_rolled = 0;
    ptcl_state = 0; // the bank tables are rebuilt with the scene

    if (ci_api == NULL)
        ci_api = (const CustomItemsAPI *)Hoshi_ImportMod(
            (char *)CUSTOM_ITEMS_MOD_NAME, CUSTOM_ITEMS_API_MAJOR, CUSTOM_ITEMS_API_MINOR);
    if (ci_api == NULL)
        return;

    ResolveItems();
    if (!pickup_registered)
    {
        ci_api->AddPickupHandler(OnPickup);
        pickup_registered = 1;
    }

    // custom_items registers at CityItemSpawn_Init's epilogue and skips a disabled
    // item, so a held-out kind is never handed an ItemKind and nothing can spawn it.
    int on = ApPatches_GetCount() > 0 &&
             Gm_IsInCity() && Gm_GetCityMode() == CITYMODE_TRIAL;
    if (patch_hash != 0)
        ci_api->SetEnabled(patch_hash, on);
    if (box_hash != 0)
        ci_api->SetEnabled(box_hash, on);
}

void ApPatches_On3DLoadEnd(void)
{
    if (ci_api == NULL || ApPatches_GetCount() == 0)
        return;
    if (!Gm_IsInCity() || Gm_GetCityMode() != CITYMODE_TRIAL)
        return;

    // The kinds are handed out at CityItemSpawn_Init, so they are only valid from
    // here on and only for this scene.
    if (patch_hash != 0)
        patch_kind = ci_api->GetAssignedKind(patch_hash);
    if (box_hash != 0)
        box_kind = ci_api->GetAssignedKind(box_hash);
    if (patch_kind < 0 || box_kind < 0)
    {
        OSReport("[APPatches] Not armed: patch kind %d, box kind %d\n", patch_kind, box_kind);
        return;
    }

    round_armed = 1;
    OSReport("[APPatches] Armed with %d patch(es) left, %d%% of box spawns\n",
             ApPatches_Remaining(), AP_BOX_PERCENT);
}

void ApPatches_On3DExit(void)
{
    if (!round_armed)
        return;
    OSReport("[APPatches] Round over: %d AP box(es) rolled, %d patch(es) left\n",
             boxes_rolled, ApPatches_Remaining());
    round_armed = 0;
}

void ApPatches_OnFrameStart(void)
{
    int any = 0;
    for (int w = 0; w < AP_PATCH_WORDS && !any; w++)
        any = ap_data->ap_patch_backfill[w] != 0;
    if (!any)
        return;

    int applied = 0;
    for (int w = 0; w < AP_PATCH_WORDS; w++)
    {
        u64 incoming = ap_data->ap_patch_backfill[w];
        u64 fresh = incoming & ~ap_save->ap_patch_collected[w];
        ap_save->ap_patch_collected[w] |= incoming;
        ap_data->ap_patch_checks[w] |= incoming;
        ap_data->ap_patch_backfill[w] = 0;
        applied += Popcount64(fresh);
    }

    if (applied)
        OSReport("[APPatches] Backfill applied (%d new patch(es))\n", applied);
}

void ApPatches_OnSaveLoaded(void)
{
    for (int w = 0; w < AP_PATCH_WORDS; w++)
        ap_data->ap_patch_checks[w] = ap_save->ap_patch_collected[w];
}

void ApPatches_ResetAll(void)
{
    for (int w = 0; w < AP_PATCH_WORDS; w++)
    {
        ap_save->ap_patch_collected[w] = 0;
        ap_data->ap_patch_checks[w] = 0;
    }
}

// Bits 0..count-1 of word w, for a count that spans the whole array.
static u64 WordMask(int count, int w)
{
    int bits = count - w * 64;
    if (bits >= 64)
        return ~0ULL;
    return bits > 0 ? (1ULL << bits) - 1 : 0;
}

void ApPatches_DebugForceMarkAll(void)
{
    int count = ApPatches_GetCount();
    for (int w = 0; w < AP_PATCH_WORDS; w++)
    {
        u64 mask = WordMask(count, w);
        ap_save->ap_patch_collected[w] = mask;
        ap_data->ap_patch_checks[w] = mask;
    }
}

void ApPatches_DebugSetCount(int count)
{
    if (count < 0)
        count = 0;
    if (count > AP_PATCH_MAX)
        count = AP_PATCH_MAX;
    ap_save->options.ap_patches = (u32)count;

    // A claimed bit past the new ceiling would keep counting toward CollectedCount,
    // so trim to the window the count now describes.
    for (int w = 0; w < AP_PATCH_WORDS; w++)
    {
        u64 mask = WordMask(count, w);
        ap_save->ap_patch_collected[w] &= mask;
        ap_data->ap_patch_checks[w] &= mask;
    }
}

int ApPatches_DebugClaim(void)
{
    return Claim();
}

int ApPatches_DebugSpawnBox(int ply)
{
    if (box_kind < 0)
        return 0;

    // The size roll is carried in so a debug box still opens into two or four.
    return APItems_SpawnForward(ply, (ItemKind)box_kind, BOXKIND_BLUE, RollBoxSize());
}
