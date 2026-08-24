#ifndef ARCHIPELAGO_AP_TEXT_H
#define ARCHIPELAGO_AP_TEXT_H

#include "main.h"

// Tracks the client heartbeat and renders a pending client message into the textbox.
void APText_OnFrameStart(void);

// 1 when the Messages menu has that APTextKind turned on.
int APText_KindEnabled(int kind);

// 1 while the client has bumped client_alive within AP_CLIENT_ALIVE_TIMEOUT frames.
int APText_ClientConnected(void);

// 1 when a received item's own announce should stay quiet because the client is
// posting the "<item> received from <player>" line instead.
int APText_ItemAnnounceSuppressed(void);

#endif // ARCHIPELAGO_AP_TEXT_H
