// Wall-clock material cycle for a machine whose descriptor asks for one, written
// into the live materials from Machine_ColAnimThink each frame. An archive cannot
// animate this itself: a MatAnim's frame is the machine's state, not elapsed time.
// The same color is written over the exhaust generators' color operands, which
// paints the particles born that frame and leaves those in flight alone.

#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "particle.h"
#include "machine.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

// The particle bank EfPtclVehicle.dat installs, which is what a machine's
// animation bank names its exhaust out of.
#define PTCL_BANK_VEHICLE 0

typedef struct PaletteTarget
{
    int star_slot;          // MachineData.kind is class-relative, so match on this
    int joint;
    int count;
    const u32 *palette;
    float phase;            // 0..1 around the palette
    float phase_per_tick;
    u32 last_tick;
    int running;
    int trail_count;        // 0 if this machine tints no trail
    u8 trail_gen[8];
    u16 trail_rgb[8];
} PaletteTarget;

static PaletteTarget stc_targets[CUSTOM_MACHINE_MAX];
static int stc_target_count;

static PaletteTarget *TargetFor(MachineData *md)
{
    if (md->is_bike)
        return NULL;
    for (int i = 0; i < stc_target_count; i++)
    {
        if (stc_targets[i].star_slot == md->kind)
            return &stc_targets[i];
    }
    return NULL;
}

// Advances on the elapsed time-base delta rather than a frame count, so the
// cycle holds its period through slowdown and pauses. Unsigned subtraction
// carries the tick counter's wrap. Several machines of the same kind in one
// frame share the advance: the first sees the whole delta and the rest see none.
static void Advance(PaletteTarget *t)
{
    u32 now = (u32)OSGetTick();

    if (!t->running)
    {
        t->running = 1;
        t->last_tick = now;
        return;
    }
    // A gap of a whole cycle or more - no machine of this kind was on the field,
    // or the tick counter wrapped - carries no information about where the cycle
    // should be, so it resumes where it left off instead.
    float step = (float)(now - t->last_tick) * t->phase_per_tick;
    t->last_tick = now;
    if (step >= 1.0f)
        return;

    t->phase += step;
    if (t->phase >= 1.0f)
        t->phase -= 1.0f;
}

static u8 Mix(u32 from, u32 to, float f)
{
    return (u8)((float)from + ((float)to - (float)from) * f);
}

static void PaletteColor(PaletteTarget *t, GXColor *out)
{
    float walk = t->phase * (float)t->count;
    int i = (int)walk;
    if (i >= t->count)
        i = t->count - 1;

    // Ease the crossfade so each color holds before it gives way, instead of the
    // whole cycle sitting in the muddy blend between two of them.
    float f = walk - (float)i;
    f = f * f * (3.0f - 2.0f * f);

    u32 from = t->palette[i];
    u32 to = t->palette[(i + 1) % t->count];
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

// The bank is rebuilt on every 3D load, so descriptors are re-resolved rather than
// cached. An offset is only written when the two bytes ahead of it are still a color
// opcode and its duration operand, which keeps this off a stale table.
static void TintTrail(const PaletteTarget *t, const GXColor *color)
{
    u8 **descs;
    GXColor tint;

    if (t->trail_count == 0)
        return;

    descs = psGeneratorDesc[PTCL_BANK_VEHICLE];
    if (descs == NULL)
        return;

    tint = *color;
    Saturate(&tint);

    for (int i = 0; i < t->trail_count; i++)
    {
        u8 *desc;
        u8 *rgb;

        if ((u32)t->trail_gen[i] >= psGeneratorCount[PTCL_BANK_VEHICLE])
            continue;
        desc = descs[t->trail_gen[i]];
        if (desc == NULL)
            continue;

        rgb = desc + t->trail_rgb[i];
        if ((rgb[-2] & 0xF0) != 0xC0 && (rgb[-2] & 0xF0) != 0xD0)
            continue;
        rgb[0] = tint.r;
        rgb[1] = tint.g;
        rgb[2] = tint.b;
    }
}

static void PaintMachine(MachineData *md)
{
    PaletteTarget *t = TargetFor(md);
    if (t == NULL)
        return;

    JOBJ *joint = CustomMachines_GetMachineJoint(md, t->joint);
    if (joint == NULL)
        return;

    Advance(t);

    GXColor color;
    PaletteColor(t, &color);
    for (DOBJ *dobj = joint->dobj; dobj != NULL; dobj = dobj->next)
    {
        if (dobj->mobj != NULL && dobj->mobj->mat != NULL)
            dobj->mobj->mat->diffuse = color;
    }
    TintTrail(t, &color);
}

// Tail of Machine_AnimThink, which runs once per machine per frame. Taking it
// here puts the write after the ColAnim overlays and before the draw.
static void MachinePalette_AnimThinkTail(MachineData *md)
{
    Machine_ColAnimThink(md);
    PaintMachine(md);
}

void CustomMachinePalette_OnBoot(void)
{
    u32 ticks_per_second = os_info->bus_clock / 4;

    for (int i = 0; i < CustomMachines_GetCount(); i++)
    {
        CustomMachineEntry *e = CustomMachines_GetEntry(i);
        if (e->palette_joint < 0 || stc_target_count >= CUSTOM_MACHINE_MAX)
            continue;

        PaletteTarget *t = &stc_targets[stc_target_count++];
        t->star_slot = e->star_slot;
        t->joint = e->palette_joint;
        t->count = e->palette_count;
        t->palette = e->palette;
        t->phase_per_tick = 1.0f / (e->palette_period * (float)ticks_per_second);
        t->trail_count = e->trail_count;
        for (int k = 0; k < e->trail_count; k++)
        {
            t->trail_gen[k] = e->trail_gen[k];
            t->trail_rgb[k] = e->trail_rgb[k];
        }
        OSReport("[MachinePalette] %s: joint %d cycles %d colors every %.1fs, %d trail tints\n",
                 e->name, t->joint, t->count, e->palette_period, t->trail_count);
    }

    if (stc_target_count == 0)
        return;

    CODEPATCH_REPLACECALL(0x801c6274, MachinePalette_AnimThinkTail);
    OSReport("[MachinePalette] %d machine(s) cycling, hooks installed\n", stc_target_count);
}
