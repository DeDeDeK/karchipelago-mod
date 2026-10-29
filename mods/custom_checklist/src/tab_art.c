#include "game.h"
#include "hsd.h"
#include "os.h"
#include "scene.h"

#include "custom_checklist.h"

// The vanilla banner quad on the frame GObj; its 248 width is unique in that scene.
#define CC_BANNER_TEX_W 248

// The vanilla tab emblem quad; a 40-wide I4 texture is unique in the background scene.
#define CC_EMBLEM_TEX_W 40

// Loaded into the per-scene heap, so valid only for the current tab's scene. NULL leaves
// the vanilla art in place.
static _HSD_ImageDesc *banner_img;
static _HSD_ImageDesc *emblem_img;

// The JOBJ walk callbacks take only a JOBJ, so the current tab's theme rides here.
static GXColor theme;

void CCArt_Load(int idx)
{
    CCTab *t = &cc_tabs[idx];
    const CustomChecklistDesc *d = &t->desc;
    banner_img = NULL; // the prior scene's are reclaimed
    emblem_img = NULL;
    if (!d->tex_file)
        return;

    // Gm_LoadGameFile asserts on a file that is not on disc. Every visit to the tab lands
    // here, so each report latches.
    if (DVDConvertPathToEntrynum(Archive_AppendExtension((char *)d->tex_file)) == -1)
    {
        if (!t->art_reported)
        {
            t->art_reported = 1;
            OSReport("[CustomChecklist] '%s': %s.dat not on disc, tab art disabled\n",
                     d->name, d->tex_file);
        }
        return;
    }

    HSD_Archive *arc = NULL;
    Gm_LoadGameFile(&arc, (char *)d->tex_file);
    if (d->banner_symbol)
        banner_img = Archive_GetPublicAddress(arc, (char *)d->banner_symbol);
    if (d->emblem_symbol)
        emblem_img = Archive_GetPublicAddress(arc, (char *)d->emblem_symbol);
    if ((!banner_img || !emblem_img) && !t->art_reported)
    {
        t->art_reported = 1;
        OSReport("[CustomChecklist] %s.dat texture symbols: banner %s, emblem %s\n",
                 d->tex_file, banner_img ? "ok" : "missing", emblem_img ? "ok" : "missing");
    }
}

// Retint one material diffuse onto the theme color, preserving the material's
// [min, green] brightness range. The green-dominance gate selects only the borrowed City
// Trial tint materials and, the theme not being green-dominant, skips a retinted one.
static void CC_RemapDiffuse(HSD_Material *mat)
{
    u8 r = mat->diffuse.r, g = mat->diffuse.g, b = mat->diffuse.b;
    if (!(g > r && g >= b))
        return;

    int tmax = theme.r;
    if (theme.g > tmax) tmax = theme.g;
    if (theme.b > tmax) tmax = theme.b;
    if (tmax == 0)
        return;

    int m = r < b ? r : b;
    int span = g - m;
    mat->diffuse.r = (u8)(m + span * theme.r / tmax);
    mat->diffuse.g = (u8)(m + span * theme.g / tmax);
    mat->diffuse.b = (u8)(m + span * theme.b / tmax);
}

// Siblings are a flat list, so only descent counts against the depth cap.
static void CC_WalkJObjTree(JOBJ *root, void (*fn)(JOBJ *), int depth)
{
    if (depth > 32)
        return;
    for (JOBJ *j = root; j; j = j->sibling)
    {
        fn(j);
        CC_WalkJObjTree(j->child, fn, depth + 1);
    }
}

// The GObj's root JOBJ and its child subtree, not the root's siblings, which belong to
// other scenes.
static void CC_WalkGObj(GOBJ *gobj, void (*fn)(JOBJ *))
{
    if (!gobj || !gobj->hsd_object)
        return;
    JOBJ *root = (JOBJ *)gobj->hsd_object;
    fn(root);
    CC_WalkJObjTree(root->child, fn, 0);
}

// Retint one JOBJ's dobjs and swap the tab emblem's texture in the same pass - the emblem
// lives in the recolored background scene, so it rides this walk.
static void CC_ProcessJObj(JOBJ *j)
{
    for (DOBJ *dj = j->dobj; dj; dj = dj->next)
    {
        MOBJ *mo = dj->mobj;
        if (!mo)
            continue;
        if (mo->mat)
            CC_RemapDiffuse(mo->mat);
        if (!emblem_img)
            continue;
        for (TOBJ *t = mo->tobj; t; t = t->next)
        {
            _HSD_ImageDesc *img = t->imagedesc;
            if (!img || img == emblem_img)
                continue;
            if (img->width != CC_EMBLEM_TEX_W || img->format != GX_TF_I4)
                continue;
            // The vanilla emblem is a flipbook whose anim pass rewrites imagedesc every
            // tick; clearing aobj/imagetbl leaves this descriptor the only binding.
            t->imagedesc = emblem_img;
            t->aobj = NULL;
            t->imagetbl = NULL;
        }
    }
}

// Diffuse is forced white so the swapped texture samples neutrally.
static void CC_RetargetBannerJObj(JOBJ *j)
{
    if (!banner_img)
        return;

    for (DOBJ *dj = j->dobj; dj; dj = dj->next)
    {
        MOBJ *mo = dj->mobj;
        if (!mo)
            continue;
        for (TOBJ *t = mo->tobj; t; t = t->next)
        {
            _HSD_ImageDesc *img = t->imagedesc;
            if (!img || (img != banner_img && img->width != CC_BANNER_TEX_W))
                continue;
            t->imagedesc = banner_img;
            if (mo->mat)
            {
                // Alpha untouched, keeping the quad's blend.
                mo->mat->diffuse.r = 0xFF;
                mo->mat->diffuse.g = 0xFF;
                mo->mat->diffuse.b = 0xFF;
            }
        }
    }
}

// A custom tab's cb_ThinkPostGObjProc2: every frame, after the GObj procs' material
// animation re-applies City Trial's green.
void CCArt_Apply(void)
{
    int idx = CCScene_FindTab(Scene_GetCurrentMinor());
    if (idx < 0)
        return;

    theme = cc_tabs[idx].desc.theme;

    // The background scene and marker GObjs carry the per-mode tint in their material
    // diffuses; the frame GObj is texture-colored, so it only takes the banner swap.
    ScMenuCommon *mm = Gm_GetMenuData();
    CC_WalkGObj(mm->clearchecker.bg_gobj, CC_ProcessJObj);
    CC_WalkGObj(mm->clearchecker.cross_gobj, CC_ProcessJObj);
    CC_WalkGObj(mm->clearchecker.prize1_gobj, CC_ProcessJObj);
    CC_WalkGObj(mm->clearchecker.prize2_gobj, CC_ProcessJObj);
    CC_WalkGObj(mm->clearchecker.frame_gobj, CC_RetargetBannerJObj);
}
