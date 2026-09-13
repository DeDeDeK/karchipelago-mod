#ifndef EVENT_SCALE_CHANGE_H
#define EVENT_SCALE_CHANGE_H

void ScaleChange_Start(void);
void ScaleChange_Active(void);
void ScaleChange_End(void);
void ScaleChange_End2(void);
void ScaleChange_Abort(void);

// Installs the camera-distance shim, a passthrough while the event is idle. Call
// once at boot.
void ScaleChange_InstallHooks(void);

#endif // EVENT_SCALE_CHANGE_H
