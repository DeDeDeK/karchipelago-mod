#include "game.h"
#include "os.h"
#include "inline.h"
#include "item.h"
#include "rider.h"
#include "stage.h"
#include "enemy.h"
#include "hud.h"
#include "hsd.h"
#include "obj.h"
#include "text.h"

#include "event_gourmet_race.h"

#define GOURMET_MAX_FOOD            60
#define GOURMET_BIG_COUNT           5
#define GOURMET_PREPLACED_COUNT     15
#define GOURMET_PREPLACED_MIN       5
#define GOURMET_PREPLACED_MAX       10
#define GOURMET_MIN_SPACING         (50.0f * 50.0f)
#define GOURMET_PREPLACED_HEIGHT    1.0f
#define GOURMET_SURFACE_HEIGHT      180.0f
#define GOURMET_ABOVE_SPLINE_HEIGHT 5.0f
#define GOURMET_UNDERGROUND_Y       44.0f
#define GOURMET_ANY_Y               1e30f
#define GOURMET_BIG_ITEM_SCALE      4.0f
#define GOURMET_ITEM_SCALE          2.0f
#define GOURMET_ITEM_LIFETIME       30000 // outlasts the event, so a vanished food was eaten
#define GOURMET_CITY_RADIUS         (350.0f * 350.0f)
#define GOURMET_CITY_CENTER_X       15.0f
#define GOURMET_CITY_CENTER_Z       (-267.4f)
#define MAX_CANDIDATES              802 // City Trial's Spline_GetCount

#define GOURMET_RESPAWN_TIME_BIG    (20 * 60)
#define GOURMET_RESPAWN_TIME        (10 * 60)
#define GOURMET_BIG_POINTS          10
#define GOURMET_REGULAR_POINTS      1

static const ItemKind food_kinds[] = {
    ITKIND_FOODMAXIMTOMATO,
    ITKIND_FOODENERGYDRINK,
    ITKIND_FOODICECREAM,
    ITKIND_FOODRICEBALL,
    ITKIND_FOODCHICKEN,
    ITKIND_FOODCURRY,
    ITKIND_FOODRAMEN,
    ITKIND_FOODOMELET,
    ITKIND_FOODHAMBURGER,
    ITKIND_FOODSUSHI,
    ITKIND_FOODHOTDOG,
    ITKIND_FOODAPPLE,
};
#define NUM_FOOD_KINDS (sizeof(food_kinds) / sizeof(food_kinds[0]))

static const Vec3 big_food_positions[GOURMET_BIG_COUNT] = {
    { 71.00f, 140.00f, -345.00f },   // tower high
    { 71.00f,  88.00f, -345.00f },   // tower low
    { -76.00f, 133.00f, -447.00f },  // random panel
    { -80.00f,  53.00f, -265.00f },  // under building 1
    { -2.00f,   5.00f,  -87.00f },   // underground garage
};

static const Vec3 preplaced_positions[GOURMET_PREPLACED_COUNT] = {
    { -28.93f,   6.74f, -204.55f },
    {  75.63f,  40.37f, -174.78f },
    { -54.18f,  30.25f,  -16.77f },
    {  52.05f,  31.15f,  -16.49f },
    { 196.41f,  18.79f,    2.47f },
    {  70.38f,  72.45f, -457.40f },
    {  -0.42f,  72.45f, -446.01f },
    { -75.77f,  70.09f, -351.28f },
    { -75.61f,  57.65f, -297.88f },
    { -74.69f,  50.54f, -225.20f },
    { -105.42f, 43.08f, -180.07f },
    { -74.01f,  36.64f, -140.79f },
    {  75.10f,  53.92f, -235.20f },
    { 142.07f,  47.70f, -207.03f },
    { -39.09f,  -2.61f,  134.16f },
};

typedef struct FoodSlot
{
    GOBJ *gobj;        // NULL while eaten
    ItemKind kind;     // kind and base_scale tell the food from a recycled GObj
    float base_scale;
    Vec3 spawn_pos;
    Vec3 last_pos;     // where the food was last seen, for scoring
    int coll_kind;
    int is_big;
    int respawn_timer; // frames until respawn while eaten
} FoodSlot;

static FoodSlot food_slots[GOURMET_MAX_FOOD];
static int num_food_slots;
static GOBJ *watcher_gobj;
static int scores[5];

#define HUD_MAX_PLAYERS     4
#define HUD_SCALE           8.0f
#define HUD_X               20.0f
#define HUD_Y_START         (-50.0f)
#define HUD_ROW_SPACING     (-40.0f)
#define HUD_GAUGE_X_OFFSET  40.0f
#define HUD_GAUGE_Y_OFFSET  35.0f // aligns the gauge with its label
#define GOURMET_HUD_FG_GXLINK 23

typedef struct ScoreHUD
{
    GOBJ *label_gobj; // ScInfPlynum model
    GOBJ *gauge_gobj; // ScInfPausegaugect model
    JOBJ *right_j;    // digit, child 4
    JOBJ *left_j;     // digit, child 5
    JOBJ *sign_j;     // minus sign, child 6
    JOBJ *bar_j;      // fill bar, child 1
    int prev_score;
} ScoreHUD;

static GOBJ *hud_camera_gobj;
static ScoreHUD score_huds[HUD_MAX_PLAYERS];
static int num_score_huds;

static void ScoreHUD_Create(void)
{
    num_score_huds = 0;
    hud_camera_gobj = NULL;

    HSD_Archive **arch = Gm_GetIfAllCityArchive();
    JOBJSet **gauge_sets = Archive_GetPublicAddress(*arch, "ScInfPausegaugect_scene_models");
    JOBJSet **plynum_sets = Archive_GetPublicAddress(*arch, "ScInfPlynum_scene_models");
    if (!gauge_sets || !plynum_sets)
    {
        OSReport("[GourmetRace] HUD models not found, no score display\n");
        return;
    }

    // Ortho camera that renders only the score HUD's GX link.
    hud_camera_gobj = GOBJ_EZCreator(0, 0, 0,
                                      0, 0,
                                      HSD_OBJKIND_COBJ, stc_text_cobjdesc,
                                      0, 0,
                                      CObjThink_Common, 0, 5);
    hud_camera_gobj->cobj_links = (1ULL << GOURMET_HUD_FG_GXLINK);
    CObj_SetOrtho(hud_camera_gobj->hsd_object, 0.0f, -480.0f, 0.0f, 640.0f);

    for (int i = 0; i < 5 && num_score_huds < HUD_MAX_PLAYERS; i++)
    {
        if (Ply_GetPKind(i) == PKIND_NONE)
            continue;

        ScoreHUD *hud = &score_huds[num_score_huds];
        float y = HUD_Y_START + HUD_ROW_SPACING * num_score_huds;

        // The P1, P2, ... label.
        hud->label_gobj = JObj_LoadSet_SetPri(
            0, plynum_sets[0], 0, (float)i,
            GAMEPLINK_HUD, GOURMET_HUD_FG_GXLINK, 1, NULL, 0);
        JOBJ *label_root = hud->label_gobj->hsd_object;
        label_root->scale.X = HUD_SCALE;
        label_root->scale.Y = HUD_SCALE;
        label_root->scale.Z = HUD_SCALE;
        label_root->trans.X = HUD_X;
        label_root->trans.Y = y;
        label_root->trans.Z = 0;
        JObj_SetMtxDirtySub(label_root);

        hud->gauge_gobj = JObj_LoadSet_SetPri(
            0, gauge_sets[0], 0, 0.0f,
            GAMEPLINK_HUD, GOURMET_HUD_FG_GXLINK, 1, NULL, 0);
        JOBJ *gauge_root = hud->gauge_gobj->hsd_object;
        JObj_ClearFlagsAll(gauge_root, JOBJ_HIDDEN);
        gauge_root->scale.X = HUD_SCALE;
        gauge_root->scale.Y = HUD_SCALE;
        gauge_root->scale.Z = HUD_SCALE;
        gauge_root->trans.X = HUD_X + HUD_GAUGE_X_OFFSET;
        gauge_root->trans.Y = y + HUD_GAUGE_Y_OFFSET;
        gauge_root->trans.Z = 0;
        JObj_SetMtxDirtySub(gauge_root);

        // Depth-first child indices in the gauge model.
        hud->bar_j = GObj_GetJObjIndex(hud->gauge_gobj, 1);
        hud->right_j = GObj_GetJObjIndex(hud->gauge_gobj, 4);
        hud->left_j = GObj_GetJObjIndex(hud->gauge_gobj, 5);
        hud->sign_j = GObj_GetJObjIndex(hud->gauge_gobj, 6);

        JObj_SetFlagsAll(hud->bar_j, JOBJ_HIDDEN);
        hud->sign_j->flags |= JOBJ_HIDDEN;
        hud->right_j->flags |= JOBJ_HIDDEN;
        HUD_UpdateElement(hud->left_j, 0);

        hud->prev_score = 0;
        num_score_huds++;
    }
}

static void ScoreHUD_Update(void)
{
    int row = 0;
    for (int i = 0; i < 5 && row < num_score_huds; i++)
    {
        if (Ply_GetPKind(i) == PKIND_NONE)
            continue;

        ScoreHUD *hud = &score_huds[row++];
        int score = scores[i] > 99 ? 99 : scores[i];
        if (score == hud->prev_score)
            continue;
        hud->prev_score = score;

        // The right digit shows only from 10 up; a lone digit on the left one reads
        // as centered.
        if (score >= 10)
        {
            hud->right_j->flags &= ~JOBJ_HIDDEN;
            HUD_UpdateElement(hud->right_j, score % 10);
            HUD_UpdateElement(hud->left_j, score / 10);
        }
        else
        {
            hud->right_j->flags |= JOBJ_HIDDEN;
            HUD_UpdateElement(hud->left_j, score);
        }

        // AnimAll can clear these flags, so re-hide on every update.
        hud->sign_j->flags |= JOBJ_HIDDEN;
        hud->bar_j->flags |= JOBJ_HIDDEN;
        for (JOBJ *child = hud->bar_j->child; child; child = child->sibling)
            child->flags |= JOBJ_HIDDEN;
    }
}

static void ScoreHUD_Destroy(void)
{
    for (int i = 0; i < num_score_huds; i++)
    {
        GObj_Destroy(score_huds[i].gauge_gobj);
        GObj_Destroy(score_huds[i].label_gobj);
    }
    num_score_huds = 0;

    if (hud_camera_gobj)
    {
        GObj_Destroy(hud_camera_gobj);
        hud_camera_gobj = NULL;
    }
}

static int FoodSlot_Spawn(FoodSlot *slot)
{
    // The engine builds the item's render matrix from up x forward; a zero forward
    // collapses the model to an invisible, still pickable sliver. up stays NULL so
    // the food tilts to the ground normal.
    Vec3 forward = { 0.0f, 0.0f, 1.0f };
    ItemKind kind = food_kinds[HSD_Randi(NUM_FOOD_KINDS)];
    float scale = slot->is_big ? GOURMET_BIG_ITEM_SCALE : GOURMET_ITEM_SCALE;

    ItemDesc desc;
    Item_InitDesc(&desc, kind, scale, 0,
                  &slot->spawn_pos, NULL, &forward, -1, -1,
                  0, slot->coll_kind, -1, -1);
    GOBJ *item = CityItem_Create(&desc);
    if (!item)
        return 0;

    ItemData *id = item->userdata;
    id->lifetime = GOURMET_ITEM_LIFETIME;

    slot->gobj = item;
    slot->kind = kind;
    slot->base_scale = id->base_scale;
    slot->last_pos = slot->spawn_pos;
    return 1;
}

// Freed GObjs are handed out again first, so an eaten food's GObj can already be
// another item; kind and base_scale tell ours apart.
static ItemData *FoodSlot_GetLive(FoodSlot *slot)
{
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_ITEM]; g; g = g->next)
    {
        if (g != slot->gobj)
            continue;

        ItemData *id = g->userdata;
        if (id->kind == slot->kind && id->base_scale == slot->base_scale)
            return id;
        return NULL;
    }
    return NULL;
}

static int IsTooClose(const Vec3 *pos)
{
    for (int i = 0; i < num_food_slots; i++)
    {
        float dx = pos->X - food_slots[i].spawn_pos.X;
        float dz = pos->Z - food_slots[i].spawn_pos.Z;
        if (dx * dx + dz * dz < GOURMET_MIN_SPACING)
            return 1;
    }
    return 0;
}

// Spawns a food at base + height into the next slot. Returns 1 on success.
static int PlaceFood(const Vec3 *base, float height, int coll_kind, int is_big)
{
    if (num_food_slots >= GOURMET_MAX_FOOD)
        return 0;

    FoodSlot *slot = &food_slots[num_food_slots];
    slot->spawn_pos.X = base->X;
    slot->spawn_pos.Y = base->Y + height;
    slot->spawn_pos.Z = base->Z;
    slot->coll_kind = coll_kind;
    slot->is_big = is_big;
    slot->respawn_timer = 0;
    if (!FoodSlot_Spawn(slot))
        return 0;

    num_food_slots++;
    return 1;
}

static void ShuffleVecs(Vec3 *arr, int count)
{
    for (int i = count - 1; i > 0; i--)
    {
        int j = HSD_Randi(i + 1);
        Vec3 tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

// Spline midpoints within the city radius.
static int CollectCandidates(Vec3 *out, int max_out)
{
    int spline_count = Spline_GetCount();
    int num = 0;

    for (int seg = 0; seg < spline_count && num < max_out; seg++)
    {
        void *spline = Spline_GetForward(seg);
        if (!spline)
            continue;

        Vec3 pt;
        splGetSplinePoint(&pt, spline, 0.5f);

        float dx = pt.X - GOURMET_CITY_CENTER_X;
        float dz = pt.Z - GOURMET_CITY_CENTER_Z;
        if (dx * dx + dz * dz < GOURMET_CITY_RADIUS)
            out[num++] = pt;
    }
    return num;
}

// Spawns up to `target` foods on shuffled candidates below max_y that keep
// GOURMET_MIN_SPACING from every food placed so far. Returns the number spawned.
static int PlaceOnSplines(Vec3 *candidates, int num, int target, float height, int coll_kind, float max_y)
{
    ShuffleVecs(candidates, num);

    int spawned = 0;
    for (int i = 0; i < num && spawned < target; i++)
    {
        if (candidates[i].Y >= max_y || IsTooClose(&candidates[i]))
            continue;
        spawned += PlaceFood(&candidates[i], height, coll_kind, 0);
    }
    return spawned;
}

// Big foods at fixed landmarks, then 5-10 of the pre-placed spots, then the rest of
// the budget split between drops onto the surface and underground spline points,
// with any underground shortfall dropped onto the surface instead. The game's item
// cap can leave the total short.
static void GourmetRace_SpawnFood(void)
{
    num_food_slots = 0;

    for (int i = 0; i < GOURMET_BIG_COUNT; i++)
        PlaceFood(&big_food_positions[i], GOURMET_PREPLACED_HEIGHT, 2, 1);

    Vec3 preplaced[GOURMET_PREPLACED_COUNT];
    for (int i = 0; i < GOURMET_PREPLACED_COUNT; i++)
        preplaced[i] = preplaced_positions[i];
    ShuffleVecs(preplaced, GOURMET_PREPLACED_COUNT);

    int preplaced_target = GOURMET_PREPLACED_MIN
        + HSD_Randi(GOURMET_PREPLACED_MAX - GOURMET_PREPLACED_MIN + 1);
    int spawned = 0;
    for (int i = 0; i < GOURMET_PREPLACED_COUNT && spawned < preplaced_target; i++)
        spawned += PlaceFood(&preplaced[i], GOURMET_PREPLACED_HEIGHT, 2, 0);

    static Vec3 candidates[MAX_CANDIDATES];
    int num = CollectCandidates(candidates, MAX_CANDIDATES);
    int remaining = GOURMET_MAX_FOOD - num_food_slots;

    int surface = PlaceOnSplines(candidates, num, remaining / 2,
                                 GOURMET_SURFACE_HEIGHT, 3, GOURMET_ANY_Y);
    int underground_target = remaining - surface;
    int underground = PlaceOnSplines(candidates, num, underground_target,
                                     GOURMET_ABOVE_SPLINE_HEIGHT, 2, GOURMET_UNDERGROUND_Y);
    PlaceOnSplines(candidates, num, underground_target - underground,
                   GOURMET_SURFACE_HEIGHT, 3, GOURMET_ANY_Y);
}

// Returns a player index, or -1 if no slot has a rider.
static int FindNearestPlayer(const Vec3 *pos)
{
    int best = -1;
    float best_dist = 1e18f;
    for (int i = 0; i < 5; i++)
    {
        GOBJ *rg = Ply_GetRiderGObj(i);
        if (!rg)
            continue;

        RiderData *rd = rg->userdata;
        float dx = rd->pos.X - pos->X;
        float dy = rd->pos.Y - pos->Y;
        float dz = rd->pos.Z - pos->Z;
        float dist = dx * dx + dy * dy + dz * dz;
        if (dist < best_dist)
        {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}

// Scores eaten foods and runs their respawn timers. GAMEPLINK_1 freezes with the
// pause and runs ahead of the event proc on GAMEPLINK_CITYEVENTSPAWN, so End2 never
// sees a food eaten since the last pass.
static void GourmetRace_WatcherProc(GOBJ *gobj)
{
    for (int i = 0; i < num_food_slots; i++)
    {
        FoodSlot *slot = &food_slots[i];

        if (slot->gobj)
        {
            ItemData *id = FoodSlot_GetLive(slot);
            if (id)
            {
                // Landing zeroes ItemData.forward, which hides the model again.
                id->forward.X = 0.0f;
                id->forward.Y = 0.0f;
                id->forward.Z = 1.0f;
                slot->last_pos = id->pos;
                continue;
            }

            int ply = FindNearestPlayer(&slot->last_pos);
            if (ply >= 0)
                scores[ply] += slot->is_big ? GOURMET_BIG_POINTS : GOURMET_REGULAR_POINTS;

            slot->gobj = NULL;
            slot->respawn_timer = slot->is_big ? GOURMET_RESPAWN_TIME_BIG : GOURMET_RESPAWN_TIME;
        }
        else if (--slot->respawn_timer <= 0 && !FoodSlot_Spawn(slot))
        {
            // Item cap reached; retry next frame.
            slot->respawn_timer = 1;
        }
    }

    ScoreHUD_Update();
}

void GourmetRace_Start(void)
{
    for (int i = 0; i < 5; i++)
        scores[i] = 0;

    GourmetRace_SpawnFood();

    watcher_gobj = GObj_Create(0, GAMEPLINK_1, 0);
    GObj_AddProc(watcher_gobj, GourmetRace_WatcherProc, 0);

    ScoreHUD_Create();

    OSReport("[GourmetRace] Started with %d food and %d score row(s)\n",
             num_food_slots, num_score_huds);
}

void GourmetRace_End2(void)
{
    GObj_Destroy(watcher_gobj);
    watcher_gobj = NULL;

    for (int i = 0; i < num_food_slots; i++)
    {
        if (FoodSlot_GetLive(&food_slots[i]))
            GObj_Destroy(food_slots[i].gobj);
    }
    num_food_slots = 0;

    ScoreHUD_Destroy();

    OSReport("[GourmetRace] Final scores: P1=%d P2=%d P3=%d P4=%d P5=%d\n",
             scores[0], scores[1], scores[2], scores[3], scores[4]);

    int best_score = 0;
    for (int i = 0; i < 5; i++)
    {
        if (scores[i] > best_score)
            best_score = scores[i];
    }
    if (best_score == 0)
        return;

    int winners = 0;
    for (int i = 0; i < 5; i++)
    {
        if (scores[i] == best_score)
            winners++;
    }

    // A tie halves the prize.
    int allups = winners > 1 ? 1 : 2;
    for (int i = 0; i < 5; i++)
    {
        if (scores[i] != best_score)
            continue;
        for (int j = 0; j < allups; j++)
            SpawnItemPlayer(i, ITKIND_ALLUP);
    }

    OSReport("[GourmetRace] %d winner(s) at %d points, %d All Up(s) each\n",
             winners, best_score, allups);
}
