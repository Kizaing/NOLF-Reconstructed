// Export a symbol map for per-function splitting / relocation naming.
// args: <symbols.csv> <summary.txt>
// Schema: addr,end,kind,name,extra   (kinds: func, jumptable, gap, import, data, label)
// Parameterised copy of scripts/ExportSymbols.java: image base from the program (d3d.ren is a DLL at 0x10000000).
//@category AVP2

import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.*;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

public class ExportSymbolsPE extends GhidraScript {

	static class Row {
		long addr, end;
		String kind, name, extra;

		Row(long a, long e, String k, String n, String x) {
			addr = a; end = e; kind = k; name = n; extra = x == null ? "" : x;
		}
	}

	static long IMAGE_BASE = 0x400000L;
	List<Row> rows = new ArrayList<>();
	Set<Long> covered = new HashSet<>(); // addrs already emitted as func/jumptable/gap/import/data

	static String hex(long v) {
		return String.format("%08x", v);
	}

	static String q(String s) {
		return "\"" + s.replace("\"", "\"\"") + "\"";
	}

	String primaryName(Address a) {
		Symbol s = currentProgram.getSymbolTable().getPrimarySymbol(a);
		return s == null ? null : s.getName(true);
	}

	/** "ptr", "byte", or null for other. */
	static String tableClass(Data d) {
		DataType dt = d.getBaseDataType();
		if (dt instanceof Pointer) {
			return d.getLength() == 4 ? "ptr" : null;
		}
		if (dt instanceof Array) {
			DataType el = ((Array) dt).getDataType();
			if (el instanceof TypeDef) {
				el = ((TypeDef) el).getBaseDataType();
			}
			if (el instanceof Pointer && el.getLength() == 4) {
				return "ptr";
			}
			if (el.getLength() == 1 && !(el instanceof AbstractStringDataType) &&
				!(el instanceof CharDataType)) {
				return "byte";
			}
			return null;
		}
		if (dt.getLength() == 1 && !(dt instanceof CharDataType)) {
			return "byte"; // byte, undefined1, uchar ...
		}
		return null;
	}

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		IMAGE_BASE = currentProgram.getImageBase().getOffset();
		Memory mem = currentProgram.getMemory();
		Listing listing = currentProgram.getListing();
		FunctionManager fm = currentProgram.getFunctionManager();
		ReferenceManager rm = currentProgram.getReferenceManager();
		SymbolTable st = currentProgram.getSymbolTable();

		MemoryBlock text = mem.getBlock(".text");
		long textStart = text.getStart().getOffset();
		long textEnd = text.getEnd().getOffset() + 1;
		long vs = peVirtualEnd(mem, text.getStart());
		if (vs > textStart && vs < textEnd) {
			textEnd = vs; // Ghidra block covers SizeOfRawData; clamp to VirtualSize
		}
		AddressSet textSet = new AddressSet(text.getStart(), text.getEnd());

		// ---- functions --------------------------------------------------------------
		TreeMap<Long, Function> entries = new TreeMap<>(); // all entries in .text (incl. excluded)
		for (Function f : fm.getFunctions(textSet, true)) {
			entries.put(f.getEntryPoint().getOffset(), f);
		}
		TreeMap<Long, Long> funcExt = new TreeMap<>(); // included func start -> end
		TreeMap<Long, Long> scanExt = new TreeMap<>(); // funcExt + excluded thunk extents (gap scan)
		int nNoncontig = 0, nThunkExt = 0, nBodyPastEnd = 0;
		List<String> noncontig = new ArrayList<>(), thunkExt = new ArrayList<>(),
				pastEnd = new ArrayList<>();
		long funcBytes = 0;
		for (Map.Entry<Long, Function> e : entries.entrySet()) {
			long a = e.getKey();
			Function f = e.getValue();
			Long next = entries.higherKey(a);
			long end = next == null ? textEnd : next;
			if (f.isThunk()) {
				Function t = f.getThunkedFunction(true);
				if (t == null || t.isExternal()) {
					nThunkExt++;
					thunkExt.add(hex(a) + " " + f.getName(true) + " -> " +
						(t == null ? "?" : t.getName(true)));
					rows.add(new Row(a, a, "label", f.getName(true),
						"thunk_external:" + hex(end)));
					covered.add(a);
					scanExt.put(a, end);
					continue;
				}
			}
			AddressSetView body = f.getBody();
			boolean nc = body.getNumAddressRanges() != 1 ||
				body.getMinAddress().getOffset() != a;
			if (nc) {
				nNoncontig++;
				StringBuilder sb = new StringBuilder();
				for (AddressRange r : body) {
					sb.append(' ').append(hex(r.getMinAddress().getOffset())).append('-')
						.append(hex(r.getMaxAddress().getOffset() + 1));
				}
				noncontig.add(hex(a) + " " + f.getName(true) + " end=" + hex(end) + " body:" + sb);
			}
			long bmax = body.getMaxAddress().getOffset() + 1;
			if (bmax > end || body.getMinAddress().getOffset() < a) {
				nBodyPastEnd++;
				pastEnd.add(hex(a) + " " + f.getName(true) + " end=" + hex(end) + " body=" +
					hex(body.getMinAddress().getOffset()) + "-" + hex(bmax));
			}
			rows.add(new Row(a, end, "func", f.getName(true), nc ? "noncontig" : ""));
			covered.add(a);
			funcExt.put(a, end);
			scanExt.put(a, end);
			funcBytes += end - a;
		}

		// ---- defined data in .text: jump tables ---------------------------------------
		int nJt = 0;
		Row cur = null;
		for (Data d : listing.getDefinedData(textSet, true)) {
			long a = d.getAddress().getOffset();
			long dend = a + Math.max(1, d.getLength());
			String cls = tableClass(d);
			String extra = cls != null ? cls : d.getDataType().getName();
			boolean hasSym = st.getPrimarySymbol(d.getAddress()) != null &&
				!st.getPrimarySymbol(d.getAddress()).isDynamic();
			if (cur != null && cls != null && cls.equals(cur.extra) && cur.end == a && !hasSym &&
				sameExtent(funcExt, cur.addr, a)) {
				cur.end = dend;
				continue;
			}
			String n = primaryName(d.getAddress());
			Map.Entry<Long, Long> fe = funcExt.floorEntry(a);
			boolean inFunc = fe != null && a < fe.getValue();
			cur = new Row(a, dend, inFunc ? "jumptable" : "data",
				n != null ? n : (inFunc ? "JT_" : "DAT_") + hex(a), extra);
			rows.add(cur);
			covered.add(a);
			if (inFunc) {
				nJt++;
			}
		}

		// ---- undefined gaps inside func extents ------------------------------------------
		int nGap = 0;
		List<String> gaps = new ArrayList<>();
		Map<String, Integer> gapKinds = new TreeMap<>();
		AddressSetView undef = listing.getUndefinedRanges(textSet, false, monitor);
		for (AddressRange r : undef) {
			long rs = r.getMinAddress().getOffset(), re = r.getMaxAddress().getOffset() + 1;
			// split the undefined range at function-extent boundaries
			long p = rs;
			while (p < re) {
				Map.Entry<Long, Long> fe = scanExt.floorEntry(p);
				Long nextF = scanExt.higherKey(p);
				long stop = Math.min(re, nextF == null ? Long.MAX_VALUE : nextF);
				if (fe == null || p >= fe.getValue()) {
					p = stop; // not inside a func/thunk extent
					continue;
				}
				long gs = p, ge = Math.min(stop, fe.getValue());
				p = ge;
				byte[] b = new byte[(int) (ge - gs)];
				mem.getBytes(toAddr(gs), b);
				boolean pad = true;
				for (byte x : b) {
					if (x != (byte) 0x90 && x != (byte) 0xCC) {
						pad = false;
						break;
					}
				}
				if (pad) {
					continue;
				}
				nGap++;
				String n = primaryName(toAddr(gs));
				boolean ref = rm.hasReferencesTo(toAddr(gs));
				String gx = isAlignNops(b) ? "align" : ref ? "undisasm_ref" : "undisasm";
				gapKinds.merge(gx, 1, Integer::sum);
				rows.add(new Row(gs, ge, "gap", n != null ? n : "GAP_" + hex(gs), gx));
				covered.add(gs);
				StringBuilder sb = new StringBuilder();
				for (int i = 0; i < Math.min(b.length, 16); i++) {
					sb.append(String.format("%02x", b[i] & 0xff));
				}
				gaps.add(hex(gs) + "-" + hex(ge) + " in " + hex(fe.getKey()) + " " +
					fm.getFunctionAt(toAddr(fe.getKey())).getName(true) + (ref ? " [ref]" : "") +
					" bytes=" + sb + (b.length > 16 ? ".." : ""));
			}
		}

		// ---- data / imports outside .text --------------------------------------------
		AddressSet nonText = new AddressSet(mem.getLoadedAndInitializedAddressSet());
		for (MemoryBlock b : mem.getBlocks()) {
			if (b.isLoaded() && b.getStart().getOffset() < 0x80000000L) { // skip KUSER_SHARED_DATA etc.
				nonText.add(b.getStart(), b.getEnd());
			}
		}
		nonText = nonText.subtract(textSet);
		nonText = nonText.intersect(new AddressSet(toAddr(0), toAddr(0x7fffffffL)));
		int nImp = 0, nData = 0;
		for (Data d : listing.getDefinedData(nonText, true)) {
			Address ad = d.getAddress();
			long a = ad.getOffset();
			String imp = importName(d, rm, st);
			if (imp != null) {
				rows.add(new Row(a, a + 4, "import", imp, ""));
				covered.add(a);
				nImp++;
				continue;
			}
			String n = primaryName(ad);
			rows.add(new Row(a, a + Math.max(1, d.getLength()), "data",
				n != null ? n : "DAT_" + hex(a), d.getDataType().getName()));
			covered.add(a);
			nData++;
		}

		// ---- other primary symbols -----------------------------------------------------
		int nLabel = 0;
		for (Symbol s : st.getAllSymbols(false)) {
			if (!s.isPrimary() || s.isExternal() || !s.getAddress().isMemoryAddress() ||
				s.getAddress().getOffset() >= 0x80000000L) {
				continue;
			}
			long a = s.getAddress().getOffset();
			if (covered.contains(a)) {
				continue;
			}
			covered.add(a);
			rows.add(new Row(a, a, "label", s.getName(true), s.getSymbolType().toString()));
			nLabel++;
		}

		rows.sort(Comparator.<Row> comparingLong(r -> r.addr).thenComparing(r -> r.kind));
		try (PrintWriter w = new PrintWriter(args[0], StandardCharsets.UTF_8)) {
			w.print("addr,end,kind,name,extra\n");
			for (Row r : rows) {
				w.print(hex(r.addr) + "," + hex(r.end) + "," + r.kind + "," + q(r.name) + "," +
					r.extra + "\n");
			}
		}

		Map<String, Integer> counts = new TreeMap<>();
		for (Row r : rows) {
			counts.merge(r.kind, 1, Integer::sum);
		}
		try (PrintWriter w = new PrintWriter(args[1], StandardCharsets.UTF_8)) {
			w.println("source: " + currentProgram.getDomainFile().getPathname());
			w.println(".text " + hex(textStart) + "-" + hex(textEnd) + " (Ghidra block, end clamped to PE VirtualSize)");
			w.println("rows: " + rows.size());
			for (Map.Entry<String, Integer> e : counts.entrySet()) {
				w.println("  " + e.getKey() + ": " + e.getValue());
			}
			w.println("func bytes (sum of extents): " + funcBytes + " (0x" +
				Long.toHexString(funcBytes) + ")");
			w.println("noncontig funcs: " + nNoncontig);
			w.println("funcs whose Ghidra body leaves [entry,end): " + nBodyPastEnd);
			w.println("thunks to external (emitted as label): " + nThunkExt);
			w.println("jumptables: " + nJt + "  gaps: " + nGap + "  imports: " + nImp +
				"  data: " + nData + "  labels: " + nLabel);
			long first = entries.isEmpty() ? textEnd : entries.firstKey();
			w.println("gap classes: " + gapKinds);
			w.println("bytes in .text before first function entry: " + (first - textStart));
			w.println();
			w.println("first 30 gaps:");
			for (int i = 0; i < Math.min(30, gaps.size()); i++) {
				w.println("  " + gaps.get(i));
			}
			w.println();
			w.println("first 30 noncontig funcs:");
			for (int i = 0; i < Math.min(30, noncontig.size()); i++) {
				w.println("  " + noncontig.get(i));
			}
			w.println();
			w.println("first 30 funcs with body outside extent:");
			for (int i = 0; i < Math.min(30, pastEnd.size()); i++) {
				w.println("  " + pastEnd.get(i));
			}
			w.println();
			w.println("thunks to external:");
			for (String s : thunkExt) {
				w.println("  " + s);
			}
		}
		println("ExportSymbolsPE: wrote " + rows.size() + " rows " + counts);
	}

	static final byte[][] NOPS = { { (byte) 0x8d, (byte) 0xa4, 0x24, 0, 0, 0, 0 },
		{ (byte) 0x8d, (byte) 0x9b, 0, 0, 0, 0 }, { (byte) 0x8d, 0x64, 0x24, 0 },
		{ (byte) 0x8d, 0x49, 0 }, { (byte) 0x8d, 0x40, 0 }, { (byte) 0x8b, (byte) 0xff },
		{ (byte) 0x8b, (byte) 0xc0 }, { (byte) 0x90 }, { (byte) 0xcc } };

	/** true if b is a sequence of VC alignment no-op idioms (nop, int3, mov edi,edi, lea r,[r]...). */
	static boolean isAlignNops(byte[] b) {
		int i = 0;
		outer: while (i < b.length) {
			for (byte[] n : NOPS) {
				if (i + n.length <= b.length) {
					boolean m = true;
					for (int k = 0; k < n.length; k++) {
						if (b[i + k] != n[k]) {
							m = false;
							break;
						}
					}
					if (m) {
						i += n.length;
						continue outer;
					}
				}
			}
			return false;
		}
		return true;
	}

	/** VA end (start+VirtualSize) of the PE section starting at secStart, or -1. */
	static long peVirtualEnd(Memory mem, Address secStart) {
		try {
			Address base = secStart.getNewAddress(IMAGE_BASE);
			int lfanew = mem.getInt(base.add(0x3c));
			Address nt = base.add(lfanew);
			int nsec = mem.getShort(nt.add(6)) & 0xffff;
			int opt = mem.getShort(nt.add(20)) & 0xffff;
			Address sh = nt.add(24 + opt);
			for (int i = 0; i < nsec; i++) {
				Address s = sh.add(40L * i);
				long vsz = mem.getInt(s.add(8)) & 0xffffffffL;
				long va = mem.getInt(s.add(12)) & 0xffffffffL;
				if (IMAGE_BASE + va == secStart.getOffset()) {
					return secStart.getOffset() + vsz;
				}
			}
		}
		catch (Exception e) {
			// fall through
		}
		return -1;
	}

	static boolean sameExtent(TreeMap<Long, Long> ext, long a, long b) {
		Map.Entry<Long, Long> ea = ext.floorEntry(a), eb = ext.floorEntry(b);
		if (ea == null || eb == null) {
			return ea == eb;
		}
		return ea.getKey().equals(eb.getKey());
	}

	/** "<DLL>!<func>" if d is an IAT slot, else null. */
	String importName(Data d, ReferenceManager rm, SymbolTable st) {
		if (!d.isPointer() && d.getLength() != 4) {
			return null;
		}
		for (Reference r : rm.getReferencesFrom(d.getAddress())) {
			if (r.isExternalReference()) {
				ExternalLocation loc = ((ExternalReference) r).getExternalLocation();
				String lib = loc.getLibraryName();
				String fn = loc.getOriginalImportedName();
				if (fn == null) {
					fn = loc.getLabel();
				}
				return lib + "!" + fn;
			}
		}
		Symbol s = st.getPrimarySymbol(d.getAddress());
		if (s != null) {
			Namespace ns = s.getParentNamespace();
			if (ns != null && !ns.isGlobal() && ns.getParentNamespace().isGlobal()) {
				String nn = ns.getName().toLowerCase();
				if (nn.endsWith(".dll") || nn.endsWith(".drv") || nn.endsWith(".ocx")) {
					return ns.getName() + "!" + s.getName();
				}
			}
		}
		return null;
	}
}
