#ifndef ARCHIPELAGO_AP_ANNOUNCE_H
#define ARCHIPELAGO_AP_ANNOUNCE_H

#include "textbox_api.h"

int APAnnounce_Grant(const char *prefix, const char *noun, GXColor color, const char *suffix);
int APAnnounce_GrantSegments(const TextSegment *segs, int seg_count);

#endif // ARCHIPELAGO_AP_ANNOUNCE_H
