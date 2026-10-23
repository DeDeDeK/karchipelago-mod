#ifndef EVENT_CANNON_H
#define EVENT_CANNON_H

void Cannon_Start(void);
void Cannon_End2(void);

// Rebuilds the cannon from the disc. Call at every City Trial load with events on.
void Cannon_OnCityLoad(void);

// Puts back the stage slots a cannon borrowed. Call at every 3D exit.
void Cannon_On3DExit(void);

// Poses machines in a placed cannon, a passthrough for every other cannon. Call once
// at boot.
void Cannon_InstallHooks(void);

#endif // EVENT_CANNON_H
