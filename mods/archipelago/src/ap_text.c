#include <string.h>

#include "os.h"

#include "main.h"
#include "ap_text.h"
#include "settings_menu.h"
#include "textbox_api.h"

// Archipelago's CommonClient GUI palette, indexed by APTextColor. Black is lifted off
// 000000 so it stays readable on the textbox's dark background; the rest are AP's own
// hex values, which were already picked for a dark UI.
static const GXColor ap_text_colors[APTEXTCOLOR_NUM] = {
    [APTEXTCOLOR_DEFAULT]   = {255, 255, 255, 255},
    [APTEXTCOLOR_BLACK]     = { 80,  80,  80, 255},
    [APTEXTCOLOR_RED]       = {238,   0,   0, 255},
    [APTEXTCOLOR_GREEN]     = {  0, 255, 127, 255},
    [APTEXTCOLOR_YELLOW]    = {250, 250, 210, 255},
    [APTEXTCOLOR_BLUE]      = {100, 149, 237, 255},
    [APTEXTCOLOR_MAGENTA]   = {238,   0, 238, 255},
    [APTEXTCOLOR_CYAN]      = {  0, 238, 238, 255},
    [APTEXTCOLOR_WHITE]     = {255, 255, 255, 255},
    [APTEXTCOLOR_ORANGE]    = {255, 119,   0, 255},
    [APTEXTCOLOR_SLATEBLUE] = {109, 139, 232, 255},
    [APTEXTCOLOR_PLUM]      = {175, 153, 239, 255},
    [APTEXTCOLOR_SALMON]    = {250, 128, 114, 255},
};

static u32 last_alive;
static int alive_frames = AP_CLIENT_ALIVE_TIMEOUT;
static int client_connected;

int APText_KindEnabled(int kind)
{
    return kind >= 0 && kind < APTEXT_KIND_NUM && ap_menu_settings.text_messages[kind];
}

int APText_ClientConnected(void)
{
    return client_connected;
}

int APText_ItemAnnounceSuppressed(void)
{
    return client_connected && APText_KindEnabled(APTEXT_KIND_ITEM);
}

static GXColor APText_Color(u8 index)
{
    if (index == APTEXTCOLOR_DEFAULT || index >= APTEXTCOLOR_NUM)
        return tb_api->DefaultColor;
    return ap_text_colors[index];
}

static void APText_Render(const APTextMessage *msg)
{
    int n = msg->seg_count;
    if (n <= 0 || n > AP_TEXT_SEG_NUM || !APText_KindEnabled(msg->kind))
        return;

    // The blob holds seg_count NUL-terminated strings back to back. The extra
    // terminator bounds the walk if the client sent an unterminated tail.
    char buf[AP_TEXT_BLOB_LEN + 1];
    memcpy(buf, msg->text, AP_TEXT_BLOB_LEN);
    buf[AP_TEXT_BLOB_LEN] = '\0';

    TextSegment segs[AP_TEXT_SEG_NUM];
    int used = 0;
    int pos = 0;
    for (int i = 0; i < n && pos <= AP_TEXT_BLOB_LEN; i++)
    {
        segs[used].text  = &buf[pos];
        segs[used].color = APText_Color(msg->colors[i]);
        pos += strlen(&buf[pos]) + 1;
        used++;
    }

    if (used > 0)
        tb_api->EnqueueSegments(segs, used);
}

static void APText_UpdateHeartbeat(void)
{
    u32 alive = ap_data->client_alive;
    if (alive != last_alive)
    {
        last_alive = alive;
        alive_frames = 0;
    }
    else if (alive_frames < AP_CLIENT_ALIVE_TIMEOUT)
    {
        alive_frames++;
    }

    int now = (alive != 0) && (alive_frames < AP_CLIENT_ALIVE_TIMEOUT);
    if (now == client_connected)
        return;
    client_connected = now;

    OSReport("[APText] Client %s\n", now ? "connected" : "lost");
    if (APText_KindEnabled(APTEXT_KIND_STATUS))
        tb_api->EnqueueColoredNoun(NULL, "Archipelago client",
                                   ap_text_colors[now ? APTEXTCOLOR_GREEN : APTEXTCOLOR_RED],
                                   now ? " connected" : " disconnected");
}

void APText_OnFrameStart(void)
{
    if (!ap_data || !tb_api)
        return;

    APText_UpdateHeartbeat();

    // Holding the mailbox while the textbox has no canvas (scene transitions) is what
    // backpressures the client instead of losing the message across a load.
    if (ap_data->text_pending && tb_api->IsReady())
    {
        APText_Render(&ap_data->text_msg);
        ap_data->text_pending = 0;
    }
}
