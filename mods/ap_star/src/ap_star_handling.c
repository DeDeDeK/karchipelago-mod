#include <string.h>

#include "os.h"
#include "machine.h"

#include "ap_star.h"
#include "ap_star_handling.h"

// A profile is a copy of the star's own two attribute blocks, with named fields
// overwritten or a stock machine's blocks laid over them whole. Machine_AdjustAttributes
// rebuilds a machine out of md->vcData, so a profile is applied by pointing the machine
// at a vcData of ours and asking for that rebuild, which re-applies the patch stats too.

// Warp Star's and Flight Warp Star's shipped blocks, word for word out of
// VcStarNormal.dat and VcStarFlight.dat, so the profiles built from them carry the
// unnamed terms - the x028/x044/x06c rows and the fall tiers - as well as the named.
static const u32 stc_warp_attr[sizeof(vcAttributes) / 4] = {
    0x0000000c, 0x00000007, 0x3f800000, 0x3f800000, 0x3fd9999a, 0x3fd9999a,
    0x3e99999a, 0x3e99999a, 0x00000000, 0x00000000, 0x3f94dd2f, 0x40800000,
    0x42700000, 0x42200000, 0x3d25e354, 0x3f19999a, 0x3e25e354, 0x3ca5e354,
    0x42340000, 0x42dc0000, 0x000000b4, 0x0000003c, 0x40000000, 0x40400000,
    0x00000050, 0x3e99999a, 0x0005a593, 0x43480000, 0x3fe00000, 0x4027ae14,
    0x41a00000, 0x3f000000, 0x41a00000, 0x3f800000, 0x3f800000, 0x3e4ccccd,
    0x3fd2786c, 0x3f000000, 0x40000000, 0x3c75c28f, 0x3cc49ba6, 0x3f800000,
    0x3c23d70a, 0x3c75c28f, 0x3cac0831, 0x3d1374bc, 0x3d63bcd3, 0x3da786c2,
    0x3dd73eab, 0x3e0a3d71, 0x3e23d70a, 0x3e3851ec, 0x3e4ccccd, 0x3f19999a,
    0x3f800000, 0x40066666, 0x3e6e978d, 0x3cf5c28f, 0x3f000000, 0x3f800000,
    0x3f000000, 0x41600000, 0x40e00000, 0x40000000, 0x40000000, 0x3f4ccccd,
    0x40200000, 0x41a00000, 0x3f800000, 0x41a00000, 0x3f800000, 0x3fe00000,
    0x4027ae14, 0x41a00000, 0x3f800000, 0x41a00000, 0x3f800000, 0x3f800000,
    0x3ba3d70a, 0x3e9126e9, 0x00000000, 0x00000000, 0x00000000, 0x3fee76c9,
    0x3ecccccd, 0x3f000000, 0x3f933333, 0x4025e354, 0x3b23d70a, 0x3ea8f5c3,
    0x401b851f, 0x406947ae, 0x3f316873, 0x3f928f5c, 0x42a00000, 0x3fdae148,
    0x42a00000, 0x3f000000, 0x3f928f5c, 0x42700000, 0x3fe66666, 0x3f000000,
    0x3ee66666, 0x3ca3d70a, 0x3f8ccccd, 0x3fd9999a, 0x3f99999a, 0x3f316873,
    0x3f81999a, 0x3cf5c28f, 0x3fd48b44, 0x3f666666, 0x3f800000, 0x00000000,
    0x00000000, 0x3f000000, 0x41b00000, 0x3f800000, 0x3f800000, 0x3f800000,
    0x41a00000, 0x40066666, 0x3f333333, 0x3f800000,
};

static const u32 stc_warp_hnd[sizeof(vcHandlingAttr) / 4] = {
    0x3f051eb8, 0x3c75c28f, 0x3f99999a, 0x3d178d50, 0x3ced9168, 0x3cb851ec,
    0x3e4ccccd, 0x42200000, 0x3f0ccccd, 0x3e4ccccd, 0x3dcccccd, 0x3ac49ba6,
    0x3a03126f, 0x00000000, 0x00000000, 0x3dcccccd, 0x3e051eb8, 0x3d75c28f,
    0x3d6d9168, 0x3d6147ae, 0x3e051eb8, 0x3e4ccccd, 0x40400000, 0x3e19999a,
    0x3ca3d70a, 0x3eae147b, 0x3da3d70a, 0x3da5e354, 0x3d4ccccd, 0x3d343958,
    0x3d343958, 0x3f800000, 0x3e4ccccd, 0x3f800000, 0x3dcccccd, 0x3e19999a,
    0x41200000, 0x3f000000, 0x3f800000, 0x3f800000, 0x42100000, 0x42100000,
    0x42100000, 0x3f000000, 0x3fcccccd, 0x00000001, 0x3d178d50, 0x3f333333,
    0x3f800000, 0x3c1374bc, 0x43fa0000, 0x40900000, 0x3f800000, 0x3d23d70a,
    0x41700000, 0x3fe66666, 0x3f4ccccd, 0x3f800000, 0x3f800000, 0x3f800000,
    0x00000000, 0x00000000,
};

static const u32 stc_flight_attr[sizeof(vcAttributes) / 4] = {
    0x0000000c, 0x00000007, 0x3f800000, 0x3f800000, 0x3fd9999a, 0x3fd9999a,
    0x3e99999a, 0x3e99999a, 0x00000000, 0x00000000, 0x3f94dd2f, 0x40800000,
    0x42700000, 0x42200000, 0x3d25e354, 0x3f19999a, 0x3e25e354, 0x3ca5e354,
    0x42340000, 0x42dc0000, 0x00000280, 0x0000003c, 0x40000000, 0x40400000,
    0x00000050, 0x3e99999a, 0x0005a593, 0x43480000, 0x3fe00000, 0x4027ae14,
    0x41a00000, 0x3f000000, 0x41a00000, 0x3f800000, 0x3f800000, 0x3e4ccccd,
    0x3fd48b44, 0x3f000000, 0x40000000, 0x3c75c28f, 0x3cc49ba6, 0x3f800000,
    0x3c271de7, 0x3c995aaf, 0x3ce1c582, 0x3d30f27c, 0x3d8ded29, 0x3dc2f838,
    0x3e0068dc, 0x3e205bc0, 0x3e4154ca, 0x3e60ded3, 0x3e800000, 0x3f19999a,
    0x3f800000, 0x40066666, 0x3e6e978d, 0x3cf5c28f, 0x3f000000, 0x3f800000,
    0x3f000000, 0x41600000, 0x40e00000, 0x40000000, 0x40000000, 0x3f4ccccd,
    0x40200000, 0x41a00000, 0x3f800000, 0x41a00000, 0x3f800000, 0x3fe00000,
    0x4027ae14, 0x41a00000, 0x3f800000, 0x41a00000, 0x3f800000, 0x3f800000,
    0x3ba3d70a, 0x3ecf5c29, 0x00000000, 0x00000000, 0x00000000, 0x4001999a,
    0x3f19999a, 0x3f000000, 0x3f933333, 0x4025e354, 0x3a03126f, 0x3ea8f5c3,
    0x401b851f, 0x406947ae, 0x3f316873, 0x3fb33333, 0x42700000, 0x3fdae148,
    0x42a00000, 0x3f000000, 0x3f8ccccd, 0x42a00000, 0x3fc00000, 0x3ecccccd,
    0x3ec28f5c, 0x3c75c28f, 0x3f8ccccd, 0x3fd9999a, 0x3f99999a, 0x3f316873,
    0x3f81999a, 0x3cf5c28f, 0x3fd48b44, 0x3f666666, 0x3f800000, 0x00000000,
    0x00000000, 0x3f000000, 0x41b00000, 0x3f800000, 0x3f800000, 0x3f800000,
    0x41a00000, 0x40066666, 0x3f333333, 0x3f800000,
};

static const u32 stc_flight_hnd[sizeof(vcHandlingAttr) / 4] = {
    0x3f051eb8, 0x3c75c28f, 0x3f99999a, 0x3d178d50, 0x3ced9168, 0x3cb851ec,
    0x3e4ccccd, 0x42200000, 0x3f0ccccd, 0x3e4ccccd, 0x3dcccccd, 0x3ac49ba6,
    0x3a03126f, 0x00000000, 0x00000000, 0x3dcccccd, 0x3e051eb8, 0x3d75c28f,
    0x3d6d9168, 0x3d6147ae, 0x3e051eb8, 0x3f000000, 0x40400000, 0x3dcccccd,
    0x3ca3d70a, 0x3eae147b, 0x3da3d70a, 0x3da5e354, 0x3d4ccccd, 0x3d343958,
    0x3d343958, 0x3f800000, 0x3e4ccccd, 0x3f800000, 0x3dcccccd, 0x3e19999a,
    0x41200000, 0x3f000000, 0x3f800000, 0x3f800000, 0x42100000, 0x42100000,
    0x42100000, 0x3e99999a, 0x3fcccccd, 0x00000001, 0x3e4ccccd, 0x3f800000,
    0x3f800000, 0x3c656042, 0x44e10000, 0x3fc00000, 0x3f800000, 0x3d23d70a,
    0x41700000, 0x3fe66666, 0x3f4ccccd, 0x3f800000, 0x3f800000, 0x3f800000,
    0x00000000, 0x00000000,
};

static vcAttributes stc_attr[AP_STAR_PROFILE_NUM];
static vcHandlingAttr stc_hnd[AP_STAR_PROFILE_NUM];
static vcData stc_vc[AP_STAR_PROFILE_NUM];
static int stc_built;

static const char *stc_names[AP_STAR_PROFILE_NUM] = {
    "Slick", "Glide", "Speed", "Warp", "Boost", "Jet",
};

// boost_gain is sampled at the charge in tenths and lerped between neighbours, so
// entry n is what a release at n/10 charge is worth.
static void SetBoostCurve(vcAttributes *a, const float *curve)
{
    for (int i = 0; i < 11; i++)
        a->boost_gain[i] = curve[i];
}

// Become a stock machine, keeping the star's identity attributes and its three
// combat stats so City Trial combat balance is untouched.
static void CopyStock(vcAttributes *a, vcHandlingAttr *h, const u32 *attr, const u32 *hnd)
{
    vcAttributes own = *a;

    memcpy(a, attr, sizeof(*a));
    memcpy(h, hnd, sizeof(*h));

    a->rider_sit_bone_idx = own.rider_sit_bone_idx;
    a->rider_extra_bone_idx = own.rider_extra_bone_idx;
    a->model_scale = own.model_scale;
    a->start_cam_distance = own.start_cam_distance;
    a->x014 = own.x014;
    a->shadow_length = own.shadow_length;
    a->shadow_width = own.shadow_width;
    a->shadow_width_turning = own.shadow_width_turning;
    a->hitbox_size = own.hitbox_size;
    a->hitbox_dist_x = own.hitbox_dist_x;
    a->landing_hitbox_size = own.landing_hitbox_size;
    a->landing_hitbox_dist_x = own.landing_hitbox_dist_x;
    a->base_hp = own.base_hp;
    a->base_offense = own.base_offense;
    a->base_defense = own.base_defense;
}

// The machine as shipped. Slick Star's blocks, which VcStarAp.dat was built from.
static void Profile_Slick(vcAttributes *a, vcHandlingAttr *h)
{
    (void)a;
    (void)h;
}

// Flight Warp Star: the game's glider, with the air control that goes with it.
static void Profile_Glide(vcAttributes *a, vcHandlingAttr *h)
{
    CopyStock(a, h, stc_flight_attr, stc_flight_hnd);
}

// Hydra's weight and its near-permanent boost at Formula Star's cruise, with a
// gain curve that pays out at every charge level rather than only a full one.
static void Profile_Speed(vcAttributes *a, vcHandlingAttr *h)
{
    static const float curve[11] = {
        0.05f, 0.07f, 0.09f, 0.11f, 0.13f, 0.15f,
        0.17f, 0.19f, 0.21f, 0.23f, 0.25f,
    };

    a->top_speed_ground = 2.70f;
    a->top_speed_air = 2.30f;
    a->slope_speed_up = 1.0f;
    a->slope_speed_down = 1.0f;
    a->ground_grip = 0.10f;
    a->ground_grip_2 = 0.03f;
    a->air_grip = 0.80f;
    a->charge_deplete_rate = 0.02f;
    a->charge_full_duration = 1600;
    a->boost_gain_sliding = 2.3f;
    SetBoostCurve(a, curve);

    h->accel_floor = 3.0f;
    h->accel_turn_keep = 0.07f;
    h->turn_rate_rest = 0.026f;
    h->turn_rate_top = 0.016f;
    h->lean_step_max_0 = 12.0f;
    h->pitch_max_down = 12.0f;
    h->pitch_max_up = 6.0f;
    h->air_impulse = 420.0f;
}

// Warp Star: the game's baseline machine, grippy and even everywhere.
static void Profile_Warp(vcAttributes *a, vcHandlingAttr *h)
{
    CopyStock(a, h, stc_warp_attr, stc_warp_hnd);
}

// Nothing until the meter is nearly full, then Rocket Star's launch. The cruise cap
// comes down to match, and a full meter holds for 15s so it can be spent on purpose.
static void Profile_Boost(vcAttributes *a, vcHandlingAttr *h)
{
    static const float curve[11] = {
        0.005f, 0.008f, 0.012f, 0.018f, 0.026f, 0.038f,
        0.055f, 0.085f, 0.14f, 0.32f, 1.20f,
    };

    a->top_speed_ground = 1.60f;
    a->ground_grip = 0.15f;
    a->air_grip = 0.40f;
    a->charge_rate = 0.035f;
    a->charge_rate_turning = 0.05f;
    a->charge_deplete_rate = 0.35f;
    a->charge_full_duration = 900;
    a->boost_gain_sliding = 2.6f;
    a->full_charge_midair_speed = 0.0035f;
    SetBoostCurve(a, curve);

    h->accel_floor = 0.9f;
    h->accel_turn_keep = 0.09f;
    h->turn_rate_rest = 0.030f;
    h->turn_rate_top = 0.017f;
    h->roll_max = 0.6f;
}

// Jet Star, whose takeoff_speed is ten times the field's: it leaves the ground off
// anything, and it is slow and heavy once it lands.
static void Profile_Jet(vcAttributes *a, vcHandlingAttr *h)
{
    static const float curve[11] = {
        0.01f, 0.015f, 0.0252f, 0.0378f, 0.0577f, 0.0856f,
        0.1171f, 0.1559f, 0.2045f, 0.2414f, 0.2694f,
    };

    a->top_speed_ground = 1.3365f;
    a->top_speed_air = 1.5795f;
    a->slope_speed_up = 0.75f;
    a->slope_speed_down = 1.2f;
    a->ground_grip = 0.233f;
    a->ground_grip_2 = 0.03f;
    a->air_grip = 0.25f;
    a->charge_rate = 0.01f;
    a->charge_rate_turning = 0.02f;
    a->charge_full_duration = 160;
    a->takeoff_speed = 3.0375f;
    a->x140 = 0.3645f;
    a->x144 = 0.6f;
    a->full_charge_midair_speed = 0.006f;
    a->x15c = 4.05f;
    a->x168 = 3.645f;
    a->x16c = 6.075f;
    a->glide_up_speed = 3.0f;
    a->glide_down_speed = 1.5f;
    a->descent_x190 = 1.8f;
    a->descent_x194 = 0.2f;
    a->descent_x198 = 0.1f;
    a->descent_x19c = 0.03f;
    SetBoostCurve(a, curve);

    h->lift_ceiling = 0.019f;
    h->accel_floor = 1.8f;
    h->turn_rate_rest = 0.035f;
    h->turn_rate_top = 0.040f;
    h->lean_step_max_0 = 25.0f;
    h->pitch_max_down = 16.0f;
    h->pitch_max_up = 4.0f;
    h->air_accel = 0.80f;
    h->air_impulse = 400.0f;
}

static void (*const stc_writers[AP_STAR_PROFILE_NUM])(vcAttributes *, vcHandlingAttr *) = {
    Profile_Slick, Profile_Glide, Profile_Speed, Profile_Warp, Profile_Boost, Profile_Jet,
};

void ApStarHandling_On3DLoadEnd(void)
{
    stc_built = 0;
}

int ApStarHandling_ProfileForPods(int pods)
{
    if (pods < 1 || pods > AP_STAR_PROFILE_NUM)
        return -1;
    return AP_STAR_PROFILE_NUM - pods;
}

// Seeded from the machine's own vcData rather than from the archive, so the
// blocks pick up whatever the scene loaded. Every profile is built at once and
// held for the round; only a machine that leaves profile 0 is ever pointed at one.
static int Build(MachineData *md)
{
    if (stc_built)
        return 1;

    vcData *vc = md->vcData;
    if (vc == NULL || vc->attr == NULL || vc->handling_attr == NULL)
        return 0;

    for (int i = 0; i < AP_STAR_PROFILE_NUM; i++)
    {
        stc_attr[i] = *vc->attr;
        stc_hnd[i] = *vc->handling_attr;
        stc_writers[i](&stc_attr[i], &stc_hnd[i]);

        stc_vc[i] = *vc;
        stc_vc[i].attr = &stc_attr[i];
        stc_vc[i].handling_attr = &stc_hnd[i];
    }

    stc_built = 1;
    OSReport("[ApStarHandling] Built %d profiles\n", AP_STAR_PROFILE_NUM);
    return 1;
}

void ApStarHandling_Apply(MachineData *md, int profile)
{
    if (profile < 0 || profile >= AP_STAR_PROFILE_NUM || md == NULL)
        return;
    if (!Build(md))
        return;

    md->vcData = &stc_vc[profile];
    Machine_AdjustAttributes(md);

    // The rebuild refreshes the attribute block but not the three fields
    // Machine_Star_Init seeds off it once, so those are carried over by hand.
    vcAttributes *live = (vcAttributes *)&md->base_attributes;
    md->ground_grip = live->ground_grip;
    md->air_grip = live->air_grip;
    md->lift_max = md->attr->handling.lift_ceiling;
    if (md->lift_accum > md->lift_max)
        md->lift_accum = md->lift_max;

    OSReport("[ApStarHandling] Profile %s\n", stc_names[profile]);
}
