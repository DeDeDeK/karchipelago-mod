#include <string.h>

#include "game.h"
#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "hurt.h"
#include "enemy.h"
#include "weapon.h"
#include "inline.h"
#include "code_patch/code_patch.h"

#include "custom_events.h"
#include "event_bomb_tacs.h"
#include "hazard_weapon.h"
#include "road_spot.h"

#define TAC_NUM           6
#define TAC_SLOT_NUM      12     // TAC_NUM on the map plus the ones leaving it
#define TAC_SPAWN_TRIES   40
#define TAC_EDGE_MARGIN   0.15f  // share of the road width kept clear at each edge
#define TAC_GAP           150.0f // from every other TAC on the map
#define TAC_PLAYER_GAP    80.0f
#define TAC_RESPAWN_DELAY 120    // frames a TAC is missing from the map before a new one comes
#define TAC_FADE_FRAMES   20
#define TAC_AIR_LIMIT     180    // frames off the ground before a TAC counts as lost

// TAC compares its grounded frame count against desc.variant unsigned, so it never
// leaves on its own.
#define TAC_STAY          -1

#define DROP_INTERVAL     540 // frames between one TAC's bombs, plus up to DROP_JITTER
#define DROP_JITTER       120
#define DROP_FIRST        60  // earliest first drop after a spawn
#define DROP_HEIGHT       3.0f
#define DROP_BEHIND       3.0f

#define BOMB_FUSE         120  // frames from the drop to the contact blast
#define BOMB_TOUCH_RANGE  8.0f // a rider this close sets it off early
#define BOMB_MODEL_SCALE  0.5f
#define BOMB_BLAST_SCALE  (1.0f / 3.0f)

typedef struct TacSlot
{
    GOBJ *gobj;     // NULL = free
    int leaving;    // hit or sent off: drops nothing more and fades out once it flees
    int fading;
    int drop_timer;
    int air_frames;
} TacSlot;

static TacSlot tacs[TAC_SLOT_NUM];
static int respawn_timer;
static int bomb_num;
static int hit_num;

static TacSlot *BombTacs_FindSlot(GOBJ *gobj)
{
    for (int i = 0; i < TAC_SLOT_NUM; i++)
    {
        if (tacs[i].gobj == gobj)
            return &tacs[i];
    }
    return NULL;
}

// The three bl Tac_ScatterItems, in Tac_RunAnim (0x8021bb78), Tac_FleePri6
// (0x8021c504) and Tac_OnHit (0x8021c62c). An event TAC throws nothing.
static void BombTacs_ScatterItems(EnemyData *ed, int num)
{
    if (!BombTacs_FindSlot(ed->gobj))
        Tac_ScatterItems(ed, num);
}

static int BombTacs_IsOnMap(TacSlot *s)
{
    return s->gobj && !s->leaving;
}

static int BombTacs_IsClear(Vec3 *pos)
{
    for (int i = 0; i < TAC_SLOT_NUM; i++)
    {
        if (!BombTacs_IsOnMap(&tacs[i]))
            continue;
        EnemyData *ed = tacs[i].gobj->userdata;
        if (VECSquareDistance(pos, &ed->pos) < TAC_GAP * TAC_GAP)
            return 0;
    }
    return CustomEvents_NearestRider(pos, TAC_PLAYER_GAP, 0) < 0;
}

static int BombTacs_SpawnOne(void)
{
    TacSlot *s = BombTacs_FindSlot(NULL);
    if (!s || !CustomEvents_HeapOk())
        return 0;

    Vec3 floor;
    int found = 0;
    for (int i = 0; i < TAC_SPAWN_TRIES && !found; i++)
        found = RoadSpot_PickFlatFloor(&floor, TAC_EDGE_MARGIN) && BombTacs_IsClear(&floor);
    if (!found)
        return 0;

    EventActorDesc desc;
    CustomEvents_InitActorDesc(&desc, ENEMYKIND_TAC, &floor, 1.0f);
    desc.forward = CustomEvents_RandomHeading();
    desc.variant = TAC_STAY;

    GOBJ *g = EventActor_Create(&desc);
    if (!g)
        return 0;

    *s = (TacSlot){.gobj = g, .drop_timer = DROP_FIRST + HSD_Randi(DROP_INTERVAL)};
    return 1;
}

// Between the blast script arming the regions (priority 1) and the hit scans
// (priority 9). Sparing event actors keeps a TAC's own bomb from sending it off.
static void BombTacs_BombProc(GOBJ *gobj)
{
    WeaponData *wp = gobj->userdata;
    if (wp->state != BOMB_STATE_BLAST)
        return;

    HurtData *hd = wp->hurt_data;
    for (int i = 0; i < hd->region_count; i++)
        hd->regions[i].params.hit_flags &= ~HITFLAG_VICTIM(HURTKIND_EVENTACTOR);
}

// Replaces the thrown state's envcoll, which sets the bomb off on its first contact.
// The thrown phys still adds gravity, so the bomb settles where it lands.
static void BombTacs_BombEnvColl(void *p)
{
    WeaponData *wp = p;
    Weapon_UpdateEnvColl(wp);
    if (wp->flag_b & WP_FLAGB_ENV_CONTACT)
        wp->vel = (Vec3){0.0f, 0.0f, 0.0f};

    if (wp->frame_counter >= BOMB_FUSE || CustomEvents_NearestRider(&wp->pos, BOMB_TOUCH_RANGE, 0) >= 0)
        Bomb_DetonationTrigger(wp);
}

// An ownerless bomb behind the TAC, shrunk with a third of the blast. Its HELD state
// would snap it to the missing owner's hand on the first proc.
static int BombTacs_DropBomb(EnemyData *ed)
{
    if (!CustomEvents_HeapOk())
        return 0;

    Vec3 pos = {
        ed->pos.X - ed->forward.X * DROP_BEHIND + ed->up.X * DROP_HEIGHT,
        ed->pos.Y - ed->forward.Y * DROP_BEHIND + ed->up.Y * DROP_HEIGHT,
        ed->pos.Z - ed->forward.Z * DROP_BEHIND + ed->up.Z * DROP_HEIGHT,
    };
    WeaponData *wp = HazardWeapon_Create(WPKIND_BOMB, &pos, NULL);
    if (!wp)
        return 0;

    Weapon_StateChange(wp, BOMB_STATE_THROWN, 0.0f, 1.0f, 1);
    wp->envcoll_callback = BombTacs_BombEnvColl;

    // The blast scale multiplies the hitbox as well as the fireball, so scale stays 1.
    wp->model_scale *= BOMB_MODEL_SCALE;
    BombParams *bp = wp->kind_params;
    bp->blast_scale_start *= BOMB_BLAST_SCALE;
    bp->blast_scale_end *= BOMB_BLAST_SCALE;
    bp->quake_amplitude *= BOMB_BLAST_SCALE;

    GObj_AddProc(wp->gobj, BombTacs_BombProc, WPPRI_8);
    return 1;
}

// Idle, looking about, circling or running, on the ground and done fading in.
static int BombTacs_CanDrop(EnemyData *ed)
{
    return ed->state >= TAC_STATE_IDLE && ed->state <= TAC_STATE_RUN_STOP &&
           !ed->is_airborne && ed->alpha >= 1.0f;
}

// Frees the slots of TACs that faded out, marks a TAC leaving once it is hit or flees,
// and fades it out as it flees, which destroys it.
static void BombTacs_Update(void)
{
    for (int i = 0; i < TAC_SLOT_NUM; i++)
    {
        TacSlot *s = &tacs[i];
        if (!s->gobj)
            continue;
        if (!CustomEvents_IsActorLive(s->gobj, ENEMYKIND_TAC))
        {
            s->gobj = NULL;
            continue;
        }

        EnemyData *ed = s->gobj->userdata;
        if (!s->leaving && (ed->state == TAC_STATE_HIT || ed->state == TAC_STATE_FLEE))
        {
            s->leaving = 1;
            hit_num++;
            // The hit's animation end flees instead of running on.
            ed->variant = 0;
        }
        if (s->leaving && !s->fading && ed->state == TAC_STATE_FLEE)
        {
            EventActor_ArmFadeOut(ed, TAC_FADE_FRAMES);
            EventActor_StartFadeOut(ed);
            s->fading = 1;
        }
    }
}

void BombTacs_Start(void)
{
    Enemy_CheckAndLoad(ENEMYKIND_TAC);

    // A round cut off mid-event leaves dead pointers behind.
    memset(tacs, 0, sizeof(tacs));
    respawn_timer = 0;
    bomb_num = 0;
    hit_num = 0;

    if (!Enemy_GetActorData(ENEMYKIND_TAC))
    {
        OSReport("[BombTacs] TAC data not loaded, no TACs spawned\n");
        return;
    }

    int num = 0;
    for (int i = 0; i < TAC_NUM; i++)
        num += BombTacs_SpawnOne();
    OSReport("[BombTacs] Spawned %d TACs\n", num);
}

void BombTacs_Active(void)
{
    BombTacs_Update();

    int on_map = 0;
    for (int i = 0; i < TAC_SLOT_NUM; i++)
    {
        TacSlot *s = &tacs[i];
        if (!BombTacs_IsOnMap(s))
            continue;
        // Event actors skip the floor kill, so one that ran off the map falls for good.
        EnemyData *ed = s->gobj->userdata;
        s->air_frames = ed->is_airborne ? s->air_frames + 1 : 0;
        if (s->air_frames > TAC_AIR_LIMIT)
        {
            EventActorGObj_Destroy(s->gobj);
            s->gobj = NULL;
            continue;
        }
        on_map++;

        if (s->drop_timer > 0)
            s->drop_timer--;
        else if (BombTacs_CanDrop(ed) && BombTacs_DropBomb(ed))
        {
            s->drop_timer = DROP_INTERVAL + HSD_Randi(DROP_JITTER);
            bomb_num++;
        }
    }

    if (on_map >= TAC_NUM)
        respawn_timer = 0;
    else if (++respawn_timer >= TAC_RESPAWN_DELAY)
    {
        respawn_timer = 0;
        BombTacs_SpawnOne();
    }
}

// Sends every TAC off. One still fading in goes at once, since the fade-in and
// fade-out procs would fight over its alpha.
void BombTacs_End(void)
{
    for (int i = 0; i < TAC_SLOT_NUM; i++)
    {
        TacSlot *s = &tacs[i];
        if (!BombTacs_IsOnMap(s) || !CustomEvents_IsActorLive(s->gobj, ENEMYKIND_TAC))
            continue;

        EnemyData *ed = s->gobj->userdata;
        if (ed->state == TAC_STATE_HIT || ed->state == TAC_STATE_FLEE)
            continue;
        if (ed->alpha < 1.0f)
        {
            EventActorGObj_Destroy(s->gobj);
            s->gobj = NULL;
            continue;
        }
        Tac_EnterFlee(ed);
        s->leaving = 1;
    }
    BombTacs_Update();
}

// The bombs left go off on their own fuses.
void BombTacs_End2(void)
{
    for (int i = 0; i < TAC_SLOT_NUM; i++)
    {
        if (tacs[i].gobj && CustomEvents_IsActorLive(tacs[i].gobj, ENEMYKIND_TAC))
            EventActorGObj_Destroy(tacs[i].gobj);
    }
    memset(tacs, 0, sizeof(tacs));
    OSReport("[BombTacs] Dropped %d bombs, %d TACs hit\n", bomb_num, hit_num);
}

// The scene exit reclaims the TACs.
void BombTacs_Abort(void)
{
    memset(tacs, 0, sizeof(tacs));
}

void BombTacs_InstallHooks(void)
{
    static const int scatter_calls[] = {0x8021bccc, 0x8021c5a8, 0x8021c67c};
    for (int i = 0; i < GetElementsIn(scatter_calls); i++)
        CODEPATCH_REPLACECALL(scatter_calls[i], BombTacs_ScatterItems);
}
