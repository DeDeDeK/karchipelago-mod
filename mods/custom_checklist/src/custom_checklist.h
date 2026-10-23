#ifndef CUSTOM_CHECKLIST_H
#define CUSTOM_CHECKLIST_H

#include "game.h"

#include "custom_checklist_api.h"

// Bounds the registry, the save slots, and the tab ring.
#define CC_TAB_MAX 4

typedef struct CCTab
{
    CustomChecklistDesc desc;
    GameClearData *clear;    // the tab's save slot once bound, else ram_clear
    GameClearData ram_clear; // until the save binds, or for good without one
    u32 name_hash;           // save-slot key
    int minor_id;
    int art_reported;        // a missing tex_file or symbol was reported
} CCTab;

extern CCTab cc_tabs[CC_TAB_MAX];
extern int cc_tab_num;

#define CC_TAB_MODE(idx) (GMMODE_NUM + (idx))

// Stamped on init, checked on load. hoshi matches a card block by mod-name hash and size
// only, never ModDesc.version, so a layout change would otherwise be read as data. Bump it
// whenever CCSave changes.
#define CC_SAVE_STAMP 0x43434B03

// Each tab's GameClearData, as the vanilla tabs' lives in the game's save. Slots are keyed
// by tab-name hash so they survive tabs being added, removed or reordered, and are never
// released.
typedef struct CCSave
{
    u32 stamp; // first, so a resize cannot displace it
    struct
    {
        u32 name_hash; // 0 = empty
        GameClearData clear;
    } slots[CC_TAB_MAX];
} CCSave;

void CCScene_InstallHooks(void);
int CCScene_InstallMinor(void);
int CCScene_FindTab(int minor);
int CCScene_GetBuildMode(void);

void CCLabels_Apply(int idx);

void CCArt_Load(int idx);
void CCArt_Apply(void);

void CCBoard_Init(int idx);
void CCBoard_Bind(int idx);
void CCBoard_InitSave(CCSave *save);
void CCBoard_LoadSave(CCSave *save);
int CCBoard_HasPendingUnlock(const GameClearData *cd);
int CCBoard_RevealChecks(int idx);
void CCBoard_Evaluate(void);

#endif // CUSTOM_CHECKLIST_H
