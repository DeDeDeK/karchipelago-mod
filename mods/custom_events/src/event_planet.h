#ifndef EVENT_PLANET_H
#define EVENT_PLANET_H

void Planet_Start(void);
void Planet_End(void);
void Planet_End2(void);
void Planet_Abort(void);

// Reserves the planet's collision in every City Trial load with events on and
// replaces Gr_GetDownVector and Machine_RefreshGravity, passthroughs while the event
// is idle. Call once at boot.
void Planet_InstallHooks(void);

#endif // EVENT_PLANET_H
