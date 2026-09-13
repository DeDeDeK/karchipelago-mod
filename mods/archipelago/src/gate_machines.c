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

// Machines that don't naturally spawn in CT: Top Ride stars, transformation forms,
// and the Meta Knight / Dedede character forms. All have a 0 base spawn chance, so
// without exclusion the unlocked-but-zero-chance fallback below would leak them
// onto the City Trial field.
#define CT_SPAWN_EXCLUDED_MASK     \
    ((1u << VCKIND_FREE)         | \
     (1u << VCKIND_STEER)        | \
     (1u << VCKIND_WINGKIRBY)    | \
     (1u << VCKIND_WINGMETAKNIGHT) | \
     (1u << VCKIND_WHEELNORMAL)  | \
     (1u << VCKIND_WHEELKIRBY)   | \
     (1u << VCKIND_WHEELDEDEDE)  | \
     (1u << VCKIND_WHEELVSDEDEDE))

// Weight handed to an unlocked machine the vanilla table gives 0 chance, so it can
// still appear on the field. Only these four vanilla kinds reach it - every other
// VCKIND either carries a real weight in all three table windows or sits in
// CT_SPAWN_EXCLUDED_MASK. Set well under the per-machine weights the table does carry.
static float ZeroChanceSpawnWeight(int vckind)
{
    switch (vckind)
    {
    case VCKIND_COMPACT: return 5.0f;
    case VCKIND_FLIGHT:  return 2.0f;
    default:             return 1.0f; // Hydra, Dragoon
    }
}

static int IsCKindUnlocked(CharacterKind ckind)
{
    if (ckind < 0 || ckind >= CharacterKind_Num())
        return 0;
    CharacterDesc *desc = Character_GetDesc(ckind);
    if (!desc)
        return 0;
    MachineKind vckind = MachineKind_Resolve(desc->is_bike, desc->machine_kind);
    return MachineKind_IsUnlocked(vckind);
}

// First unlocked City-Trial-spawnable MachineKind, or VCKIND_COMPACT as fallback.
static MachineKind GetFirstUnlockedCTMachine()
{
    for (int i = 0; i < MachineKind_Num(); i++)
    {
        if (i < VCKIND_NUM && (CT_SPAWN_EXCLUDED_MASK & (1u << i)))
            continue;
        if (MachineKind_IsUnlocked(i))
            return i;
    }
    return VCKIND_COMPACT;
}

// Random unlocked Kirby-rider CharacterKind for a City Trial starting machine.
// Excludes CKIND_DEDEDE / CKIND_METAKNIGHT: vanilla's HUD loader skips their
// rider-specific 3D HUD assets in Base CT, so picking them for the free-roam Trial
// start NULL-derefs 3DHud_CreateSpeedometerInner during scene init.
static CharacterKind RandomUnlockedKirbyCKind(void)
{
    int unlocked_count = 0;
    for (int ckind = 0; ckind < CharacterKind_Num(); ckind++)
    {
        if (ckind == CKIND_DEDEDE || ckind == CKIND_METAKNIGHT)
            continue;
        if (IsCKindUnlocked(ckind))
            unlocked_count++;
    }

    if (unlocked_count == 0)
        return CKIND_COMPACT;

    int pick = HSD_Randi(unlocked_count);
    for (int ckind = 0; ckind < CharacterKind_Num(); ckind++)
    {
        if (ckind == CKIND_DEDEDE || ckind == CKIND_METAKNIGHT)
            continue;
        if (IsCKindUnlocked(ckind) && pick-- == 0)
            return ckind;
    }
    return CKIND_COMPACT;
}

// TR's lobby "Control Type" row (Free Star = 0, Steer Star = 1) maps 1:1 to
// MachineKind, so one machine_unlocked_mask covers Air Ride, City Trial, and Top Ride.
static int IsTRMachineUnlocked(TopRideMachineKind tr)
{
    MachineKind vckind = TOPRIDE_MACHINE_TO_VCKIND(tr);
    return MachineKind_IsUnlocked(vckind);
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

// Post-init fixup for the three TR lobby init paths, each of whose per-slot loop
// writes panel_machine[slot] = 0 (Free Star) unconditionally. All three hook sites
// sit past their loop, so panel_pkind is filled by the time this runs: at
// InitSelectData every panel reads CPU, at SoloInit every panel but the active one.
static void GateMachines_FixupTRInit(void)
{
    GameData *gd = Gm_GetGameData();
    if (!gd)
        return;

    u8 *pkind   = gd->topride_select_ply.panel_pkind;
    u8 *color   = gd->topride_select_ply.color;
    u8 *machine = gd->topride_select_ply.panel_machine;
    TopRideMachineKind first = GetFirstUnlockedTRMachine();

    for (int i = 0; i < 4; i++)
    {
        if (pkind[i] != 2) // not a CPU panel
        {
            machine[i] = (u8)first;
            continue;
        }

        // panel_pkind: 1 = HMN, 2 = CPU. Only the visible panels' colors are
        // worth avoiding.
        u8 taken[4];
        int num_taken = 0;
        for (int j = 0; j < 4; j++)
        {
            if (j != i && (pkind[j] == 1 || pkind[j] == 2))
                taken[num_taken++] = color[j];
        }
        machine[i] = (u8)GetRandomUnlockedTRMachine();
        color[i] = (u8)GateColors_RandomUnlockedColorExcept(taken, num_taken);
    }
}

// Hook at 0x8002d070 in TopRide_InitSelectData, just after the per-slot init loop
// (0x8002d06c is already hooked). The three following `stb r3, {6,2,3}(r31)`
// lobby-flag clears rely on r3 = 0, which the C call wipes.
CODEPATCH_HOOKCREATE(0x8002d070,
    "",
    GateMachines_FixupTRInit,
    "li 3, 0\n\t",
    0
)

// Race-init counterpart. TopRide_RaceInit (0x8002d6c4) re-zeros all four
// panel_machine slots. Hook at 0x8002d748 (`bl Gm_GetGameData`), past the panel_pkind
// CPU-fill loop whose caller-saved iterator r7 rules out landing earlier; the
// re-executed bl restores r3 = GameData*, so no epilogue is needed.
CODEPATCH_HOOKCREATE(0x8002d748,
    "",
    GateMachines_FixupTRInit,
    "",
    0
)

// L/R cycler gate for the lobby "Control Type" row: the vanilla 0..1 clamp, minus
// writes onto a locked machine. Both lobby flavors (race TopRide_CSS_PanelThink
// 0x8002b8a8 and solo TopRide_SoloPanelThink 0x8002ca80) carry identical cyclers,
// so one gate serves both hook sites. panel_base[0x2f] = panel_machine[panel];
// input_bits = direction-edge bits (0x80002 = RIGHT, 0x40001 = LEFT).
static int GateMachines_CycleTRMachine(u8 *panel_base, u32 input_bits)
{
    u8 current = panel_base[0x2f];
    u8 new_val = current;

    if ((input_bits & 0x80002) != 0)
    {
        if (current < (TR_MACHINE_NUM - 1) && IsTRMachineUnlocked(current + 1))
            new_val = current + 1;
    }
    else if ((input_bits & 0x40001) != 0)
    {
        if (current > 0 && IsTRMachineUnlocked(current - 1))
            new_val = current - 1;
    }

    if (new_val == current)
        return 0;

    panel_base[0x2f] = new_val;
    return 1;
}

// Race-lobby cycler hook at 0x8002be44 in TopRide_CSS_PanelThink, replacing the
// cycler+compare block (..0x8002be94). r26 = panel base, r29 = direction-edge bits.
//   r3 == 0 -> 0x8002c054 (function end); r3 != 0 -> 0x8002be98 (SFX + UI update)
CODEPATCH_HOOKCONDITIONALCREATE(0x8002be44,
    "mr 3, 26\n\t"
    "mr 4, 29\n\t",
    GateMachines_CycleTRMachine,
    "",
    0x8002c054,
    0x8002be98
)

// Solo-lobby (Free Run / Time Attack) cycler hook at 0x8002cb98 in
// TopRide_SoloPanelThink, replacing the cycler+compare through the beq at 0x8002cbec.
// r30 = panel base, r26 = direction-edge bits, both callee-saved so the downstream
// SFX/UI block finds them intact.
//   r3 == 0 -> 0x8002cc18 (function end); r3 != 0 -> 0x8002cbf0 (SFX + UI update)
CODEPATCH_HOOKCONDITIONALCREATE(0x8002cb98,
    "mr 3, 30\n\t"
    "mr 4, 26\n\t",
    GateMachines_CycleTRMachine,
    "",
    0x8002cc18,
    0x8002cbf0
)

// Solo-mode counterpart. TopRide_SoloInit (0x8002d9e8) hardcodes all four
// panel_machine slots to 0 at 0x8002db70, bypassing InitSelectData. Hook at
// 0x8002dc48, the first instruction past the per-slot loop - 0x8002db90 is that
// loop's back-edge target, so hooking there ran the fixup once per slot. The
// clobbered `stb r0, 6(r31)` and the two stores after it all want r0 = 0.
CODEPATCH_HOOKCREATE(0x8002dc48,
    "",
    GateMachines_FixupTRInit,
    "li 0, 0\n\t",
    0
)

// Start-match gate for the TR lobby: with both Free and Steer locked the panel still
// defaults to Free, so Start would launch a machine the player doesn't own. Both hook
// sites reach this only on the Start rising edge, so the buzzer fires once per press.
// Returns 0 = allow start, 1 = block start.
static int GateMachines_TRLobbyCanStart(void)
{
    u32 tr_mask = (1u << VCKIND_FREE) | (1u << VCKIND_STEER);
    if (ap_save->machine_unlocked_mask & tr_mask)
        return 0;

    playSoundFX_errorNoise();
    tb_api->EnqueueColoredNoun("Unlock a ", "Top Ride machine", tb_api->MachineColor, " to start!");
    return 1;
}

// Hook at 0x8002c52c in TopRide_PreGameThink, first instruction of the race
// "start match" body (vanilla `bl` menu-confirm sound).
//   r3 == 0 -> run clobbered bl (play sound), fall through to commit+launch
//   r3 != 0 -> jump to 0x8002c878 (next-slot iterator, skip start)
// The gate is non-leaf, so the prologue stashes the caller-saved r4/r5 that the loop
// continuation at 0x8002c878 needs.
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

// Hook at 0x8002cc80 in TopRide_OnCourseSelect, the solo (Free Run / Time Attack)
// "start match" body; the clobbered instruction is the same `bl` menu-confirm SFX.
//   r3 == 0 -> run clobbered bl (play sound), fall through to commit+launch
//   r3 != 0 -> jump to 0x8002cddc (epilogue, skip start)
CODEPATCH_HOOKCONDITIONALCREATE(0x8002cc80,
    "",
    GateMachines_TRLobbyCanStart,
    "",
    0,
    0x8002cddc
)

// Answers the per-kind candidate test inside CityMachineSpawn_PickFreeRunKind
// (0x801de41c), Free Run's "place one of every machine" picker. Vanilla routes only
// kinds 4 and 8 here, to a checklist query AP never writes, and takes every other
// kind unconditionally; the widened branch below sends all 26 through, so the city
// holds one of each unlocked machine instead of the whole roster. A kind the vanilla
// per-kind spawn table already rules out never reaches the call.
int GateMachines_CheckFreeRunKindUnlocked(MachineKind kind)
{
    if (kind < 0 || kind >= MachineKind_Num())
        return 0;
    return MachineKind_IsUnlocked(kind);
}

// Weight filter handed to custom_machines, which owns the City Trial field spawn
// roll. `default_weight` is VcCommon.dat's chance for a vanilla kind in the window
// being rolled, and the descriptor's spawn_weight for a registered one.
float GateMachines_SpawnWeight(int kind, float default_weight)
{
    if (kind < 0 || kind >= MachineKind_Num())
        return 0.0f;
    if (kind < VCKIND_NUM && (CT_SPAWN_EXCLUDED_MASK & (1u << kind)))
        return 0.0f;
    if (!MachineKind_IsUnlocked(kind))
        return 0.0f;

    // A registered machine brings its own weight and takes no fallback: a descriptor
    // asking for 0 keeps it off the field however the mask reads.
    if (kind >= VCKIND_NUM || default_weight > 0.0f)
        return default_weight;
    return ZeroChanceSpawnWeight(kind);
}

// Availability filter handed to custom_machines, which owns both select screens'
// packing. The mask is the only rule here, so the engine's own checklist answer in
// `default_available` is discarded - including for appended characters, which are
// unconditional there but gated like anything else once AP hands them out.
int GateMachines_FilterSelectCharacter(int ckind, int default_available)
{
    (void)default_available;
    return IsCKindUnlocked(ckind);
}

// Replaces the respawn machine assignment in Rider_ResetStartingMachine, which
// hardcodes VCKIND_COMPACT.
void GateMachines_ResetStartingMachine(RiderData *rd)
{
    u8 ply = rd->ply;
    MachineKind vckind = rd->starting_machine_idx;
    int is_bike;
    int class_index;

    if (!MachineKind_IsUnlocked(vckind))
        vckind = GetFirstUnlockedCTMachine();

    class_index = MachineKind_ClassIndexOf(vckind, &is_bike);
    Ply_SetMachineIsBike(ply, is_bike);
    Ply_SetMachineKind(ply, class_index);
}

// Finalize the City Trial starting machine at the convergence point of
// CitySelect_InitPlayerMachines (0x8002dea0), where the Trial and Stadium / Free Run
// branches merge. Fires once per slot.
//   x215[slot]: 0 = human, 2 = CPU, else inactive.
//   x1d0: 0 = Trial (no machine grid), nonzero = Stadium / Free Run, which pick their
//   machine on the grid and are left alone.
void GateMachines_FinalizeCTMachine(int slot)
{
    GameData *gd = Gm_GetGameData();
    if (!gd)
        return;

    u8 kind = gd->city_select_ply.x215[slot];
    if (kind != 0 && kind != 2)
        return; // inactive slot

    if (gd->city_select_ply.x1d0 != 0)
        return;

    CharacterKind ck;
    if (ap_menu_settings.ct_random_start_machine)
        ck = RandomUnlockedKirbyCKind();
    else
        ck = IsCKindUnlocked(CKIND_COMPACT) ? CKIND_COMPACT : RandomUnlockedKirbyCKind();
    gd->city_select_ply.ply_icon_ckind[slot] = (u8)ck;
}

// Hook at the convergence point 0x8002dea0 (`lbz r3, 97(r28)`) in
// CitySelect_InitPlayerMachines. r26 = slot index. Skip target 0 re-executes the
// clobbered lbz, reloading the ckind just written for the Character_GetDesc lookup.
CODEPATCH_HOOKCREATE(0x8002dea0,
    "mr 3, 26\n\t",
    GateMachines_FinalizeCTMachine,
    "",
    0
)

// Hook at 0x801952c8 in Rider_ResetStartingMachine. r31 = RiderData*; replaces the
// two Ply_Set calls (is_bike=0, machine_kind=COMPACT). Skip to 0x801952e0 (epilogue).
CODEPATCH_HOOKCREATE(0x801952c8,
    "mr 3, 31\n\t",
    GateMachines_ResetStartingMachine,
    "",
    0x801952e0
)

// Replaces AirRide_CheckCharacterAvailable (0x8002090c), which decides who appears on
// the Air Ride character select screen from checklist reward indices. Vanilla also
// hardcodes Compact Star, Dragoon, Hydra and Flight Warp Star out of Air Ride whatever
// the save holds; the mask is the only rule here, so an owned machine is selectable in
// every mode whose select screen offers it. The icon archive backs all 20 characters.
int GateMachines_CheckAirRideCharacterAvailable(CharacterKind ckind)
{
    return IsCKindUnlocked(ckind);
}

// Replaces TitleScreen_CheckMachineUnlocked (0x8000c364), the unlock query for the
// title-screen attract demo's random machine picker (TitleScreen_SelectRandomMachine,
// 0x8000daa0). It does NOT run for CPUs in real Air Ride races, which draw from the
// gated character list in loadCPU.
int GateMachines_CheckTitleDemoMachineUnlocked(s8 machine_class, s8 machine_id)
{
    // machine_class = CharacterDesc.is_bike, machine_id = CharacterDesc.machine_kind,
    // a class-relative slot rather than the VCKIND.
    int vckind = MachineKind_Resolve(machine_class, machine_id);

    if (vckind < 0 || vckind >= MachineKind_Num())
        return 0;

    return MachineKind_IsUnlocked(vckind);
}

void GateMachines_OnBoot()
{
    // Free Run's picker asks the checklist whether the legendaries are unlocked. Widening
    // the `beq` that guards that call into an unconditional branch puts every kind through
    // it, so the mask decides Free Run's city roster the way it decides the field's.
    CODEPATCH_REPLACECALL(0x801de528, GateMachines_CheckFreeRunKindUnlocked);
    CODEPATCH_REPLACEINSTRUCTION(0x801de518, 0x4800000c); // b 0x801de524

    CODEPATCH_REPLACEFUNC(AirRide_CheckCharacterAvailable, GateMachines_CheckAirRideCharacterAvailable);
    CODEPATCH_REPLACEFUNC(TitleScreen_CheckMachineUnlocked, GateMachines_CheckTitleDemoMachineUnlocked);

    CODEPATCH_HOOKAPPLY(0x8002dea0);  // CT starting-machine finalize
    CODEPATCH_HOOKAPPLY(0x801952c8);  // CT respawn machine validation

    // The TR race and solo (Free Run / Time Attack) lobbies are separate code paths,
    // each with its own init, cycler, and start-match handler.
    CODEPATCH_HOOKAPPLY(0x8002d070);  // TopRide_InitSelectData post-loop fixup (main-menu reset)
    CODEPATCH_HOOKAPPLY(0x8002d748);  // TopRide_RaceInit post-reset fixup (TR Main Game)
    CODEPATCH_HOOKAPPLY(0x8002dc48);  // TopRide_SoloInit post-zero fixup (Free Run / Time Attack)
    CODEPATCH_HOOKAPPLY(0x8002be44);  // TopRide_CSS_PanelThink L/R cycler (race lobby)
    CODEPATCH_HOOKAPPLY(0x8002cb98);  // TopRide_SoloPanelThink L/R cycler (Free Run / Time Attack)
    CODEPATCH_HOOKAPPLY(0x8002c52c);  // TopRide_PreGameThink start-match gate (race)
    CODEPATCH_HOOKAPPLY(0x8002cc80);  // TopRide_OnCourseSelect start-match gate (solo)

    OSReport("[GateMachines] Hooks installed\n");
}

// Display name for any MachineKind, vanilla or registered custom.
static const char *GateMachines_GetName(MachineKind kind)
{
    if (kind >= 0 && kind < VCKIND_NUM)
        return MachineKind_Names[kind];
    const char *custom = cm_api ? cm_api->GetName(kind) : NULL;
    return custom ? custom : "Unknown Machine";
}

int GateMachines_UnlockMachine(MachineKind kind, int announce)
{
    if (kind < 0 || kind >= MachineKind_Num())
        return 0;

    if (kind < AP_MACHINE_GATE_NUM)
        ap_save->machine_unlocked_mask |= (1u << kind);

    if (!ap_regrant_quiet)
    {
        if (kind < AP_MACHINE_GATE_NUM)
            OSReport("[GateMachines] Machine %d (%s) unlocked (mask = %s)\n",
                     kind, GateMachines_GetName(kind),
                     MaskBits(ap_save->machine_unlocked_mask, 32));
        else
            OSReport("[GateMachines] Machine %d (%s) is past bit %d - always unlocked, not persisted\n",
                     kind, GateMachines_GetName(kind), AP_MACHINE_GATE_NUM - 1);
    }
    if (announce)
    {
        // VCKIND_WHEELDEDEDE / VCKIND_WINGMETAKNIGHT are the player-facing King Dedede
        // / Meta Knight unlocks, announced to match the checklist reward path.
        const char *prefix = "Unlocked Machine: ";
        const char *name   = GateMachines_GetName(kind);
        if (kind == VCKIND_WHEELDEDEDE)
        {
            prefix = "Unlocked Character: ";
            name   = "King Dedede";
        }
        else if (kind == VCKIND_WINGMETAKNIGHT)
        {
            prefix = "Unlocked Character: ";
            name   = "Meta Knight";
        }
        APAnnounce_Grant(prefix, name, tb_api->MachineColor, NULL);
    }
    return 1;
}

// Give a player the assembled legendary machine via the cutscene. machine_index:
// 0 = Dragoon, 1 = Hydra. Returns 1 if started (consume the item), 0 if it can't
// run yet (keep queued and retry).
//
// custom_machines owns the cutscene and every condition on it - City Trial only, a
// Kirby rider, one run at a time, and each vanilla legendary at most once per scene
// because the engine frees its piece archive on the way out. The engine holds a
// single cutscene, so this lands on the first human it can rather than every one.
int GateMachines_GiveLegendaryMachine(int machine_index)
{
    MachineKind kind = (machine_index == 0) ? VCKIND_DRAGOON : VCKIND_HYDRA;

    if (!cm_api)
        return 0;

    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        if (cm_api->StartAssembly(kind, i))
            return 1;
    }
    return 0;
}
