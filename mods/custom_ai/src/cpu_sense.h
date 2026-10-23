#ifndef CPU_SENSE_H
#define CPU_SENSE_H

#include "rider.h"

#define CPU_SENSE_METEOR_MAX 4

// One rider as every CPU sees it, refreshed once per tick for each slot.
typedef struct CpuRiderView
{
    u8 present;
    u8 human;
    u8 on_foot;
    u8 spinning;   // a quick spin
    u8 boosting;   // dash panel, charge-release boost hitbox
    u8 invincible; // Candy, or an invincible or intangible hurt state
    u8 stunned;    // a damage reaction, on the machine or knocked off it
    float hp_ratio;
    float progress; // metres along the race course, 0 off a course
    Vec3 pos;
    Vec3 vel;
} CpuRiderView;

// The machine a CPU rides, refreshed at the start of its update.
typedef struct CpuSelfView
{
    float speed;  // |world_velocity|, |self_vel| on foot
    float charge; // 0-1
} CpuSelfView;

typedef struct CpuMeteorView
{
    Vec3 impact;
    float radius;
    float frames;
} CpuMeteorView;

typedef struct CpuWorldView
{
    float round_progress; // City Trial round, 0-1
    int event_kind;       // the running City Trial event, -1 for none
    int meteor_num;
    CpuMeteorView meteor[CPU_SENSE_METEOR_MAX];
} CpuWorldView;

const CpuRiderView *CpuSense_Rider(int ply);
const CpuSelfView *CpuSense_Self(int ply);
const CpuWorldView *CpuSense_World(void);
int CpuSense_IsThreatening(int ply);
int CpuSense_IsVulnerable(int ply);
void CpuSense_UpdateSelf(RiderData *rd);
void CpuSense_OnTick(void);

#endif // CPU_SENSE_H
