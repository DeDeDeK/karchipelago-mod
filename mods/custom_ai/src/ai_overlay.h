#ifndef AI_OVERLAY_H
#define AI_OVERLAY_H

#include "rider.h"

// A bit each for the screen text and the in-world view.
typedef enum AIOverlayMode
{
    AI_OVERLAY_OFF,
    AI_OVERLAY_TEXT,
    AI_OVERLAY_WORLD,
    AI_OVERLAY_BOTH,
    AI_OVERLAY_NUM,
} AIOverlayMode;

extern int ai_overlay;
extern char *ai_overlay_names[AI_OVERLAY_NUM];

void AIOverlay_RecordCPU(RiderData *rd);
void AIOverlay_On3DLoadEnd(void);
void AIOverlay_On3DExit(void);

#endif // AI_OVERLAY_H
