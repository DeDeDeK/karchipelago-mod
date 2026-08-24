#include "main.h"
#include "ap_announce.h"

// Quiet during the boot regrant, and while the client is posting the AP receipt line
// for the item currently being applied.
static int ShouldAnnounce(void)
{
    return !ap_regrant_quiet && !ap_item_quiet;
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
