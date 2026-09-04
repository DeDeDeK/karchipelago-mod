#include "main.h"
#include "ap_announce.h"
#include "settings_menu.h"

int APAnnounce_LocalEnabled(APLocalKind kind)
{
    return kind >= 0 && kind < APLOCAL_NUM && ap_menu_settings.local_messages[kind];
}

// Off by default: with a client attached its receipt line already names the item, the
// sender and the color, so the mod's own would only repeat it.
static int ShouldAnnounce(void)
{
    return !ap_regrant_quiet && APAnnounce_LocalEnabled(APLOCAL_ITEM);
}

int APAnnounce_Grant(const char *prefix, const char *noun, GXColor color, const char *suffix)
{
    if (!ShouldAnnounce())
        return 0;
    return tb_api->EnqueueColoredNoun(prefix, noun, color, suffix);
}

int APAnnounce_GrantSegments(const TextSegment *segs, int seg_count)
{
    if (!ShouldAnnounce())
        return 0;
    return tb_api->EnqueueSegments(segs, seg_count);
}
