#ifndef EVENT_STAT_SHUFFLE_H
#define EVENT_STAT_SHUFFLE_H

// The shuffle needs at least two players.
int StatShuffle_CanStart(void);

void StatShuffle_Start(void);
void StatShuffle_End(void);
void StatShuffle_End2(void);

// Writes the restored stats to the player slots only, which the City Trial exit
// saves for the stadium after the 3D exit.
void StatShuffle_Abort(void);

#endif // EVENT_STAT_SHUFFLE_H
