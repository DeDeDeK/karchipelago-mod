#include "os.h"
#include "game.h"
#include "hoshi/settings.h"

#include "main.h"
#include "textbox_api.h"
#include "ap_item_handler.h"
#include "energylink.h"
#include "energylink_spend.h"

typedef struct SpendEntry
{
    APItemId item_id;
    s64 cost;
} SpendEntry;

static int Buy(OptionDesc *self)
{
    SpendEntry *entry = self->user_data;

    if (ap_data->energy_balance < entry->cost)
    {
        OSReport("[EnergyLink] Buy '%s' (id=%d) rejected: need %lld, have %lld\n",
                 self->name, entry->item_id, entry->cost, ap_data->energy_balance);
        tb_api->EnqueueColoredNounFmt("Not enough ", "energy", tb_api->EnergyColor,
                                      "! Need %lld, have %lld", entry->cost, ap_data->energy_balance);
        return 0;
    }

    // Push onto the unprocessed queue so APItems_PerFrame applies it when
    // the scene/intro gate allows — same path as items received from AP.
    if (ap_save->unprocessed_count >= MAX_RECEIVED_ITEMS)
    {
        OSReport("[EnergyLink] Buy '%s' (id=%d) rejected: queue full\n",
                 self->name, entry->item_id);
        tb_api->Enqueue("Queue full - try again later");
        return 0;
    }
    ap_save->unprocessed_items[ap_save->unprocessed_count++] = entry->item_id;

    // Local UI feedback: decrement balance immediately. The next client
    // SetReply push will overwrite with the authoritative server view.
    ap_data->energy_balance -= entry->cost;
    // Cast through s32 to avoid the s64→float libgcc call (__floatdisf).
    // Costs are small literals (≤ 600) so this is exact.
    EnergyLink_Withdraw((float)(s32)entry->cost);

    OSReport("[EnergyLink] Bought '%s' (id=%d) for %lld, balance %lld\n",
             self->name, entry->item_id, entry->cost, ap_data->energy_balance);
    tb_api->EnqueueColoredNounFmt("Bought ", self->name, tb_api->ShopColor,
                                  " for %lld energy", entry->cost);
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
        BUY(AP_ITKIND_ALLUP,    60, "All Up"),
    },
};

static MenuDesc permanent_patches_menu = {
    .option_num = 10,
    .options = {
        BUY(AP_PERM_PATCH_HP,        75,  "Perm HP"),
        BUY(AP_PERM_PATCH_ACCEL,     75,  "Perm Accel"),
        BUY(AP_PERM_PATCH_TOPSPEED,  75,  "Perm Top Speed"),
        BUY(AP_PERM_PATCH_TURN,      75,  "Perm Turn"),
        BUY(AP_PERM_PATCH_CHARGE,    75,  "Perm Charge"),
        BUY(AP_PERM_PATCH_GLIDE,     75,  "Perm Glide"),
        BUY(AP_PERM_PATCH_OFFENSE,   75,  "Perm Offense"),
        BUY(AP_PERM_PATCH_DEFENSE,   75,  "Perm Defense"),
        BUY(AP_PERM_PATCH_WEIGHT,    75,  "Perm Weight"),
        BUY(AP_ITEM_PERM_PATCH_ALL_UP, 600, "Perm All Up"),
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
        BUY(AP_ITKIND_FOODMAXIMTOMATO, 20, "Maxim Tomato"),
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
        BUY(AP_ITKIND_SPEEDMAX,   50, "Speed Max"),
        BUY(AP_ITKIND_OFFENSEMAX, 50, "Offense Max"),
        BUY(AP_ITKIND_DEFENSEMAX, 50, "Defense Max"),
        BUY(AP_ITKIND_CHARGEMAX,  50, "Charge Max"),
    },
};

static MenuDesc legendary_menu = {
    .option_num = 8,
    .options = {
        BUY(AP_ITKIND_DRAGOON1,    100, "Dragoon Part A"),
        BUY(AP_ITKIND_DRAGOON2,    100, "Dragoon Part B"),
        BUY(AP_ITKIND_DRAGOON3,    100, "Dragoon Part C"),
        BUY(AP_ITKIND_HYDRA1,      100, "Hydra Part X"),
        BUY(AP_ITKIND_HYDRA2,      100, "Hydra Part Y"),
        BUY(AP_ITKIND_HYDRA3,      100, "Hydra Part Z"),
        BUY(AP_ITEM_GIVE_DRAGOON,  350, "Full Dragoon"),
        BUY(AP_ITEM_GIVE_HYDRA,    350, "Full Hydra"),
    },
};

static MenuDesc ct_items_menu = {
    .option_num = 7,
    .options = {
        BUY(AP_ITKIND_BOXBLUE,   30, "Blue Box"),
        BUY(AP_ITKIND_BOXGREEN,  50, "Green Box"),
        BUY(AP_ITKIND_BOXRED,    80, "Red Box"),
        BUY(AP_ITKIND_FIREWORKS, 20, "Fireworks"),
        BUY(AP_ITKIND_SENSORBOMB, 30, "Sensor Bomb"),
        BUY(AP_ITKIND_GORDO,     40, "Gordo"),
        BUY(AP_ITKIND_PANICSPIN, 30, "Panic Spin"),
    },
};

static MenuDesc ct_events_menu = {
    .option_num = 16,
    .options = {
        BUY(AP_EVENT_DYNABLADE,        60, "Dyna Blade"),
        BUY(AP_EVENT_TAC,              60, "Tac"),
        BUY(AP_EVENT_METEOR,           60, "Meteor"),
        BUY(AP_EVENT_PILLAR,           60, "Pillar"),
        BUY(AP_EVENT_RUNAMOK,          60, "Run Amok"),
        BUY(AP_EVENT_RESTORATIONAREA,  60, "Restoration Area"),
        BUY(AP_EVENT_RAILFIRE,         60, "Rail Fire"),
        BUY(AP_EVENT_SAMEITEM,         60, "Same Item"),
        BUY(AP_EVENT_LIGHTHOUSE,       60, "Lighthouse"),
        BUY(AP_EVENT_SECRETCHAMBER,    60, "Secret Chamber"),
        BUY(AP_EVENT_PREDICTION,       60, "Prediction"),
        BUY(AP_EVENT_MACHINEFORMATION, 60, "Machine Formation"),
        BUY(AP_EVENT_UFO,              60, "UFO"),
        BUY(AP_EVENT_BOUNCE,           60, "Bounce"),
        BUY(AP_EVENT_FOG,              60, "Fog"),
        BUY(AP_EVENT_FAKEPOWERUPS,     60, "Fake Powerups"),
    },
};

static MenuDesc topride_items_menu = {
    .option_num = 22,
    .options = {
        BUY(AP_TOPRIDE_ITEM_GIVE_HAMMER,           15, "Hammer"),
        BUY(AP_TOPRIDE_ITEM_GIVE_BIG_CAKE,         15, "Big Cake"),
        BUY(AP_TOPRIDE_ITEM_GIVE_SPEED_UP,         15, "Speed Up"),
        BUY(AP_TOPRIDE_ITEM_GIVE_SPINNER,          15, "Spinner"),
        BUY(AP_TOPRIDE_ITEM_GIVE_CHARGE_TANK,      15, "Charge Tank"),
        BUY(AP_TOPRIDE_ITEM_GIVE_INVINCIBLE_CANDY, 20, "Invincible Candy"),
        BUY(AP_TOPRIDE_ITEM_GIVE_BUZZ_SAW,         15, "Buzz Saw"),
        BUY(AP_TOPRIDE_ITEM_GIVE_DRILL,            15, "Drill"),
        BUY(AP_TOPRIDE_ITEM_GIVE_FREEZE_FAN,       15, "Freeze Fan"),
        BUY(AP_TOPRIDE_ITEM_GIVE_MISSILE,          15, "Missile"),
        BUY(AP_TOPRIDE_ITEM_GIVE_FIRE,             15, "Fire"),
        BUY(AP_TOPRIDE_ITEM_GIVE_PARTY_BALL_ALT,   15, "Party Ball (alt)"),
        BUY(AP_TOPRIDE_ITEM_GIVE_BOMB,             15, "Bomb"),
        BUY(AP_TOPRIDE_ITEM_GIVE_STEP_BOOM,        15, "Step-boom"),
        BUY(AP_TOPRIDE_ITEM_GIVE_LANTERN,          15, "Lantern"),
        BUY(AP_TOPRIDE_ITEM_GIVE_WALKY,            15, "Walky"),
        BUY(AP_TOPRIDE_ITEM_GIVE_KRACKO,           15, "Kracko"),
        BUY(AP_TOPRIDE_ITEM_GIVE_WHO_PAINT,        15, "Who? Paint"),
        BUY(AP_TOPRIDE_ITEM_GIVE_SMOKESCREEN,      15, "Smokescreen"),
        BUY(AP_TOPRIDE_ITEM_GIVE_CHICKIE,          15, "Chickie"),
        BUY(AP_TOPRIDE_ITEM_GIVE_SPEED_DOWN,       10, "Speed Down"),
        BUY(AP_TOPRIDE_ITEM_GIVE_PARTY_BALL,       10, "Party Ball"),
    },
};

static MenuDesc checkbox_fillers_menu = {
    .option_num = 3,
    .options = {
        BUY(AP_ITEM_CHECKBOX_FILLER_AIRRIDE,   500, "Air Ride Filler"),
        BUY(AP_ITEM_CHECKBOX_FILLER_TOPRIDE,   500, "Top Ride Filler"),
        BUY(AP_ITEM_CHECKBOX_FILLER_CITYTRIAL, 500, "City Trial Filler"),
    },
};

static MenuDesc upgrades_menu = {
    .option_num = 2,
    .options = {
        BUY(AP_ITEM_PATCH_CAP_INCREASE, 75, "Patch Cap Increase"),
        BUY(AP_ITEM_SPAWN_RATE_UP,      75, "Spawn Rate Up"),
    },
};

MenuDesc energylink_spend_menu = {
    .option_num = 11,
    .options = {
        CATEGORY("Stat Patches",      "Temporary stat patches for this round",   stat_patches_menu),
        CATEGORY("Permanent Patches", "Permanent stat boosts across all rounds", permanent_patches_menu),
        CATEGORY("Copy Abilities",    "Give Kirby a copy ability",               copy_abilities_menu),
        CATEGORY("Food",              "Healing items",                           food_menu),
        CATEGORY("Special Items",     "Powerful one-use items",                  special_menu),
        CATEGORY("Legendary Pieces",  "Dragoon and Hydra machine parts",         legendary_menu),
        CATEGORY("City Trial Items",  "Boxes and standalone items",              ct_items_menu),
        CATEGORY("City Trial Events", "Trigger a City Trial event",              ct_events_menu),
        CATEGORY("Top Ride Items",    "Spawn a Top Ride item at your position",  topride_items_menu),
        CATEGORY("Checkbox Fillers",  "Fill a random checklist square",          checkbox_fillers_menu),
        CATEGORY("Upgrades",          "Permanent progression upgrades",          upgrades_menu),
    },
};
