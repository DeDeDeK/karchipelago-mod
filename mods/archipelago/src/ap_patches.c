#include <stddef.h>
#include <string.h>
#include <limits.h>

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
#include "hoshi/func.h"

#include "custom_items_api.h"

#include "main.h"
#include "ap_item_handler.h"
#include "gate_boxes.h"
#include "spawn_rate.h"
#include "ap_patches.h"
#include "settings_menu.h"

// percent: the share of box spawn ticks that become an AP Box. interval: the frame floor
// between wins, divided by the spawn-rate scale so Spawn Rate Up still moves it.
static const struct
{
    int percent;
    int interval;
} ap_box_rate[APBOXRATE_NUM] = {
    [APBOXRATE_RARE]   = {  3, 80 * 60 },
    [APBOXRATE_LOW]    = {  6, 40 * 60 },
    [APBOXRATE_MEDIUM] = { 12, 20 * 60 },
    [APBOXRATE_HIGH]   = { 20, 12 * 60 },
};

static int BoxRate(void)
{
    int rate = ap_menu_settings.ap_box_rate;
    return (rate < 0 || rate >= APBOXRATE_NUM) ? APBOXRATE_MEDIUM : rate;
}

// Caps the vanilla 1 / 2 / 4 size roll, so one large box can't hand over a run of checks.
#define AP_BOX_MAX_PATCHES 2

#define BOX_SINGLE_PITCH ((float)M_PI_2) // the fixed launch pitch a one-item box uses

static const CustomItemsAPI *ci_api;

static u32 patch_hash, box_hash; // 0 until the registry has been scanned
static int items_matched = -1;   // -1 before the first scan, then the match count

static int patch_kind = -1;      // ItemKind assigned this round, -1 if none
static int box_kind = -1;

static int round_armed;
static int boxes_rolled;
static int box_gate_frames; // match_frames_left must fall to this before the next roll

int APPatches_CollectedCount(void)
{
    int n = 0;
    for (int w = 0; w < AP_PATCH_WORDS; w++)
        n += Popcount64(ap_save->ap_patch_collected[w]);
    return n;
}

int APPatches_GetCount(void)
{
    int count = (int)ap_save->options.ap_patches;
    return count > AP_PATCH_MAX ? AP_PATCH_MAX : count;
}

int APPatches_Remaining(void)
{
    int left = APPatches_GetCount() - APPatches_CollectedCount();
    return left > 0 ? left : 0;
}

// Claims the lowest clear bit below ap_patches; the patches are interchangeable in logic.
// A 0 return is ordinary (two boxes clamp against one remaining count), so it is silent.
static int Claim(void)
{
    int count = APPatches_GetCount();

    for (int i = 0; i < count; i++)
    {
        u64 bit = 1ULL << (i & 63);
        int w = i >> 6;
        if (ap_save->ap_patch_collected[w] & bit)
            continue;
        ap_save->ap_patch_collected[w] |= bit;
        ap_data->ap_patch_checks[w] |= bit;
        // No card write: Hoshi_WriteSave stalls the frame, and this fires mid-round.
        OSReport("[APPatches] AP Patch %d collected (%d of %d)\n",
                 i + 1, APPatches_CollectedCount(), count);
        return 1;
    }
    return 0;
}

// custom_items has rewritten ItemData.kind to the base kind by the time any seam runs.
static int IsApKind(ItemData *id, int kind)
{
    return id != NULL && kind >= 0 && ci_api->GetItemKind(id) == kind;
}

// Replaces the bl GrBoxGeneratorDetermine at 0x800eb20c in CityItemSpawn_Think
// (0x800eb108). The AP Box is one more outcome of the vanilla roll, keeping the color,
// size and fall timer it landed on.
static int DetermineBox(int *box_color, int *box_size)
{
    int kind = GrBoxGeneratorDetermine(box_color, box_size);

    if (!round_armed || APPatches_Remaining() <= 0)
        return kind;

    grBoxGeneInfo *info = *stc_grBoxGeneInfo;
    int now = info != NULL ? info->match_frames_left : 0;
    if (now > box_gate_frames)
        return kind;
    int rate = BoxRate();
    if (HSD_Randi(100) >= ap_box_rate[rate].percent)
        return kind;

    box_gate_frames = now - (int)((float)ap_box_rate[rate].interval / SpawnRate_GetScale());

    // Box gating never sees the AP Box, so it lands even when no vanilla color is
    // eligible, on a size off the whole table.
    if (kind < 0)
    {
        *box_color = BOXKIND_BLUE;
        *box_size = GateBoxes_RollSize(-1);
    }

    boxes_rolled++;
    return box_kind;
}

// Box_OutcomeLogic caps a custom kind at one item, so the whole outcome is reproduced.
static void BreakApBox(ItemData *id)
{
    JOBJ *j = GObj_GetJObjIndex(id->item_gobj, 1);
    Vec3 pos;
    JObj_GetWorldPosition(j, NULL, &pos);

    memset(id->child_gobjs, 0, sizeof(id->child_gobjs));
    if (patch_kind < 0)
        return;

    int count = (id->box_size == BOXSIZE_MEDIUM) ? 2 : (id->box_size == BOXSIZE_LARGE) ? 4 : 1;
    if (count > AP_BOX_MAX_PATCHES)
        count = AP_BOX_MAX_PATCHES;
    int remaining = APPatches_Remaining();
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
        if (stc_box_slot_yaw[i] != 0.0f)
        {
            float spread = (float)HSD_Randi((int)p->box_spawn_yaw_range);
            if (HSD_Randi(2))
                spread = -spread;
            Vec3_RotateAboutUnitAxis(&dir, &id->up, MTXDegToRad(spread + stc_box_slot_yaw[i]));
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
            float pitch = MTXDegToRad(
                p->box_spawn_offset_min_v +
                (float)HSD_Randi((int)(p->box_spawn_offset_max_v - p->box_spawn_offset_min_v)));
            child = Box_SpawnContents((ItemKind)patch_kind, 2, &pos, &dir, 1,
                                      horiz, pitch);
        }

        id->child_gobjs[i] = child;
        if (child != NULL)
            ((ItemData *)child->userdata)->parent_gobj = id->item_gobj;
    }
}

// Replaces the bl Box_OutcomeLogic at 0x80258384 in ItemGObj_BoxBreak (0x802582dc).
static void OutcomeLogic(ItemData *id)
{
    if (IsApKind(id, box_kind))
        BreakApBox(id);
    else
        Box_OutcomeLogic(id);
}

// ItemGObj_BoxSpawnImpactEffect picks its burst off the clamped kind. Ptcl_Alloc keeps
// descriptor + 0x3c in the generator, so pointing stc_ps_generator_desc at a recolored
// copy for the length of the spawn recolors that burst alone.
#define AP_PTCL_DESC_SIZE 0x88 // stride of the six box descriptors in the bank
// Where each burst program's two color opcodes sit, checked before use.
#define AP_PTCL_COLOR     (offsetof(PtclDesc, program) + 0x0)
#define AP_PTCL_COLOR2    (offsetof(PtclDesc, program) + 0xc)

// psInitDataBanks biases stc_ps_generator_desc[bank] by the bank's base id and stores
// base + n as the count, so both tables are indexed by the whole effect id.
static const int ap_burst_ef[2] = { 50000, 50001 }; // hit, break

// One color per box face.
static const u8 ap_face_color[][3] = {
    { 201, 118, 130 }, { 117, 194, 117 }, { 202, 148, 194 },
    { 217, 160, 125 }, { 118, 126, 189 }, { 238, 227, 145 },
};
#define AP_FACE_NUM (int)GetElementsIn(ap_face_color)

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

// One recolored copy of each burst per face color, rebuilt each round since
// psInitDataBanks reloads the banks. The opcodes are checked rather than assumed.
static void BuildBursts(void)
{
    ptcl_state = -1;
    for (int g = 0; g < 2; g++)
    {
        int ef = ap_burst_ef[g];
        if ((u32)ef >= stc_ps_generator_count[PTCL_BANK_YAKUMONO])
            return;

        const u8 *src = stc_ps_generator_desc[PTCL_BANK_YAKUMONO][ef];
        if (src == NULL || src[AP_PTCL_COLOR] != (PTCL_OP_COLOR | 0xf) ||
            src[AP_PTCL_COLOR2] != (PTCL_OP_COLOR2 | 0xf))
        {
            static int warned;
            if (!warned)
                OSReport("[APPatches] Box burst %d is not the expected program\n", ef);
            warned = 1;
            return;
        }
        for (int c = 0; c < AP_FACE_NUM; c++)
        {
            u8 *dst = ptcl_desc[g][c];
            memcpy(dst, src, AP_PTCL_DESC_SIZE);
            RecolorOperand(dst + AP_PTCL_COLOR + PTCL_OP_COLOR_OPERANDS, ap_face_color[c]);
            RecolorOperand(dst + AP_PTCL_COLOR2 + PTCL_OP_COLOR_OPERANDS, ap_face_color[c]);
        }
    }
    ptcl_state = 1;
}

// Replaces both bl ItemGObj_BoxSpawnImpactEffect. An AP Box takes the next face color each
// time, so a box hit twice and broken throws three of its own colors.
static int SpawnImpactEffect(GOBJ *gobj, int is_break)
{
    ItemData *id = gobj != NULL ? (ItemData *)gobj->userdata : NULL;
    int g = is_break ? 1 : 0;

    if (!IsApKind(id, box_kind))
        return ItemGObj_BoxSpawnImpactEffect(gobj, is_break);

    if (ptcl_state == 0)
        BuildBursts();
    if (ptcl_state != 1)
        return ItemGObj_BoxSpawnImpactEffect(gobj, is_break);

    u8 **slot = &stc_ps_generator_desc[PTCL_BANK_YAKUMONO][ap_burst_ef[g]];
    u8 *saved = *slot;
    *slot = ptcl_desc[g][ptcl_color];
    ptcl_color = (ptcl_color + 1) % AP_FACE_NUM;

    int ret = ItemGObj_BoxSpawnImpactEffect(gobj, is_break);
    *slot = saved;
    return ret;
}

// Ply_IncrementItemCollectNum is the game's one producer of PlayerStats.item_collect[];
// returning 1 skips it for both AP kinds, and so every counter it feeds.
static int SuppressItemCollect(ItemData *id)
{
    return IsApKind(id, patch_kind) || IsApKind(id, box_kind);
}

// Hook at 0x801db91c in Machine_OnTouchItem (0x801db34c): lwz r4, 28(r21) loads the kind
// for bl Ply_IncrementItemCollectNum (r21 = ItemData). Accept resumes at 0x801db920, which
// rebuilds r3 and r5; reject skips the call.
CODEPATCH_HOOKCONDITIONALCREATE(0x801db91c, "mr 3, 21\n\t", SuppressItemCollect, "", 0, 0x801db92c)

static void OnPickup(u32 id_hash, int player)
{
    (void)player;
    if (id_hash == patch_hash && patch_hash != 0)
        Claim();
}

// Matches both drop-ins to their hashes by display name, retried each round until both
// are found.
static void ResolveItems(void)
{
    if (items_matched == 2)
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

void APPatches_OnBoot(void)
{
    CODEPATCH_REPLACECALL(0x80258384, OutcomeLogic);
    CODEPATCH_REPLACECALL(0x80258344, SpawnImpactEffect); // in ItemGObj_BoxBreak (0x802582dc)
    CODEPATCH_REPLACECALL(0x802575f0, SpawnImpactEffect); // in Box_OnTakeDamage (0x80257158)
    CODEPATCH_REPLACECALL(0x800eb20c, DetermineBox);
    CODEPATCH_HOOKAPPLY(0x801db91c);
    OSReport("[APPatches] Hooks installed\n");
}

void APPatches_On3DLoadStart(void)
{
    // Per-scene kinds, cleared here because On3DLoadEnd does not run for Top Ride.
    patch_kind = -1;
    box_kind = -1;
    round_armed = 0;
    boxes_rolled = 0;
    box_gate_frames = INT_MAX; // open, so the round's first roll is not held back
    ptcl_state = 0;

    if (ci_api == NULL)
        return;

    ResolveItems();

    // custom_items registers at CityItemSpawn_Init and skips a disabled item. The attract
    // demo is held out too: a CPU pickup would claim a location.
    int on = APPatches_GetCount() > 0 && !Gm_IsAutoDemo() &&
             Gm_IsInCity() && Gm_GetCityMode() == CITYMODE_TRIAL;
    if (patch_hash != 0)
        ci_api->SetEnabled(patch_hash, on);
    if (box_hash != 0)
        ci_api->SetEnabled(box_hash, on);
}

void APPatches_On3DLoadEnd(void)
{
    if (ci_api == NULL || APPatches_GetCount() == 0)
        return;
    if (Gm_IsAutoDemo() || !Gm_IsInCity() || Gm_GetCityMode() != CITYMODE_TRIAL)
        return;

    // Handed out at CityItemSpawn_Init, so valid from here and for this scene only.
    if (patch_hash != 0)
        patch_kind = ci_api->GetAssignedKind(patch_hash);
    if (box_hash != 0)
        box_kind = ci_api->GetAssignedKind(box_hash);
    if (patch_kind < 0 || box_kind < 0)
    {
        // A drop-in missing from items/ was already reported by ResolveItems.
        static int warned;
        if (patch_hash != 0 && box_hash != 0)
        {
            if (!warned)
                OSReport("[APPatches] Not armed: patch kind %d, box kind %d\n", patch_kind, box_kind);
            warned = 1;
        }
        return;
    }

    round_armed = 1;
    int rate = BoxRate();
    OSReport("[APPatches] Armed with %d patch(es) left, %d%% of box spawns, %d frame floor\n",
             APPatches_Remaining(), ap_box_rate[rate].percent,
             (int)((float)ap_box_rate[rate].interval / SpawnRate_GetScale()));
}

void APPatches_On3DExit(void)
{
    if (!round_armed)
        return;
    OSReport("[APPatches] Round over: %d AP box(es) rolled, %d patch(es) left\n",
             boxes_rolled, APPatches_Remaining());
    round_armed = 0;
}

void APPatches_ApplyBackfill(void)
{
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

void APPatches_OnSaveLoaded(void)
{
    // Without the mirror the client would read zeros and resend every patch.
    memcpy(ap_data->ap_patch_checks, ap_save->ap_patch_collected, sizeof(ap_data->ap_patch_checks));

    if (ci_api != NULL)
        return;
    ci_api = (const CustomItemsAPI *)Hoshi_ImportMod(
        (char *)CUSTOM_ITEMS_MOD_NAME, CUSTOM_ITEMS_API_MAJOR, CUSTOM_ITEMS_API_MINOR);
    if (ci_api != NULL)
        ci_api->AddPickupHandler(OnPickup);
    else
        OSReport("[APPatches] custom_items missing from this build, AP Patches are off\n");
}

void APPatches_ResetAll(void)
{
    memset(ap_save->ap_patch_collected, 0, sizeof(ap_save->ap_patch_collected));
    memset(ap_data->ap_patch_checks, 0, sizeof(ap_data->ap_patch_checks));
}

// Bits 0..count-1 of word w, for a count that spans the whole array.
static u64 WordMask(int count, int w)
{
    int bits = count - w * 64;
    if (bits >= 64)
        return ~0ULL;
    return bits > 0 ? (1ULL << bits) - 1 : 0;
}

void APPatches_DebugForceMarkAll(void)
{
    int count = APPatches_GetCount();
    for (int w = 0; w < AP_PATCH_WORDS; w++)
    {
        u64 mask = WordMask(count, w);
        ap_save->ap_patch_collected[w] = mask;
        ap_data->ap_patch_checks[w] = mask;
    }
}

void APPatches_DebugSetCount(int count)
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

int APPatches_DebugClaim(void)
{
    return Claim();
}

// The pending backfill goes too, or the client's next push would restore the bits.
void APPatches_DebugClearCollected(void)
{
    APPatches_ResetAll();
    memset(ap_data->ap_patch_backfill, 0, sizeof(ap_data->ap_patch_backfill));
    Hoshi_WriteSave();
    OSReport("[APPatches] Debug: cleared every collected bit\n");
}

int APPatches_DebugSpawnBox(int ply)
{
    if (box_kind < 0)
        return 0;

    return APItems_SpawnForward(ply, (ItemKind)box_kind, BOXKIND_BLUE, GateBoxes_RollSize(-1));
}
