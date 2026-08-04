#ifndef EVENT_SCALE_CHANGE_H
#define EVENT_SCALE_CHANGE_H

#include "event.h"

void ScaleChange_Start(EventCheckData *ev_chk);
void ScaleChange_Active(EventCheckData *ev_chk);
void ScaleChange_End(EventCheckData *ev_chk);
void ScaleChange_End2(EventCheckData *ev_chk);

// Installs the persistent camera-distance shim. Call once at boot.
void ScaleChange_InstallHooks(void);

#endif // EVENT_SCALE_CHANGE_H
