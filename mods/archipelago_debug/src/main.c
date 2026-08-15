#include "os.h"
#include "game.h"
#include "hsd.h"
#include "hoshi/mod.h"
#include "hoshi/func.h"

#include "archipelago_api.h"
#include "custom_events_api.h"
#include "debug_menu.h"

const ArchipelagoAPI *ap_api = 0;
static const CustomEventsAPI *ce_api = 0;
static const CustomMachinesAPI *cm_api = 0;

static void TryImportApi(void)
{
    if (!ap_api)
        ap_api = (const ArchipelagoAPI *)Hoshi_ImportMod(
            (char *)ARCHIPELAGO_MOD_NAME,
            ARCHIPELAGO_API_MAJOR, ARCHIPELAGO_API_MINOR);
    if (!ce_api)
        ce_api = (const CustomEventsAPI *)Hoshi_ImportMod(
            (char *)CUSTOM_EVENTS_MOD_NAME,
            CUSTOM_EVENTS_API_MAJOR, CUSTOM_EVENTS_API_MINOR);
    if (!cm_api)
    {
        cm_api = (const CustomMachinesAPI *)Hoshi_ImportMod(
            (char *)CUSTOM_MACHINES_MOD_NAME,
            CUSTOM_MACHINES_API_MAJOR, CUSTOM_MACHINES_API_MINOR);
        DebugMenu_BindCustomMachines(cm_api);
    }
}

static void OnSaveLoaded(void)
{
    TryImportApi();
    if (!ap_api)
    {
        OSReport("[ApDebug] failed to import KARchipelago API\n");
        return;
    }
    if (!ce_api)
        OSReport("[ApDebug] custom_events API unavailable (Scale Change trigger disabled)\n");
    // Save load may have applied AP grants; resync the menu toggles.
    DebugMenu_RefreshStateFromMasks();
    OSReport("[ApDebug] API imported, debug menu ready\n");
}

static void OnFrameStart(void)
{
    if (!ap_api)
        return;

    HSD_Pad *pad = &stc_engine_pads[0];

    if (pad->down & PAD_BUTTON_DPAD_LEFT)
    {
        // Grant a random remote-placed reward (shuffled == 0xFFFF).
        u16 pool[256];
        int n = 0;
        for (int m = 0; m < GMMODE_NUM; m++)
        {
            int count = ap_api->GetRewardCount((GameMode)m);
            for (int ri = 0; ri < count && n < (int)(sizeof(pool) / sizeof(pool[0])); ri++)
            {
                if (ap_api->GetShuffledReward((GameMode)m, (u8)ri) == 0xFFFF)
                    pool[n++] = (u16)((m << 8) | ri);
            }
        }
        if (n == 0)
        {
            OSReport("[ApDebug] no remote-placed rewards available\n");
        }
        else
        {
            u16 picked = pool[HSD_Randi(n)];
            ap_api->GrantReward((GameMode)(picked >> 8), (u8)(picked & 0xFF));
            OSReport("[ApDebug] granted random remote-placed reward\n");
        }
    }

    if (pad->down & PAD_BUTTON_DPAD_RIGHT)
    {
        // Uniform pick across every AP unlock pool plus the persistent progression
        // items. Each entry is a contiguous { base, count } block, singletons count 1.
        const struct
        {
            int base;
            int count;
        } give_pools[] = {
            { AP_STADIUM_UNLOCK_BASE,       STKIND_NUM     },
            { AP_EVENT_UNLOCK_BASE,         EVKIND_NUM     },
            { AP_ABILITY_UNLOCK_BASE,       COPYKIND_NUM   },
            { AP_PATCH_UNLOCK_BASE,         PATCHKIND_NUM  },
            { AP_ITEM_UNLOCK_BASE,          ITUNLOCK_NUM   },
            // VCKIND_WHEELVSDEDEDE is the last enum value and is not an AP unlock.
            { AP_MACHINE_UNLOCK_BASE,       VCKIND_NUM - 1 },
            // Custom machine kinds resume the alignment past the gap at 855.
            { AP_MACHINE_UNLOCK_BASE + VCKIND_NUM, cm_api ? cm_api->GetCount() : 0 },
            { AP_BOX_UNLOCK_BASE,           BOXKIND_NUM    },
            { AP_STAGE_UNLOCK_AIRRIDE_BASE, AIRRIDE_NUM    },
            { AP_COLOR_UNLOCK_BASE,         KIRBYCOLOR_NUM },
            { AP_STAGE_UNLOCK_TOPRIDE_BASE, TOPRIDE_NUM    },
            { AP_TOPRIDE_ITEM_UNLOCK_BASE,  TRITEM_NUM     },
            // Progression, not unlocks: applied globally or at next round start
            // rather than spawning a pickup, so they are not in the L+Up pool.
            { AP_PERM_PATCH_BASE,           PATCHKIND_NUM  },
            { AP_ITEM_PERM_PATCH_ALL_UP,    1              },
            { AP_ITEM_PATCH_CAP_INCREASE,   1              },
            { AP_ITEM_SPAWN_RATE_UP,        1              },
        };
        int total = 0;
        for (int i = 0; i < (int)(sizeof(give_pools) / sizeof(give_pools[0])); i++)
            total += give_pools[i].count;
        int pick = HSD_Randi(total);
        int picked = -1;
        for (int i = 0; i < (int)(sizeof(give_pools) / sizeof(give_pools[0])); i++)
        {
            if (pick < give_pools[i].count)
            {
                picked = give_pools[i].base + pick;
                break;
            }
            pick -= give_pools[i].count;
        }
        if (ap_api->QueueItem(picked))
            OSReport("[ApDebug] queued random unlock/progression id=%d\n", picked);
        else
            OSReport("[ApDebug] queue full, could not give id=%d\n", picked);
    }

    if (pad->down & PAD_BUTTON_DPAD_DOWN)
    {
        ap_api->DebugTriggerDeathlinkReceive();
        OSReport("[ApDebug] triggered deathlink_receive\n");
    }

    if (pad->down & PAD_BUTTON_DPAD_UP)
    {
        if (pad->held & PAD_TRIGGER_L)
        {
            // L+Up: queue a random in-game item that applies to the current major mode.
            MajorKind major = Scene_GetCurrentMajor();
            int picked = -1;
            const char *mode_name = 0;
            if (major == MJRKIND_CITY)
            {
                // One uniform pool over the three CT give segments: the event range,
                // the full ITKIND range, and the standalone CT gives in the 1-99
                // block (the rest of that block is global or save-only progression).
                static const u16 ct_singletons[] = {
                    AP_ITEM_ALL_UP,
                    AP_ITEM_ALL_DOWN,
                    AP_ITEM_GIVE_DRAGOON,
                    AP_ITEM_GIVE_HYDRA,
                    AP_ITEM_1_HP_TRAP,
                    AP_ITEM_DROP_PATCHES_TRAP,
                };
                int n_evt = EVKIND_NUM;
                int n_it = AP_ITKIND_WEIGHTFAKE - AP_ITKIND_BASE + 1;
                int n_one = (int)(sizeof(ct_singletons) / sizeof(ct_singletons[0]));
                int r = HSD_Randi(n_evt + n_it + n_one);
                if (r < n_evt)
                    picked = AP_EVENT_BASE + r;
                else if (r < n_evt + n_it)
                    picked = AP_ITKIND_BASE + (r - n_evt);
                else
                    picked = ct_singletons[r - n_evt - n_it];
                mode_name = "CT";
            }
            else if (major == MJRKIND_AIR)
            {
                // Only copy abilities are honored outside CT; every other ITKIND
                // no-ops behind the Gm_IsInCity gate.
                int range = AP_ITKIND_COPYMIC - AP_ITKIND_COPYBOMB + 1;
                picked = AP_ITKIND_COPYBOMB + HSD_Randi(range);
                mode_name = "AR";
            }
            else if (major == MJRKIND_TOP)
            {
                // The TR gives apply their item to each human Kirby directly.
                int range = AP_TOPRIDE_ITEM_GIVE_PARTY_BALL - AP_TOPRIDE_ITEM_GIVE_BASE + 1;
                picked = AP_TOPRIDE_ITEM_GIVE_BASE + HSD_Randi(range);
                mode_name = "TR";
            }
            if (mode_name)
            {
                if (ap_api->QueueItem(picked))
                    OSReport("[ApDebug] queued random %s item id=%d\n", mode_name, picked);
                else
                    OSReport("[ApDebug] queue full, could not give %s item id=%d\n", mode_name, picked);
            }
            else
            {
                OSReport("[ApDebug] L+Up: not in a recognized 3D game scene, no items to give\n");
            }
        }
        else
        {
            // Do() returns 0 outside City Trial, when an event is already active,
            // or if custom_events didn't export its API.
            if (ce_api && ce_api->Do(CUSTOM_EVKIND_SCALE_CHANGE))
                OSReport("[ApDebug] triggered Scale Change event\n");
            else
                OSReport("[ApDebug] Scale Change trigger unavailable (needs City Trial, no active event, custom_events loaded)\n");
        }
    }

    if (pad->down & PAD_TRIGGER_Z)
    {
        // Unlock the hovered checklist cell as if completed in-game (Z is unused
        // by vanilla checklist navigation). ClearChecker_SetNewUnlock is
        // REPLACEFUNC'd, so the AP check fires and goal evaluation runs.
        u8 mode, k;
        if (!ap_api->GetHoveredCell(&mode, &k))
        {
            OSReport("[ApDebug] Z pressed but the cursor is not on a checklist cell\n");
            return;
        }

        u8 src_mode, src_ri;
        int has_placement = ap_api->ResolveCell(mode, k, &src_mode, &src_ri);
        if (has_placement)
        {
            u8 rtype = stc_reward_table_ptrs[src_mode][src_ri].reward_type;
            OSReport("[ApDebug] Z unlock mode=%d clear_kind=%d -> %s ri=%d type=%s (0x%02x)\n",
                     mode, k,
                     (src_mode == mode) ? "same-mode" : "cross-mode",
                     src_ri, Reward_TypeName(rtype), rtype);
        }
        else
        {
            OSReport("[ApDebug] Z unlock mode=%d clear_kind=%d (no local reward placement)\n",
                     mode, k);
        }

        ClearChecker_SetNewUnlock(mode, k);
        // SetNewUnlock bails when Checklist_IsCacheValid (always true in menus), so
        // RecordCheck ran but no clear[] bits were written; set the end state here.
        GameClearData *cd = gmGetClearcheckerTypeP(mode);
        cd->clear[k].is_new = 1;
        cd->clear[k].is_unlocked = 1;
        cd->clear[k].is_visible = 1;

        if (has_placement && DebugMenu_ShouldAutoGrantOnUnlock())
        {
            OSReport("[ApDebug] auto-granting reward mode=%d ri=%d\n",
                     src_mode, src_ri);
            ap_api->GrantReward(src_mode, src_ri);
            Hoshi_WriteSave();
        }
    }
}

ModDesc mod_desc = {
    .name = "KARchipelago Debug",
    .author = "DeDeDK",
    .version.major = 1,
    .version.minor = 0,
    .option_desc = &DebugMod_RootOption,
    .OnSaveLoaded = OnSaveLoaded,
    .OnFrameStart = OnFrameStart,
};
