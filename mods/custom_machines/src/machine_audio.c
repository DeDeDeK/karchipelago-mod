// A drop-in machine's sounds, and the star class's per-kind audio parameter row.
//
// The row array is authored in VcCommon.dat, sized to the 19 vanilla star slots
// and reloaded per scene, so it is re-copied wider and repointed on every
// vcLoadCommon. Each registered machine's row starts as a copy of its
// descriptor's clone_kind.
//
// A companion .ssm beside the .dat then takes its own samples over that row.
// Reaching one needs three things, all set up on the first vcLoadCommon, by which
// point the audio system is running and the DVD is available: an SSM slot holding
// the samples, a range of global sound indices no vanilla bank claims, and a
// script per sound - a copy of the clone kind's, with its sound index rewritten so
// the drop-in keeps the donor's volume and pitch envelope - in one SEM bank
// appended past the vanilla 20. That bank is installed again after every
// FGM_InitSEM, which reloads airride.sem and puts the vanilla map back.

#include "os.h"
#include "audio.h"
#include "machine.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

// The named FGM id slots of MachineAudioParams, which open the struct in order and
// are the whole of a machine's voice. A bank holds one entry per slot and marks the
// ones it does not supply with a sample rate of 0.
#define SOUND_ROLE_NUM 13

// Retail's longest script is 70 commands.
#define SCRIPT_CMD_MAX 72

// The vanilla banks tile global sound indices 0..614, so a drop-in bank's
// samples start here. What is resident says nothing about the ceiling: the star
// bank is still on disc when the first machine registers at the title screen.
#define VANILLA_SOUND_NUM 615

// The widened script map and the scripts it points at. Both are static: the map
// stays installed across scenes, and HSD_MemAlloc only returns memory that
// outlives a scene inside a mod's boot callback. Retail is 20 banks and 825
// scripts, and one machine filling all 13 roles copies about 250 commands.
#define SCRIPT_MAP_BANK_MAX 24
#define SCRIPT_MAP_SCRIPT_MAX 896
#define SCRIPT_POOL_WORDS 1024

static MachineAudioParams stc_star_audio[CUSTOM_VCSTAR_NUM];

typedef struct DropinBank
{
    int found;                   // a companion .ssm exists and is worth loading
    int sound[SOUND_ROLE_NUM];   // global sound index per role, -1 where the bank has none
    u32 *script[SOUND_ROLE_NUM]; // the cloned script that plays it
    int sfx[SOUND_ROLE_NUM];     // its FGM id once the script map carries it
} DropinBank;

static DropinBank stc_bank[CUSTOM_MACHINE_MAX];
static int stc_script_num;   // cloned scripts, across every machine
static int stc_stamp_base;   // 0 leaves the staged header alone, else the base plus one
static int stc_ready;

static int stc_map_starts[SCRIPT_MAP_BANK_MAX];
static u32 *stc_map_scripts[SCRIPT_MAP_SCRIPT_MAX];
static u32 stc_script_pool[SCRIPT_POOL_WORDS];
static int stc_pool_next;

// FGM_LoadBankCallback takes a bank's global sound index base out of the file
// header it has just staged. A drop-in bank cannot know what else is resident,
// so its base is assigned here instead, while its own load is the only one in
// flight. Hooked past the prologue, where the DVD callback's arguments are dead.
static void StampDropinSoundBase(void)
{
    if (stc_stamp_base != 0)
        stc_ssm_load_header->sound_base = stc_stamp_base - 1;
}

CODEPATCH_HOOKCREATE(0x80447eb8,
    "",
    StampDropinSoundBase,
    "",
    0
)

// The vanilla star slot a machine's rows and donor scripts come from. A descriptor
// naming a bike or an out-of-range kind falls back to the Slick Star.
static int CloneKind(const CustomMachineEntry *e)
{
    if (e->clone_kind < 0 || e->clone_kind >= VCSTAR_NUM)
        return VCKIND_SLICK;
    return e->clone_kind;
}

static SSMSound *ChunkSound(SSMChunk *chunk, int index)
{
    SSMSound *s = (SSMSound *)(chunk + 1);
    for (int i = 0; i < index; i++)
        s = (SSMSound *)((u8 *)s + 0x10 + s->channel_num * 0x40);
    return s;
}

// The companion path: the machine archive's, with its extension swapped.
static int BankPath(char *dst, int max, const char *src)
{
    const char *ext = CUSTOM_MACHINE_AUDIO_EXT;
    int n = 0;
    int e = 0;

    while (src[n] != '\0')
    {
        if (n + 1 >= max)
            return 0;
        dst[n] = src[n];
        n++;
    }
    while (ext[e] != '\0')
        e++;
    if (n < e || dst[n - e] != '.')
        return 0;
    for (int i = 0; i < e; i++)
        dst[n - e + i] = ext[i];
    dst[n] = '\0';
    return 1;
}

// Load every companion bank into one SSM slot sized for all of them, recording
// the global sound index each role landed on.
static void LoadDropinBanks(void)
{
    char path[CUSTOM_MACHINE_PATH_MAX];
    int total = 0;

    for (int i = 0; i < CustomMachines_GetCount(); i++)
    {
        DropinBank *b = &stc_bank[i];
        for (int r = 0; r < SOUND_ROLE_NUM; r++)
            b->sound[r] = -1;

        if (!BankPath(path, sizeof(path), CustomMachines_GetEntry(i)->path))
            continue;
        if (DVDConvertPathToEntrynum(path) == -1)
            continue;
        int size = File_GetSize(path);
        if (size <= (int)sizeof(SSMHeader))
            continue;

        b->found = 1;
        total += OSRoundUp32B(size);
    }

    if (total == 0)
        return;

    // The slot is sized from whole files, which overshoots each bank's ADPCM by
    // its entry table. Slots are carved for the run of the game, so this happens
    // once and the slack is never reclaimed.
    int slot = FGM_GetNextLargestSSMSizeIndex(total);
    if (slot < 0)
    {
        OSReport("[MachineAudio] no room in ARAM for %d bytes of drop-in samples\n", total);
        return;
    }

    int next_index = VANILLA_SOUND_NUM;

    for (int i = 0; i < CustomMachines_GetCount(); i++)
    {
        DropinBank *b = &stc_bank[i];
        if (!b->found || !BankPath(path, sizeof(path), CustomMachines_GetEntry(i)->path))
            continue;

        stc_stamp_base = next_index + 1;
        FGM_QueueLoad(path, slot, NULL, NULL);
        FGM_SychronousLoad(DoTasks);
        stc_stamp_base = 0;

        SSMChunk *chunk = stc_ssm_slot_chunks[slot];
        if (chunk == NULL || chunk->sound_base != next_index)
        {
            OSReport("[MachineAudio] %s did not load\n", path);
            continue;
        }

        int roles = chunk->sound_num < SOUND_ROLE_NUM ? chunk->sound_num : SOUND_ROLE_NUM;
        for (int r = 0; r < roles; r++)
        {
            SSMSound *s = ChunkSound(chunk, r);
            if (s->sample_rate != 0)
                b->sound[r] = s->index;
        }
        next_index += chunk->sound_num;
        OSReport("[MachineAudio] %s -> SSM slot %d, sounds %d..%d\n",
                 path, slot, chunk->sound_base, next_index - 1);
    }
}

// Commands up to and including the terminator. Every retail script ends on one.
static int ScriptLength(const u32 *script)
{
    for (int i = 0; i < SCRIPT_CMD_MAX; i++)
    {
        u32 op = script[i] >> 24;
        if (op == FGMSCRIPT_END || op == FGMSCRIPT_END_RELEASE)
            return i + 1;
    }
    return 0;
}

static u32 *CloneScript(const u32 *donor, int sound_index)
{
    int n = ScriptLength(donor);
    if (n == 0 || stc_pool_next + n > SCRIPT_POOL_WORDS)
        return NULL;

    u32 *copy = &stc_script_pool[stc_pool_next];
    stc_pool_next += n;

    for (int i = 0; i < n; i++)
    {
        u32 cmd = donor[i];
        if ((cmd >> 24) == FGMSCRIPT_SOUND)
            cmd = (cmd & 0xFFFF0000) | (u32)(sound_index & 0xFFFF);
        copy[i] = cmd;
    }
    return copy;
}

// One script per loaded drop-in sound, copied from whichever vanilla script the
// clone kind uses for that sound slot. Runs once; the copies outlive any reload
// of airride.sem.
static void CloneDropinScripts(void)
{
    int *starts = *stc_fgm_bank_start_script;
    u32 **scripts = (u32 **)*stc_fgm_script_data;

    for (int i = 0; i < CustomMachines_GetCount(); i++)
    {
        DropinBank *b = &stc_bank[i];
        CustomMachineEntry *e = CustomMachines_GetEntry(i);
        int clone = CloneKind(e);
        const int *donor_row = (const int *)&stc_star_audio[clone];

        for (int r = 0; r < SOUND_ROLE_NUM; r++)
        {
            b->sfx[r] = -1;
            if (b->sound[r] < 0)
                continue;

            int donor_fid = donor_row[r];
            if (donor_fid < 0)
            {
                OSReport("[MachineAudio] '%s' sound %d has no script on kind %d to copy\n",
                         e->name, r, clone);
                continue;
            }

            int donor = starts[(donor_fid >> 16) & 0xFFFF] + (donor_fid & 0xFFFF);
            b->script[r] = CloneScript(scripts[donor], b->sound[r]);
            if (b->script[r] != NULL)
                stc_script_num++;
            else
                OSReport("[MachineAudio] no room to copy the script for '%s' sound %d\n",
                         e->name, r);
        }
    }
}

// Widen the script map by one bank holding those scripts. Nothing bounds the map
// but the four r13 globals, so relocating both tables and bumping both counts is
// the whole change, and every vanilla FGM id keeps its meaning because the
// appended bank sits past them all.
static void InstallScriptMap(void)
{
    u32 **old_scripts = (u32 **)*stc_fgm_script_data;
    if (stc_script_num == 0 || old_scripts == stc_map_scripts)
        return;

    int bank_num = *stc_fgm_bank_num;
    int script_num = *stc_fgm_script_num;
    int *old_starts = *stc_fgm_bank_start_script;

    if (bank_num + 1 > SCRIPT_MAP_BANK_MAX
        || script_num + stc_script_num > SCRIPT_MAP_SCRIPT_MAX)
    {
        OSReport("[MachineAudio] script map needs %d banks / %d scripts, past the %d / %d reserved\n",
                 bank_num + 1, script_num + stc_script_num,
                 SCRIPT_MAP_BANK_MAX, SCRIPT_MAP_SCRIPT_MAX);
        return;
    }

    for (int i = 0; i < bank_num; i++)
        stc_map_starts[i] = old_starts[i];
    stc_map_starts[bank_num] = script_num;
    for (int i = 0; i < script_num; i++)
        stc_map_scripts[i] = old_scripts[i];

    int next = script_num;
    for (int i = 0; i < CustomMachines_GetCount(); i++)
    {
        DropinBank *b = &stc_bank[i];
        for (int r = 0; r < SOUND_ROLE_NUM; r++)
        {
            if (b->script[r] == NULL)
                continue;
            stc_map_scripts[next] = b->script[r];
            b->sfx[r] = (bank_num << 16) | (next - script_num);
            next++;
        }
    }

    *stc_fgm_bank_start_script = stc_map_starts;
    *stc_fgm_script_data = (void *)stc_map_scripts;
    *stc_fgm_script_num = next;
    *stc_fgm_bank_num = bank_num + 1;
}

// Hook at 0x8005c654, in FGM_LoadAirride.sem right after it installs the file it
// just read. The engine reloads the script map on a scene reset, which puts the
// vanilla tables back and loses the appended bank.
static void ReinstallScriptMap(void)
{
    if (stc_ready)
        InstallScriptMap();
}

CODEPATCH_HOOKCREATE(0x8005c654,
    "",
    ReinstallScriptMap,
    "",
    0
)

static void SpliceStarAudioParams(void)
{
    MachineAudioParamsLookup *lookup = *stc_machineAudioParams;
    if (lookup == NULL || lookup->params[0] == NULL || lookup->params[0] == stc_star_audio)
        return;

    for (int i = 0; i < VCSTAR_NUM; i++)
        stc_star_audio[i] = lookup->params[0][i];

    // The vanilla rows have to be in place first: the donor script for a drop-in
    // sound is the one the clone kind's row names for that slot.
    if (!stc_ready)
    {
        LoadDropinBanks();
        CloneDropinScripts();
        stc_ready = 1;
        InstallScriptMap();
    }

    for (int i = 0; i < CustomMachines_GetCount(); i++)
    {
        CustomMachineEntry *e = CustomMachines_GetEntry(i);
        stc_star_audio[e->star_slot] = stc_star_audio[CloneKind(e)];

        int *row = (int *)&stc_star_audio[e->star_slot];
        for (int r = 0; r < SOUND_ROLE_NUM; r++)
        {
            if (stc_bank[i].sfx[r] >= 0)
                row[r] = stc_bank[i].sfx[r];
        }
    }

    lookup->params[0] = stc_star_audio;
}

// Hook at 0x801c6d64, vcLoadCommon's epilogue, one instruction after it caches
// the audio-params lookup into r13+0x764.
CODEPATCH_HOOKCREATE(0x801c6d64,
    "",
    SpliceStarAudioParams,
    "",
    0
)

void CustomMachineAudio_OnBoot(void)
{
    CODEPATCH_HOOKAPPLY(0x80447eb8); // FGM_LoadBankCallback, past its prologue
    CODEPATCH_HOOKAPPLY(0x8005c654); // FGM_LoadAirride.sem, after FGM_InitSEM
    CODEPATCH_HOOKAPPLY(0x801c6d64); // vcLoadCommon epilogue
}
