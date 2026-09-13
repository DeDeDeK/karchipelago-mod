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

extern int enemy_ai_preset_ar;
extern int enemy_ai_preset_ct;
extern char *enemy_ai_preset_names[ENEMY_AI_MENU_NUM];

void EnemyAI_InstallHook(void);

#endif // ENEMY_AI_H
