// Scale Change — custom City Trial event
//
// Scales the entire stage (visual model, collision model, OOB boundaries)
// for the duration of the event.
//
// The stage has two JOBJs: the visual model (on the stage GOBJ) and
// the collision model (at GrObj extended offset 0xF4). Both must be
// scaled and have their matrices marked dirty.
//
// OOB death boundaries live at stage_node offsets 0xCC-0xE0 (AABB
// min/max XYZ). These must be scaled proportionally so the kill
// boundary matches the new stage size.

#include "game.h"
#include "os.h"
#include "stage.h"

#include "event_scale_change.h"

#define SCALE_MULTIPLIER 1.5f

// OOB boundary: 6 floats at stage_node + 0xCC (minX, minY, minZ, maxX, maxY, maxZ)
#define OOB_OFFSET     0xCC
#define OOB_FLOAT_COUNT 6

static float original_scale;
static float original_oob[OOB_FLOAT_COUNT];
static int scale_modified;

static JOBJ *GetVisualJObj(void)
{
    GrObj *grobj = *stc_grobj;
    if (!grobj || !grobj->gobj)
        return NULL;
    return (JOBJ *)grobj->gobj->hsd_object;
}

static JOBJ *GetCollisionJObj(void)
{
    GrObj *grobj = *stc_grobj;
    if (!grobj)
        return NULL;
    return grobj->backdrop_jobj;
}

static float *GetOOBBounds(void)
{
    GrObj *grobj = *stc_grobj;
    if (!grobj || !grobj->gr_data || !grobj->gr_data->stage_node)
        return NULL;
    return (float *)((char *)grobj->gr_data->stage_node + OOB_OFFSET);
}

static void ApplyJObjScale(JOBJ *jobj, float s)
{
    if (!jobj)
        return;
    jobj->scale.X = s;
    jobj->scale.Y = s;
    jobj->scale.Z = s;
    JObj_SetMtxDirtySub(jobj);
}

void ScaleChange_Start(EventCheckData *ev_chk)
{
    GrObj *grobj = *stc_grobj;
    if (!grobj || !grobj->gr_data || !grobj->gr_data->stage_node)
    {
        OSReport("[ScaleChange] stage_node not available\n");
        scale_modified = 0;
        return;
    }

    // Save original scale
    original_scale = grobj->gr_data->stage_node->scale;

    // Save original OOB bounds
    float *oob = GetOOBBounds();
    if (oob)
    {
        for (int i = 0; i < OOB_FLOAT_COUNT; i++)
            original_oob[i] = oob[i];
    }

    // Apply new scale
    float new_scale = original_scale * SCALE_MULTIPLIER;

    ApplyJObjScale(GetVisualJObj(), new_scale);
    ApplyJObjScale(GetCollisionJObj(), new_scale);
    grobj->gr_data->stage_node->scale = new_scale;

    if (oob)
    {
        for (int i = 0; i < OOB_FLOAT_COUNT; i++)
            oob[i] = original_oob[i] * SCALE_MULTIPLIER;
    }

    scale_modified = 1;

    OSReport("[ScaleChange] start: original_scale=%.4f new_scale=%.4f\n",
             original_scale, new_scale);
}

void ScaleChange_Active(EventCheckData *ev_chk)
{
    if (!scale_modified)
        return;

    // Re-apply each frame as insurance
    float new_scale = original_scale * SCALE_MULTIPLIER;

    ApplyJObjScale(GetVisualJObj(), new_scale);
    ApplyJObjScale(GetCollisionJObj(), new_scale);

    GrObj *grobj = *stc_grobj;
    if (grobj && grobj->gr_data && grobj->gr_data->stage_node)
        grobj->gr_data->stage_node->scale = new_scale;

    float *oob = GetOOBBounds();
    if (oob)
    {
        for (int i = 0; i < OOB_FLOAT_COUNT; i++)
            oob[i] = original_oob[i] * SCALE_MULTIPLIER;
    }
}

void ScaleChange_End2(EventCheckData *ev_chk)
{
    if (!scale_modified)
        return;

    // Restore original scale
    ApplyJObjScale(GetVisualJObj(), original_scale);
    ApplyJObjScale(GetCollisionJObj(), original_scale);

    GrObj *grobj = *stc_grobj;
    if (grobj && grobj->gr_data && grobj->gr_data->stage_node)
        grobj->gr_data->stage_node->scale = original_scale;

    float *oob = GetOOBBounds();
    if (oob)
    {
        for (int i = 0; i < OOB_FLOAT_COUNT; i++)
            oob[i] = original_oob[i];
    }

    scale_modified = 0;
    OSReport("[ScaleChange] restored original scale\n");
}
