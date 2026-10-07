#ifndef CUSTOM_CHECKLIST_API_H
#define CUSTOM_CHECKLIST_API_H

#include "game.h"
#include "gx.h"

// Extra checklist tabs after the three vanilla ones in the L/R tab rotation. Import via
// Hoshi_ImportMod and Register from OnSaveLoaded: mods boot in alphabetical order, so a
// consumer's OnBoot can run before this mod exports the API.

#define CUSTOM_CHECKLIST_MOD_NAME  "custom_checklist"
#define CUSTOM_CHECKLIST_API_MAJOR 4
#define CUSTOM_CHECKLIST_API_MINOR 0

// is_complete is polled every frame in every scene once is_ready holds - menus, loads and
// the title attract demo included - until it first returns nonzero; the cell is then
// recorded and animated. It must be a cheap pure read of state latched elsewhere, and gate
// itself if it must not fire during the demo. It receives its own clear_kind, so one
// predicate can back every row.
//
// label is ASCII on at most two lines. A '\n' places the break; without one, a label over
// 30 characters breaks at the space nearest its midpoint. It is composed at 2 bytes per
// glyph and 1 per space or break and cut silently from byte 157; characters with no glyph
// are dropped.
typedef struct CustomCheck
{
    int clear_kind;                     // grid cell, [0, CLEAR_KIND_NUM), unique within the tab
    const char *label;                  // objective text
    int (*is_complete)(int clear_kind); // nonzero once satisfied
} CustomCheck;

// Copied by Register, but the pointers it holds are kept - pass static data.
typedef struct CustomChecklistDesc
{
    const char *name; // stable identity: keys the tab's save slot

    // Tab tint, alpha unused; black keeps City Trial's green. Must not itself be
    // green-dominant (g > r && g >= b), which is how the recolor finds City Trial's tint
    // materials.
    GXColor theme;

    // Optional art archive staged to the FST root (base name, no extension), exporting
    // two _HSD_ImageDesc publics. NULL, or a file not on disc, keeps City Trial's art.
    const char *tex_file;
    const char *banner_symbol; // replaces the 248x128 banner behind the grid
    const char *emblem_symbol; // replaces the tab emblem, any size; takes the tint

    const CustomCheck *checks;
    int check_num;

    // Completion is the consumer's to store. record_complete runs once, on first
    // completion, and is_recorded must hold from then on, across boots.
    int  (*is_recorded)(int clear_kind);
    void (*record_complete)(int clear_kind);

    // Optional gate: evaluation no-ops until this returns nonzero. NULL = always ready.
    int  (*is_ready)(void);
} CustomChecklistDesc;

typedef struct CustomChecklistAPI
{
    // Returns the tab's checklist mode (>= GMMODE_NUM), or -1 if the descriptor is
    // rejected. Pass that mode to any engine path the tab uses (ClearChecker_SetNewUnlock,
    // Checklist_GrantFiller). The tab's board, fillers included, is saved by the framework.
    int (*Register)(const CustomChecklistDesc *desc);

    // Show every cell backed by a check, leaving unlock state alone. Saved with the
    // tab's board.
    void (*RevealAll)(int mode);

    // Mode of the tab currently being built, or -1 outside a build. The build runs under
    // GMMODE_CITYTRIAL, so a hook that keys off ClearCheckerUI.mode must remap through
    // this or it applies City Trial's reward rows to the custom tab's board.
    int (*GetBuildMode)(void);
} CustomChecklistAPI;

#endif // CUSTOM_CHECKLIST_API_H
