#include "game.h"
#include "inline.h"
#include "machine.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "textbox.h"
#include "traplink.h"
#include "ap_item_handler.h"

// Send a traplink if enabled.
void TrapLink_Send(void)
{
    if (!hoshi_menu_settings.traplink_enabled)
        return;

    OSReport("Traplink send triggered\n");
    archipelago_data->traplink_send = 1;
}

// Trap items that can be randomly selected when traplink is triggered
static uint trap_items[] = {
    AP_ITKIND_COPYSLEEP,
    AP_ITKIND_SPEEDMIN,
    AP_ITKIND_CHARGENONE,
    AP_ITKIND_ACCELDOWN,
    AP_ITKIND_TOPSPEEDDOWN,
    AP_ITKIND_OFFENSEDOWN,
    AP_ITKIND_DEFENSEDOWN,
    AP_ITKIND_TURNDOWN,
    AP_ITKIND_GLIDEDOWN,
    AP_ITKIND_CHARGEDOWN,
    AP_ITKIND_WEIGHTDOWN,
    AP_ITEM_METEOR_TRAP,
    AP_EVENT_METEOR,
    AP_EVENT_RAILFIRE,
    AP_EVENT_BOUNCE,
    AP_EVENT_FAKEPOWERUPS,
    AP_EVENT_RUNAMOK,
    AP_ITEM_1_HP_TRAP,
    AP_ITKIND_WEIGHTFAKE,
};
#define TRAP_ITEM_COUNT (sizeof(trap_items) / sizeof(trap_items[0]))

// Check if a trap item is an event that requires the event unlock mask.
// Returns 1 if the item is a locked event (should be excluded), 0 otherwise.
static int IsTrapItemLocked(uint item_id)
{
    if (item_id >= AP_EVENT_BASE && item_id < AP_EVENT_BASE + EVKIND_NUM)
    {
        EventKind kind = item_id - AP_EVENT_BASE;
        return !(save_data->event_unlocked_mask & (1 << kind));
    }
    return 0;
}

// Read from the traplink_receive location and trigger a random trap
void TrapLink_PerFrame(GOBJ *g)
{
    if (Gm_GetIntroState() != GMINTRO_END)
        return;

    if (archipelago_data->traplink_receive)
    {
        // Build filtered list excluding locked events
        uint candidates[TRAP_ITEM_COUNT];
        int count = 0;
        for (int i = 0; i < TRAP_ITEM_COUNT; i++)
        {
            if (!IsTrapItemLocked(trap_items[i]))
                candidates[count++] = trap_items[i];
        }

        if (count == 0)
        {
            OSReport("TrapLink: no eligible trap items, discarding\n");
            archipelago_data->traplink_receive = 0;
            return;
        }

        uint trap_id = candidates[HSD_Randi(count)];
        OSReport("Applying trap item (AP ID %d)...\n", trap_id);
        if (APItems_HandleItem(trap_id))
        {
            TextBox_Enqueue("Trap received!");
            archipelago_data->traplink_receive = 0;
        }
    }
}

void TrapLink_On3DLoadEnd()
{
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, TrapLink_PerFrame, 0, 0, 0, 0);
}

// Hook in Machine_OnTouchItem after CityItem_IsGoodPatch returns 0 (bad patch).
// Catches SPEEDMIN, CHARGENONE, and fake patches.
// At this point r20 = MachineData*.
// Clobbered instruction: lwz r0, 0xA10(r20)
static void TrapLink_OnBadPatch(MachineData *md)
{
    int ply = Machine_GetRiderPly(md);
    if (Ply_CheckIfCPU(ply))
        return;
    TrapLink_Send();
}
CODEPATCH_HOOKCREATE(0x801DB504,
    "mr 3, 20\n\t",
    TrapLink_OnBadPatch,
    "",
    0)

void TrapLink_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x801DB504);
    OSReport("Traplink send hooks installed\n");
}
