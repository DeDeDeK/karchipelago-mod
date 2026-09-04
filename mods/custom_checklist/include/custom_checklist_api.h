#ifndef CUSTOM_CHECKLIST_API_H
#define CUSTOM_CHECKLIST_API_H

#include "datatypes.h"

// Mod-owned checklist tabs alongside the three vanilla ones, folded into the L/R tab
// rotation. Import via Hoshi_ImportMod and call Register from OnSaveLoaded - the
// framework boots after most mods.

#define CUSTOM_CHECKLIST_MOD_NAME  "custom_checklist"
#define CUSTOM_CHECKLIST_API_MAJOR 2
#define CUSTOM_CHECKLIST_API_MINOR 2

// Grid cells. A tab may define any subset; undefined cells render blank.
#define CC_CLEAR_KIND_NUM 120

// is_complete is polled every frame until it first returns nonzero, then the cell is
// recorded and animated. It receives its own clear_kind, so a tab whose cells share one
// predicate can point every row at the same function.
typedef struct CustomCheck
{
    int clear_kind;                    // grid cell index, [0, CC_CLEAR_KIND_NUM)
    const char *label;                 // objective text (plain ASCII)
    int (*is_complete)(int clear_kind); // nonzero once satisfied
} CustomCheck;

// Copied by Register, but the pointers it holds (name, checks, label/symbol strings)
// are kept - pass static data.
typedef struct CustomChecklistDesc
{
    const char *name;        // identification / logging (e.g. "Archipelago")

    // Tab tint: the dominant channel sets the hue, the channel ratios the saturation.
    // (0,0,0) keeps City Trial's green.
    u8 theme_r;
    u8 theme_g;
    u8 theme_b;

    // Optional tab artwork: an HSD archive staged to the FST root (base name, no
    // extension) exporting two _HSD_ImageDesc publics. NULL keeps CT's borrowed art.
    const char *tex_file;       // e.g. "ApChecklistTex"
    const char *banner_symbol;  // 248x128 RGB5A3 banner image-desc public
    const char *emblem_symbol;  // tab-emblem image-desc public, any size

    const CustomCheck *checks;  // static table, kept by pointer
    int check_num;

    // Optional persistence pair; leave both NULL and the framework persists the tab in
    // its own save, keyed by `name`. A half-provided pair falls back to that too.
    int  (*is_recorded)(int clear_kind);     // nonzero if already completed (out-of-range: 1)
    void (*record_complete)(int clear_kind); // mark recorded, on first completion

    // Optional cue, called once on first completion whichever side persists.
    void (*on_complete)(int clear_kind);

    // Optional gate: evaluation no-ops until this returns nonzero. NULL = always ready.
    int  (*is_ready)(void);
} CustomChecklistDesc;

// Published via Hoshi_ExportMod.
typedef struct CustomChecklistAPI
{
    // Returns the assigned checklist mode index (>= GMMODE_NUM) or -1 on failure. Pass
    // that mode to any engine record path the tab uses (e.g. ClearChecker_SetNewUnlock).
    int (*Register)(const CustomChecklistDesc *desc);

    // Show every cell backed by a check, latched for the session so the grid shuffle
    // cannot drop it. Cells with no check stay hidden; unlock state is untouched. The
    // latch is not saved - a consumer whose option outlives a boot calls this each
    // OnSaveLoaded, right after Register.
    void (*RevealAll)(int mode);

    // Mode of the tab currently being built, or -1 outside a build. The build runs
    // Checklist_Init under GMMODE_CITYTRIAL, so ClearCheckerUI.mode reads CITYTRIAL
    // while gmGetClearcheckerTypeP already serves the tab's block. A hook that keys
    // off the UI mode must remap it through this or it applies City Trial's reward
    // rows to the custom tab's board.
    int (*GetBuildMode)(void);
} CustomChecklistAPI;

#endif // CUSTOM_CHECKLIST_API_H
