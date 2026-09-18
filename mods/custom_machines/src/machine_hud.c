// Where the engine draws or places a machine by its kind outside the machine itself: the
// City Trial field blip and how high it rides, the stadium HUD's machine icon, and how far
// the stat radar screen lowers the machine model. Each reads a vanilla per-kind table with
// no bound, so a registered machine is answered from its registry index and descriptor.
// Custom machine n shows frame 20 + n in both HUD art banks.

#include "os.h"
#include "obj.h"
#include "game.h"
#include "menu.h"
#include "machine.h"
#include "code_patch/code_patch.h"

#include "custom_machines.h"

#define APPENDED_FRAME_BASE 20
#define BLIP_ARCHIVE        "IfAll2c"

#define PPC_MR_R22_R3 0x7c761b78 // mr r22, r3
#define PPC_NOP       0x60000000

// CityBlip_Create (0x801226e8) poses one template joint per MachineKind 0..25 on its frame
// of the blip TexAnim, and CityBlip_GXCallback (0x80122380) instances the template at
// Machine_GetAbsoluteKind. That fold lands an appended class slot on another kind, and the
// template array runs straight into the per-player instance joints, so a custom machine's
// template is built and held here.
static JOBJ *stc_blip_template[CUSTOM_MACHINE_MAX];

// Replaces the bl Machine_GetAbsoluteKind that indexes the template array.
static JOBJ *BlipTemplate(GOBJ *machine_gobj)
{
    MachineData *md = machine_gobj->userdata;
    CustomMachineEntry *e = CustomMachines_FindByClassSlot(md->is_bike, md->kind);

    if (e != NULL)
        return stc_blip_template[CustomMachines_Index(e)];
    return Gm_Get3dData()->blip_template[Machine_GetAbsoluteKind(machine_gobj)];
}

// CityBlip_Create past its template loop, built the same way. A bank that did not grow
// would pose the joint on a vanilla machine's frame, so the machine gets no blip instead.
// The array is cleared first, so a scene torn down without CityBlip_Destroy leaves no
// joint off its freed heap behind.
static void BuildCustomBlips(void)
{
    JOBJSet *set = Gm_Get3dData()->blip_scene_models[0];

    for (int i = 0; i < CUSTOM_MACHINE_MAX; i++)
        stc_blip_template[i] = NULL;
    if (!CustomMachineUiFrames_IsGrown(BLIP_ARCHIVE))
        return;

    for (int i = 0; i < CustomMachines_GetCount(); i++)
    {
        JOBJ *j = JObj_LoadJoint(set->jobj);

        JObj_AddSetAnim0_SetFrameAndRate(j, set, APPENDED_FRAME_BASE + i, 0.0f);
        JObj_ClearFlags(j, JOBJ_ROOT_OPA | JOBJ_ROOT_XLU | JOBJ_ROOT_TEXEDGE);
        JObj_SetAllMOBJFlags(j, RENDER_ZMODE_ALWAYS | RENDER_NO_ZUPDATE);
        stc_blip_template[i] = j;
    }
}
CODEPATCH_HOOKCREATE(0x80122840, "", BuildCustomBlips, "", 0)

// CityBlip_Destroy's tail, once the vanilla templates and every instance are gone.
static void FreeCustomBlips(void)
{
    for (int n = 0; n < CUSTOM_MACHINE_MAX; n++)
    {
        if (stc_blip_template[n] != NULL)
            JObj_Remove(stc_blip_template[n]);
        stc_blip_template[n] = NULL;
    }
}
CODEPATCH_HOOKCREATE(0x80122b20, "", FreeCustomBlips, "", 0)

// Replaces CityBlip_GetMachineLift (0x800096b8).
static float BlipLift(int is_bike, int class_slot)
{
    CustomMachineEntry *e = CustomMachines_FindByClassSlot(is_bike, class_slot);
    float lift = e != NULL ? e->blip_height
                           : (is_bike ? stc_blip_lift_bike : stc_blip_lift_star)[class_slot];

    return lift * *stc_blip_lift_scale;
}

// Replaces Machine_GetIconFrame (0x8011584c), whose vanilla table puts the bike slots at
// 12 and up, where an appended star slot would also land.
static int IconFrame(int is_bike, int class_slot)
{
    is_bike = (s8)is_bike;
    class_slot = (s8)class_slot;

    CustomMachineEntry *e = CustomMachines_FindByClassSlot(is_bike, class_slot);
    if (e != NULL)
        return APPENDED_FRAME_BASE + CustomMachines_Index(e);
    return stc_machine_icon_frame[is_bike * MACHINE_ICON_FRAME_BIKE_BASE + class_slot];
}

// MnRadar_PlaceMachines (0x80045e14), on the scale load past its table read: r30 is the
// player and f2 the value the engine read, which a custom machine replaces with its own.
static float RadarDrop(int ply, float vanilla)
{
    GameData *gd = Gm_GetGameData();
    CustomMachineEntry *e = CustomMachines_FindByClassSlot(gd->city.is_bike[ply],
                                                           gd->city.machine_kind[ply]);
    return e != NULL ? e->radar_drop : vanilla;
}

CODEPATCH_HOOKCREATE(0x80045ffc,
    "mr 3, 30\n\t"
    "fmr 1, 2\n\t",
    RadarDrop,
    "fmr 2, 1\n\t",
    0
)

void CustomMachineHud_OnBoot(void)
{
    CODEPATCH_REPLACECALL(0x80122414, BlipTemplate); // CityBlip_GXCallback
    CODEPATCH_REPLACEINSTRUCTION(0x80122418, PPC_MR_R22_R3);
    CODEPATCH_REPLACEINSTRUCTION(0x8012241c, PPC_NOP);
    CODEPATCH_REPLACEINSTRUCTION(0x80122420, PPC_NOP);
    CODEPATCH_HOOKAPPLY(0x80122840); // CityBlip_Create, past the template loop
    CODEPATCH_HOOKAPPLY(0x80122b20); // CityBlip_Destroy tail
    CODEPATCH_REPLACEFUNC(CityBlip_GetMachineLift, BlipLift);
    CODEPATCH_REPLACEFUNC(Machine_GetIconFrame, IconFrame);
    CODEPATCH_HOOKAPPLY(0x80045ffc); // MnRadar_PlaceMachines

    OSReport("[MachineHud] Blip, stadium icon and radar placement redirected for %d machine(s)\n",
             CustomMachines_GetCount());
}
