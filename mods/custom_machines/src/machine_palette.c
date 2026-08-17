// Wall-clock material cycle for a drop-in machine whose descriptor asks for one.
//
// The archive cannot animate this itself. A MatAnim's frame is the machine's
// state - the moving animation's rate rides on velocity, the charge animation's
// frame is the charge gauge - and every one of them restarts when the state
// changes, so nothing baked into the .dat advances with elapsed time. The
// descriptor therefore ships the palette and the joint, and the color is written
// into the live materials here.
//
// It reaches a pixel only where the joint's materials render with
// RENDER_CONSTANT and their texture stages pass the color through or modulate
// it; MObjMakeTExp (0x803fa0b4) hands the material's diffuse to the first TEV
// stage only in that case, and TObjMakeTExp (0x803f6860) lets a REPLACE or a
// full-strength BLEND stage overwrite it afterwards. MObjLoad copies the
// material per instance, so every machine on the field is written separately.

#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "machine.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

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
        OSReport("[MachinePalette] %s: joint %d cycles %d colors every %.1fs\n",
                 e->name, t->joint, t->count, e->palette_period);
    }

    if (stc_target_count == 0)
        return;

    CODEPATCH_REPLACECALL(0x801c6274, MachinePalette_AnimThinkTail);
    OSReport("[MachinePalette] Hooks installed\n");
}
