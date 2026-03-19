#include "game.h"
#include "inline.h"

#include "main.h"
#include "textbox.h"
#include "ap_item_handler.h"

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
    AP_EVENT_METEOR,
    AP_EVENT_RAILFIRE,
    AP_EVENT_BOUNCE,
    AP_EVENT_FAKEPOWERUPS,
    AP_EVENT_RUNAMOK,
    AP_ITEM_1_HP_TRAP,
};
#define TRAP_ITEM_COUNT (sizeof(trap_items) / sizeof(trap_items[0]))

// Read from the traplink_receive location and trigger a random trap
void TrapLink_PerFrame(GOBJ *g)
{
    if (Gm_GetIntroState() != GMINTRO_END)
        return;

    if (archipelago_data->traplink_receive)
    {
        uint trap_id = trap_items[HSD_Randi(TRAP_ITEM_COUNT)];
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
