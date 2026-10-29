#ifndef CITY_TRIAL_EVENT_H
#define CITY_TRIAL_EVENT_H

#include "event.h"

// Starts a City Trial event on the city map. Returns 0 while another event runs or the
// event can't start yet.
int CTEvent_Give(EventKind kind);

#endif
