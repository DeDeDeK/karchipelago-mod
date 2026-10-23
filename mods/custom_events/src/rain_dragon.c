#include <math.h>
#include <string.h>

#include "game.h"
#include "hsd.h"
#include "obj.h"
#include "stage.h"
#include "yakumono.h"
#include "collision.h"
#include "trigger.h"
#include "inline.h"

#include "custom_events.h"
#include "rain_dragon.h"

#define DRAGON_GOBJ_CLASS   215
#define DRAGON_GX_PRI       2     // after Map_GX (1), whose backdrop draws with no depth test
#define DRAGON_PROC_PRI     4     // before YakumonoGObj_Proc7 (7) places the hitbox on the bones
#define DRAGON_BODY_JOINTS  7     // the root and the six bones the body is skinned to
#define DRAGON_REGION_NUM   2
#define DRAGON_FALL_SPEED   3.0f
#define DRAGON_WIGGLE_END   220.0f // the main motion's first leap, which the body wiggle loops over
#define DRAGON_BODY_ROT_Z   (-M_PI_2) // the body runs along its local +X, head first

typedef struct RainDragonSlot
{
    GOBJ *gobj;     // the model, NULL = free
    GOBJ *yaku;     // the hitbox
    JOBJ *head;     // the bone region 0 follows
    Vec3 head_prev;
    float frame;    // of the body wiggle
} RainDragonSlot;

static RainDragonSlot dragons[RAIN_DRAGON_MAX];

// Latched from a vanilla dragon by RainDragon_Prepare.
static YakuLavaDragonDesc *dragon_desc;
static int dragon_data_idx; // its YakumonoTable slot, lent for GrYaku_Create alone
static HurtParams region_params[DRAGON_REGION_NUM];
static int region_bone[DRAGON_REGION_NUM]; // JObj_GetIndex under the clone's root

// No model, no audio: the hitbox yakumono only carries the HurtData.
static YakuLavaDragonParam hitbox_param;

// The clone's root holds the stage scale the dragon is drawn at and its fall; its
// child is the vanilla dragon's body.
static JOBJDesc root_desc = {
    .flags = JOBJ_CLASSICAL_SCALING | JOBJ_ROOT_MASK,
};

static RainDragonSlot *RainDragon_FindSlot(GOBJ *gobj)
{
    for (int i = 0; i < RAIN_DRAGON_MAX; i++)
    {
        if (dragons[i].gobj == gobj)
            return &dragons[i];
    }
    return NULL;
}

static void RainDragon_Remove(RainDragonSlot *s)
{
    GOBJ *g = s->gobj;
    GObj_Destroy(s->yaku);
    *s = (RainDragonSlot){0};
    GObj_Destroy(g);
}

static void RainDragon_Think(GOBJ *gobj)
{
    RainDragonSlot *s = RainDragon_FindSlot(gobj);
    if (!s)
        return;

    JOBJ *root = gobj->hsd_object;
    root->trans.Y -= DRAGON_FALL_SPEED;
    JObj_SetMtxDirtySub(root);

    // The motion's AObjs do not loop.
    JOBJ *body = root->child;
    s->frame += 1.0f;
    if (s->frame >= DRAGON_WIGGLE_END)
    {
        s->frame = 0.0f;
        JObj_ReqAnimAll(body, 0.0f);
    }
    JObj_AnimAll(body);

    Vec3 head, hit;
    JObj_GetWorldPosition(s->head, NULL, &head);
    if (Raycast_Ground(&s->head_prev, &head, &hit) >= 0)
    {
        RainDragon_Remove(s);
        return;
    }
    s->head_prev = head;
    Yaku_GetData(s->yaku)->pos = head;
}

// A vanilla dragon's two regions as its motion script last armed them, each on one of
// its body's stage joints.
static int RainDragon_CopyHitbox(YakumonoData *yd)
{
    YakuLavaDragonDesc *desc = ((YakuLavaDragonParam *)yd->data_ptr)->desc;
    HurtData *hd = yd->hurt_data;
    if (!desc || !hd || hd->region_count < DRAGON_REGION_NUM)
        return 0;

    for (int i = 0; i < DRAGON_REGION_NUM; i++)
    {
        HitRegion *r = &hd->regions[i];
        int bone = -1;
        for (int j = 0; j < DRAGON_BODY_JOINTS && r->jobj; j++)
        {
            if (Gr_GetJoint(desc->joint + j) == r->jobj)
                bone = j;
        }
        if (bone < 0 || r->params.base_damage == 0)
            return 0;

        region_params[i] = r->params;
        region_bone[i] = bone + 1;
    }
    dragon_desc = desc;
    return 1;
}

// GrYaku_Create took the dragon's data_ptr from this slot.
static int RainDragon_FindDataIdx(YakumonoTable *table, YakumonoData *yd)
{
    for (int i = 0; i < table->data_count; i++)
    {
        if (table->data_array[i] == yd->data_ptr)
            return i;
    }
    return -1;
}

int RainDragon_Prepare(void)
{
    memset(dragons, 0, sizeof(dragons));
    dragon_desc = NULL;

    YakumonoTable *table = (*stc_grobj)->gr_data->yakumono;
    if (!table)
        return 0;

    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_YAKUMONO]; g; g = g->next)
    {
        YakumonoData *yd = Yaku_GetData(g);
        if (yd->kind != YAKUKIND_LAVADRAGON)
            continue;

        dragon_data_idx = RainDragon_FindDataIdx(table, yd);
        if (dragon_data_idx >= 0 && RainDragon_CopyHitbox(yd))
            return 1;
    }
    return 0;
}

int RainDragon_Count(void)
{
    int num = 0;
    for (int i = 0; i < RAIN_DRAGON_MAX; i++)
        num += dragons[i].gobj != NULL;
    return num;
}

int RainDragon_Spawn(Vec3 *floor, Vec3 *drop)
{
    RainDragonSlot *s = RainDragon_FindSlot(NULL);
    if (!s)
        return 0;

    GrObj *gr = *stc_grobj;
    float scale = grGetStageScale();
    root_desc.child = gr->joint_table[dragon_desc->joint].desc;
    root_desc.position = *drop;
    root_desc.rotation = (Vec3){0.0f, RandomAngle(), 0.0f};
    root_desc.scale = (Vec3){scale, scale, scale};

    GOBJ *g = GObj_Create(DRAGON_GOBJ_CLASS, GAMEPLINK_1, 0);
    JOBJ *root = JObj_LoadJoint(&root_desc);
    GObj_AddObject(g, HSD_OBJKIND_JOBJ, root);
    GObj_AddGXLink(g, JObj_GX, GAMEGX_MAP, DRAGON_GX_PRI);
    GObj_AddProc(g, RainDragon_Think, DRAGON_PROC_PRI);

    // The bones keep the motion's wiggle; the body's own track, the leap's arc, is
    // dropped for the fall.
    JOBJ *body = root->child;
    GrModelMotionAnim *motion = &gr->gr_data->motion->anim[dragon_desc->motion];
    JObj_AddAnimAll(body, motion->anim_joint, NULL, NULL);
    JObj_RemoveAnim(body);
    JObj_ReqAnimAll(body, 0.0f);
    body->trans = (Vec3){0.0f, 0.0f, 0.0f};
    body->rot.X = 0.0f;
    body->rot.Y = 0.0f;
    body->rot.Z = DRAGON_BODY_ROT_Z;
    JObj_SetMtxDirtySub(root);

    // City Trial fills every slot, so one is lent and handed back: GrYaku_Create keeps
    // the static block's pointer as YakumonoData.data_ptr and never reads the slot again.
    YakumonoTable *table = gr->gr_data->yakumono;
    void *lent = table->data_array[dragon_data_idx];
    table->data_array[dragon_data_idx] = &hitbox_param;
    GOBJ *yaku = GrYaku_Create(YAKUKIND_LAVADRAGON, dragon_data_idx);
    table->data_array[dragon_data_idx] = lent;

    // Left in state -1 the yakumono runs no motion script, so the regions stay as armed.
    YakumonoData *yd = Yaku_GetData(yaku);
    for (int i = 0; i < DRAGON_REGION_NUM; i++)
        HurtData_SetAttackRegion(yd->hurt_data, i, JObj_GetIndex(root, region_bone[i]), &region_params[i]);

    *s = (RainDragonSlot){
        .gobj = g,
        .yaku = yaku,
        .head = JObj_GetIndex(root, region_bone[0]),
    };
    JObj_GetWorldPosition(s->head, NULL, &s->head_prev);
    yd->pos = s->head_prev;
    return 1;
}

void RainDragon_RemoveAll(void)
{
    for (int i = 0; i < RAIN_DRAGON_MAX; i++)
    {
        if (dragons[i].gobj)
            RainDragon_Remove(&dragons[i]);
    }
}

void RainDragon_Forget(void)
{
    memset(dragons, 0, sizeof(dragons));
}
