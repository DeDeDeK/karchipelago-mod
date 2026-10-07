#ifndef ARCHIPELAGO_AP_UNLOCK_H
#define ARCHIPELAGO_AP_UNLOCK_H

#include "archipelago_api.h"

// Per-category unlock masks. Narrower masks read zero-extended and Set truncates to the
// field's width.
u32  APUnlock_GetMask(APUnlockCategory cat);
void APUnlock_SetMask(APUnlockCategory cat, u32 mask);

// Display name, and how many low bits of the mask are meaningful.
const char *APUnlock_Name(APUnlockCategory cat);
int APUnlock_Bits(APUnlockCategory cat);

#endif // ARCHIPELAGO_AP_UNLOCK_H
