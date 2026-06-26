// Per-frame customization runtime for custom_weather: applies each active preset's
// optional CustomPresetDef layers and ticks the effect modules (rain, lightning,
// wind, hail, puddles), hooked immediately after Sky_Update so its writes layer on
// top of the per-frame sky state rather than being clobbered.

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "obj.h"
#include "code_patch/code_patch.h"

#include "custom_weather.h"

// `grobj` is reused across CT exit/re-entry (same pointer), so it's not a
// reliable freshness signal. GrObj.fade_slot_id is a fresh integer on every
// entry (ScreenFade_Alloc uses an incrementing counter), so we use that.
static GrObj *s_last_grobj = 0;
static u32 s_last_slot_id = 0;

static GXColor s_orig_terrain_color;
static GXColor s_orig_terrain_hw_color;
static int s_terrain_cached = 0;

static LOBJ *s_ambient_lobj = 0;     // slot-8 ambient, resolved lazily from the HW slot table
static GXColor s_orig_ambient_color;
static GXColor s_orig_ambient_hw_color;
static int s_ambient_cached = 0;

static int s_last_seen_preset_idx = -1;
static const CustomPresetDef *s_active_def = 0;


static void ResetPerStage(GrObj *grobj)
{
    s_last_grobj = grobj;
    s_terrain_cached = 0;
    s_ambient_lobj = 0;
    s_ambient_cached = 0;
    s_last_seen_preset_idx = -1;
    s_active_def = 0;
    Rain_Reset();
    Lightning_Reset();
    Wind_Reset();
    Puddle_Reset();
    Hail_Reset();
}

static void ApplyTerrainTint(const CustomPresetDef *def)
{
    LOBJ *l = *stc_main_light;
    if (!l)
        return;
    if (!s_terrain_cached)
    {
        s_orig_terrain_color = l->color;
        s_orig_terrain_hw_color = l->hw_color;
        s_terrain_cached = 1;
    }
    l->color = (def && def->terrain_diffuse)
                   ? GXColor_Unpack(def->terrain_diffuse)
                   : s_orig_terrain_color;
    l->hw_color = (def && def->terrain_specular)
                      ? GXColor_Unpack(def->terrain_specular)
                      : s_orig_terrain_hw_color;
}

// CT's ambient LOBJ (slot 8) is full white and is what's keeping unlit faces
// fully bright when only the directional sun is dimmed. Tint or restore it
// here. The slot pointer is resolved lazily because the HW slot table is
// populated by GX rendering, which lags our per-frame think hook by a frame.
static void ApplyAmbientTint(const CustomPresetDef *def)
{
    if (!s_ambient_lobj)
        s_ambient_lobj = stc_lobj_hw_slot_table[HSD_LOBJ_HW_SLOT_AMBIENT];
    if (!s_ambient_lobj)
        return; // not yet populated; we'll retry on the next preset change

    if (!s_ambient_cached)
    {
        s_orig_ambient_color = s_ambient_lobj->color;
        s_orig_ambient_hw_color = s_ambient_lobj->hw_color;
        s_ambient_cached = 1;
    }
    s_ambient_lobj->color = (def && def->char_ambient)
                                ? GXColor_Unpack(def->char_ambient)
                                : s_orig_ambient_color;
    s_ambient_lobj->hw_color = (def && def->char_ambient_specular)
                                   ? GXColor_Unpack(def->char_ambient_specular)
                                   : s_orig_ambient_hw_color;
}

// Map a preset's WeatherFogCurve to a GXFogType. CT always loads
// GX_FOG_PERSP_LIN, so "inherit"/"linear" both resolve to it - we write an
// absolute value every preset change rather than caching a prior (possibly
// already-modified) type, which keeps it leak-proof across CT re-entry.
static u32 FogCurveToGX(u32 curve)
{
    switch (curve)
    {
    case FOG_CURVE_EXP:     return GX_FOG_PERSP_EXP;
    case FOG_CURVE_EXP2:    return GX_FOG_PERSP_EXP2;
    case FOG_CURVE_REVEXP:  return GX_FOG_PERSP_REVEXP;
    case FOG_CURVE_REVEXP2: return GX_FOG_PERSP_REVEXP2;
    default:                return GX_FOG_PERSP_LIN; // LINEAR / INHERIT
    }
}

// Sky_Update never touches HSD_Fog.type, so a single write on preset change
// holds for the whole preset.
static void ApplyFogCurve(HSD_Fog *fog, const CustomPresetDef *def)
{
    if (!fog)
        return;
    fog->type = FogCurveToGX(def ? def->fog_curve : FOG_CURVE_INHERIT);
}

void CustomWeatherRuntime_Tick(GrObj *grobj)
{
    if (!grobj)
        return;

    // The fog/sky levers below are City-Trial weather features; leave every
    // other mode's fog untouched.
    if (grobj->gr_kind != GR_CITY1)
        return;

    if (grobj != s_last_grobj || grobj->fade_slot_id != s_last_slot_id)
    {
        ResetPerStage(grobj);
        s_last_slot_id = grobj->fade_slot_id;
    }

    GOBJ *fog_gobj = grobj->sky_gobj;
    if (!fog_gobj)
        return;
    SkyState *sky_state = (SkyState *)fog_gobj->userdata;
    if (!sky_state)
        return;

    HSD_Fog *fog = (HSD_Fog *)fog_gobj->hsd_object;

    int idx = sky_state->current_preset_index;
    if (idx != s_last_seen_preset_idx)
    {
        s_last_seen_preset_idx = idx;
        s_active_def = CustomWeather_GetPresetDef(idx);
        ApplyTerrainTint(s_active_def);
        ApplyAmbientTint(s_active_def);
        ApplyFogCurve(fog, s_active_def);
        Rain_SetActive(s_active_def ? &s_active_def->rain : 0);
        Lightning_SetActive(s_active_def ? &s_active_def->lightning : 0);
        Wind_SetActive(s_active_def ? &s_active_def->wind : 0);
        Puddle_SetActive(s_active_def ? &s_active_def->puddles : 0);

        // Drive the lbfade slot-3 overlay (the global "darken everything" path):
        // Sky_BeginFade lerps to the target tint over 30 frames and holds.
        if (grobj->fade_slot_id && s_active_def && s_active_def->screen_tint)
        {
            u32 tint = s_active_def->screen_tint;
            Sky_BeginFade(grobj, &tint, 30);
        }

        OSReport("[WeatherRuntime] Preset %d (%s) active, terrain=%s, char_ambient=%s, tint=%s, fog_curve=%d, rain=%s, lightning=%s, wind=%s, puddles=%s\n",
                 idx,
                 CustomWeather_GetPresetName(idx),
                 (s_active_def && s_active_def->terrain_diffuse) ? "tinted" : "vanilla",
                 (s_active_def && s_active_def->char_ambient) ? "tinted" : "vanilla",
                 (s_active_def && s_active_def->screen_tint) ? "on" : "off",
                 s_active_def ? (int)s_active_def->fog_curve : 0,
                 (s_active_def && s_active_def->rain.enabled) ? "on" : "off",
                 (s_active_def && s_active_def->lightning.enabled) ? "on" : "off",
                 (s_active_def && s_active_def->wind.enabled) ? "on" : "off",
                 (s_active_def && s_active_def->puddles.enabled) ? "on" : "off");
    }
    else if (s_active_def && s_active_def->char_ambient && !s_ambient_lobj)
    {
        // Ambient slot wasn't populated when the preset first activated
        // (HW slot table lags think by a frame on the very first frame of CT).
        // Retry until it resolves, then apply once and stop.
        ApplyAmbientTint(s_active_def);
    }

    // Global Fog Distance multiplier - written every frame (cheap) so a live
    // menu change takes effect immediately and it covers vanilla presets too.
    // Sky_Update leaves HSD_Fog.scale at 1.0, so we own this field.
    if (fog)
        fog->scale = CustomWeather_GetFogScale();

    Lightning_Tick(fog);
    // Advance the wind first so rain and hail read the fresh vector this frame.
    Wind_Tick();
    Rain_Tick();
    Hail_Tick();
    Puddle_Tick();
}

// Hook at 0x800ce648 (immediately after `bl Sky_Update`). Prologue copies
// r31 (= grobj, callee-saved across the bl) into r3 for our C arg. Trampoline
// then runs the original `lwz r0, 4(r31)` and branches back to 0x800ce64C.
CODEPATCH_HOOKCREATE(0x800ce648,
                     "mr 3, 31\n\t",
                     CustomWeatherRuntime_Tick,
                     "",
                     0)

void CustomWeatherRuntime_OnBoot(void)
{
    CODEPATCH_HOOKAPPLY(0x800ce648);
    OSReport("[WeatherRuntime] Per-frame hook installed at 0x800ce648\n");
}
