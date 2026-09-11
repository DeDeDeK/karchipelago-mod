#include <string.h>

#include "main.h"
#include "ap_text.h"
#include "settings_menu.h"
#include "textbox_api.h"

// Archipelago's CommonClient GUI palette, indexed by APTextColor. Black is lifted off
// 000000 so it stays readable on the textbox's dark background; the rest are AP's own
// hex values, which were already picked for a dark UI. APTEXTCOLOR_DEFAULT has no row -
// APText_Color answers it from the textbox's own default first.
static const GXColor ap_text_colors[APTEXTCOLOR_NUM] = {
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

static int APText_KindEnabled(int kind)
{
    return kind >= 0 && kind < APTEXT_KIND_NUM && ap_menu_settings.text_messages[kind];
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

void APText_OnFrameStart(void)
{
    if (!ap_data)
        return;

    // Holding the mailbox while the textbox has no canvas (scene transitions) is what
    // backpressures the client instead of losing the message across a load.
    if (ap_data->text_pending && tb_api->IsReady())
    {
        APText_Render(&ap_data->text_msg);
        ap_data->text_pending = 0;
    }
}

// One colored run of a canned debug line. A run list ends at a null text.
typedef struct
{
    const char *text;
    u8 color;
} APTextDebugRun;

// The wording and colors the Python client composes, one line per APTextKind, so the
// render path, the per-kind filter and the palette can be exercised with no client.
static const APTextDebugRun dbg_check[] = {
    {"Kirby", APTEXTCOLOR_MAGENTA},
    {" sent ", APTEXTCOLOR_DEFAULT},
    {"Progressive Sword", APTEXTCOLOR_PLUM},
    {" to ", APTEXTCOLOR_DEFAULT},
    {"Kirby64", APTEXTCOLOR_YELLOW},
    {0, 0},
};

static const APTextDebugRun dbg_item[] = {
    {"Warp Star", APTEXTCOLOR_PLUM},
    {" received from ", APTEXTCOLOR_DEFAULT},
    {"Kirby64", APTEXTCOLOR_YELLOW},
    {0, 0},
};

static const APTextDebugRun dbg_hint[] = {
    {"Hint: ", APTEXTCOLOR_DEFAULT},
    {"Kirby64's ", APTEXTCOLOR_YELLOW},
    {"Progressive Sword", APTEXTCOLOR_PLUM},
    {" is at ", APTEXTCOLOR_DEFAULT},
    {"Stadium DRAG RACE 2", APTEXTCOLOR_GREEN},
    {0, 0},
};

// Status and chat arrive from the server as one uncolored run the client tints by kind.
static const APTextDebugRun dbg_status[] = {
    {"Archipelago client connected", APTEXTCOLOR_WHITE},
    {0, 0},
};

static const APTextDebugRun dbg_chat[] = {
    {"Kirby64: glhf", APTEXTCOLOR_CYAN},
    {0, 0},
};

static const APTextDebugRun dbg_link[] = {
    {"TrapLink", APTEXTCOLOR_SALMON},
    {" from ", APTEXTCOLOR_DEFAULT},
    {"Kirby64", APTEXTCOLOR_YELLOW},
    {" (Ice Trap)", APTEXTCOLOR_DEFAULT},
    {0, 0},
};

static const APTextDebugRun *const dbg_lines[APTEXT_KIND_NUM] = {
    [APTEXT_KIND_CHECK]  = dbg_check,
    [APTEXT_KIND_ITEM]   = dbg_item,
    [APTEXT_KIND_HINT]   = dbg_hint,
    [APTEXT_KIND_STATUS] = dbg_status,
    [APTEXT_KIND_CHAT]   = dbg_chat,
    [APTEXT_KIND_LINK]   = dbg_link,
};

// All 8 runs and past the 3 lines any font size can show, so the textbox's wrap and
// its trailing ".." truncation are both on screen.
static const APTextDebugRun dbg_overlong[] = {
    {"Kirby", APTEXTCOLOR_MAGENTA},
    {" sent ", APTEXTCOLOR_DEFAULT},
    {"Progressive Beam Whip Upgrade Kit", APTEXTCOLOR_PLUM},
    {" to ", APTEXTCOLOR_DEFAULT},
    {"AVeryLongPlayerNameForWrapTesting", APTEXTCOLOR_YELLOW},
    {" at ", APTEXTCOLOR_DEFAULT},
    {"City Trial Checklist Row 7 Column 5, reached from Stadium DRAG RACE 2 "
     "after the Destruction Derby 3 run", APTEXTCOLOR_GREEN},
    {" (found)", APTEXTCOLOR_SLATEBLUE},
    {0, 0},
};

// Fills the mailbox the way the client does - whole record first, pending flag last -
// so the IsReady hold and the single-slot handshake behave identically. Returns 0 if
// an earlier message has not been rendered yet, which is the client's own precondition.
static int APText_DebugPost(int kind, const APTextDebugRun *runs)
{
    if (!ap_data || ap_data->text_pending)
        return 0;

    APTextMessage *msg = &ap_data->text_msg;
    memset(msg, 0, sizeof(*msg));
    msg->kind = (u8)kind;

    int pos = 0;
    int n = 0;
    while (n < AP_TEXT_SEG_NUM && runs[n].text)
    {
        int len = strlen(runs[n].text);
        if (pos + len + 1 > AP_TEXT_BLOB_LEN)
            break;
        memcpy(&msg->text[pos], runs[n].text, len + 1);
        pos += len + 1;
        msg->colors[n] = runs[n].color;
        n++;
    }
    msg->seg_count = (u8)n;

    ap_data->text_pending = 1;
    return 1;
}

int APText_DebugSend(int kind)
{
    if (kind < 0 || kind >= APTEXT_KIND_NUM)
        return 0;
    return APText_DebugPost(kind, dbg_lines[kind]);
}

int APText_DebugSendOverlong(void)
{
    return APText_DebugPost(APTEXT_KIND_CHECK, dbg_overlong);
}
