#ifndef AP_STAR_SHOT_H
#define AP_STAR_SHOT_H

// A full-charge release fires the pod nearest the machine's heading, wearing that
// pod's color. The sixth empties the ring and starts a 60-frame regrow, during which
// a release is an ordinary boost. A shot fired from the ground follows it; one fired
// in the air flies straight.

void ApStarShot_OnBoot(void);

// Load this scene's shot model and claim the machine's per-kind handler slots.
void ApStarShot_On3DLoadEnd(void);

#endif // AP_STAR_SHOT_H
