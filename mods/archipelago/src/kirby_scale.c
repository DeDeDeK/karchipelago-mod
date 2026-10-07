#include "game.h"
#include "rider.h"
#include "topride.h"
#include "inline.h"

#include "kirby_scale.h"
#include "main.h"
#include "ap_item_handler.h"
#include "textbox_api.h"
#include "ap_announce.h"

// Multiplicative, kept inside [0.5, 2.0] so the model never breaks the camera or the
// collision feel.
#define KIRBY_SCALE_MIN 0.5f
#define KIRBY_SCALE_MAX 2.0f
#define KIRBY_SCALE_GROW 1.5f
#define KIRBY_SCALE_SHRINK 0.5f
#define KIRBY_SCALE_NEUTRAL 1.0f

#define KIRBY_SCALE_ANIM_FRAMES 60

// Ease state, shared across human players. Settled at neutral, the appliers leave vanilla
// scaling alone.
static float kirby_scale_target  = KIRBY_SCALE_NEUTRAL;
static float kirby_scale_current = KIRBY_SCALE_NEUTRAL;
static float kirby_scale_start   = KIRBY_SCALE_NEUTRAL;
static int   kirby_scale_anim    = KIRBY_SCALE_ANIM_FRAMES;

static float ClampScale(float s)
{
    if (s < KIRBY_SCALE_MIN)
        return KIRBY_SCALE_MIN;
    if (s > KIRBY_SCALE_MAX)
        return KIRBY_SCALE_MAX;
    return s;
}

// Top Ride has no intro, so it gates on round_state == 2; easing in the countdown would
// finish before "GO".
static int InScalableScene(void)
{
    MajorKind major = Scene_GetCurrentMajor();
    if (major == MJRKIND_TOP)
    {
        TopRideKirbyMgr *mgr = *stc_topride_kirbymgr;
        return mgr != NULL && mgr->round_state == 2;
    }
    if (major == MJRKIND_CITY || major == MJRKIND_AIR)
        return Scene_GetCurrentMinor() == MNRKIND_3D;
    return 0;
}

int KirbyScale_HandleItem(uint ap_item_id)
{
    if (!InScalableScene())
        return AP_ITEM_RETRY;

    // Wait for the intro and countdown so the model grows in during play.
    if (Gm_GetIntroState() != GMINTRO_END)
        return AP_ITEM_RETRY;

    kirby_scale_start = kirby_scale_current;
    kirby_scale_anim = 0;

    if (ap_item_id == AP_ITEM_BIG_KIRBY)
    {
        kirby_scale_target = ClampScale(kirby_scale_target * KIRBY_SCALE_GROW);
        APAnnounce_Grant("Received: ", "Big Kirby", tb_api->ItemColor, NULL);
    }
    else
    {
        kirby_scale_target = ClampScale(kirby_scale_target * KIRBY_SCALE_SHRINK);
        APAnnounce_Grant("Received: ", "Small Kirby", tb_api->ItemColor, NULL);
    }

    OSReport("[KirbyScale] %s received; model scale now %d/1000\n",
             ap_item_id == AP_ITEM_BIG_KIRBY ? "Big Kirby" : "Small Kirby",
             (int)(kirby_scale_target * 1000.0f));
    return AP_ITEM_APPLIED;
}

// Once settled it sits exactly on target.
static float KirbyScale_Tick(void)
{
    if (kirby_scale_anim < KIRBY_SCALE_ANIM_FRAMES)
    {
        kirby_scale_anim++;
        float t = (float)kirby_scale_anim / (float)KIRBY_SCALE_ANIM_FRAMES;
        t = smoothstep(t);
        kirby_scale_current = lerp(kirby_scale_start, kirby_scale_target, t);
        if (kirby_scale_anim == KIRBY_SCALE_ANIM_FRAMES)
            kirby_scale_current = kirby_scale_target;
    }
    return kirby_scale_current;
}

// An ease back to neutral still has to run out, so this is the settled state rather
// than the target alone.
static int KirbyScale_Idle(void)
{
    return kirby_scale_current == KIRBY_SCALE_NEUTRAL && kirby_scale_target == KIRBY_SCALE_NEUTRAL;
}

static void KirbyScale_3DPerFrame(GOBJ *g)
{
    if (KirbyScale_Idle())
        return;

    float s = KirbyScale_Tick();
    for (int i = 0; i < PLY_NUM; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        GOBJ *rg = Ply_GetRiderGObj(i);
        if (!rg)
            continue;
        RiderData *rd = rg->userdata;
        rd->model_scale = s;
    }
}

static void KirbyScale_TopRidePerFrame(GOBJ *g)
{
    if (KirbyScale_Idle())
        return;

    TopRideKirbyMgr *mgr = *stc_topride_kirbymgr;
    if (!mgr)
        return;

    float s = KirbyScale_Tick();
    for (int i = 0; i < 4; i++)
    {
        TopRideKirby *kirby = mgr->kirbys[i];
        if (!kirby)
            continue;
        if (TopRide_GetPlayerKind(kirby->player_slot) != TR_PKIND_HMN)
            continue;
        kirby->charge.model_scale = s;
    }
}

void KirbyScale_On3DLoadEnd(void)
{
    GOBJ_EZCreator(0, GAMEPLINK_SYS, 0, 0, 0, HSD_OBJKIND_NONE, 0, KirbyScale_3DPerFrame, 0, 0, 0, 0);
}

void KirbyScale_OnTopRideLoadEnd(void)
{
    GOBJ_EZCreator(0, GAMEPLINK_SYS, 0, 0, 0, HSD_OBJKIND_NONE, 0, KirbyScale_TopRidePerFrame, 0, 0, 0, 0);
}

void KirbyScale_OnSceneChange(void)
{
    // Scenes recreate Kirby at model_scale 1.0, so snap to neutral with no ease.
    kirby_scale_target  = KIRBY_SCALE_NEUTRAL;
    kirby_scale_current = KIRBY_SCALE_NEUTRAL;
    kirby_scale_start   = KIRBY_SCALE_NEUTRAL;
    kirby_scale_anim    = KIRBY_SCALE_ANIM_FRAMES;
}
