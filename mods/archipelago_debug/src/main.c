#include "os.h"
#include "game.h"
#include "hsd.h"
#include "hoshi/mod.h"
#include "hoshi/func.h"

#include "archipelago_api.h"
#include "debug_menu.h"

const ArchipelagoAPI *ap_api = 0;

static void TryImportApi(void)
{
    if (!ap_api)
        ap_api = (const ArchipelagoAPI *)Hoshi_ImportMod(
            (char *)ARCHIPELAGO_MOD_NAME,
            ARCHIPELAGO_API_MAJOR, ARCHIPELAGO_API_MINOR);
}

static void OnSaveLoaded(void)
{
    TryImportApi();
    if (!ap_api)
    {
        OSReport("[ApDebug] failed to import KARchipelago API\n");
        return;
    }
    // Reflect archipelago's masks into our local toggle state so the menu
    // matches reality after AP grants from save load.
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
        // Build a pool of every remote-placed reward (shuffled == 0xFFFF),
        // pick one at random, grant it.
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
        // Pick uniformly across every AP unlock pool. Useful for exercising
        // the receive path on unlocks specifically, including idempotence
        // (the same unlock landing twice should be a no-op).
        static const struct
        {
            int base;
            int count;
        } unlock_pools[] = {
            { AP_STADIUM_UNLOCK_BASE,       STKIND_NUM     },
            { AP_EVENT_UNLOCK_BASE,         EVKIND_NUM     },
            { AP_ABILITY_UNLOCK_BASE,       COPYKIND_NUM   },
            { AP_PATCH_UNLOCK_BASE,         PATCHKIND_NUM  },
            { AP_ITEM_UNLOCK_BASE,          ITUNLOCK_NUM   },
            // VCKIND_WHEELVSDEDEDE is the last enum value but is intentionally
            // not exposed as an AP unlock (see archipelago_api.h).
            { AP_MACHINE_UNLOCK_BASE,       VCKIND_NUM - 1 },
            { AP_BOX_UNLOCK_BASE,           BOXKIND_NUM    },
            { AP_STAGE_UNLOCK_AIRRIDE_BASE, AIRRIDE_NUM    },
            { AP_COLOR_UNLOCK_BASE,         KIRBYCOLOR_NUM },
            { AP_STAGE_UNLOCK_TOPRIDE_BASE, TOPRIDE_NUM    },
            { AP_TOPRIDE_ITEM_UNLOCK_BASE,  TRITEM_NUM     },
        };
        int total = 0;
        for (int i = 0; i < (int)(sizeof(unlock_pools) / sizeof(unlock_pools[0])); i++)
            total += unlock_pools[i].count;
        int pick = HSD_Randi(total);
        int picked = -1;
        for (int i = 0; i < (int)(sizeof(unlock_pools) / sizeof(unlock_pools[0])); i++)
        {
            if (pick < unlock_pools[i].count)
            {
                picked = unlock_pools[i].base + pick;
                break;
            }
            pick -= unlock_pools[i].count;
        }
        if (ap_api->QueueItem(picked))
            OSReport("[ApDebug] queued random unlock id=%d\n", picked);
        else
            OSReport("[ApDebug] queue full, could not give unlock id=%d\n", picked);
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
            // L+Up: queue a random in-game item from the pool that applies to
            // the current major mode. Complements the unlocks pool on D-Pad
            // Right — these are the items you'd actually pick up in-game.
            MajorKind major = Scene_GetCurrentMajor();
            int picked = -1;
            const char *mode_name = 0;
            if (major == MJRKIND_CITY)
            {
                // Full ITKIND range — boxes, copies, food, patches, fakes, etc.
                int range = AP_ITKIND_WEIGHTFAKE - AP_ITKIND_BASE + 1;
                picked = AP_ITKIND_BASE + HSD_Randi(range);
                mode_name = "CT";
            }
            else if (major == MJRKIND_AIR)
            {
                // Only copy abilities are honored outside CT — every other
                // ITKIND falls through to the Gm_IsInCity gate in
                // ap_item_handler and no-ops.
                int range = AP_ITKIND_COPYMIC - AP_ITKIND_COPYBOMB + 1;
                picked = AP_ITKIND_COPYBOMB + HSD_Randi(range);
                mode_name = "AR";
            }
            else if (major == MJRKIND_TOP)
            {
                // TR-specific GIVE pool spawns the matching TR item at each
                // human Kirby's position.
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
            ap_api->DebugTriggerTraplinkReceive();
            OSReport("[ApDebug] triggered traplink_receive\n");
        }
    }

    if (pad->down & PAD_TRIGGER_Z)
    {
        // In a checklist menu, Z unlocks the currently hovered cell as if the
        // objective had been completed in-game. Z is unused by vanilla
        // checklist navigation. ClearChecker_SetNewUnlock is REPLACEFUNC'd by
        // archipelago's check_detection, so the AP check fires and goal
        // evaluation runs.
        u8 mode, k;
        if (!ap_api->GetHoveredCell(&mode, &k))
        {
            OSReport("[ApDebug] Z pressed but no cell hovered yet (move cursor first)\n");
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
        // SetNewUnlock bails when Checklist_IsCacheValid (always true in
        // menus), so RecordCheck ran but no clear[] bits got written. Set the
        // end-state bits directly.
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
