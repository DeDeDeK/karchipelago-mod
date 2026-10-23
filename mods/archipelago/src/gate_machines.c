#include "game.h"
#include "menu.h"
#include "os.h"
#include "scene.h"
#include "topride.h"
#include "audio.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_machines.h"
#include "gate_colors.h"
#include "settings_menu.h"
#include "textbox_api.h"
#include "inline.h"
#include "ap_announce.h"
#include "gate_ap_star.h"
#include "ap_star_api.h"
#include "ap_item_handler.h"

// Kinds with a 0 base City Trial spawn chance that must never take the zero-chance
// fallback weight: the Top Ride stars, the transformation forms and the characters' own
// machines.
#define CT_SPAWN_EXCLUDED_MASK     \
    ((1u << VCKIND_FREE)         | \
     (1u << VCKIND_STEER)        | \
     (1u << VCKIND_WINGKIRBY)    | \
     (1u << VCKIND_WINGMETAKNIGHT) | \
     (1u << VCKIND_WHEELNORMAL)  | \
     (1u << VCKIND_WHEELKIRBY)   | \
     (1u << VCKIND_WHEELDEDEDE)  | \
     (1u << VCKIND_WHEELVSDEDEDE))

// Weight for an unlocked kind the vanilla table gives 0 chance (Compact, Flight, Hydra,
// Dragoon), well under the weights the table does carry.
static float ZeroChanceSpawnWeight(int vckind)
{
    switch (vckind)
    {
    case VCKIND_COMPACT: return 5.0f;
    case VCKIND_FLIGHT:  return 2.0f;
    default:             return 1.0f; // Hydra, Dragoon
    }
}

// Unlock mask bit a MachineKind is gated on, or -1 for an ungated registered machine.
static int GateBit(int kind)
{
    if (kind >= 0 && kind < VCKIND_NUM)
        return kind;
    if (kind >= VCKIND_NUM && kind == GateApStar_MachineKind())
        return AP_MACHINE_BIT_AP_STAR;
    return -1;
}

static int IsKindUnlocked(int kind)
{
    if (kind < 0)
        return 0;

    int bit = GateBit(kind);
    return bit < 0 || ((ap_save->machine_unlocked_mask >> bit) & 1);
}

static int IsCKindUnlocked(CharacterKind ckind)
{
    if (ckind < 0 || ckind >= CustomMachines_CharacterKindNum(cm_api))
        return 0;
    CharacterDesc *desc = Character_GetDesc(ckind);
    if (!desc)
        return 0;
    MachineKind vckind = CustomMachines_ResolveKind(cm_api, desc->is_bike, desc->machine_kind);
    return IsKindUnlocked(vckind);
}

static MachineKind GetFirstUnlockedCTMachine()
{
    for (int i = 0; i < CustomMachines_KindNum(cm_api); i++)
    {
        if (i < VCKIND_NUM && (CT_SPAWN_EXCLUDED_MASK & (1u << i)))
            continue;
        if (IsKindUnlocked(i))
            return i;
    }
    return VCKIND_COMPACT;
}

// Skips Dedede and Meta Knight: Base City Trial loads no HUD assets for their riders, and
// 3DHud_CreateSpeedometerInner NULL-derefs.
static CharacterKind RandomUnlockedKirbyCKind(void)
{
    int unlocked_count = 0;
    for (int ckind = 0; ckind < CustomMachines_CharacterKindNum(cm_api); ckind++)
    {
        if (ckind == CKIND_DEDEDE || ckind == CKIND_METAKNIGHT)
            continue;
        if (IsCKindUnlocked(ckind))
            unlocked_count++;
    }

    if (unlocked_count == 0)
        return CKIND_COMPACT;

    int pick = HSD_Randi(unlocked_count);
    for (int ckind = 0; ckind < CustomMachines_CharacterKindNum(cm_api); ckind++)
    {
        if (ckind == CKIND_DEDEDE || ckind == CKIND_METAKNIGHT)
            continue;
        if (IsCKindUnlocked(ckind) && pick-- == 0)
            return ckind;
    }
    return CKIND_COMPACT;
}

static int IsTRMachineUnlocked(TopRideMachineKind tr)
{
    MachineKind vckind = TOPRIDE_MACHINE_TO_VCKIND(tr);
    return IsKindUnlocked(vckind);
}

static TopRideMachineKind GetFirstUnlockedTRMachine()
{
    if (IsTRMachineUnlocked(TR_MACHINE_FREE))
        return TR_MACHINE_FREE;
    if (IsTRMachineUnlocked(TR_MACHINE_STEER))
        return TR_MACHINE_STEER;
    return TR_MACHINE_FREE;
}

static TopRideMachineKind GetRandomUnlockedTRMachine()
{
    TopRideMachineKind unlocked[TR_MACHINE_NUM];
    int count = 0;
    if (IsTRMachineUnlocked(TR_MACHINE_FREE))
        unlocked[count++] = TR_MACHINE_FREE;
    if (IsTRMachineUnlocked(TR_MACHINE_STEER))
        unlocked[count++] = TR_MACHINE_STEER;
    if (count == 0)
        return GetFirstUnlockedTRMachine();
    return unlocked[HSD_Randi(count)];
}

// Each TR lobby init writes panel_machine[] = 0 in its per-slot loop. The hooks sit past
// it, with panel_pkind filled: every panel reads CPU at InitSelectData, every one but the
// active one at SoloInit.
static void GateMachines_FixupTRInit(void)
{
    GameData *gd = Gm_GetGameData();
    u8 *pkind   = gd->topride_select_ply.panel_pkind;
    u8 *machine = gd->topride_select_ply.panel_machine;
    TopRideMachineKind first = GetFirstUnlockedTRMachine();

    for (int i = 0; i < 4; i++)
    {
        if (pkind[i] != TR_PANEL_CPU)
        {
            machine[i] = (u8)first;
            continue;
        }

        machine[i] = (u8)GetRandomUnlockedTRMachine();
        gd->topride_select_ply.color[i] =
            (u8)GateColors_RandomForPanel(pkind, gd->topride_select_ply.color, i, TR_PANEL_HMN);
    }
}

// Hook at 0x8002d070 in TopRide_InitSelectData (0x8002cfd8), past the per-slot loop. The
// three following stb r3 lobby-flag clears need r3 = 0.
CODEPATCH_HOOKCREATE(0x8002d070,
    "",
    GateMachines_FixupTRInit,
    "li 3, 0\n\t",
    0
)

// Hook at 0x8002d748 (bl Gm_GetGameData) in TopRide_RaceInit (0x8002d0ec), past the CPU-fill
// loop whose iterator r7 is caller-saved. The re-executed bl restores r3.
CODEPATCH_HOOKCREATE(0x8002d748,
    "",
    GateMachines_FixupTRInit,
    "",
    0
)

// Direction edges the lobby cyclers test: stick or D-pad.
#define TR_CYCLE_RIGHT (PAD_BUTTON_RIGHT | PAD_BUTTON_DPAD_RIGHT)
#define TR_CYCLE_LEFT  (PAD_BUTTON_LEFT | PAD_BUTTON_DPAD_LEFT)

// The lobby "Control Type" L/R cycler: the vanilla 0..1 clamp, minus a move onto a locked
// machine. The race and solo lobbies carry identical cyclers. slot_base is
// &topride_select_ply.x197 plus the panel's slot. Returns 1 when the machine changed.
static int GateMachines_CycleTRMachine(u8 *slot_base, u32 input_bits)
{
    GameData *gd = Gm_GetGameData();
    int slot = slot_base - &gd->topride_select_ply.x197;
    u8 *machine = &gd->topride_select_ply.panel_machine[slot];
    u8 current = *machine;
    u8 new_val = current;

    if (input_bits & TR_CYCLE_RIGHT)
    {
        if (current < (TR_MACHINE_NUM - 1) && IsTRMachineUnlocked(current + 1))
            new_val = current + 1;
    }
    else if (input_bits & TR_CYCLE_LEFT)
    {
        if (current > 0 && IsTRMachineUnlocked(current - 1))
            new_val = current - 1;
    }

    if (new_val == current)
        return 0;

    *machine = new_val;
    return 1;
}

// Hook at 0x8002be44 in TopRide_CSS_PanelThink (0x8002b8a8), replacing the cycler through
// 0x8002be94. r26 = slot base, r29 = edge bits. Unchanged -> 0x8002c054 (function end);
// changed -> 0x8002be98 (SFX and UI update).
CODEPATCH_HOOKCONDITIONALCREATE(0x8002be44,
    "mr 3, 26\n\t"
    "mr 4, 29\n\t",
    GateMachines_CycleTRMachine,
    "",
    0x8002c054,
    0x8002be98
)

// Hook at 0x8002cb98 in TopRide_SoloPanelThink (0x8002ca80), replacing the cycler through
// the beq at 0x8002cbec. r30 = slot base, r26 = edge bits, both callee-saved for the SFX
// and UI block.
CODEPATCH_HOOKCONDITIONALCREATE(0x8002cb98,
    "mr 3, 30\n\t"
    "mr 4, 26\n\t",
    GateMachines_CycleTRMachine,
    "",
    0x8002cc18,
    0x8002cbf0
)

// Hook at 0x8002dc48 in TopRide_SoloInit (0x8002d9e8), past the per-slot loop that zeroes
// panel_machine. The clobbered stb r0, 6(r31) and the two stores after it need r0 = 0.
CODEPATCH_HOOKCREATE(0x8002dc48,
    "",
    GateMachines_FixupTRInit,
    "li 0, 0\n\t",
    0
)

// Blocks a TR start while Free and Steer are both locked; returns 1 to block. Both sites
// run on the Start rising edge only.
static int GateMachines_TRLobbyCanStart(void)
{
    if (ap_save->machine_unlocked_mask & TR_MACHINE_BITS)
        return 0;

    playSoundFX_errorNoise();
    tb_api->EnqueueColoredNoun("Unlock a ", "Top Ride machine", tb_api->MachineColor, " to start!");
    return 1;
}

// Hook at 0x8002c52c in TopRide_PreGameThink (0x8002c06c), the race start body; the
// clobbered bl plays the confirm sound. Blocking goes to 0x8002c878, the next-slot
// iterator, which needs the caller-saved r4 / r5 the prologue stashes.
CODEPATCH_HOOKCONDITIONALCREATE(0x8002c52c,
    "stwu 1, -16(1)\n\t"
    "stw 4, 8(1)\n\t"
    "stw 5, 12(1)\n\t",
    GateMachines_TRLobbyCanStart,
    "lwz 4, 8(1)\n\t"
    "lwz 5, 12(1)\n\t"
    "addi 1, 1, 16\n\t",
    0,
    0x8002c878
)

// Hook at 0x8002cc80 in TopRide_OnCourseSelect (0x8002cc30), the solo (Free Run / Time
// Attack) start body; the clobbered instruction is bl Gm_PlayPauseSFX. Blocking goes to
// 0x8002cddc, the no-Start path, which tests the held-button word in r3 for a B hold, so
// the epilogue reloads it from the pad entry r30 points 8 bytes past.
CODEPATCH_HOOKCONDITIONALCREATE(0x8002cc80,
    "",
    GateMachines_TRLobbyCanStart,
    "lwz 3, -8(30)\n\t",
    0,
    0x8002cddc
)

// Replaces the per-kind checklist query in CityMachineSpawn_PickFreeRunKind (0x801de41c),
// Free Run's one-of-every-machine picker; the beq widened at 0x801de518 routes every kind
// here.
static int GateMachines_CheckFreeRunKindUnlocked(MachineKind kind)
{
    if (kind < 0 || kind >= CustomMachines_KindNum(cm_api))
        return 0;
    return IsKindUnlocked(kind);
}

// City Trial field spawn weight. default_weight is the vanilla table's chance, or a
// registered machine's descriptor spawn_weight.
float GateMachines_SpawnWeight(int kind, float default_weight)
{
    if (kind < 0 || kind >= CustomMachines_KindNum(cm_api))
        return 0.0f;
    if (kind < VCKIND_NUM && (CT_SPAWN_EXCLUDED_MASK & (1u << kind)))
        return 0.0f;
    if (!IsKindUnlocked(kind))
        return 0.0f;

    // Registered machines take no fallback; a descriptor weight of 0 keeps one off the field.
    if (kind >= VCKIND_NUM || default_weight > 0.0f)
        return default_weight;
    return ZeroChanceSpawnWeight(kind);
}

// The unlock mask replaces the engine's select-screen checklist answer, appended
// characters included.
int GateMachines_FilterSelectCharacter(int ckind, int default_available)
{
    (void)default_available;
    return IsCKindUnlocked(ckind);
}

// MachineKind each player spawned on this scene, -1 for none.
static int stc_start_kind[PLY_NUM];

void GateMachines_On3DLoadEnd(void)
{
    for (int ply = 0; ply < PLY_NUM; ply++)
    {
        stc_start_kind[ply] = -1;
        if (Ply_GetRiderGObj(ply) != NULL)
            stc_start_kind[ply] = CustomMachines_ResolveKind(cm_api, Ply_GetMachineIsBike(ply),
                                                      Ply_GetMachineKind(ply));
    }
}

// Replaces Rider_ResetStartingMachine's hardcoded VCKIND_COMPACT respawn. A custom_machines
// mount wins over the machine the player spawned on.
static void GateMachines_ResetStartingMachine(RiderData *rd)
{
    u8 ply = rd->ply;
    int kind = CustomMachines_GetRespawnKind(cm_api, ply);
    int is_bike;
    int class_index;

    if (kind < 0)
        kind = stc_start_kind[ply];
    MachineKind vckind = (MachineKind)kind;
    if (kind < 0 || !IsKindUnlocked(vckind))
        vckind = GetFirstUnlockedCTMachine();

    class_index = CustomMachines_ClassIndexOf(cm_api, vckind, &is_bike);
    Ply_SetMachineIsBike(ply, is_bike);
    Ply_SetMachineKind(ply, class_index);
}

// Once per human or CPU slot. Stadium and Free Run pick on the machine grid and are left
// alone.
static void GateMachines_FinalizeCTMachine(int slot)
{
    GameData *gd = Gm_GetGameData();

    u8 kind = gd->city_select_ply.slot_kind[slot];
    if (kind != SELECTSLOT_HMN && kind != SELECTSLOT_CPU)
        return;

    if (gd->city_select_ply.mode != CITYMODE_TRIAL)
        return;

    CharacterKind ck;
    if (ap_menu_settings.ct_random_start_machine)
        ck = RandomUnlockedKirbyCKind();
    else
        ck = IsCKindUnlocked(CKIND_COMPACT) ? CKIND_COMPACT : RandomUnlockedKirbyCKind();
    gd->city_select_ply.ply_icon_ckind[slot] = (u8)ck;
}

// Hook at 0x8002dea0 (lbz r3, 97(r28)) in CitySelect_InitPlayerMachines (0x8002ddd8),
// where the Trial and Stadium / Free Run branches merge. r26 = slot. The re-run lbz
// reloads the ckind just written.
CODEPATCH_HOOKCREATE(0x8002dea0,
    "mr 3, 26\n\t",
    GateMachines_FinalizeCTMachine,
    "",
    0
)

// Hook at 0x801952c8 in Rider_ResetStartingMachine (0x80195288), r31 = RiderData. Replaces
// its two Ply_Set calls, exiting at the 0x801952e0 epilogue.
CODEPATCH_HOOKCREATE(0x801952c8,
    "mr 3, 31\n\t",
    GateMachines_ResetStartingMachine,
    "",
    0x801952e0
)

// Replaces TitleScreen_CheckMachineUnlocked (0x8000c364), the attract demo's machine-pick
// query. machine_class = CharacterDesc.is_bike, machine_id the class-relative slot.
static int GateMachines_CheckTitleDemoMachineUnlocked(s8 machine_class, s8 machine_id)
{
    int vckind = CustomMachines_ResolveKind(cm_api, machine_class, machine_id);

    if (vckind < 0 || vckind >= CustomMachines_KindNum(cm_api))
        return 0;

    return IsKindUnlocked(vckind);
}

void GateMachines_OnBoot()
{
    CODEPATCH_REPLACECALL(0x801de528, GateMachines_CheckFreeRunKindUnlocked);
    CODEPATCH_REPLACEINSTRUCTION(0x801de518, 0x4800000c); // b 0x801de524

    CODEPATCH_REPLACEFUNC(TitleScreen_CheckMachineUnlocked, GateMachines_CheckTitleDemoMachineUnlocked);

    CODEPATCH_HOOKAPPLY(0x8002dea0);
    CODEPATCH_HOOKAPPLY(0x801952c8);

    // The TR race and solo lobbies each have their own init, cycler and start handler.
    CODEPATCH_HOOKAPPLY(0x8002d070);
    CODEPATCH_HOOKAPPLY(0x8002d748);
    CODEPATCH_HOOKAPPLY(0x8002dc48);
    CODEPATCH_HOOKAPPLY(0x8002be44);
    CODEPATCH_HOOKAPPLY(0x8002cb98);
    CODEPATCH_HOOKAPPLY(0x8002c52c);
    CODEPATCH_HOOKAPPLY(0x8002cc80);

    OSReport("[GateMachines] Hooks installed\n");
}

// The star's bit is kept whether or not this build registered the star.
int GateMachines_UnlockMachine(int bit, int announce)
{
    if (bit < 0 || bit >= AP_MACHINE_BIT_NUM)
        return 0;

    ap_save->machine_unlocked_mask |= (1u << bit);

    const char *name = bit == AP_MACHINE_BIT_AP_STAR ? AP_STAR_MACHINE_NAME : MachineKind_Names[bit];
    if (!ap_regrant_quiet)
        OSReport("[GateMachines] Machine %d (%s) unlocked (mask = %s)\n", bit, name,
                 MaskBits(ap_save->machine_unlocked_mask, AP_MACHINE_BIT_NUM));
    if (announce)
    {
        // These bits are the King Dedede / Meta Knight character unlocks.
        const char *prefix = "Unlocked Machine: ";
        if (bit == VCKIND_WHEELDEDEDE)
        {
            prefix = "Unlocked Character: ";
            name   = "King Dedede";
        }
        else if (bit == VCKIND_WINGMETAKNIGHT)
        {
            prefix = "Unlocked Character: ";
            name   = "Meta Knight";
        }
        APAnnounce_Grant(prefix, name, tb_api->MachineColor, NULL);
    }
    return 1;
}

// machine_index 0 = Dragoon, 1 = Hydra. custom_machines owns the cutscene and refuses it
// while it can't run, which keeps the item queued; the engine holds one cutscene, so it
// lands on the first human that can take it.
int GateMachines_GiveLegendaryMachine(int machine_index)
{
    MachineKind kind = (machine_index == 0) ? VCKIND_DRAGOON : VCKIND_HYDRA;

    // No later round changes a build without custom_machines.
    if (!cm_api)
        return AP_ITEM_DROP;

    for (int i = 0; i < PLY_NUM; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        if (cm_api->StartAssembly(kind, i))
            return AP_ITEM_APPLIED;
    }
    return AP_ITEM_RETRY;
}
