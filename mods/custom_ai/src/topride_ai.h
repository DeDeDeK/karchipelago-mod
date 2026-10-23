#ifndef TOPRIDE_AI_H
#define TOPRIDE_AI_H

typedef enum TopRideAIPreset
{
    TOPRIDE_AI_DEFAULT,
    TOPRIDE_AI_CAUTIOUS,
    TOPRIDE_AI_AGGRESSIVE,
    TOPRIDE_AI_HOARDER,
    TOPRIDE_AI_EXPERT,
    TOPRIDE_AI_RANDOM, // menu-only, rolls one of the presets above Default per slot
    TOPRIDE_AI_MENU_NUM,
} TopRideAIPreset;

extern int topride_ai_menu;
extern char *topride_ai_names[TOPRIDE_AI_MENU_NUM];

void TopRideAI_OnLoadEnd(void);
void TopRideAI_InstallHooks(void);

#endif // TOPRIDE_AI_H
