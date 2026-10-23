#ifndef EVENT_DANGER_RAIN_H
#define EVENT_DANGER_RAIN_H

void DangerRain_Start(void);
void DangerRain_Active(void);
void DangerRain_End2(void);
void DangerRain_Abort(void);

// The DangerRainKind the next start rains, -1 for a random one.
void DangerRain_SetNext(int kind);

// Shrinks the rain meteors' quake and keeps ownerless Gordos out of the Gordo cap.
// Call once at boot.
void DangerRain_InstallHooks(void);

#endif // EVENT_DANGER_RAIN_H
