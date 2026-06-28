#ifndef CUSTOM_CHECKLIST_API_H
#define CUSTOM_CHECKLIST_API_H

#include "datatypes.h"

// Custom Checklist - framework for adding mod-owned checklist tabs alongside the
// three vanilla ones (Air Ride / Top Ride / City Trial), folded into the L/R tab
// rotation. A mod supplies the objectives, theme, and art; the framework owns the
// presentation and per-frame evaluation. Import via Hoshi_ImportMod and call
// Register from OnSaveLoaded (the framework boots after most mods).

#define CUSTOM_CHECKLIST_MOD_NAME  "custom_checklist"
#define CUSTOM_CHECKLIST_API_MAJOR 1
#define CUSTOM_CHECKLIST_API_MINOR 1

// Valid clear_kind range for a check: [0, CC_CLEAR_KIND_NUM). A tab may define any
// subset of cells; undefined cells render blank.
#define CC_CLEAR_KIND_NUM 120

// One checklist cell: an objective at grid cell `clear_kind`, with `label` shown
// when the cell is selected and `is_complete` polled every frame until it first
// returns nonzero (then the cell is recorded and animated).
typedef struct CustomCheck
{
    int clear_kind;           // grid cell index, [0, CC_CLEAR_KIND_NUM)
    const char *label;        // objective text (plain ASCII)
    int (*is_complete)(void); // nonzero once satisfied
} CustomCheck;

// Descriptor passed to Register. The framework copies the struct but keeps the
// pointers it holds (name, checks, label/symbol strings) - pass static data.
typedef struct CustomChecklistDesc
{
    const char *name;        // identification / logging (e.g. "Archipelago")

    // Tab tint. The framework retints City Trial's green tab onto this hue: the
    // dominant channel sets the hue, the channel ratios set the saturation.
    // (0,0,0) keeps CT's green.
    u8 theme_r;
    u8 theme_g;
    u8 theme_b;

    // Tab artwork (optional). HSD archive staged to the FST root (base name, no
    // extension) exporting two _HSD_ImageDesc publics. NULL tex_file keeps CT's
    // borrowed art.
    const char *tex_file;       // e.g. "ApChecklistTex"
    const char *banner_symbol;  // 248x128 RGB5A3 banner image-desc public
    const char *emblem_symbol;  // 40x40 I4 emblem image-desc public

    // Objectives (static table; kept by pointer).
    const CustomCheck *checks;
    int check_num;

    // Persistence (OPTIONAL - leave both NULL for the common case, where the
    // framework persists the tab in its own save keyed by `name`). Provide both
    // only when the mod must own where a completion is stored. A half-provided
    // pair falls back to framework persistence.
    //   is_recorded(clear_kind)     -> nonzero if already completed (out-of-range: 1).
    //   record_complete(clear_kind) -> mark recorded; called once, the first frame
    //                                  the predicate holds.
    int  (*is_recorded)(int clear_kind);
    void (*record_complete)(int clear_kind);

    // Optional completion cue, called once the first frame a check completes
    // (whichever side persists) - the seam to raise a mod-specific notification
    // without owning storage. NULL => none.
    void (*on_complete)(int clear_kind);

    // Optional readiness gate: evaluation no-ops until this returns nonzero (e.g.
    // save loaded, dependent APIs resolved). NULL => always ready.
    int  (*is_ready)(void);
} CustomChecklistDesc;

// API published via Hoshi_ExportMod for other mods to consume.
typedef struct CustomChecklistAPI
{
    // Register a checklist tab. Returns the assigned checklist mode index
    // (>= GMMODE_NUM) or -1 on failure. Pass this returned mode whenever the mod's
    // record path routes through the engine (e.g. ClearChecker_SetNewUnlock).
    int (*Register)(const CustomChecklistDesc *desc);
} CustomChecklistAPI;

#endif // CUSTOM_CHECKLIST_API_H
