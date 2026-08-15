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

// Sizes the spawn-chance scratch array; the runtime ceiling is MachineKind_Num().
#define MACHINE_KIND_CEILING (VCKIND_NUM + CUSTOM_MACHINE_MAX)

// The Air Ride select screen keeps the icons it offers at its own base in GameData:
// a count, then one CharacterKind per icon. The row-split flag sits one byte past the
// list, so the list can never carry more than SELECT_ICON_MAX entries.
#define AIRRIDE_SELECT_BASE 0x10a
#define SELECT_COUNT        0x65
#define SELECT_LIST         0x66
#define SELECT_ROW_SPLIT    0x7a
#define SELECT_DEBUG_GRID   0x7b
#define SELECT_ICON_MAX     20

// Columns per grid row, and so also the count at which a drawn row is full and the
// icons wrap to two rows.
#define SELECT_GRID_COLS    10

// Weight handed to an unlocked machine the vanilla table gives 0 chance, so it can
// still appear on the field. Only these four vanilla kinds reach it - every other
// VCKIND either carries a real weight in all three table windows or sits in
// CT_SPAWN_EXCLUDED_MASK - plus every registered custom machine, which has no row
// in the table at all and brings its own weight from its descriptor.
// Vanilla per-machine weights run 6-10 out of a ~111-119 table total, so these land
// well under the machines the table actually wants: Compact ~4% of spawns, Flight
// ~1.7%, each legendary ~0.8%.
static float ZeroChanceSpawnWeight(int vckind)
{
    if (vckind >= VCKIND_NUM)
        return cm_api ? cm_api->GetSpawnWeight(vckind) : 0.0f;

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
    return (ap_save->machine_unlocked_mask & (1 << vckind)) ? 1 : 0;
}

// First unlocked City-Trial-spawnable MachineKind, or VCKIND_COMPACT as fallback.
static MachineKind GetFirstUnlockedCTMachine()
{
    u32 mask = ap_save->machine_unlocked_mask;
    for (int i = 0; i < MachineKind_Num(); i++)
    {
        if (i < VCKIND_NUM && (CT_SPAWN_EXCLUDED_MASK & (1u << i)))
            continue;
        if (mask & (1 << i))
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
    return (ap_save->machine_unlocked_mask & (1u << vckind)) ? 1 : 0;
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

// Post-init fixup for TopRide_InitSelectData (0x8002cfd8), whose per-slot loop
// unconditionally writes panel_machine[slot] = 0 (Free Star). Only the RaceInit site
// (0x8002d748) runs after the panel-kind field is filled, so the other sites see
// non-CPU and fall through to first-unlocked.
void GateMachines_FixupTRInit(u8 *lobby_base)
{
    TopRideMachineKind first = GetFirstUnlockedTRMachine();
    // Relative to lobby base (GameData+0x197): 0x2f = panel_machine[slot], 0x23 = color[slot].
    for (int i = 0; i < 4; i++)
    {
        if (lobby_base[0x1b + i] == 2) // CPU panel
        {
            lobby_base[0x2f + i] = (u8)GetRandomUnlockedTRMachine();
            lobby_base[0x23 + i] = (u8)GateColors_RandomUnlockedColor();
        }
        else
            lobby_base[0x2f + i] = (u8)first;
    }
}

// Hook at 0x8002d070 in TopRide_InitSelectData, just after the per-slot init loop
// (0x8002d06c is already hooked). r31 = lobby base. The three following
// `stb r3, {6,2,3}(r31)` lobby-flag clears rely on r3 = 0, which the C call wipes.
CODEPATCH_HOOKCREATE(0x8002d070,
    "mr 3, 31\n\t",
    GateMachines_FixupTRInit,
    "li 3, 0\n\t",
    0x8002d074
)

// Race-init counterpart. TopRide_RaceInit re-zeros all four panel_machine slots at
// 0x8002d6c4, after InitSelectData's fixup. Hook at 0x8002d748 (`bl gmGetGlobalP`),
// past the panel_pkind CPU-fill loop whose caller-saved iterator r7 rules out landing
// earlier; the re-executed bl restores r3 = GameData*, so no epilogue is needed.
CODEPATCH_HOOKCREATE(0x8002d748,
    "mr 3, 31\n\t",
    GateMachines_FixupTRInit,
    "",
    0
)

// L/R cycler gate for the lobby "Control Type" row: the vanilla 0..1 clamp, minus
// writes onto a locked machine. Both lobby flavors (race TopRide_CSS_PanelThink
// 0x8002b8a8 and solo TopRide_SoloPanelThink 0x8002ca80) carry identical cyclers,
// so one gate serves both hook sites. panel_base[0x2f] = panel_machine[panel];
// input_bits = direction-edge bits (0x80002 = RIGHT, 0x40001 = LEFT).
int GateMachines_CycleTRMachine(u8 *panel_base, u32 input_bits)
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

// Solo-mode counterpart. TopRide_SoloInit hardcodes all four panel_machine slots to 0
// at 0x8002db70, bypassing InitSelectData. Hook at 0x8002db90 (`add r30, r31, r28`),
// one instruction past the already-hooked `li r28, 0`, so r28 = 0 and r31 = lobby base.
CODEPATCH_HOOKCREATE(0x8002db90,
    "mr 3, 31\n\t",
    GateMachines_FixupTRInit,
    "",
    0
)

// Start-match gate for the TR lobby: with both Free and Steer locked the panel still
// defaults to Free, so Start would launch a machine the player doesn't own. Both hook
// sites reach this only on the Start rising edge, so the buzzer fires once per press.
// Returns 0 = allow start, 1 = block start.
int GateMachines_TRLobbyCanStart(void)
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

int GateMachines_SelectSpawn(MachineSpawnData *msd, float match_progress)
{
    u32 unlocked_mask = ap_save->machine_unlocked_mask;
    vcDataCommon *vc_data_common = (*stc_vcDataCommon);
    int kind_num = MachineKind_Num();

    int spawn_table_idx = 0;
    while (match_progress > vc_data_common->spawn_data->spawn_desc[spawn_table_idx].match_progress)
        spawn_table_idx++;

    // VcCommon.dat's chance row is authored with exactly VCKIND_NUM columns, so
    // registered custom kinds are never read from it - they start at 0 and pick up
    // their descriptor weight below.
    float spawn_chances[MACHINE_KIND_CEILING];
    for (int i = 0; i < kind_num; i++)
        spawn_chances[i] = (i < VCKIND_NUM)
                               ? vc_data_common->spawn_data->spawn_desc[spawn_table_idx].chance[i]
                               : 0.0f;

    for (int i = 0; i < kind_num; i++)
    {
        if (i < VCKIND_NUM && (CT_SPAWN_EXCLUDED_MASK & (1u << i)))
            spawn_chances[i] = 0;
        else if (!(unlocked_mask & (1u << i)))
            spawn_chances[i] = 0;
        else if (spawn_chances[i] == 0)
            spawn_chances[i] = ZeroChanceSpawnWeight(i);
    }

    int spawnable_count = 0;
    for (int i = 0; i < kind_num; i++)
    {
        if (spawn_chances[i] > 0)
            spawnable_count++;
    }

    if (spawnable_count == 0)
        return GetFirstUnlockedCTMachine();

    // Shrink history when few machines are spawnable so the only candidate can't be
    // excluded by its own history.
    int history_size = (spawnable_count <= 4) ? (spawnable_count - 1) : 4;
    for (int i = 0; i < kind_num; i++)
    {
        for (int j = 0; j < history_size; j++)
        {
            if (i == msd->prev_machine_kind[j])
                spawn_chances[i] = 0;
        }
    }

    int machine_kind = VCKIND_COMPACT;
    float chance_total = 0;
    for (int i = 0; i < kind_num; i++)
        chance_total += spawn_chances[i];

    float random_chance = HSD_Randf() * chance_total;
    chance_total = 0;
    for (int i = 0; i < kind_num; i++)
    {
        chance_total += spawn_chances[i];
        if (random_chance < chance_total)
        {
            machine_kind = i;
            break;
        }
    }

    // Vanilla code after the skip target (0x801df220 / 0x801df630) writes r31 to the
    // history buffer.
    return machine_kind;
}

// Replace the spawn selection in CityMachineSpawn_DecideAndSpawn (0x801defac).
// At 0x801df00c: r30 = MachineSpawnData* (-> r3), f1 = match_progress. Result -> r31,
// which feeds the history write and CityMachineSpawn_Create past the skip target.
CODEPATCH_HOOKCREATE(0x801df00c,
    "mr 3, 30\n\t",
    GateMachines_SelectSpawn,
    "mr 31, 3\n\t",
    0x801df220
)

// Replace the spawn selection in cityTrialSpawnFormationStar (0x801df408).
// Same register layout as DecideAndSpawn at its hook point.
CODEPATCH_HOOKCREATE(0x801df44c,
    "mr 3, 30\n\t",
    GateMachines_SelectSpawn,
    "mr 31, 3\n\t",
    0x801df630
)

// Availability filter handed to custom_machines, which owns both select screens'
// packing. The mask is the only rule here, so the engine's own checklist answer in
// `default_available` is discarded - including for appended characters, which are
// unconditional there but gated like anything else once AP hands them out.
int GateMachines_FilterSelectCharacter(int ckind, int default_available)
{
    (void)default_available;
    return IsCKindUnlocked(ckind);
}

static int CountUnlockedCharacters(void)
{
    int n = 0;

    for (int ckind = 0; ckind < CharacterKind_Num(); ckind++)
    {
        if (IsCKindUnlocked(ckind))
            n++;
    }
    return n;
}

int GateMachines_CountCTSelectAvailable(void)
{
    return CountUnlockedCharacters();
}

// Packs the unlocked characters of the 2x10 icon grid into the City Trial select
// screen's list, then lays the result out. Replaces the vanilla array-building pass
// and the reorder that follows it, which assumes vanilla's grid iteration (special
// characters at fixed col 0/9) and duplicates icons on a packed list.
int GateMachines_FillCityIcons(u8 *base)
{
    int n = 0;

    for (int i = 0; i < SELECT_ICON_MAX; i++)
        base[SELECT_LIST + i] = 0;

    for (int row = 0; row < 2; row++)
    {
        for (int col = 0; col < SELECT_GRID_COLS && n < SELECT_ICON_MAX; col++)
        {
            CharacterKind ckind = SelIcon_GetCKind(row, col);
            if (IsCKindUnlocked(ckind))
                base[SELECT_LIST + n++] = (u8)ckind;
        }
    }

    base[SELECT_COUNT] = (u8)n;

    CitySelect_LayoutMachineIcons((s8)n);
    for (int i = 0; i < n; i++)
        CitySelect_CreateMachineIcon((s8)base[SELECT_LIST + i], (s8)i);
    return n;
}

// Mode 1 (Stadium) and mode 2 (Free Run) counting passes of
// CitySelect_CreateMachineIcons. Result -> r27; exit past the loop where the mode is
// rechecked before the array-building pass. The clobbered `li r24, 0` at the mode 2
// site is harmless - r24 is unused after the loop this skips.
CODEPATCH_HOOKCREATE(0x8002e4d0,
    "",
    GateMachines_CountCTSelectAvailable,
    "mr 27, 3\n\t",
    0x8002e670
)

CODEPATCH_HOOKCREATE(0x8002e5c0,
    "",
    GateMachines_CountCTSelectAvailable,
    "mr 27, 3\n\t",
    0x8002e670
)

// Tail of CitySelect_CreateMachineIcons. r30 = the City Trial select base; the
// clobbered `stb r27, 101(r30)` stores the count the epilogue puts back in r27, and
// the exit skips the layout call and icon loop this replaces.
CODEPATCH_HOOKCREATE(0x8002f0b8,
    "mr 3, 30\n\t",
    GateMachines_FillCityIcons,
    "mr 27, 3\n\t",
    0x8002f220
)

// custom_machines owns the City Trial select screen's packing when it is built, and
// gating rides on the availability filter it takes. Without it nothing patches that
// screen at all, so the same gating is applied directly here. Idempotent, since the
// import that decides this is re-tried per call.
void GateMachines_OnCustomMachinesAbsent(void)
{
    static int applied;

    if (applied)
        return;
    applied = 1;

    CODEPATCH_HOOKAPPLY(0x8002e4d0);  // CT Stadium (mode 1) counting pass
    CODEPATCH_HOOKAPPLY(0x8002e5c0);  // CT Free Run (mode 2) counting pass
    CODEPATCH_HOOKAPPLY(0x8002f0b8);  // CT select list, layout and icons

    // The two array-building passes now have nothing to build: skip each straight to
    // the tail above, which also skips the reorder between them.
    CODEPATCH_REPLACEINSTRUCTION(0x8002e67c, 0x48000a3c);  // b 0x8002f0b8
    CODEPATCH_REPLACEINSTRUCTION(0x8002e738, 0x48000980);  // b 0x8002f0b8

    // CitySelect_Cursor1InputThink splits cursor rows at num>=10 (`cmpwi r3, 9; ble`),
    // but the grid renderer keeps up to 10 icons on one drawn row and only wraps at 11,
    // so at num==10 the cursor splits 5+5 across a single row. Vanilla CT only produces
    // counts 15-20; a gated roster can land on exactly 10.
    CODEPATCH_REPLACEINSTRUCTION(0x80031350, 0x2c03000a);  // cmpwi r3, 10

    OSReport("[GateMachines] CT select screen gated standalone\n");
}

// Replaces the respawn machine assignment in Rider_ResetStartingMachine, which
// hardcodes VCKIND_COMPACT.
void GateMachines_ResetStartingMachine(RiderData *rd)
{
    u8 ply = rd->ply;
    MachineKind vckind = rd->starting_machine_idx;

    if (!(ap_save->machine_unlocked_mask & (1 << vckind)))
        vckind = GetFirstUnlockedCTMachine();

    if (vckind >= VCKIND_WHEELNORMAL)
    {
        Ply_SetMachineIsBike(ply, 1);
        Ply_SetMachineKind(ply, vckind - VCKIND_WHEELNORMAL);
    }
    else
    {
        Ply_SetMachineIsBike(ply, 0);
        Ply_SetMachineKind(ply, vckind);
    }
}

// CT machine-select slots the player explicitly picked on the grid this session
// (bit = slot). A manual CPU pick suppresses that slot's random-start-machine re-roll.
static u8 ct_machine_manual_pick_mask = 0;

void GateMachines_NoteManualMachinePick(int slot)
{
    if (slot >= 0 && slot < 4)
        ct_machine_manual_pick_mask |= (u8)(1 << slot);
}

// Finalize the City Trial starting machine at the convergence point of
// CitySelect_InitPlayerMachines (0x8002dea0), where the Trial and Stadium / Free Run
// branches merge. Fires once per slot.
//   x215[slot]: 0 = human, 2 = CPU, else inactive.
//   x1d0: 0 = Trial (no machine grid), nonzero = Stadium / Free Run.
void GateMachines_FinalizeCTMachine(int slot)
{
    GameData *gd = Gm_GetGameData();
    if (!gd)
        return;

    // Cleared even on the inactive-slot early return below, so it never leaks into
    // the next match.
    u8 manual_pick = (slot >= 0 && slot < 4) &&
                     (ct_machine_manual_pick_mask & (1 << slot));
    if (slot >= 0 && slot < 4)
        ct_machine_manual_pick_mask &= (u8)~(1 << slot);

    u8 kind = gd->city_select_ply.x215[slot];
    if (kind != 0 && kind != 2)
        return; // inactive slot

    // Independent of the machine toggle; humans keep their CSS color pick.
    if (kind == 2)
        gd->city_select_ply.ply_color[slot] = (u8)GateColors_RandomUnlockedColor();

    if (gd->city_select_ply.x1d0 == 0)
    {
        CharacterKind ck;
        if (ap_menu_settings.ct_random_start_machine)
            ck = RandomUnlockedKirbyCKind();
        else
            ck = IsCKindUnlocked(CKIND_COMPACT) ? CKIND_COMPACT : RandomUnlockedKirbyCKind();
        gd->city_select_ply.ply_icon_ckind[slot] = (u8)ck;
    }
    else if (kind == 2 && !manual_pick && ap_menu_settings.ct_random_start_machine)
    {
        u8 num = gd->city_select_ply.machine_select.num;
        if (num > 0)
            gd->city_select_ply.ply_icon_ckind[slot] =
                gd->city_select_ply.machine_select.c_kind_arr[HSD_Randi(num)];
    }
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

// Hook the icon[slot] store in CitySelect_Cursor1InputThink (0x800315ac,
// `stb r27, 45(r30)`) - the sole player-driven machine-grid pick, since it runs only
// when the chosen grid index changes. r29 = slot.
CODEPATCH_HOOKCREATE(0x800315ac,
    "mr 3, 29\n\t",
    GateMachines_NoteManualMachinePick,
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

// Replaces AirRide_PopulateSelectIcons (0x80020a08). Vanilla packs the grid into two
// rows and then rebalances them, and that rebalance hangs the console whenever row 0
// ends up more than three icons ahead of row 1: its row0-heavy half-step re-reads the
// counts and undoes the move it just made instead of mirroring the other branch, so
// the rows oscillate forever. Vanilla can never reach that gap, because Compact Star,
// Dragoon, Hydra and Flight Warp Star are hardcoded out of Air Ride and all four sit
// in row 0, capping it at six of ten - but handing them back on the mask lets row 0
// reach ten. Packing in grid order and laying the result out directly skips the
// rebalance, which only ever reordered icons for looks.
void GateMachines_PopulateAirRideIcons(void)
{
    u8 *base = (u8 *)Gm_GetGameData() + AIRRIDE_SELECT_BASE;
    int n = 0;

    for (int i = 0; i < SELECT_ICON_MAX; i++)
        base[SELECT_LIST + i] = 0;

    if (base[SELECT_DEBUG_GRID] && *stc_dblevel > DB_DEBUG_DEVELOP)
    {
        // The debug grid shows every character, gated or not.
        for (int row = 0; row < 2; row++)
        {
            for (int col = 0; col < SELECT_GRID_COLS && n < SELECT_ICON_MAX; col++)
                base[SELECT_LIST + n++] = (u8)SelIcon_GetCKind(row, col);
        }
    }
    else if (CountUnlockedCharacters() < SELECT_GRID_COLS)
    {
        // Icons that fit on one drawn row take their order from the one-row strip.
        for (int i = 0; i < CharacterKind_Num() && n < SELECT_ICON_MAX; i++)
        {
            CharacterKind ckind = SelIcon_GetCKindLinear(i);
            if (IsCKindUnlocked(ckind))
                base[SELECT_LIST + n++] = (u8)ckind;
        }
    }
    else
    {
        for (int row = 0; row < 2; row++)
        {
            for (int col = 0; col < SELECT_GRID_COLS && n < SELECT_ICON_MAX; col++)
            {
                CharacterKind ckind = SelIcon_GetCKind(row, col);
                if (IsCKindUnlocked(ckind))
                    base[SELECT_LIST + n++] = (u8)ckind;
            }
        }
    }

    base[SELECT_COUNT] = (u8)n;
    // AirRideSelect_Cursor1InputThink splits its cursor rows on the same threshold.
    base[SELECT_ROW_SPLIT] = (n >= SELECT_GRID_COLS) ? 1 : 0;

    AirRideSelect_LayoutIcons((s8)n);
    for (int i = 0; i < n; i++)
        AirRideSelect_CreateSIcon((s8)base[SELECT_LIST + i], (s8)i);
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

    return (ap_save->machine_unlocked_mask & (1 << vckind)) ? 1 : 0;
}

void GateMachines_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x801df00c);  // CityMachineSpawn_DecideAndSpawn selection
    CODEPATCH_HOOKAPPLY(0x801df44c);  // cityTrialSpawnFormationStar selection

    CODEPATCH_REPLACEFUNC(AirRide_CheckCharacterAvailable, GateMachines_CheckAirRideCharacterAvailable);
    CODEPATCH_REPLACEFUNC(TitleScreen_CheckMachineUnlocked, GateMachines_CheckTitleDemoMachineUnlocked);

    // custom_machines boots after us and replaces this again with the packing its
    // widened grid needs; the later patch wins and both pack the same list below 21
    // icons.
    CODEPATCH_REPLACEFUNC(AirRide_PopulateSelectIcons, GateMachines_PopulateAirRideIcons);

    // City Trial's select screen is packed by custom_machines when that mod is built,
    // and gated through the availability filter main.c hands it; otherwise
    // GateMachines_OnCustomMachinesAbsent patches the screen here. Both are decided
    // past OnBoot, because the registry boots after us.

    CODEPATCH_HOOKAPPLY(0x8002dea0);  // CT starting-machine finalize
    CODEPATCH_HOOKAPPLY(0x801952c8);  // CT respawn machine validation

    // The TR race and solo (Free Run / Time Attack) lobbies are separate code paths,
    // each with its own init, cycler, and start-match handler.
    CODEPATCH_HOOKAPPLY(0x8002d070);  // TopRide_InitSelectData post-loop fixup (main-menu reset)
    CODEPATCH_HOOKAPPLY(0x8002d748);  // TopRide_RaceInit post-reset fixup (TR Main Game)
    CODEPATCH_HOOKAPPLY(0x8002db90);  // TopRide_SoloInit post-zero fixup (Free Run / Time Attack)
    CODEPATCH_HOOKAPPLY(0x8002be44);  // TopRide_CSS_PanelThink L/R cycler (race lobby)
    CODEPATCH_HOOKAPPLY(0x8002cb98);  // TopRide_SoloPanelThink L/R cycler (Free Run / Time Attack)
    CODEPATCH_HOOKAPPLY(0x8002c52c);  // TopRide_PreGameThink start-match gate (race)
    CODEPATCH_HOOKAPPLY(0x8002cc80);  // TopRide_OnCourseSelect start-match gate (solo)

    OSReport("[GateMachines] Hooks installed\n");
}

// Display name for any MachineKind, vanilla or registered custom.
const char *GateMachines_GetName(MachineKind kind)
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

    ap_save->machine_unlocked_mask |= (1 << kind);
    OSReport("[GateMachines] Machine %d (%s) unlocked (mask = %s)\n",
             kind, GateMachines_GetName(kind), MaskBits(ap_save->machine_unlocked_mask, 32));
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
        tb_api->EnqueueColoredNoun(prefix, name, tb_api->MachineColor, NULL);
    }
    return 1;
}

// Legendary machines assembled in the current City Trial scene: bit 0 = Dragoon,
// bit 1 = Hydra. The piece archives (VsDragoon.dat / VsHydra.dat) are freed when the
// assembly cinematic finishes, so a second cinematic in the same scene loads a
// dangling joint and crashes in HSD_JObjLoadJoint.
static u8 legendary_assembled_mask;

void GateMachines_On3DLoadEnd(void)
{
    legendary_assembled_mask = 0;
}

// Give a player the assembled legendary machine via the cinematic. machine_index:
// 0 = Dragoon, 1 = Hydra. Returns 1 if started (consume the item), 0 if it can't run
// yet (keep queued and retry).
//
// The cinematic loads legendary piece models and drives the CT sky/area-light setup,
// which only exist on the open City Trial map - running it in a stadium or AR/TR
// dereferences a null jobj or hits the area-light assert.
int GateMachines_GiveLegendaryMachine(int machine_index)
{
    if (!Gm_IsInCity())
        return 0;

    u8 bit = (u8)(1 << machine_index);
    if (legendary_assembled_mask & bit)
        return 0;

    // A second concurrent cinematic (GObj at GameData+0xA8C) tears down the running
    // one's piece GObjs and leaves a dangling jobj that crashes on the next update.
    if (Gm_IsLegendaryAssembling())
        return 0;

    int given = 0;
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;

        GOBJ *machine_gobj = Ply_GetMachineGObj(i);
        if (!machine_gobj)
            continue;

        MachineData *md = (MachineData *)machine_gobj->userdata;
        if (!md)
            continue;

        LegendaryAssemblyParams params;
        params.machine_index = machine_index;
        params.ply = i;
        params.pos = md->pos;
        params.up = md->up;
        params.forward = md->forward;

        LegendaryMachine_StartAssembly(&params);
        OSReport("[GateMachines] Legendary machine %s assembly started for player %d\n",
                 machine_index == 0 ? "Dragoon" : "Hydra", i);
        given = 1;
    }

    if (given)
        legendary_assembled_mask |= bit;

    return given;
}
