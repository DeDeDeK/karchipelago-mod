#include <string.h>

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "code_patch/code_patch.h"
#include "hoshi/settings.h"

#include "custom_weather.h"

// CustomPresetDef + WeatherAnimKind live in custom_weather.h. SkyPresetEntry
// and AreaLightData live in hoshi (stage.h, obj.h). RGBA() is in gx.h. See
// docs/sky-lighting-system.md for the rendering model and the vanilla CT
// preset table used as base values below.

const CustomPresetDef custom_defs[WEATHER_CUSTOM_NUM] = {
    // Deep Blue — based on Night (12)
    // Deep ocean feel: dark blue fog, cool blue light from above
    { .base_preset = WEATHER_NIGHT,
      .fog_color = RGBA(8, 20, 40, 255),
      .fog_start = 120.0f,
      .fog_end = 600.0f,
      .sky_color = RGBA(12, 30, 60, 190),
      .char_diffuse = RGBA(140, 150, 200, 255),
      .char_specular = RGBA(100, 120, 180, 255),
      .char_dir = { 0.00f, 1.00f, 0.00f },
      .char_dir_lit = 0,
    },
    // Golden Hour — based on Dusk (11)
    // Warm sunset: orange fog, deep warm light, low sun
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
    // Blood Red — based on Red Vignette (15)
    // Hellscape: intense red fog, sickly red light from above
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
    // Whiteout — based on Dense Fog (9)
    // Blizzard: white fog, flat diffuse light, no directional source
    { .base_preset = WEATHER_DENSE_FOG,
      .fog_color = RGBA(224, 224, 232, 255),
      .fog_start = 15.0f,
      .fog_end = 80.0f,
      .sky_color = RGBA(208, 208, 224, 255),
      .char_diffuse = RGBA(220, 220, 240, 255),
      .char_specular = RGBA(230, 230, 250, 255),
      .char_dir = { 0.00f, 1.00f, 0.00f },
      .char_dir_lit = 0,
    },
    // Toxic Green — based on Dark Vignette (5)
    // Poisonous: dark green fog, eerie green-tinted light
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
    // Neon — based on Dark Purple (14)
    // Cyberpunk: magenta fog, vivid purple-pink light
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
    // Cotton Candy — based on Pink Sky (8)
    // Pink sky with contrasting teal fog, warm pink light
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
    // Frozen Dawn — based on Blue Sky (7)
    // Yellow sky with cool blue fog, cold blue-white light
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
    // Void — based on Night (12)
    // Black sky with white fog creeping in, very dim light from below
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
    // ---- Animated prototype presets ----
    // Storm — base Dark Vignette. Heavy near-fog so terrain visibly hazes
    // (KAR stage geometry doesn't read HSD light colors — fog + screen_tint
    // are the only levers that darken terrain). Lightning is a per-frame
    // fog/EFB-clear override punched in by ANIM_LIGHTNING.
    { .base_preset = WEATHER_DARK_VIGNETTE,
      .fog_color = RGBA(14, 18, 28, 255),
      .fog_start = 10.0f,
      .fog_end = 220.0f,
      .sky_color = RGBA(20, 24, 36, 220),
      .terrain_diffuse = RGBA(55, 65, 85, 255),       // cold dim directional
      .terrain_specular = RGBA(45, 55, 75, 255),
      .char_diffuse = RGBA(80, 90, 110, 255),
      .char_specular = RGBA(60, 70, 95, 255),
      .char_dir = { 0.00f, 1.00f, 0.00f },
      .char_dir_lit = 0,
      .char_ambient = RGBA(35, 40, 60, 255),
      .char_ambient_specular = RGBA(30, 35, 50, 255),
      .screen_tint = RGBA(0, 0, 8, 110),              // dark-blue lbfade overlay
      .anim_kind = ANIM_LIGHTNING,
      .anim_param = RGBA(255, 250, 240, 255),         // flash color
    },
    // Aurora — base Night. Slowly cycling green/cyan/violet directional light
    // overhead. Faint blue terrain tint. Exercises ANIM_AURORA + extra LOBJ +
    // terrain re-tint.
    { .base_preset = WEATHER_NIGHT,
      .fog_color = RGBA(8, 12, 28, 255),
      .fog_start = 180.0f,
      .fog_end = 800.0f,
      .sky_color = RGBA(0, 4, 12, 220),
      .terrain_diffuse = RGBA(80, 110, 140, 255),     // chilly blue terrain
      .terrain_specular = RGBA(120, 160, 200, 255),
      .char_diffuse = RGBA(120, 200, 200, 255),
      .char_specular = RGBA(100, 220, 220, 255),
      .char_dir = { 0.00f, 1.00f, 0.00f },
      .char_dir_lit = 1,
      .anim_kind = ANIM_AURORA,
      .anim_param = 0,
    },
    // Inferno — base Red Vignette. Hot orange terrain tint, sinusoidal fog
    // pulse so the heat haze visibly breathes. Exercises ANIM_PULSE_FOG +
    // terrain re-tint.
    { .base_preset = WEATHER_RED_VIGNETTE,
      .fog_color = RGBA(180, 60, 20, 255),
      .fog_start = 220.0f,
      .fog_end = 600.0f,
      .sky_color = RGBA(140, 32, 8, 200),
      .terrain_diffuse = RGBA(255, 200, 130, 255),    // hot warm terrain
      .terrain_specular = RGBA(255, 180, 90, 255),
      .char_diffuse = RGBA(255, 200, 140, 255),
      .char_specular = RGBA(255, 180, 100, 255),
      .char_dir = { -0.40f, 0.60f, 0.50f },
      .char_dir_lit = 1,
      .screen_tint = RGBA(90, 20, 0, 90),             // burnt orange-red overlay
      .anim_kind = ANIM_PULSE_FOG,
      .anim_param = 80,                               // ±80 distance amplitude
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
    "Storm", "Aurora", "Inferno",
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
    1, 1, 1, 1, 1, 1, 1, 1, 1,
};
_Static_assert(sizeof(weather_enabled) / sizeof(weather_enabled[0]) == WEATHER_TOTAL,
               "weather_enabled init must match WEATHER_TOTAL");

static char *toggle_names[] = {"Disabled", "Enabled"};

// Copy vanilla presets into our static buffer, append custom presets,
// and repoint the game's sub-header to use the extended array.
// Safe to call each stage load (idempotent).
static void ExtendPresetArray(GrObj *grobj)
{
    // Access path (confirmed via disasm of Sky_GetPresetCount / Sky_BeginTransition):
    //   grobj->gr_data (+0x08) -> sky_block (+0x34) -> sub_header (+0x04)
    //   sub_header[0] = preset array base pointer
    //   sub_header[1] = preset count (int stored as pointer-width)
    void **sky_block = *(void ***)((u8 *)grobj->gr_data + 0x34);
    void **sub_header = (void **)sky_block[1];
    SkyPresetEntry *vanilla_array = (SkyPresetEntry *)sub_header[0];

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
    sub_header[0] = (void *)extended_presets;
    sub_header[1] = (void *)WEATHER_TOTAL;
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
    .option_num = WEATHER_TOTAL + 2,
    .options = {
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
        // Animated prototypes
        WEATHER_TOGGLE(WEATHER_STORM,          "Storm"),
        WEATHER_TOGGLE(WEATHER_AURORA,         "Aurora"),
        WEATHER_TOGGLE(WEATHER_INFERNO,        "Inferno"),
    },
};
