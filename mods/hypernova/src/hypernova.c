#include "os.h"
#include "game.h"
#include "hsd.h"
#include "scene.h"
#include "rider.h"
#include "obj.h"
#include "effect.h"
#include "hoshi/mod.h"

#include "hypernova.h"
#include "hypernova_api.h"

// Frames @ 60fps: Short / Medium / Long.
const int hypernova_duration_table[HYPERNOVA_DURATION_NUM] = { 300, 600, 1200 };

// Menu-backed settings
int hypernova_enabled       = 1;
int hypernova_duration_sel  = 1;   // Medium
int hypernova_suck_yaku     = 1;
int hypernova_suck_machines = 1;
int hypernova_selftest      = 0;
int hypernova_debug_cone    = 0;

// Per-player power-up state, indexed by player slot (0..4).
static u8  stc_active[5];
static int stc_timer[5];

enum { HYPERNOVA_PHASE_IDLE, HYPERNOVA_PHASE_GULP, HYPERNOVA_PHASE_LOOP };
static u8 stc_inhale_phase[5];

// Init to neutral so an inactive player's model_scale is never written.
static float stc_scale_current[5] = { HYPERNOVA_SCALE_NEUTRAL, HYPERNOVA_SCALE_NEUTRAL,
                                      HYPERNOVA_SCALE_NEUTRAL, HYPERNOVA_SCALE_NEUTRAL,
                                      HYPERNOVA_SCALE_NEUTRAL };
static float stc_scale_start[5]   = { HYPERNOVA_SCALE_NEUTRAL, HYPERNOVA_SCALE_NEUTRAL,
                                      HYPERNOVA_SCALE_NEUTRAL, HYPERNOVA_SCALE_NEUTRAL,
                                      HYPERNOVA_SCALE_NEUTRAL };
static int   stc_scale_anim[5]    = { HYPERNOVA_SCALE_ANIM_FRAMES, HYPERNOVA_SCALE_ANIM_FRAMES,
                                      HYPERNOVA_SCALE_ANIM_FRAMES, HYPERNOVA_SCALE_ANIM_FRAMES,
                                      HYPERNOVA_SCALE_ANIM_FRAMES };

// Rainbow hue phase (0..1), shared by all riders.
static float stc_hue = 0.0f;

static void StopRainbowPlayer(int player);

// City Trial gameplay, round underway (riders exist, intro/countdown done).
static int InCityTrialGameplay(void)
{
    if (Scene_GetCurrentMajor() != MJRKIND_CITY)
        return 0;
    if (Scene_GetCurrentMinor() != MNRKIND_3D)
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
        t = t * t * (3.0f - 2.0f * t); // smoothstep
        stc_scale_current[p] = stc_scale_start[p] + (target - stc_scale_start[p]) * t;
    }
    else
    {
        stc_scale_current[p] = target;
    }
    return stc_scale_current[p];
}

// Stop the power-up for one player; their scale eases back down.
static void StopPlayer(int p)
{
    if (!stc_active[p])
        return;
    stc_active[p]        = 0;
    stc_timer[p]         = 0;
    stc_inhale_phase[p]  = HYPERNOVA_PHASE_IDLE;
    Hypernova_VacuumFinishClaimedPlayer(p);
    RetargetScale(p);
    StopRainbowPlayer(p);
    OSReport("[Hypernova] Player %d deactivated\n", p + 1);
}

// Scene change: models are recreated at scale 1.0, so snap to neutral with no ease.
static void ResetState(void)
{
    for (int i = 0; i < 5; i++)
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
    Hypernova_DebugConeReset();
}

// Hypernova is mutually exclusive with copy abilities and City Trial power-ups.
static int PlayerHoldsAbility(RiderData *rd)
{
    return rd->copy_kind != COPYKIND_NONE || rd->powerup_kind != POWERUPKIND_NONE;
}

int Hypernova_ActivatePlayer(int player, int duration_frames)
{
    if (!hypernova_enabled)
        return 0;
    if (!InCityTrialGameplay())
        return 0;
    if (player < 0 || player >= 5)
        return 0;
    if (Ply_GetPKind(player) != PKIND_HMN)
        return 0;

    if (duration_frames <= 0)
    {
        int sel = hypernova_duration_sel;
        if (sel < 0 || sel >= HYPERNOVA_DURATION_NUM)
            sel = 0;
        duration_frames = hypernova_duration_table[sel];
    }

    stc_timer[player] = duration_frames;
    if (!stc_active[player])
    {
        // Strip any held ability/power-up, or OnFrameEnd's has-ability guard cancels Hypernova
        // on its first frame.
        GOBJ *rg = Ply_GetRiderGObj(player);
        if (rg != NULL)
        {
            RiderData *rd = rg->userdata;
            if (PlayerHoldsAbility(rd))
            {
                Rider_AbilityRemoveModel(rd);     // clears copy_kind/powerup_kind
                Rider_LoseAbilityState_Enter(rd); // spit-out -> neutral
            }
        }
        stc_active[player] = 1;
        RetargetScale(player);
        OSReport("[Hypernova] Player %d activated for %d frames\n", player + 1, duration_frames);
    }
    else
    {
        OSReport("[Hypernova] Player %d extended to %d frames\n", player + 1, duration_frames);
    }
    return 1;
}

int Hypernova_Activate(int duration_frames)
{
    int any = 0;
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        any |= Hypernova_ActivatePlayer(i, duration_frames);
    }
    return any;
}

void Hypernova_Deactivate(void)
{
    for (int i = 0; i < 5; i++)
        StopPlayer(i);
}

int Hypernova_IsActive(void)
{
    for (int i = 0; i < 5; i++)
        if (stc_active[i])
            return 1;
    return 0;
}

int Hypernova_FramesRemaining(void)
{
    int most = 0;
    for (int i = 0; i < 5; i++)
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

// Owns the IDLE -> GULP -> LOOP gesture across the three vanilla inhale action-states, which do
// not chain on their own. Returns 1 while a suck is being driven.
static int DriveInhale(RiderData *rd, int player, int held)
{
    int st = rd->state_idx;

    if (!held)
    {
        // End an active suck through the engine's own END; a mere tap's gulp finishes itself.
        if (stc_inhale_phase[player] == HYPERNOVA_PHASE_LOOP && st == HYPERNOVA_INHALE_LOOP)
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
            if (st != HYPERNOVA_INHALE_START)
            {
                Rider_StartInhaleLoop(rd);
                stc_inhale_phase[player] = HYPERNOVA_PHASE_LOOP;
            }
            break;

        case HYPERNOVA_PHASE_LOOP:
            if (st == HYPERNOVA_INHALE_LOOP)
            {
                // Keep the suck from timing out while held.
                rd->inhale_timer = HYPERNOVA_INHALE_TIMER_HOLD;
            }
            else if (st != HYPERNOVA_INHALE_END && st != HYPERNOVA_INHALE_START)
            {
                // Engine dropped us out of the loop while still held: re-enter without a gulp.
                Rider_StartInhaleLoop(rd);
            }
            break;
    }

    return stc_inhale_phase[player] != HYPERNOVA_PHASE_IDLE;
}

// Hue (0..1) -> full-saturation/value RGB (0..255).
static void HueToRgb(float h, u8 *r, u8 *g, u8 *b)
{
    float hh = h * 6.0f;
    int   seg = (int)hh;
    float f = hh - (float)seg;
    u8 up   = (u8)(255.0f * f);
    u8 down = (u8)(255.0f * (1.0f - f));
    switch (seg % 6)
    {
        case 0:  *r = 255;  *g = up;   *b = 0;    break;
        case 1:  *r = down; *g = 255;  *b = 0;    break;
        case 2:  *r = 0;    *g = 255;  *b = up;   break;
        case 3:  *r = 0;    *g = down; *b = 255;  break;
        case 4:  *r = up;   *g = 0;    *b = 255;  break;
        default: *r = 255;  *g = 0;    *b = down; break;
    }
}

// keep=1 -> full color, keep=0 -> white.
static u8 TowardWhite(u8 v, float keep)
{
    return (u8)(255.0f - (255.0f - (float)v) * keep);
}

// Drive a hue into the rider's body ColAnim overlay. With the candy tick frozen the mod owns
// every field each frame.
static void DriveRainbow(RiderData *rd, float hue)
{
    char *slot = (char *)rd + HYPERNOVA_COLANIM_BODY_OFF;
    int  *st   = (int *)slot;

    // Floor priority before re-taking the slot, or the priority-gated ColAnim_Apply rejects it.
    if (st[HYPERNOVA_COLANIM_INDEX_W] != HYPERNOVA_OVERLAY_COLANIM)
    {
        slot[HYPERNOVA_COLANIM_PRI_OFF] = 0;
        Rider_ApplyColAnim(rd, HYPERNOVA_OVERLAY_COLANIM, 0);
    }

    // Freeze the candy animation tick, or it re-stamps its green over the hue.
    *(u32 *)(slot + HYPERNOVA_COLANIM_DATA_OFF) = 0;

    // Pin max priority so a pickup flash can't win the selector or the apply-gate.
    slot[HYPERNOVA_COLANIM_PRI_OFF] = (char)HYPERNOVA_COLANIM_PRI_MAX;

    // Hold color-override active; with the tick frozen nothing else sets it, and the selector
    // would stop drawing the overlay.
    slot[HYPERNOVA_COLANIM_STFLAG_OFF] |= 0x80;

    u8 r, g, b;
    HueToRgb(hue, &r, &g, &b);
    u32 packed = ((u32)r << 24) | ((u32)g << 16) | ((u32)b << 8) | (u32)HYPERNOVA_RAINBOW_ALPHA;

    *(u32 *)(slot + HYPERNOVA_COLANIM_RENDER_OFF) = packed;  // what the overlay renders
    *(u32 *)(slot + HYPERNOVA_COLANIM_COL2C_OFF)  = packed;  // selector color source

    float *col = (float *)(slot + HYPERNOVA_COLANIM_COLOR_OFF);
    col[0] = (float)r;
    col[1] = (float)g;
    col[2] = (float)b;
    col[3] = (float)HYPERNOVA_RAINBOW_ALPHA;

    slot[HYPERNOVA_COLANIM_FLAGB_OFF] |= 0x80;          // force color-override render path
    slot[HYPERNOVA_COLANIM_FLAGA_OFF]  = (char)0xff;    // ratio/blend path off
}

// Clearing the slot restores normal hurt/invincibility flashes.
static void StopRainbowPlayer(int player)
{
    if (Ply_GetPKind(player) != PKIND_HMN)
        return;
    GOBJ *rg = Ply_GetRiderGObj(player);
    if (!rg)
        return;
    RiderData *rd = rg->userdata;
    ColAnim_Reset((char *)rd + HYPERNOVA_COLANIM_BODY_OFF);
}

// Alpha shapes the swirl, so leaving it untouched keeps the whirlwind's vanilla opacity.
static void TintTevColor(GXColor *c, u8 r, u8 g, u8 b)
{
    c->r = r;
    c->g = g;
    c->b = b;
}

// Rewrites each TObj's tev constant/tev0/tev1 RGB - value fields MObjSetupTev re-reads every
// frame. The TExp node tree itself is never touched (clobbering it crashes the walk).
static void RecolorEffectTree(JOBJ *j, u8 r, u8 g, u8 b)
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
                TintTevColor(&t->tev->constant, r, g, b);
                TintTevColor(&t->tev->tev0, r, g, b);
                TintTevColor(&t->tev->tev1, r, g, b);
            }
        }
        if (j->child != NULL)
            RecolorEffectTree(j->child, r, g, b);
        j = j->sibling;
    }
}

// The spawn discards the handle, so live whirlwinds are found by walking the model-effect
// bucket and matching the Effect kind.
static void RecolorWhirlwinds(u8 r, u8 g, u8 b)
{
    for (GOBJ *g_eff = (*stc_gobj_lookup)[HYPERNOVA_EFFECT_PLINK]; g_eff != NULL; g_eff = g_eff->next)
    {
        if (g_eff->entity_class != HYPERNOVA_EFFECT_GOBJ_KIND)
            continue;
        Effect *eff = g_eff->userdata;
        if (eff == NULL)
            continue;
        if (eff->kind != HYPERNOVA_INHALE_EFFECT_ID)
            continue;
        RecolorEffectTree((JOBJ *)g_eff->hsd_object, r, g, b);
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

// Runs after the frame's game procs so the vacuum's position overrides win over item
// physics/ground-snap.
void Hypernova_OnFrameEnd(void)
{
    if (!hypernova_enabled)
        return;

    if (!InCityTrialGameplay())
    {
        ResetState();
        return;
    }

    // Before the pause early-out, so the cone still renders while paused.
    Hypernova_DebugConeEnsure();

    // Freeze while paused: the procs this cooperates with (model_scale, ColAnim selector,
    // effect models) are frozen too.
    if (Gm_CheckPauseKind(PAUSEKIND_GAME))
        return;

    SelfTestPoll();

    // End on expiry, or the instant a player gains an ability/power-up - before DriveInhale, so
    // the drive doesn't fight the state the engine already moved the rider into this frame.
    for (int i = 0; i < 5; i++)
    {
        if (!stc_active[i])
            continue;
        if (--stc_timer[i] <= 0)
        {
            StopPlayer(i);
            continue;
        }
        GOBJ *rg = Ply_GetRiderGObj(i);
        if (rg != NULL && PlayerHoldsAbility((RiderData *)rg->userdata))
            StopPlayer(i);
    }

    int any_active = 0;
    for (int i = 0; i < 5; i++)
        any_active |= stc_active[i];

    if (any_active)
    {
        stc_hue += 1.0f / (float)HYPERNOVA_RAINBOW_PERIOD;
        if (stc_hue >= 1.0f)
            stc_hue -= 1.0f;
    }

    for (int i = 0; i < 5; i++)
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

        // Whirlwinds run hue-shifted off the bodies' and softened to a light wash.
        float whue = stc_hue + HYPERNOVA_WHIRLWIND_HUE_OFFSET;
        if (whue >= 1.0f)
            whue -= 1.0f;
        u8 wr, wg, wb;
        HueToRgb(whue, &wr, &wg, &wb);
        wr = TowardWhite(wr, HYPERNOVA_WHIRLWIND_TINT);
        wg = TowardWhite(wg, HYPERNOVA_WHIRLWIND_TINT);
        wb = TowardWhite(wb, HYPERNOVA_WHIRLWIND_TINT);
        RecolorWhirlwinds(wr, wg, wb);
    }
}
