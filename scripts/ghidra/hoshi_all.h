/*
 * Master header aggregating hoshi's game-relevant type definitions for
 * Ghidra's C-Source parser. Includes every top-level hoshi header whose
 * struct/enum bodies we want in the Ghidra data type manager, in rough
 * dependency order. Excluded: inline.h (function bodies) and the ip/*
 * networking stack (both shadowed by empty stubs on the include path).
 */

/* Neutralize C11 static assertions the parser doesn't evaluate. */
#define _Static_assert(cond, msg)
#define static_assert(cond, msg)

/* Strip storage/inline keywords left on the (body-stripped) prototypes. */
#define static
#define inline

/* Foundation: forward-decl typedefs + scalar base types. */
#include "structs.h"
#include "datatypes.h"

/* OS / low-level */
#include "os.h"
#include "exi.h"
#include "memcard.h"

/* HSD graphics core */
#include "gx.h"
#include "obj.h"
#include "hsd.h"

/* Audio */
#include "audio.h"

/* Physics / collision / triggers */
#include "collision.h"
#include "hurt.h"
#include "trigger.h"
#include "camera.h"

/* Presentation helpers */
#include "effect.h"
#include "color.h"
#include "devtext.h"
#include "hud.h"
#include "text.h"

/* Loading */
#include "preload.h"

/* Scene / mode */
#include "scene.h"
#include "stadium.h"
#include "menu.h"

/* Gameplay entities */
#include "item.h"
#include "machine.h"
#include "rider.h"
#include "projectile.h"
#include "enemy.h"
#include "yakumono.h"
#include "stage.h"
#include "event.h"
#include "topride.h"

/* Top-level game state (pulls most of the above) */
#include "game.h"
