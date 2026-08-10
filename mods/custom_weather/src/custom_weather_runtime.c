// Applies the active preset's optional CustomPresetDef layers and ticks the effect
// modules. Hooked immediately after Sky_Update so its writes layer on top of the
// per-frame sky state rather than being clobbered.

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "obj.h"
#include "code_patch/code_patch.h"

#include "custom_weather.h"

// `grobj` is reused across CT exit/re-entry, so it can't signal a fresh entry.
// GrObj.fade_slot_id can: ScreenFade_Alloc hands out an incrementing id per entry.
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
    Snow_Reset();
    Lightning_Reset();
    Wind_Reset();
    Puddle_Reset();
    Hail_Reset();
    Tree_Reset();
    Cloud_Reset();
    Moon_Reset();
    Star_Reset();
    Volcano_Reset();
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

// CT's ambient LOBJ (slot 8) is full white, so it keeps unlit faces bright when
// only the directional sun is dimmed. The slot pointer resolves lazily: the HW
// slot table is populated by GX rendering, which lags the think hook by a frame.
static void ApplyAmbientTint(const CustomPresetDef *def)
{
    if (!s_ambient_lobj)
        s_ambient_lobj = stc_lobj_hw_slot_table[HSD_LOBJ_HW_SLOT_AMBIENT];
    if (!s_ambient_lobj)
        return; // not yet populated; retried later

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

// CT always loads GX_FOG_PERSP_LIN, so inherit and linear both resolve to it.
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

    // City Trial only; every other mode keeps its own fog and sky.
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
        Snow_SetActive(s_active_def ? &s_active_def->snow : 0);
        Hail_SetActive(s_active_def ? &s_active_def->hail : 0);
        Lightning_SetActive(s_active_def ? &s_active_def->lightning : 0);
        Wind_SetActive(s_active_def ? &s_active_def->wind : 0);
        Puddle_SetActive(s_active_def ? &s_active_def->puddles : 0);
        Cloud_SetActive(s_active_def ? &s_active_def->clouds : 0);
        Moon_SetActive(s_active_def ? &s_active_def->moon : 0);
        Star_SetActive(s_active_def ? &s_active_def->stars : 0);
        Volcano_SetActive(s_active_def ? &s_active_def->volcano : 0);

        // Sky_BeginFade lerps the lbfade slot-3 overlay to the tint over 30
        // frames, then holds it.
        if (grobj->fade_slot_id && s_active_def && s_active_def->screen_tint)
        {
            u32 tint = s_active_def->screen_tint;
            Sky_BeginFade(grobj, &tint, 30);
        }

        OSReport("[WeatherRuntime] Preset %d (%s) active, terrain=%s, char_ambient=%s, tint=%s, fog_curve=%d, rain=%s, snow=%s, hail=%s, lightning=%s, wind=%s, puddles=%s, clouds=%s, moon=%s, stars=%s, volcano=%s\n",
                 idx,
                 CustomWeather_GetPresetName(idx),
                 (s_active_def && s_active_def->terrain_diffuse) ? "tinted" : "vanilla",
                 (s_active_def && s_active_def->char_ambient) ? "tinted" : "vanilla",
                 (s_active_def && s_active_def->screen_tint) ? "on" : "off",
                 s_active_def ? (int)s_active_def->fog_curve : 0,
                 (s_active_def && s_active_def->rain.enabled) ? "on" : "off",
                 (s_active_def && s_active_def->snow.enabled) ? "on" : "off",
                 (s_active_def && s_active_def->hail.enabled) ? "on" : "off",
                 (s_active_def && s_active_def->lightning.enabled) ? "on" : "off",
                 (s_active_def && s_active_def->wind.enabled) ? "on" : "off",
                 (s_active_def && s_active_def->puddles.enabled) ? "on" : "off",
                 (s_active_def && s_active_def->clouds.enabled) ? "on" : "off",
                 (s_active_def && s_active_def->moon.enabled) ? "on" : "off",
                 (s_active_def && s_active_def->stars.enabled) ? "on" : "off",
                 (s_active_def && s_active_def->volcano.enabled) ? "on" : "off");
    }
    else if (s_active_def && s_active_def->char_ambient && !s_ambient_lobj)
    {
        // The HW slot table lags think by a frame on the first CT frame; retry
        // until the ambient slot resolves, then apply once and stop.
        ApplyAmbientTint(s_active_def);
    }

    // Sky_Update leaves HSD_Fog.scale at 1.0, so this field is ours. Written every
    // frame so a live menu change lands immediately, vanilla presets included.
    if (fog)
        fog->scale = CustomWeather_GetFogScale();

    Lightning_Tick(fog);
    // Advance the wind first so rain, snow, hail, and trees read the fresh vector.
    Wind_Tick();
    Rain_Tick();
    Snow_Tick();
    Hail_Tick();
    Puddle_Tick();
    Tree_Tick();
    Cloud_Tick();
    Moon_Tick();
    Star_Tick();
    Volcano_Tick();
}

// Immediately after `bl Sky_Update`; r31 = grobj, callee-saved across the bl.
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
