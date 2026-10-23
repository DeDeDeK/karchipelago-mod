#ifndef ENEMY_AI_H
#define ENEMY_AI_H

typedef enum EnemyAIPreset
{
    ENEMY_AI_DEFAULT,
    ENEMY_AI_AGGRESSIVE,
    ENEMY_AI_RELENTLESS,
    ENEMY_AI_DOCILE,
    ENEMY_AI_ERRATIC,
    ENEMY_AI_TANKY,
    ENEMY_AI_RANDOM, // menu-only, rolls one of the presets above Default
    ENEMY_AI_MENU_NUM,
} EnemyAIPreset;

typedef enum EnemyDamage
{
    ENEMY_DAMAGE_NONE,
    ENEMY_DAMAGE_HALF,
    ENEMY_DAMAGE_DEFAULT,
    ENEMY_DAMAGE_DOUBLE,
    ENEMY_DAMAGE_TRIPLE,
    ENEMY_DAMAGE_NUM,
} EnemyDamage;

typedef enum EnemyBehavior
{
    ENEMY_BEHAVIOR_DEFAULT,
    ENEMY_BEHAVIOR_PURSUE,
    ENEMY_BEHAVIOR_SKITTISH,
    ENEMY_BEHAVIOR_NUM,
} EnemyBehavior;

extern int enemy_damage;
extern int enemy_behavior;
extern char *enemy_damage_names[ENEMY_DAMAGE_NUM];
extern char *enemy_behavior_names[ENEMY_BEHAVIOR_NUM];

extern int enemy_ai_preset_ar;
extern int enemy_ai_preset_ct;
extern char *enemy_ai_preset_names[ENEMY_AI_MENU_NUM];

void EnemyAI_On3DLoadEnd(void);
void EnemyAI_InstallHooks(void);

#endif // ENEMY_AI_H
