// Parse hoshi C headers into the current program's DataTypeManager via Ghidra's
// C-Source parser (CParserUtils). Driven by a properties config file at a fixed
// path (the ghidra-cli bridge does not forward `-- ARGS` to runScript). Writes a
// diagnostic report to the configured path.
//
// The ghidra-cli bridge runs each script inside its own (wrapper) transaction, so
// this script cannot save the program itself and an in-script transaction abort
// does not roll back CParser's changes. It therefore just parses into the DTM;
// the changes commit to the in-memory program when the script finishes. Persist
// them to disk afterward with `ghidra analyze` (which saves outside script scope).
//
// Config file: ~/.config/ghidra-cli/parse_hoshi.cfg
//   report   = /abs/path/report.txt (parse messages + type count delta)
//   includes = /path/a,/path/b      (comma-separated include search paths)
//   files    = /path/master.h,...   (comma-separated header files to parse)
//   defines  = -DFOO=1,-DBAR        (optional; comma-separated parser args)
//
//@category KAR
import ghidra.app.script.GhidraScript;
import ghidra.app.util.cparser.C.CParserUtils;
import ghidra.app.util.cparser.C.CParserUtils.CParseResults;
import ghidra.program.model.data.DataTypeManager;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Properties;

public class ParseHoshiHeaders extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] scriptArgs = getScriptArgs();
        String cfgPath = (scriptArgs.length >= 1)
                ? scriptArgs[0]
                : System.getProperty("user.home") + "/.config/ghidra-cli/parse_hoshi.cfg";
        File cfgFile = new File(cfgPath);
        if (!cfgFile.exists()) {
            println("[ParseHoshiHeaders] ERROR: config not found: " + cfgPath);
            return;
        }
        Properties cfg = new Properties();
        try (FileInputStream in = new FileInputStream(cfgFile)) {
            cfg.load(in);
        }
        println("[ParseHoshiHeaders] config: " + cfgPath);

        String reportPath = cfg.getProperty("report", "").trim();
        String[] includes = splitCsv(cfg.getProperty("includes", ""));
        String[] files = splitCsv(cfg.getProperty("files", ""));
        String[] defines = splitCsv(cfg.getProperty("defines", ""));

        DataTypeManager dtm = currentProgram.getDataTypeManager();
        int before = dtm.getDataTypeCount(true);

        StringBuilder rpt = new StringBuilder();
        rpt(rpt, "=== ParseHoshiHeaders ===");
        rpt(rpt, "program    : " + currentProgram.getName());
        rpt(rpt, "language   : " + currentProgram.getLanguageID()
                + "  compiler: " + currentProgram.getCompilerSpec().getCompilerSpecID());
        rpt(rpt, "includes   : " + String.join(" | ", includes));
        rpt(rpt, "files      : " + String.join(" | ", files));
        rpt(rpt, "defines    : " + String.join(" ", defines));
        rpt(rpt, "types before: " + before);

        CParseResults res = null;
        Throwable failure = null;
        int after = before;

        int tx = currentProgram.startTransaction("Parse hoshi headers");
        try {
            res = CParserUtils.parseHeaderFiles(null, files, includes, defines, dtm, monitor);
            after = dtm.getDataTypeCount(true);
        } catch (Throwable t) {
            failure = t;
            after = dtm.getDataTypeCount(true);
        } finally {
            currentProgram.endTransaction(tx, true);
        }

        rpt(rpt, "types after : " + after + "   (delta " + (after - before) + ")");
        if (res != null) {
            rpt(rpt, "successful  : " + res.successful());
        }
        if (failure != null) {
            rpt(rpt, "EXCEPTION   : " + failure.getClass().getName() + ": " + failure.getMessage());
        }
        rpt(rpt, "");
        rpt(rpt, "NOTE: changes are in-memory only; run `ghidra analyze` to persist to disk.");
        rpt(rpt, "");
        rpt(rpt, "----- CPP (preprocessor) parse messages -----");
        rpt(rpt, res != null && res.cppParseMessages() != null ? res.cppParseMessages() : "(none)");
        rpt(rpt, "");
        rpt(rpt, "----- C parse messages -----");
        rpt(rpt, res != null && res.cParseMessages() != null ? res.cParseMessages() : "(none)");

        println("[ParseHoshiHeaders] done. delta=" + (after - before)
                + (res != null ? " successful=" + res.successful() : ""));
        if (!reportPath.isEmpty()) {
            try (FileWriter w = new FileWriter(reportPath)) {
                w.write(rpt.toString());
            }
            println("[ParseHoshiHeaders] report -> " + reportPath);
        } else {
            println(rpt.toString());
        }
    }

    private static void rpt(StringBuilder sb, String line) {
        sb.append(line).append('\n');
    }

    private static String[] splitCsv(String v) {
        List<String> out = new ArrayList<>();
        if (v != null) {
            for (String part : v.split(",")) {
                String t = part.trim();
                if (!t.isEmpty()) out.add(t);
            }
        }
        return out.toArray(new String[0]);
    }
}
