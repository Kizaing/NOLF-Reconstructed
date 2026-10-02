// Sync the decomp's Ghidra overrides back into the project:
//   decomp\config\splits.csv   (addr,name,evidence)            function starts Ghidra merged into the
//                                                              preceding extent: the tail is cut off into
//                                                              a new function
//   decomp\config\renames.csv  (addr,name,ghidra_name,evidence) names proven by byte-matching code
// Functions are renamed (class path -> namespace hierarchy), other symbols get a primary label.
// Import (DLL!name) rows are skipped. Names are applied with source IMPORTED, each change gets a NOTE
// bookmark in category Decomp-Sync, and the previous names go to <revertCsv>.
// args: <renamesCsv> <splitsCsv> <revertCsv>
//@category AVP2

import java.io.File;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;

import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.NamespaceUtils;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class SyncDecomp extends GhidraScript {

	static final String CATEGORY = "Decomp-Sync";

	static String[] splitCsv(String line) {
		List<String> out = new ArrayList<>();
		StringBuilder cur = new StringBuilder();
		boolean q = false;
		for (char c : line.toCharArray()) {
			if (c == '"') {
				q = !q;
			}
			else if (c == ',' && !q) {
				out.add(cur.toString());
				cur.setLength(0);
			}
			else {
				cur.append(c);
			}
		}
		out.add(cur.toString());
		return out.toArray(new String[0]);
	}

	// split on "::" outside template brackets
	static List<String> splitQualified(String q) {
		List<String> out = new ArrayList<>();
		int depth = 0, start = 0;
		for (int i = 0; i < q.length(); i++) {
			char c = q.charAt(i);
			if (c == '<') {
				depth++;
			}
			else if (c == '>') {
				depth--;
			}
			else if (depth == 0 && c == ':' && i + 1 < q.length() && q.charAt(i + 1) == ':') {
				out.add(q.substring(start, i));
				start = i + 2;
				i++;
			}
		}
		out.add(q.substring(start));
		return out;
	}

	static List<String[]> rows(String path) throws Exception {
		List<String[]> out = new ArrayList<>();
		boolean header = true;
		for (String line : Files.readAllLines(new File(path).toPath())) {
			if (line.isBlank() || line.startsWith("#")) {
				continue;
			}
			if (header) {
				header = false;
				continue;
			}
			out.add(splitCsv(line.trim()));
		}
		return out;
	}

	PrintWriter rev;
	BookmarkManager bm;

	void split(Address a, String name, String evidence) throws Exception {
		FunctionManager fm = currentProgram.getFunctionManager();
		if (fm.getFunctionAt(a) != null) {
			rename(a, name, evidence);	// already split: just make sure the name is namespaced
			return;
		}
		Function f = fm.getFunctionContaining(a);
		if (f == null) {
			println("SPLIT skip " + a + ": not inside a function");
			return;
		}
		AddressSetView body = f.getBody();
		AddressSet tail = new AddressSet(body.intersect(new AddressSet(a, body.getMaxAddress())));
		f.setBody(new AddressSet(body).subtract(tail));
		CreateFunctionCmd cmd = new CreateFunctionCmd(null, a, tail, SourceType.DEFAULT);
		if (!cmd.applyTo(currentProgram)) {
			f.setBody(body);	// put it back
			println("SPLIT FAILED " + a + ": " + cmd.getStatusMsg());
			return;
		}
		rev.printf("split,%s,\"%s\"%n", a, f.getName(true));
		rename(a, name, evidence);
		bm.setBookmark(a, BookmarkType.NOTE, CATEGORY, "split from " + f.getName(true) + ": " + evidence);
		println("SPLIT " + a + " " + name + " out of " + f.getName(true));
	}

	boolean rename(Address a, String qualified, String evidence) throws Exception {
		qualified = qualified.replace(' ', '_');	// Ghidra's spelling: CMoArray<unsigned_char,DefaultCache>
		List<String> parts = splitQualified(qualified);
		String name = parts.remove(parts.size() - 1);
		Namespace ns = currentProgram.getGlobalNamespace();
		if (!parts.isEmpty()) {
			ns = NamespaceUtils.createNamespaceHierarchy(String.join("::", parts), null, currentProgram,
				SourceType.IMPORTED);
		}
		Function f = getFunctionAt(a);
		if (f != null) {
			if (f.getName().equals(name) && f.getName(true).equals(qualified)) {
				return false;
			}
			rev.printf("func,%s,\"%s\"%n", a, f.getName(true));
			f.getSymbol().setNameAndNamespace(name, ns, SourceType.IMPORTED);
		}
		else {
			Symbol s = currentProgram.getSymbolTable().getPrimarySymbol(a);
			rev.printf("label,%s,\"%s\"%n", a, s == null ? "" : s.getName(true));
			if (s != null && !s.isDynamic() && s.getSymbolType() == SymbolType.LABEL) {
				s.setNameAndNamespace(name, ns, SourceType.IMPORTED);
			}
			else {
				Symbol n = currentProgram.getSymbolTable().createLabel(a, name, ns, SourceType.IMPORTED);
				n.setPrimary();
			}
		}
		bm.setBookmark(a, BookmarkType.NOTE, CATEGORY, "renamed to " + qualified + ": " + evidence);
		return true;
	}

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		bm = currentProgram.getBookmarkManager();
		int nSplit = 0, nRen = 0, nSame = 0, nSkip = 0, nFail = 0;
		try (PrintWriter w = new PrintWriter(args[2])) {
			rev = w;
			rev.println("kind,addr,previous_name");
			// Splits first, so renames of the new functions land on them.
			for (String[] c : rows(args[1])) {
				split(toAddr(c[0]), c[1], c.length > 2 ? c[2] : "");
				nSplit++;
			}
			for (String[] c : rows(args[0])) {
				String name = c[1];
				if (name.contains("!")) {	// import: Ghidra models these as external locations
					println("SKIP import " + c[0] + " " + name);
					nSkip++;
					continue;
				}
				try {
					if (rename(toAddr(c[0]), name, c.length > 3 ? c[3] : "")) {
						nRen++;
					}
					else {
						nSame++;
					}
				}
				catch (Exception e) {
					println("FAILED " + c[0] + " " + name + ": " + e.getMessage());
					nFail++;
				}
			}
		}
		println(String.format("SYNC splits %d, renamed %d, already named %d, imports skipped %d, failed %d",
			nSplit, nRen, nSame, nSkip, nFail));
	}
}
