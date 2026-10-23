#ifndef CPU_STRATEGIST_H
#define CPU_STRATEGIST_H

#include "rider.h"

void Strategist_Init(int ply);
void Strategist_Navigate(RiderData *rd);
void Strategist_Heal(RiderData *rd);
void Strategist_Maneuver(RiderData *rd);
int Strategist_ItemScore(RiderData *rd, int kind, int score);
const char *Strategist_GetGoalName(int ply);
int Strategist_GetTarget(int ply, void **target, Vec3 *pos);

#endif // CPU_STRATEGIST_H
