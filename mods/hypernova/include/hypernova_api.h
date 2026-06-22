#ifndef HYPERNOVA_API_H
#define HYPERNOVA_API_H

#include "datatypes.h"

#define HYPERNOVA_MOD_NAME  "hypernova"
#define HYPERNOVA_API_MAJOR 1
#define HYPERNOVA_API_MINOR 0

typedef struct HypernovaAPI
{
    // Activate for all human players for `duration_frames` frames (0 = menu default).
    // Returns 1 if it started (mod enabled + in a City Trial gameplay scene), else 0.
    // Calling while already active refreshes the timer.
    int (*Activate)(int duration_frames);

    // Stop immediately. Kirby eases back to normal size.
    void (*Deactivate)(void);

    // 1 while active, else 0.
    int (*IsActive)(void);

    // Frames remaining while active (0 when inactive).
    int (*FramesRemaining)(void);
} HypernovaAPI;

#endif // HYPERNOVA_API_H
