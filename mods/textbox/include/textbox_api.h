#ifndef TEXTBOX_API_H
#define TEXTBOX_API_H

#include "datatypes.h"
#include "structs.h"
#include "gx.h"
#include "rider.h"

// Hoshi mod name for Hoshi_ImportMod() lookups.
#define TEXTBOX_MOD_NAME "textbox"

// API version: bump major on breaking changes, minor on additions.
#define TEXTBOX_API_MAJOR 1
#define TEXTBOX_API_MINOR 0

// Maximum colored runs per message. Each segment becomes one subtext (with its
// own COLOR opcode) inside the underlying Text GObj.
#define TEXTBOX_MAX_SEGMENTS 5

// One coloured run of text.
typedef struct TextSegment
{
    const char *text;
    GXColor color;
} TextSegment;

// Function-pointer table exported by the textbox mod via Hoshi_ExportMod.
// Imported by other mods with
// `Hoshi_ImportMod(TEXTBOX_MOD_NAME, TEXTBOX_API_MAJOR, TEXTBOX_API_MINOR)`.
typedef struct TextBoxAPI
{
    // Plain message - single segment in TextBox_DefaultColor. printf-style.
    int (*Enqueue)(const char *format, ...);

    // N-segment message with per-segment colors.
    int (*EnqueueSegments)(const TextSegment *segs, int seg_count);

    // Three-segment shortcut: prefix + noun + suffix, where only the noun gets
    // a custom color (prefix/suffix render in DefaultColor). NULL/empty prefix
    // or suffix is allowed.
    int (*EnqueueColoredNoun)(const char *prefix, const char *noun, GXColor noun_color, const char *suffix);

    // Same as EnqueueColoredNoun, but suffix is a printf-style format string
    // with vararg substitutions.
    int (*EnqueueColoredNounFmt)(const char *prefix, const char *noun, GXColor noun_color,
                                 const char *suffix_format, ...);

    // Named color palette. RGB only - alpha is set per-frame by the lifetime/
    // fade machinery, so the alpha byte here is ignored.
    GXColor DefaultColor;     
    GXColor MachineColor;     
    GXColor EventColor;       
    GXColor StadiumColor;     
    GXColor StageColor;       
    GXColor TopRideItemColor; 
    GXColor ItemColor;        
    GXColor TrapColor;        
    GXColor DeathColor;       
    GXColor EnergyColor;      
    GXColor CheckColor;       
    GXColor GoalColor;        
    GXColor RewardColor;      
    GXColor ShopColor;        
    GXColor FillerColor;      

    // Indexed palettes.
    const GXColor *AbilityColors; // [COPYKIND_NUM]
    const GXColor *KirbyColors;   // [KIRBYCOLOR_NUM]
    const GXColor *ModeColors;    // [GMMODE_NUM] - mode name (AR/TR/CT)
    const GXColor *PatchColors;   // [PATCHKIND_NUM] - per-stat patch color
    const GXColor *BoxColors;     // [BOXKIND_NUM] - per-box color
} TextBoxAPI;

#endif // TEXTBOX_API_H
