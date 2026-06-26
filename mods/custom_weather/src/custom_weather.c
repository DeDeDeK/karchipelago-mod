#include <string.h>

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "code_patch/code_patch.h"
#include "hoshi/settings.h"

#include "custom_weather.h"

const CustomPresetDef custom_defs[WEATHER_CUSTOM_NUM] = {
    { .base_preset = WEATHER_NIGHT,
      .fog_color = RGBA(8, 20, 40, 255),
      .fog_start = 120.0f,
      .fog_end = 600.0f,
      .sky_color = RGBA(12, 30, 60, 190),
      .char_diffuse = RGBA(140, 150, 200, 255),
      .char_specular = RGBA(100, 120, 180, 255),
      .char_dir = { 0.00f, 1.00f, 0.00f },
      .char_dir_lit = 0,
      .fog_curve = FOG_CURVE_EXP2,
    },
    { .base_preset = WEATHER_DUSK,
      .fog_color = RGBA(200, 128, 48, 255),
      .fog_start = 280.0f,
      .fog_end = 850.0f,
      .sky_color = RGBA(224, 152, 56, 150),
      .char_diffuse = RGBA(240, 200, 140, 255),
      .char_specular = RGBA(255, 180, 90, 255),
      .char_dir = { -0.40f, 0.20f, 0.50f },
      .char_dir_lit = 1,
    },
    { .base_preset = WEATHER_RED_VIGNETTE,
      .fog_color = RGBA(96, 8, 8, 255),
      .fog_start = 80.0f,
      .fog_end = 450.0f,
      .sky_color = RGBA(128, 16, 16, 190),
      .char_diffuse = RGBA(200, 120, 100, 255),
      .char_specular = RGBA(220, 100, 80, 255),
      .char_dir = { 0.00f, 1.00f, 0.00f },
      .char_dir_lit = 1,
    },
    { .base_preset = WEATHER_DENSE_FOG,
      .fog_color = RGBA(224, 224, 232, 255),
      .fog_start = 20.0f,
      .fog_end = 150.0f,
      .sky_color = RGBA(208, 208, 224, 255),
      .char_diffuse = RGBA(220, 220, 240, 255),
      .char_specular = RGBA(230, 230, 250, 255),
      .char_dir = { 0.00f, 1.00f, 0.00f },
      .char_dir_lit = 0,
      .fog_curve = FOG_CURVE_EXP2,
      .rain = {
          .enabled = 1,
          .color = RGBA(255, 255, 255, 220),          
          .density = 1200,
          .fall_speed = 8.0f,                         
          .line_width = 30,                           
          .streak = 0.3f,                             
      },
      .wind = {
          .enabled = 1,
          .speed = 4.5f,
          .heading = 60.0f,
          .gustiness = 0.6f,
          .chaos = 0.5f,
      },
    },
    { .base_preset = WEATHER_DARK_VIGNETTE,
      .fog_color = RGBA(16, 48, 16, 255),
      .fog_start = 120.0f,
      .fog_end = 500.0f,
      .sky_color = RGBA(24, 72, 24, 170),
      .char_diffuse = RGBA(140, 180, 130, 255),
      .char_specular = RGBA(120, 170, 110, 255),
      .char_dir = { -0.40f, 0.80f, 0.50f },
      .char_dir_lit = 0,
    },
    { .base_preset = WEATHER_DARK_PURPLE,
      .fog_color = RGBA(128, 32, 192, 255),
      .fog_start = 450.0f,
      .fog_end = 1200.0f,
      .sky_color = RGBA(32, 8, 64, 150),
      .char_diffuse = RGBA(200, 160, 220, 255),
      .char_specular = RGBA(240, 180, 255, 255),
      .char_dir = { -0.40f, 0.80f, 0.50f },
      .char_dir_lit = 0,
    },
    { .base_preset = WEATHER_PINK_SKY,
      .fog_color = RGBA(0, 160, 160, 255),
      .fog_start = 200.0f,
      .fog_end = 900.0f,
      .sky_color = RGBA(255, 140, 200, 120),
      .char_diffuse = RGBA(240, 210, 230, 255),
      .char_specular = RGBA(255, 200, 240, 255),
      .char_dir = { -0.40f, 0.80f, 0.50f },
      .char_dir_lit = 1,
    },
    { .base_preset = WEATHER_BLUE_SKY,
      .fog_color = RGBA(40, 60, 140, 255),
      .fog_start = 250.0f,
      .fog_end = 900.0f,
      .sky_color = RGBA(220, 200, 80, 140),
      .char_diffuse = RGBA(190, 200, 240, 255),
      .char_specular = RGBA(210, 220, 255, 255),
      .char_dir = { -0.40f, 0.80f, 0.50f },
      .char_dir_lit = 1,
    },
    { .base_preset = WEATHER_NIGHT,
      .fog_color = RGBA(200, 200, 210, 255),
      .fog_start = 150.0f,
      .fog_end = 700.0f,
      .sky_color = RGBA(0, 0, 0, 200),
      .char_diffuse = RGBA(100, 100, 110, 255),
      .char_specular = RGBA(80, 80, 90, 255),
      .char_dir = { 0.00f, -1.00f, 0.00f },
      .char_dir_lit = 0,
    },
    { .base_preset = WEATHER_DARK_VIGNETTE,
      .fog_color = RGBA(14, 18, 28, 255),
      .fog_start = 10.0f,
      .fog_end = 220.0f,
      .sky_color = RGBA(20, 24, 36, 220),
      .terrain_diffuse = RGBA(55, 65, 85, 255),       
      .terrain_specular = RGBA(45, 55, 75, 255),
      .char_diffuse = RGBA(80, 90, 110, 255),
      .char_specular = RGBA(60, 70, 95, 255),
      .char_dir = { 0.00f, 1.00f, 0.00f },
      .char_dir_lit = 0,
      .char_ambient = RGBA(35, 40, 60, 255),
      .char_ambient_specular = RGBA(30, 35, 50, 255),
      .screen_tint = RGBA(0, 0, 8, 110),              
      .lightning = {
          .enabled = 1,
          .flash_color = RGBA(255, 250, 240, 255),
      },
      .rain = {
          .enabled = 1,
          .color = RGBA(170, 185, 205, 150),          
          .density = 1300,
          .fall_speed = 38.0f,                        
          .line_width = 7,                            
          .streak = 1.0f,                             
      },
      .wind = {
          .enabled = 1,
          .speed = 9.0f,
          .heading = 90.0f,
          .gustiness = 0.6f,
          .chaos = 0.5f,
      },
    },
    { .base_preset = WEATHER_GRAY_SKY,
      .fog_color = RGBA(58, 68, 84, 255),
      .fog_start = 140.0f,
      .fog_end = 850.0f,
      .sky_color = RGBA(92, 102, 118, 170),
      .char_diffuse = RGBA(150, 160, 180, 255),
      .char_specular = RGBA(140, 152, 175, 255),
      .char_dir = { -0.20f, 0.85f, 0.40f },
      .char_dir_lit = 0,
      .char_ambient = RGBA(95, 105, 125, 255),
      .screen_tint = RGBA(18, 24, 36, 70),            
      .rain = {
          .enabled = 1,
          .color = RGBA(165, 180, 215, 120),          
          .density = 900,
          .fall_speed = 26.0f,
      },
      .wind = {
          .enabled = 1,
          .speed = 3.0f,
          .heading = 90.0f,
          .gustiness = 0.2f,
          .chaos = 0.15f,
      },
    },
    { .base_preset = WEATHER_RED_VIGNETTE,
      .fog_color = RGBA(130, 36, 14, 255),
      .fog_start = 60.0f,
      .fog_end = 480.0f,
      .sky_color = RGBA(150, 44, 18, 185),
      .char_diffuse = RGBA(225, 125, 80, 255),
      .char_specular = RGBA(245, 110, 70, 255),
      .char_dir = { -0.30f, 0.80f, 0.40f },
      .char_dir_lit = 1,
      .char_ambient = RGBA(90, 35, 25, 255),
      .screen_tint = RGBA(36, 6, 4, 85),              
      .rain = {
          .enabled = 1,
          .color = RGBA(210, 45, 30, 155),            
          .density = 1100,
          .fall_speed = 28.0f,
      },
      .lightning = {
          .enabled = 1,
          .flash_color = RGBA(255, 80, 50, 255),
      },
    },
    { .base_preset = WEATHER_GRAY_SKY,
      .fog_color = RGBA(64, 74, 90, 255),
      .fog_start = 320.0f,                            
      .fog_end = 1300.0f,
      .sky_color = RGBA(88, 98, 114, 160),
      .char_diffuse = RGBA(138, 146, 164, 255),
      .char_specular = RGBA(128, 138, 158, 255),
      .char_dir = { -0.20f, 0.85f, 0.40f },
      .char_dir_lit = 0,
      .char_ambient = RGBA(86, 94, 110, 255),
      .screen_tint = RGBA(14, 20, 30, 95),            
      .rain = {
          .enabled = 1,
          .color = RGBA(150, 165, 195, 100),          
          .density = 450,
          .fall_speed = 20.0f,
          .line_width = 6,
          .streak = 1.2f,
      },
      .wind = {
          .enabled = 1,
          .speed = 2.0f,
          .heading = 70.0f,
          .gustiness = 0.2f,
          .chaos = 0.2f,
      },
      .puddles = {
          .enabled = 1,
          .color = RGBA(150, 178, 205, 195),          
          .count = 28,
          .radius = 32.0f,
          .slow_factor = 0.90f,                       
      },
    },
};

const CustomPresetDef *CustomWeather_GetPresetDef(int weather_kind)
{
    if (weather_kind < WEATHER_VANILLA_NUM || weather_kind >= WEATHER_TOTAL)
        return 0;
    return &custom_defs[weather_kind - WEATHER_VANILLA_NUM];
}

static const char *preset_names[WEATHER_TOTAL] = {
    "Day", "Midnight", "Light Fog", "Dusk 2", "Dusky Clouds",
    "Dark Vignette", "Day 2", "Blue Sky", "Pink Sky", "Dense Fog",
    "Foggy", "Dusk", "Night", "Gray Sky", "Dark Purple",
    "Red Vignette", "Dark Low Vis",
    "Deep Blue", "Golden Hour", "Blood Red", "Whiteout",
    "Toxic Green", "Neon", "Cotton Candy", "Frozen Dawn", "Void",
    "Storm", "Rain", "Blood Rain", "Puddles",
};

const char *CustomWeather_GetPresetName(int weather_kind)
{
    if (weather_kind < 0 || weather_kind >= WEATHER_TOTAL)
        return "Unknown";
    return preset_names[weather_kind];
}

// Extended preset buffer: vanilla entries copied from stage file + custom appended.
static SkyPresetEntry extended_presets[WEATHER_TOTAL];

// Per-preset enabled toggle. 1 = Enabled, 0 = Disabled. Default: all enabled.
// Each entry is persisted by hoshi menu save (keyed by option name hash).
static int weather_enabled[WEATHER_TOTAL] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};
_Static_assert(sizeof(weather_enabled) / sizeof(weather_enabled[0]) == WEATHER_TOTAL,
               "weather_enabled init must match WEATHER_TOTAL");

static char *toggle_names[] = {"Disabled", "Enabled"};

// Global "Fog Distance" multiplier written into HSD_Fog.scale every frame by
// the anim runtime. Index into fog_distance_factors; the engine emits
// GXSetFog(..., end * scale, ...), so <1 pulls the fog wall in (denser, closer)
// and >1 pushes it out (clearer). Default 100% = no change. Persisted by hoshi
// menu save (keyed by option name).
static const float fog_distance_factors[] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
static char *fog_distance_names[] = {"50%", "75%", "100%", "125%", "150%", "200%"};
#define FOG_DISTANCE_NUM (sizeof(fog_distance_factors) / sizeof(fog_distance_factors[0]))
static int fog_distance_index = 2; // default 100%

float CustomWeather_GetFogScale(void)
{
    return fog_distance_factors[fog_distance_index];
}

// Copy vanilla presets into our static buffer, append custom presets,
// and repoint the game's sub-header to use the extended array.
// Safe to call each stage load (idempotent).
static void ExtendPresetArray(GrObj *grobj)
{
    // grobj->gr_data->sky_block->preset_header holds {preset_array, preset_count}.
    SkyBlock *sky_block = grobj->gr_data->sky_block;
    SkyPresetSubHeader *sub_header = sky_block->preset_header;
    SkyPresetEntry *vanilla_array = sub_header->preset_array;

    // Copy all vanilla presets
    memcpy(extended_presets, vanilla_array,
           WEATHER_VANILLA_NUM * sizeof(SkyPresetEntry));

    // Build each custom preset by cloning its base then overriding colors/fog/light
    for (int i = 0; i < WEATHER_CUSTOM_NUM; i++)
    {
        const CustomPresetDef *def = &custom_defs[i];
        SkyPresetEntry *entry = &extended_presets[WEATHER_VANILLA_NUM + i];

        // Clone base (inherits non-overridden area light params: flags, attn, header)
        *entry = extended_presets[def->base_preset];

        entry->fog_color = def->fog_color;
        entry->fog_start = def->fog_start;
        entry->fog_end = def->fog_end;
        entry->sky_ambient_color = def->sky_color;
        entry->fade_color = 0;          // we drive screen tint ourselves via screen_tint + Sky_BeginFade
        entry->area_light.color = def->char_diffuse;
        entry->area_light.hw_color = def->char_specular;
        entry->area_light.direction = def->char_dir;
        entry->light_vis_flag = (u8)def->char_dir_lit;
        entry->transition_frames = 1;
    }

    // Repoint game data to our extended array
    sub_header->preset_array = extended_presets;
    sub_header->preset_count = WEATHER_TOTAL;
}

// Replaces vanilla random/fixed sky selection.
// Extends the preset array, then picks uniformly from enabled presets.
static void CustomWeather_OverrideSky(GrObj *grobj)
{
    ExtendPresetArray(grobj);

    // Count enabled presets
    int enabled_count = 0;
    for (int i = 0; i < WEATHER_TOTAL; i++)
    {
        if (weather_enabled[i])
            enabled_count++;
    }

    // Pick random from enabled set; fall back to Day if none enabled
    int preset = WEATHER_DAY;
    if (enabled_count > 0)
    {
        int pick = HSD_Randi(enabled_count);
        for (int i = 0; i < WEATHER_TOTAL; i++)
        {
            if (weather_enabled[i])
            {
                if (pick == 0)
                {
                    preset = i;
                    break;
                }
                pick--;
            }
        }
    }

    OSReport("[CustomWeather] Selected preset %d: %s (%d/%d enabled)\n",
             preset, CustomWeather_GetPresetName(preset), enabled_count, WEATHER_TOTAL);

    Sky_SetPresetIndex(grobj, preset);
}

// Hook at 0x8010f1a4 (inside Sky_Init): City Trial (stage kind 9) random selection block.
// r30 = grobj (the extended stage object). Exits past vanilla setSkyIndex.
CODEPATCH_HOOKCREATE(0x8010f1a4,
    "mr 3, 30\n\t",
    CustomWeather_OverrideSky,
    "", 0x8010f1d0);

// Hook at 0x8010f224 (inside Sky_Init): City Trial Free Run (stage kind 52) sky init.
// Vanilla hardcodes preset 0. Same r30 = grobj.
CODEPATCH_HOOKCREATE(0x8010f224,
    "mr 3, 30\n\t",
    CustomWeather_OverrideSky,
    "", 0x8010f230);

void CustomWeather_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x8010f1a4);
    CODEPATCH_HOOKAPPLY(0x8010f224);
    OSReport("[CustomWeather] Hooks installed (City Trial + City Trial Free Run)\n");
}

static int EnableAllWeather(OptionDesc *self)
{
    (void)self;
    for (int i = 0; i < WEATHER_TOTAL; i++)
        weather_enabled[i] = 1;
    return 1;
}

static int DisableAllWeather(OptionDesc *self)
{
    (void)self;
    for (int i = 0; i < WEATHER_TOTAL; i++)
        weather_enabled[i] = 0;
    return 1;
}

#define WEATHER_TOGGLE(idx, label) \
    &(OptionDesc){ \
        .name = label, \
        .kind = OPTKIND_VALUE, \
        .val = &weather_enabled[idx], \
        .value_num = 2, \
        .value_names = toggle_names, \
    }

MenuDesc weather_menu = {
    .option_num = WEATHER_TOTAL + 3,
    .options = {
        &(OptionDesc){
            .name = "Fog Distance",
            .description = "Scale how far the fog wall sits in every CT preset (lower = denser/closer)",
            .kind = OPTKIND_VALUE,
            .val = &fog_distance_index,
            .value_num = FOG_DISTANCE_NUM,
            .value_names = fog_distance_names,
        },
        &(OptionDesc){
            .name = "Enable All",
            .description = "Enable all weather presets",
            .kind = OPTKIND_ACTION,
            .on_action = EnableAllWeather,
        },
        &(OptionDesc){
            .name = "Disable All",
            .description = "Disable all weather presets",
            .kind = OPTKIND_ACTION,
            .on_action = DisableAllWeather,
        },
        // Vanilla presets
        WEATHER_TOGGLE(WEATHER_DAY,            "Day"),
        WEATHER_TOGGLE(WEATHER_MIDNIGHT,       "Midnight"),
        WEATHER_TOGGLE(WEATHER_LIGHT_FOG,      "Light Fog"),
        WEATHER_TOGGLE(WEATHER_DUSK_2,         "Dusk 2"),
        WEATHER_TOGGLE(WEATHER_DUSKY_CLOUDS,   "Dusky Clouds"),
        WEATHER_TOGGLE(WEATHER_DARK_VIGNETTE,  "Dark Vignette"),
        WEATHER_TOGGLE(WEATHER_DAY_2,          "Day 2"),
        WEATHER_TOGGLE(WEATHER_BLUE_SKY,       "Blue Sky"),
        WEATHER_TOGGLE(WEATHER_PINK_SKY,       "Pink Sky"),
        WEATHER_TOGGLE(WEATHER_DENSE_FOG,      "Dense Fog"),
        WEATHER_TOGGLE(WEATHER_FOGGY,          "Foggy"),
        WEATHER_TOGGLE(WEATHER_DUSK,           "Dusk"),
        WEATHER_TOGGLE(WEATHER_NIGHT,          "Night"),
        WEATHER_TOGGLE(WEATHER_GRAY_SKY,       "Gray Sky"),
        WEATHER_TOGGLE(WEATHER_DARK_PURPLE,    "Dark Purple"),
        WEATHER_TOGGLE(WEATHER_RED_VIGNETTE,   "Red Vignette"),
        WEATHER_TOGGLE(WEATHER_DARK_LOW_VIS,   "Dark Low Vis"),
        // Custom presets
        WEATHER_TOGGLE(WEATHER_DEEP_BLUE,      "Deep Blue"),
        WEATHER_TOGGLE(WEATHER_GOLDEN_HOUR,    "Golden Hour"),
        WEATHER_TOGGLE(WEATHER_BLOOD_RED,      "Blood Red"),
        WEATHER_TOGGLE(WEATHER_WHITEOUT,       "Whiteout"),
        WEATHER_TOGGLE(WEATHER_TOXIC_GREEN,    "Toxic Green"),
        WEATHER_TOGGLE(WEATHER_NEON,           "Neon"),
        WEATHER_TOGGLE(WEATHER_COTTON_CANDY,   "Cotton Candy"),
        WEATHER_TOGGLE(WEATHER_FROZEN_DAWN,    "Frozen Dawn"),
        WEATHER_TOGGLE(WEATHER_VOID,           "Void"),
        // Effect-layer presets
        WEATHER_TOGGLE(WEATHER_STORM,          "Storm"),
        WEATHER_TOGGLE(WEATHER_RAIN,           "Rain"),
        WEATHER_TOGGLE(WEATHER_BLOOD_RAIN,     "Blood Rain"),
        WEATHER_TOGGLE(WEATHER_PUDDLES,        "Puddles"),
    },
};
