#ifndef EVENT_MINI_UFOS_H
#define EVENT_MINI_UFOS_H

int MiniUFOs_CanStart(void);
void MiniUFOs_Start(void);
void MiniUFOs_End(void);
void MiniUFOs_End2(void);
void MiniUFOs_Abort(void);

// Lays the UFOs' flight path over the stage's course loop. Call after every City
// Trial load with events on.
void MiniUFOs_OnCityLoad(void);

#endif // EVENT_MINI_UFOS_H
