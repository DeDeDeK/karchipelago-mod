#include "game.h"
#include "os.h"
#include "hoshi/mod.h"

#include "custom_checklist.h"

extern ModDesc mod_desc;

CCTab cc_tabs[CC_TAB_MAX];
int cc_tab_num = 0;

// Everything the per-frame paths would otherwise re-check every frame.
static int CC_IsValidDesc(const CustomChecklistDesc *d)
{
    if (!d || !d->name || !d->checks || d->check_num <= 0 ||
        !d->is_recorded || !d->record_complete)
        return 0;

    u64 seen[2] = {0, 0};
    for (int c = 0; c < d->check_num; c++)
    {
        const CustomCheck *chk = &d->checks[c];
        int ck = chk->clear_kind;
        if (ck < 0 || ck >= CLEAR_KIND_NUM || !chk->label || !chk->is_complete)
            return 0;
        if ((seen[ck >> 6] >> (ck & 63)) & 1)
            return 0;
        seen[ck >> 6] |= 1ULL << (ck & 63);
    }
    return 1;
}

static int CC_Register(const CustomChecklistDesc *desc)
{
    if (!CC_IsValidDesc(desc))
    {
        OSReport("[CustomChecklist] Register rejected: invalid descriptor\n");
        return -1;
    }
    if (cc_tab_num >= CC_TAB_MAX)
    {
        OSReport("[CustomChecklist] Register rejected: registry full (max %d)\n", CC_TAB_MAX);
        return -1;
    }

    int idx = cc_tab_num;
    CCTab *t = &cc_tabs[idx];
    t->desc = *desc;
    t->name_hash = (u32)hash_32_str(desc->name);
    if (!t->name_hash) // 0 marks an empty save slot
        t->name_hash = 1;
    t->minor_id = CCScene_InstallMinor();
    t->art_reported = 0;
    CCBoard_Init(idx);
    CCBoard_Bind(idx);
    cc_tab_num++;

    OSReport("[CustomChecklist] Registered '%s' as mode %d (minor scene %d, %d checks)\n",
             desc->name, CC_TAB_MODE(idx), t->minor_id, desc->check_num);
    return CC_TAB_MODE(idx);
}

static void CC_RevealAll(int mode)
{
    int idx = mode - GMMODE_NUM;
    if (idx < 0 || idx >= cc_tab_num)
        return;

    int n = CCBoard_RevealChecks(idx);
    if (n > 0)
        OSReport("[CustomChecklist] '%s': revealed %d cells\n", cc_tabs[idx].desc.name, n);
}

static const CustomChecklistAPI api = {
    .Register = CC_Register,
    .RevealAll = CC_RevealAll,
    .GetBuildMode = CCScene_GetBuildMode,
};

static void OnBoot(void)
{
    // Installed unconditionally; with no tabs registered they reproduce vanilla.
    CCScene_InstallHooks();

    Hoshi_ExportMod((void *)&api);

    OSReport("[CustomChecklist] Hooks installed, API exported (v%d.%d)\n",
             CUSTOM_CHECKLIST_API_MAJOR, CUSTOM_CHECKLIST_API_MINOR);
}

static void OnSaveInit(void)
{
    CCBoard_InitSave((CCSave *)mod_desc.save_ptr);
}

static void OnSaveLoaded(void)
{
    CCBoard_LoadSave((CCSave *)mod_desc.save_ptr);
}

ModDesc mod_desc = {
    .name = CUSTOM_CHECKLIST_MOD_NAME,
    .author = "DeDeDK",
    .version.major = CUSTOM_CHECKLIST_API_MAJOR,
    .version.minor = CUSTOM_CHECKLIST_API_MINOR,
    .save_size = sizeof(CCSave),
    .OnBoot = OnBoot,
    .OnSaveInit = OnSaveInit,
    .OnSaveLoaded = OnSaveLoaded,
    .OnFrameStart = CCBoard_Evaluate,
};
