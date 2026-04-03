#ifndef ARCHIPELAGO_AIRRIDE_SPEED_H
#define ARCHIPELAGO_AIRRIDE_SPEED_H

// Increment the Air Ride speed boost counter in save data.
// Called from APItems_HandleItem (no scene requirement).
void AirRideSpeed_Increment(void);

// Apply accumulated speed boosts as top speed patches to human players.
// Called from On3DLoadEnd when entering an Air Ride race.
void AirRideSpeed_On3DLoadEnd(void);

#endif // ARCHIPELAGO_AIRRIDE_SPEED_H
