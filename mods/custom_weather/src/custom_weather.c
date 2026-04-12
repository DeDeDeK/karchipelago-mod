#include <string.h>

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "code_patch/code_patch.h"
#include "hoshi/settings.h"

#include "custom_weather.h"

// Pack separate R, G, B, A bytes into a u32 color value.
#define RGBA(r, g, b, a) ((u32)(r) << 24 | (u32)(g) << 16 | (u32)(b) << 8 | (u32)(a))

// Sky preset entry: 0x48 bytes, matching the game's stage-file format.
// Loaded from stage data (e.g. GrCity1.dat) and interpolated per-frame by
// Sky_Update. Fields confirmed via disasm of Sky_LoadPreset, Sky_Update,
// AreaLight_Lerp (lbarealight.c), and the lbfade system (lbfade.c).
typedef struct AreaLightData
{
    u32 header;             // 0x00  metadata for Sky_SetupLights, not interpolated
    u8 unk_04;              // 0x04  not interpolated
    u8 flags;               // 0x05  bit 0+1: validity (asserted), bit 2: intensity lerp
    u16 unk_06;             // 0x06  not interpolated
    u32 light_color;        // 0x08  RGBA diffuse light color (interpolated)
    u32 light_hw_color;     // 0x0C  RGBA specular/hardware light color (interpolated)
    float light_dir_x;      // 0x10  light direction X (interpolated)
    float light_dir_y;      // 0x14  light direction Y (interpolated)
    float light_dir_z;      // 0x18  light direction Z (interpolated)
    u8 unk_1C[3];           // 0x1C  raw-copied (likely attenuation type/flags)
    u8 light_intensity;     // 0x1F  interpolated (byte x ratio) if flags bit 2
    u32 attn_param_0;       // 0x20  raw-copied (HSD light attn/spot params)
    u32 attn_param_1;       // 0x24  raw-copied
    u32 attn_param_2;       // 0x28  raw-copied
} AreaLightData;

typedef struct SkyPresetEntry
{
    s32 transition_frames;      // 0x00  blend duration in frames
    u32 fog_color;              // 0x04  RGBA fog/background color (interpolated)
    float fog_start;            // 0x08  fog near distance (interpolated)
    float fog_end;              // 0x0C  fog far distance (interpolated)
    u32 fade_color;             // 0x10  RGBA screen tint overlay via lbfade (interpolated)
    u32 sky_ambient_color;      // 0x14  RGBA ambient sky color (interpolated)
    AreaLightData area_light;   // 0x18  area light params (0x2C bytes)
    u8 light_vis_flag;          // 0x44  bit 0 -> light #3 render node visibility
    u8 pad[3];                  // 0x45
} SkyPresetEntry;

_Static_assert(sizeof(SkyPresetEntry) == 0x48, "SkyPresetEntry must be 0x48 bytes");

// Custom preset spec: clone a vanilla base preset then override colors/fog/fade/light.
//
// fog_color:         RGBA fog/background color. Only RGB matters; alpha is ignored by GX.
// sky_ambient_color: RGBA skybox tint. Alpha controls opacity: 0=skybox texture visible,
//                    255=solid flat color. Most vanilla presets use 128-200.
// fade_color:        RGBA full-screen tint overlay via lbfade. Only applied during
//                    Sky_BeginTransition (event-driven mid-game changes), NOT on initial
//                    load via Sky_LoadPreset. Dead for initial sky selection.
// fog_start/end:     Fog near/far distances. Objects closer than start are unfogged,
//                    objects beyond end are fully fogged. Vanilla range: 1-1300.
// light_color:       RGBA diffuse light. How objects/terrain are lit. Vanilla uses muted
//                    pastels tinted to match the preset mood. Alpha always FF.
// light_hw_color:    RGBA specular/hardware light. Usually brighter than light_color.
// light_dir:         Light direction vector (x, y, z). Two vanilla modes:
//                      "sun":     (-0.40, 0.80, 0.50) — upper-left, most day presets.
//                      "ambient": ( 0.00, 1.00, 0.00) — straight up, dark/night presets.
//                    Dusk (11) uses (-0.40, 0.30, 0.50) for a low sun angle.
// light_vis:         Third light source visibility. 1=visible (day/lit), 0=hidden (dark).
typedef struct CustomPresetDef
{
    int base_index;
    u32 fog_color;
    u32 sky_ambient_color;
    u32 fade_color;
    float fog_start;
    float fog_end;
    u32 light_color;
    u32 light_hw_color;
    float light_dir_x;
    float light_dir_y;
    float light_dir_z;
    u8 light_vis;
} CustomPresetDef;

// Vanilla preset reference (from GrCity1.dat runtime dump):
//                       fog          start  end   fade       sky        light      hw_light   dir                vis
//  [ 0] Day             9FCFFFFF      210   665   00000000   00000000   D7D7FFFF   FFFFFFFF   (-0.40,0.80,0.50)   1
//  [ 1] Midnight        1E0005FF      140   560   00000080   1E0005C8   D2CDD2FF   A096A0FF   ( 0.00,-1.0,0.00)   0
//  [ 2] Light Fog       969696FF      140  1000   00000000   A0A0A0AA   BEBEE6FF   D2D2F0FF   (-0.40,0.80,0.50)   1
//  [ 3] Dusk 2          C8461EFF      240   900   3200006E   7832198C   E6DCD2FF   B4AAAAFF   ( 0.00,1.00,0.00)   0
//  [ 4] Dusky Clouds    C8B4E6FF        1  1000   00000000   C8B4E600   D2D2E6FF   DCD2FAFF   (-0.40,0.80,0.50)   1
//  [ 5] Dark Vignette   000000FF      140   500   0000003C   00143CB2   A0A0AAFF   9696AAFF   (-0.40,0.80,0.50)   0
//  [ 6] Day 2           32A0C8FF      240  1000   00000000   32A0C800   DCE6FAFF   C8F0FFFF   (-0.40,0.80,0.50)   1
//  [ 7] Blue Sky        82AAFFFF      300  1000   00000000   1450DC80   DCDCFFFF   FAFAFFFF   (-0.40,0.80,0.50)   1
//  [ 8] Pink Sky        FFA0D9FF      180   900   00000000   FFA0D964   F0E6F0FF   FFEBF5FF   (-0.40,0.80,0.50)   1
//  [ 9] Dense Fog       E6E6E6FF       20    90   00000000   E6E6E6FF   D2D2F0FF   DCDCFAFF   ( 0.00,1.00,0.00)   0
// [10] Foggy            E6E6E6FF      130   800   80808050   E6E6D2C8   D2D2E6FF   AAAABEFF   (-0.40,0.80,0.50)   1
// [11] Dusk             DC783CFF      300   900   785A3C00   F0965080   DCC8BEFF   FFAA6EFF   (-0.40,0.30,0.50)   1
// [12] Night            00143CFF      140   665   00001080   00143CC6   B4B4D2FF   AAB4BEFF   ( 0.00,1.00,0.00)   0
// [13] Gray Sky         785A32FF      500  1300   00000000   785A32AA   E6DCC8FF   FADCB4FF   (-0.40,0.80,0.50)   1
// [14] Dark Purple      000000FF      500  1300   0000005A   3C0000A0   DCC8C8FF   F0DCFFFF   (-0.40,0.80,0.50)   0
// [15] Red Vignette     C8461EFF      100   500   32000000   783219B4   DCC8BEFF   FAD2AAFF   ( 0.00,1.00,0.00)   1
// [16] Dark Low Vis     000000FF       90   360   2800006E   1E0005C8   F0DCC8FF   DCA078FF   ( 0.00,-1.0,0.00)   1
static const CustomPresetDef custom_defs[WEATHER_CUSTOM_NUM] = {
    // Deep Blue — based on Night (12)
    // Deep ocean feel: dark blue fog, cool blue light from above
    { .base_index = WEATHER_NIGHT,
      .fog_color = RGBA(8, 20, 40, 255),
      .sky_ambient_color = RGBA(12, 30, 60, 190),
      .fade_color = RGBA(6, 16, 32, 64),
      .fog_start = 120.0f, 
      .fog_end = 600.0f,
      .light_color = RGBA(140, 150, 200, 255), 
      .light_hw_color = RGBA(100, 120, 180, 255),
      .light_dir_x = 0.00f, 
      .light_dir_y = 1.00f, 
      .light_dir_z = 0.00f, 
      .light_vis = 0 
    },
    // Golden Hour — based on Dusk (11)
    // Warm sunset: orange fog, deep warm light, low sun
    { .base_index = WEATHER_DUSK,
      .fog_color = RGBA(200, 128, 48, 255),
      .sky_ambient_color = RGBA(224, 152, 56, 150),
      .fade_color = RGBA(64, 40, 0, 48),
      .fog_start = 280.0f, 
      .fog_end = 850.0f,
      .light_color = RGBA(240, 200, 140, 255), 
      .light_hw_color = RGBA(255, 180, 90, 255),
      .light_dir_x = -0.40f, 
      .light_dir_y = 0.20f, 
      .light_dir_z = 0.50f, 
      .light_vis = 1 
    },
    // Blood Red — based on Red Vignette (15)
    // Hellscape: intense red fog, sickly red light from above
    { .base_index = WEATHER_RED_VIGNETTE,
      .fog_color = RGBA(96, 8, 8, 255),
      .sky_ambient_color = RGBA(128, 16, 16, 190),
      .fade_color = RGBA(80, 8, 0, 80),
      .fog_start = 80.0f, 
      .fog_end = 450.0f,
      .light_color = RGBA(200, 120, 100, 255), 
      .light_hw_color = RGBA(220, 100, 80, 255),
      .light_dir_x = 0.00f, 
      .light_dir_y = 1.00f, 
      .light_dir_z = 0.00f, 
      .light_vis = 1 
    },
    // Whiteout — based on Dense Fog (9)
    // Blizzard: white fog, flat diffuse light, no directional source
    { .base_index = WEATHER_DENSE_FOG,
      .fog_color = RGBA(224, 224, 232, 255),
      .sky_ambient_color = RGBA(208, 208, 224, 255),
      .fade_color = RGBA(192, 192, 208, 32),
      .fog_start = 15.0f, 
      .fog_end = 80.0f,
      .light_color = RGBA(220, 220, 240, 255), 
      .light_hw_color = RGBA(230, 230, 250, 255),
      .light_dir_x = 0.00f, 
      .light_dir_y = 1.00f, 
      .light_dir_z = 0.00f, 
      .light_vis = 0 
    },
    // Toxic Green — based on Dark Vignette (5)
    // Poisonous: dark green fog, eerie green-tinted light
    { .base_index = WEATHER_DARK_VIGNETTE,
      .fog_color = RGBA(16, 48, 16, 255),
      .sky_ambient_color = RGBA(24, 72, 24, 170),
      .fade_color = RGBA(8, 32, 8, 64),
      .fog_start = 120.0f, 
      .fog_end = 500.0f,
      .light_color = RGBA(140, 180, 130, 255), 
      .light_hw_color = RGBA(120, 170, 110, 255),
      .light_dir_x = -0.40f, 
      .light_dir_y = 0.80f, 
      .light_dir_z = 0.50f, 
      .light_vis = 0 
    },
    // Neon — based on Dark Purple (14)
    // Cyberpunk: magenta fog, vivid purple-pink light
    { .base_index = WEATHER_DARK_PURPLE,
      .fog_color = RGBA(128, 32, 192, 255),
      .sky_ambient_color = RGBA(32, 8, 64, 150),
      .fade_color = RGBA(48, 16, 64, 72),
      .fog_start = 450.0f, 
      .fog_end = 1200.0f,
      .light_color = RGBA(200, 160, 220, 255), 
      .light_hw_color = RGBA(240, 180, 255, 255),
      .light_dir_x = -0.40f, 
      .light_dir_y = 0.80f, 
      .light_dir_z = 0.50f, 
      .light_vis = 0 
    },
    // Cotton Candy — based on Pink Sky (8)
    // Pink sky with contrasting teal fog, warm pink light
    { .base_index = WEATHER_PINK_SKY,
      .fog_color = RGBA(0, 160, 160, 255),
      .sky_ambient_color = RGBA(255, 140, 200, 120),
      .fade_color = RGBA(0, 0, 0, 0),
      .fog_start = 200.0f, 
      .fog_end = 900.0f,
      .light_color = RGBA(240, 210, 230, 255), 
      .light_hw_color = RGBA(255, 200, 240, 255),
      .light_dir_x = -0.40f, 
      .light_dir_y = 0.80f, 
      .light_dir_z = 0.50f, 
      .light_vis = 1 
    },
    // Frozen Dawn — based on Blue Sky (7)
    // Yellow sky with cool blue fog, cold blue-white light
    { .base_index = WEATHER_BLUE_SKY,
      .fog_color = RGBA(40, 60, 140, 255),
      .sky_ambient_color = RGBA(220, 200, 80, 140),
      .fade_color = RGBA(0, 0, 0, 0),
      .fog_start = 250.0f, 
      .fog_end = 900.0f,
      .light_color = RGBA(190, 200, 240, 255), 
      .light_hw_color = RGBA(210, 220, 255, 255),
      .light_dir_x = -0.40f, 
      .light_dir_y = 0.80f, 
      .light_dir_z = 0.50f, 
      .light_vis = 1 
    },
    // Void — based on Night (12)
    // Black sky with white fog creeping in, very dim light from below
    { .base_index = WEATHER_NIGHT,
      .fog_color = RGBA(200, 200, 210, 255),
      .sky_ambient_color = RGBA(0, 0, 0, 200),
      .fade_color = RGBA(0, 0, 0, 0),
      .fog_start = 150.0f, 
      .fog_end = 700.0f,
      .light_color = RGBA(100, 100, 110, 255), 
      .light_hw_color = RGBA(80, 80, 90, 255),
      .light_dir_x = 0.00f, 
      .light_dir_y = -1.00f, 
      .light_dir_z = 0.00f, 
      .light_vis = 0 
    },
};

// Extended preset buffer: vanilla entries copied from stage file + custom appended.
static SkyPresetEntry extended_presets[WEATHER_TOTAL];

// Per-preset enabled toggle. 1 = Enabled, 0 = Disabled. Default: all enabled.
// Each entry is persisted by hoshi menu save (keyed by option name hash).
static int weather_enabled[WEATHER_TOTAL] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1,
};

static char *toggle_names[] = {"Disabled", "Enabled"};

// ---------------------------------------------------------------
// Preset array extension
// ---------------------------------------------------------------

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

    // Dump vanilla preset values for tuning reference
    for (int i = 0; i < WEATHER_VANILLA_NUM; i++)
    {
        SkyPresetEntry *p = &extended_presets[i];
        OSReport("[CustomWeather] Vanilla[%2d] tf=%3d fog=0x%08x start=%.0f end=%.0f fade=0x%08x sky=0x%08x\n",
                 i, p->transition_frames,
                 p->fog_color, p->fog_start, p->fog_end,
                 p->fade_color, p->sky_ambient_color);
        AreaLightData *al = &p->area_light;
        OSReport("[CustomWeather]   light=0x%08x hw=0x%08x dir=(%.2f,%.2f,%.2f) intensity=%d flags=0x%02x vis=%d\n",
                 al->light_color, al->light_hw_color,
                 al->light_dir_x, al->light_dir_y, al->light_dir_z,
                 al->light_intensity, al->flags, p->light_vis_flag);
    }

    // Build each custom preset by cloning its base then overriding colors/fog/light
    for (int i = 0; i < WEATHER_CUSTOM_NUM; i++)
    {
        const CustomPresetDef *def = &custom_defs[i];
        SkyPresetEntry *entry = &extended_presets[WEATHER_VANILLA_NUM + i];

        // Clone base (inherits non-overridden area light params: flags, attn, header)
        *entry = extended_presets[def->base_index];

        entry->fog_color = def->fog_color;
        entry->sky_ambient_color = def->sky_ambient_color;
        entry->fade_color = def->fade_color;
        entry->fog_start = def->fog_start;
        entry->fog_end = def->fog_end;
        entry->area_light.light_color = def->light_color;
        entry->area_light.light_hw_color = def->light_hw_color;
        entry->area_light.light_dir_x = def->light_dir_x;
        entry->area_light.light_dir_y = def->light_dir_y;
        entry->area_light.light_dir_z = def->light_dir_z;
        entry->light_vis_flag = def->light_vis;
        entry->transition_frames = 1;
    }

    // Repoint game data to our extended array
    sub_header[0] = (void *)extended_presets;
    sub_header[1] = (void *)WEATHER_TOTAL;
}

// ---------------------------------------------------------------
// Sky override (called from hooks in Sky_Init)
// ---------------------------------------------------------------

static const char *preset_names[WEATHER_TOTAL] = {
    "Day", "Midnight", "Light Fog", "Dusk 2", "Dusky Clouds",
    "Dark Vignette", "Day 2", "Blue Sky", "Pink Sky", "Dense Fog",
    "Foggy", "Dusk", "Night", "Gray Sky", "Dark Purple",
    "Red Vignette", "Dark Low Vis",
    "Deep Blue", "Golden Hour", "Blood Red", "Whiteout",
    "Toxic Green", "Neon", "Cotton Candy", "Frozen Dawn", "Void",
};

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
             preset, preset_names[preset], enabled_count, WEATHER_TOTAL);

    Sky_SetPresetIndex(grobj, preset);
}

// ---------------------------------------------------------------
// Hooks into Sky_Init
// ---------------------------------------------------------------

// Hook at 0x8010f1a4: City Trial (stage kind 9) random selection block.
// r30 = grobj (the extended stage object). Exits past vanilla setSkyIndex.
CODEPATCH_HOOKCREATE(0x8010f1a4,
    "mr 3, 30\n\t",
    CustomWeather_OverrideSky,
    "", 0x8010f1d0);

// Hook at 0x8010f224: City Trial Free Run (stage kind 52) sky init.
// Vanilla hardcodes preset 0. Same r30 = grobj.
CODEPATCH_HOOKCREATE(0x8010f224,
    "mr 3, 30\n\t",
    CustomWeather_OverrideSky,
    "", 0x8010f230);

void CustomWeather_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x8010f1a4);
    CODEPATCH_HOOKAPPLY(0x8010f224);
    OSReport("[CustomWeather] Hooks installed (Sky_Init Trial + Free Run)\n");
}

// ---------------------------------------------------------------
// Menu: Enable All / Disable All actions
// ---------------------------------------------------------------

static int EnableAllWeather(void)
{
    for (int i = 0; i < WEATHER_TOTAL; i++)
        weather_enabled[i] = 1;
    return 1;
}

static int DisableAllWeather(void)
{
    for (int i = 0; i < WEATHER_TOTAL; i++)
        weather_enabled[i] = 0;
    return 1;
}

// ---------------------------------------------------------------
// Menu definition: per-preset toggles + bulk actions
// ---------------------------------------------------------------

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
    },
};
