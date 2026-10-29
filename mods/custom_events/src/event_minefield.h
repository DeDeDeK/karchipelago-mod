#ifndef EVENT_MINEFIELD_H
#define EVENT_MINEFIELD_H

void Minefield_Start(void);

// Keeps a player's sensor bomb throw from fading the field. Call once at boot.
void Minefield_InstallHooks(void);

#endif // EVENT_MINEFIELD_H
