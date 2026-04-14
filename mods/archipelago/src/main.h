#ifndef ARCHIPELAGO_MAIN_H
#define ARCHIPELAGO_MAIN_H

#include "structs.h"
#include "event.h"
#include "custom_events_api.h"

#define MAX_RECEIVED_ITEMS 512

#define REWARD_COUNT_AIRRIDE   46
#define REWARD_COUNT_TOPRIDE   33
#define REWARD_COUNT_CITYTRIAL 44
#define REWARD_COUNT_MAX       REWARD_COUNT_AIRRIDE  // Largest per-mode reward count (46)

// Number of checkboxes per mode (clear_kind range 0..CLEAR_KIND_NUM-1).
#define CLEAR_KIND_NUM 120

#define PATCH_STAT_MAX 18

// AP Item ID Definitions
// These must match the item IDs defined in the APWorld Python code.
// ID 0 is reserved as the "empty" sentinel for the mailbox.
typedef enum APItemId
{
    // Standalone items (1-99)
    AP_ITEM_CHECKBOX_FILLER_AIRRIDE = 1,
    AP_ITEM_CHECKBOX_FILLER_TOPRIDE,
    AP_ITEM_CHECKBOX_FILLER_CITYTRIAL,
    AP_ITEM_PATCH_CAP_INCREASE,
    AP_ITEM_1_HP_TRAP,
    AP_ITEM_METEOR_TRAP,
    AP_ITEM_PERM_PATCH_ALL_UP,
    AP_ITEM_ALL_DOWN,
    AP_ITEM_GIVE_DRAGOON,
    AP_ITEM_GIVE_HYDRA,
    AP_ITEM_EVENT_CUSTOM,
    AP_ITEM_BOMB_TRAP,
    AP_ITEM_GORDO_TRAP,
    AP_ITEM_SENSORBOMB_TRAP,
    AP_ITEM_SPAWN_RATE_UP,

    // Permanent +1 patch items (100-199, aligned to PatchKind)
    AP_PERM_PATCH_BASE = 100,
    AP_PERM_PATCH_WEIGHT = 100,     // PATCHKIND_WEIGHT
    AP_PERM_PATCH_ACCEL,            // PATCHKIND_ACCEL
    AP_PERM_PATCH_TOPSPEED,         // PATCHKIND_TOPSPEED
    AP_PERM_PATCH_TURN,             // PATCHKIND_TURN
    AP_PERM_PATCH_CHARGE,           // PATCHKIND_CHARGE
    AP_PERM_PATCH_GLIDE,            // PATCHKIND_GLIDE
    AP_PERM_PATCH_OFFENSE,          // PATCHKIND_OFFENSE
    AP_PERM_PATCH_DEFENSE,          // PATCHKIND_DEFENSE
    AP_PERM_PATCH_HP,               // PATCHKIND_HP

    // City Trial events (200-299, aligned to EventKind)
    AP_EVENT_BASE = 200,
    AP_EVENT_DYNABLADE = 200,       // EVKIND_DYNABLADE
    AP_EVENT_TAC,                   // EVKIND_TAC
    AP_EVENT_METEOR,                // EVKIND_METEOR
    AP_EVENT_PILLAR,                // EVKIND_PILLAR
    AP_EVENT_RUNAMOK,               // EVKIND_RUNAMOK
    AP_EVENT_RESTORATIONAREA,       // EVKIND_RESTORATIONAREA
    AP_EVENT_RAILFIRE,              // EVKIND_RAILFIRE
    AP_EVENT_SAMEITEM,              // EVKIND_SAMEITEM
    AP_EVENT_LIGHTHOUSE,            // EVKIND_LIGHTHOUSE
    AP_EVENT_SECRETCHAMBER,         // EVKIND_SECRETCHAMBER
    AP_EVENT_PREDICTION,            // EVKIND_PREDICTION
    AP_EVENT_MACHINEFORMATION,      // EVKIND_MACHINEFORMATION
    AP_EVENT_UFO,                   // EVKIND_UFO
    AP_EVENT_BOUNCE,                // EVKIND_BOUNCE
    AP_EVENT_FOG,                   // EVKIND_FOG
    AP_EVENT_FAKEPOWERUPS,          // EVKIND_FAKEPOWERUPS

    // Direct game items (300+, aligned to ItemKind)
    AP_ITKIND_BASE = 300,
    AP_ITKIND_BOXBLUE = 300,        // ITKIND_BOXBLUE
    AP_ITKIND_BOXGREEN,             // ITKIND_BOXGREEN
    AP_ITKIND_BOXRED,               // ITKIND_BOXRED
    AP_ITKIND_ACCEL,                // ITKIND_ACCEL
    AP_ITKIND_ACCELDOWN,            // ITKIND_ACCELDOWN
    AP_ITKIND_TOPSPEED,             // ITKIND_TOPSPEED
    AP_ITKIND_TOPSPEEDDOWN,         // ITKIND_TOPSPEEDDOWN
    AP_ITKIND_OFFENSE,              // ITKIND_OFFENSE
    AP_ITKIND_OFFENSEDOWN,          // ITKIND_OFFENSEDOWN
    AP_ITKIND_DEFENSE,              // ITKIND_DEFENSE
    AP_ITKIND_DEFENSEDOWN,          // ITKIND_DEFENSEDOWN
    AP_ITKIND_TURN,                 // ITKIND_TURN
    AP_ITKIND_TURNDOWN,             // ITKIND_TURNDOWN
    AP_ITKIND_GLIDE,                // ITKIND_GLIDE
    AP_ITKIND_GLIDEDOWN,            // ITKIND_GLIDEDOWN
    AP_ITKIND_CHARGE,               // ITKIND_CHARGE
    AP_ITKIND_CHARGEDOWN,           // ITKIND_CHARGEDOWN
    AP_ITKIND_WEIGHT,               // ITKIND_WEIGHT
    AP_ITKIND_WEIGHTDOWN,           // ITKIND_WEIGHTDOWN
    AP_ITKIND_HP,                   // ITKIND_HP
    AP_ITKIND_ALLUP,                // ITKIND_ALLUP
    AP_ITKIND_SPEEDMAX,             // ITKIND_SPEEDMAX
    AP_ITKIND_SPEEDMIN,             // ITKIND_SPEEDMIN
    AP_ITKIND_OFFENSEMAX,           // ITKIND_OFFENSEMAX
    AP_ITKIND_DEFENSEMAX,           // ITKIND_DEFENSEMAX
    AP_ITKIND_CHARGEMAX,            // ITKIND_CHARGEMAX
    AP_ITKIND_CHARGENONE,           // ITKIND_CHARGENONE
    AP_ITKIND_CANDY,                // ITKIND_CANDY
    AP_ITKIND_COPYBOMB,             // ITKIND_COPYBOMB
    AP_ITKIND_COPYFIRE,             // ITKIND_COPYFIRE
    AP_ITKIND_COPYICE,              // ITKIND_COPYICE
    AP_ITKIND_COPYSLEEP,            // ITKIND_COPYSLEEP
    AP_ITKIND_COPYTIRE,             // ITKIND_COPYTIRE
    AP_ITKIND_COPYBIRD,             // ITKIND_COPYBIRD
    AP_ITKIND_COPYPLASMA,           // ITKIND_COPYPLASMA
    AP_ITKIND_COPYTORNADO,          // ITKIND_COPYTORNADO
    AP_ITKIND_COPYSWORD,            // ITKIND_COPYSWORD
    AP_ITKIND_COPYSPIKE,            // ITKIND_COPYSPIKE
    AP_ITKIND_COPYMIC,              // ITKIND_COPYMIC
    AP_ITKIND_FOODMAXIMTOMATO,      // ITKIND_FOODMAXIMTOMATO
    AP_ITKIND_FOODENERGYDRINK,      // ITKIND_FOODENERGYDRINK
    AP_ITKIND_FOODICECREAM,         // ITKIND_FOODICECREAM
    AP_ITKIND_FOODRICEBALL,         // ITKIND_FOODRICEBALL
    AP_ITKIND_FOODCHICKEN,          // ITKIND_FOODCHICKEN
    AP_ITKIND_FOODCURRY,            // ITKIND_FOODCURRY
    AP_ITKIND_FOODRAMEN,            // ITKIND_FOODRAMEN
    AP_ITKIND_FOODOMELET,           // ITKIND_FOODOMELET
    AP_ITKIND_FOODHAMBURGER,        // ITKIND_FOODHAMBURGER
    AP_ITKIND_FOODSUSHI,            // ITKIND_FOODSUSHI
    AP_ITKIND_FOODHOTDOG,           // ITKIND_FOODHOTDOG
    AP_ITKIND_FOODAPPLE,            // ITKIND_FOODAPPLE
    AP_ITKIND_FIREWORKS,            // ITKIND_FIREWORKS
    AP_ITKIND_PANICSPIN,            // ITKIND_PANICSPIN
    AP_ITKIND_TIMEBOMB,             // ITKIND_TIMEBOMB
    AP_ITKIND_GORDO,                // ITKIND_GORDO
    AP_ITKIND_HYDRA1,               // ITKIND_HYDRA1
    AP_ITKIND_HYDRA2,               // ITKIND_HYDRA2
    AP_ITKIND_HYDRA3,               // ITKIND_HYDRA3
    AP_ITKIND_DRAGOON1,             // ITKIND_DRAGOON1
    AP_ITKIND_DRAGOON2,             // ITKIND_DRAGOON2
    AP_ITKIND_DRAGOON3,             // ITKIND_DRAGOON3
    AP_ITKIND_ACCELFAKE,            // ITKIND_ACCELFAKE
    AP_ITKIND_TOPSPEEDFAKE,         // ITKIND_TOPSPEEDFAKE
    AP_ITKIND_OFFENSEFAKE,          // ITKIND_OFFENSEFAKE
    AP_ITKIND_DEFENSEFAKE,          // ITKIND_DEFENSEFAKE
    AP_ITKIND_TURNFAKE,             // ITKIND_TURNFAKE
    AP_ITKIND_GLIDEFAKE,            // ITKIND_GLIDEFAKE
    AP_ITKIND_CHARGEFAKE,           // ITKIND_CHARGEFAKE
    AP_ITKIND_WEIGHTFAKE,           // ITKIND_WEIGHTFAKE

    // Stadium unlock items (400-423, aligned to StadiumKind)
    AP_STADIUM_UNLOCK_BASE = 400,
    AP_STADIUM_UNLOCK_DRAG1 = 400,         // STKIND_DRAG1
    AP_STADIUM_UNLOCK_DRAG2,               // STKIND_DRAG2
    AP_STADIUM_UNLOCK_DRAG3,               // STKIND_DRAG3
    AP_STADIUM_UNLOCK_DRAG4,               // STKIND_DRAG4
    AP_STADIUM_UNLOCK_AIRGLIDER,           // STKIND_AIRGLIDER
    AP_STADIUM_UNLOCK_TARGETFLIGHT,        // STKIND_TARGETFLIGHT
    AP_STADIUM_UNLOCK_HIGHJUMP,            // STKIND_HIGHJUMP
    AP_STADIUM_UNLOCK_MELEE1,              // STKIND_MELEE1
    AP_STADIUM_UNLOCK_MELEE2,              // STKIND_MELEE2
    AP_STADIUM_UNLOCK_DESTRUCTION1,        // STKIND_DESTRUCTION1
    AP_STADIUM_UNLOCK_DESTRUCTION2,        // STKIND_DESTRUCTION2
    AP_STADIUM_UNLOCK_DESTRUCTION3,        // STKIND_DESTRUCTION3
    AP_STADIUM_UNLOCK_DESTRUCTION4,        // STKIND_DESTRUCTION4
    AP_STADIUM_UNLOCK_DESTRUCTION5,        // STKIND_DESTRUCTION5
    AP_STADIUM_UNLOCK_SINGLERACE1,         // STKIND_SINGLERACE1
    AP_STADIUM_UNLOCK_SINGLERACE2,         // STKIND_SINGLERACE2
    AP_STADIUM_UNLOCK_SINGLERACE3,         // STKIND_SINGLERACE3
    AP_STADIUM_UNLOCK_SINGLERACE4,         // STKIND_SINGLERACE4
    AP_STADIUM_UNLOCK_SINGLERACE5,         // STKIND_SINGLERACE5
    AP_STADIUM_UNLOCK_SINGLERACE6,         // STKIND_SINGLERACE6
    AP_STADIUM_UNLOCK_SINGLERACE7,         // STKIND_SINGLERACE7
    AP_STADIUM_UNLOCK_SINGLERACE8,         // STKIND_SINGLERACE8
    AP_STADIUM_UNLOCK_SINGLERACE9,         // STKIND_SINGLERACE9
    AP_STADIUM_UNLOCK_VSKINGDEDEDE,        // STKIND_VSKINGDEDEDE

    // Checklist rewards (500-649, encoded as base + mode*50 + reward_index)
    AP_CHECKLIST_REWARD_BASE = 500,

    // Air Ride rewards (500-545, mode 0, indices 0-45)
    AP_CHECKLIST_REWARD_AIRRIDE_BASE = 500,
    AP_REWARD_AR_COURSE_NEBULA_BELT = 500,             // New Course: Nebula Belt
    AP_REWARD_AR_MUSIC_NEBULA,                         // Music: Nebula
    AP_REWARD_AR_METAKNIGHT,                           // META KNIGHT: Available on normal courses
    AP_REWARD_AR_BONUS_MOVIE,                          // Bonus Movie: Special machine intros
    AP_REWARD_AR_DEDEDE,                               // KING DEDEDE: Available on normal courses
    AP_REWARD_AR_COLOR_GREEN,                          // Green Kirby
    AP_REWARD_AR_MACHINE_WAGON_STAR,                   // New Machine: WAGON STAR
    AP_REWARD_AR_SOUND_MAGMA_FLOWS,                    // Sound Test: MAGMA FLOWS
    AP_REWARD_AR_FILLER_1,                             // Check off an empty box of your choice!
    AP_REWARD_AR_MACHINE_REX_WHEELIE,                  // New Machine: REX WHEELIE
    AP_REWARD_AR_COLOR_PURPLE,                         // Purple Kirby
    AP_REWARD_AR_MACHINE_SLICK_STAR,                   // New Machine: SLICK STAR
    AP_REWARD_AR_ENDING,                               // Ending
    AP_REWARD_AR_COLOR_WHITE,                          // White Kirby
    AP_REWARD_AR_MACHINE_SWERVE_STAR,                  // New Machine: SWERVE STAR
    AP_REWARD_AR_MACHINE_SHADOW_STAR,                  // New Machine: SHADOW STAR
    AP_REWARD_AR_MACHINE_JET_STAR,                     // New Machine: JET STAR
    AP_REWARD_AR_MUSIC_HILLSIDE,                       // Music: Hillside
    AP_REWARD_AR_SOUND_CHECKER_KNIGHTS,                // Sound Test: CHECKER KNIGHTS
    AP_REWARD_AR_MUSIC_MEADOWS,                        // Music: Meadows
    AP_REWARD_AR_MACHINE_BULK_STAR,                    // New Machine: BULK STAR
    AP_REWARD_AR_SOUND_SKY_SANDS,                      // Sound Test: SKY SANDS
    AP_REWARD_AR_MACHINE_FORMULA_STAR,                 // New Machine: FORMULA STAR
    AP_REWARD_AR_MUSIC_MAGMA,                          // Music: Magma
    AP_REWARD_AR_MUSIC_BEANSTALK,                      // Music: Beanstalk
    AP_REWARD_AR_SOUND_MACHINE_PASSAGE,                // Sound Test: MACHINE PASSAGE
    AP_REWARD_AR_SOUND_FANTASY_MEADOWS,                // Sound Test: FANTASY MEADOWS
    AP_REWARD_AR_SOUND_CELESTIAL_VALLEY,               // Sound Test: CELESTIAL VALLEY
    AP_REWARD_AR_COLOR_BROWN,                          // Brown Kirby
    AP_REWARD_AR_SOUND_FROZEN_HILLSIDE,                // Sound Test: FROZEN HILLSIDE
    AP_REWARD_AR_SOUND_BEANSTALK_PARK,                 // Sound Test: BEANSTALK PARK
    AP_REWARD_AR_MACHINE_ROCKET_STAR,                  // New Machine: ROCKET STAR
    AP_REWARD_AR_SOUND_RESULTS_SCREEN,                 // Sound Test: Results Screen
    AP_REWARD_AR_MACHINE_WHEELIE_BIKE,                 // New Machine: WHEELIE BIKE
    AP_REWARD_AR_MACHINE_WHEELIE_SCOOTER,              // New Machine: WHEELIE SCOOTER
    AP_REWARD_AR_MACHINE_WINGED_STAR,                  // New Machine: WINGED STAR
    AP_REWARD_AR_FILLER_2,                             // Check off an empty box of your choice!
    AP_REWARD_AR_MUSIC_CHECKER,                        // Music: Checker
    AP_REWARD_AR_FILLER_3,                             // Check off an empty box of your choice!
    AP_REWARD_AR_MUSIC_SKY_SANDS,                      // Music: Sky Sands
    AP_REWARD_AR_MUSIC_MACHINE,                        // Music: Machine
    AP_REWARD_AR_MACHINE_TURBO_STAR,                   // New Machine: TURBO STAR
    AP_REWARD_AR_FILLER_4,                             // Check off an empty box of your choice!
    AP_REWARD_AR_MUSIC_CELESTIAL,                      // Music: Celestial
    AP_REWARD_AR_FILLER_5,                             // Check off an empty box of your choice!
    AP_REWARD_AR_SOUND_NEBULA_BELT,                    // Sound Test: NEBULA BELT

    // Top Ride rewards (550-582, mode 1, indices 0-32)
    AP_CHECKLIST_REWARD_TOPRIDE_BASE = 550,
    AP_REWARD_TR_COLOR_GREEN = 550,                    // Green Kirby
    AP_REWARD_TR_COLOR_PURPLE,                         // Purple Kirby
    AP_REWARD_TR_RULE_DIAGONAL_CAMERA,                 // Extra Rule: Diagonal Camera Angle
    AP_REWARD_TR_RULE_MYSTERY_ITEM_SET,                // Extra Rule: Mystery Item Set
    AP_REWARD_TR_ITEM_LANTERN,                         // New Item: Lantern
    AP_REWARD_TR_ITEM_WHO_PAINT,                       // New Item: Who? Paint
    AP_REWARD_TR_FILLER_1,                             // Check off an empty box of your choice!
    AP_REWARD_TR_ITEM_CHICKIE,                         // New Item: Chickie
    AP_REWARD_TR_SOUND_GRASS,                          // Sound Test: GRASS
    AP_REWARD_TR_MUSIC_GRASS,                          // Music: Grass
    AP_REWARD_TR_SOUND_SAND,                           // Sound Test: SAND
    AP_REWARD_TR_FILLER_2,                             // Check off an empty box of your choice!
    AP_REWARD_TR_COLOR_BROWN,                          // Brown Kirby
    AP_REWARD_TR_SOUND_SKY,                            // Sound Test: SKY
    AP_REWARD_TR_SOUND_FIRE,                           // Sound Test: FIRE
    AP_REWARD_TR_FILLER_3,                             // Check off an empty box of your choice!
    AP_REWARD_TR_MUSIC_FIRE,                           // Music: Fire
    AP_REWARD_TR_SOUND_WATER,                          // Sound Test: WATER
    AP_REWARD_TR_RULE_DEVICE_QUANTITY,                 // Extra Rule: Device Quantity
    AP_REWARD_TR_MUSIC_WATER,                          // Music: Water
    AP_REWARD_TR_SOUND_LIGHT,                          // Sound Test: LIGHT
    AP_REWARD_TR_FILLER_4,                             // Check off an empty box of your choice!
    AP_REWARD_TR_MUSIC_METAL,                          // Music: Metal
    AP_REWARD_TR_SOUND_METAL,                          // Sound Test: METAL
    AP_REWARD_TR_COLOR_WHITE,                          // White Kirby
    AP_REWARD_TR_FILLER_5,                             // Check off an empty box of your choice!
    AP_REWARD_TR_MUSIC_SAND,                           // Music: Sand
    AP_REWARD_TR_MUSIC_LIGHT,                          // Music: Light
    AP_REWARD_TR_RULE_ATTACK_ITEM_SET,                 // Extra Rule: Attack Item Set
    AP_REWARD_TR_SOUND_RESULTS_SCREEN,                 // Sound Test: Results Screen
    AP_REWARD_TR_MUSIC_SKY,                            // Music: Sky
    AP_REWARD_TR_RULE_SIDE_CAMERA,                     // Extra Rule: Side Camera Angle
    AP_REWARD_TR_ENDING,                               // Ending

    // City Trial rewards (600-643, mode 2, indices 0-43)
    AP_CHECKLIST_REWARD_CITYTRIAL_BASE = 600,
    AP_REWARD_CT_FILLER_1 = 600,                       // Check off an empty box of your choice!
    AP_REWARD_CT_SOUND_ITEM_BOUNCE,                    // Sound Test: Item bounce
    AP_REWARD_CT_BONUS_PAUSE_POWERUPS,                 // Bonus: Pause screen power-ups
    AP_REWARD_CT_MUSIC_CITY,                           // Music: City
    AP_REWARD_CT_SOUND_LEGENDARY_MACHINE,              // Sound Test: Legendary Air Ride Machine
    AP_REWARD_CT_SOUND_DENSE_FOG,                      // Sound Test: dense fog today
    AP_REWARD_CT_METAKNIGHT_FREERUN,                   // Meta Knight: Select in free run mode
    AP_REWARD_CT_SOUND_CITY_TRIAL,                     // Sound Test: City Trial
    AP_REWARD_CT_FILLER_2,                             // Check off an empty box of your choice!
    AP_REWARD_CT_STADIUM_SINGLERACE_NEBULA,            // New Stadium: Single Race (Nebula Belt)
    AP_REWARD_CT_SOUND_ROWDY_CHARGE_TANK,              // Sound Test: rowdy charge tank
    AP_REWARD_CT_STADIUM_DRAG_RACE_4,                  // New Stadium: Drag Race 4
    AP_REWARD_CT_SOUND_DRAG_RACE,                      // Sound Test: Drag Race
    AP_REWARD_CT_DRAGOON_PART_A,                       // Dragoon Part A
    AP_REWARD_CT_SOUND_TARGET_FLIGHT,                  // Sound Test: Target Flight
    AP_REWARD_CT_DRAGOON_PART_C,                       // Dragoon Part C
    AP_REWARD_CT_SOUND_AIR_GLIDER,                     // Sound Test: Air Glider
    AP_REWARD_CT_STADIUM_DESTRUCTION_4,                // New Stadium: Destruction Derby 4
    AP_REWARD_CT_FILLER_3,                             // Check off an empty box of your choice!
    AP_REWARD_CT_HYDRA_PART_Y,                         // Hydra Part Y
    AP_REWARD_CT_SOUND_WHATS_IN_THE_BOX,               // Sound Test: What's in the box?
    AP_REWARD_CT_HYDRA_PART_Z,                         // Hydra Part Z
    AP_REWARD_CT_DEDEDE_FREERUN,                       // King Dedede: Select in free run mode
    AP_REWARD_CT_SOUND_DYNA_BLADE_INTRO,               // Sound Test: Dyna Blade Intro
    AP_REWARD_CT_FILLER_4,                             // Check off an empty box of your choice!
    AP_REWARD_CT_SOUND_HUGE_PILLAR,                    // Sound Test: Huge Pillar
    AP_REWARD_CT_SOUND_TAC_CHALLENGE,                  // Sound Test: Tac Challenge
    AP_REWARD_CT_SOUND_FLYING_METEOR,                  // Sound Test: Flying Meteor
    AP_REWARD_CT_ENDING,                               // Ending
    AP_REWARD_CT_DRAGOON_PART_B,                       // Dragoon Part B
    AP_REWARD_CT_FILLER_5,                             // Check off an empty box of your choice!
    AP_REWARD_CT_HYDRA_PART_X,                         // Hydra Part X
    AP_REWARD_CT_COLOR_PURPLE,                         // Purple Kirby
    AP_REWARD_CT_STADIUM_DESTRUCTION_3,                // New Stadium: Destruction Derby 3
    AP_REWARD_CT_STADIUM_DESTRUCTION_5,                // New Stadium: Destruction Derby 5
    AP_REWARD_CT_STADIUM_KIRBY_MELEE_2,                // New Stadium: Kirby Melee 2
    AP_REWARD_CT_SOUND_KIRBY_MELEE,                    // Sound Test: Kirby Melee
    AP_REWARD_CT_COLOR_GREEN,                          // Green Kirby
    AP_REWARD_CT_COLOR_BROWN,                          // Brown Kirby
    AP_REWARD_CT_DRAGOON_FREERUN,                      // Dragoon: Select in Free Run mode
    AP_REWARD_CT_HYDRA_FREERUN,                        // Hydra: Select in Free Run mode
    AP_REWARD_CT_SOUND_LIGHTHOUSE,                     // Sound Test: The Lighthouse Light Burns
    AP_REWARD_CT_SOUND_STATION_FIRE,                   // Sound Test: Station Fire
    AP_REWARD_CT_COLOR_WHITE,                          // White Kirby

    // Event unlock items (700-715, aligned to EventKind)
    AP_EVENT_UNLOCK_BASE = 700,
    AP_EVENT_UNLOCK_DYNABLADE = 700,       // EVKIND_DYNABLADE
    AP_EVENT_UNLOCK_TAC,                   // EVKIND_TAC
    AP_EVENT_UNLOCK_METEOR,                // EVKIND_METEOR
    AP_EVENT_UNLOCK_PILLAR,                // EVKIND_PILLAR
    AP_EVENT_UNLOCK_RUNAMOK,               // EVKIND_RUNAMOK
    AP_EVENT_UNLOCK_RESTORATIONAREA,       // EVKIND_RESTORATIONAREA
    AP_EVENT_UNLOCK_RAILFIRE,              // EVKIND_RAILFIRE
    AP_EVENT_UNLOCK_SAMEITEM,              // EVKIND_SAMEITEM
    AP_EVENT_UNLOCK_LIGHTHOUSE,            // EVKIND_LIGHTHOUSE
    AP_EVENT_UNLOCK_SECRETCHAMBER,         // EVKIND_SECRETCHAMBER
    AP_EVENT_UNLOCK_PREDICTION,            // EVKIND_PREDICTION
    AP_EVENT_UNLOCK_MACHINEFORMATION,      // EVKIND_MACHINEFORMATION
    AP_EVENT_UNLOCK_UFO,                   // EVKIND_UFO
    AP_EVENT_UNLOCK_BOUNCE,                // EVKIND_BOUNCE
    AP_EVENT_UNLOCK_FOG,                   // EVKIND_FOG
    AP_EVENT_UNLOCK_FAKEPOWERUPS,          // EVKIND_FAKEPOWERUPS

    // Copy ability unlock items (760-770, aligned to CopyKind)
    AP_ABILITY_UNLOCK_BASE = 760,
    AP_ABILITY_UNLOCK_FIRE = 760,          // COPYKIND_FIRE
    AP_ABILITY_UNLOCK_WHEEL,               // COPYKIND_WHEEL
    AP_ABILITY_UNLOCK_SLEEP,               // COPYKIND_SLEEP
    AP_ABILITY_UNLOCK_SWORD,               // COPYKIND_SWORD
    AP_ABILITY_UNLOCK_BOMB,                // COPYKIND_BOMB
    AP_ABILITY_UNLOCK_PLASMA,              // COPYKIND_PLASMA
    AP_ABILITY_UNLOCK_NEEDLE,              // COPYKIND_NEEDLE
    AP_ABILITY_UNLOCK_MIC,                 // COPYKIND_MIC
    AP_ABILITY_UNLOCK_ICE,                 // COPYKIND_ICE
    AP_ABILITY_UNLOCK_TORNADO,             // COPYKIND_TORNADO
    AP_ABILITY_UNLOCK_BIRD,                // COPYKIND_BIRD

    // Patch type unlock items (780-788, aligned to PatchKind)
    AP_PATCH_UNLOCK_BASE = 780,
    AP_PATCH_UNLOCK_WEIGHT = 780,          // PATCHKIND_WEIGHT
    AP_PATCH_UNLOCK_ACCEL,                 // PATCHKIND_ACCEL
    AP_PATCH_UNLOCK_TOPSPEED,              // PATCHKIND_TOPSPEED
    AP_PATCH_UNLOCK_TURN,                  // PATCHKIND_TURN
    AP_PATCH_UNLOCK_CHARGE,                // PATCHKIND_CHARGE
    AP_PATCH_UNLOCK_GLIDE,                 // PATCHKIND_GLIDE
    AP_PATCH_UNLOCK_OFFENSE,               // PATCHKIND_OFFENSE
    AP_PATCH_UNLOCK_DEFENSE,               // PATCHKIND_DEFENSE
    AP_PATCH_UNLOCK_HP,                    // PATCHKIND_HP

    // Individual item unlock items (790-819, aligned to ItemUnlockKind)
    AP_ITEM_UNLOCK_BASE = 790,
    AP_ITEM_UNLOCK_ALLUP = 790,            // ITUNLOCK_ALLUP
    AP_ITEM_UNLOCK_SPEEDMAX,               // ITUNLOCK_SPEEDMAX
    AP_ITEM_UNLOCK_SPEEDMIN,               // ITUNLOCK_SPEEDMIN
    AP_ITEM_UNLOCK_OFFENSEMAX,             // ITUNLOCK_OFFENSEMAX
    AP_ITEM_UNLOCK_DEFENSEMAX,             // ITUNLOCK_DEFENSEMAX
    AP_ITEM_UNLOCK_CHARGEMAX,              // ITUNLOCK_CHARGEMAX
    AP_ITEM_UNLOCK_CHARGENONE,             // ITUNLOCK_CHARGENONE
    AP_ITEM_UNLOCK_CANDY,                  // ITUNLOCK_CANDY
    AP_ITEM_UNLOCK_FOODMAXIMTOMATO,        // ITUNLOCK_FOODMAXIMTOMATO
    AP_ITEM_UNLOCK_FOODENERGYDRINK,        // ITUNLOCK_FOODENERGYDRINK
    AP_ITEM_UNLOCK_FOODICECREAM,           // ITUNLOCK_FOODICECREAM
    AP_ITEM_UNLOCK_FOODRICEBALL,           // ITUNLOCK_FOODRICEBALL
    AP_ITEM_UNLOCK_FOODCHICKEN,            // ITUNLOCK_FOODCHICKEN
    AP_ITEM_UNLOCK_FOODCURRY,              // ITUNLOCK_FOODCURRY
    AP_ITEM_UNLOCK_FOODRAMEN,              // ITUNLOCK_FOODRAMEN
    AP_ITEM_UNLOCK_FOODOMELET,             // ITUNLOCK_FOODOMELET
    AP_ITEM_UNLOCK_FOODHAMBURGER,          // ITUNLOCK_FOODHAMBURGER
    AP_ITEM_UNLOCK_FOODSUSHI,              // ITUNLOCK_FOODSUSHI
    AP_ITEM_UNLOCK_FOODHOTDOG,             // ITUNLOCK_FOODHOTDOG
    AP_ITEM_UNLOCK_FOODAPPLE,              // ITUNLOCK_FOODAPPLE
    AP_ITEM_UNLOCK_FIREWORKS,              // ITUNLOCK_FIREWORKS
    AP_ITEM_UNLOCK_PANICSPIN,              // ITUNLOCK_PANICSPIN
    AP_ITEM_UNLOCK_TIMEBOMB,               // ITUNLOCK_TIMEBOMB
    AP_ITEM_UNLOCK_GORDO,                  // ITUNLOCK_GORDO
    AP_ITEM_UNLOCK_HYDRA1,                 // ITUNLOCK_HYDRA1
    AP_ITEM_UNLOCK_HYDRA2,                 // ITUNLOCK_HYDRA2
    AP_ITEM_UNLOCK_HYDRA3,                 // ITUNLOCK_HYDRA3
    AP_ITEM_UNLOCK_DRAGOON1,               // ITUNLOCK_DRAGOON1
    AP_ITEM_UNLOCK_DRAGOON2,               // ITUNLOCK_DRAGOON2
    AP_ITEM_UNLOCK_DRAGOON3,               // ITUNLOCK_DRAGOON3

    // Machine unlock items (830-855, aligned to MachineKind)
    AP_MACHINE_UNLOCK_BASE = 830,
    AP_MACHINE_UNLOCK_WARP = 830,          // VCKIND_WARP
    AP_MACHINE_UNLOCK_COMPACT,             // VCKIND_COMPACT
    AP_MACHINE_UNLOCK_WINGED,              // VCKIND_WINGED
    AP_MACHINE_UNLOCK_SHADOW,              // VCKIND_SHADOW
    AP_MACHINE_UNLOCK_HYDRA,               // VCKIND_HYDRA
    AP_MACHINE_UNLOCK_BULK,                // VCKIND_BULK
    AP_MACHINE_UNLOCK_SLICK,               // VCKIND_SLICK
    AP_MACHINE_UNLOCK_FORMULA,             // VCKIND_FORMULA
    AP_MACHINE_UNLOCK_DRAGOON,             // VCKIND_DRAGOON
    AP_MACHINE_UNLOCK_WAGON,               // VCKIND_WAGON
    AP_MACHINE_UNLOCK_ROCKET,              // VCKIND_ROCKET
    AP_MACHINE_UNLOCK_SWERVE,              // VCKIND_SWERVE
    AP_MACHINE_UNLOCK_TURBO,               // VCKIND_TURBO
    AP_MACHINE_UNLOCK_JET,                 // VCKIND_JET
    AP_MACHINE_UNLOCK_FLIGHT,              // VCKIND_FLIGHT
    AP_MACHINE_UNLOCK_FREE,                // VCKIND_FREE
    AP_MACHINE_UNLOCK_STEER,               // VCKIND_STEER
    AP_MACHINE_UNLOCK_WINGKIRBY,           // VCKIND_WINGKIRBY
    AP_MACHINE_UNLOCK_WINGMETAKNIGHT,      // VCKIND_WINGMETAKNIGHT
    AP_MACHINE_UNLOCK_WHEELNORMAL,         // VCKIND_WHEELNORMAL
    AP_MACHINE_UNLOCK_WHEELKIRBY,          // VCKIND_WHEELKIRBY
    AP_MACHINE_UNLOCK_WHEELIEBIKE,         // VCKIND_WHEELIEBIKE
    AP_MACHINE_UNLOCK_REXWHEELIE,          // VCKIND_REXWHEELIE
    AP_MACHINE_UNLOCK_WHEELIESCOOTER,      // VCKIND_WHEELIESCOOTER
    AP_MACHINE_UNLOCK_WHEELDEDEDE,         // VCKIND_WHEELDEDEDE
    AP_MACHINE_UNLOCK_WHEELVSDEDEDE,       // VCKIND_WHEELVSDEDEDE

    // Box type unlock items (860-862, aligned to BoxKind)
    AP_BOX_UNLOCK_BASE = 860,
    AP_BOX_UNLOCK_BLUE = 860,              // BOXKIND_BLUE
    AP_BOX_UNLOCK_GREEN,                   // BOXKIND_GREEN
    AP_BOX_UNLOCK_RED,                     // BOXKIND_RED

    // Air Ride stage unlock items (870-878, aligned to AirRideCourse)
    AP_STAGE_UNLOCK_AIRRIDE_BASE = 870,
    AP_STAGE_UNLOCK_FANTASY_MEADOWS = 870, // AIRRIDE_FANTASY_MEADOWS
    AP_STAGE_UNLOCK_MAGMA_FLOWS,           // AIRRIDE_MAGMA_FLOWS
    AP_STAGE_UNLOCK_SKY_SANDS,             // AIRRIDE_SKY_SANDS
    AP_STAGE_UNLOCK_FROZEN_HILLSIDE,       // AIRRIDE_FROZEN_HILLSIDE
    AP_STAGE_UNLOCK_BEANSTALK_PARK,        // AIRRIDE_BEANSTALK_PARK
    AP_STAGE_UNLOCK_CELESTIAL_VALLEY,      // AIRRIDE_CELESTIAL_VALLEY
    AP_STAGE_UNLOCK_MACHINE_PASSAGE,       // AIRRIDE_MACHINE_PASSAGE
    AP_STAGE_UNLOCK_CHECKER_KNIGHTS,       // AIRRIDE_CHECKER_KNIGHTS
    AP_STAGE_UNLOCK_NEBULA_BELT,           // AIRRIDE_NEBULA_BELT

    // Kirby color unlock items (880-887, aligned to KirbyColor)
    AP_COLOR_UNLOCK_BASE = 880,
    AP_COLOR_UNLOCK_PINK = 880,         // KIRBYCOLOR_PINK
    AP_COLOR_UNLOCK_YELLOW = 881,       // KIRBYCOLOR_YELLOW
    AP_COLOR_UNLOCK_BLUE,               // KIRBYCOLOR_BLUE
    AP_COLOR_UNLOCK_RED,                // KIRBYCOLOR_RED
    AP_COLOR_UNLOCK_GREEN,              // KIRBYCOLOR_GREEN
    AP_COLOR_UNLOCK_PURPLE,             // KIRBYCOLOR_PURPLE
    AP_COLOR_UNLOCK_BROWN,              // KIRBYCOLOR_BROWN
    AP_COLOR_UNLOCK_WHITE,              // KIRBYCOLOR_WHITE

    // Top Ride stage unlock items (890-896, aligned to TopRideCourse)
    AP_STAGE_UNLOCK_TOPRIDE_BASE = 890,
    AP_STAGE_UNLOCK_GRASS = 890,           // TOPRIDE_GRASS
    AP_STAGE_UNLOCK_SAND,                  // TOPRIDE_SAND
    AP_STAGE_UNLOCK_SKY,                   // TOPRIDE_SKY
    AP_STAGE_UNLOCK_FIRE,                  // TOPRIDE_FIRE
    AP_STAGE_UNLOCK_LIGHT,                 // TOPRIDE_LIGHT
    AP_STAGE_UNLOCK_WATER,                 // TOPRIDE_WATER
    AP_STAGE_UNLOCK_METAL,                 // TOPRIDE_METAL

    // Top Ride item unlock items (900-921, aligned to TopRideItemKind)
    AP_TOPRIDE_ITEM_UNLOCK_BASE = 900,
    AP_TOPRIDE_ITEM_UNLOCK_HAMMER = 900,   // TRITEM_HAMMER
    AP_TOPRIDE_ITEM_UNLOCK_GROW,           // TRITEM_GROW
    AP_TOPRIDE_ITEM_UNLOCK_SPEEDUP,        // TRITEM_SPEEDUP
    AP_TOPRIDE_ITEM_UNLOCK_SPEEDDOWN,      // TRITEM_SPEEDDOWN
    AP_TOPRIDE_ITEM_UNLOCK_BOOST_SAW,      // TRITEM_BOOST_SAW
    AP_TOPRIDE_ITEM_UNLOCK_CHARGEBOOST,    // TRITEM_CHARGEBOOST
    AP_TOPRIDE_ITEM_UNLOCK_INVINCIBLE,     // TRITEM_INVINCIBLE
    AP_TOPRIDE_ITEM_UNLOCK_BUZZSAW,        // TRITEM_BUZZSAW
    AP_TOPRIDE_ITEM_UNLOCK_SPEAR,          // TRITEM_SPEAR
    AP_TOPRIDE_ITEM_UNLOCK_FREEZE,         // TRITEM_FREEZE
    AP_TOPRIDE_ITEM_UNLOCK_MISSILE,        // TRITEM_MISSILE
    AP_TOPRIDE_ITEM_UNLOCK_FIRE,           // TRITEM_FIRE
    AP_TOPRIDE_ITEM_UNLOCK_NEEDLE,         // TRITEM_NEEDLE
    AP_TOPRIDE_ITEM_UNLOCK_BOMB,           // TRITEM_BOMB
    AP_TOPRIDE_ITEM_UNLOCK_LANDMINE,       // TRITEM_LANDMINE
    AP_TOPRIDE_ITEM_UNLOCK_SENSORBOMB,     // TRITEM_SENSORBOMB
    AP_TOPRIDE_ITEM_UNLOCK_MIKE,           // TRITEM_MIKE
    AP_TOPRIDE_ITEM_UNLOCK_CRACKER,        // TRITEM_CRACKER
    AP_TOPRIDE_ITEM_UNLOCK_METAKNIGHT,     // TRITEM_METAKNIGHT
    AP_TOPRIDE_ITEM_UNLOCK_SMOKESCREEN,    // TRITEM_SMOKESCREEN
    AP_TOPRIDE_ITEM_UNLOCK_DIZZY,          // TRITEM_DIZZY
    AP_TOPRIDE_ITEM_UNLOCK_BACKWARD,       // TRITEM_BACKWARD

} APItemId;

// AP Slot Options
// Written by the Python client to APData on connect, then copied
// to save data once. All fields are u32 for 4-byte alignment (atomic on PPC).

typedef enum APGoalKind
{
    GOAL_100_CHECKLIST = 0,     // Complete 100 checklist squares
    GOAL_N_CHECKLIST,           // Complete N checklist squares
    GOAL_HYDRA_AND_DRAGOON,     // City Trial only: assemble both legendary machines
    GOAL_BEAT_KING_DEDEDE,      // City Trial only: defeat King Dedede in stadium
    GOAL_NONE,                  // No goal for this mode
    GOAL_CHECKLIST_LIST,        // Complete all checkboxes specified in goal_checks[mode]
} APGoalKind;

typedef struct APSlotOptions
{
    // General
    u32 death_link_enabled;                // 0 or 1 — sets initial deathlink menu toggle
    u32 energy_link_enabled;               // 0 or 1 — sets initial energylink menu toggle
    u32 trap_link_enabled;                 // 0 or 1 — sets initial traplink menu toggle
    u32 reveal_checklists;                 // 0 or 1 — reveal all checklist squares

    // Per-mode goal settings (indexed by GameMode)
    u32 goal[GMMODE_NUM];                  // APGoalKind — completion condition per mode
    u32 checklist_amount[GMMODE_NUM];      // 1-120 — threshold for GOAL_N_CHECKLIST per mode

    // City Trial-specific
    u32 city_trial_progressive_patch_caps; // 0 or 1 — patch cap starts low, items raise it
    u32 city_trial_patch_cap_amount;       // 1-17 — starting cap when progressive is on
    u32 city_trial_progressive_stadiums;   // 0 or 1 — stadiums locked until items received

    // GOAL_CHECKLIST_LIST: bitmask of required checkboxes per mode.
    // Same encoding as sent_checks: bit (k % 64) of word (k / 64) for clear_kind k.
    // Only consulted when the mode's goal is GOAL_CHECKLIST_LIST.
    u64 goal_checks[GMMODE_NUM][2];
} APSlotOptions;

typedef struct APSave
{
    // Small critical fields first (ensure they fit within card save limits)
    uint boot_num;
    uint item_received_count;                           // Total items received from AP client
    uint unprocessed_count;                             // Number of items in the unprocessed list
    u32 stadium_unlocked_mask;                          // Bitmask of AP-unlocked stadiums (bit N = StadiumKind N)
    u32 event_unlocked_mask;                            // Bitmask of AP-unlocked events (bit N = EventKind N)
    u16 ability_unlocked_mask;                          // Bitmask of AP-unlocked copy abilities (bit N = CopyKind N)
    u8 box_unlocked_mask;                               // Bitmask of AP-unlocked box types (bit N = BoxKind N)
    u16 patch_unlocked_mask;                            // Bitmask of AP-unlocked patch types (bit N = PatchKind N)
    u32 item_unlocked_mask;                              // Bitmask of AP-unlocked items (bit N = ItemUnlockKind N)
    u32 machine_unlocked_mask;                          // Bitmask of AP-unlocked machines (bit N = MachineKind N)
    u16 airride_stage_unlocked_mask;                    // Bitmask of AP-unlocked Air Ride stages (bit N = StageKind N)
    u16 topride_stage_unlocked_mask;                    // Bitmask of AP-unlocked Top Ride courses (bit N = course N)
    u32 topride_item_unlocked_mask;                     // Bitmask of AP-unlocked Top Ride items (bit N = TopRideItemKind N)
    u8 color_unlocked_mask;                             // Bitmask of AP-unlocked Kirby colors (bit N = KirbyColor N)
    u8 patch_cap_count;                                 // Number of Patch Cap Increase items received
    u8 spawn_rate_level;                                // Number of Spawn Rate Up items received
    u8 permanent_patches[PATCHKIND_NUM];                // Accumulated permanent patch count per stat (0-18)
    u8 options_received;                                // Nonzero if AP slot options have been saved
    u16 shuffled_rewards[GMMODE_NUM][REWARD_COUNT_MAX];     // Saved location assignment per mode: (target_mode << 8) | clear_kind
    u64 received_checklist_rewards[3];                  // [GMMODE_NUM] bit N = reward_index N received for that mode
    u64 has_local_location[3];                          // [GMMODE_NUM] bit N = reward_index N is assigned to a local checkpoint
    // Authoritative record of which checkboxes the player has completed (in
    // gameplay or via filler placement). Indexed [mode][word], 2 u64s per mode
    // covers the 0..119 clear_kind range (bit (k%64) of word (k/64)).
    u64 sent_checks[3][2];
    u8 goal_complete;                                   // Sticky once set; persisted across boots
    APSlotOptions options;                              // AP slot options (copied from APData on first connect)
    // Large arrays last
    uint received_items[MAX_RECEIVED_ITEMS];            // Ordered list of all received AP item IDs
    uint unprocessed_items[MAX_RECEIVED_ITEMS];         // AP item IDs waiting to be applied
} APSave;

// Shared data struct stored at a static location in memory.
// The Python AP client reads/writes this via dolphin-memory-engine.
// All fields are 4-byte aligned. Reads/writes are atomic on PPC at this alignment.
//
// Field offsets are computed by the compiler; the protocol doc
// (docs/client-game-protocol.md) is the canonical reference for the Python
// client. Field order in this struct is the source of truth.
typedef struct APData
{
    float energy_balance;
    float energy_send;
    uint deathlink_receive;
    uint deathlink_send;
    uint traplink_receive;
    uint traplink_send;
    uint incoming_item_id;    // Mailbox: client writes AP item ID, game reads and clears to 0
    uint item_received_index; // Mirror of ap_save->item_received_count for client to read
    u32 game_ready;           // Game sets to 1 after save data is loaded and mod is initialized
    u32 options_valid;        // Client sets to 1 after writing all options fields
    APSlotOptions options;    // Slot options from AP server

    // Location assignment: which checklist slot each reward_index is placed at.
    // Written by the AP client once per session after the server reveals placement.
    // Encoding: (target_mode << 8) | clear_kind. target_mode selects which mode's
    // checklist the reward appears in (enables cross-mode reward shuffling).
    // 0xFFFF means the reward has no local slot (it will arrive from another world via the mailbox).
    u32 location_data_valid;                         // Client sets to 1; game clears after applying
    u16 locations[GMMODE_NUM][REWARD_COUNT_MAX];      // location per reward_index, indexed by mode

    // Check detection: mod-side authoritative record of completed checkboxes.
    // sent_checks mirrors ap_save->sent_checks; bit (k%64) of word (k/64)
    // is set when the player completes checkbox clear_kind k in mode m.
    // The Python client polls this and forwards new bits as AP location checks.
    u64 sent_checks[3][2];

    // Client backfill: AP client writes bits here to back-fill checks the
    // server already knows about (e.g., fresh-save / slot-takeover). Mod ORs
    // them into sent_checks each frame and clears this field. Additive only.
    u64 client_backfill[3][2];

    // Goal completion: mod sets to 1 when the player satisfies the active
    // goal. Sticky and persisted to save. Client reads and forwards victory.
    u8 goal_complete;
} APData;

// In-game menu toggle state. Bound to the Settings menu via OptionDesc.
// Initial values are set from APSlotOptions on first connect; the player
// can override them at any time via the Settings menu.
typedef struct APMenuSettings
{
    int deathlink_enabled;
    int energylink_enabled;
    int energylink_autocharge;
    int traplink_enabled;
    int textbox_enabled;
    int ct_permanent_patches_enabled;
    int ar_permanent_patches_enabled;
} APMenuSettings;

extern APData *ap_data;
extern APMenuSettings ap_menu_settings;
extern APSave *ap_save;
extern CustomEventsAPI *custom_events;

void OnBoot();
void OnSaveInit();
void OnSaveLoaded();
void OnMainMenuLoad();
void OnPlayerSelectLoad();
void On3DLoadStart();
void On3DLoadEnd();
void On3DPause(int pause_ply);
void On3DUnpause(int pause_ply);
void On3DExit();
void OnSceneChange();
void OnTopRideLoad();
void OnFrameStart();
void OnFrameEnd();

#endif // ARCHIPELAGO_MAIN_H
