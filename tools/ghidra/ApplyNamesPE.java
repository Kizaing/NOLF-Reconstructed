// Apply names from config/d3dren/names_proposal.csv (header: address,name,kind,source_file,evidence,confidence,provenance)
// to the open program: kind=func renames the function (class path -> namespace hierarchy), kind=data|vtable
// renames/creates the label at the address.  Only rows whose confidence is in the allowed list are applied.
// The previous names go to <revertCsv> (address,kind,previous_name) so the change can be undone with names.
// A NOTE bookmark in <category> marks each applied row.
// args: <csv> <revertCsv> <category> [confidences, comma separated, default "high"]
//@category AVP2

import java.io.File;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.util.*;

import ghidra.app.script.GhidraScript;
import ghidra.app.util.NamespaceUtils;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class ApplyNamesPE extends GhidraScript {

	static String[] splitCsv(String line) {
		List<String> out = new ArrayList<>();
		StringBuilder cur = new StringBuilder();
		boolean q = false;
		for (int i = 0; i < line.length(); i++) {
			char c = line.charAt(i);
			if (c == '"') {
				if (q && i + 1 < line.length() && line.charAt(i + 1) == '"') {
					cur.append('"');
					i++;
				}
				else {
					q = !q;
				}
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

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		List<String> lines = Files.readAllLines(new File(args[0]).toPath());
		String category = args[2];
		Set<String> allow = new HashSet<>(Arrays.asList((args.length > 3 ? args[3] : "high").split(",")));
		BookmarkManager bm = currentProgram.getBookmarkManager();
		SymbolTable st = currentProgram.getSymbolTable();
		int nf = 0, nd = 0, skipped = 0, fail = 0;
		try (PrintWriter rev = new PrintWriter(args[1])) {
			rev.println("address,kind,previous_name");
			String[] hdr = splitCsv(lines.get(0));
			List<String> h = Arrays.asList(hdr);
			int iAddr = h.indexOf("address"), iName = h.indexOf("name"), iKind = h.indexOf("kind"),
					iConf = h.indexOf("confidence"), iProv = h.indexOf("provenance");
			for (String line : lines.subList(1, lines.size())) {
				if (line.isBlank()) {
					continue;
				}
				String[] c = splitCsv(line);
				if (c.length <= Math.max(iConf, iProv) || !allow.contains(c[iConf].trim())) {
					skipped++;
					continue;
				}
				String kind = c[iKind].trim();
				Address a = toAddr(Long.parseLong(c[iAddr].trim(), 16));
				List<String> parts = splitQualified(c[iName].trim());
				String name = parts.remove(parts.size() - 1);
				try {
					Namespace ns = currentProgram.getGlobalNamespace();
					if (!parts.isEmpty()) {
						ns = NamespaceUtils.createNamespaceHierarchy(String.join("::", parts), null,
							currentProgram, SourceType.IMPORTED);
					}
					if (kind.equals("func")) {
						Function f = getFunctionAt(a);
						if (f == null) {
							fail++;
							println("NO FUNCTION at " + c[iAddr]);
							continue;
						}
						if (f.getName(true).equals(c[iName].trim())) {
							continue;
						}
						rev.printf("%s,func,\"%s\"%n", c[iAddr], f.getName(true));
						f.getSymbol().setNameAndNamespace(name, ns, SourceType.IMPORTED);
						nf++;
					}
					else if (kind.equals("data") || kind.equals("vtable")) {
						Symbol old = st.getPrimarySymbol(a);
						if (old != null && old.getName(true).equals(c[iName].trim())) {
							continue;
						}
						rev.printf("%s,%s,\"%s\"%n", c[iAddr], kind, old == null ? "" : old.getName(true));
						if (old != null && old.getSource() != SourceType.DEFAULT && !old.isDynamic()) {
							old.setNameAndNamespace(name, ns, SourceType.IMPORTED);
						}
						else {
							Symbol s = st.createLabel(a, name, ns, SourceType.IMPORTED);
							s.setPrimary();
						}
						nd++;
					}
					else {
						skipped++;
						continue;
					}
					bm.setBookmark(a, BookmarkType.NOTE, category,
						"named (" + c[iProv].trim() + "): " + c[iName].trim());
				}
				catch (Exception e) {
					println("FAILED " + c[iAddr] + " " + c[iName] + ": " + e.getMessage());
					fail++;
				}
			}
		}
		println(String.format("ApplyNamesPE: functions %d, data %d, skipped (confidence/kind) %d, failed %d", nf, nd, skipped, fail));
	}
}
