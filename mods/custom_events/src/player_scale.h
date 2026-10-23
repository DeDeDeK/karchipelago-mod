#ifndef PLAYER_SCALE_H
#define PLAYER_SCALE_H

// Moves cur toward target by at most step.
float PlayerScale_Ease(float cur, float target, float step);

// Scales every player's rider and ridden machine - models and map collision - by factor
// about their ground contact point, and each player camera's eye and interest by
// camera_factor about the body it tracks. Call every frame the scale should hold.
void PlayerScale_Apply(float factor, float camera_factor);

// Returns every scaled rider, machine and camera to normal size.
void PlayerScale_Restore(void);

// Forgets the scaled objects without touching them, for a scene exit.
void PlayerScale_Forget(void);

// Installs the radius2 and camera hooks, passthroughs while nothing is scaled. Call
// once at boot.
void PlayerScale_InstallHooks(void);

#endif // PLAYER_SCALE_H
