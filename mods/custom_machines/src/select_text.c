// Machine name and description text on both select screens.
//
// Each screen turns a CharacterKind into a pair of SIS text indices through two
// 20-entry tables, read by exactly one function per screen -
// AirRideSelect_SetMachineText and CitySelect_SetMachineText. Both screens' SIS
// files hold exactly 48 entries with none to spare, so an appended CharacterKind
// reads an index past the end of its table and then dereferences whatever lies
// past the end of the entry array.
//
// All four tables are relocated widened, and each appended character gets a name
// and a description entry composed here from its descriptor and appended to the
// loaded SIS pointer array. A machine with no description text still gets an
// entry, empty: the screen draws neither text unless both indices are valid.

#include "os.h"
#include "hsd.h"
#include "menu.h"
#include "text.h"
#include "scene.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

// Entries in SisSelply.dat and SisSelplyCt.dat, both of which load into SIS slot
// 0. Indices 0-1 are the image and kerning banks, 2-7 screen furniture, 8-27 the
// machine names and 28-47 their descriptions.
#define SIS_SELPLY_ENTRY_NUM 48

// Both hold their string's glyphs at two bytes each plus the styling around them.
#define SIS_NAME_TEXT_MAX 96
#define SIS_DESCRIPTION_TEXT_MAX 160

// Air Ride then City Trial: each screen's name and description index table, with
// the lis / addi pair that forms it inside the one function that reads it.
static const u32 stc_index_tables[2][2][3] = {
    { { 0x804aa3d8, 0x80153d58, 0x80153d68 }, { 0x804aa428, 0x80153d5c, 0x80153d6c } },
    { { 0x804aa598, 0x8015e76c, 0x8015e77c }, { 0x804aa5e8, 0x8015e770, 0x8015e780 } },
};

// Entries are read as a word and sign-extended from their low byte, so a text
// index has to fit a signed char and -1 means "draw nothing".
static u32 stc_text_index[2][2][CUSTOM_CKIND_NUM + 1];

static void *stc_sis_ptrs[SIS_SELPLY_ENTRY_NUM + CUSTOM_MACHINE_MAX * 2];
static u8 stc_sis_name_text[CUSTOM_MACHINE_MAX][SIS_NAME_TEXT_MAX];
static u8 stc_sis_description_text[CUSTOM_MACHINE_MAX][SIS_DESCRIPTION_TEXT_MAX];

// Glyphs of `str` into `p`, stopping short of `end`. Characters the master font
// has no code for are dropped, as they are everywhere else the game composes text.
static u8 *WriteGlyphs(u8 *p, u8 *end, const char *str, int upper)
{
    for (int i = 0; str[i] != '\0' && p < end; i++)
    {
        char c = str[i];

        if (c == ' ')
        {
            *p++ = 0x1a;  // SPACE
            continue;
        }
        if (c == '\n')
        {
            *p++ = 0x03;  // LINEBREAK
            continue;
        }
        if (upper && c >= 'a' && c <= 'z')
            c -= 'a' - 'A';

        int cmd = Text_CharToCommand(c);
        if (cmd == -1)
            continue;
        *p++ = (u8)(cmd >> 8);
        *p++ = (u8)cmd;
    }
    return p;
}

// A machine name, styled as the vanilla name entries are and upper-cased because
// every one of them is.
static void ComposeName(u8 *buf, const char *name)
{
    u8 *p = buf;

    *p++ = 0x10;                                                      // ALIGN_CENTER
    *p++ = 0x18;                                                      // FIT_ON
    *p++ = 0x16;                                                      // KERNING_ON
    *p++ = 0x0c; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;               // COLOR black
    *p++ = 0x0e; *p++ = 0x00; *p++ = 0x80; *p++ = 0x00; *p++ = 0x80;  // SCALE 0.5

    p = WriteGlyphs(p, buf + SIS_NAME_TEXT_MAX - 9, name, 1);

    *p++ = 0x03;                                         // LINEBREAK
    *p++ = 0x0d; *p++ = 0x0f; *p++ = 0x17; *p++ = 0x19;  // COLOR_POP SCALE_POP KERNING_OFF FIT_OFF
    *p++ = 0x11;                                         // ALIGN_POP
    *p++ = 0x00;                                         // TERMINATE
}

// The blurb under the name, in the vanilla descriptions' box and styling. An empty
// string composes an empty box, which is what a machine with no description gets.
static void ComposeDescription(u8 *buf, const char *description)
{
    u8 *p = buf;

    *p++ = 0x0a; *p++ = 0x00; *p++ = 0x00; *p++ = 0x14; *p++ = 0x00;  // POS_PUSH (0, 20)
    *p++ = 0x12;                                                      // ALIGN_LEFT
    *p++ = 0x18;                                                      // FIT_ON
    *p++ = 0x16;                                                      // KERNING_ON
    *p++ = 0x0c; *p++ = 0x30; *p++ = 0x30; *p++ = 0x30;               // COLOR gray
    *p++ = 0x0e; *p++ = 0x00; *p++ = 0x8c; *p++ = 0x00; *p++ = 0x8c;  // SCALE 0.55

    p = WriteGlyphs(p, buf + SIS_DESCRIPTION_TEXT_MAX - 9, description, 0);

    *p++ = 0x03;                                         // LINEBREAK
    *p++ = 0x0d; *p++ = 0x0f; *p++ = 0x17; *p++ = 0x19;  // COLOR_POP SCALE_POP KERNING_OFF FIT_OFF
    *p++ = 0x13;                                         // ALIGN_POP
    *p++ = 0x00;                                         // TERMINATE
}

// Re-point SIS slot 0 at a copy of the archive's pointer array with the appended
// entries after it. The array lives in the scene's heap, so this runs on every
// load of either screen's SIS file.
static void CustomMachineText_ExtendSis(void)
{
    void **loaded = (void **)stc_sis_data[0];

    if (loaded == NULL || loaded == stc_sis_ptrs)
        return;

    for (int i = 0; i < SIS_SELPLY_ENTRY_NUM; i++)
        stc_sis_ptrs[i] = loaded[i];

    stc_sis_data[0] = (SISData *)stc_sis_ptrs;
}

// Epilogues of AirRideSelect_LoadSisFile and CitySelect_LoadSisFile, past the
// Text_LoadSisFile that fills the slot.
CODEPATCH_HOOKCREATE(0x8013baf0,
    "",
    CustomMachineText_ExtendSis,
    "",
    0
)

CODEPATCH_HOOKCREATE(0x8013c4cc,
    "",
    CustomMachineText_ExtendSis,
    "",
    0
)

void CustomMachineText_OnBoot(void)
{
    int appended = CustomMachines_GetCharacterKindCeiling() - CKIND_NUM;
    if (appended <= 0)
        return;

    // A name then its description, one pair per appended character.
    for (int i = 0; i < appended; i++)
    {
        CustomMachineEntry *e = CustomMachines_FindByCharacterKind(CKIND_NUM + i);

        ComposeName(stc_sis_name_text[i], e != NULL ? e->name : "");
        ComposeDescription(stc_sis_description_text[i], e != NULL ? e->description : "");
        stc_sis_ptrs[SIS_SELPLY_ENTRY_NUM + i * 2 + 0] = stc_sis_name_text[i];
        stc_sis_ptrs[SIS_SELPLY_ENTRY_NUM + i * 2 + 1] = stc_sis_description_text[i];
    }

    for (int screen = 0; screen < 2; screen++)
    {
        for (int which = 0; which < 2; which++)
        {
            const u32 *table = stc_index_tables[screen][which];
            const u32 *vanilla = (const u32 *)table[0];
            u32 *dst = stc_text_index[screen][which];

            for (int i = 0; i < CKIND_NUM; i++)
                dst[i] = vanilla[i];
            for (int i = 0; i < appended; i++)
                dst[CKIND_NUM + i] = (u32)(SIS_SELPLY_ENTRY_NUM + i * 2 + which);
            for (int i = CKIND_NUM + appended; i <= CUSTOM_CKIND_NUM; i++)
                dst[i] = (u32)-1;

            CustomMachines_RepointTable(table[1], table[2], dst);
        }
    }

    CODEPATCH_HOOKAPPLY(0x8013baf0);  // Air Ride select SIS load
    CODEPATCH_HOOKAPPLY(0x8013c4cc);  // City Trial select SIS load

    OSReport("[CustomMachines] %d machine name/description pair(s) spliced in at SIS entry %d\n",
             appended, SIS_SELPLY_ENTRY_NUM);
}
