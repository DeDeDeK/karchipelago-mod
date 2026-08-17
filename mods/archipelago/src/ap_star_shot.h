#ifndef ARCHIPELAGO_AP_STAR_SHOT_H
#define ARCHIPELAGO_AP_STAR_SHOT_H

// Releasing a full charge on the Archipelago Star fires one of its six pods as a
// projectile. The pod nearest the machine's heading leaves the ring, so the shot
// always launches off the nose, and the shot wears that pod's color. The sixth
// shot empties the ring and starts all six growing back over a second, during
// which a full-charge release is an ordinary boost with no shot.
//
// A shot fired from the ground follows the ground; one fired in the air flies
// straight from where the machine was pointing.

// Install the two charge-release hooks.
void ApStarShot_OnBoot(void);

// Load this scene's shot model and claim the machine's per-kind handler slots.
void ApStarShot_On3DLoadEnd(void);

#endif // ARCHIPELAGO_AP_STAR_SHOT_H
