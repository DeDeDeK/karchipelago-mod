#include "game.h"
#include "os.h"
#include "obj.h"
#include "machine.h"
#include "inline.h"
#include "code_patch/code_patch.h"

#include "event_drive_backwards.h"

static int active;

// Steering, thrust, grip and collision all work along MachineData.forward. Reversing
// the basis for just the procs that run them drives the machine tail-first, while the
// model, hitboxes and camera, read outside those procs, keep the nose where it was.
static void FlipHeading(MachineData *md)
{
    md->forward = (Vec3){-md->forward.X, -md->forward.Y, -md->forward.Z};
    md->left = (Vec3){-md->left.X, -md->left.Y, -md->left.Z};
}

static void RunFlipped(GOBJ *gobj, void (*proc)(GOBJ *))
{
    MachineData *md = gobj->userdata;
    int flip = active;

    if (flip)
        FlipHeading(md);
    proc(gobj);
    if (flip)
        FlipHeading(md);
}

static void FrameStartThink(GOBJ *gobj) { RunFlipped(gobj, Machine_FrameStartThink); }
static void AnimThink(GOBJ *gobj) { RunFlipped(gobj, MachineGObj_AnimThink); }
static void InputThink(GOBJ *gobj) { RunFlipped(gobj, Machine_InputThink); }
static void PhysicsThink(GOBJ *gobj) { RunFlipped(gobj, Machine_PhysicsThink); }
static void EnvCollThink(GOBJ *gobj) { RunFlipped(gobj, Machine_EnvCollThink); }

// The lis/addi pairs in Machine_Create (0x801c552c) that load its procs at priorities
// 0-5, everything before Machine_ModelMatrixThink.
static const struct
{
    int lis;
    int addi;
    void (*proc)(GOBJ *);
} proc_loads[] = {
    {0x801c568c, 0x801c5694, FrameStartThink},
    {0x801c56a0, 0x801c56a8, AnimThink},
    {0x801c56b4, 0x801c56bc, InputThink},
    {0x801c56c8, 0x801c56d0, PhysicsThink},
    {0x801c56dc, 0x801c56e4, EnvCollThink},
};

void DriveBackwards_Start(void)
{
    active = 1;
    OSReport("[DriveBackwards] Started, machines drive tail-first\n");
}

void DriveBackwards_Stop(void)
{
    if (!active)
        return;
    active = 0;
    OSReport("[DriveBackwards] Machines drive forward again\n");
}

void DriveBackwards_InstallHooks(void)
{
    for (int i = 0; i < GetElementsIn(proc_loads); i++)
        CODEPATCH_REPLACEADDRESS(proc_loads[i].lis, proc_loads[i].addi, proc_loads[i].proc);
}
