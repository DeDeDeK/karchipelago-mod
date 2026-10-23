#include "game.h"
#include "text.h"

#include "custom_checklist.h"

#define CC_SIS_LABEL_END (CLEARCHECKER_SIS_OBJECTIVE_BASE + CLEAR_KIND_NUM)
// The reward panel also reads CLEARCHECKER_SIS_NO_REWARD and CLEARCHECKER_SIS_REWARD_BASE +
// reward_index, so the array spans City Trial's rewards too.
#define CC_SIS_PTR_NUM (CLEARCHECKER_SIS_REWARD_BASE + CITYTRIAL_REWARD_NUM)
// Every vanilla objective entry fits in 128; the extra room is for longer custom labels.
#define CC_SIS_LABEL_MAX 160
// Past this many characters a label with no '\n' of its own breaks at the space nearest its
// midpoint: the cell holds two lines, and the engine squeezes an over-wide line rather than
// breaking it.
#define CC_SIS_WRAP 30

// Only one custom tab is on screen at a time, so these are shared.
static SISEntry sis_ptrs[CC_SIS_PTR_NUM];
static u8 sis_blank[1] = { TEXTCMD_TERMINATE };
static u8 sis_label[CLEAR_KIND_NUM][CC_SIS_LABEL_MAX];

// Index of the space to break at, or -1 for no break.
static int CC_WrapIndex(const char *str)
{
    int len = 0;
    for (; str[len]; len++)
        if (str[len] == '\n')
            return -1;
    if (len <= CC_SIS_WRAP)
        return -1;

    int mid = len / 2;
    int best = -1;
    int best_dist = len;
    for (int i = 0; i < len; i++)
    {
        int dist = i < mid ? mid - i : i - mid;
        if (str[i] == ' ' && dist < best_dist)
        {
            best = i;
            best_dist = dist;
        }
    }
    return best;
}

// Vanilla-shaped SIS entry: glyphs, separators, optional break, terminator. No
// align/fit/kerning/color/scale opcodes - the checklist UI's Text object supplies those.
static void CC_ComposeSis(u8 *buf, const char *str)
{
    u8 *p = buf;
    int wrap = CC_WrapIndex(str);

    // The last byte is kept for the terminator.
    for (int i = 0; str[i]; i++)
    {
        u8 *next = Text_WriteSisChar(p, buf + CC_SIS_LABEL_MAX - 1, i == wrap ? '\n' : str[i]);
        if (next == NULL)
            break;
        p = next;
    }

    *p++ = TEXTCMD_TERMINATE;
}

// Repoint SIS slot 0, which Checklist_Init has just loaded with City Trial's archive, at
// this tab's labels. Outside the objective range every entry passes through to the archive.
void CCLabels_Apply(int idx)
{
    SISEntry *loaded = stc_sis_data[0];
    for (int i = 0; i < CC_SIS_PTR_NUM; i++)
        sis_ptrs[i] = i >= CLEARCHECKER_SIS_OBJECTIVE_BASE && i < CC_SIS_LABEL_END ? sis_blank : loaded[i];

    const CustomChecklistDesc *d = &cc_tabs[idx].desc;
    for (int c = 0; c < d->check_num; c++)
    {
        int ck = d->checks[c].clear_kind;
        CC_ComposeSis(sis_label[ck], d->checks[c].label);
        sis_ptrs[CLEARCHECKER_SIS_OBJECTIVE_BASE + ck] = sis_label[ck];
    }

    stc_sis_data[0] = sis_ptrs;
}
