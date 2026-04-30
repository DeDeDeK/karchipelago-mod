#include "os.h"
#include "game.h"
#include "hoshi/settings.h"

#include "main.h"
#include "textbox.h"
#include "ap_item_handler.h"
#include "energylink.h"
#include "energylink_spend.h"

typedef struct SpendEntry
{
    APItemId item_id;
    float cost;
} SpendEntry;

static int Buy(OptionDesc *self)
{
    SpendEntry *entry = self->user_data;

    if (ap_data->energy_balance < entry->cost)
    {
        OSReport("[EnergyLink] Buy '%s' (id=%d) rejected: need %.0f, have %.0f\n",
                 self->name, entry->item_id, entry->cost, ap_data->energy_balance);
        TextBox_Enqueue("Not enough energy! Need %.0f, have %.0f", entry->cost, ap_data->energy_balance);
        return 0;
    }

    // Push onto the unprocessed queue so APItems_PerFrame applies it when
    // the scene/intro gate allows — same path as items received from AP.
    if (ap_save->unprocessed_count >= MAX_RECEIVED_ITEMS)
    {
        OSReport("[EnergyLink] Buy '%s' (id=%d) rejected: queue full\n",
                 self->name, entry->item_id);
        TextBox_Enqueue("Queue full — try again later");
        return 0;
    }
    ap_save->unprocessed_items[ap_save->unprocessed_count++] = entry->item_id;

    EnergyLink_Withdraw(entry->cost);

    OSReport("[EnergyLink] Bought '%s' (id=%d) for %.0f, balance %.0f\n",
             self->name, entry->item_id, entry->cost, ap_data->energy_balance);
    TextBox_Enqueue("Bought %s for %.0f energy", self->name, entry->cost);
    return 1;
}

#define BUY(item, cost_lit, label_str) \
    &(OptionDesc){ \
        .name = label_str, \
        .description = "Cost: " #cost_lit " energy", \
        .kind = OPTKIND_ACTION, \
        .on_action = Buy, \
        .user_data = &(SpendEntry){ .item_id = item, .cost = cost_lit }, \
    }

#define CATEGORY(cat_name, desc_str, menu_ref) \
    &(OptionDesc){ \
        .name = cat_name, \
        .description = desc_str, \
        .kind = OPTKIND_MENU, \
        .menu_ptr = &menu_ref, \
    }

static MenuDesc stat_patches_menu = {
    .option_num = 10,
    .options = {
        BUY(AP_ITKIND_HP,       5,  "HP Patch"),
        BUY(AP_ITKIND_ACCEL,    5,  "Accel Patch"),
        BUY(AP_ITKIND_TOPSPEED, 5,  "Top Speed Patch"),
        BUY(AP_ITKIND_TURN,     5,  "Turn Patch"),
        BUY(AP_ITKIND_CHARGE,   5,  "Charge Patch"),
        BUY(AP_ITKIND_GLIDE,    5,  "Glide Patch"),
        BUY(AP_ITKIND_OFFENSE,  5,  "Offense Patch"),
        BUY(AP_ITKIND_DEFENSE,  5,  "Defense Patch"),
        BUY(AP_ITKIND_WEIGHT,   5,  "Weight Patch"),
        BUY(AP_ITKIND_ALLUP,    40, "All Up"),
    },
};

static MenuDesc permanent_patches_menu = {
    .option_num = 10,
    .options = {
        BUY(AP_PERM_PATCH_HP,        50,  "Perm HP"),
        BUY(AP_PERM_PATCH_ACCEL,     50,  "Perm Accel"),
        BUY(AP_PERM_PATCH_TOPSPEED,  50,  "Perm Top Speed"),
        BUY(AP_PERM_PATCH_TURN,      50,  "Perm Turn"),
        BUY(AP_PERM_PATCH_CHARGE,    50,  "Perm Charge"),
        BUY(AP_PERM_PATCH_GLIDE,     50,  "Perm Glide"),
        BUY(AP_PERM_PATCH_OFFENSE,   50,  "Perm Offense"),
        BUY(AP_PERM_PATCH_DEFENSE,   50,  "Perm Defense"),
        BUY(AP_PERM_PATCH_WEIGHT,    50,  "Perm Weight"),
        BUY(AP_ITEM_PERM_PATCH_ALL_UP, 400, "Perm All Up"),
    },
};

static MenuDesc copy_abilities_menu = {
    .option_num = 11,
    .options = {
        BUY(AP_ITKIND_COPYBOMB,    10, "Bomb"),
        BUY(AP_ITKIND_COPYFIRE,    10, "Fire"),
        BUY(AP_ITKIND_COPYICE,     10, "Ice"),
        BUY(AP_ITKIND_COPYSLEEP,   10, "Sleep"),
        BUY(AP_ITKIND_COPYTIRE,    10, "Wheel"),
        BUY(AP_ITKIND_COPYBIRD,    10, "Wing"),
        BUY(AP_ITKIND_COPYPLASMA,  10, "Plasma"),
        BUY(AP_ITKIND_COPYTORNADO, 10, "Tornado"),
        BUY(AP_ITKIND_COPYSWORD,   10, "Sword"),
        BUY(AP_ITKIND_COPYSPIKE,   10, "Needle"),
        BUY(AP_ITKIND_COPYMIC,     10, "Mike"),
    },
};

static MenuDesc food_menu = {
    .option_num = 12,
    .options = {
        BUY(AP_ITKIND_FOODMAXIMTOMATO, 15, "Maxim Tomato"),
        BUY(AP_ITKIND_FOODENERGYDRINK,  8, "Energy Drink"),
        BUY(AP_ITKIND_FOODICECREAM,     8, "Ice Cream"),
        BUY(AP_ITKIND_FOODRICEBALL,     6, "Rice Ball"),
        BUY(AP_ITKIND_FOODCHICKEN,      6, "Chicken"),
        BUY(AP_ITKIND_FOODCURRY,        6, "Curry"),
        BUY(AP_ITKIND_FOODRAMEN,        6, "Ramen"),
        BUY(AP_ITKIND_FOODOMELET,       6, "Omelet"),
        BUY(AP_ITKIND_FOODHAMBURGER,    6, "Hamburger"),
        BUY(AP_ITKIND_FOODSUSHI,        6, "Sushi"),
        BUY(AP_ITKIND_FOODHOTDOG,       6, "Hot Dog"),
        BUY(AP_ITKIND_FOODAPPLE,        4, "Apple"),
    },
};

static MenuDesc special_menu = {
    .option_num = 5,
    .options = {
        BUY(AP_ITKIND_CANDY,      20, "Candy"),
        BUY(AP_ITKIND_SPEEDMAX,   30, "Speed Max"),
        BUY(AP_ITKIND_OFFENSEMAX, 30, "Offense Max"),
        BUY(AP_ITKIND_DEFENSEMAX, 30, "Defense Max"),
        BUY(AP_ITKIND_CHARGEMAX,  30, "Charge Max"),
    },
};

static MenuDesc legendary_menu = {
    .option_num = 6,
    .options = {
        BUY(AP_ITKIND_DRAGOON1, 100, "Dragoon Part A"),
        BUY(AP_ITKIND_DRAGOON2, 100, "Dragoon Part B"),
        BUY(AP_ITKIND_DRAGOON3, 100, "Dragoon Part C"),
        BUY(AP_ITKIND_HYDRA1,   100, "Hydra Part S"),
        BUY(AP_ITKIND_HYDRA2,   100, "Hydra Part L"),
        BUY(AP_ITKIND_HYDRA3,   100, "Hydra Part R"),
    },
};

static MenuDesc checkbox_fillers_menu = {
    .option_num = 3,
    .options = {
        BUY(AP_ITEM_CHECKBOX_FILLER_AIRRIDE,   25, "Air Ride Filler"),
        BUY(AP_ITEM_CHECKBOX_FILLER_TOPRIDE,   25, "Top Ride Filler"),
        BUY(AP_ITEM_CHECKBOX_FILLER_CITYTRIAL, 25, "City Trial Filler"),
    },
};

static MenuDesc upgrades_menu = {
    .option_num = 1,
    .options = {
        BUY(AP_ITEM_PATCH_CAP_INCREASE, 75, "Patch Cap Increase"),
    },
};

MenuDesc energylink_spend_menu = {
    .option_num = 8,
    .options = {
        CATEGORY("Stat Patches",      "Temporary stat patches for this round",   stat_patches_menu),
        CATEGORY("Permanent Patches", "Permanent stat boosts across all rounds", permanent_patches_menu),
        CATEGORY("Copy Abilities",    "Give Kirby a copy ability",               copy_abilities_menu),
        CATEGORY("Food",              "Healing items",                           food_menu),
        CATEGORY("Special Items",     "Powerful one-use items",                  special_menu),
        CATEGORY("Legendary Pieces",  "Dragoon and Hydra machine parts",         legendary_menu),
        CATEGORY("Checkbox Fillers",  "Fill a random checklist square",          checkbox_fillers_menu),
        CATEGORY("Upgrades",          "Permanent progression upgrades",          upgrades_menu),
    },
};
