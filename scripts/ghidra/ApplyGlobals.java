// Type hoshi's fixed-address engine globals in the current program's listing.
// Phase 3 of the type-import workflow (phase 1 = ParseHoshiHeaders.java loads the
// structs; phase 2 = apply_protos.py sets function signatures). Auto-analysis has
// already laid down conflicting `undefined`/pointer data at these addresses and
// ghidra-cli exposes no clear/undefine command, so this script clears the listing
// over each object's span and re-creates it as the documented hoshi type, then
// puts a primary label there. That folds `*(int*)(ADDR + off)` blobs in the
// decompiler into `name.field_off`.
//
// Data file (TSV, path from the config below), one object per line:
//     addr <TAB> base <TAB> pointee_stars <TAB> array_count <TAB> name
// where the object at `addr` is `base` wrapped in `pointee_stars` pointers, as an
// array of `array_count` (1 = a single element). Produced by extract_globals.py.
//
// The ghidra-cli bridge runs each script in its own transaction and does not save;
// changes commit to the in-memory program when the script ends. Persist to disk
// afterward with `ghidra program close` (which flushes the listing). `ghidra
// analyze` does NOT reliably save these freshly-created data definitions -- they
// stay in memory but aren't written on reload -- so use close. (A later analyze
// is harmless once they're saved: it preserves already-defined data.)
// import_globals.sh does the close+open for you.
//
// Config file: ~/.config/ghidra-cli/apply_globals.cfg
//   data   = /abs/path/globals.tsv
//   report = /abs/path/report.txt
//
//@category KAR
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.data.ArrayDataType;
import ghidra.program.model.data.CharDataType;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeConflictHandler;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.DoubleDataType;
import ghidra.program.model.data.FloatDataType;
import ghidra.program.model.data.IntegerDataType;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.data.UnsignedCharDataType;
import ghidra.program.model.data.UnsignedIntegerDataType;
import ghidra.program.model.data.UnsignedLongLongDataType;
import ghidra.program.model.data.UnsignedShortDataType;
import ghidra.program.model.data.VoidDataType;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileReader;
import java.io.FileWriter;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import java.util.Properties;

public class ApplyGlobals extends GhidraScript {

    private final Map<String, DataType> byName = new HashMap<>();
    private final Map<String, DataType> prim = new HashMap<>();

    @Override
    public void run() throws Exception {
        String cfgPath = System.getProperty("user.home") + "/.config/ghidra-cli/apply_globals.cfg";
        File cfgFile = new File(cfgPath);
        if (!cfgFile.exists()) {
            println("[ApplyGlobals] ERROR: config not found: " + cfgPath);
            return;
        }
        Properties cfg = new Properties();
        try (FileInputStream in = new FileInputStream(cfgFile)) {
            cfg.load(in);
        }
        String dataPath = cfg.getProperty("data", "").trim();
        String reportPath = cfg.getProperty("report", "").trim();
        if (dataPath.isEmpty()) {
            println("[ApplyGlobals] ERROR: no `data` in config");
            return;
        }

        DataTypeManager dtm = currentProgram.getDataTypeManager();
        Iterator<DataType> it = dtm.getAllDataTypes();
        while (it.hasNext()) {
            DataType d = it.next();
            byName.putIfAbsent(d.getName(), d);
        }
        prim.put("void", VoidDataType.dataType);
        prim.put("char", CharDataType.dataType);
        prim.put("int", IntegerDataType.dataType);
        prim.put("float", FloatDataType.dataType);
        prim.put("double", DoubleDataType.dataType);
        prim.put("u8", UnsignedCharDataType.dataType);
        prim.put("u16", UnsignedShortDataType.dataType);
        prim.put("u32", UnsignedIntegerDataType.dataType);
        prim.put("u64", UnsignedLongLongDataType.dataType);

        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();
        SymbolTable st = currentProgram.getSymbolTable();

        StringBuilder rpt = new StringBuilder();
        rpt(rpt, "=== ApplyGlobals ===");
        rpt(rpt, "program : " + currentProgram.getName());
        rpt(rpt, "data    : " + dataPath);
        rpt(rpt, "");

        int applied = 0, skipped = 0, failed = 0;

        int tx = currentProgram.startTransaction("Apply hoshi globals");
        try (BufferedReader br = new BufferedReader(new FileReader(dataPath))) {
            String line;
            while ((line = br.readLine()) != null) {
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#")) {
                    continue;
                }
                String[] f = line.split("\t");
                if (f.length < 5) {
                    rpt(rpt, "FAIL  malformed line: " + line);
                    failed++;
                    continue;
                }
                String addrStr = f[0], base = f[1], name = f[4];
                int stars = Integer.parseInt(f[2].trim());
                int count = Integer.parseInt(f[3].trim());

                Address addr = toAddr(addrStr);
                if (addr == null) {
                    rpt(rpt, "FAIL  " + name + " @ " + addrStr + ": bad address");
                    failed++;
                    continue;
                }
                if (addr.getOffset() >= 0xc0000000L) {
                    rpt(rpt, "skip  " + name + " @ " + addrStr + ": MMIO region");
                    skipped++;
                    continue;
                }
                DataType dt = resolve(base, stars, count, dtm);
                if (dt == null) {
                    rpt(rpt, "skip  " + name + " @ " + addrStr + ": unknown type '" + base + "'");
                    skipped++;
                    continue;
                }
                int len = dt.getLength();
                if (len <= 0) {
                    rpt(rpt, "skip  " + name + " @ " + addrStr + ": non-sized type " + dt.getName());
                    skipped++;
                    continue;
                }
                Address end = addr.add(len - 1);
                if (!mem.contains(addr, end)) {
                    rpt(rpt, "skip  " + name + " @ " + addrStr + ": range not fully in memory");
                    skipped++;
                    continue;
                }
                if (fm.getFunctionsOverlapping(new AddressSet(addr, end)).hasNext()) {
                    rpt(rpt, "skip  " + name + " @ " + addrStr + ": overlaps a function (code)");
                    skipped++;
                    continue;
                }

                try {
                    clearListing(addr, end);
                    createData(addr, dt);
                } catch (Exception e) {
                    rpt(rpt, "FAIL  " + name + " @ " + addrStr + " (" + dt.getName() + "): "
                            + e.getClass().getSimpleName() + ": " + e.getMessage());
                    failed++;
                    continue;
                }
                try {
                    Symbol s = st.createLabel(addr, name, SourceType.USER_DEFINED);
                    s.setPrimary();
                } catch (Exception e) {
                    rpt(rpt, "note  " + name + " @ " + addrStr + ": typed but label failed ("
                            + e.getMessage() + ")");
                }
                applied++;
            }
        } finally {
            currentProgram.endTransaction(tx, true);
        }

        rpt(rpt, "");
        rpt(rpt, "applied=" + applied + " skipped=" + skipped + " failed=" + failed);
        rpt(rpt, "");
        rpt(rpt, "NOTE: changes are in-memory only; run `ghidra analyze` to persist to disk.");

        println("[ApplyGlobals] applied=" + applied + " skipped=" + skipped + " failed=" + failed);
        if (!reportPath.isEmpty()) {
            try (FileWriter w = new FileWriter(reportPath)) {
                w.write(rpt.toString());
            }
            println("[ApplyGlobals] report -> " + reportPath);
        } else {
            println(rpt.toString());
        }
    }

    // base type -> wrapped in `stars` pointers -> as array of `count` (1 = scalar),
    // resolved into the program's DataTypeManager. Null if base is unknown.
    private DataType resolve(String base, int stars, int count, DataTypeManager dtm) {
        DataType dt = byName.get(base);
        if (dt == null) {
            dt = prim.get(base);
        }
        if (dt == null) {
            return null;
        }
        for (int i = 0; i < stars; i++) {
            dt = new PointerDataType(dt, dtm);
        }
        if (count > 1) {
            dt = new ArrayDataType(dt, count, dt.getLength());
        }
        return dtm.resolve(dt, DataTypeConflictHandler.DEFAULT_HANDLER);
    }

    private static void rpt(StringBuilder sb, String line) {
        sb.append(line).append('\n');
    }
}
