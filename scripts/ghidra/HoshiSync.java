// Bridge-side half of the hoshi -> Ghidra sync, driven by sync.py.
//
// Two tasks, selected by the `task` key:
//   parse    run Ghidra's C parser over the body-stripped hoshi headers into the
//            program's DataTypeManager, then drop orphaned CParser conflicts
//   globals  retype + label each fixed-address engine global in the listing
//
// Config comes from a properties file rather than script arguments, and results
// go to a report file rather than stdout, because the ghidra-cli bridge forwards
// neither. Config path: ~/.config/ghidra-cli/hoshi_sync.cfg
//   task     = parse | globals
//   report   = /abs/path/report.txt
//   includes = /path/a,/path/b        (parse)
//   files    = /path/master.h         (parse)
//   defines  = -DFOO=1                (parse, optional)
//   data     = /abs/path/globals.tsv  (globals)
//
// The bridge wraps each script in its own transaction and never saves; edits
// commit to the in-memory program when the script ends. sync.py persists them
// with `ghidra program close`, which flushes the whole program. `ghidra analyze`
// does not reliably write freshly-created listing data back to disk.
//
//@category KAR
import ghidra.app.script.GhidraScript;
import ghidra.app.util.cparser.C.CParserUtils;
import ghidra.app.util.cparser.C.CParserUtils.CParseResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.data.ArrayDataType;
import ghidra.program.model.data.CharDataType;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeConflictHandler;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.DoubleDataType;
import ghidra.program.model.data.FloatDataType;
import ghidra.program.model.data.FunctionDefinition;
import ghidra.program.model.data.IntegerDataType;
import ghidra.program.model.data.Pointer;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.data.TypeDef;
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
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Properties;
import java.util.regex.Pattern;

public class HoshiSync extends GhidraScript {

    // CParser names anonymous inner records `_struct_N` / `_union_N` by parse
    // order, so a re-import cannot match the previous run's numbering and banks
    // a `.conflict` copy of each. Unreferenced ones are pure litter.
    private static final Pattern ANON_CONFLICT =
            Pattern.compile("^_(?:struct|union|enum)_\\d+\\.conflict\\d*$");

    private final StringBuilder report = new StringBuilder();

    @Override
    public void run() throws Exception {
        String cfgPath = System.getProperty("user.home")
                + "/.config/ghidra-cli/hoshi_sync.cfg";
        File cfgFile = new File(cfgPath);
        if (!cfgFile.exists()) {
            println("[HoshiSync] ERROR: config not found: " + cfgPath);
            return;
        }
        Properties cfg = new Properties();
        try (FileInputStream in = new FileInputStream(cfgFile)) {
            cfg.load(in);
        }

        String task = cfg.getProperty("task", "").trim();
        rpt("task    : " + task);
        rpt("program : " + currentProgram.getName());

        String summary;
        if (task.equals("parse")) {
            summary = parse(cfg);
        } else if (task.equals("globals")) {
            summary = globals(cfg);
        } else {
            summary = "ERROR: unknown task '" + task + "'";
            rpt(summary);
        }

        println("[HoshiSync] " + task + ": " + summary);
        String reportPath = cfg.getProperty("report", "").trim();
        if (reportPath.isEmpty()) {
            println(report.toString());
        } else {
            try (FileWriter w = new FileWriter(reportPath)) {
                w.write(report.toString());
            }
        }
    }

    private String parse(Properties cfg) {
        String[] includes = splitCsv(cfg.getProperty("includes", ""));
        String[] files = splitCsv(cfg.getProperty("files", ""));
        String[] defines = splitCsv(cfg.getProperty("defines", ""));

        DataTypeManager dtm = currentProgram.getDataTypeManager();
        int before = dtm.getDataTypeCount(true);
        rpt("includes: " + String.join(" | ", includes));
        rpt("files   : " + String.join(" | ", files));
        if (defines.length > 0) {
            rpt("defines : " + String.join(" ", defines));
        }

        CParseResults res = null;
        Throwable failure = null;
        int tx = currentProgram.startTransaction("Parse hoshi headers");
        try {
            res = CParserUtils.parseHeaderFiles(null, files, includes, defines, dtm, monitor);
        } catch (Throwable t) {
            failure = t;
        } finally {
            currentProgram.endTransaction(tx, true);
        }

        int pruned = pruneOrphanConflicts(dtm);
        int renamed = disambiguateCallbackTypedefs(dtm);
        int after = dtm.getDataTypeCount(true);

        rpt("types   : " + before + " -> " + after + " (delta " + (after - before)
                + ", pruned " + pruned + " orphaned conflicts, resolved " + renamed
                + " shadowed function definitions)");
        if (res != null) {
            rpt("parsed  : successful=" + res.successful());
        }
        if (failure != null) {
            rpt("EXCEPTION: " + failure.getClass().getName() + ": " + failure.getMessage());
        }
        rpt("");
        rpt("--- preprocessor messages ---");
        rpt(res != null && res.cppParseMessages() != null ? res.cppParseMessages() : "(none)");
        rpt("");
        rpt("--- C parse messages ---");
        rpt(res != null && res.cParseMessages() != null ? res.cParseMessages() : "(none)");

        return "delta=" + (after - before) + " pruned=" + pruned + " renamed=" + renamed
                + (res != null ? " successful=" + res.successful() : " FAILED");
    }

    // Removing one conflict can orphan another that only it referenced, so
    // repeat until a pass finds nothing.
    private int pruneOrphanConflicts(DataTypeManager dtm) {
        int total = 0;
        for (int pass = 0; pass < 8; pass++) {
            List<DataType> doomed = new ArrayList<>();
            Iterator<DataType> it = dtm.getAllDataTypes();
            while (it.hasNext()) {
                DataType d = it.next();
                if (ANON_CONFLICT.matcher(d.getName()).matches() && d.getParents().isEmpty()) {
                    doomed.add(d);
                }
            }
            if (doomed.isEmpty()) {
                break;
            }
            int tx = currentProgram.startTransaction("Prune orphaned CParser conflicts");
            try {
                dtm.remove(doomed, monitor);
                total += doomed.size();
            } catch (Exception e) {
                rpt("note    : prune failed: " + e.getClass().getSimpleName()
                        + ": " + e.getMessage());
                return total;
            } finally {
                currentProgram.endTransaction(tx, true);
            }
        }
        return total;
    }

    // The C parser emits a FunctionDefinition beside every function-pointer
    // typedef, under the same name. FunctionSignatureParser resolves parameter
    // types by simple name and gives up when one name matches two types, which
    // makes such a typedef unusable in a prototype. Renaming the definition
    // leaves the typedef as the sole owner of the name.
    //
    // Every parse re-emits the definition and re-points the typedef at the new
    // copy, so the one renamed last time is stale and is dropped first - without
    // that the rename would collide and a `_fn2`, `_fn3` chain would grow.
    private int disambiguateCallbackTypedefs(DataTypeManager dtm) {
        Map<String, DataType> typedefs = new HashMap<>();
        List<DataType> defs = new ArrayList<>();
        Iterator<DataType> it = dtm.getAllDataTypes();
        while (it.hasNext()) {
            DataType d = it.next();
            if (d instanceof TypeDef) {
                typedefs.put(d.getName(), d);
            } else if (d instanceof FunctionDefinition) {
                defs.add(d);
            }
        }
        List<DataType> shadowed = new ArrayList<>();
        for (DataType d : defs) {
            if (typedefs.containsKey(d.getName())) {
                shadowed.add(d);
            }
        }
        if (shadowed.isEmpty()) {
            return 0;
        }
        int touched = 0;
        int tx = currentProgram.startTransaction("Rename shadowed function definitions");
        try {
            for (DataType d : shadowed) {
                String want = d.getName() + "_fn";
                DataType stale = dtm.getDataType(d.getCategoryPath(), want);
                if (stale != null && !sameType(typedefTarget(typedefs.get(d.getName())), stale)) {
                    List<DataType> one = new ArrayList<>();
                    one.add(stale);
                    dtm.remove(one, monitor);
                }
                try {
                    d.setName(want);
                    touched++;
                } catch (Exception e) {
                    rpt("note    : rename failed for " + d.getName() + ": " + e.getMessage());
                }
            }
        } catch (Exception e) {
            rpt("note    : disambiguate failed: " + e.getClass().getSimpleName()
                    + ": " + e.getMessage());
        } finally {
            currentProgram.endTransaction(tx, true);
        }
        return touched;
    }

    private DataType typedefTarget(DataType td) {
        if (!(td instanceof TypeDef)) {
            return null;
        }
        DataType t = ((TypeDef) td).getDataType();
        return (t instanceof Pointer) ? ((Pointer) t).getDataType() : t;
    }

    private boolean sameType(DataType a, DataType b) {
        return a != null && b != null && a.getPathName().equals(b.getPathName());
    }

    // Auto-analysis has already laid down conflicting `undefined` data at these
    // addresses and the bridge exposes no clear/undefine command, so each object
    // is cleared over its span and re-created as the documented hoshi type. That
    // folds `*(int *)(ADDR + off)` blobs in the decompiler into `name.field`.
    private String globals(Properties cfg) throws Exception {
        String dataPath = cfg.getProperty("data", "").trim();
        if (dataPath.isEmpty()) {
            rpt("ERROR: no `data` in config");
            return "ERROR: no data file";
        }
        rpt("data    : " + dataPath);

        DataTypeManager dtm = currentProgram.getDataTypeManager();
        Map<String, DataType> byName = new HashMap<>();
        Iterator<DataType> it = dtm.getAllDataTypes();
        while (it.hasNext()) {
            DataType d = it.next();
            byName.putIfAbsent(d.getName(), d);
        }
        byName.putIfAbsent("void", VoidDataType.dataType);
        byName.putIfAbsent("char", CharDataType.dataType);
        byName.putIfAbsent("int", IntegerDataType.dataType);
        byName.putIfAbsent("float", FloatDataType.dataType);
        byName.putIfAbsent("double", DoubleDataType.dataType);
        byName.putIfAbsent("u8", UnsignedCharDataType.dataType);
        byName.putIfAbsent("u16", UnsignedShortDataType.dataType);
        byName.putIfAbsent("u32", UnsignedIntegerDataType.dataType);
        byName.putIfAbsent("u64", UnsignedLongLongDataType.dataType);

        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();
        SymbolTable st = currentProgram.getSymbolTable();

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
                    rpt("FAIL  malformed line: " + line);
                    failed++;
                    continue;
                }
                String addrStr = f[0], base = f[1], name = f[4];
                int stars = Integer.parseInt(f[2].trim());
                int count = Integer.parseInt(f[3].trim());

                Address addr = toAddr(addrStr);
                String why = null;
                DataType dt = null;
                Address end = null;

                if (addr == null) {
                    why = "bad address";
                } else if (addr.getOffset() >= 0xc0000000L) {
                    why = "MMIO region";
                } else if ((dt = resolve(byName, base, stars, count, dtm)) == null) {
                    why = "unknown type '" + base + "'";
                } else if (dt.getLength() <= 0) {
                    why = "non-sized type " + dt.getName();
                } else if (!mem.contains(addr, end = addr.add(dt.getLength() - 1))) {
                    why = "range not fully in memory";
                } else if (fm.getFunctionsOverlapping(new AddressSet(addr, end)).hasNext()) {
                    why = "overlaps a function (code)";
                }
                if (why != null) {
                    rpt("skip  " + name + " @ " + addrStr + ": " + why);
                    skipped++;
                    continue;
                }

                try {
                    clearListing(addr, end);
                    createData(addr, dt);
                } catch (Exception e) {
                    rpt("FAIL  " + name + " @ " + addrStr + " (" + dt.getName() + "): "
                            + e.getClass().getSimpleName() + ": " + e.getMessage());
                    failed++;
                    continue;
                }
                try {
                    Symbol s = st.createLabel(addr, name, SourceType.USER_DEFINED);
                    s.setPrimary();
                } catch (Exception e) {
                    rpt("note  " + name + " @ " + addrStr + ": typed but label failed ("
                            + e.getMessage() + ")");
                }
                applied++;
            }
        } finally {
            currentProgram.endTransaction(tx, true);
        }

        String summary = "applied=" + applied + " skipped=" + skipped + " failed=" + failed;
        rpt(summary);
        return summary;
    }

    // `base` wrapped in `stars` pointers, as an array of `count` (1 = scalar),
    // resolved into the program's DataTypeManager. Null if base is unknown.
    private DataType resolve(Map<String, DataType> byName, String base, int stars,
                             int count, DataTypeManager dtm) {
        DataType dt = byName.get(base);
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

    private void rpt(String line) {
        report.append(line).append('\n');
    }

    private static String[] splitCsv(String v) {
        List<String> out = new ArrayList<>();
        if (v != null) {
            for (String part : v.split(",")) {
                String t = part.trim();
                if (!t.isEmpty()) {
                    out.add(t);
                }
            }
        }
        return out.toArray(new String[0]);
    }
}
