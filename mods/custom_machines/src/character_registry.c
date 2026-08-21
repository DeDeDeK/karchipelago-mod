// Appends a CharacterKind for each registered machine that asks for one.
//
// The four DOL roster tables sit back to back with no slack, and each is read by
// exactly one accessor that does nothing but form an address, so all four are
// relocated by rewriting the lis/addi pair inside the accessor. The grid grows a
// column per two appended characters; the at most one leftover cell holds
// SENTINEL_CKIND, which every availability predicate rejects.

#include "os.h"
#include "menu.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

#define VANILLA_GRID_COLS 10

// One row past the last real character, so every availability predicate rejects it.
#define SENTINEL_CKIND CUSTOM_CKIND_NUM

#define MAX_GRID_COLS (VANILLA_GRID_COLS + (CUSTOM_MACHINE_MAX + 1) / 2)

static u8 stc_char_desc[(CUSTOM_CKIND_NUM + 1) * 3];
static u8 stc_icon_linear[CUSTOM_CKIND_NUM + 1];
// Flat, because its row stride is the runtime column count Icon_GetCKind is
// patched to multiply by - not the compile-time maximum.
static u8 stc_icon_grid[2 * MAX_GRID_COLS];

// Machine_GetCKind's star half, which the results screens and the time-attack
// board go through to reach a machine's art - the select screens hold the
// CharacterKind already and never touch it. A slot with no CharacterKind behind
// it takes the one vanilla parks its own art-less stars on.
static u8 stc_star_ckind[CUSTOM_VCSTAR_NUM];

static int stc_grid_cols = VANILLA_GRID_COLS;

int CustomMachineCharacter_GetGridCols(void)
{
    return stc_grid_cols;
}

int CustomMachineCharacter_GetSentinel(void)
{
    return SENTINEL_CKIND;
}

void CustomMachineCharacter_OnBoot(void)
{
    const u8 *v_desc = (const u8 *)0x80495814;
    const u8 *v_grid = (const u8 *)0x80495800;
    const u8 *v_linear = (const u8 *)0x804957ec;
    const u8 *v_star_ckind = (const u8 *)0x80495850;

    for (int i = 0; i < CKIND_NUM * 3; i++)
        stc_char_desc[i] = v_desc[i];
    for (int i = 0; i < CKIND_NUM; i++)
        stc_icon_linear[i] = v_linear[i];
    for (int i = 0; i < VCSTAR_NUM; i++)
        stc_star_ckind[i] = v_star_ckind[i];
    for (int i = VCSTAR_NUM; i < CUSTOM_VCSTAR_NUM; i++)
        stc_star_ckind[i] = v_star_ckind[VCKIND_FREE];

    int appended = 0;
    for (int i = 0; i < CustomMachines_GetCount(); i++)
    {
        CustomMachineEntry *e = CustomMachines_GetEntry(i);
        if (e->character_kind < 0)
            continue;
        u8 *row = &stc_char_desc[e->character_kind * 3];
        row[0] = (u8)e->rider_kind;
        row[1] = 0;                  // star class
        row[2] = (u8)e->star_slot;   // class-relative, as every CharacterDesc is
        stc_icon_linear[e->character_kind] = (u8)e->character_kind;
        stc_star_ckind[e->star_slot] = (u8)e->character_kind;
        appended++;
    }

    // Sentinel row: a valid CharacterDesc address for any stray lookup, and a
    // ckind the availability predicates reject.
    stc_char_desc[SENTINEL_CKIND * 3 + 0] = 0;
    stc_char_desc[SENTINEL_CKIND * 3 + 1] = 0;
    stc_char_desc[SENTINEL_CKIND * 3 + 2] = 0;
    stc_icon_linear[SENTINEL_CKIND] = SENTINEL_CKIND;

    stc_grid_cols = VANILLA_GRID_COLS + (appended + 1) / 2;
    for (int row = 0; row < 2; row++)
    {
        for (int col = 0; col < stc_grid_cols; col++)
        {
            stc_icon_grid[row * stc_grid_cols + col] =
                (col < VANILLA_GRID_COLS) ? v_grid[row * VANILLA_GRID_COLS + col]
                                          : (u8)SENTINEL_CKIND;
        }
    }
    // Appended characters fill the new columns row 0 first, so a single addition
    // gives an 11 / 10 grid rather than 10 / 11.
    int n = 0;
    for (int i = 0; i < CustomMachines_GetCount(); i++)
    {
        CustomMachineEntry *e = CustomMachines_GetEntry(i);
        if (e->character_kind < 0)
            continue;
        stc_icon_grid[(n & 1) * stc_grid_cols + VANILLA_GRID_COLS + (n >> 1)] =
            (u8)e->character_kind;
        n++;
    }

    CustomMachines_RepointTable(0x8000b9a8, 0x8000b9b0, stc_icon_linear);  // SelIcon_GetCKindLinear
    CustomMachines_RepointTable(0x8000b9c0, 0x8000b9cc, stc_icon_grid);    // Icon_GetCKind
    CustomMachines_RepointTable(0x8000b9e0, 0x8000b9e8, stc_char_desc);    // Character_GetDesc
    CustomMachines_RepointTable(0x8000b9fc, 0x8000ba04, stc_star_ckind);   // Machine_GetCKind
    CODEPATCH_REPLACEINSTRUCTION(0x8000b9c4, 0x1CA00000 | stc_grid_cols); // mulli r5, r0, cols

    OSReport("[CharacterRegistry] %d character(s) appended, grid is 2x%d\n",
             appended, stc_grid_cols);
}
