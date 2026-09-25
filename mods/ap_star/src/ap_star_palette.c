// An archive cannot animate this itself: a MatAnim's frame is the machine's state, not
// elapsed time. The same color is written over the exhaust generators' color operands,
// so it paints the particles born that frame and leaves those in flight alone.

#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "machine.h"

#include "ap_star.h"
#include "ap_star_palette.h"

#define PLATFORM_JOINT 6     // body root, whose DObjs are the platform disc
#define CYCLE_PERIOD   12.0f // seconds for one pass through the six colors

// The cruise and boost generators share one program layout. These are its color
// opcodes' RGB operands: the two colors a particle spawns holding, the one it ramps up
// to over its first three frames, and the one it fades out through.
#define TRAIL_GEN_NUM 2
#define TRAIL_RGB_NUM 4
static const u16 stc_trail_rgb[TRAIL_RGB_NUM] = { 0x53, 0x5a, 0x60, 0x89 };

// A color opcode's operands start past the opcode and its duration byte.
#define PTCL_COLOR_OPERAND_SKIP 2

static int stc_bound;
static float stc_phase;       // 0..1 around the palette
static float stc_phase_per_tick;
static u32 stc_last_tick;
static int stc_running;
static int stc_trail_num;
static u8 *stc_trail[TRAIL_GEN_NUM * TRAIL_RGB_NUM];

// Advances on the elapsed time-base delta rather than a frame count, so the cycle
// holds its period through slowdown and pauses. Unsigned subtraction carries the tick
// counter's wrap. Several stars in one frame share the advance: the first sees the
// whole delta and the rest see none.
static void Advance(void)
{
    u32 now = (u32)OSGetTick();

    if (!stc_running)
    {
        stc_running = 1;
        stc_last_tick = now;
        return;
    }
    // A gap of a whole cycle or more - no star was on the field, or the tick counter
    // wrapped - carries no information about where the cycle should be, so it resumes
    // where it left off instead.
    float step = (float)(now - stc_last_tick) * stc_phase_per_tick;
    stc_last_tick = now;
    if (step >= 1.0f)
        return;

    stc_phase += step;
    if (stc_phase >= 1.0f)
        stc_phase -= 1.0f;
}

static u8 Mix(u32 from, u32 to, float f)
{
    return (u8)((float)from + ((float)to - (float)from) * f);
}

static void PaletteColor(GXColor *out)
{
    float walk = stc_phase * (float)APSTARPIECE_NUM;
    int i = (int)walk;
    if (i >= APSTARPIECE_NUM)
        i = APSTARPIECE_NUM - 1;

    // Ease the crossfade so each color holds before it gives way, instead of the
    // whole cycle sitting in the muddy blend between two of them.
    float f = walk - (float)i;
    f = f * f * (3.0f - 2.0f * f);

    u32 from = ap_star_piece_colors[i];
    u32 to = ap_star_piece_colors[(i + 1) % APSTARPIECE_NUM];
    out->r = Mix((from >> 16) & 0xFF, (to >> 16) & 0xFF, f);
    out->g = Mix((from >> 8) & 0xFF, (to >> 8) & 0xFF, f);
    out->b = Mix(from & 0xFF, to & 0xFF, f);
    out->a = 0xFF;
}

// Particles blend additively, so where a trail overlaps itself the channels sum and
// clamp and a pastel color reaches that sum as white. Stretching to full saturation
// gives up the lightness the blend would have destroyed and keeps the channel ratio.
static void Saturate(GXColor *c)
{
    u8 lo = c->r, hi = c->r;

    if (c->g < lo) lo = c->g;
    if (c->b < lo) lo = c->b;
    if (c->g > hi) hi = c->g;
    if (c->b > hi) hi = c->b;
    if (hi == lo)
        return;

    c->r = (u8)((c->r - lo) * hi / (hi - lo));
    c->g = (u8)((c->g - lo) * hi / (hi - lo));
    c->b = (u8)((c->b - lo) * hi / (hi - lo));
}

static void TintTrail(const GXColor *color)
{
    GXColor tint = *color;

    Saturate(&tint);
    for (int i = 0; i < stc_trail_num; i++)
    {
        stc_trail[i][0] = tint.r;
        stc_trail[i][1] = tint.g;
        stc_trail[i][2] = tint.b;
    }
}

static void OnStarAnim(MachineData *md)
{
    JOBJ *joint = cm_api->GetMachineJoint(md, PLATFORM_JOINT);
    if (joint == NULL)
        return;

    Advance();

    GXColor color;
    PaletteColor(&color);
    for (DOBJ *dobj = joint->dobj; dobj != NULL; dobj = dobj->next)
    {
        if (dobj->mobj != NULL && dobj->mobj->mat != NULL)
            dobj->mobj->mat->diffuse = color;
    }
    TintTrail(&color);
}

// The generators are the registry's copies, which outlive every scene, so each operand
// is resolved once. One that does not land on a color opcode's operands is left alone.
static void BindTrail(int kind)
{
    for (int g = 0; g < TRAIL_GEN_NUM; g++)
    {
        int size = 0;
        u8 *gen = cm_api->GetGenerator(kind, g, &size);

        for (int k = 0; k < TRAIL_RGB_NUM; k++)
        {
            int rgb = stc_trail_rgb[k];
            int op = -1;

            if (gen != NULL && rgb + 3 <= size)
                op = gen[rgb - PTCL_COLOR_OPERAND_SKIP] & 0xF0;
            if (op != 0xC0 && op != 0xD0)
            {
                OSReport("[ApStarPalette] Generator %d +0x%x is not a color operand, left untinted\n",
                         g, rgb);
                continue;
            }
            stc_trail[stc_trail_num++] = gen + rgb;
        }
    }
}

// Idempotent. custom_machines exports after this mod boots, and a registered kind is
// fixed for the run once it has, so the first scene change that finds the star binds it.
void ApStarPalette_OnSceneChange(void)
{
    if (stc_bound)
        return;

    int kind = ApStar_MachineKind();
    if (kind < 0 || !cm_api->SetAnimHandler(kind, OnStarAnim))
        return;

    stc_bound = 1;
    stc_phase_per_tick = 1.0f / (CYCLE_PERIOD * (float)(os_info->bus_clock / 4));
    BindTrail(kind);
    OSReport("[ApStarPalette] Platform cycling with %d trail operand(s), handler installed\n",
             stc_trail_num);
}
