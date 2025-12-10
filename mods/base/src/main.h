#include "structs.h"

typedef struct TemplateSave
{
    int boot_num;
    int item_received_index;
} TemplateSave;

typedef struct PerFrameFuncData
{
    int timer;
} PerFrameFuncData;

void Func_PerFrame(GOBJ *g);