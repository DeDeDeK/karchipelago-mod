#include <string.h>

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "obj.h"
#include "code_patch/code_patch.h"
#include "inline.h"

#include "custom_weather.h"
#include "weather_fx.h"

// A stage light a preset can recolour, and the colours it loaded with, which the
// vanilla presets get back.
typedef struct LightTint
{
    LOBJ   *light;
    GXColor color;
    GXColor hw_color;
} LightTint;

static int       s_lights_resolved = 0;
static LightTint s_terrain;  // *stc_main_light, the primary chain's INFINITE light
static LightTint s_ambient;  // the primary chain's ambient light

static int s_last_seen_preset_idx = -1;

static void LightTint_Capture(LightTint *t, LOBJ *l)
{
    t->light = l;
    if (l)
    {
        t->color = l->color;
        t->hw_color = l->hw_color;
    }
}

static void LightTint_Apply(const LightTint *t, u32 color, u32 hw_color)
{
    if (!t->light)
        return;
    t->light->color = color ? GXColor_Unpack(color) : t->color;
    t->light->hw_color = hw_color ? GXColor_Unpack(hw_color) : t->hw_color;
}

// The primary chain - an ambient, then the INFINITE light *stc_main_light caches - is
// the object of the light GObj Light_CreateForStage (0x800d5fd0) builds on p_link 1.
static LOBJ *StageAmbientLight(void)
{
    LOBJ *main_light = *stc_main_light;
    if (!main_light)
        return NULL;

    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_1]; g != NULL; g = g->next)
    {
        if (g->obj_kind != HSD_OBJKIND_LOBJ)
            continue;
        LOBJ *ambient = NULL;
        int has_main = 0;
        for (LOBJ *l = g->hsd_object; l != NULL; l = l->next)
        {
            if (l == main_light)
                has_main = 1;
            else if (!ambient && (l->flags & LOBJ_TYPE_MASK) == LOBJ_AMBIENT)
                ambient = l;
        }
        if (has_main)
            return ambient;
    }
    return NULL;
}

// Comma-joins the layers the preset itself switches on; the menu's overrides can add
// or remove some. `out` is sized for every name plus its separator.
static void ListPresetLayers(const CustomPresetDef *def, char *out)
{
    out[0] = '\0';
    if (def)
    {
        const struct
        {
            const char *name;
            int on;
        } layers[] = {
            {"terrain", def->terrain_diffuse || def->terrain_specular
                        || def->terrain_ambient || def->terrain_ambient_specular},
            {"tint", def->screen_tint != 0},
            {"rain", def->rain.enabled},
            {"hail", def->hail.enabled},
            {"snow", def->snow.enabled},
            {"lightning", def->lightning.enabled},
            {"wind", def->wind.enabled},
            {"puddles", def->puddles.enabled},
            {"clouds", def->clouds.enabled},
            {"moon", def->moon.enabled},
            {"stars", def->stars.enabled},
            {"volcano", def->volcano.enabled},
            {"tornado", def->tornado.enabled},
            {"earthquake", def->earthquake.enabled},
            {"tsunami", def->tsunami.enabled},
            {"glow", def->glow.enabled},
            {"starlight", def->starlight.enabled},
        };
        for (int i = 0; i < (int)GetElementsIn(layers); i++)
        {
            if (!layers[i].on)
                continue;
            if (out[0])
                strcat(out, ", ");
            strcat(out, layers[i].name);
        }
    }
    if (!out[0])
        strcpy(out, "none");
}

void CustomWeatherRuntime_Reset(void)
{
    s_lights_resolved = 0;
    s_last_seen_preset_idx = -1;
    Rain_Reset();
    Snow_Reset();
    Lightning_Reset();
    Wind_Reset();
    Puddle_Reset();
    Hail_Reset();
    Tree_Reset();
    Cloud_Reset();
    Moon_Reset();
    Star_Reset();
    Volcano_Reset();
    Tornado_Reset();
    Earthquake_Reset();
    Tsunami_Reset();
    Glow_Reset();
    StarLight_Reset();
}

void CustomWeatherRuntime_Tick(GrObj *grobj)
{
    // The City Trial ground: City Trial, Free Run and the Destruction Derby 4 and 5
    // stadiums. Every other stage keeps its own fog and sky.
    if (grobj->gr_kind != GR_CITY1)
        return;

    GOBJ *fog_gobj = grobj->fog_gobj;
    if (!fog_gobj)
        return;
    GrFogData *fog_data = (GrFogData *)fog_gobj->userdata;
    if (!fog_data)
        return;

    HSD_Fog *fog = (HSD_Fog *)fog_gobj->hsd_object;

    if (!s_lights_resolved)
    {
        LightTint_Capture(&s_terrain, *stc_main_light);
        LightTint_Capture(&s_ambient, StageAmbientLight());
        s_lights_resolved = 1;
    }

    int idx = fog_data->fog_kind;
    if (idx != s_last_seen_preset_idx)
    {
        s_last_seen_preset_idx = idx;
        const CustomPresetDef *def = CustomWeather_GetPresetDef(idx);
        LightTint_Apply(&s_terrain, def ? def->terrain_diffuse : 0,
                        def ? def->terrain_specular : 0);
        LightTint_Apply(&s_ambient, def ? def->terrain_ambient : 0,
                        def ? def->terrain_ambient_specular : 0);

        // Sky_Update never writes HSD_Fog.type or scale, so one write holds until the
        // next preset change.
        if (fog)
        {
            fog->type = (def && def->fog_type) ? def->fog_type : GX_FOG_PERSP_LIN;
            fog->scale = CustomWeather_GetFogScale();
        }

        Rain_SetActive(def ? &def->rain : 0);
        Snow_SetActive(def ? &def->snow : 0);
        Hail_SetActive(def ? &def->hail : 0);
        Lightning_SetActive(def ? &def->lightning : 0);
        Wind_SetActive(def ? &def->wind : 0);
        Puddle_SetActive(def ? &def->puddles : 0);
        Cloud_SetActive(def ? &def->clouds : 0);
        Moon_SetActive(def ? &def->moon : 0);
        Star_SetActive(def ? &def->stars : 0);
        Volcano_SetActive(def ? &def->volcano : 0);
        Tornado_SetActive(def ? &def->tornado : 0);
        Earthquake_SetActive(def ? &def->earthquake : 0);
        Tsunami_SetActive(def ? &def->tsunami : 0);
        Glow_SetActive(def ? &def->glow : 0);
        StarLight_SetActive(def ? &def->starlight : 0);

        // The round's first preset arrives through Sky_LoadPreset, which fires no
        // overlay, so the tint is driven here: lerped over 30 frames, then held.
        if (grobj->fade_slot_id && def && def->screen_tint)
        {
            u32 tint = def->screen_tint;
            Sky_BeginFade(grobj, &tint, 30);
        }

        char layers[128];
        ListPresetLayers(def, layers);
        OSReport("[WeatherRuntime] Preset %d (%s) applied, preset layers: %s\n",
                 idx, CustomWeather_GetPresetName(idx), layers);
    }

    Lightning_Tick(fog);
    // Wind first, so every layer that reads it sees this frame's vector.
    Wind_Tick();
    Rain_Tick();
    Snow_Tick();
    Hail_Tick();
    Puddle_Tick();
    Tree_Tick();
    Cloud_Tick();
    Moon_Tick();
    Star_Tick();
    Volcano_Tick();
    Tornado_Tick();
    Earthquake_Tick();
    Tsunami_Tick();
    // The star before the glow, whose map draw picks up the star's TObjs.
    StarLight_Tick();
    Glow_Tick();
}

// In Gr_Think (0x800ce618), immediately after `bl Sky_Update`; r31 = grobj,
// callee-saved across the bl.
CODEPATCH_HOOKCREATE(0x800ce648,
                     "mr 3, 31\n\t",
                     CustomWeatherRuntime_Tick,
                     "",
                     0)

void CustomWeatherRuntime_OnBoot(void)
{
    CODEPATCH_HOOKAPPLY(0x800ce648);
    OSReport("[WeatherRuntime] Hooks installed\n");
}
