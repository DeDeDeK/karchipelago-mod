#include "game.h"
#include "hsd.h"
#include "scene.h"
#include "text.h"
#include "obj.h"
#include "gx.h"
#include "os.h"
#include "inline.h"
#include "hoshi/screen_cam.h"

#include "cpu_ai.h"
#include "cpu_hazards.h"
#include "cpu_line.h"
#include "cpu_sense.h"
#include "cpu_strategist.h"
#include "cpu_target.h"
#include "cpu_catchup.h"
#include "ai_overlay.h"

#define OVERLAY_X 16.0f
#define OVERLAY_Y 72.0f
#define OVERLAY_SCALE 0.4f
#define OVERLAY_LINE_H 32.0f
#define OVERLAY_W 1100.0f
#define OVERLAY_PAD 8.0f
#define OVERLAY_REFRESH 6
#define OVERLAY_LINE_NUM (PLY_NUM + 1)

#define WORLD_GOBJ_CLASS 202
#define WORLD_GOBJ_PLINK 36
#define WORLD_SEG_MAX 256
#define WORLD_LINE_POINTS 48
#define WORLD_LINE_WIDTH 18
#define WORLD_CROSS_MIN 2.0f

int ai_overlay = AI_OVERLAY_OFF;

char *ai_overlay_names[AI_OVERLAY_NUM] = {
    [AI_OVERLAY_OFF] = "Off",
    [AI_OVERLAY_TEXT] = "Text",
    [AI_OVERLAY_WORLD] = "World",
    [AI_OVERLAY_BOTH] = "Both",
};

static const GXColor stc_ply_colors[PLY_NUM] = {
    {255, 80, 80, 255}, {80, 140, 255, 255}, {255, 220, 60, 255}, {80, 230, 110, 255}, {200, 120, 255, 255},
};
static const GXColor stc_line_color = {255, 255, 255, 160};
static const GXColor stc_hazard_color = {255, 40, 40, 200};
static const GXColor stc_meteor_color = {255, 150, 0, 220};

static Text *stc_text;
static int stc_tick;

// Each CPU's route this frame, copied out of the per-update scratch.
typedef struct WorldRecord
{
    u32 frame;
    Vec3 pos;
    Vec3 route[4];
    Vec3 steer;
    Vec3 goal;
    u8 has_goal;
} WorldRecord;

static WorldRecord stc_records[PLY_NUM];
static Vec3 stc_seg[WORLD_SEG_MAX][2];
static GXColor stc_seg_color[WORLD_SEG_MAX];
static int stc_seg_num;

static void AIOverlay_Create(void)
{
    Text *t = Hoshi_CreateScreenText();
    int i;

    if (!t)
        return;

    t->kerning = 1;
    t->viewport_scale = (Vec2){OVERLAY_SCALE, OVERLAY_SCALE};
    t->color = (GXColor){255, 255, 255, 255};
    t->viewport_color = (GXColor){0, 0, 0, 140};
    t->aspect = (Vec2){OVERLAY_W, OVERLAY_LINE_H * OVERLAY_LINE_NUM + OVERLAY_PAD};
    t->trans = (Vec3){OVERLAY_X, OVERLAY_Y, 0};

    for (i = 0; i < OVERLAY_LINE_NUM; i++)
        Text_AddSubtext(t, OVERLAY_PAD, OVERLAY_LINE_H * i, " ");
    stc_text = t;
}

static void AIOverlay_Destroy(void)
{
    if (stc_text)
        Text_Destroy(stc_text);
    stc_text = NULL;
}

// One line per CPU: level, preset, profile, committed maneuver, goal, rival, riding or on
// foot, speed and charge.
static void AIOverlay_Line(int line, int ply)
{
    RiderData *rd = CpuAI_GetCpuRider(ply);
    CpuData *cpu;

    if (!rd)
    {
        Text_SetText(stc_text, line, " ");
        return;
    }
    cpu = rd->cpu;

    Text_SetText(stc_text, line, "P%d L%d %s  ai %d  man %02x  goal %s  rival P%d  %s  v%.2f c%.2f", ply + 1,
                 cpu->difficulty_level + 1, CpuAI_GetPresetName(ply), cpu->ai_state, cpu->maneuver,
                 Strategist_GetGoalName(ply), cpu->rival_ply + 1, rd->machine_gobj ? "riding" : "on foot",
                 CpuSense_Self(ply)->speed, CpuSense_Self(ply)->charge);
}

static void AIOverlay_Seg(Vec3 *a, Vec3 *b, GXColor c)
{
    if (stc_seg_num >= WORLD_SEG_MAX)
        return;
    stc_seg[stc_seg_num][0] = *a;
    stc_seg[stc_seg_num][1] = *b;
    stc_seg_color[stc_seg_num] = c;
    stc_seg_num++;
}

static void AIOverlay_Cross(Vec3 *p, float r, GXColor c)
{
    Vec3 a, b;

    if (r < WORLD_CROSS_MIN)
        r = WORLD_CROSS_MIN;
    a = (Vec3){p->X - r, p->Y, p->Z};
    b = (Vec3){p->X + r, p->Y, p->Z};
    AIOverlay_Seg(&a, &b, c);
    a = (Vec3){p->X, p->Y - r, p->Z};
    b = (Vec3){p->X, p->Y + r, p->Z};
    AIOverlay_Seg(&a, &b, c);
    a = (Vec3){p->X, p->Y, p->Z - r};
    b = (Vec3){p->X, p->Y, p->Z + r};
    AIOverlay_Seg(&a, &b, c);
}

static void AIOverlay_BuildSegs(void)
{
    const CpuWorldView *w = CpuSense_World();
    const CpuSphere *spheres;
    WorldRecord *r;
    Vec3 line[WORLD_LINE_POINTS];
    u32 frame = stc_hsd_update->engine_frames;
    int line_ply = -1;
    int n, i, k;

    stc_seg_num = 0;
    for (i = 0; i < PLY_NUM; i++)
    {
        r = &stc_records[i];
        if (r->frame != frame && r->frame != frame - 1)
            continue;
        if (line_ply < 0)
            line_ply = i;
        AIOverlay_Seg(&r->pos, &r->route[1], stc_ply_colors[i]);
        for (k = 1; k < 3; k++)
            AIOverlay_Seg(&r->route[k], &r->route[k + 1], stc_ply_colors[i]);
        AIOverlay_Seg(&r->pos, &r->steer, stc_line_color);
        if (r->has_goal)
            AIOverlay_Seg(&r->pos, &r->goal, stc_ply_colors[i]);
    }

    n = CpuLine_GetPoints(line_ply, line, WORLD_LINE_POINTS);
    for (k = 1; k < n; k++)
        AIOverlay_Seg(&line[k - 1], &line[k], stc_line_color);

    n = CpuHazards_GetSpheres(&spheres);
    for (k = 0; k < n; k++)
        AIOverlay_Cross(&spheres[k].pos, spheres[k].radius, stc_hazard_color);
    for (k = 0; k < w->meteor_num; k++)
        AIOverlay_Cross((Vec3 *)&w->meteor[k].impact, w->meteor[k].radius, stc_meteor_color);
}

// Drawn through walls, so a route behind cover still shows.
static void AIOverlay_GX(GOBJ *g, int pass)
{
    COBJ *cam;
    int i;

    if (pass != GXPASS_TEXEDGE || !(cam = COBJ_GetCurrent()))
        return;
    AIOverlay_BuildSegs();
    if (stc_seg_num == 0)
        return;

    GX_BeginXlu(cam, RENDER_VERTEX, GX_BL_INVSRCALPHA);
    GXSetZMode(GX_DISABLE, GX_ALWAYS, GX_DISABLE);
    GXSetLineWidth(WORLD_LINE_WIDTH, GX_TO_ONE);
    GXBegin(GX_LINES, GX_VTXFMT0, stc_seg_num * 2);
    for (i = 0; i < stc_seg_num; i++)
    {
        GXPosition3f32(stc_seg[i][0].X, stc_seg[i][0].Y, stc_seg[i][0].Z);
        GXColor4u8(stc_seg_color[i].r, stc_seg_color[i].g, stc_seg_color[i].b, stc_seg_color[i].a);
        GXPosition3f32(stc_seg[i][1].X, stc_seg[i][1].Y, stc_seg[i][1].Z);
        GXColor4u8(stc_seg_color[i].r, stc_seg_color[i].g, stc_seg_color[i].b, stc_seg_color[i].a);
    }
    HSD_StateInvalidate(HSD_STATE_ALL);
}

// After a CPU's update, while the route scratch is still its own.
void AIOverlay_RecordCPU(RiderData *rd)
{
    WorldRecord *r = &stc_records[rd->ply];
    void *target;
    int k;

    if (!(ai_overlay & AI_OVERLAY_WORLD))
        return;
    r->frame = stc_hsd_update->engine_frames;
    r->pos = rd->pos;
    for (k = 0; k < 4; k++)
        r->route[k] = stc_cpu_route->point[k];
    r->steer = rd->cpu->steer_target_pos;
    r->has_goal = Strategist_GetTarget(rd->ply, &target, &r->goal);
}

// On p_link 0, so the text keeps up through the match pause.
static void AIOverlay_TextThink(GOBJ *gobj)
{
    int hunt;
    int i;

    if (!stc_text || ++stc_tick % OVERLAY_REFRESH != 0)
        return;

    hunt = CpuTarget_GetHuntPly();
    if (hunt < PLY_NUM)
        Text_SetText(stc_text, 0, "Custom AI  level offset %d  event %d  line %s  hunting P%d",
                     CpuCatchup_GetLevelOffset(), CpuSense_World()->event_kind, CpuLine_IsReady() ? "on" : "off",
                     hunt + 1);
    else
        Text_SetText(stc_text, 0, "Custom AI  level offset %d  event %d  line %s", CpuCatchup_GetLevelOffset(),
                     CpuSense_World()->event_kind, CpuLine_IsReady() ? "on" : "off");

    for (i = 0; i < PLY_NUM; i++)
        AIOverlay_Line(i + 1, i);
}

void AIOverlay_On3DLoadEnd(void)
{
    int major = Scene_GetCurrentMajor();
    GOBJ *g;

    if (major != MJRKIND_AIR && major != MJRKIND_CITY)
        return;
    if (ai_overlay & AI_OVERLAY_TEXT)
    {
        AIOverlay_Create();
        GObj_AddProc(GObj_Create(0, GAMEPLINK_SYS, 0), AIOverlay_TextThink, 0);
    }
    if (ai_overlay & AI_OVERLAY_WORLD)
    {
        g = GObj_Create(WORLD_GOBJ_CLASS, WORLD_GOBJ_PLINK, 0);
        GObj_AddGXLink(g, AIOverlay_GX, 0, 0);
    }
}

// Scene teardown does not reliably reclaim a Text, and one left behind draws over the next.
// The GObjs go with the scene.
void AIOverlay_On3DExit(void)
{
    AIOverlay_Destroy();
}
