#include "game.h"
#include "code_patch/code_patch.h"
#include "deathlink.h"

// hook into the Ply_AddDeath function
// we're actually hooking at the end of the function, just after the function "epilogue" where the typical function
// cleanup begins. Our program cleans itself up, so the only thing left to do for the original function is to clean itself
// up, which doesn't depend on any of the context we messed up when we injected into the function.
CODEPATCH_HOOKCREATE(0x8022f880,     // Memory address to begin executing custom code
                     "",             // ASM instructions to execute before calling our C function.
                     ReceiveDeath,   // Pointer to our C function
                     "",             // ASM instructions to execute after calling our C function.
                     0);             // Return address. Use 0 to branch back to the injection site.

void ReceiveDeath()
{
    // TODO: this currently fires for any death. We need to find a way to get the player index into our function
    // by accepting an argument and then moving some data from whatever register holds the player index into the register
    // that our function takes in. 
    OSReport("Death received!\n");
}


void DeathLinkOnFrame() {
    // read from the deathlink_give location in the static array we created during boot, and if it is 1, kill the player
    if (Gm_IsInCity()) {
        if (Ply_GetPKind(0) != PKIND_NONE) {
                   
        }
    }
}

void DeathLinkPatchesApply() {
    CODEPATCH_HOOKAPPLY(0x8022f880);
}