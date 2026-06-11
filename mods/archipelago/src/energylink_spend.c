#include "os.h"
#include "game.h"
#include "inline.h"
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

    // Event-trigger items are gated by the player's event-unlock mask: an event
    // the player hasn't unlocked from AP can't be bought, so energy can't fire an
    // event out of logic. (item_id - AP_EVENT_BASE is the EventKind, matching the
    // give path in ap_item_handler.) Other categories have no such gate.
    if (entry->item_id >= AP_EVENT_BASE && entry->item_id < AP_EVENT_BASE + EVKIND_NUM)
    {
        int kind = entry->item_id - AP_EVENT_BASE;
        if (!(ap_save->event_unlocked_mask & (1 << kind)))
        {
            OSReport("[EnergyLink] Buy '%s' (id=%d) rejected: event not unlocked (mask = %s)\n",
                     self->name, entry->item_id, MaskBits(ap_save->event_unlocked_mask, EVKIND_NUM));
            tb_api->EnqueueColoredNoun("Event not unlocked: ", self->name, tb_api->EventColor, NULL);
            return 0;
        }
    }

    if (ap_data->energy_balance < entry->cost)
    {
        OSReport("[EnergyLink] Buy '%s' (id=%d) rejected: need %lld, have %lld\n",
                 self->name, entry->item_id, entry->cost, ap_data->energy_balance);
        tb_api->EnqueueColoredNounFmt("Not enough ", "energy", tb_api->EnergyColor,
                                      "! Need %lld, have %lld", entry->cost, ap_data->energy_balance);
        return 0;
    }

    // Push onto the unprocessed queue so APItems_PerFrame applies it when
    // the scene/intro gate allows - same path as items received from AP.
    if (ap_save->unprocessed_count >= MAX_RECEIVED_ITEMS)
    {
        OSReport("[EnergyLink] Buy '%s' (id=%d) rejected: queue full\n",
                 self->name, entry->item_id);
        tb_api->Enqueue("Queue full - try again later");
        return 0;
    }
    ap_save->unprocessed_items[ap_save->unprocessed_count++] = entry->item_id;

    // Withdraw the cost. Subtract from both the cumulative send counter (exact
    // integer - lands immediately, so the client diffs it on its next poll in
    // ANY scene with no flush; this is what makes menu purchases actually reach
    // the pool) and the local balance (instant UI feedback + keeps the gate
    // above honest). The next client set_notify push overwrites energy_balance
    // with the authoritative server view. Both are s64 -= s64, inline on PPC32
    // (no float, no libgcc).
    ap_data->energy_sent_total -= entry->cost;
    ap_data->energy_balance    -= entry->cost;

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
        BUY(AP_ITKIND_HP,       250,  "HP Patch"),
        BUY(AP_ITKIND_BOOST,    250,  "Boost Patch"),
        BUY(AP_ITKIND_TOPSPEED, 250,  "Top Speed Patch"),
        BUY(AP_ITKIND_TURN,     250,  "Turn Patch"),
        BUY(AP_ITKIND_CHARGE,   250,  "Charge Patch"),
        BUY(AP_ITKIND_GLIDE,    250,  "Glide Patch"),
        BUY(AP_ITKIND_OFFENSE,  250,  "Offense Patch"),
        BUY(AP_ITKIND_DEFENSE,  250,  "Defense Patch"),
        BUY(AP_ITKIND_WEIGHT,   250,  "Weight Patch"),
        BUY(AP_ITKIND_ALLUP,    2000, "All Up"),
    },
};

static MenuDesc permanent_patches_menu = {
    .option_num = 10,
    .options = {
        BUY(AP_PERM_PATCH_HP,        3500,  "Perm HP"),
        BUY(AP_PERM_PATCH_BOOST,     3500,  "Perm Boost"),
        BUY(AP_PERM_PATCH_TOPSPEED,  3500,  "Perm Top Speed"),
        BUY(AP_PERM_PATCH_TURN,      3500,  "Perm Turn"),
        BUY(AP_PERM_PATCH_CHARGE,    3500,  "Perm Charge"),
        BUY(AP_PERM_PATCH_GLIDE,     3500,  "Perm Glide"),
        BUY(AP_PERM_PATCH_OFFENSE,   3500,  "Perm Offense"),
        BUY(AP_PERM_PATCH_DEFENSE,   3500,  "Perm Defense"),
        BUY(AP_PERM_PATCH_WEIGHT,    3500,  "Perm Weight"),
        BUY(AP_ITEM_PERM_PATCH_ALL_UP, 25000, "Perm All Up"),
    },
};

static MenuDesc copy_abilities_menu = {
    .option_num = 11,
    .options = {
        BUY(AP_ITKIND_COPYBOMB,    600, "Bomb"),
        BUY(AP_ITKIND_COPYFIRE,    600, "Fire"),
        BUY(AP_ITKIND_COPYFREEZE,  600, "Freeze"),
        BUY(AP_ITKIND_COPYSLEEP,   600, "Sleep"),
        BUY(AP_ITKIND_COPYTIRE,    600, "Wheel"),
        BUY(AP_ITKIND_COPYBIRD,    600, "Wing"),
        BUY(AP_ITKIND_COPYPLASMA,  600, "Plasma"),
        BUY(AP_ITKIND_COPYTORNADO, 600, "Tornado"),
        BUY(AP_ITKIND_COPYSWORD,   600, "Sword"),
        BUY(AP_ITKIND_COPYSPIKE,   600, "Needle"),
        BUY(AP_ITKIND_COPYMIC,     600, "Mike"),
    },
};

static MenuDesc food_menu = {
    .option_num = 12,
    .options = {
        BUY(AP_ITKIND_FOODMAXIMTOMATO, 1000, "Maxim Tomato"),
        BUY(AP_ITKIND_FOODENERGYDRINK,  400, "Energy Drink"),
        BUY(AP_ITKIND_FOODICECREAM,     400, "Ice Cream"),
        BUY(AP_ITKIND_FOODRICEBALL,     300, "Rice Ball"),
        BUY(AP_ITKIND_FOODCHICKEN,      300, "Chicken"),
        BUY(AP_ITKIND_FOODCURRY,        300, "Curry"),
        BUY(AP_ITKIND_FOODRAMEN,        300, "Ramen"),
        BUY(AP_ITKIND_FOODOMELET,       300, "Omelet"),
        BUY(AP_ITKIND_FOODHAMBURGER,    300, "Hamburger"),
        BUY(AP_ITKIND_FOODSUSHI,        300, "Sushi"),
        BUY(AP_ITKIND_FOODHOTDOG,       300, "Hot Dog"),
        BUY(AP_ITKIND_FOODAPPLE,        200, "Apple"),
    },
};

static MenuDesc special_menu = {
    .option_num = 5,
    .options = {
        BUY(AP_ITKIND_CANDY,      1000, "Candy"),
        BUY(AP_ITKIND_SPEEDMAX,   2500, "Speed Max"),
        BUY(AP_ITKIND_OFFENSEMAX, 2500, "Offense Max"),
        BUY(AP_ITKIND_DEFENSEMAX, 2500, "Defense Max"),
        BUY(AP_ITKIND_CHARGEMAX,  2500, "Charge Max"),
    },
};

static MenuDesc legendary_menu = {
    .option_num = 8,
    .options = {
        BUY(AP_ITKIND_DRAGOON1,    5000, "Dragoon Part A"),
        BUY(AP_ITKIND_DRAGOON2,    5000, "Dragoon Part B"),
        BUY(AP_ITKIND_DRAGOON3,    5000, "Dragoon Part C"),
        BUY(AP_ITKIND_HYDRA1,      5000, "Hydra Part X"),
        BUY(AP_ITKIND_HYDRA2,      5000, "Hydra Part Y"),
        BUY(AP_ITKIND_HYDRA3,      5000, "Hydra Part Z"),
        BUY(AP_ITEM_GIVE_DRAGOON,  17500, "Full Dragoon"),
        BUY(AP_ITEM_GIVE_HYDRA,    17500, "Full Hydra"),
    },
};

static MenuDesc ct_items_menu = {
    .option_num = 7,
    .options = {
        BUY(AP_ITKIND_BOXBLUE,   1200, "Blue Box"),
        BUY(AP_ITKIND_BOXGREEN,  2000, "Green Box"),
        BUY(AP_ITKIND_BOXRED,    3200, "Red Box"),
        BUY(AP_ITKIND_FIREWORKS,  800, "Fireworks"),
        BUY(AP_ITKIND_SENSORBOMB, 1200, "Sensor Bomb"),
        BUY(AP_ITKIND_GORDO,     1600, "Gordo"),
        BUY(AP_ITKIND_PANICSPIN, 1200, "Panic Spin"),
    },
};

static MenuDesc ct_events_menu = {
    .option_num = 16,
    .options = {
        BUY(AP_EVENT_DYNABLADE,        2500, "Dyna Blade"),
        BUY(AP_EVENT_TAC,              2500, "Tac"),
        BUY(AP_EVENT_METEOR,           2500, "Meteor"),
        BUY(AP_EVENT_PILLAR,           2500, "Pillar"),
        BUY(AP_EVENT_RUNAMOK,          2500, "Run Amok"),
        BUY(AP_EVENT_RESTORATIONAREA,  2500, "Restoration Area"),
        BUY(AP_EVENT_RAILFIRE,         2500, "Rail Fire"),
        BUY(AP_EVENT_SAMEITEM,         2500, "Same Item"),
        BUY(AP_EVENT_LIGHTHOUSE,       2500, "Lighthouse"),
        BUY(AP_EVENT_SECRETCHAMBER,    2500, "Secret Chamber"),
        BUY(AP_EVENT_PREDICTION,       2500, "Prediction"),
        BUY(AP_EVENT_MACHINEFORMATION, 2500, "Machine Formation"),
        BUY(AP_EVENT_UFO,              2500, "UFO"),
        BUY(AP_EVENT_BOUNCE,           2500, "Bounce"),
        BUY(AP_EVENT_FOG,              2500, "Fog"),
        BUY(AP_EVENT_FAKEPOWERUPS,     2500, "Fake Powerups"),
    },
};

static MenuDesc topride_items_menu = {
    .option_num = 22,
    .options = {
        BUY(AP_TOPRIDE_ITEM_GIVE_HAMMER,           600, "Hammer"),
        BUY(AP_TOPRIDE_ITEM_GIVE_BIG_CAKE,         600, "Big Cake"),
        BUY(AP_TOPRIDE_ITEM_GIVE_SPEED_UP,         600, "Speed Up"),
        BUY(AP_TOPRIDE_ITEM_GIVE_SPINNER,          600, "Spinner"),
        BUY(AP_TOPRIDE_ITEM_GIVE_CHARGE_TANK,      600, "Charge Tank"),
        BUY(AP_TOPRIDE_ITEM_GIVE_INVINCIBLE_CANDY, 800, "Invincible Candy"),
        BUY(AP_TOPRIDE_ITEM_GIVE_BUZZ_SAW,         600, "Buzz Saw"),
        BUY(AP_TOPRIDE_ITEM_GIVE_DRILL,            600, "Drill"),
        BUY(AP_TOPRIDE_ITEM_GIVE_FREEZE_FAN,       600, "Freeze Fan"),
        BUY(AP_TOPRIDE_ITEM_GIVE_MISSILE,          600, "Missile"),
        BUY(AP_TOPRIDE_ITEM_GIVE_FIRE,             600, "Fire"),
        BUY(AP_TOPRIDE_ITEM_GIVE_PARTY_BALL_ALT,   600, "Party Ball (alt)"),
        BUY(AP_TOPRIDE_ITEM_GIVE_BOMB,             600, "Bomb"),
        BUY(AP_TOPRIDE_ITEM_GIVE_STEP_BOOM,        600, "Step-boom"),
        BUY(AP_TOPRIDE_ITEM_GIVE_LANTERN,          600, "Lantern"),
        BUY(AP_TOPRIDE_ITEM_GIVE_WALKY,            600, "Walky"),
        BUY(AP_TOPRIDE_ITEM_GIVE_KRACKO,           600, "Kracko"),
        BUY(AP_TOPRIDE_ITEM_GIVE_WHO_PAINT,        600, "Who? Paint"),
        BUY(AP_TOPRIDE_ITEM_GIVE_SMOKESCREEN,      600, "Smokescreen"),
        BUY(AP_TOPRIDE_ITEM_GIVE_CHICKIE,          600, "Chickie"),
        BUY(AP_TOPRIDE_ITEM_GIVE_SPEED_DOWN,       400, "Speed Down"),
        BUY(AP_TOPRIDE_ITEM_GIVE_PARTY_BALL,       400, "Party Ball"),
    },
};

static MenuDesc checkbox_fillers_menu = {
    .option_num = 3,
    .options = {
        BUY(AP_ITEM_CHECKBOX_FILLER_AIRRIDE,   50000, "Air Ride Filler"),
        BUY(AP_ITEM_CHECKBOX_FILLER_TOPRIDE,   50000, "Top Ride Filler"),
        BUY(AP_ITEM_CHECKBOX_FILLER_CITYTRIAL, 50000, "City Trial Filler"),
    },
};

static MenuDesc cosmetic_menu = {
    .option_num = 2,
    .options = {
        BUY(AP_ITEM_BIG_KIRBY,   500, "Big Kirby"),
        BUY(AP_ITEM_SMALL_KIRBY, 500, "Small Kirby"),
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
        CATEGORY("City Trial Items",  "Boxes and other CT items",                ct_items_menu),
        CATEGORY("City Trial Events", "Trigger a City Trial event",              ct_events_menu),
        CATEGORY("Top Ride Items",    "Spawn a Top Ride item at your position",  topride_items_menu),
        CATEGORY("Checkbox Fillers",  "Fill a checklist square of your choice",  checkbox_fillers_menu),
        CATEGORY("Cosmetic",          "Cosmetic items",                          cosmetic_menu),
    },
};
