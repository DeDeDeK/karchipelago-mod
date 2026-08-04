#ifndef TEXTBOX_API_H
#define TEXTBOX_API_H

#include "datatypes.h"
#include "structs.h"
#include "gx.h"
#include "rider.h"

#define TEXTBOX_MOD_NAME "textbox"

#define TEXTBOX_API_MAJOR 1
#define TEXTBOX_API_MINOR 0

// Maximum colored runs per message; each becomes one subtext of the underlying Text GObj.
#define TEXTBOX_MAX_SEGMENTS 5

// One colored run of text. `text` is copied at enqueue, so it need not outlive the call.
typedef struct TextSegment
{
    const char *text;
    GXColor color;
} TextSegment;

// Exported via Hoshi_ExportMod; every Enqueue* returns 1 on success, 0 if the message was
// dropped (textbox disabled, bad segment count, or no screen canvas yet).
typedef struct TextBoxAPI
{
    // printf-style single segment in DefaultColor.
    int (*Enqueue)(const char *format, ...);

    // 1..TEXTBOX_MAX_SEGMENTS segments with per-segment colors.
    int (*EnqueueSegments)(const TextSegment *segs, int seg_count);

    // prefix + noun + suffix, only the noun colored; NULL/empty prefix or suffix is allowed.
    int (*EnqueueColoredNoun)(const char *prefix, const char *noun, GXColor noun_color, const char *suffix);

    // EnqueueColoredNoun with a printf-style suffix.
    int (*EnqueueColoredNounFmt)(const char *prefix, const char *noun, GXColor noun_color,
                                 const char *suffix_format, ...);

    // Named color palette. RGB only - the alpha byte is ignored, since alpha is set per-frame
    // by the fade machinery.
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
