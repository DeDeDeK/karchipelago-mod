#ifndef ARCHIPELAGO_AP_TEXT_H
#define ARCHIPELAGO_AP_TEXT_H

// Renders a pending client message into the textbox.
void APText_OnFrameStart(void);

// Post a canned client-authored line of the given APTextKind, or one that fills all 8
// runs and overflows the 3 rendered lines. Both return 0 if the mailbox is still full.
int APText_DebugSend(int kind);
int APText_DebugSendOverlong(void);

#endif // ARCHIPELAGO_AP_TEXT_H
