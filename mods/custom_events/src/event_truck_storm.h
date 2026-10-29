#ifndef EVENT_TRUCK_STORM_H
#define EVENT_TRUCK_STORM_H

void TruckStorm_Start(void);
void TruckStorm_Active(void);
void TruckStorm_End(void);
void TruckStorm_End2(void);

// Keeps riders from inhaling a storm truck, a passthrough while no storm runs.
// Call once at boot.
void TruckStorm_InstallHooks(void);

#endif // EVENT_TRUCK_STORM_H
