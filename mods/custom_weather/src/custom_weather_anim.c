// Per-frame animation runtime for custom_weather.
//
// Three customization layers are exercised here, all gated by the active
// preset's CustomPresetDef (see custom_weather.h):
//
//   1. Terrain re-tint  - write to (*stc_main_light)->color/hw_color.
//      The stage's directional LOBJ (for CT: LOBJ[1] of the primary chain,
//      flags=0x0D INFINITE+DIFFUSE+SPECULAR) is what lights terrain. Sky
//      presets don't touch it normally.
//   2. Extra LOBJ       - spawn one INFINITE light overhead and animate its
//      color per frame. Hits all stage geometry automatically - there is no
//      per-material light_mask gating in CT.
//   3. Fog modulation   - write directly to HSD_Fog at (fog_gobj)->hsd_object
//      so fog.start/end/color move independently of the sky preset's lerp.
//
// Hook: at 0x800ce648 - the instruction immediately AFTER `bl Sky_Update`.
// At this point Sky_Update has finished writing fog/sky state for the frame,
// and r31 still holds grobj (callee-saved across the bl). The original
// instruction at 0x800ce648 is `lwz r0, 4(r31)`; the CODEPATCH trampoline runs
// it after our C function returns, so as long as we leave r31 alone (which a
// normal C call does - r31 is non-volatile) the post-hook lwz is safe.
//
// We hook AFTER Sky_Update rather than at the call site so that:
//   (a) we don't have to call Sky_Update ourselves and risk register state
//       drift between our call and the trampoline's re-execution of the
//       original `bl`,
//   (b) our LOBJ/fog writes layer on top of Sky_Update's per-frame writes
//       (otherwise Sky_Update would clobber them).

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "obj.h"
#include "code_patch/code_patch.h"

#include "custom_weather.h"

// ---- Static descriptors for the spare LOBJ ----
//
// INFINITE light (flags & 3 == 1): LObjLoad allocates a WObj from
// `position` and ignores the union. The WObj's pos vector is the light
// direction (light shines toward the origin from that point).
static WOBJDesc s_extra_pos_desc = {
    .class_name = 0,
    .pos = { 0.0f, 1500.0f, 0.0f }, // overhead
    .robjdesc = 0,
    .next = 0,
};
static LObjDesc s_extra_lobj_desc = {
    .class_name = 0,
    .next = 0,
    .flags = 0x0D,        // LOBJ_INFINITE | LOBJ_DIFFUSE | LOBJ_SPECULAR
    .attnflags = 0,
    .color = { 0, 0, 0, 0xFF },
    .position = (struct _HSD_WObjDesc *)&s_extra_pos_desc,
    .interest = 0,
    .u = { .p = 0 },
};

// ---- Per-stage state ----
// `grobj` is reused across CT exit/re-entry (same pointer), so it's not a
// reliable freshness signal. GrObj.fade_slot_id is a fresh integer on every
// entry (ScreenFade_Alloc uses an incrementing counter), so we use that.
static GrObj *s_last_grobj = 0;
static u32 s_last_slot_id = 0;
static LOBJ *s_extra_lobj = 0;

static GXColor s_orig_terrain_color;
static GXColor s_orig_terrain_hw_color;
static int s_terrain_cached = 0;

static LOBJ *s_ambient_lobj = 0;     // slot-8 ambient, resolved lazily from the HW slot table
static GXColor s_orig_ambient_color;
static GXColor s_orig_ambient_hw_color;
static int s_ambient_cached = 0;

static int s_last_seen_preset_idx = -1;
static const CustomPresetDef *s_active_def = 0;

// ---- Per-anim state ----
#define STORM_FLASH_FRAMES 18                  // flash envelope duration
#define STORM_INITIAL_LULL 30                  // first strike fires after 0.5s
static int s_storm_lull_frames = STORM_INITIAL_LULL;
static int s_storm_flash_frames = 0;           // counts DOWN from STORM_FLASH_FRAMES while flashing
static int s_aurora_phase = 0;                 // 0..359
static int s_pulse_phase = 0;                  // 0..359


static void ResetPerStage(GrObj *grobj)
{
    s_last_grobj = grobj;
    s_extra_lobj = 0;
    s_terrain_cached = 0;
    s_ambient_lobj = 0;
    s_ambient_cached = 0;
    s_last_seen_preset_idx = -1;
    s_active_def = 0;
    s_storm_lull_frames = STORM_INITIAL_LULL;
    s_storm_flash_frames = 0;
    s_aurora_phase = 0;
    s_pulse_phase = 0;
}

static void EnsureExtraLight(void)
{
    if (s_extra_lobj)
        return;
    GOBJ *gobj = GObj_Create(38, 32, 0);
    s_extra_lobj = LObj_LoadDesc(&s_extra_lobj_desc);
    GObj_AddObject(gobj, HSD_OBJKIND_LOBJ, s_extra_lobj);
    GObj_AddGXLink(gobj, LObj_GX, 0, 0);
    OSReport("[CustomWeatherAnim] Spare LOBJ created (lobj=%p, gobj=%p)\n",
             s_extra_lobj, gobj);
}

static void SetExtraColor(u8 r, u8 g, u8 b)
{
    if (!s_extra_lobj)
        return;
    s_extra_lobj->color.r = r;
    s_extra_lobj->color.g = g;
    s_extra_lobj->color.b = b;
    s_extra_lobj->color.a = 0xFF;
    s_extra_lobj->hw_color = s_extra_lobj->color;
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

// Crude integer sine, returns -100..+100. phase is degrees (0..359).
// Piecewise-linear triangle wave is enough for our visible-distance pulse;
// avoids pulling in libm.
static int IntSinTri(int deg)
{
    int p = deg % 360;
    if (p < 0)
        p += 360;
    if (p < 90)
        return (p * 100) / 90;
    if (p < 180)
        return ((180 - p) * 100) / 90;
    if (p < 270)
        return -((p - 180) * 100) / 90;
    return -((360 - p) * 100) / 90;
}

static void RunAuroraTick(void)
{
    EnsureExtraLight();
    s_aurora_phase = (s_aurora_phase + 2) % 360;
    int p = s_aurora_phase;
    int r, g, b;
    // Three colored bands, smooth crossfades:
    //   0..120  : green → cyan       (R=0, G=200, B 0→200)
    //   120..240: cyan  → violet     (R 0→200, G 200→100, B=200)
    //   240..360: violet → green     (R 200→0, G 100→200, B 200→0)
    if (p < 120)
    {
        r = 0;
        g = 200;
        b = (p * 200) / 120;
    }
    else if (p < 240)
    {
        int t = p - 120;
        r = (t * 200) / 120;
        g = 200 - (t * 100) / 120;
        b = 200;
    }
    else
    {
        int t = p - 240;
        r = 200 - (t * 200) / 120;
        g = 100 + (t * 100) / 120;
        b = 200 - (t * 200) / 120;
    }
    SetExtraColor((u8)r, (u8)g, (u8)b);
}

// Sky_Update overwrites *stc_global_fog_color each frame from the active
// preset, so writing here only needs to override for the current frame;
// no restore needed.
static void RunLightningTick(HSD_Fog *fog, u32 flash_color)
{
    EnsureExtraLight();
    if (s_storm_flash_frames > 0)
    {
        // Triangular envelope: ramp up to peak at the midpoint, then ramp down.
        int elapsed = STORM_FLASH_FRAMES - s_storm_flash_frames;
        int half = STORM_FLASH_FRAMES / 2;
        int num = (elapsed <= half) ? elapsed : (STORM_FLASH_FRAMES - elapsed);

        u8 fr = (flash_color >> 24) & 0xFF;
        u8 fg = (flash_color >> 16) & 0xFF;
        u8 fb = (flash_color >> 8) & 0xFF;

        // Spare LOBJ flash - visible on character/machine geometry that does
        // read GX hardware lights (terrain doesn't, but riders do via the
        // diffuse channel).
        SetExtraColor((u8)(fr * num / half),
                      (u8)(fg * num / half),
                      (u8)(fb * num / half));

        // Fog + EFB clear flash - this is what actually lights up terrain.
        // We lerp the per-frame fog/EFB values (already written this frame
        // by Sky_Update) toward the flash color, and pull fog_start in close
        // so the bright tint reaches near geometry too. Sky_Update overwrites
        // these every frame, so once flashing stops the preset values come
        // back automatically next frame.
        if (fog)
        {
            u8 br = fog->color.r;
            u8 bg = fog->color.g;
            u8 bb = fog->color.b;
            fog->color.r = (u8)(br + ((int)(fr - br) * num) / half);
            fog->color.g = (u8)(bg + ((int)(fg - bg) * num) / half);
            fog->color.b = (u8)(bb + ((int)(fb - bb) * num) / half);
            // Pull the fog wall in toward the camera so the flash reaches
            // near terrain instead of only the distance band.
            fog->start = fog->start * (float)(half - num) / (float)half;
        }

        u32 efb_base = *stc_global_fog_color;
        u8 br = (efb_base >> 24) & 0xFF;
        u8 bg = (efb_base >> 16) & 0xFF;
        u8 bb = (efb_base >> 8) & 0xFF;
        u8 ba = efb_base & 0xFF;
        u8 nr = (u8)(br + ((int)(fr - br) * num) / half);
        u8 ng = (u8)(bg + ((int)(fg - bg) * num) / half);
        u8 nb = (u8)(bb + ((int)(fb - bb) * num) / half);
        *stc_global_fog_color = RGBA(nr, ng, nb, ba);

        s_storm_flash_frames--;
    }
    else
    {
        SetExtraColor(0, 0, 0);
        s_storm_lull_frames--;
        if (s_storm_lull_frames <= 0)
        {
            s_storm_flash_frames = STORM_FLASH_FRAMES;
            // 3-7 seconds at 60fps between strikes.
            s_storm_lull_frames = 180 + HSD_Randi(240);
            OSReport("[CustomWeatherAnim] Lightning strike (next in %d frames)\n",
                     s_storm_lull_frames);
        }
    }
}

static void RunPulseFogTick(HSD_Fog *fog, SkyState *sky_state, int amplitude)
{
    if (!fog || !sky_state || !sky_state->target_preset)
        return;
    s_pulse_phase = (s_pulse_phase + 3) % 360; // ~6s period at 60fps
    float offset = (float)(IntSinTri(s_pulse_phase) * amplitude) / 100.0f;
    // Modulate the *current* preset values (Sky_Update has already written
    // them this frame; we add an oscillating offset on top).
    fog->start = sky_state->target_preset->fog_start + offset;
    fog->end = sky_state->target_preset->fog_end + offset;
}

static void RunFrameAnim(HSD_Fog *fog, SkyState *sky_state)
{
    if (!s_active_def)
        return;
    switch (s_active_def->anim_kind)
    {
    case ANIM_LIGHTNING:
        RunLightningTick(fog, s_active_def->anim_param);
        break;
    case ANIM_AURORA:
        RunAuroraTick();
        break;
    case ANIM_PULSE_FOG:
        RunPulseFogTick(fog, sky_state, (int)s_active_def->anim_param);
        break;
    default:
        break;
    }
}

void CustomWeatherAnim_Tick(GrObj *grobj)
{
    if (!grobj)
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

    int idx = sky_state->current_preset_index;
    if (idx != s_last_seen_preset_idx)
    {
        s_last_seen_preset_idx = idx;
        s_active_def = CustomWeather_GetPresetDef(idx);
        ApplyTerrainTint(s_active_def);
        ApplyAmbientTint(s_active_def);

        // Drive the lbfade slot-3 overlay (the global "darken everything" path):
        // Sky_BeginFade lerps to the target tint over 30 frames and holds.
        if (grobj->fade_slot_id && s_active_def && s_active_def->screen_tint)
        {
            u32 tint = s_active_def->screen_tint;
            Sky_BeginFade(grobj, &tint, 30);
        }

        OSReport("[CustomWeatherAnim] Preset %d (%s) active, anim_kind=%d, terrain=%s, char_ambient=%s, tint=%s\n",
                 idx,
                 CustomWeather_GetPresetName(idx),
                 s_active_def ? (int)s_active_def->anim_kind : 0,
                 (s_active_def && s_active_def->terrain_diffuse) ? "tinted" : "vanilla",
                 (s_active_def && s_active_def->char_ambient) ? "tinted" : "vanilla",
                 (s_active_def && s_active_def->screen_tint) ? "on" : "off");
    }
    else if (s_active_def && s_active_def->char_ambient && !s_ambient_lobj)
    {
        // Ambient slot wasn't populated when the preset first activated
        // (HW slot table lags think by a frame on the very first frame of CT).
        // Retry until it resolves, then apply once and stop.
        ApplyAmbientTint(s_active_def);
    }

    HSD_Fog *fog = fog_gobj ? (HSD_Fog *)fog_gobj->hsd_object : 0;
    RunFrameAnim(fog, sky_state);
}

// Hook at 0x800ce648 (immediately after `bl Sky_Update`). Prologue copies
// r31 (= grobj, callee-saved across the bl) into r3 for our C arg. Trampoline
// then runs the original `lwz r0, 4(r31)` and branches back to 0x800ce64C.
CODEPATCH_HOOKCREATE(0x800ce648,
                     "mr 3, 31\n\t",
                     CustomWeatherAnim_Tick,
                     "",
                     0)

void CustomWeatherAnim_OnBoot(void)
{
    CODEPATCH_HOOKAPPLY(0x800ce648);
    OSReport("[CustomWeatherAnim] Per-frame hook installed at 0x800ce648\n");
}
