#ifndef TRAPLINK_H
#define TRAPLINK_H

#include "structs.h"

void TrapLink_PerFrame(GOBJ *g);
void TrapLink_On3DLoadEnd();
void TrapLink_OnBoot();
void TrapLink_Send(void);

#endif // TRAPLINK_H
