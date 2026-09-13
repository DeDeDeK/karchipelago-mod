#ifndef CUSTOM_EVENTS_H
#define CUSTOM_EVENTS_H

#include "custom_events_api.h"

void CustomEvents_OnBoot(void);

// Appends the custom announcements to City Trial's SIS pointer array. Call after
// every City Trial load.
void CustomEvents_InitSis(void);

// Runs the abort callback of a custom event the scene exit cut off.
void CustomEvents_On3DExit(void);

#endif // CUSTOM_EVENTS_H
