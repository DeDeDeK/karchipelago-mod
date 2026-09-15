// Appends a CharacterKind for each registered machine that asks for one. Three DOL roster
// tables sit back to back with no slack, and each is read by exactly one accessor that
// does nothing but form an address, so all three are relocated by rewriting the lis/addi
// pair inside the accessor. The machine-to-CharacterKind map is replaced outright instead:
// its bike half is reached r13-relative, which no lis/addi pair forms.

#include "os.h"
#include "menu.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

// One row past the last real character, so every availability predicate rejects it.
// Its CharacterDesc stays zeroed, a valid row for any stray lookup.
#define SENTINEL_CKIND CUSTOM_CKIND_NUM

#define MAX_GRID_COLS (SELICON_GRID_COLS + (CUSTOM_MACHINE_MAX + 1) / 2)

static CharacterDesc stc_char_desc[CUSTOM_CKIND_NUM + 1];
static u8 stc_icon_linear[CUSTOM_CKIND_NUM + 1];
// Flat, because its row stride is the runtime column count SelIcon_GetCKind is
// patched to multiply by - not the compile-time maximum.
static u8 stc_icon_grid[SELICON_GRID_ROWS * MAX_GRID_COLS];

static int stc_grid_cols = SELICON_GRID_COLS;

int CustomMachineCharacterRegistry_GetGridCols(void)
{
    return stc_grid_cols;
}

// Replaces Machine_GetCKind (0x8000b9f4), which the results screens and the time-attack
// board go through to reach a machine's art - the select screens hold the CharacterKind
// already and never touch it. A custom slot with no CharacterKind behind it takes the one
// vanilla parks its class's art-less machines on: the Free Star's for a star, the plain
// Wheel's for a bike.
static CharacterKind GetCKind(int is_bike, int class_slot)
{
    is_bike = (s8)is_bike;
    class_slot = (s8)class_slot;

    CustomMachineEntry *e = CustomMachines_FindByClassSlot(is_bike, class_slot);
    if (e != NULL)
    {
        if (e->character_kind >= 0)
            return e->character_kind;
        class_slot = MachineKind_ClassIndex(is_bike ? VCKIND_WHEELNORMAL : VCKIND_FREE);
    }
    return (is_bike ? stc_machine_ckind_bike : stc_machine_ckind_star)[class_slot];
}

void CustomMachineCharacterRegistry_OnBoot(void)
{
    for (int i = 0; i < CKIND_NUM; i++)
    {
        stc_char_desc[i] = stc_character_desc[i];
        stc_icon_linear[i] = stc_selicon_ckind_linear[i];
    }

    int appended = 0;
    for (int i = 0; i < CustomMachines_GetCount(); i++)
    {
        CustomMachineEntry *e = CustomMachines_GetEntry(i);
        if (e->character_kind < 0)
            continue;
        CharacterDesc *row = &stc_char_desc[e->character_kind];
        row->rider_kind = (u8)e->rider_kind;
        row->is_bike = (u8)e->is_bike;
        row->machine_kind = (u8)e->class_slot;  // class-relative, as every CharacterDesc is
        stc_icon_linear[e->character_kind] = (u8)e->character_kind;
        appended++;
    }
    stc_icon_linear[SENTINEL_CKIND] = SENTINEL_CKIND;

    stc_grid_cols = SELICON_GRID_COLS + (appended + 1) / 2;
    for (int row = 0; row < SELICON_GRID_ROWS; row++)
    {
        for (int col = 0; col < stc_grid_cols; col++)
        {
            stc_icon_grid[row * stc_grid_cols + col] =
                (col < SELICON_GRID_COLS) ? stc_selicon_ckind_grid[row * SELICON_GRID_COLS + col]
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
        stc_icon_grid[(n & 1) * stc_grid_cols + SELICON_GRID_COLS + (n >> 1)] =
            (u8)e->character_kind;
        n++;
    }

    CustomMachines_RepointTable(0x8000b9a8, 0x8000b9b0, stc_icon_linear);  // SelIcon_GetCKindLinear
    CustomMachines_RepointTable(0x8000b9c0, 0x8000b9cc, stc_icon_grid);    // SelIcon_GetCKind
    CustomMachines_RepointTable(0x8000b9e0, 0x8000b9e8, stc_char_desc);    // Character_GetDesc
    CustomMachines_SetImmediate(0x8000b9c4, stc_grid_cols); // mulli r5, r0, cols
    CODEPATCH_REPLACEFUNC(Machine_GetCKind, GetCKind);

    OSReport("[CharacterRegistry] %d character(s) appended, grid is 2x%d\n",
             appended, stc_grid_cols);
}
