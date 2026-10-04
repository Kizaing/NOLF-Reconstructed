// Fix function boundaries in .text so every byte is inside a correctly-bounded function, defined
// data, or padding.  args: <changes.csv> <report.txt>
//
// Phases (every change is logged to changes.csv as address,action,before,after):
//   0  data: DirectInput c_dfDIJoystick object table that lives in .text (+ its DIDATAFORMAT and
//      GUID labels), the "VC20XC00" signature before __except_handler3
//   1  merge pseudo-functions into their parent: VC SEH __finally blocks ($L... called locally from
//      the parent whose body surrounds them), catch-continuation / tail fragments that Ghidra made
//      into FUN_ (referenced only by a Catch@ funclet's "mov eax,offset" or by jumps of one function)
//   2  clip bodies that leave [entry, next entry) unless the outside part is the parent tail that
//      sits inside its own Catch@/Unwind@ funclet extent (STL append) -- that layout is kept
//   3  iterate over undisassembled bytes and over code that belongs to no function:
//        padding before it / called from another function / address taken by other code or by a
//        fn-pointer table / jumped to from >=2 functions  -> new function
//        SEH scope-table filter/handler, local call, jump target of one function, catch
//        continuation, unreferenced                      -> added to the owning function's body
//   4  report what is left
// Never renames an existing function; a removed pseudo-function keeps its name as a label.
// Parameterised copy of scripts/FixBoundaries.java for PE images at any base (d3d.ren: DLL at 0x10000000).
// Exe-specific parts removed: DirectInput tables, fixed OVERRIDE/LABEL_TABLES addresses; the "VC20XC00"
// signature before __except_handler3 is found by scanning .text instead of at a fixed address.
//@category AVP2

import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.*;

import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.datatype.microsoft.GuidDataType;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

public class FixBoundariesPE extends GhidraScript {

	// fixed decisions for cases the generic rules cannot see (address -> action[:reason]); filled in run()
	static final Map<Long, String> OVERRIDE = new HashMap<>();
	// data tables whose entries are labels inside one CRT asm routine (not function pointers); filled by
	// optional args: "labeltable=<hexlo>-<hexhi>" and "override=<hexaddr>=<action>"
	static final List<long[]> LABEL_TABLES_L = new ArrayList<>();
	static long IMAGE_BASE = 0x400000L;
	static long SIG_FUNC = -1;

	PrintWriter log, rep;
	Memory mem;
	Listing listing;
	FunctionManager fm;
	ReferenceManager rm;
	SymbolTable st;
	AddressSet textSet;
	long textStart, textEnd;
	Map<Long, List<Long>> dataRefs = new HashMap<>(); // value in .text -> locations (non-.text)
	Set<Long> failed = new HashSet<>();
	Map<Long, AddressSet> origBodies = new HashMap<>();
	Map<Long, String> origNames = new HashMap<>();
	Set<Long> explicitlyChanged = new HashSet<>(); // pre-existing functions changed by a logged action
	Map<String, Integer> counts = new TreeMap<>();

	static String hex(long v) {
		return String.format("%08x", v);
	}

	static String q(String s) {
		return "\"" + s.replace("\"", "\"\"") + "\"";
	}

	void change(long a, String action, String before, String after) {
		log.println(hex(a) + "," + action + "," + q(before) + "," + q(after));
		log.flush();
		counts.merge(action, 1, Integer::sum);
	}

	String fdesc(Function f) {
		if (f == null) {
			return "-";
		}
		return f.getName(true) + "@" + hex(f.getEntryPoint().getOffset()) + " body=" + bodyStr(f.getBody());
	}

	static String bodyStr(AddressSetView b) {
		StringBuilder sb = new StringBuilder();
		for (AddressRange r : b) {
			if (sb.length() > 0) {
				sb.append(' ');
			}
			sb.append(hex(r.getMinAddress().getOffset())).append('-')
				.append(hex(r.getMaxAddress().getOffset() + 1));
		}
		return sb.toString();
	}

	boolean inText(long v) {
		return v >= textStart && v < textEnd;
	}

	int getInt(long a) throws Exception {
		return mem.getInt(toAddr(a));
	}

	// ------------------------------------------------------------------ extents
	TreeMap<Long, Function> entries() {
		TreeMap<Long, Function> m = new TreeMap<>();
		for (Function f : fm.getFunctions(textSet, true)) {
			m.put(f.getEntryPoint().getOffset(), f);
		}
		return m;
	}

	/** function whose [entry, next entry) contains a (thunks to externals included). */
	Function extentOwner(TreeMap<Long, Function> e, long a) {
		Map.Entry<Long, Function> fe = e.floorEntry(a);
		return fe == null ? null : fe.getValue();
	}

	static boolean isFunclet(Function f) {
		String n = f.getName();
		return n.startsWith("Catch@") || n.startsWith("Unwind@");
	}

	// ------------------------------------------------------------------ main
	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		IMAGE_BASE = currentProgram.getImageBase().getOffset();
		for (int i = 2; i < args.length; i++) {
			if (args[i].startsWith("labeltable=")) {
				String[] r = args[i].substring(11).split("-");
				LABEL_TABLES_L.add(new long[] { Long.parseLong(r[0], 16), Long.parseLong(r[1], 16) });
			}
			else if (args[i].startsWith("override=")) {
				String[] r = args[i].substring(9).split("=", 2);
				OVERRIDE.put(Long.parseLong(r[0], 16), r[1]);
			}
		}
		mem = currentProgram.getMemory();
		listing = currentProgram.getListing();
		fm = currentProgram.getFunctionManager();
		rm = currentProgram.getReferenceManager();
		st = currentProgram.getSymbolTable();
		MemoryBlock text = mem.getBlock(".text");
		textStart = text.getStart().getOffset();
		textEnd = text.getEnd().getOffset() + 1;
		long vs = peVirtualEnd(text.getStart());
		if (vs > textStart && vs < textEnd) {
			textEnd = vs;
		}
		textSet = new AddressSet(toAddr(textStart), toAddr(textEnd - 1));
		log = new PrintWriter(args[0], StandardCharsets.UTF_8);
		log.println("address,action,before,after");
		rep = new PrintWriter(args[1], StandardCharsets.UTF_8);

		int nBefore = entries().size();
		rep.println("functions in .text before: " + nBefore);
		buildDataRefs();
		buildScopeHeads();
		for (Function f : fm.getFunctions(textSet, true)) {
			origBodies.put(f.getEntryPoint().getOffset(), new AddressSet(f.getBody()));
			origNames.put(f.getEntryPoint().getOffset(), f.getName(true));
		}

		phase0Data();
		phase1Merge();
		phase2Clip();
		phase3Loop();
		phase4Report(nBefore);
		log.close();
		rep.close();
		println("FixBoundariesPE: " + counts);
	}

	// ------------------------------------------------------------------ data refs
	void buildDataRefs() throws Exception {
		for (MemoryBlock b : mem.getBlocks()) {
			if (!b.isInitialized() || !b.isLoaded() || b.getName().equals(".text") ||
				b.getStart().getOffset() >= 0x80000000L) {
				continue;
			}
			long s = b.getStart().getOffset();
			int n = (int) b.getSize();
			byte[] buf = new byte[n];
			b.getBytes(b.getStart(), buf);
			for (int i = 0; i + 4 <= n; i++) {
				long v = (buf[i] & 0xffL) | (buf[i + 1] & 0xffL) << 8 | (buf[i + 2] & 0xffL) << 16 |
					(buf[i + 3] & 0xffL) << 24;
				if (inText(v)) {
					dataRefs.computeIfAbsent(v, k -> new ArrayList<>()).add(s + i);
				}
			}
		}
	}

	static boolean tryLevel(long v) {
		return v == 0xffffffffL || (v >= 0 && v <= 64);
	}

	/** heads of VC C-SEH scope tables: imm32 of "push -1; push offset table" (6a ff 68 imm32) */
	Set<Long> scopeHeads = new HashSet<>();

	void buildScopeHeads() throws Exception {
		byte[] b = new byte[(int) (textEnd - textStart)];
		mem.getBytes(toAddr(textStart), b);
		for (int i = 0; i + 7 <= b.length; i++) {
			if (b[i] == 0x6a && b[i + 1] == (byte) 0xff && b[i + 2] == 0x68) {
				long v = (b[i + 3] & 0xffL) | (b[i + 4] & 0xffL) << 8 | (b[i + 5] & 0xffL) << 16 |
					(b[i + 6] & 0xffL) << 24;
				scopeHeads.add(v);
			}
		}
		rep.println("C-SEH scope table heads (push -1; push offset): " + scopeHeads.size());
	}

	/** s is the filter or handler slot of an entry {prevLevel, filter, handler} of a scope table
	 *  that some SEH prolog pushes. */
	boolean isScopeEntry(long s) {
		for (int k = 0; k < 32; k++) {
			for (int slot = 1; slot <= 2; slot++) {
				long entry = s - 4L * slot;
				long head = entry - 12L * k;
				if (!scopeHeads.contains(head)) {
					continue;
				}
				try {
					long lvl = getInt(entry) & 0xffffffffL;
					long other = getInt(slot == 1 ? s + 4 : s - 4) & 0xffffffffL;
					if (tryLevel(lvl) && (inText(other) || (slot == 2 && other == 0))) {
						return true;
					}
				}
				catch (Exception e) {
					// ignore
				}
			}
		}
		return false;
	}

	static boolean inTables(long s, List<long[]> t) {
		for (long[] r : t) {
			if (s >= r[0] && s < r[1]) {
				return true;
			}
		}
		return false;
	}

	// ------------------------------------------------------------------ phase 0
	void phase0Data() throws Exception {
		DataTypeManager dtm = currentProgram.getDataTypeManager();
		// "VC20XC00" signature (8 bytes) in .text, right before __except_handler3
		byte[] b = new byte[(int) (textEnd - textStart)];
		mem.getBytes(toAddr(textStart), b);
		byte[] pat = "VC20XC00".getBytes(StandardCharsets.ISO_8859_1);
		for (int i = 0; i + 8 <= b.length; i++) {
			boolean m = true;
			for (int k = 0; k < 8 && m; k++) {
				m = b[i + k] == pat[k];
			}
			if (m) {
				long sig = textStart + i;
				if (listing.getInstructionContaining(toAddr(sig)) == null) {
					makeData(sig, new ArrayDataType(CharDataType.dataType, 8, 1, dtm), null);
				}
				SIG_FUNC = sig + 8;
				OVERRIDE.put(sig, "data:VC20XC00 signature preceding __except_handler3");
				rep.println("VC20XC00 signature at " + hex(sig));
			}
		}
	}

	void makeData(long a, DataType dt, String name) throws Exception {
		Address ad = toAddr(a);
		Data old = listing.getDefinedDataAt(ad);
		String before = old == null ? "undefined" : old.getDataType().getName();
		if (old != null && old.getDataType().isEquivalent(dt)) {
			// already done
		}
		else {
			if (listing.getInstructions(new AddressSet(ad, ad.add(dt.getLength() - 1)), true).hasNext()) {
				rep.println("WARN data " + hex(a) + " overlaps instructions, skipped");
				return;
			}
			listing.clearCodeUnits(ad, ad.add(dt.getLength() - 1), false);
			listing.createData(ad, dt);
			change(a, "data", before, dt.getName() + " len=" + dt.getLength());
		}
		if (name != null) {
			Symbol s = st.getPrimarySymbol(ad);
			if (s == null || s.getSource() == SourceType.DEFAULT || s.isDynamic()) {
				st.createLabel(ad, name, SourceType.USER_DEFINED).setPrimary();
				change(a, "label", s == null ? "-" : s.getName(), name);
			}
		}
	}

	static final String[][] GUIDS = { { "a36d02e0-c9f3-11cf-bfc7-444553540000", "GUID_XAxis" },
		{ "a36d02e1-c9f3-11cf-bfc7-444553540000", "GUID_YAxis" },
		{ "a36d02e2-c9f3-11cf-bfc7-444553540000", "GUID_ZAxis" },
		{ "a36d02f4-c9f3-11cf-bfc7-444553540000", "GUID_RxAxis" },
		{ "a36d02f5-c9f3-11cf-bfc7-444553540000", "GUID_RyAxis" },
		{ "a36d02e3-c9f3-11cf-bfc7-444553540000", "GUID_RzAxis" },
		{ "a36d02e4-c9f3-11cf-bfc7-444553540000", "GUID_Slider" },
		{ "a36d02f0-c9f3-11cf-bfc7-444553540000", "GUID_Button" },
		{ "55728220-d33c-11cf-bfc7-444553540000", "GUID_Key" },
		{ "a36d02f2-c9f3-11cf-bfc7-444553540000", "GUID_POV" },
		{ "a36d02f3-c9f3-11cf-bfc7-444553540000", "GUID_Unknown" } };

	void labelGuid(long g) throws Exception {
		byte[] b = new byte[16];
		mem.getBytes(toAddr(g), b);
		java.nio.ByteBuffer bb = java.nio.ByteBuffer.wrap(b).order(java.nio.ByteOrder.LITTLE_ENDIAN);
		String s = String.format("%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", bb.getInt(0),
			bb.getShort(4), bb.getShort(6), b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
		for (String[] kv : GUIDS) {
			if (kv[0].equals(s)) {
				Address ad = toAddr(g);
				Symbol sym = st.getPrimarySymbol(ad);
				if (sym != null && sym.getName().equals(kv[1])) {
					return;
				}
				if (sym == null || sym.getSource() == SourceType.DEFAULT || sym.isDynamic()) {
					Data d = listing.getDefinedDataAt(ad);
					if (d == null || !(d.getDataType() instanceof GuidDataType)) {
						listing.clearCodeUnits(ad, ad.add(15), false);
						listing.createData(ad, new GuidDataType());
					}
					st.createLabel(ad, kv[1], SourceType.USER_DEFINED).setPrimary();
					change(g, "label", sym == null ? "-" : sym.getName(), kv[1] + " (GUID)");
				}
				return;
			}
		}
	}

	// ------------------------------------------------------------------ phase 1
	boolean padBefore(long a) throws Exception {
		Address p = toAddr(a - 1);
		byte b = mem.getByte(p);
		if (b != (byte) 0x90 && b != (byte) 0xcc) {
			return false;
		}
		Instruction ins = listing.getInstructionContaining(p);
		if (ins == null) {
			return listing.getDefinedDataContaining(p) == null;
		}
		String m = ins.getMnemonicString();
		return ins.getLength() == 1 && (m.equals("NOP") || m.equals("INT3"));
	}

	/** nearest preceding function entry that is not a funclet and not in skip. */
	Function precedingParent(TreeMap<Long, Function> e, long a, Set<Long> skip) {
		Long k = e.lowerKey(a);
		while (k != null) {
			Function f = e.get(k);
			if (!isFunclet(f) && !skip.contains(k)) {
				return f;
			}
			k = e.lowerKey(k);
		}
		return null;
	}

	/** CALL / JMP-Jcc / other, by the mnemonic of the referencing instruction (Ghidra turns
	 *  "jmp <function>" into a call-flow, so the reference type alone is misleading). */
	static String refKind(Instruction ins, Reference r) {
		if (ins == null) {
			return r.getReferenceType().isFlow() ? "jump" : "data";
		}
		String m = ins.getMnemonicString();
		boolean target = false;
		for (Address t : ins.getFlows()) {
			if (t.equals(r.getToAddress())) {
				target = true;
			}
		}
		if (target || r.getReferenceType().isFlow()) {
			if (m.equals("CALL")) {
				return "call";
			}
			if (m.startsWith("J") || m.equals("LOOP")) {
				return "jump";
			}
		}
		return "data";
	}

	/** instruction that ends right before a (null if data/undefined). */
	Instruction prevInstr(long a) {
		Instruction p = listing.getInstructionContaining(toAddr(a - 1));
		if (p == null || p.getMaxAddress().getOffset() != a - 1) {
			return null;
		}
		return p;
	}

	static boolean isTerminator(Instruction p) {
		if (p == null) {
			return false;
		}
		String m = p.getMnemonicString();
		return m.equals("RET") || m.equals("JMP") || m.equals("IRET") || m.equals("HLT");
	}

	void phase1Merge() throws Exception {
		TreeMap<Long, Function> e = entries();
		// candidate -> parent entry (-1: preceding non-funclet function)
		Map<Long, Long> merge = new TreeMap<>();
		Map<Long, String> why = new HashMap<>();
		for (Function f : e.values()) {
			if (f.isThunk() || isFunclet(f)) {
				continue;
			}
			Address a = f.getEntryPoint();
			long ao = a.getOffset();
			String n = f.getName();
			boolean dflt = f.getSymbol().getSource() == SourceType.DEFAULT || n.startsWith("FUN_");
			boolean dollarL = n.contains("$L");
			if (!dflt && !dollarL) {
				continue;
			}
			Set<Function> callers = new HashSet<>(), jumpers = new HashSet<>();
			boolean callNull = false, jumpNull = false, otherData = false, contRef = false;
			for (Reference r : rm.getReferencesTo(a)) {
				Function g = fm.getFunctionContaining(r.getFromAddress());
				Instruction ins = listing.getInstructionAt(r.getFromAddress());
				String k = refKind(ins, r);
				if (k.equals("call")) {
					if (g == null) {
						callNull = true;
					}
					else {
						callers.add(g);
					}
				}
				else if (k.equals("jump")) {
					if (g == null) {
						jumpNull = true;
					}
					else {
						jumpers.add(g);
					}
				}
				else if (ins != null) {
					// catch continuation: "mov eax, offset cont; ret" inside a Catch@ funclet
					if (g != null && g.getName().startsWith("Catch@") && ins.getMnemonicString().equals("MOV")) {
						contRef = true;
					}
					else {
						otherData = true;
					}
				}
				else if (!isScopeEntry(r.getFromAddress().getOffset())) {
					otherData = true;
				}
			}
			for (long s : dataRefs.getOrDefault(ao, List.of())) {
				if (!isScopeEntry(s)) {
					otherData = true;
				}
			}
			if (callNull || jumpNull || otherData) {
				continue;
			}
			boolean pad = padBefore(ao);
			// (A) local __finally / $L block called only from the function that surrounds it
			if (callers.size() == 1 && jumpers.isEmpty() && !contRef) {
				Function p = callers.iterator().next();
				if (p.getEntryPoint().getOffset() < ao && !pad) {
					boolean surrounds = p.getBody().getMaxAddress().getOffset() > ao;
					if (dollarL || surrounds) {
						merge.put(ao, p.getEntryPoint().getOffset());
						why.put(ao, "SEH __finally/$L block called locally from " + p.getName(true) +
							(surrounds ? " (parent body continues after it)" : ""));
					}
				}
				continue;
			}
			if (!callers.isEmpty() || pad || !dflt) {
				continue;
			}
			Set<Function> js = new HashSet<>();
			for (Function g : jumpers) {
				js.add(isFunclet(g) ? null : g);
			}
			js.remove(null);
			Instruction prev = prevInstr(ao);
			Function prevF = prev == null ? null : fm.getFunctionContaining(prev.getAddress());
			if (contRef) {
				// (B) catch continuation
				merge.put(ao, -1L);
				why.put(ao, "catch continuation (target of Catch@ funclet)");
			}
			else if (!jumpers.isEmpty()) {
				// (C) jump-only fragment of the function right before it (a far-away jumper is a
				//     tail call into a real function, not a fragment)
				Set<Long> skip = new HashSet<>(merge.keySet());
				Function pp = precedingParent(e, ao, skip);
				boolean near = true;
				for (Function g : jumpers) {
					Function gp = isFunclet(g) ? precedingParent(e, g.getEntryPoint().getOffset(), skip) : g;
					if (gp != pp && !merge.containsKey(g.getEntryPoint().getOffset())) {
						near = false;
					}
				}
				if (near && pp != null) {
					merge.put(ao, -1L);
					why.put(ao, "code fragment of the preceding function reached only by its jumps " +
						"(Ghidra had turned the jumps into calls)");
				}
			}
			else if (prev != null && !isTerminator(prev) && prevF != null && !isFunclet(prevF)) {
				// (D) no references at all, previous function falls (or "returns" from a call
				//     Ghidra wrongly treats as non-returning) into it
				merge.put(ao, prevF.getEntryPoint().getOffset());
				why.put(ao, "unreferenced continuation of the preceding function (previous instruction '" +
					prev + "' does not end it)");
			}
		}
		// resolve parents, then apply
		for (Map.Entry<Long, Long> m : merge.entrySet()) {
			long ao = m.getKey();
			long po = m.getValue();
			Function p;
			if (po == -1L) {
				p = precedingParent(e, ao, merge.keySet());
			}
			else {
				p = e.get(po);
				int guard = 0;
				while (p != null && merge.containsKey(p.getEntryPoint().getOffset()) && guard++ < 10) {
					Long pp = merge.get(p.getEntryPoint().getOffset());
					p = pp == -1L ? precedingParent(e, p.getEntryPoint().getOffset(), merge.keySet()) : e.get(pp);
				}
			}
			Function f = e.get(ao);
			if (p == null || f == null) {
				rep.println("WARN merge " + hex(ao) + ": no parent");
				continue;
			}
			mergeInto(f, p, why.get(ao));
		}
	}

	void mergeInto(Function f, Function p, String reason) throws Exception {
		Address a = f.getEntryPoint();
		String before = fdesc(f) + " | parent " + fdesc(p);
		AddressSetView fb = f.getBody();
		String name = f.getName();
		SourceType src = f.getSymbol().getSource();
		Namespace ns = f.getParentNamespace();
		boolean dflt = src == SourceType.DEFAULT;
		fm.removeFunction(a);
		if (!dflt) {
			Symbol s = st.getPrimarySymbol(a);
			if (s == null || !s.getName().equals(name)) {
				st.createLabel(a, name, ns, src);
			}
		}
		// jumps that Ghidra had turned into call-flows (because the target was a function) are
		// plain jumps again
		int cleared = 0;
		for (Reference r : rm.getReferencesTo(a)) {
			Instruction ins = listing.getInstructionAt(r.getFromAddress());
			if (ins != null && ins.getMnemonicString().startsWith("J") &&
				ins.getFlowOverride() != FlowOverride.NONE) {
				ins.setFlowOverride(FlowOverride.NONE);
				cleared++;
			}
		}
		AddressSet nb = new AddressSet(p.getBody()).union(fb);
		p.setBody(nb);
		explicitlyChanged.add(p.getEntryPoint().getOffset());
		explicitlyChanged.add(a.getOffset());
		change(a.getOffset(), "merge_into_parent", before,
			reason + " -> " + fdesc(p) + (dflt ? "" : " (name kept as label)") +
				(cleared > 0 ? " (cleared " + cleared + " jump flow override(s))" : ""));
	}

	// ------------------------------------------------------------------ phase 2
	void phase2Clip() throws Exception {
		TreeMap<Long, Function> e = entries();
		for (Map.Entry<Long, Function> en : e.entrySet()) {
			Function f = en.getValue();
			if (f.isThunk()) {
				continue;
			}
			Long nx = e.higherKey(en.getKey());
			long end = nx == null ? textEnd : nx;
			AddressSet ext = new AddressSet(toAddr(en.getKey()), toAddr(end - 1));
			AddressSetView out = f.getBody().subtract(ext);
			if (out.isEmpty()) {
				continue;
			}
			AddressSet cut = new AddressSet();
			for (AddressRange r : out) {
				Function o = extentOwner(e, r.getMinAddress().getOffset());
				if (o != null && isFunclet(o) && o.getEntryPoint().getOffset() > en.getKey()) {
					rep.println("KEEP " + hex(en.getKey()) + " " + f.getName(true) + " tail " +
						hex(r.getMinAddress().getOffset()) + "-" + hex(r.getMaxAddress().getOffset() + 1) +
						" lies in the extent of its funclet " + o.getName());
					continue;
				}
				cut.add(r);
			}
			if (cut.isEmpty()) {
				continue;
			}
			String before = fdesc(f);
			f.setBody(new AddressSet(f.getBody()).subtract(cut));
			explicitlyChanged.add(en.getKey());
			change(en.getKey(), "clip_body", before, "removed " + bodyStr(cut) + " -> " + fdesc(f));
		}
	}

	// ------------------------------------------------------------------ phase 3
	static final byte[][] NOPS = { { (byte) 0x8d, (byte) 0xa4, 0x24, 0, 0, 0, 0 },
		{ (byte) 0x8d, (byte) 0x9b, 0, 0, 0, 0 }, { (byte) 0x8d, 0x64, 0x24, 0 },
		{ (byte) 0x8d, 0x49, 0 }, { (byte) 0x8d, 0x40, 0 }, { (byte) 0x8b, (byte) 0xff },
		{ (byte) 0x8b, (byte) 0xc0 }, { (byte) 0x90 }, { (byte) 0xcc } };

	/** length of the alignment-idiom prefix of b[off..] */
	static int alignPrefix(byte[] b, int off) {
		int i = off;
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
			break;
		}
		return i - off;
	}

	static class Cand {
		long a;
		boolean undef;
		long runEnd;

		Cand(long a, boolean u, long e) {
			this.a = a;
			undef = u;
			runEnd = e;
		}
	}

	List<Cand> candidates() throws Exception {
		List<Cand> out = new ArrayList<>();
		// undefined runs
		for (AddressRange r : listing.getUndefinedRanges(textSet, false, monitor)) {
			long s = r.getMinAddress().getOffset(), e = r.getMaxAddress().getOffset() + 1;
			byte[] b = new byte[(int) (e - s)];
			mem.getBytes(toAddr(s), b);
			int i = 0;
			while (i < b.length) {
				// skip pad bytes only (0x90/0xcc); multi-byte idioms only if the run is align-only
				while (i < b.length && (b[i] == (byte) 0x90 || b[i] == (byte) 0xcc)) {
					i++;
				}
				if (i >= b.length) {
					break;
				}
				if (i + alignPrefix(b, i) >= b.length) {
					break; // rest is alignment no-ops
				}
				out.add(new Cand(s + i, true, e));
				break; // one candidate per run; later bytes are revisited next iteration
			}
		}
		// instructions in no function body
		AddressSet orphan = new AddressSet();
		for (Instruction ins : listing.getInstructions(textSet, true)) {
			if (fm.getFunctionContaining(ins.getAddress()) == null) {
				orphan.add(ins.getMinAddress(), ins.getMaxAddress());
			}
		}
		for (AddressRange r : orphan) {
			long s = r.getMinAddress().getOffset(), e = r.getMaxAddress().getOffset() + 1;
			byte[] b = new byte[(int) (e - s)];
			mem.getBytes(toAddr(s), b);
			int i = alignPrefix(b, 0);
			if (i >= b.length) {
				continue; // NOP-only padding instructions
			}
			Instruction ins = listing.getInstructionContaining(toAddr(s + i));
			if (ins == null || ins.getAddress().getOffset() != s + i) {
				i = 0;
			}
			out.add(new Cand(s + i, false, e));
		}
		out.sort(Comparator.comparingLong(c -> c.a));
		return out;
	}

	/** extent owner with funclets mapped to their parent function */
	Function logicalOwner(TreeMap<Long, Function> e, long a) {
		Function o = extentOwner(e, a);
		if (o != null && isFunclet(o)) {
			Function p = precedingParent(e, o.getEntryPoint().getOffset(), Set.of());
			return p != null ? p : o;
		}
		return o;
	}

	/** c's code run ends right before f's entry with only padding/alignment in between */
	boolean adjacentBefore(Cand c, Function f) throws Exception {
		long entry = f.getEntryPoint().getOffset();
		if (entry <= c.a || entry - c.a > 64) {
			return false;
		}
		long x = c.a;
		while (x < entry) {
			Instruction ins = listing.getInstructionAt(toAddr(x));
			if (ins == null) {
				break;
			}
			x = ins.getMaxAddress().getOffset() + 1;
			if (isTerminator(ins)) {
				break;
			}
		}
		if (x > entry) {
			return false;
		}
		byte[] b = new byte[(int) (entry - x)];
		mem.getBytes(toAddr(x), b);
		return alignPrefix(b, 0) == b.length;
	}

	/** returns "func:reason" | "absorb:<entryhex>:reason" | "data:reason" */
	String classify(TreeMap<Long, Function> e, Cand c) throws Exception {
		long a = c.a;
		if (OVERRIDE.containsKey(a)) {
			return OVERRIDE.get(a);
		}
		Function owner = extentOwner(e, a);
		if (owner != null && owner.isThunk() && owner.getThunkedFunction(true) != null &&
			owner.getThunkedFunction(true).isExternal()) {
			owner = null; // after an import thunk: belongs to nobody
		}
		Function lowner = owner == null ? null : logicalOwner(e, a);
		boolean pad = padBefore(a);
		Set<Function> callers = new LinkedHashSet<>();
		boolean callFromNoFunc = false;
		List<Long> jumpSrc = new ArrayList<>();
		List<String> codeData = new ArrayList<>();
		Function contParent = null;
		boolean scope = false, labelTable = false, fnTable = false, ownTable = false;
		for (Reference r : rm.getReferencesTo(toAddr(a))) {
			Address from = r.getFromAddress();
			Function g = fm.getFunctionContaining(from);
			Instruction ins = listing.getInstructionAt(from);
			String k = refKind(ins, r);
			if (k.equals("call")) {
				if (g == null) {
					callFromNoFunc = true;
				}
				else {
					callers.add(g);
				}
			}
			else if (k.equals("jump")) {
				jumpSrc.add(from.getOffset());
			}
			else if (ins != null) {
				if (g != null && g.getName().startsWith("Catch@") && ins.getMnemonicString().equals("MOV")) {
					contParent = precedingParent(e, g.getEntryPoint().getOffset(), Set.of());
				}
				else {
					codeData.add(hex(from.getOffset()) + "[" + (g == null ? "-" : g.getName(true)) + "]");
				}
			}
			else if (inText(from.getOffset())) {
				// defined data in .text referencing it: a switch table of the owner?
				Data d = listing.getDefinedDataContaining(from);
				if (d != null && owner != null && extentOwner(e, d.getAddress().getOffset()) == owner) {
					ownTable = true;
				}
				else {
					fnTable = true;
				}
			}
		}
		for (long s : dataRefs.getOrDefault(a, List.of())) {
			if (inTables(s, LABEL_TABLES_L)) {
				labelTable = true;
			}
			else if (isScopeEntry(s)) {
				scope = true;
			}
			else {
				fnTable = true;
			}
		}
		if (pad) {
			// asm code placed before its proc's entry (strchr): padding, then code that only the
			// following function jumps to
			if (callers.isEmpty() && !callFromNoFunc && codeData.isEmpty() && !fnTable && !scope &&
				!labelTable && !jumpSrc.isEmpty() && owner != null) {
				Function only = null;
				boolean one = true;
				for (long s : jumpSrc) {
					Function so = logicalOwner(e, s);
					if (only == null) {
						only = so;
					}
					else if (so != only) {
						one = false;
					}
				}
				if (one && only != null && adjacentBefore(c, only)) {
					return "absorb:" + hex(only.getEntryPoint().getOffset()) +
						":code right before the entry of " + only.getName(true) + " that only it jumps to";
				}
			}
			return "func:padding before";
		}
		for (Function g : callers) {
			if (g != owner && g != lowner) {
				return "func:called from " + g.getName(true);
			}
		}
		if (callFromNoFunc) {
			return "func:called from code outside functions";
		}
		if (!codeData.isEmpty()) {
			return "func:address taken by code " + String.join(" ", codeData);
		}
		if (fnTable) {
			return "func:referenced from pointer table " + dataRefs.getOrDefault(a, List.of()).stream()
				.map(FixBoundariesPE::hex).reduce((x, y) -> x + " " + y).orElse("(data in .text)");
		}
		if (owner == null) {
			return "func:outside any function extent";
		}
		if (contParent != null) {
			return "absorb:" + hex(contParent.getEntryPoint().getOffset()) + ":catch continuation";
		}
		if (scope) {
			return "absorb:" + hex(lowner.getEntryPoint().getOffset()) + ":SEH scope-table filter/handler";
		}
		if (labelTable) {
			return "absorb:" + hex(lowner.getEntryPoint().getOffset()) + ":label table of CRT asm routine";
		}
		if (ownTable) {
			return "absorb:" + hex(lowner.getEntryPoint().getOffset()) + ":switch table target";
		}
		if (!callers.isEmpty()) {
			return "func:called (static helper) from " + callers.iterator().next().getName(true);
		}
		if (!jumpSrc.isEmpty()) {
			// classify each jump by the extent it comes from
			Set<Function> internal = new LinkedHashSet<>(), external = new LinkedHashSet<>();
			Function adjacentTo = null;
			for (long s : jumpSrc) {
				Function so = logicalOwner(e, s);
				if (so == lowner) {
					internal.add(so);
				}
				else if (so != null && adjacentBefore(c, so)) {
					adjacentTo = so; // asm code placed before its proc's entry (e.g. strchr)
				}
				else {
					external.add(so);
				}
			}
			if (external.isEmpty() && adjacentTo != null && internal.isEmpty()) {
				return "absorb:" + hex(adjacentTo.getEntryPoint().getOffset()) +
					":code right before the entry of " + adjacentTo.getName(true) + " that only it jumps to";
			}
			if (!external.isEmpty()) {
				StringBuilder sb = new StringBuilder();
				for (Function g : external) {
					sb.append(' ').append(g == null ? "-" : g.getName(true));
				}
				return "func:tail-jumped to from other function(s)" + sb;
			}
			return "absorb:" + hex(lowner.getEntryPoint().getOffset()) + ":jump target inside its extent";
		}
		// unreferenced
		Instruction prev = prevInstr(a);
		// code that jumps back into its owner is a piece of the owner (asm label code)
		for (Instruction ins : listing.getInstructions(flowSet(toAddr(a)), true)) {
			if (ins.getMnemonicString().startsWith("J")) {
				for (Address t : ins.getFlows()) {
					if (lowner.getBody().contains(t)) {
						return "absorb:" + hex(lowner.getEntryPoint().getOffset()) +
							":unreferenced code that jumps back into " + lowner.getName(true);
					}
				}
			}
		}
		// code that uses the owner's EBP frame (negative ebp offsets) without setting up its own
		int nIns = 0;
		boolean ownFrame = false, ebpLocal = false;
		for (Instruction ins : listing.getInstructions(flowSet(toAddr(a)), true)) {
			if (nIns++ >= 12) {
				break;
			}
			String t = ins.toString();
			if (t.equals("PUSH EBP")) {
				ownFrame = true;
			}
			for (int oi = 0; oi < ins.getNumOperands(); oi++) {
				Object[] objs = ins.getOpObjects(oi);
				boolean ebp = false;
				for (Object o : objs) {
					if (o instanceof ghidra.program.model.lang.Register &&
						((ghidra.program.model.lang.Register) o).getName().equals("EBP")) {
						ebp = true;
					}
				}
				if (ebp && (ins.getOperandType(oi) & ghidra.program.model.lang.OperandType.DYNAMIC) != 0) {
					for (Object o : objs) {
						if (o instanceof ghidra.program.model.scalar.Scalar &&
							((ghidra.program.model.scalar.Scalar) o).getSignedValue() < 0) {
							ebpLocal = true;
						}
					}
				}
			}
		}
		if (ebpLocal && !ownFrame) {
			return "absorb:" + hex(lowner.getEntryPoint().getOffset()) +
				":unreferenced code using the EBP frame of " + lowner.getName(true);
		}
		if (prev != null && prev.getMnemonicString().equals("RET")) {
			return "func:unreferenced code after a RET (separate routine)";
		}
		return "absorb:" + hex(lowner.getEntryPoint().getOffset()) + ":unreferenced code inside extent (after " +
			(prev == null ? "data/undefined" : "'" + prev + "'") + ")";
	}

	boolean stillCandidate(Cand c) {
		Address a = toAddr(c.a);
		if (c.undef) {
			return listing.getInstructionContaining(a) == null && listing.getDefinedDataContaining(a) == null;
		}
		return listing.getInstructionAt(a) != null && fm.getFunctionContaining(a) == null;
	}

	/** disassemble every undefined candidate first, so references from not-yet-disassembled code
	 *  exist before anything is classified */
	void predisassemble() throws Exception {
		Set<Long> tried = new HashSet<>();
		for (int pass = 0; pass < 50; pass++) {
			int n = 0;
			for (Cand c : candidates()) {
				if (!c.undef || tried.contains(c.a) || OVERRIDE.getOrDefault(c.a, "").startsWith("data")) {
					continue;
				}
				tried.add(c.a);
				if (disassemble(toAddr(c.a)) && listing.getInstructionAt(toAddr(c.a)) != null) {
					n++;
				}
				else {
					rep.println("FAIL disassemble " + hex(c.a));
				}
			}
			rep.println("predisassemble pass " + pass + ": " + n);
			if (n == 0) {
				break;
			}
		}
	}

	Set<Long> newFuncs = new TreeSet<>();

	/** passes: create all new functions first (they change what is "inside a function"), absorb
	 *  only when a pass finds no new function. */
	void phase3Loop() throws Exception {
		predisassemble();
		for (int pass = 0; pass < 200; pass++) {
			TreeMap<Long, Function> e = entries();
			List<Cand> cs = candidates();
			List<Cand> fc = new ArrayList<>(), ac = new ArrayList<>();
			Map<Cand, String> dec = new HashMap<>();
			for (Cand c : cs) {
				if (failed.contains(c.a)) {
					continue;
				}
				String d = classify(e, c);
				dec.put(c, d);
				(d.startsWith("absorb") ? ac : fc).add(c);
			}
			if (!fc.isEmpty()) {
				// 1) placeholders (first instruction only) for every new entry, 2) then bodies, so no
				//    new function swallows another one's entry
				List<Function> created = new ArrayList<>();
				Map<Function, Cand> cof = new HashMap<>();
				for (Cand c : fc) {
					if (!stillCandidate(c)) {
						continue;
					}
					Function f = createPlaceholder(c, dec.get(c));
					if (f == null) {
						failed.add(c.a);
						continue;
					}
					created.add(f);
					cof.put(f, c);
				}
				for (Function f : created) {
					CreateFunctionCmd.fixupFunctionBody(currentProgram, fm.getFunctionAt(f.getEntryPoint()),
						monitor);
				}
				// a new body must not run past the next entry (it would swallow a tail-jump target
				// that is still an orphan); the clipped code is classified again next pass
				TreeMap<Long, Function> e2 = entries();
				Map<Function, String> clipNote = new HashMap<>();
				for (Function f : created) {
					Function g = fm.getFunctionAt(f.getEntryPoint());
					if (g == null || g.isThunk()) {
						continue;
					}
					long en = g.getEntryPoint().getOffset();
					Long nx = e2.higherKey(en);
					AddressSet ext = new AddressSet(toAddr(en), toAddr((nx == null ? textEnd : nx) - 1));
					AddressSetView out = g.getBody().subtract(ext);
					if (!out.isEmpty()) {
						g.setBody(g.getBody().intersect(ext));
						clipNote.put(f, " (clipped " + bodyStr(out) + " past next entry)");
					}
				}
				for (Function f : created) {
					Address ea = f.getEntryPoint();
					Function g = fm.getFunctionAt(ea);
					Cand c = cof.get(f);
					String d = dec.get(c);
					if (c.a == SIG_FUNC && g.getName().startsWith("FUN_")) {
						g.setName("__except_handler3", SourceType.USER_DEFINED);
					}
					change(c.a, "new_function", (c.undef ? "undisassembled " : "code outside any function ") +
						bytesAt(c.a, 8) + " in extent of " + nameOf(extentOwner(e, c.a)),
						d.substring(5) + " -> " + fdesc(g) + (g.isThunk() ? " (thunk)" : "") +
							clipNote.getOrDefault(f, ""));
					newFuncs.add(c.a);
				}
				rep.println("pass " + pass + ": func " + created.size() + "/" + fc.size());
				rep.flush();
				if (!created.isEmpty()) {
					continue;
				}
			}
			if (ac.isEmpty()) {
				break;
			}
			int done = 0;
			for (Cand c : ac) {
				if (!stillCandidate(c)) {
					continue;
				}
				String d = classify(entries(), c); // bodies change as we go
				if (!d.startsWith("absorb")) {
					continue; // next pass creates it
				}
				boolean ok;
				try {
					ok = applyAbsorb(c, d);
				}
				catch (Exception ex) {
					rep.println("FAIL " + hex(c.a) + " " + d + ": " + ex);
					ok = false;
				}
				if (ok) {
					done++;
				}
				else {
					failed.add(c.a);
				}
			}
			rep.println("pass " + pass + ": absorb " + done + "/" + ac.size());
			rep.flush();
			if (done == 0) {
				break;
			}
		}
	}

	Function createPlaceholder(Cand c, String d) throws Exception {
		Address a = toAddr(c.a);
		if (d.startsWith("data")) {
			change(c.a, "skip_data", bytesAt(c.a, 8), d.substring(5));
			return null;
		}
		Instruction ins = listing.getInstructionAt(a);
		if (ins == null && (!disassemble(a) || (ins = listing.getInstructionAt(a)) == null)) {
			rep.println("FAIL disassemble " + hex(c.a) + " (" + d + ")");
			return null;
		}
		try {
			return fm.createFunction(null, a, new AddressSet(ins.getMinAddress(), ins.getMaxAddress()),
				SourceType.DEFAULT);
		}
		catch (Exception ex) {
			rep.println("FAIL create function " + hex(c.a) + " (" + d + "): " + ex);
			return null;
		}
	}

	boolean applyAbsorb(Cand c, String d) throws Exception {
		Address a = toAddr(c.a);
		String[] p = d.split(":", 3);
		if (c.undef && (!disassemble(a) || listing.getInstructionAt(a) == null)) {
			rep.println("FAIL disassemble " + hex(c.a) + " (" + d + ")");
			return false;
		}
		Function t = fm.getFunctionAt(toAddr(Long.parseLong(p[1], 16)));
		if (t == null) {
			rep.println("FAIL absorb target missing " + d);
			return false;
		}
		AddressSet add = flowSet(a);
		if (add.isEmpty()) {
			rep.println("FAIL absorb " + hex(c.a) + " nothing to add (" + d + ")");
			return false;
		}
		String before = fdesc(t);
		t.setBody(new AddressSet(t.getBody()).union(add));
		explicitlyChanged.add(t.getEntryPoint().getOffset());
		change(c.a, c.undef ? "disasm_add_to_body" : "add_to_body",
			(c.undef ? "undisassembled " : "code outside any function ") + bytesAt(c.a, 8) + " | " + before,
			p[2] + " -> added " + bodyStr(add) + " to " + t.getName(true));
		return true;
	}

	String nameOf(Function f) {
		return f == null ? "-" : f.getName(true);
	}

	String bytesAt(long a, int n) throws Exception {
		byte[] b = new byte[n];
		mem.getBytes(toAddr(a), b);
		StringBuilder sb = new StringBuilder();
		for (byte x : b) {
			sb.append(String.format("%02x", x & 0xff));
		}
		return sb.toString();
	}

	/** instructions reachable from a by fall-through/jumps (no calls) that are in no function and
	 *  are not function entries. */
	AddressSet flowSet(Address a) {
		AddressSet s = new AddressSet();
		Deque<Address> work = new ArrayDeque<>();
		work.add(a);
		while (!work.isEmpty()) {
			Address x = work.pop();
			if (s.contains(x) || !textSet.contains(x)) {
				continue;
			}
			Instruction ins = listing.getInstructionAt(x);
			if (ins == null || fm.getFunctionContaining(x) != null) {
				continue;
			}
			if (!x.equals(a) && fm.getFunctionAt(x) != null) {
				continue;
			}
			s.add(ins.getMinAddress(), ins.getMaxAddress());
			FlowType ft = ins.getFlowType();
			if (ft.hasFallthrough() || ft.isCall()) {
				Address fall = ins.getFallThrough();
				if (fall != null) {
					work.add(fall);
				}
			}
			if (!ins.getMnemonicString().equals("CALL")) {
				for (Address t : ins.getFlows()) {
					work.add(t);
				}
			}
		}
		return s;
	}

	// ------------------------------------------------------------------ phase 4
	void phase4Report(int nBefore) throws Exception {
		TreeMap<Long, Function> e = entries();
		rep.println("functions in .text after: " + e.size() + " (delta " + (e.size() - nBefore) + ")");
		rep.println("changes: " + counts);
		rep.println();
		rep.println("remaining candidates (undisassembled non-pad / code outside functions):");
		for (Cand c : candidates()) {
			rep.println("  " + hex(c.a) + "-" + hex(c.runEnd) + (c.undef ? " undef " : " orphan ") +
				bytesAt(c.a, 8) + " in extent of " + nameOf(extentOwner(e, c.a)) + " -> " + classify(e, c));
		}
		rep.println();
		rep.println("bodies leaving [entry, next entry):");
		for (Map.Entry<Long, Function> en : e.entrySet()) {
			Long nx = e.higherKey(en.getKey());
			long end = nx == null ? textEnd : nx;
			AddressSetView out = en.getValue().getBody()
				.subtract(new AddressSet(toAddr(en.getKey()), toAddr(end - 1)));
			if (!out.isEmpty() && !en.getValue().isThunk()) {
				rep.println("  " + hex(en.getKey()) + " " + en.getValue().getName(true) + " end=" + hex(end) +
					" outside=" + bodyStr(out));
			}
		}
		rep.println();
		rep.println("pre-existing functions whose body changed without an explicit logged action:");
		for (Map.Entry<Long, AddressSet> o : origBodies.entrySet()) {
			Function f = fm.getFunctionAt(toAddr(o.getKey()));
			if (f == null) {
				if (!explicitlyChanged.contains(o.getKey())) {
					rep.println("  REMOVED " + hex(o.getKey()) + " " + origNames.get(o.getKey()));
				}
				continue;
			}
			if (!f.getName(true).equals(origNames.get(o.getKey()))) {
				rep.println("  RENAMED " + hex(o.getKey()) + " " + origNames.get(o.getKey()) + " -> " + f.getName(true));
			}
			if (!f.getBody().equals(o.getValue()) && !explicitlyChanged.contains(o.getKey())) {
				rep.println("  " + hex(o.getKey()) + " " + f.getName(true) + " was " + bodyStr(o.getValue()) +
					" now " + bodyStr(f.getBody()));
				change(o.getKey(), "body_changed_by_new_function", bodyStr(o.getValue()), bodyStr(f.getBody()));
			}
		}
		rep.println();
		rep.println("indirect jumps (switch) in new functions -- check their tables are defined:");
		for (long a : newFuncs) {
			Function f = fm.getFunctionAt(toAddr(a));
			if (f == null) {
				continue;
			}
			for (Instruction ins : listing.getInstructions(f.getBody(), true)) {
				if (ins.getMnemonicString().equals("JMP") && ins.getFlowType().isComputed()) {
					rep.println("  " + hex(a) + " " + f.getName() + " " + ins.getAddress() + " " + ins +
						" flows=" + ins.getFlows().length);
				}
			}
		}
	}

	long peVirtualEnd(Address secStart) {
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
}
