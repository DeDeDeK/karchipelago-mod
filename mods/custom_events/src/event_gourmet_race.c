// Gourmet Race — custom City Trial event
//
// Spawns food items across the city in four passes:
//   1. Big foods at 5 pre-placed locations (large scale, no city radius check)
//   2. Regular foods at 5-10 of 15 pre-placed locations (no city radius check)
//   3. Random foods at spline midpoints, high up + ground snap (city radius check)
//   4. Random foods at spline midpoints underground, just above spline (city radius check)
//
// Total target: up to 60 foods (capped by city item limit of ~100).
// Eaten foods respawn at the same location after a cooldown.

#include "game.h"
#include "os.h"
#include "inline.h"
#include "item.h"
#include "stage.h"
#include "enemy.h"
#include "hud.h"
#include "hsd.h"

#include "event_gourmet_race.h"
#include "spawn_item.h"

// --- Configuration ---
#define GOURMET_MAX_FOOD            60
#define GOURMET_BIG_COUNT           5
#define GOURMET_PREPLACED_COUNT     15
#define GOURMET_PREPLACED_MIN       5
#define GOURMET_PREPLACED_MAX       10
#define GOURMET_MIN_SPACING         (50.0f * 50.0f)
#define GOURMET_SURFACE_HEIGHT      180.0f
#define GOURMET_ABOVE_SPLINE_HEIGHT 5.0f
#define GOURMET_BIG_ITEM_SCALE      4.0f
#define GOURMET_ITEM_SCALE          2.0f
#define GOURMET_ITEM_LIFETIME       30000  // ~8 min, outlasts event duration so disappearance = player pickup
#define GOURMET_CITY_RADIUS         (350.0f * 350.0f)
#define GOURMET_CITY_CENTER_X       15.0f
#define GOURMET_CITY_CENTER_Z       (-267.4f)
#define MAX_CANDIDATES              802

#define GOURMET_RESPAWN_TIME_BIG    (20 * 60)  // 20 seconds at 60fps
#define GOURMET_RESPAWN_TIME        (10 * 60)  // 10 seconds at 60fps
#define GOURMET_BIG_POINTS          10
#define GOURMET_REGULAR_POINTS      1

// --- Food item kinds for random selection ---
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

static ItemKind RandomFoodKind(void)
{
    return food_kinds[HSD_Randi(NUM_FOOD_KINDS)];
}

// --- Pre-placed locations ---
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

// --- Food slot tracking for respawn ---
typedef struct FoodSlot
{
    Vec3 spawn_pos;     // position to spawn at (with Y offset already applied)
    ItemKind kind;      // food kind (re-randomized on respawn)
    float scale;        // item scale
    int coll_kind;      // collision kind (2 or 3)
    GOBJ *gobj;         // current item GObj, NULL if eaten/pending
    int respawn_timer;  // frames until respawn, 0 = not pending
    int is_big;         // big food = longer respawn
} FoodSlot;

static FoodSlot food_slots[GOURMET_MAX_FOOD];
static int num_food_slots;
static GOBJ *watcher_gobj;
static int gourmet_active;
static int scores[5]; // per-player scores

// --- Score HUD ---
#define HUD_MAX_PLAYERS     4
#define HUD_SCALE           8.0f
#define HUD_X               20.0f
#define HUD_Y_START         (-50.0f)
#define HUD_ROW_SPACING     (-40.0f)
#define HUD_GAUGE_X_OFFSET  40.0f
#define HUD_GAUGE_Y_OFFSET  35.0f   // nudge gauge up to align with label
#define GOURMET_HUD_BG_GXLINK   22  // background renders first
#define GOURMET_HUD_FG_GXLINK   23  // foreground (labels + digits) renders on top
#define HUD_BG_COLOR_R      0
#define HUD_BG_COLOR_G      0
#define HUD_BG_COLOR_B      0
#define HUD_BG_COLOR_A      50
#define HUD_BG_PAD_X        8.0f
#define HUD_BG_PAD_Y        5.0f

// Functions not in link.ld
static void (*CObj_GX)(GOBJ *g, int pass) = (void *)0x8042a29c;
static void (*pGXSetBlendMode)(int type, int src_factor, int dst_factor, int op) = (void *)0x803cf820;

static GOBJ *hud_camera_gobj;
static GOBJ *hud_bg_gobj;

typedef struct ScoreHUD
{
    GOBJ *label_gobj;   // ScInfPlynum model
    GOBJ *gauge_gobj;   // ScInfPausegaugect model
    JOBJ *ones_j;       // ones digit (child 6)
    JOBJ *tens_j;       // tens digit (child 5)
    JOBJ *sign_j;       // minus sign (child 4)
    JOBJ *bar_j;        // fill bar (child 1)
    int prev_score;     // for change detection
} ScoreHUD;

static ScoreHUD score_huds[HUD_MAX_PLAYERS];
static int num_score_huds;

static void ScoreHUD_BGRender(GOBJ *gobj, int pass)
{
    if (pass != 0 || num_score_huds == 0)
        return;

    // Compute bounding box around all HUD rows.
    float top = HUD_Y_START + HUD_BG_PAD_Y;
    float bottom = HUD_Y_START + HUD_ROW_SPACING * (num_score_huds - 1)
                   - HUD_BG_PAD_Y;
    float left = HUD_X - HUD_BG_PAD_X;
    float right = HUD_X + HUD_GAUGE_X_OFFSET + 60.0f + HUD_BG_PAD_X;

    // Use GX_DrawRect (known working), then we'll address transparency later.
    Vec3 bl = { left, bottom, 0 };
    Vec3 tr = { right, top, 0 };
    GXColor color = { HUD_BG_COLOR_R, HUD_BG_COLOR_G, HUD_BG_COLOR_B, HUD_BG_COLOR_A };
    GX_DrawRect(&bl, &tr, &color);
}

static void ScoreHUD_Create(void)
{
    HSD_Archive **arch = Gm_GetIfAllCityArchive();
    JOBJSet **gauge_sets = Archive_GetPublicAddress(*arch, "ScInfPausegaugect_scene_models");
    JOBJSet **plynum_sets = Archive_GetPublicAddress(*arch, "ScInfPlynum_scene_models");
    if (!gauge_sets || !plynum_sets)
    {
        OSReport("[GourmetRace] Failed to load HUD archives!\n");
        return;
    }

    // Create a dedicated ortho camera for our HUD.
    // Two GX links: BG (22) renders first, FG (23) renders on top.
    hud_camera_gobj = GOBJ_EZCreator(0, 0, 0,
                                      0, 0,
                                      HSD_OBJKIND_COBJ, (COBJDesc *)0x805096a0,
                                      0, 0,
                                      CObj_GX, 0, 5);
    hud_camera_gobj->cobj_links = (1ULL << GOURMET_HUD_BG_GXLINK)
                                | (1ULL << GOURMET_HUD_FG_GXLINK);
    COBJ *cam_cobj = hud_camera_gobj->hsd_object;
    CObj_SetOrtho(cam_cobj, 0.0f, -480.0f, 0.0f, 640.0f);

    // Background rectangle GOBJ on BG link (renders behind everything)
    hud_bg_gobj = GObj_Create(0, 0, 0);
    GObj_AddGXLink(hud_bg_gobj, ScoreHUD_BGRender, GOURMET_HUD_BG_GXLINK, 0);

    num_score_huds = 0;

    for (int i = 0; i < 5 && num_score_huds < HUD_MAX_PLAYERS; i++)
    {
        if (Ply_GetPKind(i) == PKIND_NONE)
            continue;

        ScoreHUD *hud = &score_huds[num_score_huds];
        float y = HUD_Y_START + HUD_ROW_SPACING * num_score_huds;

        // Player label (P1, P2, etc.)
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

        // Gauge
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

        // Cache child JOBJs (depth-first traversal indices)
        // Child 4 = tens digit, child 5 = ones digit, child 6 = sign/minus
        hud->bar_j  = GObj_GetJObjIndex(hud->gauge_gobj, 1);
        hud->tens_j = GObj_GetJObjIndex(hud->gauge_gobj, 4);
        hud->ones_j = GObj_GetJObjIndex(hud->gauge_gobj, 5);
        hud->sign_j = GObj_GetJObjIndex(hud->gauge_gobj, 6);

        // Hide unwanted elements: bar and its subtree, sign
        JObj_SetFlagsAll(hud->bar_j, JOBJ_HIDDEN);
        hud->sign_j->flags |= JOBJ_HIDDEN;
        // Hide tens initially (shown when score >= 10)
        hud->tens_j->flags |= JOBJ_HIDDEN;

        // Set ones digit to 0
        HUD_UpdateElement(hud->ones_j, 0);

        hud->prev_score = 0;
        num_score_huds++;
    }

    OSReport("[GourmetRace] Score HUD created for %d players\n", num_score_huds);
}

static void ScoreHUD_Update(void)
{
    int score_idx = 0;
    for (int i = 0; i < 5 && score_idx < num_score_huds; i++)
    {
        if (Ply_GetPKind(i) == PKIND_NONE)
            continue;

        ScoreHUD *hud = &score_huds[score_idx];
        int score = scores[i];
        if (score > 99) score = 99;

        if (score != hud->prev_score)
        {
            // Child 4 is visually right, child 5 is visually left.
            // For single digit: show on child 5 (left position, looks centered).
            // For two digits: tens on child 5 (left), ones on child 4 (right).
            if (score >= 10)
            {
                hud->tens_j->flags &= ~JOBJ_HIDDEN;
                HUD_UpdateElement(hud->tens_j, score % 10);  // child 4 = right = ones
                HUD_UpdateElement(hud->ones_j, score / 10);  // child 5 = left = tens
            }
            else
            {
                hud->tens_j->flags |= JOBJ_HIDDEN;
                HUD_UpdateElement(hud->ones_j, score % 10);
            }

            // Re-hide sign and bar in case AnimAll clears flags
            hud->sign_j->flags |= JOBJ_HIDDEN;
            hud->bar_j->flags |= JOBJ_HIDDEN;
            // Also re-hide bar children
            JOBJ *bar_child = hud->bar_j->child;
            while (bar_child)
            {
                bar_child->flags |= JOBJ_HIDDEN;
                bar_child = bar_child->sibling;
            }

            hud->prev_score = score;
        }

        score_idx++;
    }
}

static void ScoreHUD_Destroy(void)
{
    for (int i = 0; i < num_score_huds; i++)
    {
        if (score_huds[i].gauge_gobj)
            GObj_Destroy(score_huds[i].gauge_gobj);
        if (score_huds[i].label_gobj)
            GObj_Destroy(score_huds[i].label_gobj);
    }
    num_score_huds = 0;

    if (hud_bg_gobj)
    {
        GObj_Destroy(hud_bg_gobj);
        hud_bg_gobj = NULL;
    }
    if (hud_camera_gobj)
    {
        GObj_Destroy(hud_camera_gobj);
        hud_camera_gobj = NULL;
    }
}

// Track all spawned base positions for cross-pass spacing enforcement.
static Vec3 all_spawned[GOURMET_MAX_FOOD];
static int total_spawned;

// Collect spline midpoints within the city radius.
static int CollectCandidates(Vec3 *out_candidates, int max_out)
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
        {
            out_candidates[num] = pt;
            num++;
        }
    }
    return num;
}

// Fisher-Yates shuffle.
static void ShuffleCandidates(Vec3 *arr, int count)
{
    for (int i = count - 1; i > 0; i--)
    {
        int j = HSD_Randi(i + 1);
        Vec3 tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

// Check if a position is too close to any already-spawned item.
static int IsTooClose(Vec3 *pos)
{
    for (int j = 0; j < total_spawned; j++)
    {
        float dx = pos->X - all_spawned[j].X;
        float dz = pos->Z - all_spawned[j].Z;
        if (dx * dx + dz * dz < GOURMET_MIN_SPACING)
            return 1;
    }
    return 0;
}

// Record a spawned position.
static void RecordSpawn(Vec3 *pos)
{
    if (total_spawned < GOURMET_MAX_FOOD)
        all_spawned[total_spawned++] = *pos;
}

// Spawn a single food item. Returns the GObj or NULL.
static GOBJ *SpawnFoodItem(ItemKind kind, Vec3 *pos, float scale, int coll_kind)
{
    ItemDesc desc;
    Item_InitDesc(&desc, kind, scale, 0,
                  pos, NULL, NULL, -1, -1,
                  0, coll_kind, -1, -1);
    GOBJ *item = Item_Create(&desc);
    if (item)
    {
        ItemData *id = item->userdata;
        id->lifetime = GOURMET_ITEM_LIFETIME;
    }
    return item;
}

// Register a spawned food into the slot tracker.
static void RegisterFood(GOBJ *gobj, Vec3 *spawn_pos, float scale, int coll_kind, int is_big)
{
    if (num_food_slots >= GOURMET_MAX_FOOD)
        return;
    FoodSlot *slot = &food_slots[num_food_slots++];
    slot->spawn_pos = *spawn_pos;
    slot->kind = ((ItemData *)gobj->userdata)->kind;
    slot->scale = scale;
    slot->coll_kind = coll_kind;
    slot->gobj = gobj;
    slot->respawn_timer = 0;
    slot->is_big = is_big;
}

// Find the nearest player to a position. Returns player index or -1.
static int FindNearestPlayer(Vec3 *pos)
{
    int best = -1;
    float best_dist = 1e18f;
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) == PKIND_NONE)
            continue;
        GOBJ *mg = Ply_GetMachineGObj(i);
        if (!mg)
            continue;
        MachineData *md = mg->userdata;
        float dx = md->pos.X - pos->X;
        float dy = md->pos.Y - pos->Y;
        float dz = md->pos.Z - pos->Z;
        float dist = dx * dx + dy * dy + dz * dz;
        if (dist < best_dist)
        {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}

// Watcher GObj proc: checks for eaten food, manages respawn timers.
static void GourmetRace_WatcherProc(GOBJ *gobj)
{
    if (!gourmet_active)
        return;

    // Walk the item GObj list once to build a set of live pointers.
    // Items are on GAMEPLINK_ITEM (13).
    static GOBJ *live_items[128];
    int live_count = 0;
    GOBJ *iter = (*stc_gobj_lookup)[GAMEPLINK_ITEM];
    while (iter && live_count < 128)
    {
        live_items[live_count++] = iter;
        iter = iter->next;
    }

    for (int i = 0; i < num_food_slots; i++)
    {
        FoodSlot *slot = &food_slots[i];

        if (slot->gobj)
        {
            // Check if the item GObj is still alive
            int alive = 0;
            for (int j = 0; j < live_count; j++)
            {
                if (live_items[j] == slot->gobj)
                {
                    alive = 1;
                    break;
                }
            }
            if (!alive)
            {
                // Attribute to nearest player
                int ply = FindNearestPlayer(&slot->spawn_pos);
                if (ply >= 0)
                {
                    int pts = slot->is_big ? GOURMET_BIG_POINTS : GOURMET_REGULAR_POINTS;
                    scores[ply] += pts;
                    OSReport("[GourmetRace] P%d +%d pts (total: %d)\n", ply, pts, scores[ply]);
                }

                slot->gobj = NULL;
                slot->respawn_timer = slot->is_big
                    ? GOURMET_RESPAWN_TIME_BIG
                    : GOURMET_RESPAWN_TIME;
            }
        }
        else if (slot->respawn_timer > 0)
        {
            slot->respawn_timer--;
            if (slot->respawn_timer == 0)
            {
                ItemKind kind = RandomFoodKind();
                GOBJ *item = SpawnFoodItem(kind, &slot->spawn_pos,
                                           slot->scale, slot->coll_kind);
                if (item)
                {
                    slot->gobj = item;
                    slot->kind = kind;
                }
                else
                {
                    // Item cap hit, retry next frame
                    slot->respawn_timer = 1;
                }
            }
        }
    }

    ScoreHUD_Update();
}

// Main spawning logic — four passes per spec.
static void GourmetRace_SpawnFood(void)
{
    if (!Gm_IsInCity())
    {
        OSReport("[GourmetRace] Not in City Trial\n");
        return;
    }

    total_spawned = 0;
    num_food_slots = 0;

    // --- Pass 1: Big foods at pre-placed locations ---
    int spawned_big = 0;
    for (int i = 0; i < GOURMET_BIG_COUNT; i++)
    {
        Vec3 pos = {
            .X = big_food_positions[i].X,
            .Y = big_food_positions[i].Y + 1.0f,
            .Z = big_food_positions[i].Z
        };
        GOBJ *item = SpawnFoodItem(RandomFoodKind(), &pos, GOURMET_BIG_ITEM_SCALE, 2);
        if (item)
        {
            RecordSpawn(&big_food_positions[i]);
            RegisterFood(item, &pos, GOURMET_BIG_ITEM_SCALE, 2, 1);
            spawned_big++;
        }
    }
    OSReport("[GourmetRace] Pass 1 (big): %d/%d\n", spawned_big, GOURMET_BIG_COUNT);

    // --- Pass 2: Regular foods at 5-10 of 15 pre-placed locations ---
    int preplaced_target = GOURMET_PREPLACED_MIN
        + HSD_Randi(GOURMET_PREPLACED_MAX - GOURMET_PREPLACED_MIN + 1);

    // Shuffle indices to pick random subset
    int indices[GOURMET_PREPLACED_COUNT];
    for (int i = 0; i < GOURMET_PREPLACED_COUNT; i++)
        indices[i] = i;
    for (int i = GOURMET_PREPLACED_COUNT - 1; i > 0; i--)
    {
        int j = HSD_Randi(i + 1);
        int tmp = indices[i];
        indices[i] = indices[j];
        indices[j] = tmp;
    }

    int spawned_pre = 0;
    for (int i = 0; i < GOURMET_PREPLACED_COUNT && spawned_pre < preplaced_target; i++)
    {
        int idx = indices[i];
        Vec3 pos = {
            .X = preplaced_positions[idx].X,
            .Y = preplaced_positions[idx].Y + 1.0f,
            .Z = preplaced_positions[idx].Z
        };
        GOBJ *item = SpawnFoodItem(RandomFoodKind(), &pos, GOURMET_ITEM_SCALE, 2);
        if (item)
        {
            RecordSpawn(&preplaced_positions[idx]);
            RegisterFood(item, &pos, GOURMET_ITEM_SCALE, 2, 0);
            spawned_pre++;
        }
    }
    OSReport("[GourmetRace] Pass 2 (preplaced): %d/%d\n", spawned_pre, preplaced_target);

    // Remaining budget for random passes
    int remaining = GOURMET_MAX_FOOD - total_spawned;
    if (remaining <= 0)
    {
        OSReport("[GourmetRace] Done! %d total\n", total_spawned);
        return;
    }

    // Collect spline candidates for random passes
    static Vec3 candidates[MAX_CANDIDATES];
    int num_candidates = CollectCandidates(candidates, MAX_CANDIDATES);
    OSReport("[GourmetRace] Collected %d candidate points\n", num_candidates);

    // --- Pass 3: Half of remaining, high up + ground snap ---
    int pass3_target = remaining / 2;
    ShuffleCandidates(candidates, num_candidates);

    int spawned_s = 0;
    for (int i = 0; i < num_candidates && spawned_s < pass3_target; i++)
    {
        Vec3 base = candidates[i];
        if (IsTooClose(&base))
            continue;

        Vec3 pos = {
            .X = base.X,
            .Y = base.Y + GOURMET_SURFACE_HEIGHT,
            .Z = base.Z
        };
        GOBJ *item = SpawnFoodItem(RandomFoodKind(), &pos, GOURMET_ITEM_SCALE, 3);
        if (item)
        {
            RecordSpawn(&base);
            RegisterFood(item, &pos, GOURMET_ITEM_SCALE, 3, 0);
            spawned_s++;
        }
    }
    OSReport("[GourmetRace] Pass 3 (surface): %d/%d\n", spawned_s, pass3_target);

    // --- Pass 4: Other half, underground (Y < 44) + ground snap ---
    int pass4_target = remaining - spawned_s;
    ShuffleCandidates(candidates, num_candidates);

    int spawned_u = 0;
    for (int i = 0; i < num_candidates && spawned_u < pass4_target; i++)
    {
        Vec3 base = candidates[i];
        if (base.Y >= 44.0f)
            continue;
        if (IsTooClose(&base))
            continue;

        Vec3 pos = {
            .X = base.X,
            .Y = base.Y + GOURMET_ABOVE_SPLINE_HEIGHT,
            .Z = base.Z
        };
        GOBJ *item = SpawnFoodItem(RandomFoodKind(), &pos, GOURMET_ITEM_SCALE, 2);
        if (item)
        {
            RecordSpawn(&base);
            RegisterFood(item, &pos, GOURMET_ITEM_SCALE, 2, 0);
            spawned_u++;
        }
    }
    OSReport("[GourmetRace] Pass 4 (underground): %d/%d\n", spawned_u, pass4_target);

    // --- Pass 5 (overflow): if pass 4 fell short, fill remainder as surface ---
    int spawned_overflow = 0;
    if (spawned_u < pass4_target)
    {
        int pass5_target = pass4_target - spawned_u;
        ShuffleCandidates(candidates, num_candidates);

        for (int i = 0; i < num_candidates && spawned_overflow < pass5_target; i++)
        {
            Vec3 base = candidates[i];
            if (IsTooClose(&base))
                continue;

            Vec3 pos = {
                .X = base.X,
                .Y = base.Y + GOURMET_SURFACE_HEIGHT,
                .Z = base.Z
            };
            GOBJ *item = SpawnFoodItem(RandomFoodKind(), &pos, GOURMET_ITEM_SCALE, 3);
            if (item)
            {
                RecordSpawn(&base);
                RegisterFood(item, &pos, GOURMET_ITEM_SCALE, 3, 0);
                spawned_overflow++;
            }
        }
        OSReport("[GourmetRace] Pass 5 (overflow surface): %d/%d\n", spawned_overflow, pass5_target);
    }

    OSReport("[GourmetRace] Done! %d big + %d preplaced + %d surface + %d underground + %d overflow = %d total\n",
             spawned_big, spawned_pre, spawned_s, spawned_u, spawned_overflow, total_spawned);
}

// === Custom Event Callbacks ===

void GourmetRace_Start(EventCheckData *ev_chk)
{
    gourmet_active = 1;
    for (int i = 0; i < 5; i++)
        scores[i] = 0;
    GourmetRace_SpawnFood();

    // Create watcher GObj to manage respawns
    watcher_gobj = GObj_Create(0, GAMEPLINK_SYS, 0);
    GObj_AddProc(watcher_gobj, GourmetRace_WatcherProc, 0);
    OSReport("[GourmetRace] Watcher GObj created, tracking %d food slots\n", num_food_slots);

    ScoreHUD_Create();
}

void GourmetRace_Active(EventCheckData *ev_chk)
{
    // Respawn and HUD update handled by watcher GObj proc
}

void GourmetRace_End2(EventCheckData *ev_chk)
{
    gourmet_active = 0;

    // Destroy all remaining food items
    for (int i = 0; i < num_food_slots; i++)
    {
        if (food_slots[i].gobj)
        {
            GObj_Destroy(food_slots[i].gobj);
            food_slots[i].gobj = NULL;
        }
    }
    num_food_slots = 0;

    if (watcher_gobj)
    {
        GObj_Destroy(watcher_gobj);
        watcher_gobj = NULL;
    }

    ScoreHUD_Destroy();

    // Determine winner(s) and give rewards
    int best_score = 0;
    for (int i = 0; i < 5; i++)
    {
        OSReport("[GourmetRace] P%d score: %d\n", i, scores[i]);
        if (scores[i] > best_score)
            best_score = scores[i];
    }

    if (best_score == 0)
    {
        OSReport("[GourmetRace] No food eaten, no winner\n");
        return;
    }

    // Count how many players tied for first
    int winner_count = 0;
    for (int i = 0; i < 5; i++)
    {
        if (scores[i] == best_score)
            winner_count++;
    }

    int is_tie = winner_count > 1;
    int allups = is_tie ? 1 : 2;

    for (int i = 0; i < 5; i++)
    {
        if (scores[i] != best_score)
            continue;
        for (int j = 0; j < allups; j++)
            SpawnItemPlayer(i, ITKIND_ALLUP);
        OSReport("[GourmetRace] P%d wins! (%d all-ups)\n", i, allups);
    }
}
