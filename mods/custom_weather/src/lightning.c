// World lightning for custom_weather: a per-preset strike loop - a screen/EFB
// flash (and an opt-in jagged GX bolt) punctuating long random lulls.

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "obj.h"
#include "gx.h"
#include "hoshi/settings.h"

#include "custom_weather.h"

// ---- Defaults applied when a preset leaves the matching LightningDef field 0.
#define LTNG_DEF_FLASH_COLOR   RGBA(255, 250, 240, 255) // near-white strike
#define LTNG_DEF_FLASH_FRAMES  18                       // flash envelope length
#define LTNG_DEF_MIN_LULL      180                      // 3s at 60fps
#define LTNG_DEF_MAX_LULL      420                      // 7s at 60fps
#define LTNG_INITIAL_LULL      30                       // first strike after 0.5s

// ---- Flicker shape. Inside the flash window the brightness strobes: full for
// STROBE_ON frames, then dim (STROBE_FLOOR, not fully dark) for STROBE_GAP, all
// scaled by an overall decay so the strike fades out. Drives the flash and the
// bolt alike, so they pulse a few times together.
#define LTNG_STROBE_ON     2
#define LTNG_STROBE_GAP    2
#define LTNG_STROBE_FLOOR  0.18f

// ---- Bolt geometry (world units). A jagged channel descending from sky to
// ground with one offshoot fork, jittered horizontally as it falls.
#define BOLT_TOP_Y        820.0f
#define BOLT_GROUND_Y     -40.0f
#define BOLT_SEGMENTS     13              // main channel segment count
#define BOLT_FORK_SEGS    4              // offshoot length
#define BOLT_MAX_SEG      (BOLT_SEGMENTS + BOLT_FORK_SEGS)
#define BOLT_JITTER       42.0f          // max horizontal wander per main step
#define BOLT_FORK_JITTER  60.0f
#define BOLT_SPREAD       1000.0f        // fallback scatter half-width (used only if no OOB box)
#define BOLT_DEF_COLOR    RGBA(210, 225, 255, 255) // fallback blue-white
#define BOLT_GLOW_WIDTH   44             // wide dim glow pass (1/6-px units)
#define BOLT_CORE_WIDTH   14             // thin bright core pass
#define BOLT_GLOW_ALPHA   0.5f           // glow alpha relative to the core

// ---- Point light at the bolt midpoint. Computed attenuation (ref_br at
// ref_dist) so the engine derives the GX coefficients; omnidirectional.
#define BOLT_LIGHT_REF_DIST  520.0f
#define BOLT_LIGHT_REF_BR    0.5f
#define GX_SPOT_OFF          0           // GXSpotFn GX_SP_OFF (omnidirectional)
#define GX_DIST_MEDIUM       2           // GXDistAttnFn GX_DA_MEDIUM

// Bolt render GObj: arbitrary high entity class (avoids real engine classes), a
// spare p_link, and the world camera's gx_link 0 on the XLU sub-pass - matching
// the rain layer so the bolt shares the same depth-tested world pass.
#define BOLT_GOBJ_CLASS   202
#define BOLT_GOBJ_PLINK   26
#define BOLT_GX_LINK      0
#define BOLT_GX_PRI       0

// ---- Descriptors for the overhead INFINITE flash LOBJ (the global rider flash).
// INFINITE light (flags & 3 == 1): LObjLoad allocates a WObj from `position` and
// ignores the union. The WObj's pos vector is the light direction (the light
// shines toward the origin from that point).
static WOBJDesc s_flash_pos_desc = {
    .class_name = 0,
    .pos = { 0.0f, 1500.0f, 0.0f }, // overhead
    .robjdesc = 0,
    .next = 0,
};
static LObjDesc s_flash_lobj_desc = {
    .class_name = 0,
    .next = 0,
    .flags = LOBJ_INFINITE | LOBJ_DIFFUSE | LOBJ_SPECULAR, // 0x0D
    .attnflags = 0,
    .color = { 0, 0, 0, 0xFF },
    .position = (struct _HSD_WObjDesc *)&s_flash_pos_desc,
    .interest = 0,
    .u = { .p = 0 },
};

// ---- Descriptors for the POINT LOBJ at the bolt midpoint. POINT light
// (flags & 3 == 2): LObjLoad loads `position` as the world point; the computed
// point desc (attnflags bit 0 clear) gives ref-brightness/ref-distance falloff.
static WOBJDesc s_bolt_pos_desc = {
    .class_name = 0,
    .pos = { 0.0f, 400.0f, 0.0f }, // repositioned to the bolt midpoint each strike
    .robjdesc = 0,
    .next = 0,
};
static struct _HSD_LightPointDesc s_bolt_point = {
    .cutoff = 1.0f,
    .point_func = GX_SPOT_OFF,
    .ref_br = BOLT_LIGHT_REF_BR,
    .ref_dist = BOLT_LIGHT_REF_DIST,
    .dist_func = GX_DIST_MEDIUM,
};
static LObjDesc s_bolt_lobj_desc = {
    .class_name = 0,
    .next = 0,
    .flags = LOBJ_POINT | LOBJ_DIFFUSE | LOBJ_SPECULAR, // 0x0E
    .attnflags = 0,        // computed (ref_br/ref_dist) attenuation, not raw
    .color = { 0, 0, 0, 0xFF },
    .position = (struct _HSD_WObjDesc *)&s_bolt_pos_desc,
    .interest = 0,
    .u = { .point = &s_bolt_point },
};

static LOBJ *s_flash_lobj = 0;
static LOBJ *s_bolt_lobj = 0;
static GOBJ *s_bolt_render = 0;

static int stc_active = 0;

// Resolved appearance/timing for the active preset (LightningDef + defaults).
static GXColor stc_flash_color = {255, 250, 240, 255};
static GXColor stc_bolt_color = {210, 225, 255, 255};
static int     stc_flash_len = LTNG_DEF_FLASH_FRAMES;
static int     stc_min_lull = LTNG_DEF_MIN_LULL;
static int     stc_max_lull = LTNG_DEF_MAX_LULL;
static int     stc_preset_bolt = LTNG_BOLT_OFF; // this preset's bolt mode

// Strike state machine: counts down lull, then counts down a flash envelope.
static int s_lull_frames = LTNG_INITIAL_LULL;
static int s_flash_frames = 0;

// Bolt geometry (world space), generated per strike. Segments are point pairs so
// the fork is just extra entries past the main channel.
static Vec3 s_seg_a[BOLT_MAX_SEG];
static Vec3 s_seg_b[BOLT_MAX_SEG];
static int  s_seg_count = 0;
static Vec3 s_bolt_mid = {0.0f, 0.0f, 0.0f};

// ---- Global "Lightning Bolts" override (settings menu) ----
static char *bolt_override_names[] = {"Auto", "Off", "Force"};
static int   bolt_override_index = 0; // 0=Auto (honor preset), 1=Off, 2=Force

// Symmetric random in [-1, 1).
static float Randf2(void)
{
    return HSD_Randf() * 2.0f - 1.0f;
}

// Resolve the effective bolt mode from the active preset's setting and the
// global menu override. Force lifts an off/augment preset to at least augment,
// but honors a preset that specifically asked to replace the flash.
static int EffectiveBoltMode(void)
{
    switch (bolt_override_index)
    {
    case 1: return LTNG_BOLT_OFF;
    case 2: return (stc_preset_bolt == LTNG_BOLT_REPLACE) ? LTNG_BOLT_REPLACE : LTNG_BOLT_AUGMENT;
    default: return stc_preset_bolt;
    }
}

// Shared strobe brightness in [0, 1] for the current frame of the flash window:
// a few sharp pulses decaying to nothing. Drives the flash and the bolt alike.
static float FlashBrightness(void)
{
    if (s_flash_frames <= 0 || stc_flash_len <= 0)
        return 0.0f;
    int elapsed = stc_flash_len - s_flash_frames; // 0 .. len-1
    float decay = (float)s_flash_frames / (float)stc_flash_len;
    int phase = elapsed % (LTNG_STROBE_ON + LTNG_STROBE_GAP);
    float strobe = (phase < LTNG_STROBE_ON) ? 1.0f : LTNG_STROBE_FLOOR;
    return decay * strobe;
}

// Ground anchor for a strike. Primary: a uniform random XZ inside the stage's
// out-of-bounds death box, so bolts fall anywhere across the map (including the
// OOB margin), independent of any camera. Fallback (no OOB box): a random
// active rider's XZ plus a wide scatter, else the world origin.
static void StrikeAnchor(float *ax, float *az)
{
    GrObj *gr = *stc_grobj;
    if (gr && gr->gr_data && gr->gr_data->stage_node)
    {
        StageNode *sn = gr->gr_data->stage_node;
        *ax = sn->oob_min.X + HSD_Randf() * (sn->oob_max.X - sn->oob_min.X);
        *az = sn->oob_min.Z + HSD_Randf() * (sn->oob_max.Z - sn->oob_min.Z);
        return;
    }

    GOBJ *riders[5];
    int count = 0;
    for (int i = 0; i < 5; i++)
    {
        GOBJ *rg = Ply_GetRiderGObj(i);
        if (rg)
            riders[count++] = rg;
    }

    float cx = 0.0f, cz = 0.0f;
    if (count > 0)
    {
        RiderData *rd = (RiderData *)riders[HSD_Randi(count)]->userdata;
        if (rd)
        {
            cx = rd->pos.X;
            cz = rd->pos.Z;
        }
    }

    *ax = cx + Randf2() * BOLT_SPREAD;
    *az = cz + Randf2() * BOLT_SPREAD;
}

static void SetLightColor(LOBJ *l, u8 r, u8 g, u8 b)
{
    if (!l)
        return;
    l->color.r = r;
    l->color.g = g;
    l->color.b = b;
    l->color.a = 0xFF;
    l->hw_color = l->color;
}

static void SetLightColorScaled(LOBJ *l, GXColor c, float s)
{
    SetLightColor(l, (u8)(c.r * s), (u8)(c.g * s), (u8)(c.b * s));
}

static void EnsureFlashLight(void)
{
    if (s_flash_lobj)
        return;
    GOBJ *gobj = GObj_Create(38, 32, 0);
    if (!gobj)
        return;
    s_flash_lobj = LObj_LoadDesc(&s_flash_lobj_desc);
    GObj_AddObject(gobj, HSD_OBJKIND_LOBJ, s_flash_lobj);
    GObj_AddGXLink(gobj, LObj_GX, 0, 0);
}

static void EnsureBoltLight(void)
{
    if (s_bolt_lobj)
        return;
    GOBJ *gobj = GObj_Create(38, 32, 0);
    if (!gobj)
        return;
    s_bolt_lobj = LObj_LoadDesc(&s_bolt_lobj_desc);
    GObj_AddObject(gobj, HSD_OBJKIND_LOBJ, s_bolt_lobj);
    GObj_AddGXLink(gobj, LObj_GX, 0, 0);
}

// Build a fresh jagged bolt near the stashed camera focus and move the midpoint
// point light onto it. Called once when a strike fires.
static void GenerateBolt(void)
{
    float x, z;
    StrikeAnchor(&x, &z);
    float y = BOLT_TOP_Y;
    float dy = (BOLT_TOP_Y - BOLT_GROUND_Y) / (float)BOLT_SEGMENTS;

    Vec3 prev = { x, y, z };
    Vec3 fork_origin = prev;
    int mid = BOLT_SEGMENTS / 2;
    int fork_at = BOLT_SEGMENTS / 3;
    int n = 0;

    for (int i = 1; i <= BOLT_SEGMENTS; i++)
    {
        y -= dy;
        x += Randf2() * BOLT_JITTER;
        z += Randf2() * BOLT_JITTER;
        Vec3 cur = { x, y, z };
        s_seg_a[n] = prev;
        s_seg_b[n] = cur;
        n++;
        if (i == mid)
            s_bolt_mid = cur;
        if (i == fork_at)
            fork_origin = cur;
        prev = cur;
    }

    // One offshoot branching from the upper third, veering sideways as it falls.
    float fx = fork_origin.X, fy = fork_origin.Y, fz = fork_origin.Z;
    float fdy = dy * 1.1f;
    Vec3 fprev = fork_origin;
    for (int i = 0; i < BOLT_FORK_SEGS && n < BOLT_MAX_SEG; i++)
    {
        fy -= fdy;
        fx += Randf2() * BOLT_FORK_JITTER + 25.0f;
        fz += Randf2() * BOLT_FORK_JITTER;
        Vec3 fcur = { fx, fy, fz };
        s_seg_a[n] = fprev;
        s_seg_b[n] = fcur;
        n++;
        fprev = fcur;
    }
    s_seg_count = n;

    s_bolt_pos_desc.pos = s_bolt_mid;
    if (s_bolt_lobj && s_bolt_lobj->position)
        s_bolt_lobj->position->pos = s_bolt_mid;
}

// Draw the bolt as GX line segments. Flat per-vertex color, additive blend so
// the core glows, depth-tested but not depth-writing so stage geometry occludes
// the bolt (same occlusion rule as the rain). Mirrors rain.c's GX setup.
static void DrawBoltPass(COBJ *cam, GXColor col, int width, u8 alpha)
{
    HSD_StateInitDirect(GX_VTXFMT0, 2);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetNumTexGens(0);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0, GX_DISABLE, Vertex, Vertex, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetChanCtrl(GX_ALPHA0, GX_DISABLE, Vertex, Vertex, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR); // additive
    GXSetZMode(GX_ENABLE, GX_LEQUAL, GX_DISABLE);
    GXSetCullMode(GX_CULL_NONE);
    GXSetLineWidth((u8)width, 5);
    GXLoadPosMtxImm(&cam->view_mtx, GX_PNMTX0);

    GXBegin(GX_LINES, GX_VTXFMT0, s_seg_count * 2);
    for (int i = 0; i < s_seg_count; i++)
    {
        GXPosition3f32(s_seg_a[i].X, s_seg_a[i].Y, s_seg_a[i].Z);
        GXColor4u8(col.r, col.g, col.b, alpha);
        GXPosition3f32(s_seg_b[i].X, s_seg_b[i].Y, s_seg_b[i].Z);
        GXColor4u8(col.r, col.g, col.b, alpha);
    }
    HSD_StateInvalidate(-1);
}

static const GXColor s_bolt_core = {255, 255, 255, 255}; // white-hot core

static void Bolt_GX(GOBJ *g, int pass)
{
    (void)g;
    if (pass != 1)
        return;

    COBJ *cam = COBJ_GetCurrent();
    if (!cam)
        return;

    if (!stc_active || s_flash_frames <= 0 || s_seg_count <= 0)
        return;
    if (EffectiveBoltMode() == LTNG_BOLT_OFF)
        return;

    float bright = FlashBrightness();
    if (bright <= 0.0f)
        return;

    // Wide dim glow, then a thin white-hot core, both strobing with the flash.
    DrawBoltPass(cam, stc_bolt_color, BOLT_GLOW_WIDTH, (u8)(stc_bolt_color.a * BOLT_GLOW_ALPHA * bright));
    DrawBoltPass(cam, s_bolt_core, BOLT_CORE_WIDTH, (u8)(255.0f * bright));
}

static void EnsureBoltRender(void)
{
    if (s_bolt_render)
        return;
    GOBJ *g = GObj_Create(BOLT_GOBJ_CLASS, BOLT_GOBJ_PLINK, 0);
    if (!g)
        return;
    GObj_AddGXLink(g, Bolt_GX, BOLT_GX_LINK, BOLT_GX_PRI);
    s_bolt_render = g;
    OSReport("[Lightning] Bolt render layer installed\n");
}

// Latch the active preset's lightning config, resolving each 0 field to its
// module default. NULL or def->enabled == 0 turns lightning off. Resets the
// strike timers so the first strike fires a short beat after the preset starts.
void Lightning_SetActive(const LightningDef *def)
{
    if (!def || !def->enabled)
    {
        stc_active = 0;
        return;
    }
    stc_active = 1;

    stc_flash_color = GXColor_Unpack(def->flash_color ? def->flash_color : LTNG_DEF_FLASH_COLOR);
    // The bolt glow inherits the preset's flash color (red for Blood Rain,
    // near-white for Storm); the core is always white-hot.
    stc_bolt_color = GXColor_Unpack(def->flash_color ? def->flash_color : BOLT_DEF_COLOR);
    stc_flash_len = def->flash_frames > 0 ? def->flash_frames : LTNG_DEF_FLASH_FRAMES;
    stc_min_lull = def->min_lull > 0 ? def->min_lull : LTNG_DEF_MIN_LULL;
    stc_max_lull = def->max_lull > 0 ? def->max_lull : LTNG_DEF_MAX_LULL;
    if (stc_max_lull < stc_min_lull)
        stc_max_lull = stc_min_lull;
    stc_preset_bolt = def->bolt;

    s_lull_frames = LTNG_INITIAL_LULL;
    s_flash_frames = 0;
}

// Lerp the per-frame fog/EFB color toward the flash color by `bright` and pull
// the fog wall in so the brightness reaches near terrain. This is what lights
// the (LOBJ-blind) stage geometry on a strike.
static void ApplyScreenFlash(HSD_Fog *fog, float bright)
{
    u8 fr = stc_flash_color.r, fg = stc_flash_color.g, fb = stc_flash_color.b;

    if (fog)
    {
        u8 br = fog->color.r, bg = fog->color.g, bb = fog->color.b;
        fog->color.r = (u8)(br + (int)((fr - br) * bright));
        fog->color.g = (u8)(bg + (int)((fg - bg) * bright));
        fog->color.b = (u8)(bb + (int)((fb - bb) * bright));
        fog->start = fog->start * (1.0f - bright);
    }

    u32 efb_base = *stc_global_fog_color;
    u8 br = (efb_base >> 24) & 0xFF;
    u8 bg = (efb_base >> 16) & 0xFF;
    u8 bb = (efb_base >> 8) & 0xFF;
    u8 ba = efb_base & 0xFF;
    u8 nr = (u8)(br + (int)((fr - br) * bright));
    u8 ng = (u8)(bg + (int)((fg - bg) * bright));
    u8 nb = (u8)(bb + (int)((fb - bb) * bright));
    *stc_global_fog_color = RGBA(nr, ng, nb, ba);
}

// Per-frame strike driver. `fog` is the active HSD_Fog (already written this
// frame by Sky_Update); we lerp it toward the flash color during a strike.
void Lightning_Tick(HSD_Fog *fog)
{
    if (!stc_active)
        return;
    EnsureFlashLight();
    EnsureBoltLight();
    EnsureBoltRender();

    int mode = EffectiveBoltMode();

    if (s_flash_frames > 0)
    {
        float bright = FlashBrightness();

        // Screen flash + global rider light, unless a bolt fully replaces them.
        if (mode != LTNG_BOLT_REPLACE)
        {
            ApplyScreenFlash(fog, bright);
            SetLightColorScaled(s_flash_lobj, stc_flash_color, bright);
        }
        else
        {
            SetLightColor(s_flash_lobj, 0, 0, 0);
        }

        // Localized point light at the bolt midpoint pulses with the bolt.
        if (mode != LTNG_BOLT_OFF)
            SetLightColorScaled(s_bolt_lobj, stc_bolt_color, bright);
        else
            SetLightColor(s_bolt_lobj, 0, 0, 0);

        s_flash_frames--;
    }
    else
    {
        SetLightColor(s_flash_lobj, 0, 0, 0);
        SetLightColor(s_bolt_lobj, 0, 0, 0);
        s_lull_frames--;
        if (s_lull_frames <= 0)
        {
            s_flash_frames = stc_flash_len;
            int span = stc_max_lull - stc_min_lull;
            s_lull_frames = stc_min_lull + (span > 0 ? HSD_Randi(span) : 0);
            GenerateBolt();
            OSReport("[Lightning] Strike (next in %d frames)\n", s_lull_frames);
        }
    }
}

void Lightning_Reset(void)
{
    // The engine frees every world GObj on scene teardown; drop our cached
    // handles so the next active frame recreates them, and re-arm the timer.
    s_flash_lobj = 0;
    s_bolt_lobj = 0;
    s_bolt_render = 0;
    s_seg_count = 0;
    s_lull_frames = LTNG_INITIAL_LULL;
    s_flash_frames = 0;
}

MenuDesc lightning_menu = {
    .option_num = 1,
    .options = {
        &(OptionDesc){
            .name = "Lightning Bolts",
            .description = "Visible bolts in storms: Auto = per-preset, Off = flash only, Force = bolts on every lightning preset",
            .kind = OPTKIND_VALUE,
            .val = &bolt_override_index,
            .value_num = 3,
            .value_names = bolt_override_names,
        },
    },
};
