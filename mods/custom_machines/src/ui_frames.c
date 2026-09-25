// Grows every UI art bank that picks its frame by kind, so the appended machines have
// frames. Twenty banks across eight menu archives are TexAnims whose animation frame is
// the CharacterKind, and the two HUD machine icon banks are keyed by registry order; each gains
// CUSTOM_MACHINE_MAX frames out of the CmUiFrames.dat side-car, plus the quad-scale tracks
// that size the menu ones, every time its archive loads through Gm_LoadGameFile
// (0x80059818).

#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "menu.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

#define UI_FRAMES_FILE   "CmUiFrames.dat"
#define UI_FRAMES_PUBLIC "cmUiFrames"

// The widest bank grows 34 -> 47; the pool holds a table per bank for the run of
// the game, because the archive holding a bank is rebuilt on every scene entry.
#define UI_FRAME_ENTRY_MAX 48
#define UI_FRAME_BANK_MAX  24
#define UI_FRAME_FILE_MAX  12

// A bank whose appended frames count registered machines in registry order rather than
// appended CharacterKinds: one that draws a machine by kind.
#define UI_FRAME_KEY_MACHINE 1

// Fits the longest bank archive basename, "MnSelstadiumAll", plus the engine's trailing '.'.
#define UI_FRAME_NAME_MAX 24

// Where the engine diverts the two characters it keeps out of the roster's own
// frame run, each plus color. Both runs move up by the appended count.
#define DIVERT_DEDEDE_FRAME    20
#define DIVERT_METAKNIGHT_FRAME 30

// Every `addi rD, rS, 20` forming King Dedede's diverted frame. Meta Knight's
// `addi rD, rS, 30` is 0x14 past each of them, in the same if/else chain.
static const u32 stc_divert_sites[] = {
    0x80151b08, 0x80151bd4, // AirRideSelect_SetSIcon2Color / _SetSIcon2Character
    0x8015c5c8, 0x8015c694, // CitySelect_SetSIcon2Color / _SetSIcon2Character
    0x801672bc, 0x8016b064, // MnResult_CreateSiconBig / MnResult2_
    0x8016e9bc, 0x80177b5c, // MnResult4_CreateSiconBig / MnResultCt_
};
#define DIVERT_METAKNIGHT_OFF 0x14

// Laid out byte for byte by whatever authors the side-car.
typedef struct UiFrameBank
{
    u32 texanim_off;       // 0x00 data-section offset of the TexAnim
    u16 n_images;          // 0x04 the archive's own image count, before the append
    u16 n_tluts;           // 0x06 its own TLUT count
    u16 src;               // 0x08 frame the appended ones are cloned from
    u16 n_appended;        // 0x0a frames the ramps were authored to add
    _HSD_ImageDesc *image; // 0x0c the placeholder frame
    u32 timg_off;          // 0x10 data-section offset of the image-index FObjDesc
    void *timg_buf;        // 0x14 its replacement keyframe buffer
    u32 timg_len;          // 0x18
    u32 tclt_off;          // 0x1c the TLUT-index FObjDesc, 0 when the bank has none
    void *tclt_buf;        // 0x20
    u32 tclt_len;          // 0x24
    u16 src_width;         // 0x28 source frame geometry, the key a machine's art matches
    u16 src_height;        // 0x2a
    u32 src_format;        // 0x2c
    u8 timg_flag;          // 0x30 value flag the widened ramp needs
    u8 tclt_flag;          // 0x31
    u8 key;                // 0x32 UI_FRAME_KEY_MACHINE, else keyed by CharacterKind
    u8 pad;                // 0x33
} UiFrameBank;

// A ramp outside any bank that is keyed by CharacterKind all the same: the Sicon
// quad's scale tracks. Nothing but the keyframe buffer changes, so the widened one
// is baked whole.
typedef struct UiFrameRamp
{
    u32 fobj_off; // 0x00 data-section offset of the FObjDesc
    void *buffer; // 0x04 its replacement keyframe buffer
    u32 length;   // 0x08
    u8 flag;      // 0x0c value flag the widened ramp needs
    u8 pad[3];    // 0x0d
} UiFrameRamp;

typedef struct UiFrameFile
{
    char *name;         // 0x00 archive basename, no extension
    u32 n_banks;        // 0x04
    UiFrameBank *banks; // 0x08
    u32 n_ramps;        // 0x0c
    UiFrameRamp *ramps; // 0x10
} UiFrameFile;

static UiFrameFile *stc_files;
static int stc_slot_base[UI_FRAME_FILE_MAX]; // first pool slot of each file's banks
static int stc_file_num;
static int stc_appended;

// A machine's art, by its appended CharacterKind's slot and by its registry index.
static CustomMachineArt *stc_art[CUSTOM_MACHINE_MAX];
static CustomMachineArt *stc_machine_art[CUSTOM_MACHINE_MAX];

static _HSD_ImageDesc *stc_image_pool[UI_FRAME_BANK_MAX][UI_FRAME_ENTRY_MAX];
static HSD_TlutDesc *stc_tlut_pool[UI_FRAME_BANK_MAX][UI_FRAME_ENTRY_MAX];
static u8 stc_grown[UI_FRAME_BANK_MAX];

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

static void SetRamp(HSD_Archive *archive, u32 off, void *buffer, u32 length, u8 flag)
{
    HSD_FObjDesc *fobj = (HSD_FObjDesc *)(archive->data + off);

    fobj->buffer = buffer;
    fobj->length = length;
    fobj->value_flag = flag;
}

// The image an appended frame shows: the machine holding that frame's CharacterKind or
// registry index, if it shipped art for this bank's geometry, else the placeholder.
static _HSD_ImageDesc *AppendedImage(UiFrameBank *bank, int frame)
{
    CustomMachineArt **arts = bank->key == UI_FRAME_KEY_MACHINE ? stc_machine_art : stc_art;
    CustomMachineArt *art = frame < CUSTOM_MACHINE_MAX ? arts[frame] : NULL;

    if (art != NULL)
    {
        for (int i = 0; i < art->count; i++)
        {
            CustomMachineArtEntry *e = &art->entry[i];
            if (e->width == bank->src_width && e->height == bank->src_height &&
                e->format == bank->src_format && e->image != NULL)
                return (_HSD_ImageDesc *)e->image;
        }
    }
    return bank->image;
}

// Returns whether the bank holds the appended frames, including a preloaded archive the
// pool was already spliced into.
static int PatchBank(HSD_Archive *archive, UiFrameBank *bank, int slot)
{
    HSD_TexAnim *tex;
    _HSD_ImageDesc **images;
    int a = bank->n_appended;
    int n;
    int i;

    if (!InBounds(archive, bank->texanim_off, sizeof(HSD_TexAnim))
        || !InBounds(archive, bank->timg_off, sizeof(HSD_FObjDesc)))
        return 0;

    tex = (HSD_TexAnim *)(archive->data + bank->texanim_off);
    if (tex->imagetbl == stc_image_pool[slot])
        return 1;
    n = tex->n_imagetbl;
    if (n != bank->n_images || n + a > UI_FRAME_ENTRY_MAX)
        return 0;

    // The TLUT table and its ramp go together: a ramp keying an entry the table
    // does not have would index off the end of it.
    if (bank->tclt_off != 0)
    {
        int nt = tex->n_tluttbl;

        if (!InBounds(archive, bank->tclt_off, sizeof(HSD_FObjDesc))
            || nt != bank->n_tluts || nt + a > UI_FRAME_ENTRY_MAX
            || bank->src >= nt)
            return 0;

        HSD_TlutDesc **tluts = stc_tlut_pool[slot];

        for (i = 0; i < nt; i++)
            tluts[i] = tex->tluttbl[i];
        for (i = 0; i < a; i++)
            tluts[nt + i] = tluts[bank->src];
        tex->tluttbl = tluts;
        tex->n_tluttbl = nt + a;
        SetRamp(archive, bank->tclt_off, bank->tclt_buf, bank->tclt_len, bank->tclt_flag);
    }

    images = stc_image_pool[slot];
    for (i = 0; i < n; i++)
        images[i] = tex->imagetbl[i];
    for (i = 0; i < a; i++)
        images[n + i] = AppendedImage(bank, i);
    tex->imagetbl = images;
    tex->n_imagetbl = n + a;
    SetRamp(archive, bank->timg_off, bank->timg_buf, bank->timg_len, bank->timg_flag);
    return 1;
}

// Gm_LoadGameFile's `mr r3, r25`, where r25 is the basename and both the
// preload-hit and cold-read paths are still ahead. The name is stashed rather
// than read at the tail because the preload path reuses r25 as an allocation
// size; the loader is synchronous, so one slot is enough.
static void OnLoadBegin(char *name)
{
    CustomMachines_CopyStr(stc_loading, name, UI_FRAME_NAME_MAX);
}
CODEPATCH_HOOKCREATE(0x80059834, "mr 3,25\n\t", OnLoadBegin, "", 0)

// Where the two paths converge, r30 holding the archive each built.
static void OnLoadEnd(HSD_Archive *archive)
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
        {
            int slot = stc_slot_base[f] + b;
            stc_grown[slot] = PatchBank(archive, &file->banks[b], slot);
        }
        for (b = 0; b < file->n_ramps; b++)
        {
            UiFrameRamp *r = &file->ramps[b];

            if (InBounds(archive, r->fobj_off, sizeof(HSD_FObjDesc)))
                SetRamp(archive, r->fobj_off, r->buffer, r->length, r->flag);
        }
        return;
    }
}
CODEPATCH_HOOKCREATE(0x800599f8, "mr 3,30\n\t", OnLoadEnd, "", 0)

int CustomMachineUiFrames_IsGrown(const char *name)
{
    for (int f = 0; f < stc_file_num; f++)
    {
        if (!NameMatches(name, stc_files[f].name))
            continue;
        for (u32 b = 0; b < stc_files[f].n_banks; b++)
        {
            if (!stc_grown[stc_slot_base[f] + b])
                return 0;
        }
        return 1;
    }
    return 0;
}

// Load each registered machine's art side-car into its registry index's entry, and into
// the slot its CharacterKind takes if it has one. The archives are never freed - the
// banks point straight into them.
static int LoadMachineArt(void)
{
    char path[CUSTOM_MACHINE_PATH_MAX];
    int found = 0;

    for (int i = 0; i < CustomMachines_GetCount(); i++)
    {
        CustomMachineEntry *e = CustomMachines_GetEntry(i);
        int character = e->character_kind - CKIND_NUM;

        if (!CustomMachines_SideCarPath(path, sizeof(path), e->path, CUSTOM_MACHINE_ART_EXT))
            continue;
        if (DVDConvertPathToEntrynum(path) == -1)
            continue;

        void *mark = HSD_ArenaMark();
        HSD_Archive *arc = Archive_LoadFile(path);
        if (arc == NULL)
        {
            HSD_ArenaRelease(mark);
            continue;
        }

        CustomMachineArt *art =
            (CustomMachineArt *)Archive_GetPublicAddress(arc, CUSTOM_MACHINE_ART_SYMBOL);
        if (art == NULL || art->magic != CUSTOM_MACHINE_ART_MAGIC ||
            art->version != CUSTOM_MACHINE_ART_VERSION || art->count == 0)
        {
            OSReport("[UiFrames] %s is not a machine art side-car\n", path);
            HSD_ArenaRelease(mark);
            continue;
        }

        stc_machine_art[i] = art;
        if (character >= 0)
            stc_art[character] = art;
        found++;
    }
    return found;
}

// Move both color diverts past the appended frames. Each site's immediate is the
// base frame of one run, and the run is contiguous, so the whole of it moves with
// the one instruction.
static void MoveDiverts(void)
{
    for (int i = 0; i < (int)(sizeof(stc_divert_sites) / sizeof(u32)); i++)
    {
        u32 dedede = stc_divert_sites[i];
        u32 metaknight = dedede + DIVERT_METAKNIGHT_OFF;

        CustomMachines_SetImmediate(dedede, DIVERT_DEDEDE_FRAME + stc_appended);
        CustomMachines_SetImmediate(metaknight, DIVERT_METAKNIGHT_FRAME + stc_appended);
    }
}

void CustomMachineUiFrames_OnBoot(void)
{
    void *mark = HSD_ArenaMark();
    HSD_Archive *archive = Archive_LoadFile(UI_FRAMES_FILE);
    UiFrameFile *files;
    int slots = 0;
    int ramps = 0;
    int f;

    if (archive == NULL)
    {
        OSReport("[UiFrames] %s did not load, appended characters have no art\n", UI_FRAMES_FILE);
        HSD_ArenaRelease(mark);
        return;
    }

    files = Archive_GetPublicAddress(archive, UI_FRAMES_PUBLIC);
    if (files == NULL)
    {
        OSReport("[UiFrames] %s has no %s public\n", UI_FRAMES_FILE, UI_FRAMES_PUBLIC);
        HSD_ArenaRelease(mark);
        return;
    }

    for (f = 0; f < UI_FRAME_FILE_MAX && files[f].name != NULL; f++)
    {
        if (slots + (int)files[f].n_banks > UI_FRAME_BANK_MAX)
        {
            OSReport("[UiFrames] More than %d banks, %s onward dropped\n",
                     UI_FRAME_BANK_MAX, files[f].name);
            break;
        }
        if (stc_appended == 0 && files[f].n_banks > 0)
            stc_appended = files[f].banks[0].n_appended;
        stc_slot_base[f] = slots;
        slots += files[f].n_banks;
        ramps += files[f].n_ramps;
    }
    stc_files = files;
    stc_file_num = f;

    if (stc_appended <= 0)
    {
        OSReport("[UiFrames] %s adds no frames\n", UI_FRAMES_FILE);
        stc_files = NULL;
        HSD_ArenaRelease(mark);
        return;
    }

    // The ramps are authored keyframe data, so the side-car decides how many frames
    // every bank grows by. A CharacterKind past that run falls off the last key.
    if (stc_appended != CUSTOM_MACHINE_MAX)
        OSReport("[UiFrames] %s adds %d frames, not the %d appended kinds allow\n",
                 UI_FRAMES_FILE, stc_appended, CUSTOM_MACHINE_MAX);

    int art = LoadMachineArt();
    MoveDiverts();
    CODEPATCH_HOOKAPPLY(0x80059834);
    CODEPATCH_HOOKAPPLY(0x800599f8);
    OSReport("[UiFrames] %d bank(s) and %d quad ramp(s) across %d archive(s) +%d frames, "
             "%d machine(s) with art\n",
             slots, ramps, stc_file_num, stc_appended, art);
}
