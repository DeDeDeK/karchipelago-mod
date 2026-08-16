// Gives every character-indexed UI art bank a 21st frame, at load time.
//
// The select-screen portraits, name plates, results art and time-attack board are
// TexAnims whose animation frame is the CharacterKind, so an appended character
// has no art until each of those banks holds one more image and its image-index
// ramp has a key selecting it. Sixteen banks across eight archives need that.
//
// The additions ride in ApUiFrames.dat rather than in rewritten copies of the
// eight: each bank's appended image and the replacement keyframe buffer its ramps
// need are the only parts that are ours, and both are position-independent, so
// the side-car carries them and the vanilla archives are edited in memory. The
// widened image and TLUT tables cannot be baked that way - they hold pointers to
// the donor's own descriptors, which move with each load - so they are rebuilt
// here into a static pool, one fixed slot per bank.
//
// Every one of the eight loads through Gm_LoadGameFile (0x80059818) and is rebuilt
// on each entry into its scene, freed by the shared menu teardown (0x80131928) on
// the way out, so the splice runs per load rather than once.

#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

#define UI_FRAMES_FILE   "ApUiFrames.dat"
#define UI_FRAMES_PUBLIC "apUiFrames"

// The widest bank grows 34 -> 35; the pool holds a table per bank for the run of
// the game, because a donor is rebuilt on every scene entry.
#define UI_FRAME_ENTRY_MAX 36
#define UI_FRAME_BANK_MAX  16
#define UI_FRAME_FILE_MAX  8

// Longest donor basename is "MnSelplyctAll", and the engine passes it with a
// trailing '.' and no extension.
#define UI_FRAME_NAME_MAX 24

// Authored by scripts/hsd/make_ui_frames.py; the layouts must match.
typedef struct UiFrameBank
{
    u32 texanim_off;       // 0x00 data-section offset of the TexAnim
    u16 n_images;          // 0x04 donor image count, before the append
    u16 n_tluts;           // 0x06 donor TLUT count
    u16 tlut_src;          // 0x08 TLUT index the appended frame reuses
    u16 pad;               // 0x0a
    _HSD_ImageDesc *image; // 0x0c the appended frame
    u32 timg_off;          // 0x10 data-section offset of the image-index FObjDesc
    void *timg_buf;        // 0x14 its replacement keyframe buffer
    u32 timg_len;          // 0x18
    u32 tclt_off;          // 0x1c the TLUT-index FObjDesc, 0 when the bank has none
    void *tclt_buf;        // 0x20
    u32 tclt_len;          // 0x24
} UiFrameBank;

typedef struct UiFrameFile
{
    char *name;         // 0x00 donor basename, no extension
    u32 n_banks;        // 0x04
    UiFrameBank *banks; // 0x08
} UiFrameFile;

static UiFrameFile *stc_files;
static int stc_slot_base[UI_FRAME_FILE_MAX]; // first pool slot of each file's banks
static int stc_file_num;

static _HSD_ImageDesc *stc_image_pool[UI_FRAME_BANK_MAX][UI_FRAME_ENTRY_MAX];
static HSD_TlutDesc *stc_tlut_pool[UI_FRAME_BANK_MAX][UI_FRAME_ENTRY_MAX];

static char stc_loading[UI_FRAME_NAME_MAX];

// The engine's name carries a trailing '.' and its case does not always match the
// disc ("MnSelplyCtAll" against MnSelplyctAll.dat), which it gets away with
// because DVDConvertPathToEntrynum is case-insensitive.
static int NameMatches(const char *engine, const char *ours)
{
    int i;

    for (i = 0; ours[i] != '\0'; i++)
    {
        char a = engine[i];
        char b = ours[i];

        if (a >= 'A' && a <= 'Z')
            a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z')
            b += 'a' - 'A';
        if (a != b)
            return 0;
    }
    return engine[i] == '\0' || engine[i] == '.';
}

// Whether a data-section offset can hold a struct of `size` bytes. Every offset
// in the side-car is authored against the retail archive, so this only fires if
// the two have gone out of step.
static int InBounds(HSD_Archive *archive, u32 off, u32 size)
{
    return off != 0 && off + size <= archive->header.data_size;
}

static void SetRamp(HSD_Archive *archive, u32 off, void *buffer, u32 length)
{
    HSD_FObjDesc *fobj = (HSD_FObjDesc *)(archive->data + off);

    fobj->buffer = buffer;
    fobj->length = length;
}

static void PatchBank(HSD_Archive *archive, UiFrameBank *bank, int slot)
{
    HSD_TexAnim *tex;
    _HSD_ImageDesc **images;
    int n;
    int i;

    if (!InBounds(archive, bank->texanim_off, sizeof(HSD_TexAnim))
        || !InBounds(archive, bank->timg_off, sizeof(HSD_FObjDesc)))
        return;

    tex = (HSD_TexAnim *)(archive->data + bank->texanim_off);
    n = tex->n_imagetbl;
    if (n != bank->n_images || n + 1 > UI_FRAME_ENTRY_MAX)
        return;

    // The TLUT table and its ramp go together: a ramp keying an entry the table
    // does not have would index off the end of it.
    if (bank->tclt_off != 0)
    {
        int nt = tex->n_tluttbl;

        if (!InBounds(archive, bank->tclt_off, sizeof(HSD_FObjDesc))
            || nt != bank->n_tluts || nt + 1 > UI_FRAME_ENTRY_MAX
            || bank->tlut_src >= nt)
            return;

        HSD_TlutDesc **tluts = stc_tlut_pool[slot];

        for (i = 0; i < nt; i++)
            tluts[i] = tex->tluttbl[i];
        tluts[nt] = tluts[bank->tlut_src];
        tex->tluttbl = tluts;
        tex->n_tluttbl = nt + 1;
        SetRamp(archive, bank->tclt_off, bank->tclt_buf, bank->tclt_len);
    }

    images = stc_image_pool[slot];
    for (i = 0; i < n; i++)
        images[i] = tex->imagetbl[i];
    images[n] = bank->image;
    tex->imagetbl = images;
    tex->n_imagetbl = n + 1;
    SetRamp(archive, bank->timg_off, bank->timg_buf, bank->timg_len);
}

// Gm_LoadGameFile's `mr r3, r25`, where r25 is the basename and both the
// preload-hit and cold-read paths are still ahead. The name is stashed rather
// than read at the tail because the preload path reuses r25 as an allocation
// size; the loader is synchronous, so one slot is enough.
static void UiFrames_OnLoadBegin(char *name)
{
    CustomMachines_CopyStr(stc_loading, name, UI_FRAME_NAME_MAX);
}
CODEPATCH_HOOKCREATE(0x80059834, "mr 3,25\n\t", UiFrames_OnLoadBegin, "", 0)

// Where the two paths converge, r30 holding the archive each built.
static void UiFrames_OnLoadEnd(HSD_Archive *archive)
{
    int f;

    if (archive == NULL || stc_files == NULL)
        return;

    for (f = 0; f < stc_file_num; f++)
    {
        UiFrameFile *file = &stc_files[f];
        u32 b;

        if (!NameMatches(stc_loading, file->name))
            continue;
        for (b = 0; b < file->n_banks; b++)
            PatchBank(archive, &file->banks[b], stc_slot_base[f] + b);
        return;
    }
}
CODEPATCH_HOOKCREATE(0x800599f8, "mr 3,30\n\t", UiFrames_OnLoadEnd, "", 0)

void CustomMachineUiFrames_OnBoot(void)
{
    HSD_Archive *archive = CustomMachines_LoadArchiveAtBoot(UI_FRAMES_FILE);
    UiFrameFile *files;
    int slots = 0;
    int f;

    if (archive == NULL)
    {
        OSReport("[UiFrames] %s missing, appended character has no art\n", UI_FRAMES_FILE);
        return;
    }

    files = Archive_GetPublicAddress(archive, UI_FRAMES_PUBLIC);
    if (files == NULL)
    {
        OSReport("[UiFrames] %s has no %s public\n", UI_FRAMES_FILE, UI_FRAMES_PUBLIC);
        return;
    }

    for (f = 0; f < UI_FRAME_FILE_MAX && files[f].name != NULL; f++)
    {
        if (slots + (int)files[f].n_banks > UI_FRAME_BANK_MAX)
        {
            OSReport("[UiFrames] more than %d banks, %s onward dropped\n",
                     UI_FRAME_BANK_MAX, files[f].name);
            break;
        }
        stc_slot_base[f] = slots;
        slots += files[f].n_banks;
    }
    stc_files = files;
    stc_file_num = f;

    CODEPATCH_HOOKAPPLY(0x80059834);
    CODEPATCH_HOOKAPPLY(0x800599f8);
    OSReport("[UiFrames] %d bank(s) across %d archive(s), hooks installed\n",
             slots, stc_file_num);
}
