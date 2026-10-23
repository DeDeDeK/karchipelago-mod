#ifndef CPU_GLIDE_H
#define CPU_GLIDE_H

#include "rider.h"

void CpuGlide_Track(RiderData *rd);
int CpuGlide_Predict(RiderData *rd, Vec3 *land);

#endif // CPU_GLIDE_H
