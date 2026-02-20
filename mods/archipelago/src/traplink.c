#include "game.h"
#include "inline.h"

#include "main.h"

// Trap items that can be randomly selected when traplink is triggered
static uint trap_items[] = {
    AP_ITEM_HP_TRAP,                            // 1 HP damage
    AP_ITKIND_BASE + ITKIND_COPYSLEEP,          // Sleep ability
    AP_ITKIND_BASE + ITKIND_PANICSPIN,          // Panic spin
    AP_ITKIND_BASE + ITKIND_GORDO,              // Gordo
    AP_ITKIND_BASE + ITKIND_SPEEDMIN,           // Speed set to minimum
    AP_ITKIND_BASE + ITKIND_CHARGENONE,         // Charge removed
    AP_ITKIND_BASE + ITKIND_ACCELDOWN,          // Accel down
    AP_ITKIND_BASE + ITKIND_TOPSPEEDDOWN,       // Top Speed down
    AP_ITKIND_BASE + ITKIND_OFFENSEDOWN,        // Offense down
    AP_ITKIND_BASE + ITKIND_DEFENSEDOWN,        // Defense down
    AP_ITKIND_BASE + ITKIND_TURNDOWN,           // Turn down
    AP_ITKIND_BASE + ITKIND_GLIDEDOWN,          // Glide down
    AP_ITKIND_BASE + ITKIND_CHARGEDOWN,         // Charge down
    AP_ITKIND_BASE + ITKIND_WEIGHTDOWN,         // Weight down
    AP_EVENT_BASE + EVKIND_METEOR,              // Meteor event
    AP_EVENT_BASE + EVKIND_RAILFIRE,            // Rail station fire event
};
#define TRAP_ITEM_COUNT (sizeof(trap_items) / sizeof(trap_items[0]))

// Read from the traplink_receive location and trigger a random trap
void TrapLink_PerFrame() {
    if (archipelago_data->traplink_receive) {
        uint trap_id = trap_items[HSD_Randi(TRAP_ITEM_COUNT)];
        OSReport("Queueing trap item (AP ID %d)...\n", trap_id);
        TextBox_Enqueue("Trap received!");
        Item_Enqueue(trap_id);
        archipelago_data->traplink_receive = 0;
    }
}

void TrapLink_On3DLoadEnd() {
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, TrapLink_PerFrame, 0, 0, 0, 0);
}
