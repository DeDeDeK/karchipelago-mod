#ifndef ARCHIPELAGO_AP_ANNOUNCE_H
#define ARCHIPELAGO_AP_ANNOUNCE_H

#include "textbox_api.h"

// Categories of line the mod composes about Archipelago traffic, each with its own
// toggle under Messages -> Local. Separate from APTextKind, which gates the lines the
// client composes and is on the wire.
typedef enum APLocalKind
{
    APLOCAL_CHECK = 0, // "Check recorded" as a checkbox completes
    APLOCAL_ITEM,      // a grant applied from the AP queue
    APLOCAL_GOAL,      // a mode goal, or every goal, becoming satisfied
    APLOCAL_NUM,
} APLocalKind;

// 1 when the Messages -> Local menu has that category turned on.
int APAnnounce_LocalEnabled(APLocalKind kind);

int APAnnounce_Grant(const char *prefix, const char *noun, GXColor color, const char *suffix);
int APAnnounce_GrantSegments(const TextSegment *segs, int seg_count);

#endif // ARCHIPELAGO_AP_ANNOUNCE_H
