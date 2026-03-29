#include "os.h"
#include "game.h"
#include "hoshi/settings.h"

#include "main.h"
#include "textbox.h"
#include "ap_item_handler.h"
#include "energylink.h"
#include "energylink_spend.h"

// Cost table for purchasable items.
// Each entry is an AP item ID and its energy cost.
typedef struct SpendEntry
{
    APItemId item_id;
    float cost;
    char *name;
} SpendEntry;

static SpendEntry stat_patches[] = {
    {AP_ITKIND_HP,           5, "HP Patch"},
    {AP_ITKIND_ACCEL,        5, "Accel Patch"},
    {AP_ITKIND_TOPSPEED,     5, "Top Speed Patch"},
    {AP_ITKIND_TURN,         5, "Turn Patch"},
    {AP_ITKIND_CHARGE,       5, "Charge Patch"},
    {AP_ITKIND_GLIDE,        5, "Glide Patch"},
    {AP_ITKIND_OFFENSE,      5, "Offense Patch"},
    {AP_ITKIND_DEFENSE,      5, "Defense Patch"},
    {AP_ITKIND_WEIGHT,       5, "Weight Patch"},
    {AP_ITEM_ALL_UP,        40, "All Up"},
};

static SpendEntry permanent_patches[] = {
    {AP_PERM_PATCH_HP,       50, "Perm HP"},
    {AP_PERM_PATCH_ACCEL,    50, "Perm Accel"},
    {AP_PERM_PATCH_TOPSPEED, 50, "Perm Top Speed"},
    {AP_PERM_PATCH_TURN,     50, "Perm Turn"},
    {AP_PERM_PATCH_CHARGE,   50, "Perm Charge"},
    {AP_PERM_PATCH_GLIDE,    50, "Perm Glide"},
    {AP_PERM_PATCH_OFFENSE,  50, "Perm Offense"},
    {AP_PERM_PATCH_DEFENSE,  50, "Perm Defense"},
    {AP_PERM_PATCH_WEIGHT,   50, "Perm Weight"},
    {AP_ITEM_PERM_PATCH_ALL_UP, 400, "Perm All Up"},
};

static SpendEntry copy_abilities[] = {
    {AP_ITKIND_COPYBOMB,     10, "Bomb"},
    {AP_ITKIND_COPYFIRE,     10, "Fire"},
    {AP_ITKIND_COPYICE,      10, "Ice"},
    {AP_ITKIND_COPYSLEEP,    10, "Sleep"},
    {AP_ITKIND_COPYTIRE,     10, "Wheel"},
    {AP_ITKIND_COPYBIRD,     10, "Wing"},
    {AP_ITKIND_COPYPLASMA,   10, "Plasma"},
    {AP_ITKIND_COPYTORNADO,  10, "Tornado"},
    {AP_ITKIND_COPYSWORD,    10, "Sword"},
    {AP_ITKIND_COPYSPIKE,    10, "Needle"},
    {AP_ITKIND_COPYMIC,      10, "Mike"},
};

static SpendEntry food_items[] = {
    {AP_ITKIND_FOODMAXIMTOMATO,  15, "Maxim Tomato"},
    {AP_ITKIND_FOODENERGYDRINK,   8, "Energy Drink"},
    {AP_ITKIND_FOODICECREAM,      8, "Ice Cream"},
    {AP_ITKIND_FOODRICEBALL,      6, "Rice Ball"},
    {AP_ITKIND_FOODCHICKEN,       6, "Chicken"},
    {AP_ITKIND_FOODCURRY,         6, "Curry"},
    {AP_ITKIND_FOODRAMEN,         6, "Ramen"},
    {AP_ITKIND_FOODOMELET,        6, "Omelet"},
    {AP_ITKIND_FOODHAMBURGER,     6, "Hamburger"},
    {AP_ITKIND_FOODSUSHI,         6, "Sushi"},
    {AP_ITKIND_FOODHOTDOG,        6, "Hot Dog"},
    {AP_ITKIND_FOODAPPLE,         4, "Apple"},
};

static SpendEntry special_items[] = {
    {AP_ITKIND_CANDY,       20, "Candy"},
    {AP_ITKIND_SPEEDMAX,    30, "Speed Max"},
    {AP_ITKIND_OFFENSEMAX,  30, "Offense Max"},
    {AP_ITKIND_DEFENSEMAX,  30, "Defense Max"},
    {AP_ITKIND_CHARGEMAX,   30, "Charge Max"},
};

static SpendEntry legendary_pieces[] = {
    {AP_ITKIND_DRAGOON1, 100, "Dragoon Part A"},
    {AP_ITKIND_DRAGOON2, 100, "Dragoon Part B"},
    {AP_ITKIND_DRAGOON3, 100, "Dragoon Part C"},
    {AP_ITKIND_HYDRA1,   100, "Hydra Part S"},
    {AP_ITKIND_HYDRA2,   100, "Hydra Part L"},
    {AP_ITKIND_HYDRA3,   100, "Hydra Part R"},
};

static SpendEntry checkbox_fillers[] = {
    {AP_ITEM_CHECKBOX_FILLER_AIRRIDE,   25, "Air Ride Filler"},
    {AP_ITEM_CHECKBOX_FILLER_TOPRIDE,   25, "Top Ride Filler"},
    {AP_ITEM_CHECKBOX_FILLER_CITYTRIAL, 25, "City Trial Filler"},
};

static SpendEntry upgrades[] = {
    {AP_ITEM_PATCH_CAP_INCREASE, 75, "Patch Cap Increase"},
    {AP_ITEM_AIRRIDE_SPEED_BOOST, 75, "Air Ride Speed Boost"},
};

static int TryPurchase(SpendEntry *entry)
{
    float balance = archipelago_data->energy_balance;
    if (balance < entry->cost)
    {
        TextBox_Enqueue("Not enough energy! Need %.0f, have %.0f", entry->cost, balance);
        return 0;
    }

    // Items that require being in a 3D scene may fail
    int result = APItems_HandleItem(entry->item_id);
    if (!result)
    {
        TextBox_Enqueue("Can't use that item right now!");
        return 0;
    }

    // Deduct from local balance and queue withdrawal through accumulator
    archipelago_data->energy_balance -= entry->cost;
    EnergyLink_Withdraw(entry->cost);

    TextBox_Enqueue("Bought %s for %.0f energy", entry->name, entry->cost);
    return 1;
}

// We need individual callbacks per entry since on_action takes no args.
// Use a macro to stamp them out per table.
#define BUY_FUNC(name, table, idx) \
    static int name(void) { return TryPurchase(&table[idx]); }

// Stat Patches
BUY_FUNC(Buy_StatPatch_0,  stat_patches, 0)
BUY_FUNC(Buy_StatPatch_1,  stat_patches, 1)
BUY_FUNC(Buy_StatPatch_2,  stat_patches, 2)
BUY_FUNC(Buy_StatPatch_3,  stat_patches, 3)
BUY_FUNC(Buy_StatPatch_4,  stat_patches, 4)
BUY_FUNC(Buy_StatPatch_5,  stat_patches, 5)
BUY_FUNC(Buy_StatPatch_6,  stat_patches, 6)
BUY_FUNC(Buy_StatPatch_7,  stat_patches, 7)
BUY_FUNC(Buy_StatPatch_8,  stat_patches, 8)
BUY_FUNC(Buy_StatPatch_9,  stat_patches, 9)

// Permanent Patches
BUY_FUNC(Buy_PermPatch_0,  permanent_patches, 0)
BUY_FUNC(Buy_PermPatch_1,  permanent_patches, 1)
BUY_FUNC(Buy_PermPatch_2,  permanent_patches, 2)
BUY_FUNC(Buy_PermPatch_3,  permanent_patches, 3)
BUY_FUNC(Buy_PermPatch_4,  permanent_patches, 4)
BUY_FUNC(Buy_PermPatch_5,  permanent_patches, 5)
BUY_FUNC(Buy_PermPatch_6,  permanent_patches, 6)
BUY_FUNC(Buy_PermPatch_7,  permanent_patches, 7)
BUY_FUNC(Buy_PermPatch_8,  permanent_patches, 8)
BUY_FUNC(Buy_PermPatch_9,  permanent_patches, 9)

// Copy Abilities
BUY_FUNC(Buy_Ability_0,  copy_abilities, 0)
BUY_FUNC(Buy_Ability_1,  copy_abilities, 1)
BUY_FUNC(Buy_Ability_2,  copy_abilities, 2)
BUY_FUNC(Buy_Ability_3,  copy_abilities, 3)
BUY_FUNC(Buy_Ability_4,  copy_abilities, 4)
BUY_FUNC(Buy_Ability_5,  copy_abilities, 5)
BUY_FUNC(Buy_Ability_6,  copy_abilities, 6)
BUY_FUNC(Buy_Ability_7,  copy_abilities, 7)
BUY_FUNC(Buy_Ability_8,  copy_abilities, 8)
BUY_FUNC(Buy_Ability_9,  copy_abilities, 9)
BUY_FUNC(Buy_Ability_10, copy_abilities, 10)

// Food
BUY_FUNC(Buy_Food_0,  food_items, 0)
BUY_FUNC(Buy_Food_1,  food_items, 1)
BUY_FUNC(Buy_Food_2,  food_items, 2)
BUY_FUNC(Buy_Food_3,  food_items, 3)
BUY_FUNC(Buy_Food_4,  food_items, 4)
BUY_FUNC(Buy_Food_5,  food_items, 5)
BUY_FUNC(Buy_Food_6,  food_items, 6)
BUY_FUNC(Buy_Food_7,  food_items, 7)
BUY_FUNC(Buy_Food_8,  food_items, 8)
BUY_FUNC(Buy_Food_9,  food_items, 9)
BUY_FUNC(Buy_Food_10, food_items, 10)
BUY_FUNC(Buy_Food_11, food_items, 11)

// Special
BUY_FUNC(Buy_Special_0, special_items, 0)
BUY_FUNC(Buy_Special_1, special_items, 1)
BUY_FUNC(Buy_Special_2, special_items, 2)
BUY_FUNC(Buy_Special_3, special_items, 3)
BUY_FUNC(Buy_Special_4, special_items, 4)

// Legendary Pieces
BUY_FUNC(Buy_Legend_0, legendary_pieces, 0)
BUY_FUNC(Buy_Legend_1, legendary_pieces, 1)
BUY_FUNC(Buy_Legend_2, legendary_pieces, 2)
BUY_FUNC(Buy_Legend_3, legendary_pieces, 3)
BUY_FUNC(Buy_Legend_4, legendary_pieces, 4)
BUY_FUNC(Buy_Legend_5, legendary_pieces, 5)

// Checkbox Fillers
BUY_FUNC(Buy_Filler_0, checkbox_fillers, 0)
BUY_FUNC(Buy_Filler_1, checkbox_fillers, 1)
BUY_FUNC(Buy_Filler_2, checkbox_fillers, 2)

// Upgrades
BUY_FUNC(Buy_Upgrade_0, upgrades, 0)
BUY_FUNC(Buy_Upgrade_1, upgrades, 1)

#define ACTION_OPT(entry_name, cost_str, buy_fn) \
    &(OptionDesc){                                \
        .name = entry_name,                       \
        .description = "Cost: " cost_str " energy", \
        .kind = OPTKIND_ACTION,                   \
        .on_action = buy_fn,                      \
    }

static MenuDesc stat_patches_menu = {
    .option_num = 10,
    .options = {
        ACTION_OPT("HP Patch",        "5",  Buy_StatPatch_0),
        ACTION_OPT("Accel Patch",     "5",  Buy_StatPatch_1),
        ACTION_OPT("Top Speed Patch", "5",  Buy_StatPatch_2),
        ACTION_OPT("Turn Patch",      "5",  Buy_StatPatch_3),
        ACTION_OPT("Charge Patch",    "5",  Buy_StatPatch_4),
        ACTION_OPT("Glide Patch",     "5",  Buy_StatPatch_5),
        ACTION_OPT("Offense Patch",   "5",  Buy_StatPatch_6),
        ACTION_OPT("Defense Patch",   "5",  Buy_StatPatch_7),
        ACTION_OPT("Weight Patch",    "5",  Buy_StatPatch_8),
        ACTION_OPT("All Up",          "40", Buy_StatPatch_9),
    },
};

static MenuDesc permanent_patches_menu = {
    .option_num = 10,
    .options = {
        ACTION_OPT("Perm HP",        "50",  Buy_PermPatch_0),
        ACTION_OPT("Perm Accel",     "50",  Buy_PermPatch_1),
        ACTION_OPT("Perm Top Speed", "50",  Buy_PermPatch_2),
        ACTION_OPT("Perm Turn",      "50",  Buy_PermPatch_3),
        ACTION_OPT("Perm Charge",    "50",  Buy_PermPatch_4),
        ACTION_OPT("Perm Glide",     "50",  Buy_PermPatch_5),
        ACTION_OPT("Perm Offense",   "50",  Buy_PermPatch_6),
        ACTION_OPT("Perm Defense",   "50",  Buy_PermPatch_7),
        ACTION_OPT("Perm Weight",    "50",  Buy_PermPatch_8),
        ACTION_OPT("Perm All Up",    "400", Buy_PermPatch_9),
    },
};

static MenuDesc copy_abilities_menu = {
    .option_num = 11,
    .options = {
        ACTION_OPT("Bomb",    "10", Buy_Ability_0),
        ACTION_OPT("Fire",    "10", Buy_Ability_1),
        ACTION_OPT("Ice",     "10", Buy_Ability_2),
        ACTION_OPT("Sleep",   "10", Buy_Ability_3),
        ACTION_OPT("Wheel",   "10", Buy_Ability_4),
        ACTION_OPT("Wing",    "10", Buy_Ability_5),
        ACTION_OPT("Plasma",  "10", Buy_Ability_6),
        ACTION_OPT("Tornado", "10", Buy_Ability_7),
        ACTION_OPT("Sword",   "10", Buy_Ability_8),
        ACTION_OPT("Needle",  "10", Buy_Ability_9),
        ACTION_OPT("Mike",    "10", Buy_Ability_10),
    },
};

static MenuDesc food_menu = {
    .option_num = 12,
    .options = {
        ACTION_OPT("Maxim Tomato",  "15", Buy_Food_0),
        ACTION_OPT("Energy Drink",  "8",  Buy_Food_1),
        ACTION_OPT("Ice Cream",     "8",  Buy_Food_2),
        ACTION_OPT("Rice Ball",     "6",  Buy_Food_3),
        ACTION_OPT("Chicken",       "6",  Buy_Food_4),
        ACTION_OPT("Curry",         "6",  Buy_Food_5),
        ACTION_OPT("Ramen",         "6",  Buy_Food_6),
        ACTION_OPT("Omelet",        "6",  Buy_Food_7),
        ACTION_OPT("Hamburger",     "6",  Buy_Food_8),
        ACTION_OPT("Sushi",         "6",  Buy_Food_9),
        ACTION_OPT("Hot Dog",       "6",  Buy_Food_10),
        ACTION_OPT("Apple",         "4",  Buy_Food_11),
    },
};

static MenuDesc special_menu = {
    .option_num = 5,
    .options = {
        ACTION_OPT("Candy",       "20", Buy_Special_0),
        ACTION_OPT("Speed Max",   "30", Buy_Special_1),
        ACTION_OPT("Offense Max", "30", Buy_Special_2),
        ACTION_OPT("Defense Max", "30", Buy_Special_3),
        ACTION_OPT("Charge Max",  "30", Buy_Special_4),
    },
};

static MenuDesc legendary_menu = {
    .option_num = 6,
    .options = {
        ACTION_OPT("Dragoon Part A", "100", Buy_Legend_0),
        ACTION_OPT("Dragoon Part B", "100", Buy_Legend_1),
        ACTION_OPT("Dragoon Part C", "100", Buy_Legend_2),
        ACTION_OPT("Hydra Part S",   "100", Buy_Legend_3),
        ACTION_OPT("Hydra Part L",   "100", Buy_Legend_4),
        ACTION_OPT("Hydra Part R",   "100", Buy_Legend_5),
    },
};

static MenuDesc checkbox_fillers_menu = {
    .option_num = 3,
    .options = {
        ACTION_OPT("Air Ride Filler",   "25", Buy_Filler_0),
        ACTION_OPT("Top Ride Filler",   "25", Buy_Filler_1),
        ACTION_OPT("City Trial Filler", "25", Buy_Filler_2),
    },
};

static MenuDesc upgrades_menu = {
    .option_num = 2,
    .options = {
        ACTION_OPT("Patch Cap Increase",  "75", Buy_Upgrade_0),
        ACTION_OPT("Air Ride Speed Boost", "75", Buy_Upgrade_1),
    },
};

#define CATEGORY(cat_name, desc_str, menu_ref) \
    &(OptionDesc){                             \
        .name = cat_name,                      \
        .description = desc_str,               \
        .kind = OPTKIND_MENU,                  \
        .menu_ptr = &menu_ref,                 \
    }

MenuDesc energylink_spend_menu = {
    .option_num = 8,
    .options = {
        CATEGORY("Stat Patches",      "Temporary stat patches for this round",       stat_patches_menu),
        CATEGORY("Permanent Patches", "Permanent stat boosts across all rounds",     permanent_patches_menu),
        CATEGORY("Copy Abilities",    "Give Kirby a copy ability",                   copy_abilities_menu),
        CATEGORY("Food",              "Healing items",                               food_menu),
        CATEGORY("Special Items",     "Powerful one-use items",                      special_menu),
        CATEGORY("Legendary Pieces",  "Dragoon and Hydra machine parts",             legendary_menu),
        CATEGORY("Checkbox Fillers",  "Fill a random checklist square",              checkbox_fillers_menu),
        CATEGORY("Upgrades",          "Permanent progression upgrades",              upgrades_menu),
    },
};
