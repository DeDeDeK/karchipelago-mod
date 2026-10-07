#include "os.h"
#include "game.h"
#include "hsd.h"
#include "scene.h"
#include "rider.h"
#include "obj.h"
#include "effect.h"
#include "inline.h"
#include "hoshi/mod.h"

#include "hypernova.h"
#include "hypernova_api.h"

// Frames @ 60fps: Short / Medium / Long.
const int hypernova_duration_table[HYPERNOVA_DURATION_NUM] = { 300, 600, 1200 };

int hypernova_enabled       = 1;
int hypernova_duration_sel  = 1;   // Medium
int hypernova_suck_yaku     = 1;
int hypernova_suck_machines = 1;
int hypernova_selftest      = 0;
int hypernova_debug_cone    = 0;

static u8  stc_active[PLY_NUM];
static int stc_timer[PLY_NUM];

enum { HYPERNOVA_PHASE_IDLE, HYPERNOVA_PHASE_GULP, HYPERNOVA_PHASE_LOOP };
static u8 stc_inhale_phase[PLY_NUM];

// Init to neutral so an inactive player's model_scale is never written.
static float stc_scale_current[PLY_NUM] = { HYPERNOVA_SCALE_NEUTRAL, HYPERNOVA_SCALE_NEUTRAL,
                                      HYPERNOVA_SCALE_NEUTRAL, HYPERNOVA_SCALE_NEUTRAL,
                                      HYPERNOVA_SCALE_NEUTRAL };
static float stc_scale_start[PLY_NUM]   = { HYPERNOVA_SCALE_NEUTRAL, HYPERNOVA_SCALE_NEUTRAL,
                                      HYPERNOVA_SCALE_NEUTRAL, HYPERNOVA_SCALE_NEUTRAL,
                                      HYPERNOVA_SCALE_NEUTRAL };
static int   stc_scale_anim[PLY_NUM]    = { HYPERNOVA_SCALE_ANIM_FRAMES, HYPERNOVA_SCALE_ANIM_FRAMES,
                                      HYPERNOVA_SCALE_ANIM_FRAMES, HYPERNOVA_SCALE_ANIM_FRAMES,
                                      HYPERNOVA_SCALE_ANIM_FRAMES };

// Rainbow hue phase (0..1), shared by all riders.
static float stc_hue = 0.0f;

static void StopRainbowPlayer(int player);

// City Trial gameplay, round underway (riders exist, intro/countdown done). Stadium events run
// as MJRKIND_CITY + MNRKIND_3D too, so they need excluding by city_kind.
static int InCityTrialGameplay(void)
{
    if (Scene_GetCurrentMajor() != MJRKIND_CITY)
        return 0;
    if (Scene_GetCurrentMinor() != MNRKIND_3D)
        return 0;
    if (CityTrial_IsInStadium())
        return 0;
    if (Gm_GetIntroState() != GMINTRO_END)
        return 0;
    return 1;
}

static void RetargetScale(int p)
{
    stc_scale_start[p] = stc_scale_current[p];
    stc_scale_anim[p]  = 0;
}

static float TickScale(int p)
{
    float target = stc_active[p] ? HYPERNOVA_SCALE_TARGET : HYPERNOVA_SCALE_NEUTRAL;

    if (stc_scale_anim[p] < HYPERNOVA_SCALE_ANIM_FRAMES)
    {
        stc_scale_anim[p]++;
        float t = (float)stc_scale_anim[p] / (float)HYPERNOVA_SCALE_ANIM_FRAMES;
        t = smoothstep(t);
        stc_scale_current[p] = lerp(stc_scale_start[p], target, t);
    }
    else
    {
        stc_scale_current[p] = target;
    }
    return stc_scale_current[p];
}

// Stop the power-up for one player; their scale eases back down. Returns 1 if they were active,
// so callers can report a count rather than a line per player.
static int StopPlayer(int p)
{
    if (!stc_active[p])
        return 0;

    // End a running suck here: the frame proc stops visiting this player the moment it goes
    // inactive, so DriveInhale's release branch would never run again.
    if (stc_inhale_phase[p] == HYPERNOVA_PHASE_LOOP)
    {
        GOBJ *rg = Ply_GetRiderGObj(p);
        if (rg != NULL && ((RiderData *)rg->userdata)->status == RDSTATE_DRAW)
            Rider_EndInhale((RiderData *)rg->userdata);
    }

    stc_active[p]        = 0;
    stc_timer[p]         = 0;
    stc_inhale_phase[p]  = HYPERNOVA_PHASE_IDLE;
    Hypernova_VacuumFinishClaimedPlayer(p);
    RetargetScale(p);
    StopRainbowPlayer(p);
    return 1;
}

// Scene change: models are recreated at scale 1.0, so snap to neutral with no ease.
static void ResetState(void)
{
    for (int i = 0; i < PLY_NUM; i++)
    {
        stc_active[i]        = 0;
        stc_timer[i]         = 0;
        stc_scale_current[i] = HYPERNOVA_SCALE_NEUTRAL;
        stc_scale_start[i]   = HYPERNOVA_SCALE_NEUTRAL;
        stc_scale_anim[i]    = HYPERNOVA_SCALE_ANIM_FRAMES;
        stc_inhale_phase[i]  = HYPERNOVA_PHASE_IDLE;
    }
    stc_hue = 0.0f;
    Hypernova_VacuumReset();
}

// Hypernova is mutually exclusive with copy abilities and City Trial power-ups.
static int PlayerHoldsAbility(RiderData *rd)
{
    return rd->copy_kind != COPYKIND_NONE || rd->powerup_kind != POWERUPKIND_NONE;
}

// 0 means "use the menu setting".
static int ResolveDuration(int duration_frames)
{
    if (duration_frames > 0)
        return duration_frames;
    return hypernova_duration_table[hypernova_duration_sel];
}

// 0 rejected, 1 newly started, 2 refreshed an already-active player.
static int StartPlayer(int player, int duration_frames)
{
    if (!hypernova_enabled)
        return 0;
    if (!InCityTrialGameplay())
        return 0;
    if (player < 0 || player >= PLY_NUM)
        return 0;
    if (Ply_GetPKind(player) != PKIND_HMN)
        return 0;

    stc_timer[player] = ResolveDuration(duration_frames);
    if (stc_active[player])
        return 2;

    // Strip any held ability/power-up, or the frame proc's has-ability guard cancels Hypernova
    // on its first frame.
    GOBJ *rg = Ply_GetRiderGObj(player);
    if (rg != NULL)
    {
        RiderData *rd = rg->userdata;
        if (PlayerHoldsAbility(rd))
        {
            Rider_AbilityRemoveModel(rd);     // clears copy_kind/powerup_kind
            RiderState_LoseAbilityEnter(rd); // spit-out -> neutral
        }
    }
    stc_active[player] = 1;
    RetargetScale(player);
    return 1;
}

int Hypernova_ActivatePlayer(int player, int duration_frames)
{
    int r = StartPlayer(player, duration_frames);
    if (r == 1)
        OSReport("[Hypernova] Player %d activated for %d frames\n", player + 1, stc_timer[player]);
    else if (r == 2)
        OSReport("[Hypernova] Player %d extended to %d frames\n", player + 1, stc_timer[player]);
    return r != 0;
}

int Hypernova_Activate(int duration_frames)
{
    int frames = ResolveDuration(duration_frames);
    int n = 0;
    for (int i = 0; i < PLY_NUM; i++)
        if (StartPlayer(i, frames) != 0)
            n++;
    if (n > 0)
        OSReport("[Hypernova] Activated %d player(s) for %d frames\n", n, frames);
    return n > 0;
}

void Hypernova_Deactivate(void)
{
    int n = 0;
    for (int i = 0; i < PLY_NUM; i++)
        n += StopPlayer(i);
    if (n > 0)
        OSReport("[Hypernova] Deactivated %d player(s)\n", n);
}

int Hypernova_IsActive(void)
{
    for (int i = 0; i < PLY_NUM; i++)
        if (stc_active[i])
            return 1;
    return 0;
}

int Hypernova_FramesRemaining(void)
{
    int most = 0;
    for (int i = 0; i < PLY_NUM; i++)
        if (stc_active[i] && stc_timer[i] > most)
            most = stc_timer[i];
    return most;
}

static const HypernovaAPI stc_api = {
    .Activate        = Hypernova_Activate,
    .ActivatePlayer  = Hypernova_ActivatePlayer,
    .Deactivate      = Hypernova_Deactivate,
    .IsActive        = Hypernova_IsActive,
    .FramesRemaining = Hypernova_FramesRemaining,
};

static void SelfTestPoll(void)
{
    if (!hypernova_selftest)
        return;
    if (stc_engine_pads[0].down & PAD_BUTTON_DPAD_UP)
        Hypernova_Activate(0);
}

// Owns the IDLE -> GULP -> LOOP gesture. Returns 1 while a suck is being driven.
static int DriveInhale(RiderData *rd, int player, int held)
{
    int st = rd->status;

    if (!held)
    {
        // End an active suck through the engine's own END; a mere tap's gulp finishes itself.
        if (stc_inhale_phase[player] == HYPERNOVA_PHASE_LOOP && st == RDSTATE_DRAW)
            Rider_EndInhale(rd);
        stc_inhale_phase[player] = HYPERNOVA_PHASE_IDLE;
        return 0;
    }

    switch (stc_inhale_phase[player])
    {
        case HYPERNOVA_PHASE_IDLE:
            Rider_StartInhale(rd);                            // gulp + whirlwind + SFX
            stc_inhale_phase[player] = HYPERNOVA_PHASE_GULP;
            break;

        case HYPERNOVA_PHASE_GULP:
            // Promote the same frame the gulp leaves START, so the open mouth doesn't flicker
            // back to neutral.
            if (st != RDSTATE_DRAWSTART)
            {
                Rider_StartInhaleLoop(rd);
                stc_inhale_phase[player] = HYPERNOVA_PHASE_LOOP;
            }
            break;

        case HYPERNOVA_PHASE_LOOP:
            if (st == RDSTATE_DRAW)
            {
                rd->inhale_timer = HYPERNOVA_INHALE_TIMER_HOLD;
            }
            else if (st != RDSTATE_DRAWEND && st != RDSTATE_DRAWSTART)
            {
                // Engine dropped us out of the loop while still held: re-enter without a gulp.
                Rider_StartInhaleLoop(rd);
            }
            break;
    }

    return stc_inhale_phase[player] != HYPERNOVA_PHASE_IDLE;
}

// Hue (0..1) -> full-saturation/value RGB (0..255).
static GXColor HueToRgb(float h, u8 a)
{
    float hh = h * 6.0f;
    int   seg = (int)hh;
    float f = hh - (float)seg;
    u8 up   = (u8)(255.0f * f);
    u8 down = (u8)(255.0f * (1.0f - f));
    switch (seg % 6)
    {
        case 0:  return (GXColor){255,  up,   0,    a};
        case 1:  return (GXColor){down, 255,  0,    a};
        case 2:  return (GXColor){0,    255,  up,   a};
        case 3:  return (GXColor){0,    down, 255,  a};
        case 4:  return (GXColor){up,   0,    255,  a};
        default: return (GXColor){255,  0,    down, a};
    }
}

// keep=1 -> full color, keep=0 -> white.
static GXColor TowardWhite(GXColor c, float keep)
{
    c.r = (u8)(255.0f - (255.0f - (float)c.r) * keep);
    c.g = (u8)(255.0f - (255.0f - (float)c.g) * keep);
    c.b = (u8)(255.0f - (255.0f - (float)c.b) * keep);
    return c;
}

// Drive a hue into the rider's body ColAnim overlay. With the anim tick frozen the mod owns
// every field each frame, so it writes both the slot and the state ColAnim_Resolve would.
static void DriveRainbow(RiderData *rd, float hue)
{
    ColAnimSlot *slot = &rd->col_anim.slot[0];

    // Floor priority before re-taking the slot, or the priority-gated ColAnim_Apply rejects it.
    if (slot->anim_index != HYPERNOVA_RAINBOW_COLANIM)
    {
        slot->pri = 0;
        Rider_ApplyColAnim(rd, HYPERNOVA_RAINBOW_COLANIM, 0);
    }

    // Freeze the anim tick, or it re-stamps its own color over the hue.
    slot->cmd_data = NULL;

    // Pin max priority so a pickup flash can't win the resolver or the apply-gate.
    slot->pri = COLANIM_PRI_MAX;

    // Hold the tint live; with the tick frozen nothing else sets it, and the resolver
    // would stop drawing the overlay.
    slot->flags |= COLANIM_FLAG_TINT;

    GXColor col = HueToRgb(hue, HYPERNOVA_RAINBOW_ALPHA);

    slot->color      = col;
    slot->color_f[0] = (float)col.r;
    slot->color_f[1] = (float)col.g;
    slot->color_f[2] = (float)col.b;
    slot->color_f[3] = (float)col.a;

    rd->col_anim.color  = col;
    rd->col_anim.flags |= COLANIM_FLAG_TINT;
    rd->col_anim.ratio  = COLANIM_RATIO_NONE;
}

// Clearing the slot restores normal intangibility/invincibility flashes.
static void StopRainbowPlayer(int player)
{
    GOBJ *rg = Ply_GetRiderGObj(player);
    if (!rg)
        return;
    RiderData *rd = rg->userdata;
    ColAnim_Reset(&rd->col_anim.slot[0]);
}

static void TintTevColor(GXColor *c, GXColor rgb)
{
    *c = (GXColor){rgb.r, rgb.g, rgb.b, c->a};
}

// Rewrites each TObj's tev constant/tev0/tev1 RGB - value fields MObjSetupTev re-reads every
// frame. The TExp node tree itself is never touched (clobbering it crashes the walk).
static void RecolorEffectTree(JOBJ *j, GXColor rgb)
{
    while (j != NULL)
    {
        for (DOBJ *d = j->dobj; d != NULL; d = d->next)
        {
            MOBJ *m = d->mobj;
            if (m == NULL)
                continue;
            for (TOBJ *t = m->tobj; t != NULL; t = t->next)
            {
                if (t->tev == NULL)
                    continue;
                TintTevColor(&t->tev->constant, rgb);
                TintTevColor(&t->tev->tev0, rgb);
                TintTevColor(&t->tev->tev1, rgb);
            }
        }
        if (j->child != NULL)
            RecolorEffectTree(j->child, rgb);
        j = j->sibling;
    }
}

// The spawn discards the handle, so live whirlwinds are found by walking the model-effect
// bucket and matching the Effect kind.
static void RecolorWhirlwinds(GXColor rgb)
{
    for (GOBJ *g_eff = (*stc_gobj_lookup)[GAMEPLINK_EFFECTMODEL]; g_eff != NULL; g_eff = g_eff->next)
    {
        if (g_eff->entity_class != GAMEENTITY_EFFECT)
            continue;
        Effect *eff = g_eff->userdata;
        if (eff == NULL)
            continue;
        if (eff->kind != HYPERNOVA_INHALE_EFFECT_ID)
            continue;
        RecolorEffectTree((JOBJ *)g_eff->hsd_object, rgb);
    }
}

void Hypernova_OnBoot(void)
{
    Hoshi_ExportMod((void *)&stc_api);
    OSReport("[Hypernova] Initialized (%s, City Trial)\n",
             hypernova_enabled ? "enabled" : "disabled");
}

void Hypernova_OnSceneChange(void)
{
    ResetState();
}

static void Hypernova_Think(GOBJ *g)
{
    if (!InCityTrialGameplay())
        return;

    SelfTestPoll();

    // End on expiry, or the instant a player gains an ability/power-up - before DriveInhale, so
    // the drive doesn't fight the state the engine already moved the rider into this frame.
    int expired = 0, cancelled = 0;
    for (int i = 0; i < PLY_NUM; i++)
    {
        if (!stc_active[i])
            continue;
        if (--stc_timer[i] <= 0)
        {
            expired += StopPlayer(i);
            continue;
        }
        GOBJ *rg = Ply_GetRiderGObj(i);
        if (rg != NULL && PlayerHoldsAbility((RiderData *)rg->userdata))
            cancelled += StopPlayer(i);
    }
    if (expired > 0)
        OSReport("[Hypernova] Expired for %d player(s)\n", expired);
    if (cancelled > 0)
        OSReport("[Hypernova] Cancelled for %d player(s) who gained an ability\n", cancelled);

    int any_active = 0;
    for (int i = 0; i < PLY_NUM; i++)
        any_active |= stc_active[i];

    if (any_active)
    {
        stc_hue += 1.0f / (float)HYPERNOVA_RAINBOW_PERIOD;
        if (stc_hue >= 1.0f)
            stc_hue -= 1.0f;
    }

    for (int i = 0; i < PLY_NUM; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        GOBJ *rg = Ply_GetRiderGObj(i);
        if (!rg)
            continue;
        RiderData *rd = rg->userdata;

        // Leave a settled, inactive player's model_scale alone.
        int scaling = stc_active[i]
            || stc_scale_current[i] != HYPERNOVA_SCALE_NEUTRAL
            || stc_scale_anim[i] < HYPERNOVA_SCALE_ANIM_FRAMES;
        if (!scaling)
            continue;

        rd->model_scale = TickScale(i);

        if (!stc_active[i])
            continue;

        DriveRainbow(rd, stc_hue);

        // The inhale action-state procs deref the rider's machine, so being off-machine reads
        // as a release: it ends any running suck and skips the vacuum.
        int held = Rider_IsOnMachine(rd) && (rd->input.held & HYPERNOVA_TRIGGER_BUTTON) != 0;
        if (DriveInhale(rd, i, held))
            Hypernova_VacuumPlayer(i, rd);
    }

    if (any_active)
    {
        // Claimed targets keep flying in even once the cone no longer covers them.
        Hypernova_VacuumProcessClaimedItems();
        Hypernova_VacuumProcessClaimed();
        Hypernova_VacuumProcessClaimedMachines();

        float whue = stc_hue + HYPERNOVA_WHIRLWIND_HUE_OFFSET;
        if (whue >= 1.0f)
            whue -= 1.0f;
        RecolorWhirlwinds(TowardWhite(HueToRgb(whue, 255), HYPERNOVA_WHIRLWIND_TINT));
    }
}

// The last priority, so the vacuum's position overrides win over item physics (4), the
// ground snap (5) and the model-matrix appliers (6). p_link 1 freezes with the match pause
// and the hitstops, as do the procs this cooperates with (model_scale, ColAnim selector,
// effect models).
void Hypernova_On3DLoadEnd(void)
{
    if (!Gm_IsInCity())
        return;

    if (hypernova_enabled)
    {
        GOBJ *g = GObj_Create(0, GAMEPLINK_1, 0);
        GObj_AddProc(g, Hypernova_Think, 23);
    }
    // Drawn from compile-time constants, so the cone needs no power-up.
    if (hypernova_debug_cone && !CityTrial_IsInStadium())
        Hypernova_DebugConeCreate();
}
