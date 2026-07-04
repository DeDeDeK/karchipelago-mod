// Shared helpers for the custom_weather effect layers: a common translucent GX
// pass setup and world-camera GX-layer creation, plus the symmetric RNG.

#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "gx.h"

#include "weather_fx.h"

float Weather_Randf2(void)
{
    return HSD_Randf() * 2.0f - 1.0f;
}

void WeatherGX_BeginXlu(COBJ *cam, int additive, int line_width)
{
    HSD_StateInitDirect(GX_VTXFMT0, 2);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetNumTexGens(0);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0, GX_DISABLE, Vertex, Vertex, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetChanCtrl(GX_ALPHA0, GX_DISABLE, Vertex, Vertex, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA,
                   additive ? GX_BL_ONE : GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZMode(GX_ENABLE, GX_LEQUAL, GX_DISABLE);
    GXSetCullMode(GX_CULL_NONE);
    if (line_width > 0)
        GXSetLineWidth((u8)line_width, 5);
    GXLoadPosMtxImm(&cam->view_mtx, GX_PNMTX0);
}

GOBJ *WeatherGX_EnsureLayer(int entity_class, int p_link, void *cb,
                            int gx_link, int gx_pri, const char *log)
{
    GOBJ *g = GObj_Create(entity_class, p_link, 0);
    if (!g)
        return NULL;
    GObj_AddGXLink(g, cb, gx_link, gx_pri);
    if (log)
        OSReport("%s\n", log);
    return g;
}
