void ReceiveDeath();

typedef struct DeathLinkPerFrameData {
    int framecounter; 
} DeathLinkPerFrameData;

void DeathLinkPerFrame(GOBJ *g);
void DeathLinkPatchesApply();
void DeathLink_OnSceneChange();