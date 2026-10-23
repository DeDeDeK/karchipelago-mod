#ifndef CUSTOM_AI_API_H
#define CUSTOM_AI_API_H

// CPU behavior controls for other mods. Import via Hoshi_ImportMod from OnSaveLoaded or
// later: mods boot in alphabetical order.

#define CUSTOM_AI_MOD_NAME "custom_ai"
#define CUSTOM_AI_API_MAJOR 1
#define CUSTOM_AI_API_MINOR 0

typedef struct CustomAIAPI
{
    // Every CPU chases ply for frames, in the City Trial city across the whole map, and
    // quick-spins it when close. A new call replaces the running hunt; the next 3D scene
    // ends it.
    void (*Hunt)(int ply, int frames);

    // Every CPU plays at the top CPU level and attacks its rival for frames. The next 3D scene
    // ends it.
    void (*Frenzy)(int frames);

    // Added to every CPU's level at its next init, clamped to the 0-8 range, on top of
    // the Adaptive CPUs setting.
    void (*SetLevelOffset)(int offset);
} CustomAIAPI;

#endif // CUSTOM_AI_API_H
