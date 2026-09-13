#ifndef EVENT_GRAVITY_CHANGE_H
#define EVENT_GRAVITY_CHANGE_H

void GravityChange_Start(void);

// Also the abort callback: the stage archive can stay preloaded into the next
// round, so a cut-off event still has to put the strength back.
void GravityChange_End2(void);

#endif // EVENT_GRAVITY_CHANGE_H
