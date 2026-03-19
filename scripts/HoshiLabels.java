// Hoshi Labels — Ghidra Java Script for Kirby Air Ride (GKYE01)
// Run in Ghidra Script Manager (Window > Script Manager > Run)
//
// Labels known static data addresses derived from the hoshi modding framework
// headers (externals/hoshi/include/*.h) and KARchipelago documentation.
//
// Covers: struct bases, static variable instances, pointer tables, and
// sub-field offsets within GameData. Skips any address that already has a
// USER_DEFINED label. Function symbols are assumed already present via the
// imported GKYE01.map symbol table.
//
// Sources: hoshi/include/*.h, clearchecker-system.md, client-game-protocol.md
//
// =============================================================================
// WORKFLOW: DEFINING STRUCT TYPES AND APPLYING THEM
// =============================================================================
//
// Running this script is step 1. It places named labels at the base address
// of each static struct instance. Ghidra will then show those names in the
// decompiler and listing views, but it won't yet know the internal field
// layout — fields will still appear as numerical offsets like field_0x4 or
// *(int *)(ptr + 0x3a0).
//
// To get Ghidra to auto-populate fields by name, you need to define the struct
// types and apply them to the labeled addresses. Here's how:
//
// STEP 1 — CREATE STRUCT TYPES (Data Type Manager)
//   Open: Window > Data Type Manager
//   Right-click your program archive > New > Structure
//   Name it to match the hoshi header (e.g. "GameData", "PlayerData").
//   Add fields manually using the hoshi header as reference:
//     - Field name  (e.g. "timer_minutes")
//     - Data type   (e.g. int, uint, float, pointer-to-struct, etc.)
//     - Offset      (the hex offset from the struct base)
//   For now, you can add just the fields you care about and leave gaps —
//   Ghidra represents unknown regions as padding bytes automatically.
//   Repeat for each struct you want to annotate.
//
// STEP 2 — APPLY A STRUCT TYPE TO A LABEL
//   In the Listing view, click the address where a struct base is labeled
//   (e.g. click on "GameData" at 0x805359D8).
//   Press 'T' (or right-click > Data > Choose Data Type), then search for
//   the struct name you just created and confirm.
//   Ghidra will overlay the struct layout onto the memory region. Each
//   named field now appears in the listing and decompiler by name.
//
// STEP 3 — APPLY TO POINTER STATICS
//   For "stc_foo_ptr" labels (double-pointer slots in SceneDataBlock), the
//   stored value is a pointer to the real struct. Apply the appropriate
//   pointer type at the stc_ address (e.g. "GameData *" or "PlayerData **"),
//   and Ghidra will propagate type info through dereferences in the
//   decompiler automatically.
//
// STEP 4 — DYNAMIC INSTANCES (RiderData, MachineData, etc.)
//   These structs are allocated per-GObj at runtime and have no fixed address.
//   Define their types in the Data Type Manager anyway. Ghidra will apply
//   the type automatically wherever the decompiler can trace the pointer —
//   e.g. once stc_rdDataKirby_ptr is typed as "rdDataKirby **", any
//   dereference of it in decompiled code will show field names.
//
// FIELD NAMING CONVENTION (from hoshi CLAUDE.md):
//   Gm_   = GameData level (global game state)
//   Ply_  = PlayerData level (per-player)
//   Rider_= RiderData level (character on machine)
//   Machine_ = MachineData level (the vehicle)
//
// =============================================================================
//
//@author KARchipelago
//@category KAR.Hoshi
//@keybinding
//@menupath
//@toolbar

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class HoshiLabels extends GhidraScript {

    @Override
    public void run() throws Exception {
        println("=== hoshi label import: starting ===");
        applyDataLabels();
        println("=== hoshi label import: complete ===");
        println("Next steps:");
        println("  1. Define struct types in Data Type Manager from hoshi headers");
        println("  2. Apply GameData struct at 0x805359D8");
        println("  3. Apply GameClearData at GameData+0xD68, +0xE80, +0xFA8");
        println("  4. Verify r13 at __init_registers (0x8000539C) for any SDA globals");
    }

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    private void label(long addr, String name) throws Exception {
        label(addr, name, null);
    }

    private void label(long addr, String name, String comment) throws Exception {
        Address a = toAddr(addr);
        Symbol sym = getSymbolAt(a);
        if (sym != null && sym.getSource() == SourceType.USER_DEFINED) {
            return;
        }
        createLabel(a, name, true, SourceType.USER_DEFINED);
        if (comment != null) {
            currentProgram.getListing().setComment(a, CodeUnit.PLATE_COMMENT, comment);
        }
    }

    // -------------------------------------------------------------------------
    // DATA LABELS
    // -------------------------------------------------------------------------

    private void applyDataLabels() throws Exception {

        // =====================================================================
        //  CORE GAME STATE — GameData @ 0x805359D8
        //  Source: game.h
        //  The entire persistent game state lives in this BSS struct (~0x1514+ bytes).
        // =====================================================================

        final long GM = 0x805359D8L;

        label(GM,          "GameData",                    "GameData struct base (game.h)");
        label(GM + 0x0C,   "GameData_title_data",         "TitleScreenData (8 bytes)");
        label(GM + 0x30,   "GameData_main_menu",          "MainMenuData sub-struct");
        label(GM + 0x108,  "GameData_airride_select",     "airride_select_ply sub-struct");
        label(GM + 0x1D0,  "GameData_city_select",        "city_select_ply sub-struct");
        label(GM + 0x290,  "GameData_timer_minutes",      "Match timer: minutes");
        label(GM + 0x294,  "GameData_timer_seconds",      "Match timer: seconds within minute");
        label(GM + 0x298,  "GameData_timer_subseconds",   "Match timer: subseconds (0-99)");
        label(GM + 0x29C,  "GameData_match_subsecs_left", "Subseconds remaining in match");
        label(GM + 0x2A0,  "GameData_match_initial_subsecs", "Initial subseconds at match start");
        label(GM + 0x2A4,  "GameData_match_progress",    "Match progress (0.0 -> 1.0)");
        label(GM + 0x2A8,  "GameData_flags_x2a8",        "Flags: bit 0x40 = is_match_intro");
        label(GM + 0x354,  "GameData_city_kind",         "Copied to city_kind");
        label(GM + 0x35D,  "GameData_airride_mode",      "AirRideMode enum");
        label(GM + 0x390,  "GameData_city_sub",          "City Trial sub-struct start");
        label(GM + 0x397,  "GameData_city_flags",        "City flags: game_tempo, events_enable, etc.");
        label(GM + 0x399,  "GameData_city_mode",         "CityMode enum (trial/stadium/freerun)");
        label(GM + 0x39B,  "GameData_city_scene",        "City scene (3=select,4=ingame,5=graph,7=stadium)");
        label(GM + 0x3A0,  "GameData_city_ply_stats",    "float[5][9] — City Trial per-player stats");
        label(GM + 0x454,  "GameData_city_is_bike",      "u8[5] — per-player is_bike flags");
        label(GM + 0x459,  "GameData_city_machine_kind", "u8[5] — per-player machine kind");
        label(GM + 0x45E,  "GameData_city_prev_stadium", "u8[5] — prev stadium kind per player");
        label(GM + 0x5AD,  "GameData_city_stadium_kind", "Current stadium kind");
        label(GM + 0x5AF,  "GameData_city_stadium_round","Current stadium round");

        // Checklist / ClearChecker bitfield cache (within GameData)
        // Built by Checklist_BuildUnlockBitfields; layout per clearchecker-system.md
        label(GM + 0xD50, "GameData_airride_unlock_lo",    "u32 — Air Ride unlock bits 0-31");
        label(GM + 0xD54, "GameData_airride_unlock_hi",    "u32 — Air Ride unlock bits 32-45");
        label(GM + 0xD58, "GameData_citytrial_unlock_lo",  "u32 — City Trial unlock bits 0-31");
        label(GM + 0xD5C, "GameData_citytrial_unlock_hi",  "u32 — City Trial unlock bits 32-43");
        label(GM + 0xD60, "GameData_stadium_flags_cache",  "u32 — cached copy of stadium unlock flags");

        // GameClearData instances — layout: +0x00 new_unlock_flag (u8),
        //   +0x02 checkbox_filler_num (u8), +0x03 filler_list_len (u8),
        //   +0x04 grid_mapping[120] (u8[120]), +0x7C clear[120] (u8[120])
        label(GM + 0xD68, "GameData_AirRideClear",   "GameClearData for Air Ride (244 bytes)");
        label(GM + 0xE80, "GameData_TopRideClear",   "GameClearData for Top Ride (244 bytes)");
        label(GM + 0xF74, "GameData_TopRideStats",   "TopRideStats (52 bytes)");
        label(GM + 0xFA8, "GameData_CityTrialClear", "GameClearData for City Trial (244 bytes)");

        // Icon template / visual cell pointers
        label(GM + 0xEF0, "GameData_icon_templates", "HSD_Archive*[4] — icon template ptrs (types 0-3)");
        label(GM + 0xF0C, "GameData_visual_cells",   "GObj*[] — visual cell array (cell_index * 4)");

        // Stadium unlock flags — two adjacent u32s (stadium.h volatile writes)
        label(GM + 0x1510, "GameData_StadiumFlags",    "u32 — stadium unlock flags (24-bit mask)");
        label(GM + 0x1514, "GameData_StadiumNewLabel", "u32 — stadium new-label flags");

        // =====================================================================
        //  SCENE DATA BLOCK @ 0x805DD0E0
        //  Many hoshi statics are pointer-sized slots within this block.
        //  The block is allocated at scene init; slot contents are double-pointers.
        // =====================================================================

        final long SC = 0x805DD0E0L;

        label(SC, "SceneDataBlock", "Base of large scene/runtime pointer table (many hoshi statics are offsets into this)");

        // game.h
        label(SC + 0x494, "stc_gmdataall_ptr",            "gmDataAll** — global game data pointer (game.h)");
        label(SC + 0x600, "stc_legendary_piece_data_ptr", "LegendaryPieceData** — Dragoon/Hydra piece data (game.h)");
        label(SC + 0x608, "stc_grBoxGeneObj_ptr",         "grBoxGeneObj** — item box spawner GObjs (game.h)");
        label(SC + 0x610, "stc_grBoxGeneInfo_ptr",        "grBoxGeneInfo** — item box spawner info (game.h)");
        label(SC + 0x754, "stc_city_machine_num_ptr",     "int* — number of machines in City Trial (game.h)");

        // machine.h
        label(SC + 0x758, "stc_vcDataCommon_ptr",  "vcDataCommon** — common vehicle data (machine.h)");
        label(SC + 0x770, "stc_vcDataKindStar_ptr","vcDataKindStar** — per-kind star vehicle data (machine.h)");
        label(SC + 0x780, "stc_machinespawn_gobj", "GOBJ* — machine spawn gobj (MachineSpawnData) (machine.h)");

        // item.h
        label(SC + 0x7E8, "stc_item_param_ptr",     "ItemCommonParam** (item.h)");
        label(SC + 0x7EC, "stc_item_param2_ptr",    "ItemParam2** (item.h)");
        label(SC + 0x7F0, "stc_it_common_data_ptr", "itCommonDataAll** (game.h)");

        // event.h
        label(SC + 0x618, "stc_eventcheck_gobj_ptr",          "GOBJ** — event check gobj (EventCheckData) (event.h)");
        label(SC + 0x750, "stc_event_machineformation_loadnum","int* — machines spawned for formation event (event.h)");

        // hud.h
        label(SC + 0x690, "stc_if_all_archive_ptr",      "HSD_Archive** — IfAll HUD archive (hud.h)");

        // menu.h
        label(SC + 0x6DC, "stc_MnSelplyAll_archive_ptr", "HSD_Archive** — MnSelplyAll player select archive (menu.h)");

        // stage.h
        label(SC + 0x5EC, "stc_grobj_ptr", "GrObj** — current stage GrObj pointer (stage.h)");

        // =====================================================================
        //  PLAYER DATA (game.h)
        // =====================================================================

        label(0x8055A9F0L, "stc_playerdata", "PlayerData[4] — per-player runtime state (game.h)");

        // =====================================================================
        //  MACHINE / VEHICLE DATA (machine.h)
        // =====================================================================

        label(0x8055A068L, "stc_vcDataLookup", "vcDataLookup — machine stat lookup table (machine.h)");

        // =====================================================================
        //  RIDER DATA (rider.h)
        // =====================================================================

        label(0x80559FA8L, "stc_rdDataKirby_ptr", "rdDataKirby** — Kirby rider data pointer (rider.h)");

        // =====================================================================
        //  CAMERA (camera.h)
        // =====================================================================

        label(0x80557248L, "stc_plycam_lookup", "PlayerCamLookup[32] — per-player camera lookup (camera.h)");

        // =====================================================================
        //  HSD ENGINE (hsd.h)
        // =====================================================================

        label(0x804C23ECL, "stc_hsd_default_table",      "HSD_IDTable — default HSD ID table (hsd.h)");
        label(0x8046B0F0L, "stc_HSD_VI",                 "HSD_VI struct (hsd.h)");
        label(0x80479D58L, "stc_hsd_update",             "HSD_Update — engine update state (pause, frame counter) (hsd.h)");
        label(0x805DCD38L, "stc_rng_seed_ptr",           "int** — RNG seed pointer (hsd.h)");
        label(0x805DCD30L, "hsd_rand_seed",              "int* — HSD random seed value (hsd.h)");
        label(0x8058B634L, "stc_engine_pads",            "HSD_Pad[4] — engine-level pad state (hsd.h)");
        label(0x80494F68L, "stc_pause_plink_whitelists", "u64[] — PauseKind -> p_link whitelist bitfields (hsd.h)");
        label(0x8058B080L, "stc_hsd_padqueue",           "HSD_PadQueueInfo (hsd.h)");
        label(0x804D76C8L, "stc_hsd_pixelfmt",           "GXPixelFmt* (hsd.h)");
        label(0x805DD630L, "stc_dblevel",                "DebugLevel (0=master/off, 4=develop) (hsd.h)");
        label(0x8058C190L, "stc_gobj_init_data",         "HSD_GObjInitData — gobj system init data (hsd.h)");

        // =====================================================================
        //  GOBJ / HSD OBJECTS (obj.h)
        // =====================================================================

        label(0x805DE334L, "stc_gobj_lookup",            "GOBJ*** — gobj linked-list lookup table (obj.h)");
        label(0x804CE382L, "stc_gobj_proc_num",          "u8 — number of gobj proc entries (obj.h)");
        label(0x804D7840L, "stc_gobjproc_lookup",        "GOBJProc*** — gobj proc pointer array (obj.h)");
        label(0x804D7838L, "stc_gobjproc_cur",           "GOBJProc** — current gobj proc being processed (obj.h)");
        label(0x804D783CL, "stc_gobjproc_updateidx_cur", "u32* — update index of current gobj proc (obj.h)");
        label(0x805DEB20L, "stc_cobj_aspect",            "float* — camera object aspect ratio (obj.h)");

        // =====================================================================
        //  AUDIO (audio.h)
        //  Note: 0x80538088 is shared between AudioSourceTable and EventGlobal;
        //  audio.h declares it AudioSourceTable, event.h reinterprets it as EventGlobal.
        // =====================================================================

        label(0x80538088L, "audio_source_table",      "AudioSourceTable / EventGlobal (audio.h / event.h — same address)");
        label(0x80508BC8L, "stc_bgm_data_arr",        "BGMData[3] — 0=?, 1=main song, 2=event song (audio.h)");
        label(0x804C45A0L, "fgm_live",                "FGMLive* — foreground music/SFX live data (audio.h)");
        label(0x80596DA0L, "ax_live",                 "AXLive — AX DSP audio live data (audio.h)");
        label(0x804C2C64L, "stc_voice_data",          "VPB — voice parameter block (audio.h)");
        label(0x8054F508L, "stc_bgmkind_cur_playing", "BGMKind* — currently playing BGM kind (audio.h)");
        label(0x80498750L, "stc_bgm_desc",            "BGMDesc[] — BGM descriptors (kind, path, flags) (audio.h)");

        // =====================================================================
        //  EVENTS — City Trial (event.h)
        // =====================================================================

        label(0x804A5410L, "stc_event_function", "EventFunction[EVKIND_NUM] — per-event-kind function table (event.h)");

        // =====================================================================
        //  HIT COLLISION (hurt.h)
        // =====================================================================

        label(0x80559BF4L, "stc_hitcolldata", "HitCollData — global hit collision data (hurt.h)");

        // =====================================================================
        //  MEMCARD / SAVE (memcard.h)
        // =====================================================================

        label(0x805528F8L, "stc_memcard_unk", "MemcardUnk struct (memcard.h)");
        label(0x8059A880L, "stc_save_info",   "SaveInfo struct — save_size at +0x54C (memcard.h)");

        // =====================================================================
        //  CHECKLIST / REWARD TABLES (game.h, clearchecker-system.md)
        // =====================================================================

        label(0x8049755CL, "stc_reward_table_ptrs",       "RewardEntry*[3] — per-mode reward table pointers (game.h)");
        label(0x804AD270L, "stc_checkbox_filler_indices",  "u8[15] — checkbox filler reward indices, 5 per mode (game.h)");
        label(0x80552A4CL, "stc_unlock_cache_valid_ptr",   "u32* — non-null when unlock bitfield cache is valid");
        label(0x805D51D0L, "stc_clear_num",                "u8[3] — clear/reward counts per GMMODE (game.h)");
        label(0x80495816L, "stc_city_starting_machine",    "u8 — City Trial starting machine kind (game.h)");

        // =====================================================================
        //  SCENE / MENU (scene.h)
        // =====================================================================

        label(0x80558788L, "stc_scene_menu_common", "ScMenuCommon — shared menu scene state (scene.h)");
        label(0x80495058L, "stc_major_scene_desc",  "MajorSceneDesc[] — major scene descriptors (scene.h)");
        label(0x80495154L, "stc_minor_scene_desc",  "MinorSceneDesc[] — minor scene descriptors (scene.h)");
        label(0x804962B0L, "stc_menu_select",       "ScMenuSelect — menu selection state (scene.h)");

        // =====================================================================
        //  STAGE (stage.h)
        // =====================================================================

        label(0x80557638L, "stc_grdatalookup", "GrData*[] — stage data pointer array indexed by GroundKind (stage.h)");

        // =====================================================================
        //  STADIUM (stadium.h)
        // =====================================================================

        label(0x80535A9CL, "stc_stadium_option_to_kind", "u8[] — stadium menu option -> StadiumKind mapping (stadium.h)");

        // =====================================================================
        //  PRELOAD (preload.h)
        // =====================================================================

        label(0x80550F68L, "stc_preload_table",        "Preload — main preload table (80 entries) (preload.h)");
        label(0x80558818L, "stc_preload_menu_files",   "int* — preloaded menu file flags (preload.h)");
        label(0x80489648L, "stc_preload_entry_descs",  "PreloadEntryDesc[82] — preload file descriptors (preload.h)");
        label(0x80537F40L, "stc_preload_heaps_lookup", "PreloadHeapLookup — DRAM/ARAM heap info (preload.h)");
        label(0x80497ED0L, "stc_preload_heap_descs",   "PreloadHeapDesc[] — heap descriptors; kind 10 = terminator (preload.h)");

        // =====================================================================
        //  TEXT SYSTEM (text.h)
        // =====================================================================

        label(0x805DE558L, "stc_textheap_size",    "int* — text heap total size (text.h)");
        label(0x805DE55CL, "stc_textheap_start",   "TextHeapCell** — text heap start (text.h)");
        label(0x805DE560L, "stc_textheap_free",    "TextHeapCell** — text heap free list head (text.h)");
        label(0x805DE568L, "stc_text_first",       "Text** — head of Text linked list (text.h)");
        label(0x805DE56CL, "stc_textcanvas_first", "TextCanvas** — head of TextCanvas linked list (text.h)");

        // =====================================================================
        //  HUD / MENU (hud.h, menu.h)
        // =====================================================================

        label(0x80496458L, "stc_soundtest_desc", "SoundTestDesc[62] — sound test menu entries (menu.h)");

        // =====================================================================
        //  OS / FILESYSTEM (os.h)
        // =====================================================================

        label(0x80000000L, "os_info",               "OSInfo — OS info block at MEM1 base (os.h)");
        label(0x805DDEB0L, "stc_OSHeapTable_ptr",   "OSHeap** — OS heap table pointer (os.h)");
        label(0x805DDD8CL, "stc_fst_entries_ptr",   "FSTEntry** — FST entry array (indexed by entrynum) (os.h)");
        label(0x805DDD90L, "stc_fst_filenames_ptr", "char** — FST filename string table (os.h)");
        label(0x805DDD94L, "stc_fst_totalentrynum", "int* — total FST entry count (os.h)");
        label(0x804D740CL, "stc_si_sampling_rate",  "int* — SI sampling rate (controller polling Hz) (os.h)");
        label(0x80402CA0L, "stc_si_xy",             "SIXYLookup — SI XY stick calibration data (os.h)");
        label(0x8058D198L, "osreport_data",         "OSReportData — OS debug report state (os.h)");

        // =====================================================================
        //  GX HARDWARE (gx.h)
        // =====================================================================

        label(0xCC008000L, "gx_pipe",       "GXPipe — GX command FIFO hardware register (write-only) (gx.h)");
        label(0x804C0980L, "stc_vi_unknown","VIUnknown struct (gx.h)");

        // =====================================================================
        //  AP MOD (main.h — KARchipelago)
        //  ArchipelagoData is heap-allocated on boot; the pointer to it is
        //  stored at this fixed static slot so the Python AP client can find it
        //  via dolphin-memory-engine without scanning the heap.
        // =====================================================================

        label(0x805D52D4L, "stc_ap_data_ptr",
              "ArchipelagoData* — AP mod data pointer (main.h); set at boot to the heap-allocated ArchipelagoData block");
    }
}
