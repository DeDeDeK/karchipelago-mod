// HoshiStructs — Ghidra Java Script for Kirby Air Ride (GKYE01)
// Run in Ghidra Script Manager (Window > Script Manager > Run)
//
// Creates struct types in the Data Type Manager under the /hoshi category,
// derived directly from the hoshi framework headers (externals/hoshi/include/).
// Re-running the script is safe — types are replaced, not duplicated.
//
// After running, Ghidra will use these types when you:
//   - Click a labeled address and press 'T' to apply a type
//   - Dereference a typed pointer in the decompiler (propagates automatically)
//
// To add more fields as they are reverse engineered, find the relevant
// make*() method and add a replaceAtOffset() call. The offset is the
// byte offset from the start of the struct (not an absolute address).
//
// Sources: hoshi/include/game.h, machine.h, rider.h, hsd.h, hurt.h,
//          obj.h, scene.h, preload.h, datatypes.h, clearchecker-system.md
//
//@author KARchipelago
//@category KAR.Hoshi
//@keybinding
//@menupath
//@toolbar

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;

public class HoshiStructs extends GhidraScript {

    private DataTypeManager dtm;
    private CategoryPath cat;

    @Override
    public void run() throws Exception {
        dtm = currentProgram.getDataTypeManager();
        cat = new CategoryPath("/hoshi");

        println("=== hoshi struct import: starting ===");

        // ---------------------------------------------------------------
        // Build types in dependency order (referenced types first).
        // Each make*() returns an unregistered type; reg() registers it
        // and returns the canonical DTM version to use as a field type.
        // ---------------------------------------------------------------

        // Primitives / geometry
        DataType vec2 = reg(makeVec2());
        DataType vec3 = reg(makeVec3());
        DataType vec4 = reg(makeVec4());

        // HSD engine
        DataType hsdPad     = reg(makeHSD_Pad());
        DataType hsdUpdate  = reg(makeHSD_Update());
        DataType hsdArchive = reg(makeHSD_Archive());

        // Combat / collision
        DataType hurtDesc   = reg(makeHurtDesc(vec3));
        DataType hurtData   = reg(makeHurtData(vec3));
        DataType hitCollData = reg(makeHitCollData(hurtData));

        // Scene / stage
        DataType majorSceneDesc = reg(makeMajorSceneDesc());
        DataType minorSceneDesc = reg(makeMinorSceneDesc());
        DataType grObj           = reg(makeGrObj());

        // Preload
        DataType preloadEntry = reg(makePreloadEntry());

        // Game data
        DataType gameData        = reg(makeGameData(hsdUpdate));
        DataType gameClearData   = reg(makeGameClearData());
        DataType playerData      = reg(makePlayerData());
        DataType machineSpawnData = reg(makeMachineSpawnData(vec3));

        // Character data (dynamic allocation — no static base to apply at)
        DataType riderData   = reg(makeRiderData(vec3, hurtData));
        DataType machineData = reg(makeMachineData(vec3, hurtData));

        // AP mod types (main.h — KARchipelago)
        // ArchipelagoData is heap-allocated; the static slot at 0x805D52D4 holds
        // an ArchipelagoData* so the Python client can find it without heap scanning.
        DataType apSlotOptions   = reg(makeAPSlotOptions());
        DataType textBoxMessage  = reg(makeTextBoxMessage(vec2, vec3));
        DataType archipelagoData = reg(makeArchipelagoData(textBoxMessage, apSlotOptions));

        println("Created " + 22 + " struct types in /hoshi.");

        // ---------------------------------------------------------------
        // Apply types at known static instances.
        // Skipped for dynamic per-GObj structs (RiderData, MachineData).
        // ---------------------------------------------------------------
        applyType(0x805359D8L, gameData);             // GameData (global game state)
        applyType(0x80479D58L, hsdUpdate);
        applyType(0x8058B634L, arr(hsdPad, 4));          // stc_engine_pads: HSD_Pad[4]
        applyType(0x80559BF4L, hitCollData);              // stc_hitcolldata
        applyType(0x80536740L, gameClearData);            // GameData_AirRideClear
        applyType(0x80536858L, gameClearData);            // GameData_TopRideClear
        applyType(0x80536980L, gameClearData);            // GameData_CityTrialClear
        applyType(0x8055A9F0L, arr(playerData, 4));       // stc_playerdata: PlayerData[4]
        applyType(0x805D52D4L, ptr(archipelagoData));     // stc_ap_data_ptr: ArchipelagoData*

        println("Applied types at 9 static addresses.");
        println("=== hoshi struct import: complete ===");
    }

    // =========================================================================
    // Helpers
    // =========================================================================

    /** Register a type with the DTM (replace on conflict). Returns the canonical instance. */
    private DataType reg(DataType dt) {
        return dtm.addDataType(dt, DataTypeConflictHandler.REPLACE_HANDLER);
    }

    private StructureDataType makeStruct(String name, int size) {
        return new StructureDataType(cat, name, size, dtm);
    }

    /** void* (4-byte pointer, GameCube is 32-bit) */
    private DataType voidPtr() {
        return new PointerDataType(VoidDataType.dataType, 4, dtm);
    }

    /** Typed pointer (4 bytes) */
    private DataType ptr(DataType target) {
        return new PointerDataType(target, 4, dtm);
    }

    /** Inline array type */
    private DataType arr(DataType element, int count) {
        return new ArrayDataType(element, count, -1, dtm);
    }

    /**
     * Apply a data type at a static address.
     * Clears any existing code units in the range first.
     */
    private void applyType(long addr, DataType dt) throws Exception {
        int len = dt.getLength();
        if (len <= 0) {
            println("WARN: skipping " + dt.getName() + " at 0x"
                    + Long.toHexString(addr) + " — variable-length type");
            return;
        }
        Address a = toAddr(addr);
        currentProgram.getListing().clearCodeUnits(a, a.add(len - 1), false);
        try {
            currentProgram.getListing().createData(a, dt);
        } catch (Exception e) {
            println("WARN: could not apply " + dt.getName() + " at 0x"
                    + Long.toHexString(addr) + ": " + e.getMessage());
        }
    }

    // =========================================================================
    // Type definitions
    // =========================================================================

    // -------------------------------------------------------------------------
    //  GEOMETRY (datatypes.h)
    // -------------------------------------------------------------------------

    private DataType makeVec2() {
        StructureDataType s = makeStruct("Vec2", 8);
        s.replaceAtOffset(0, FloatDataType.dataType, 4, "X", null);
        s.replaceAtOffset(4, FloatDataType.dataType, 4, "Y", null);
        return s;
    }

    private DataType makeVec3() {
        StructureDataType s = makeStruct("Vec3", 12);
        s.replaceAtOffset(0, FloatDataType.dataType, 4, "X", null);
        s.replaceAtOffset(4, FloatDataType.dataType, 4, "Y", null);
        s.replaceAtOffset(8, FloatDataType.dataType, 4, "Z", null);
        return s;
    }

    private DataType makeVec4() {
        StructureDataType s = makeStruct("Vec4", 16);
        s.replaceAtOffset(0,  FloatDataType.dataType, 4, "X", null);
        s.replaceAtOffset(4,  FloatDataType.dataType, 4, "Y", null);
        s.replaceAtOffset(8,  FloatDataType.dataType, 4, "Z", null);
        s.replaceAtOffset(12, FloatDataType.dataType, 4, "W", null);
        return s;
    }

    // -------------------------------------------------------------------------
    //  HSD ENGINE (hsd.h)
    // -------------------------------------------------------------------------

    private DataType makeHSD_Pad() {
        // Per-player controller state snapshot. Applied at stc_engine_pads[4].
        StructureDataType s = makeStruct("HSD_Pad", 0x42);
        s.replaceAtOffset(0x00, IntegerDataType.dataType,     4, "held",         "held buttons (bitmask)");
        s.replaceAtOffset(0x04, IntegerDataType.dataType,     4, "heldPrev",     "held buttons previous frame");
        s.replaceAtOffset(0x08, IntegerDataType.dataType,     4, "down",         "newly pressed this frame");
        s.replaceAtOffset(0x0c, IntegerDataType.dataType,     4, "rapidFire",    "held with rapid-fire repeat");
        s.replaceAtOffset(0x10, IntegerDataType.dataType,     4, "up",           "newly released this frame");
        s.replaceAtOffset(0x14, IntegerDataType.dataType,     4, "rapidTimer",   "rapid-fire timer");
        s.replaceAtOffset(0x18, SignedByteDataType.dataType,  1, "stickX",       "left stick X (-128..127)");
        s.replaceAtOffset(0x19, SignedByteDataType.dataType,  1, "stickY",       "left stick Y (-128..127)");
        s.replaceAtOffset(0x1a, SignedByteDataType.dataType,  1, "substickX",    "C-stick X");
        s.replaceAtOffset(0x1b, SignedByteDataType.dataType,  1, "substickY",    "C-stick Y");
        s.replaceAtOffset(0x1c, ByteDataType.dataType,        1, "triggerLeft",  "L trigger (0-255)");
        s.replaceAtOffset(0x1d, ByteDataType.dataType,        1, "triggerRight", "R trigger (0-255)");
        // 0x1e-0x1f: implicit alignment padding before float fields
        s.replaceAtOffset(0x20, FloatDataType.dataType,       4, "fstickX",       "left stick X (float -1..1)");
        s.replaceAtOffset(0x24, FloatDataType.dataType,       4, "fstickY",       "left stick Y (float -1..1)");
        s.replaceAtOffset(0x28, FloatDataType.dataType,       4, "fsubstickX",    "C-stick X (float)");
        s.replaceAtOffset(0x2c, FloatDataType.dataType,       4, "fsubstickY",    "C-stick Y (float)");
        s.replaceAtOffset(0x30, FloatDataType.dataType,       4, "ftriggerLeft",  "L trigger (float 0..1)");
        s.replaceAtOffset(0x34, FloatDataType.dataType,       4, "ftriggerRight", "R trigger (float 0..1)");
        s.replaceAtOffset(0x38, FloatDataType.dataType,       4, "x38",           null);
        s.replaceAtOffset(0x3c, FloatDataType.dataType,       4, "x3c",           null);
        s.replaceAtOffset(0x40, ByteDataType.dataType,        1, "x40",           null);
        s.replaceAtOffset(0x41, SignedByteDataType.dataType,  1, "status",        "0=plugged in, -1=unplugged");
        return s;
    }

    private DataType makeHSD_Update() {
        // Engine update/pause state. Embedded in GameData at +0x7E0; also standalone
        // at stc_hsd_update (0x80479D58). Field offsets here are struct-relative
        // (i.e. GameData-relative offsets minus 0x7E0).
        StructureDataType s = makeStruct("HSD_Update", 0x34);
        s.replaceAtOffset(0x00, IntegerDataType.dataType,          4, "x0",                    null);
        s.replaceAtOffset(0x04, UnsignedIntegerDataType.dataType,  4, "engine_frames",          "frames since engine start");
        s.replaceAtOffset(0x08, UnsignedIntegerDataType.dataType,  4, "sys_frames",             "system frame counter");
        s.replaceAtOffset(0x0c, UnsignedIntegerDataType.dataType,  4, "x0c",                    null);
        s.replaceAtOffset(0x10, ByteDataType.dataType,             1, "pause_kind",             "1 << PauseKind");
        s.replaceAtOffset(0x11, ByteDataType.dataType,             1, "pause_kind_prev",        "previous pause kind");
        s.replaceAtOffset(0x12, ByteDataType.dataType,             1, "is_frame_advance",       "0=none 1=advance");
        s.replaceAtOffset(0x13, ByteDataType.dataType,             1, "is_frame_advance_prev",  "previous frame-advance state");
        s.replaceAtOffset(0x14, voidPtr(),                         4, "isRequestPause",         "fn ptr: int (*)(void)");
        s.replaceAtOffset(0x18, voidPtr(),                         4, "isRequestFrameAdvance",  "fn ptr: int (*)(void)");
        s.replaceAtOffset(0x1c, IntegerDataType.dataType,          4, "x1c",                    null);
        s.replaceAtOffset(0x20, UnsignedLongLongDataType.dataType, 8, "plink_whitelist",        "allowed GObj plinks during pause state change");
        s.replaceAtOffset(0x28, UnsignedLongLongDataType.dataType, 8, "plink_whitelist_prev",   "previous allowed plinks");
        s.replaceAtOffset(0x30, voidPtr(),                         4, "funcs",                  "ptr to engine function table");
        return s;
    }

    private DataType makeHSD_Archive() {
        // Loaded HAL archive (dat/usd file). 0x44 bytes.
        StructureDataType s = makeStruct("HSD_Archive", 0x44);
        // Inlined header (first 0x20 bytes)
        s.replaceAtOffset(0x00, UnsignedIntegerDataType.dataType, 4, "file_size",   null);
        s.replaceAtOffset(0x04, UnsignedIntegerDataType.dataType, 4, "data_size",   null);
        s.replaceAtOffset(0x08, UnsignedIntegerDataType.dataType, 4, "nb_reloc",    "number of relocation entries");
        s.replaceAtOffset(0x0c, UnsignedIntegerDataType.dataType, 4, "nb_public",   "number of public symbol entries");
        s.replaceAtOffset(0x10, UnsignedIntegerDataType.dataType, 4, "nb_extern",   "number of external symbol entries");
        s.replaceAtOffset(0x14, arr(ByteDataType.dataType, 4),    4, "version",     "HAL archive version bytes");
        // 0x18-0x1f: padding
        // Runtime fields (0x20+)
        s.replaceAtOffset(0x20, voidPtr(), 4, "data",        "ptr to raw data section");
        s.replaceAtOffset(0x24, voidPtr(), 4, "reloc_info",  "HSD_ArchiveRelocationInfo*");
        s.replaceAtOffset(0x28, voidPtr(), 4, "public_info", "HSD_ArchivePublicInfo*");
        s.replaceAtOffset(0x2c, voidPtr(), 4, "extern_info", "HSD_ArchiveExternInfo*");
        s.replaceAtOffset(0x30, voidPtr(), 4, "symbols",     "char* — symbol string table");
        s.replaceAtOffset(0x34, voidPtr(), 4, "next",        "HSD_Archive* — next in chain");
        s.replaceAtOffset(0x38, voidPtr(), 4, "name",        "char* — archive name");
        s.replaceAtOffset(0x3c, UnsignedIntegerDataType.dataType, 4, "flags", null);
        s.replaceAtOffset(0x40, voidPtr(), 4, "top_ptr",     "ptr to first public data entry");
        return s;
    }

    // -------------------------------------------------------------------------
    //  COMBAT / COLLISION (hurt.h)
    // -------------------------------------------------------------------------

    private DataType makeHurtDesc(DataType vec3) {
        // Static collision sphere description (usually a ROM data table). 0x18 bytes.
        StructureDataType s = makeStruct("HurtDesc", 0x18);
        s.replaceAtOffset(0x00, IntegerDataType.dataType, 4, "joint_idx", "bone this hurtbox is attached to");
        s.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "x4",        null);
        s.replaceAtOffset(0x08, FloatDataType.dataType,   4, "scale",     "collision sphere radius scale");
        s.replaceAtOffset(0x0c, vec3,                    12, "offset",    "offset from bone in local space");
        return s;
    }

    private DataType makeHurtData(DataType vec3) {
        // Runtime hurtbox / vulnerability state for a rider or machine. 0x9C bytes.
        StructureDataType s = makeStruct("HurtData", 0x9C);
        s.replaceAtOffset(0x00, IntegerDataType.dataType, 4, "kind",           "HurtKind enum");
        s.replaceAtOffset(0x04, voidPtr(),                4, "desc",           "HurtDesc* — static collision description");
        s.replaceAtOffset(0x08, IntegerDataType.dataType, 4, "xc_obj_kind",    "owner type: 2=stage, 4=rider/machine");
        s.replaceAtOffset(0x0c, voidPtr(),                4, "xc_obj",         "ptr to owning object");
        s.replaceAtOffset(0x10, IntegerDataType.dataType, 4, "x10",            null);
        s.replaceAtOffset(0x14, voidPtr(),                4, "x14",            "sub-hurt data array ptr");
        s.replaceAtOffset(0x18, voidPtr(),                4, "x18",            null);
        s.replaceAtOffset(0x1c, IntegerDataType.dataType, 4, "hitcoll_log_idx","index in HitCollData.log");
        s.replaceAtOffset(0x20, IntegerDataType.dataType, 4, "x20",            null);
        s.replaceAtOffset(0x24, FloatDataType.dataType,   4, "kb_mag",         "knockback magnitude (set by HitColl_SetDamageLog)");
        s.replaceAtOffset(0x28, FloatDataType.dataType,   4, "dmg_taken",      "damage taken this hit");
        s.replaceAtOffset(0x2c, FloatDataType.dataType,   4, "x2c",            null);
        // 0x30-0x47: unknown int fields
        s.replaceAtOffset(0x48, vec3,                    12, "coll_pos",       "collision contact position XYZ");
        s.replaceAtOffset(0x54, IntegerDataType.dataType, 4, "flags",          null);
        s.replaceAtOffset(0x68, voidPtr(),                4, "x68_obj",        "associated object reference");
        s.replaceAtOffset(0x80, voidPtr(),                4, "x80",            "set from MachineData during init");
        // Vulnerability sub-struct at 0x88
        s.replaceAtOffset(0x88, IntegerDataType.dataType, 4, "vuln_kind",      "0=vulnerable, 2=intangible");
        s.replaceAtOffset(0x8c, voidPtr(),                4, "vuln_on_damage", "fn ptr: void (*)(void *, void *)");
        s.replaceAtOffset(0x90, IntegerDataType.dataType, 4, "vuln_x90",       null);
        s.replaceAtOffset(0x94, IntegerDataType.dataType, 4, "intang_timer",   "intangibility countdown (frames)");
        s.replaceAtOffset(0x98, IntegerDataType.dataType, 4, "invuln_timer",   "invulnerability countdown (frames)");
        return s;
    }

    private DataType makeHitCollData(DataType hurtData) {
        // Global hit-collision result table. 0x244 bytes. Applied at stc_hitcolldata.
        // log[20]: each entry is 0x1c bytes (HurtData* + void* + void* + Vec3(12) + float(4)).
        // The log array (0x230 bytes) is left as raw bytes here; promote the field to
        // a typed HitCollEntry[20] array once HitCollEntry is added to this script.
        StructureDataType s = makeStruct("HitCollData", 0x244);
        s.replaceAtOffset(0x00, IntegerDataType.dataType, 4, "x0",       null);
        s.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "x4",       null);
        s.replaceAtOffset(0x08, ByteDataType.dataType,    1, "x8",       null);
        s.replaceAtOffset(0x09, ByteDataType.dataType,    1, "x9",       null);
        s.replaceAtOffset(0x0a, ByteDataType.dataType,    1, "xa",       null);
        s.replaceAtOffset(0x0b, ByteDataType.dataType,    1, "xb",       null);
        s.replaceAtOffset(0x0c, arr(ByteDataType.dataType, 0x230), 0x230,
                          "log",       "HitCollEntry[20] — 0x1c bytes each: HurtData*, void*, void*, Vec3, float");
        s.replaceAtOffset(0x23c, IntegerDataType.dataType, 4, "coll_num",  "number of valid entries in log[]");
        s.replaceAtOffset(0x240, ptr(hurtData),            4, "hurt_data", "HurtData* currently being tested for hits");
        return s;
    }

    // -------------------------------------------------------------------------
    //  SCENE (scene.h)
    // -------------------------------------------------------------------------

    private DataType makeMajorSceneDesc() {
        // Static entry in the major scene table. 0x0C bytes.
        StructureDataType s = makeStruct("MajorSceneDesc", 0x0c);
        s.replaceAtOffset(0x00, ByteDataType.dataType, 1, "major_id",        "MajorKind — this scene's ID");
        s.replaceAtOffset(0x01, ByteDataType.dataType, 1, "next_major_id",   "MajorKind — default transition target");
        s.replaceAtOffset(0x02, ByteDataType.dataType, 1, "initial_minor_id","MinorKind — first minor scene");
        // 0x03: implicit padding
        s.replaceAtOffset(0x04, voidPtr(), 4, "cb_Enter",     "fn: void (*)(void)");
        s.replaceAtOffset(0x08, voidPtr(), 4, "cb_ExitMinor", "fn: void (*)(void)");
        return s;
    }

    private DataType makeMinorSceneDesc() {
        // Static entry in the minor scene table. 0x24 bytes.
        StructureDataType s = makeStruct("MinorSceneDesc", 0x24);
        s.replaceAtOffset(0x00, SignedByteDataType.dataType, 1, "idx", "MinorKind — this scene's ID (-1 = terminator)");
        s.replaceAtOffset(0x01, SignedByteDataType.dataType, 1, "x1",  null);
        s.replaceAtOffset(0x02, ByteDataType.dataType,       1, "x2",  null);
        // 0x03: implicit padding
        s.replaceAtOffset(0x04, voidPtr(), 4, "cb_Load",                "fn: void (*)(void) — load resources");
        s.replaceAtOffset(0x08, voidPtr(), 4, "cb_Exit",                "fn: void (*)(void *)");
        s.replaceAtOffset(0x0c, voidPtr(), 4, "cb_ThinkPreGObjProc",    "fn: called first each frame");
        s.replaceAtOffset(0x10, voidPtr(), 4, "cb_ThinkPostGObjProc",   "fn: called after GObj proc update");
        s.replaceAtOffset(0x14, voidPtr(), 4, "cb_ThinkPostGObjProc2",  "fn: called immediately after previous");
        s.replaceAtOffset(0x18, voidPtr(), 4, "cb_ThinkPreRender",      "fn: called before rendering");
        s.replaceAtOffset(0x1c, voidPtr(), 4, "cb_ThinkPostRender",     "fn: called after all rendering");
        s.replaceAtOffset(0x20, IntegerDataType.dataType, 4, "preload_kind", "copied to Preload::kind on load");
        return s;
    }

    // -------------------------------------------------------------------------
    //  STAGE (stage.h)
    // -------------------------------------------------------------------------

    private DataType makeGrObj() {
        // Per-stage runtime object. Pointed to by stc_grobj_ptr. 0x0C bytes.
        StructureDataType s = makeStruct("GrObj", 0x0c);
        s.replaceAtOffset(0x00, voidPtr(),                4, "gobj",    "GOBJ* — stage game object");
        s.replaceAtOffset(0x04, IntegerDataType.dataType, 4, "gr_kind", "GroundKind enum");
        s.replaceAtOffset(0x08, voidPtr(),                4, "gr_data", "GrData* — stage attribute/animation data");
        return s;
    }

    // -------------------------------------------------------------------------
    //  PRELOAD (preload.h)
    // -------------------------------------------------------------------------

    private DataType makePreloadEntry() {
        // One file entry in the Preload table (Preload.entry[80]). 0x28 bytes.
        StructureDataType s = makeStruct("PreloadEntry", 0x28);
        s.replaceAtOffset(0x00, ByteDataType.dataType,          1, "load_state",     "0=none 1=queued 2=loading 3=loaded 4=persistent");
        s.replaceAtOffset(0x01, ByteDataType.dataType,          1, "file_kind",      "PreloadFileKind enum");
        s.replaceAtOffset(0x02, ByteDataType.dataType,          1, "heap_kind",      "PreloadHeapKind enum");
        s.replaceAtOffset(0x03, ByteDataType.dataType,          1, "init_kind",      "PreloadEntryInitKind enum");
        s.replaceAtOffset(0x04, ByteDataType.dataType,          1, "flags",          "purpose flags");
        // 0x05: pad
        s.replaceAtOffset(0x06, UnsignedShortDataType.dataType, 2, "file_entry_num", "FST entry number; >2000 = special alloc");
        s.replaceAtOffset(0x08, ShortDataType.dataType,         2, "status",         "-9999=unload pending, 9999=needed");
        // 0x0a-0x0b: pad
        s.replaceAtOffset(0x0c, IntegerDataType.dataType,       4, "file_size",      null);
        s.replaceAtOffset(0x10, voidPtr(),                      4, "file_data",      "PreloadAllocData* — file allocation");
        s.replaceAtOffset(0x14, voidPtr(),                      4, "header_data",    "PreloadAllocData* — header allocation");
        s.replaceAtOffset(0x18, IntegerDataType.dataType,       4, "x18",            null);
        s.replaceAtOffset(0x1c, IntegerDataType.dataType,       4, "x1c",            null);
        s.replaceAtOffset(0x20, IntegerDataType.dataType,       4, "x20",            null);
        s.replaceAtOffset(0x24, IntegerDataType.dataType,       4, "x24",            null);
        return s;
    }

    // -------------------------------------------------------------------------
    //  GAME DATA (game.h)
    // -------------------------------------------------------------------------

    private DataType makeGameData(DataType hsdUpdate) {
        // Global game state. Static instance at 0x805359D8.
        // Size 0x1518 covers all confirmed fields; actual size may be slightly larger.
        // Many gaps remain unknown — documented as xNNN placeholders in game.h.
        //
        // Large embedded sub-structs (inlined at their absolute offsets below):
        //   0x030: MainMenuData         (~0x20 bytes)
        //   0x108: airride_select_ply   (to 0x160)
        //   0x1D0: city_select_ply      (to 0x25C)
        //   0x394: city                 (to 0x5B0) — key fields inlined below
        //   0xAC8: PlayerDesc[4]        (0xC0 bytes raw; PlayerDesc not yet defined)
        //   0xBB8: ply_view_desc[4]     (0x0C bytes: s8 ply, s8 flag, s8 x2 each)
        //   0xBC8: StadiumResults       (0x5C bytes raw)
        //
        // GameClearData instances are applied separately via applyType() to avoid
        // conflict with the large GameData annotation:
        //   GameData+0xD68 = AirRide, +0xE80 = TopRide, +0xFA8 = CityTrial
        StructureDataType s = makeStruct("GameData", 0x1518);

        // Air Ride mode setting
        s.replaceAtOffset(0x35D, ByteDataType.dataType, 1, "airride_mode", "AirRideMode enum");

        // City Trial settings (fields within the embedded city sub-struct, 0x394–0x5AF)
        s.replaceAtOffset(0x394, UnsignedShortDataType.dataType,    2, "city_time_seconds",           "match duration in seconds");
        s.replaceAtOffset(0x396, ByteDataType.dataType,             1, "city_menu_stadium_selection",  "stadium option index (subtract 1 = StadiumGroup)");
        s.replaceAtOffset(0x397, ByteDataType.dataType,             1, "city_flags",                  "game_tempo:2, events_enable:1, (see game.h bitfield)");
        s.replaceAtOffset(0x399, ByteDataType.dataType,             1, "city_mode",                   "CityMode enum (trial/stadium/freerun)");
        s.replaceAtOffset(0x39B, ByteDataType.dataType,             1, "city_scene",
                          "3=select, 4=ingame, 5=graph, 6=stadium_splash, 7=stadium, 8=results");
        s.replaceAtOffset(0x3A0, arr(FloatDataType.dataType, 45), 180, "city_ply_stats",
                          "float[5][9] — per-player City Trial stats (same 9-float layout as RiderData.stats)");
        s.replaceAtOffset(0x454, arr(ByteDataType.dataType, 5),     5, "city_is_bike",               "u8[5] — per-player is_bike flag");
        s.replaceAtOffset(0x459, arr(ByteDataType.dataType, 5),     5, "city_machine_kind",           "u8[5] — per-player MachineKind");
        s.replaceAtOffset(0x45E, arr(ByteDataType.dataType, 5),     5, "city_prev_stadium_kind",      "u8[5] — previous StadiumKind per player");
        s.replaceAtOffset(0x5AD, ByteDataType.dataType,             1, "city_stadium_kind",           "current StadiumKind");
        s.replaceAtOffset(0x5AF, ByteDataType.dataType,             1, "city_stadium_round",          "current stadium round number");

        // Scene management
        s.replaceAtOffset(0x7D4, ByteDataType.dataType, 1, "major_cur",           "MajorKind — currently active major scene");
        s.replaceAtOffset(0x7D5, ByteDataType.dataType, 1, "major_pending",       "MajorKind — pending scene transition target");
        s.replaceAtOffset(0x7D6, ByteDataType.dataType, 1, "request_major_exit",  "nonzero to trigger scene transition");

        // Embedded HSD engine state
        s.replaceAtOffset(0x7E0, hsdUpdate, 0x34, "update",
                          "HSD_Update — engine pause/frame counter state (also at stc_hsd_update)");

        // Pause menu
        s.replaceAtOffset(0x830, ByteDataType.dataType,    1, "pause_ply",       "player slot who paused (0–3)");
        s.replaceAtOffset(0x831, ByteDataType.dataType,    1, "pause_cursor",    "0=resume, 1=restart, 2=exit");
        s.replaceAtOffset(0x832, ByteDataType.dataType,    1, "intro_state",     null);
        s.replaceAtOffset(0x833, ByteDataType.dataType,    1, "frames_in_second",null);
        s.replaceAtOffset(0x834, IntegerDataType.dataType, 4, "seconds_passed",  null);
        s.replaceAtOffset(0x838, IntegerDataType.dataType, 4, "pause_delay",     null);

        // Stadium game results
        s.replaceAtOffset(0xA38, arr(IntegerDataType.dataType, 4), 16, "destruction_derby_ko_num",
                          "int[4] — KO count per player in Destruction Derby");

        // Match configuration (outside city sub-struct, applies to all modes)
        s.replaceAtOffset(0xA94, ByteDataType.dataType,          1, "city_kind",
                          "5=city trial; stadium modes derived here (0xE = Destruction Derby)");
        s.replaceAtOffset(0xA96, ByteDataType.dataType,          1, "view_num",            "number of screen viewports");
        s.replaceAtOffset(0xA97, ByteDataType.dataType,          1, "stage_kind",          "StageKind enum");
        s.replaceAtOffset(0xA98, ByteDataType.dataType,          1, "bgm_override",        "when not 1, plays this value as song ID");
        s.replaceAtOffset(0xA99, ByteDataType.dataType,          1, "is_always_ura_bgm",   null);
        s.replaceAtOffset(0xA9C, UnsignedShortDataType.dataType, 2, "time_seconds",        "match timer in seconds");
        s.replaceAtOffset(0xAA0, IntegerDataType.dataType,       4, "rng_seed_initial",    "RNG seed at match start");
        s.replaceAtOffset(0xAA6, ByteDataType.dataType,          1, "flags_aa6",
                          "tempo:2, xaa6_20:1 (always on), xaa6_10:1 (always on), xaa6_08:1, xaa6_04:1, xaa6_02:1, xaa6_01:1");
        s.replaceAtOffset(0xAA7, ByteDataType.dataType,          1, "flags_aa7",
                          "xaa7_80:1, xaa7_40:1, is_play_music:1, is_enable_events:1, is_replay:1, xaa7_04:1, xaa7_02:1, xaa7_01:1");

        // Player descriptors — 0x30 bytes each, 4 entries (PlayerDesc not yet a typed struct)
        s.replaceAtOffset(0xAC8, arr(ByteDataType.dataType, 0xC0), 0xC0, "ply_desc",
                          "PlayerDesc[4] — 0x30 bytes each; define PlayerDesc type when fields are known");

        // ClearChecker
        s.replaceAtOffset(0xED0, voidPtr(), 4, "clearchecker_gobj",
                          "GOBJ* — ClearChecker game object");

        // Checklist icon UI (in region the header still shows as unknown int fields)
        s.replaceAtOffset(0xEF0, arr(voidPtr(), 4), 16, "icon_templates",
                          "HSD_Archive*[4] — checklist icon template archives (types 0–3)");
        s.replaceAtOffset(0xF0C, voidPtr(),         4,  "visual_cells",
                          "GObj*[] base — visual cell array (index * 4 offset from here)");

        // Unlock bitfield cache (built by Checklist_BuildUnlockBitfields at 0x80007AF0)
        s.replaceAtOffset(0xD50, UnsignedIntegerDataType.dataType, 4, "airride_unlock_lo",
                          "Air Ride reward unlock bits 0–31");
        s.replaceAtOffset(0xD54, UnsignedIntegerDataType.dataType, 4, "airride_unlock_hi",
                          "Air Ride reward unlock bits 32–45");
        s.replaceAtOffset(0xD58, UnsignedIntegerDataType.dataType, 4, "citytrial_unlock_lo",
                          "City Trial reward unlock bits 0–31");
        s.replaceAtOffset(0xD5C, UnsignedIntegerDataType.dataType, 4, "citytrial_unlock_hi",
                          "City Trial reward unlock bits 32–43");
        s.replaceAtOffset(0xD60, UnsignedIntegerDataType.dataType, 4, "stadium_flags_cache",
                          "cached copy of StadiumFlags (from GameData+0x1510)");

        // Stadium unlock flags (beyond current game.h documentation, from stadium.h)
        s.replaceAtOffset(0x1510, UnsignedIntegerDataType.dataType, 4, "StadiumFlags",
                          "u32 — stadium unlock flags; bit N = StadiumKind N is unlocked (24-bit mask)");
        s.replaceAtOffset(0x1514, UnsignedIntegerDataType.dataType, 4, "StadiumNewLabel",
                          "u32 — stadium new-label pending flags");
        return s;
    }

    private DataType makeGameClearData() {
        // Per-mode checklist/reward state. 0xF4 (244) bytes.
        // Applied at: GameData_AirRideClear (+0xD68), TopRideClear (+0xE80), CityTrialClear (+0xFA8).
        //
        // clear[clear_kind] bitfield layout (each byte):
        //   bit7=x0_80  bit6=x0_40   bit5=x0_20    bit4=is_visible
        //   bit3=has_reward  bit2=is_unlocked  bit1=is_filler  bit0=is_new
        StructureDataType s = makeStruct("GameClearData", 0xF4);
        s.replaceAtOffset(0x00, ByteDataType.dataType, 1, "new_unlock_flag",
                          "nonzero when new unlocks are pending visual update");
        s.replaceAtOffset(0x01, ByteDataType.dataType, 1, "x1", null);
        s.replaceAtOffset(0x02, ByteDataType.dataType, 1, "checkbox_filler_num",
                          "number of filler checkboxes currently available");
        s.replaceAtOffset(0x03, ByteDataType.dataType, 1, "checkbox_filler_list_len",
                          "length of filler list shown in checklist UI (max 5)");
        s.replaceAtOffset(0x04, arr(ByteDataType.dataType, 120), 120,
                          "grid_mapping",
                          "grid_mapping[clear_kind] = visual cell position (0-119); col=pos%12, row=pos/12");
        s.replaceAtOffset(0x7C, arr(ByteDataType.dataType, 120), 120,
                          "clear",
                          "clear[clear_kind]: bit4=is_visible, bit3=has_reward, bit2=is_unlocked, bit1=is_filler, bit0=is_new");
        return s;
    }

    private DataType makePlayerData() {
        // Per-player runtime state. Static array of 4 at stc_playerdata (0x8055A9F0).
        // Total size: 0x90C bytes.
        StructureDataType s = makeStruct("PlayerData", 0x90C);
        // 0x00-0x33: unknown
        s.replaceAtOffset(0x34, FloatDataType.dataType,          4, "hp",                 "current HP");
        s.replaceAtOffset(0x38, FloatDataType.dataType,          4, "max_hp",             "maximum HP");
        s.replaceAtOffset(0x3c, voidPtr(),                       4, "rider_gobj",         "GOBJ* — rider game object");
        s.replaceAtOffset(0x40, voidPtr(),                       4, "machine_gobj",       "GOBJ* — machine game object");
        // stats union at 0x44 (float[9], same layout as RiderData.stats)
        s.replaceAtOffset(0x44, FloatDataType.dataType,          4, "stat_weight",        null);
        s.replaceAtOffset(0x48, FloatDataType.dataType,          4, "stat_boost",         null);
        s.replaceAtOffset(0x4c, FloatDataType.dataType,          4, "stat_top_speed",     null);
        s.replaceAtOffset(0x50, FloatDataType.dataType,          4, "stat_turn",          null);
        s.replaceAtOffset(0x54, FloatDataType.dataType,          4, "stat_charge",        null);
        s.replaceAtOffset(0x58, FloatDataType.dataType,          4, "stat_glide",         null);
        s.replaceAtOffset(0x5c, FloatDataType.dataType,          4, "stat_offense",       null);
        s.replaceAtOffset(0x60, FloatDataType.dataType,          4, "stat_defense",       null);
        s.replaceAtOffset(0x64, FloatDataType.dataType,          4, "stat_hp",            null);
        // 0x68-0x8AF: unknown
        s.replaceAtOffset(0x8B0, UnsignedIntegerDataType.dataType, 4, "objects_destroyed_num",
                          "running count of breakable objects destroyed (star poles, rocks, houses, etc.)");
        // 0x8B4-0x907: unknown
        // 0x908: u16 bitfield for legendary machine piece collection
        s.replaceAtOffset(0x908, UnsignedShortDataType.dataType, 2, "legendary_flags",
                          "hydra_piece[2:0]=bits12-10, dragoon_piece[2:0]=bits9-7");
        // 0x90A-0x90B: pad
        return s;
    }

    // -------------------------------------------------------------------------
    //  MACHINE SPAWN (machine.h)
    // -------------------------------------------------------------------------

    private DataType makeMachineSpawnData(DataType vec3) {
        // City Trial machine spawner state. Pointed to by stc_machinespawn_gobj. 0xC8 bytes.
        StructureDataType s = makeStruct("MachineSpawnData", 0xC8);
        s.replaceAtOffset(0x00, voidPtr(),                     4, "gobj",                "GOBJ* — spawner game object");
        s.replaceAtOffset(0x04, IntegerDataType.dataType,      4, "total_match_frames",  "total frames in match");
        s.replaceAtOffset(0x08, IntegerDataType.dataType,      4, "spawn_timer",         "countdown to next spawn (frames)");
        s.replaceAtOffset(0x0c, IntegerDataType.dataType,      4, "vehicle_max",         "will not spawn if cur count exceeds this");
        s.replaceAtOffset(0x10, UnsignedShortDataType.dataType,2, "vehicle_cur_num",     "currently existing machine count");
        s.replaceAtOffset(0x12, UnsignedShortDataType.dataType,2, "vehicle_req_num",     "target machine count");
        s.replaceAtOffset(0x14, IntegerDataType.dataType,      4, "vehicle_pos_num",     null);
        s.replaceAtOffset(0x18, IntegerDataType.dataType,      4, "vehicle_area_pos_num",null);
        s.replaceAtOffset(0x1c, IntegerDataType.dataType,      4, "x1c",                 null);
        s.replaceAtOffset(0x20, arr(vec3, 4),                 48, "machineformation_pos",
                          "Vec3[4] — spawn positions for formation event");
        s.replaceAtOffset(0x50, arr(ByteDataType.dataType, 4), 4, "prev_machine_kind",
                          "u8[4] — circular buffer of last 4 spawned MachineKinds");
        s.replaceAtOffset(0x54, IntegerDataType.dataType,      4, "prev_machine_index",  "write index into prev_machine_kind[]");
        s.replaceAtOffset(0xc4, ByteDataType.dataType,         1, "xc4",                 null);
        s.replaceAtOffset(0xc5, ByteDataType.dataType,         1, "xc5",                 null);
        s.replaceAtOffset(0xc6, ByteDataType.dataType,         1, "spawn_flags",
                          "0x80=spawning_done, 0x40=formation_queued, 0x20=formation_spawning");
        return s;
    }

    // -------------------------------------------------------------------------
    //  RIDER DATA (rider.h)
    // -------------------------------------------------------------------------

    private DataType makeRiderData(DataType vec3, DataType hurtData) {
        // Per-rider runtime state. Dynamically allocated — no static base address.
        // Size based on last documented field (0xAC0 + 4). Only key named fields are
        // defined here; add more via replaceAtOffset() as they are reverse engineered.
        StructureDataType s = makeStruct("RiderData", 0xAC4);
        s.replaceAtOffset(0x00, IntegerDataType.dataType,    4, "x0",                   null);
        s.replaceAtOffset(0x04, IntegerDataType.dataType,    4, "kind",                 "RiderKind enum");
        s.replaceAtOffset(0x08, ByteDataType.dataType,       1, "ply",                  "player slot index (0-3)");
        s.replaceAtOffset(0x09, ByteDataType.dataType,       1, "x9",                   null);
        s.replaceAtOffset(0x0a, ByteDataType.dataType,       1, "color_idx",            "color palette index");
        s.replaceAtOffset(0x0b, ByteDataType.dataType,       1, "xb",                   null);
        s.replaceAtOffset(0x0c, ByteDataType.dataType,       1, "starting_machine_idx", "MachineKind at match start");
        s.replaceAtOffset(0x18, voidPtr(),                   4, "rdDataKirby",          "rdDataKirby* — Kirby-specific data");
        s.replaceAtOffset(0x1c, IntegerDataType.dataType,    4, "state_idx",            "current FSM state index");
        s.replaceAtOffset(0x24, IntegerDataType.dataType,    4, "state_frame",          "frames spent in current state");
        s.replaceAtOffset(0x2dc, vec3,                      12, "self_vel",             "self-driven velocity vector");
        s.replaceAtOffset(0x300, vec3,                      12, "pos",                  "world position");
        s.replaceAtOffset(0x324, vec3,                      12, "forward",              "forward movement direction");
        s.replaceAtOffset(0x330, vec3,                      12, "up",                   "up vector");
        s.replaceAtOffset(0x348, FloatDataType.dataType,     4, "model_scale",          "Kirby model display scale");
        s.replaceAtOffset(0x390, ptr(hurtData),              4, "hurt_data",            "HurtData* — this rider's hurtbox");
        // Input sub-struct (0x394-0x3F4)
        s.replaceAtOffset(0x394, FloatDataType.dataType,     4, "lstick_x",             "left stick X (float)");
        s.replaceAtOffset(0x398, FloatDataType.dataType,     4, "lstick_y",             "left stick Y (float)");
        s.replaceAtOffset(0x3d8, IntegerDataType.dataType,   4, "input_held",           "held buttons bitmask");
        s.replaceAtOffset(0x3e4, IntegerDataType.dataType,   4, "input_down",           "newly pressed buttons bitmask");
        s.replaceAtOffset(0x3ec, SignedByteDataType.dataType, 1,"stickX_replay",        "stick X as byte (replay recording)");
        s.replaceAtOffset(0x3ed, SignedByteDataType.dataType, 1,"stickY_replay",        "stick Y as byte (replay recording)");
        s.replaceAtOffset(0x3f4, voidPtr(),                  4, "machine_gobj",         "GOBJ* — machine this rider occupies");
        s.replaceAtOffset(0x454, IntegerDataType.dataType,   4, "copy_kind",            "CopyKind enum — current copy ability");
        s.replaceAtOffset(0x45c, IntegerDataType.dataType,   4, "powerup_kind",         "PowerUpKind enum");
        s.replaceAtOffset(0x588, IntegerDataType.dataType,   4, "candy_duration",       "frames of candy invincibility remaining");
        s.replaceAtOffset(0x66c, voidPtr(),                  4, "shadow_gobj",          "GOBJ* — circular shadow object");
        // stats union at 0x74c (same 9-float layout as PlayerData)
        s.replaceAtOffset(0x74c, FloatDataType.dataType,     4, "stat_weight",          null);
        s.replaceAtOffset(0x750, FloatDataType.dataType,     4, "stat_boost",           null);
        s.replaceAtOffset(0x754, FloatDataType.dataType,     4, "stat_top_speed",       null);
        s.replaceAtOffset(0x758, FloatDataType.dataType,     4, "stat_turn",            null);
        s.replaceAtOffset(0x75c, FloatDataType.dataType,     4, "stat_charge",          null);
        s.replaceAtOffset(0x760, FloatDataType.dataType,     4, "stat_glide",           null);
        s.replaceAtOffset(0x764, FloatDataType.dataType,     4, "stat_offense",         null);
        s.replaceAtOffset(0x768, FloatDataType.dataType,     4, "stat_defense",         null);
        s.replaceAtOffset(0x76c, FloatDataType.dataType,     4, "stat_hp",              null);
        // State callbacks at 0x7B4
        s.replaceAtOffset(0x7b4, voidPtr(), 4, "cb_anim", "fn: animation step");
        s.replaceAtOffset(0x7b8, voidPtr(), 4, "cb_iasa", "fn: interruptible-as-soon-as");
        s.replaceAtOffset(0x7bc, voidPtr(), 4, "cb_phys", "fn: physics step");
        s.replaceAtOffset(0x7c0, voidPtr(), 4, "cb_coll", "fn: collision step");
        s.replaceAtOffset(0x91c, IntegerDataType.dataType,   4, "copy_timer",           "frames remaining on copy ability");
        s.replaceAtOffset(0x9c8, IntegerDataType.dataType,   4, "jumps_used",           "jump count (resets on landing)");
        s.replaceAtOffset(0xac0, IntegerDataType.dataType,   4, "xac0",                 null);
        return s;
    }

    // -------------------------------------------------------------------------
    //  AP MOD TYPES (mods/archipelago/src/main.h — KARchipelago)
    // -------------------------------------------------------------------------

    private DataType makeAPSlotOptions() {
        // Slot options written by the Python AP client on connection. 14 × u32 = 0x38 bytes.
        StructureDataType s = makeStruct("APSlotOptions", 0x38);
        // General
        s.replaceAtOffset(0x00, UnsignedIntegerDataType.dataType, 4, "death_link",                     "0 or 1");
        s.replaceAtOffset(0x04, UnsignedIntegerDataType.dataType, 4, "energy_link",                    "0 or 1");
        s.replaceAtOffset(0x08, UnsignedIntegerDataType.dataType, 4, "traplink",                       "0 or 1");
        s.replaceAtOffset(0x0c, UnsignedIntegerDataType.dataType, 4, "reveal_checklists",              "0 or 1");
        // City Trial
        s.replaceAtOffset(0x10, UnsignedIntegerDataType.dataType, 4, "city_trial_goal",                "GoalKind enum");
        s.replaceAtOffset(0x14, UnsignedIntegerDataType.dataType, 4, "city_trial_checklist_amount",    "1-120");
        s.replaceAtOffset(0x18, UnsignedIntegerDataType.dataType, 4, "city_trial_permanent_patches",   "0 or 1");
        s.replaceAtOffset(0x1c, UnsignedIntegerDataType.dataType, 4, "city_trial_progressive_patch_caps", "0 or 1");
        s.replaceAtOffset(0x20, UnsignedIntegerDataType.dataType, 4, "city_trial_patch_cap_amount",    "1-17");
        s.replaceAtOffset(0x24, UnsignedIntegerDataType.dataType, 4, "city_trial_progressive_stadiums","0 or 1");
        // Air Ride
        s.replaceAtOffset(0x28, UnsignedIntegerDataType.dataType, 4, "air_ride_goal",                  "GoalKind enum");
        s.replaceAtOffset(0x2c, UnsignedIntegerDataType.dataType, 4, "air_ride_checklist_amount",      "1-120");
        // Top Ride
        s.replaceAtOffset(0x30, UnsignedIntegerDataType.dataType, 4, "top_ride_goal",                  "GoalKind enum");
        s.replaceAtOffset(0x34, UnsignedIntegerDataType.dataType, 4, "top_ride_checklist_amount",      "1-120");
        return s;
    }

    private DataType makeTextBoxMessage(DataType vec2, DataType vec3) {
        // One entry in ArchipelagoData's textbox FIFO queue. 0x11C bytes.
        //   char message[256]  = 0x100 bytes at 0x00
        //   uint lifetime      = 4 bytes  at 0x100
        //   Vec3 pos           = 12 bytes at 0x104
        //   Vec2 scale         = 8 bytes  at 0x110
        //   Text* text         = 4 bytes  at 0x118
        StructureDataType s = makeStruct("TextBoxMessage", 0x11C);
        s.replaceAtOffset(0x000, arr(ByteDataType.dataType, 256), 256, "message", "null-terminated string (256 bytes)");
        s.replaceAtOffset(0x100, UnsignedIntegerDataType.dataType,  4, "lifetime","display duration in frames");
        s.replaceAtOffset(0x104, vec3,                             12, "pos",     "world-space position");
        s.replaceAtOffset(0x110, vec2,                              8, "scale",   "text scale X/Y");
        s.replaceAtOffset(0x118, voidPtr(),                         4, "text",    "Text* — game text object");
        return s;
    }

    private DataType makeArchipelagoData(DataType textBoxMessage, DataType apSlotOptions) {
        // Shared memory block between the game and the Python AP client.
        // Heap-allocated at boot; pointer stored at stc_ap_data_ptr (0x805D52D4).
        // Total size: 0x714 bytes.
        //
        // textbox_queue layout (6 entries × 0x11C bytes = 0x6A8 bytes at 0x20):
        //   0x20 + 6*0x11C = 0x20 + 0x6A8 = 0x6C8
        StructureDataType s = makeStruct("ArchipelagoData", 0x714);
        s.replaceAtOffset(0x00, FloatDataType.dataType,            4, "energy_give",          "EnergyLink: energy to give to pool (written by game)");
        s.replaceAtOffset(0x04, FloatDataType.dataType,            4, "energy_receive",       "EnergyLink: energy to receive from pool (written by client)");
        s.replaceAtOffset(0x08, UnsignedIntegerDataType.dataType,  4, "deathlink_receive",    "DeathLink: client writes 1 to kill player");
        s.replaceAtOffset(0x0c, UnsignedIntegerDataType.dataType,  4, "deathlink_send",       "DeathLink: game writes 1 when player dies");
        s.replaceAtOffset(0x10, UnsignedIntegerDataType.dataType,  4, "traplink_receive",     "TrapLink: client writes 1 to trigger trap");
        s.replaceAtOffset(0x14, UnsignedIntegerDataType.dataType,  4, "traplink_send",        "TrapLink: game writes 1 when trap triggered");
        s.replaceAtOffset(0x18, UnsignedIntegerDataType.dataType,  4, "incoming_item_id",     "Mailbox: client writes APItemId, game reads and clears to 0");
        s.replaceAtOffset(0x1c, UnsignedIntegerDataType.dataType,  4, "item_received_index",  "Mirror of save_data->item_received_count for client to read");
        s.replaceAtOffset(0x20, arr(textBoxMessage, 6),        6 * 0x11C, "textbox_queue",    "TextBoxMessage[6] — FIFO queue for on-screen messages");
        s.replaceAtOffset(0x6c8, UnsignedIntegerDataType.dataType, 4, "textbox_queue_head",  "dequeue index (read position)");
        s.replaceAtOffset(0x6cc, UnsignedIntegerDataType.dataType, 4, "textbox_queue_tail",  "enqueue index (write position)");
        s.replaceAtOffset(0x6d0, UnsignedIntegerDataType.dataType, 4, "textbox_framecounter","frame counter for textbox fade timing");
        s.replaceAtOffset(0x6d4, UnsignedIntegerDataType.dataType, 4, "game_ready",          "game sets 1 after save loaded and mod initialized");
        s.replaceAtOffset(0x6d8, UnsignedIntegerDataType.dataType, 4, "options_valid",       "client sets 1 after writing all options fields");
        s.replaceAtOffset(0x6dc, apSlotOptions,               0x38, "options",               "APSlotOptions from AP server");
        return s;
    }

    // -------------------------------------------------------------------------
    //  MACHINE DATA (machine.h)
    // -------------------------------------------------------------------------

    private DataType makeMachineData(DataType vec3, DataType hurtData) {
        // Per-machine runtime state. Dynamically allocated — no static base address.
        // Size based on last documented field (0x674 + 4). Add more fields via
        // replaceAtOffset() as they are discovered.
        StructureDataType s = makeStruct("MachineData", 0x678);
        s.replaceAtOffset(0x00, voidPtr(),                  4, "gobj",             "GOBJ* — this machine's game object");
        s.replaceAtOffset(0x04, voidPtr(),                  4, "rider_gobj",       "GOBJ* — rider currently on this machine");
        s.replaceAtOffset(0x08, voidPtr(),                  4, "rider_unk1",       "GOBJ* — unk rider-related object");
        s.replaceAtOffset(0x0c, voidPtr(),                  4, "rider_unk2",       "GOBJ* — unk rider-related object");
        s.replaceAtOffset(0x10, IntegerDataType.dataType,   4, "is_bike",          "nonzero if this is a wheel/bike");
        s.replaceAtOffset(0x24, ByteDataType.dataType,      1, "kind",             "MachineKind enum");
        s.replaceAtOffset(0x2c, voidPtr(),                  4, "vcData",           "vcData* — vehicle config/ROM data");
        s.replaceAtOffset(0x310, FloatDataType.dataType,    4, "model_scale",      "visual model scale");
        s.replaceAtOffset(0x3e8, vec3,                     12, "pos",              "world position");
        s.replaceAtOffset(0x418, vec3,                     12, "forward",          "forward / velocity direction");
        s.replaceAtOffset(0x424, vec3,                     12, "up",               "up vector");
        s.replaceAtOffset(0x4cc, FloatDataType.dataType,    4, "hp_max",           "maximum HP");
        s.replaceAtOffset(0x4f0, FloatDataType.dataType,    4, "top_speed_ground", "top speed on ground surface");
        s.replaceAtOffset(0x4fc, FloatDataType.dataType,    4, "base_charge_rate", "charge rate (scaled by stat patches)");
        s.replaceAtOffset(0x660, ptr(hurtData),             4, "hurt_data",        "HurtData* — this machine's hurtbox");
        // Input sub-struct at 0x664
        s.replaceAtOffset(0x664, FloatDataType.dataType,    4, "input_stick_x",   "left stick X (float)");
        s.replaceAtOffset(0x668, FloatDataType.dataType,    4, "input_stick_y",   "left stick Y (float)");
        s.replaceAtOffset(0x66c, IntegerDataType.dataType,  4, "input_buttons",   "held button bitmask");
        s.replaceAtOffset(0x670, ByteDataType.dataType,     1, "tilt_timer_x",    "tilt input timer X");
        s.replaceAtOffset(0x671, ByteDataType.dataType,     1, "tilt_timer_y",    "tilt input timer Y");
        s.replaceAtOffset(0x674, voidPtr(),                 4, "shadow_gobj",     "GOBJ* — circular shadow object");
        return s;
    }
}
