#include "main.h"
#include "ap_announce.h"
#include "settings_menu.h"

int APAnnounce_LocalEnabled(APLocalKind kind)
{
    return kind >= 0 && kind < APLOCAL_NUM && ap_menu_settings.local_messages[kind];
}

static int ShouldAnnounce(void)
{
    return !ap_regrant_quiet && APAnnounce_LocalEnabled(APLOCAL_ITEM);
}

void APAnnounce_Grant(const char *prefix, const char *noun, GXColor color, const char *suffix)
{
    if (!ShouldAnnounce())
        return;
    tb_api->EnqueueColoredNoun(prefix, noun, color, suffix);
}

void APAnnounce_GrantSegments(const TextSegment *segs, int seg_count)
{
    if (!ShouldAnnounce())
        return;
    tb_api->EnqueueSegments(segs, seg_count);
}
