#include <string.h>

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "code_patch/code_patch.h"
#include "hoshi/settings.h"

#include "custom_weather.h"

// Custom presets in enum order, WEATHER_BLOOD_RAIN .. WEATHER_TORNADO.
const CustomPresetDef custom_defs[WEATHER_CUSTOM_NUM] = {
    // Blood Rain
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
          .bolt = LTNG_BOLT_AUGMENT,
      },
    },
    // Storm
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
          .bolt = LTNG_BOLT_AUGMENT,
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
      .clouds = {
          .enabled = 1,
          .color = RGBA(58, 64, 78, 225),
          .count = 16,
          .size = 74.0f,
          .size_var = 0.45f,
          .puff_var = 0.9f,
          .height_var = 120.0f,
      },
      .volcano = {
          .enabled = 1,
          .theme = VOLC_THEME_PLASMA,
          .eruptions = 2,
      },
    },
    // Rain
    { .base_preset = WEATHER_GRAY_SKY,
      .fog_color = RGBA(90, 110, 140, 255),
      .fog_start = 600.0f,
      .fog_end = 1300.0f,
      .sky_color = RGBA(120, 150, 200, 110),
      .char_diffuse = RGBA(180, 195, 225, 255),
      .char_specular = RGBA(175, 195, 230, 255),
      .char_dir = { -0.20f, 0.85f, 0.40f },
      .char_dir_lit = 0,
      .char_ambient = RGBA(120, 135, 165, 255),
      .rain = {
          .enabled = 1,
          .color = RGBA(150, 175, 215, 130),
          .density = 700,
          .fall_speed = 26.0f,
      },
      .wind = {
          .enabled = 1,
          .speed = 2.5f,
          .heading = 70.0f,
          .gustiness = 0.25f,
          .chaos = 0.2f,
      },
      .puddles = {
          .enabled = 1,
          .color = RGBA(140, 170, 205, 180),
          .count = 16,
          .radius = 30.0f,
          .slow_factor = 0.93f,
      },
    },
    // Hailstorm
    { .base_preset = WEATHER_GRAY_SKY,
      .fog_color = RGBA(50, 60, 78, 255),
      .fog_start = 200.0f,
      .fog_end = 900.0f,
      .sky_color = RGBA(70, 82, 100, 175),
      .char_diffuse = RGBA(140, 152, 175, 255),
      .char_specular = RGBA(150, 165, 195, 255),
      .char_dir = { -0.20f, 0.85f, 0.40f },
      .char_dir_lit = 0,
      .char_ambient = RGBA(85, 95, 115, 255),
      .screen_tint = RGBA(10, 14, 22, 70),
      .rain = {
          .enabled = 1,
          .color = RGBA(175, 190, 220, 150),
          .density = 1300,
          .fall_speed = 34.0f,
          .line_width = 8,
          .streak = 1.0f,
      },
      .hail = {
          .enabled = 1,
          .amount = 1.0f,
      },
      .wind = {
          .enabled = 1,
          .speed = 8.0f,
          .heading = 90.0f,
          .gustiness = 0.6f,
          .chaos = 0.4f,
      },
      .clouds = {
          .enabled = 1,
          .color = RGBA(80, 88, 104, 215),
          .count = 15,
          .size = 70.0f,
          .size_var = 0.45f,
          .puff_var = 0.85f,
          .height_var = 110.0f,
      },
    },
    // Snowstorm
    { .base_preset = WEATHER_DENSE_FOG,
      .fog_color = RGBA(225, 230, 240, 255),
      .fog_start = 150.0f,
      .fog_end = 700.0f,
      .sky_color = RGBA(210, 218, 232, 200),
      .terrain_diffuse = RGBA(210, 216, 228, 255),
      .terrain_specular = RGBA(215, 222, 236, 255),
      .char_diffuse = RGBA(225, 230, 242, 255),
      .char_specular = RGBA(230, 236, 248, 255),
      .char_dir = { 0.00f, 1.00f, 0.00f },
      .char_dir_lit = 0,
      .char_ambient = RGBA(200, 208, 222, 255),
      .fog_curve = FOG_CURVE_EXP2,
      .snow = {
          .enabled = 1,
          .color = RGBA(248, 250, 255, 235),
          .density = 800,
          .fall_speed = 3.0f,
          .flutter = 1.8f,
          .size = 4.5f,
      },
      .wind = {
          .enabled = 1,
          .speed = 5.0f,
          .heading = 75.0f,
          .gustiness = 0.5f,
          .chaos = 0.4f,
      },
      .clouds = {
          .enabled = 1,
          .color = RGBA(210, 216, 228, 205),
          .count = 14,
          .size = 68.0f,
      },
    },
    // Moonlight
    { .base_preset = WEATHER_MIDNIGHT,
      .fog_color = RGBA(4, 6, 16, 255),
      .fog_start = 300.0f,
      .fog_end = 1100.0f,
      .sky_color = RGBA(6, 9, 22, 200),
      .terrain_diffuse = RGBA(30, 36, 56, 255),
      .terrain_specular = RGBA(24, 30, 48, 255),
      .char_diffuse = RGBA(60, 72, 105, 255),
      .char_specular = RGBA(80, 96, 140, 255),
      .char_dir = { 0.00f, 1.00f, 0.00f },
      .char_dir_lit = 0,
      .char_ambient = RGBA(22, 26, 44, 255),
      .char_ambient_specular = RGBA(18, 22, 38, 255),
      .screen_tint = RGBA(0, 2, 10, 90),
      .moon = {
          .enabled = 1,
          .phase = MOON_WAXING_GIBBOUS,
          .light = 1,
      },
      .stars = {
          .enabled = 1,
          .density = 170,
          .twinkle = 0.5f,
          .size_var = 0.6f,
          .shoot = SHOOT_FREQ_FREQUENT,
      },
    },
    // Cotton Candy
    { .base_preset = WEATHER_PINK_SKY,
      .fog_color = RGBA(0, 160, 160, 255),
      .fog_start = 200.0f,
      .fog_end = 900.0f,
      .sky_color = RGBA(255, 140, 200, 120),
      .char_diffuse = RGBA(240, 210, 230, 255),
      .char_specular = RGBA(255, 200, 240, 255),
      .char_dir = { -0.40f, 0.80f, 0.50f },
      .char_dir_lit = 1,
      .clouds = {
          .enabled = 1,
          .color = RGBA(255, 224, 240, 200),
          .count = 12,
          .size = 64.0f,
          .size_var = 0.4f,
      },
    },
    // Toxic
    { .base_preset = WEATHER_DARK_VIGNETTE,
      .fog_color = RGBA(24, 72, 28, 255),
      .fog_start = 80.0f,
      .fog_end = 450.0f,
      .sky_color = RGBA(30, 88, 34, 180),
      .char_diffuse = RGBA(150, 190, 130, 255),
      .char_specular = RGBA(130, 180, 110, 255),
      .char_dir = { -0.30f, 0.80f, 0.40f },
      .char_dir_lit = 0,
      .char_ambient = RGBA(45, 75, 40, 255),
      .screen_tint = RGBA(8, 30, 10, 70),
      .rain = {
          .enabled = 1,
          .color = RGBA(130, 210, 120, 110),
          .density = 250,
          .fall_speed = 18.0f,
          .line_width = 6,
          .streak = 1.0f,
      },
      .wind = {
          .enabled = 1,
          .speed = 2.0f,
          .heading = 60.0f,
          .gustiness = 0.2f,
          .chaos = 0.2f,
      },
      .puddles = {
          .enabled = 1,
          .color = RGBA(90, 190, 80, 185),
          .count = 22,
          .radius = 32.0f,
          .slow_factor = 0.90f,
      },
    },
    // Bubblegum
    { .base_preset = WEATHER_PINK_SKY,
      .fog_color = RGBA(255, 150, 205, 255),
      .fog_start = 250.0f,
      .fog_end = 1000.0f,
      .sky_color = RGBA(255, 130, 195, 160),
      .char_diffuse = RGBA(255, 200, 225, 255),
      .char_specular = RGBA(255, 190, 220, 255),
      .char_dir = { -0.40f, 0.80f, 0.50f },
      .char_dir_lit = 1,
      .char_ambient = RGBA(230, 160, 195, 255),
      .clouds = {
          .enabled = 1,
          .color = RGBA(255, 190, 225, 210),
          .count = 13,
          .size = 64.0f,
          .size_var = 0.4f,
      },
    },
    // Volcanic
    { .base_preset = WEATHER_DARK_VIGNETTE,
      .fog_color = RGBA(58, 30, 22, 255),
      .fog_start = 120.0f,
      .fog_end = 620.0f,
      .sky_color = RGBA(74, 34, 24, 195),
      .terrain_diffuse = RGBA(120, 78, 58, 255),
      .terrain_specular = RGBA(150, 80, 45, 255),
      .char_diffuse = RGBA(210, 140, 95, 255),
      .char_specular = RGBA(240, 130, 70, 255),
      .char_dir = { -0.25f, 0.75f, 0.45f },
      .char_dir_lit = 1,
      .char_ambient = RGBA(80, 45, 35, 255),
      .char_ambient_specular = RGBA(70, 38, 28, 255),
      .screen_tint = RGBA(30, 8, 2, 70),
      // Ashfall, drawn by the snow layer in soot grey.
      .snow = {
          .enabled = 1,
          .color = RGBA(96, 86, 80, 190),
          .density = 500,
          .fall_speed = 2.2f,
          .flutter = 1.4f,
          .size = 3.5f,
      },
      .wind = {
          .enabled = 1,
          .speed = 3.5f,
          .heading = 120.0f,
          .gustiness = 0.4f,
          .chaos = 0.35f,
      },
      .clouds = {
          .enabled = 1,
          .color = RGBA(64, 44, 38, 215),
          .count = 14,
          .size = 72.0f,
          .size_var = 0.45f,
          .puff_var = 0.85f,
          .height_var = 110.0f,
      },
      .volcano = {
          .enabled = 1,
          .theme = VOLC_THEME_FIRE,
          .eruptions = 4,
      },
    },
    // Tornado - the sickly green supercell sky that goes with a funnel on the ground.
    { .base_preset = WEATHER_GRAY_SKY,
      .fog_color = RGBA(98, 106, 78, 255),
      .fog_start = 90.0f,
      .fog_end = 700.0f,
      .sky_color = RGBA(122, 132, 92, 205),
      .terrain_diffuse = RGBA(118, 124, 96, 255),
      .terrain_specular = RGBA(98, 106, 82, 255),
      .char_diffuse = RGBA(172, 180, 145, 255),
      .char_specular = RGBA(150, 160, 124, 255),
      .char_dir = { 0.00f, 1.00f, 0.00f },
      .char_dir_lit = 0,
      .char_ambient = RGBA(88, 94, 74, 255),
      .char_ambient_specular = RGBA(74, 80, 62, 255),
      .screen_tint = RGBA(26, 30, 10, 70),
      .rain = {
          .enabled = 1,
          .color = RGBA(180, 190, 170, 130),
          .density = 800,
          .fall_speed = 32.0f,
      },
      .lightning = {
          .enabled = 1,
          .flash_color = RGBA(255, 250, 225, 255),
          .bolt = LTNG_BOLT_AUGMENT,
      },
      .wind = {
          .enabled = 1,
          .speed = 11.0f,
          .heading = 200.0f,
          .gustiness = 0.75f,
          .chaos = 0.6f,
      },
      .clouds = {
          .enabled = 1,
          .color = RGBA(74, 80, 62, 230),
          .count = 18,
          .size = 80.0f,
          .size_var = 0.5f,
          .puff_var = 0.9f,
          .height_var = 100.0f,
      },
      // Tuned for testing: several touchdowns per round so one is never far off.
      .tornado = {
          .enabled = 1,
          .count = 3,
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
    "Blood Rain", "Storm", "Rain", "Hailstorm", "Snowstorm",
    "Moonlight", "Cotton Candy", "Toxic", "Bubblegum", "Volcanic",
    "Tornado",
};

const char *CustomWeather_GetPresetName(int weather_kind)
{
    if (weather_kind < 0 || weather_kind >= WEATHER_TOTAL)
        return "Unknown";
    return preset_names[weather_kind];
}

// Vanilla entries copied from the stage file, custom entries appended.
static SkyPresetEntry extended_presets[WEATHER_TOTAL];

// Per-preset pool toggle, persisted by hoshi menu save (keyed by option name hash).
static int weather_enabled[WEATHER_TOTAL] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
};
_Static_assert(sizeof(weather_enabled) / sizeof(weather_enabled[0]) == WEATHER_TOTAL,
               "weather_enabled init must match WEATHER_TOTAL");

static char *toggle_names[] = {"Disabled", "Enabled"};

// Global "Fog Distance" multiplier written into HSD_Fog.scale. HSD_FogSet emits
// GXSetFog(..., end * scale, ...), so <1 pulls the fog wall in and >1 pushes it out.
static const float fog_distance_factors[] = {1.0f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
static char *fog_distance_names[] = {"Preset", "50%", "75%", "100%", "125%", "150%", "200%"};
#define FOG_DISTANCE_NUM (sizeof(fog_distance_factors) / sizeof(fog_distance_factors[0]))
static int fog_distance_index = 0;

float CustomWeather_GetFogScale(void)
{
    return fog_distance_factors[fog_distance_index];
}

// Repoints the game's preset sub-header at the extended array. Idempotent, so it
// can run on every stage load.
static void ExtendPresetArray(GrObj *grobj)
{
    SkyBlock *sky_block = grobj->gr_data->sky_block;
    SkyPresetSubHeader *sub_header = sky_block->preset_header;
    SkyPresetEntry *vanilla_array = sub_header->preset_array;

    memcpy(extended_presets, vanilla_array,
           WEATHER_VANILLA_NUM * sizeof(SkyPresetEntry));

    for (int i = 0; i < WEATHER_CUSTOM_NUM; i++)
    {
        const CustomPresetDef *def = &custom_defs[i];
        SkyPresetEntry *entry = &extended_presets[WEATHER_VANILLA_NUM + i];

        // Inherits the base's un-overridden AreaLight params (flags, attn, header)
        *entry = extended_presets[def->base_preset];

        entry->fog_color = def->fog_color;
        entry->fog_start = def->fog_start;
        entry->fog_end = def->fog_end;
        entry->sky_ambient_color = def->sky_color;
        entry->fade_color = 0;          // screen tint is driven from screen_tint via Sky_BeginFade
        entry->area_light.color = def->char_diffuse;
        entry->area_light.hw_color = def->char_specular;
        entry->area_light.direction = def->char_dir;
        entry->light_vis_flag = (u8)def->char_dir_lit;
        entry->transition_frames = 1;
    }

    sub_header->preset_array = extended_presets;
    sub_header->preset_count = WEATHER_TOTAL;
}

// Replaces vanilla random/fixed sky selection with a uniform pick over the
// enabled presets.
static void CustomWeather_OverrideSky(GrObj *grobj)
{
    ExtendPresetArray(grobj);

    int enabled_count = 0;
    for (int i = 0; i < WEATHER_TOTAL; i++)
    {
        if (weather_enabled[i])
            enabled_count++;
    }

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

    // WeatherRuntime names the winner a frame later, on the same path a mid-round
    // sky transition takes, so only the pool size is reported here.
    OSReport("[CustomWeather] Rolling from %d of %d presets\n", enabled_count, WEATHER_TOTAL);

    Sky_SetPresetIndex(grobj, preset);
}

// Inside Sky_Init: the City Trial (gr_kind 9) random selection block. r30 = grobj;
// the hook exits past the vanilla Sky_SetPresetIndex call.
CODEPATCH_HOOKCREATE(0x8010f1a4,
    "mr 3, 30\n\t",
    CustomWeather_OverrideSky,
    "", 0x8010f1d0);

// Inside Sky_Init: City Trial Free Run (stage kind 52) sky init, where vanilla
// hardcodes preset 0. Same r30 = grobj.
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
        WEATHER_TOGGLE(WEATHER_BLOOD_RAIN,     "Blood Rain"),
        WEATHER_TOGGLE(WEATHER_STORM,          "Storm"),
        WEATHER_TOGGLE(WEATHER_RAIN,           "Rain"),
        WEATHER_TOGGLE(WEATHER_HAILSTORM,      "Hailstorm"),
        WEATHER_TOGGLE(WEATHER_SNOWSTORM,      "Snowstorm"),
        WEATHER_TOGGLE(WEATHER_MOONLIGHT,      "Moonlight"),
        WEATHER_TOGGLE(WEATHER_COTTON_CANDY,   "Cotton Candy"),
        WEATHER_TOGGLE(WEATHER_TOXIC,          "Toxic"),
        WEATHER_TOGGLE(WEATHER_BUBBLEGUM,      "Bubblegum"),
        WEATHER_TOGGLE(WEATHER_VOLCANIC,       "Volcanic"),
        WEATHER_TOGGLE(WEATHER_TORNADO,        "Tornado"),
    },
};
