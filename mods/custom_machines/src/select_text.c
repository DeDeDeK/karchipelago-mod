#include <string.h>

#include "os.h"
#include "hsd.h"
#include "menu.h"
#include "text.h"
#include "scene.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

// Both hold their string's glyphs at two bytes each plus the styling around them.
#define SIS_NAME_TEXT_MAX 96
#define SIS_DESCRIPTION_TEXT_MAX 160

// Air Ride then City Trial, name then description: the lis / addi pair that forms each
// index table inside the one function that reads it.
static const u32 stc_index_table_sites[2][2][2] = {
    { { 0x80153d58, 0x80153d68 }, { 0x80153d5c, 0x80153d6c } }, // AirRideSelect_SetMachineText (0x80153d2c)
    { { 0x8015e76c, 0x8015e77c }, { 0x8015e770, 0x8015e780 } }, // CitySelect_SetMachineText (0x8015e740)
};

// Entries are read as a word and sign-extended from their low byte, so a text
// index has to fit a signed char and -1 means "draw nothing".
static u32 stc_text_index[2][2][CUSTOM_CKIND_NUM + 1];

static SISEntry stc_sis_ptrs[SIS_SELPLY_ENTRY_NUM + CUSTOM_MACHINE_MAX * 2];
static u8 stc_sis_name_text[CUSTOM_MACHINE_MAX][SIS_NAME_TEXT_MAX];
static u8 stc_sis_description_text[CUSTOM_MACHINE_MAX][SIS_DESCRIPTION_TEXT_MAX];

// Closing commands both entries end on.
#define SIS_CLOSE_LEN 7

// A machine name, styled as the vanilla name entries are and upper-cased because
// every one of them is.
static void ComposeName(u8 *buf, const char *name)
{
    char upper[CUSTOM_MACHINE_NAME_MAX];
    u8 *p = buf;

    CustomMachines_CopyStr(upper, name, sizeof(upper));
    for (char *c = upper; *c != '\0'; c++)
    {
        if (*c >= 'a' && *c <= 'z')
            *c -= 'a' - 'A';
    }

    *p++ = TEXTCMD_ALIGNCENTER;
    *p++ = TEXTCMD_FIT;
    *p++ = TEXTCMD_KERNING;
    *p++ = TEXTCMD_COLOR; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;              // black
    *p++ = TEXTCMD_SCALE; *p++ = 0x00; *p++ = 0x80; *p++ = 0x00; *p++ = 0x80; // 0.5

    p = Text_WriteSisString(p, buf + SIS_NAME_TEXT_MAX - SIS_CLOSE_LEN, upper);

    *p++ = TEXTCMD_LINEBREAK;
    *p++ = TEXTCMD_COLOREND;
    *p++ = TEXTCMD_SCALEEND;
    *p++ = TEXTCMD_KERNINGEND;
    *p++ = TEXTCMD_FITEND;
    *p++ = TEXTCMD_ALIGNCENTEREND;
    *p++ = TEXTCMD_TERMINATE;
}

// The blurb under the name, in the vanilla descriptions' box and styling. An empty
// string composes an empty box, which is what a machine with no description gets.
static void ComposeDescription(u8 *buf, const char *description)
{
    u8 *p = buf;

    *p++ = TEXTCMD_POSPUSH; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x14; // (0, 20)
    *p++ = TEXTCMD_ALIGNLEFT;
    *p++ = TEXTCMD_FIT;
    *p++ = TEXTCMD_KERNING;
    *p++ = TEXTCMD_COLOR; *p++ = 0x30; *p++ = 0x30; *p++ = 0x30;              // gray
    *p++ = TEXTCMD_SCALE; *p++ = 0x00; *p++ = 0x8c; *p++ = 0x00; *p++ = 0x8c; // 0.55

    p = Text_WriteSisString(p, buf + SIS_DESCRIPTION_TEXT_MAX - SIS_CLOSE_LEN, description);

    *p++ = TEXTCMD_LINEBREAK;
    *p++ = TEXTCMD_COLOREND;
    *p++ = TEXTCMD_SCALEEND;
    *p++ = TEXTCMD_KERNINGEND;
    *p++ = TEXTCMD_FITEND;
    *p++ = TEXTCMD_ALIGNLEFTEND;
    *p++ = TEXTCMD_TERMINATE;
}

// Re-point SIS slot 0 at a copy of the archive's pointer array with the appended
// entries after it. The array lives in the scene's heap, so this runs on every
// load of either screen's SIS file.
static void ExtendSis(void)
{
    Text_ExtendSis(0, stc_sis_ptrs, SIS_SELPLY_ENTRY_NUM);
}

// Epilogues of AirRideSelect_LoadSisFile (0x8013bacc) and CitySelect_LoadSisFile
// (0x8013c4a8), past the Text_LoadSisFile that fills the slot.
CODEPATCH_HOOKCREATE(0x8013baf0,
    "",
    ExtendSis,
    "",
    0
)

CODEPATCH_HOOKCREATE(0x8013c4cc,
    "",
    ExtendSis,
    "",
    0
)

void CustomMachineSelectText_OnBoot(void)
{
    int appended = CustomMachines_GetCharacterKindCeiling() - CKIND_NUM;
    if (appended <= 0)
        return;

    // A name then its description, one pair per appended character.
    for (int i = 0; i < CustomMachines_GetCount(); i++)
    {
        CustomMachineEntry *e = CustomMachines_GetEntry(i);
        int n = e->character_kind - CKIND_NUM;

        if (n < 0)
            continue;
        ComposeName(stc_sis_name_text[n], e->name);
        ComposeDescription(stc_sis_description_text[n], e->description);
        stc_sis_ptrs[SIS_SELPLY_ENTRY_NUM + n * 2 + 0] = stc_sis_name_text[n];
        stc_sis_ptrs[SIS_SELPLY_ENTRY_NUM + n * 2 + 1] = stc_sis_description_text[n];
    }

    const int *vanilla[2][2] = {
        { stc_airride_select_name_text, stc_airride_select_desc_text },
        { stc_city_select_name_text, stc_city_select_desc_text },
    };
    for (int screen = 0; screen < 2; screen++)
    {
        for (int which = 0; which < 2; which++)
        {
            u32 *dst = stc_text_index[screen][which];

            memcpy(dst, vanilla[screen][which], CKIND_NUM * sizeof(u32));
            for (int i = 0; i < appended; i++)
                dst[CKIND_NUM + i] = (u32)(SIS_SELPLY_ENTRY_NUM + i * 2 + which);
            for (int i = CKIND_NUM + appended; i <= CUSTOM_CKIND_NUM; i++)
                dst[i] = (u32)-1;

            CODEPATCH_REPLACEADDRESS(stc_index_table_sites[screen][which][0],
                                        stc_index_table_sites[screen][which][1], dst);
        }
    }

    CODEPATCH_HOOKAPPLY(0x8013baf0);  // Air Ride select SIS load
    CODEPATCH_HOOKAPPLY(0x8013c4cc);  // City Trial select SIS load

    OSReport("[SelectText] %d machine name/description pair(s) spliced in at SIS entry %d\n",
             appended, SIS_SELPLY_ENTRY_NUM);
}
