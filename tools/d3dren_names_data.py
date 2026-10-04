r"""Hand curated rows for config/d3dren/names_proposal.csv (merged by d3dren_names_build.py after the computed core).

Row = (address, name, kind, source_file, evidence, confidence, provenance).
Rules: high = identity and exact name certain; medium = identity certain, name probable; low/guess_ = guess.
"""

ROWS = [
    # ---- cross-binary byte matches against lithtech.exe (relocation-masked, tools/d3dren_names_xmatch.py) --------
    (0x10001000, '??_H@YGXPAXIHP6EX0@Z@Z', 'func', '', 'masked bytes identical (48 bytes) to lithtech.exe 0x00401000 `vector constructor iterator` (compiler generated helper)', 'medium', 'exe-decomp'),
    (0x10009020, 'MatMul', 'func', 'ltmatrix', 'masked bytes identical (544 bytes, 208 insns) to lithtech.exe 0x0043bde0 ?MatMul@@YAXPAVLTMatrix@@00@Z (SDK ltmatrix.h)', 'high', 'exe-decomp'),
    (0x1000bcf3, 'MatVMul', 'func', 'ltmatrix', 'mnemonic sequence identical to lithtech.exe 0x00428120 ?MatVMul@@YAXPAV?$_CVector@M@@PAVLTMatrix@@0@Z (92 vs 96 bytes, register allocation differs)', 'medium', 'exe-decomp'),
    (0x10030ed1, 'i_FindIntersectionsHPoly', 'func', 'intersect_line', 'mnemonic sequence and size (135) identical to lithtech.exe 0x00437ef3 ?i_FindIntersectionsHPoly@@YAXPAVWorldBsp@@...', 'high', 'exe-decomp'),
    (0x10030f58, 'IntersectRequest::IntersectRequest', 'func', 'intersect_line', 'masked bytes identical (53 bytes) to lithtech.exe 0x00437f7a ??0IntersectRequest@@QAE@XZ', 'high', 'exe-decomp'),
    (0x10030f8d, 'i_FindIntersections', 'func', 'intersect_line', 'mnemonic sequence and size (76) identical to lithtech.exe 0x00437faf ?i_FindIntersections@@YAXPAVWorldBsp@@...', 'high', 'exe-decomp'),
    (0x10030fd9, 'i_ISCallback', 'func', 'intersect_line', 'masked bytes identical (104 bytes) to lithtech.exe 0x00437ffb ?i_ISCallback@@YAIPAVWorldTreeObj@@PAX@Z', 'high', 'exe-decomp'),
    (0x1003b520, 'sb_Init', 'func', 'struct_bank', 'masked bytes identical (80) to lithtech.exe 0x004b26b0 ?sb_Init@@YAXPAUStructBank_t@@KK@Z; StdLith struct_bank.h', 'high', 'exe-decomp'),
    (0x1003b570, 'sb_Init2', 'func', 'struct_bank', 'masked bytes identical (48) to lithtech.exe 0x004b2700 sb_Init2', 'high', 'exe-decomp'),
    (0x1003b5a0, 'sb_Term2', 'func', 'struct_bank', 'masked bytes identical (64) to lithtech.exe 0x004b2730 sb_Term2; calls dfree 0x10012d44', 'high', 'exe-decomp'),
    (0x1003b5e0, 'sb_Term', 'func', 'struct_bank', 'masked bytes identical (16) to lithtech.exe 0x004b2770 sb_Term (tail-calls sb_Term2)', 'high', 'exe-decomp'),
    (0x1003b5f0, 'sb_AllocateNewStructPage', 'func', 'struct_bank', 'called by sb_Init2 exactly like lithtech.exe 0x004b2780 sb_AllocateNewStructPage (same size 96, allocator = dalloc 0x10012ce5)', 'high', 'exe-decomp'),
    (0x1003b6d0, 'MoArray_FindElementMemcmp', 'func', 'dynarray', 'masked bytes identical (64) to lithtech.exe 0x004b3410 ?MoArray_FindElementMemcmp@@YAKPBX0KK@Z', 'high', 'exe-decomp'),
    (0x100109fd, 'DDPFToPFormat', 'func', 'd3d_utils', 'same code as lithtech.exe 0x00426ac0 ?DDPFToPFormat@@YAXPAU_DDPIXELFORMAT@@PAVPFormat@@@Z (src/client/cutil.cpp): calls PFormat::Init(type 2+(bpp!=16), bits, masks) from DDPIXELFORMAT dwRGBBitCount/dwRBitMask/..', 'high', 'exe-decomp'),
    # ---- strings that name the function -----------------------------------------------------------------------
    (0x10010a24, 'r_GetBufferFormatOfSurface', 'func', 'd3d_init', 'd3d_Init prints "r_GetBufferFormatOfSurface failed." when this returns 0: GetPixelFormat (surface +0x54) then DDPFToPFormat', 'high', 'string'),
    (0x10020360, 'r_TransferTexture', 'func', 'd3d_texture', 'contains "r_TransferTexture: mipmap count doesn\'t match!" and "Uploading a (%dx%d) texture" (Jupiter d3d_texture.cpp d3d_TransferTexture)', 'high', 'string'),
    (0x1003689c, 'FormatMgr::ConvertPixels', 'func', 'pixelformat', 'called by the screenshot code with a FMConvertRequest ("ScreenShot: FormatMgr::ConvertPixels returned %d."); dispatches through the Convert table at 0x1004bfb8 by source/dest PFormat types (engine 0x0046c490 ConvertPixels is the same method in another revision)', 'high', 'string'),
    (0x10036761, 'FMConvertRequest::FMConvertRequest', 'func', 'pixelformat', 'constructor of the request built on the stack by ScreenShot / texture upload before FormatMgr::ConvertPixels (embeds two PFormat at +0x28/+0x60 with vftable 0x100461d8)', 'medium', 'callgraph'),
    (0x10038cf0, 'VisibleSet::Init', 'func', 'tagnodes', 'initialises the VS_* AllocSets; its failure prints "VisibleSet::Init failed (invalid object list size?)" in d3d_Init (Jupiter tagnodes.cpp VisibleSet::Init)', 'high', 'string'),
    (0x10039e00, 'd3d_GetVisibleSet', 'func', 'tagnodes', 'returns &g_VisibleSet (0x100937a8), 31 callers; Jupiter tagnodes.cpp d3d_GetVisibleSet', 'high', 'jupiter'),
    (0x1002a693, 'DrawPolyMgr::DrawPolyAdditionalPass', 'func', 'drawpolymgr', 'contains "DrawPolyMgr::DrawPolyAdditionalPass: nQVerts != nVertices"', 'high', 'string'),
    (0x10022ab6, 'BaseObjectSet::Add', 'func', 'tagnodes', 'prints "Set \'%s\' overflowed" via AddDebugMessage(1, ...) like Jupiter tagnodes.h BaseObjectSet::Add', 'medium', 'jupiter'),

    # ---- object handler table (Jupiter drawobjects.cpp g_ObjectHandlers, Talon stride 0x14) ------------------------
    (0x1004ba78, 'g_ObjectHandlers', 'data', 'drawobjects', '11 entries x 0x14 bytes {ModuleInit, ModuleTerm, bModuleInitted, PreFrameFn, ProcessObjectFn} indexed by object type (OT_NORMAL..OT_CANVAS); same table as Jupiter drawobjects.cpp g_ObjectHandlers (Talon has no VolumeEffect entry, no m_bCheckWorldVisibility/m_GetDims)', 'high', 'jupiter'),
    (0x100285e0, 'd3d_InitObjectModules', 'func', 'drawobjects', 'loops g_ObjectHandlers calling ModuleInit (+0) and setting bModuleInitted (+8)=1; called from the post-device-init path 0x1001b870; Jupiter d3d_InitObjectModules', 'high', 'jupiter'),
    (0x10028610, 'd3d_TermObjectModules', 'func', 'drawobjects', 'loops g_ObjectHandlers calling ModuleTerm (+4) when bModuleInitted, then clears it; called from 0x1001b840 (d3d_Term path); Jupiter d3d_TermObjectModules', 'high', 'jupiter'),
    (0x10028640, 'd3d_InitObjectQueues', 'func', 'drawobjects', 'loops g_ObjectHandlers calling PreFrameFn (+0xc); called once per frame from 0x10014a40; Jupiter d3d_InitObjectQueues', 'high', 'jupiter'),
    (0x10028660, 'd3d_FlushObjectQueues', 'func', 'drawobjects', 'times each draw phase with the RenderStruct tick counters, draws solid world models/polygrids/canvases/models, then fills a static ObjectDrawList (12-byte {obj, drawfn, dist} entries, guard DAT_1006b930, atexit 0x10028960) sorted by distance and draws translucents: Jupiter drawobjects.cpp d3d_FlushObjectQueues', 'high', 'jupiter'),
    (0x10024ceb, 'd3d_ProcessModel', 'func', 'drawmodel', 'g_ObjectHandlers[OT_MODEL].ProcessObjectFn: adds the object to VisibleSet VS_MODELS (+0x144) / VS_MODELS_TRANSLUCENT (+0x164) / VS_MODELS_CHROMAKEY (+0x184) by flags; Jupiter drawmodel.cpp d3d_ProcessModel', 'high', 'jupiter'),
    (0x1000b6ae, 'd3d_ModelPreFrame', 'func', 'drawmodel', 'g_ObjectHandlers[OT_MODEL].PreFrameFn; Jupiter drawmodel.cpp d3d_ModelPreFrame is the OT_MODEL PreFrameFn', 'high', 'jupiter'),
    (0x1002f360, 'd3d_ProcessWorldModel', 'func', 'drawworldmodel', 'g_ObjectHandlers[OT_WORLDMODEL] and [OT_CONTAINER].ProcessObjectFn (Jupiter shares it for containers too): adds to VS_WORLDMODELS / _TRANSLUCENT / _CHROMAKEY sets', 'high', 'jupiter'),
    (0x1002e9f0, 'd3d_ProcessSprite', 'func', 'drawsprite', 'g_ObjectHandlers[OT_SPRITE].ProcessObjectFn: adds to VS_SPRITES (+0x1a4) or VS_SPRITES_NOZ (+0x1c4) by flag 0x10 at +0x88; Jupiter drawsprite.cpp d3d_ProcessSprite', 'high', 'jupiter'),
    (0x10023860, 'd3d_ProcessLight', 'func', 'drawlight', 'g_ObjectHandlers[OT_LIGHT].ProcessObjectFn: adds to VS_LIGHTS (+0x244) when DynamicLight (DAT_1004875c) is on; Jupiter drawlight.cpp d3d_ProcessLight', 'high', 'jupiter'),
    (0x100296f7, 'd3d_ProcessParticles', 'func', 'drawparticles_A', 'g_ObjectHandlers[OT_PARTICLESYSTEM].ProcessObjectFn: BaseObjectSet::Add on VS_PARTICLESYSTEMS (+0x2c4); Jupiter drawparticles_A.cpp d3d_ProcessParticles', 'high', 'jupiter'),
    (0x1002cc90, 'd3d_ProcessPolyGrid', 'func', 'drawpolygrid', 'g_ObjectHandlers[OT_POLYGRID].ProcessObjectFn: adds to VS_POLYGRIDS (+0x264); Jupiter drawpolygrid.cpp d3d_ProcessPolyGrid', 'high', 'jupiter'),
    (0x1002ce30, 'd3d_TermPolyGridDraw', 'func', 'drawpolygrid', 'g_ObjectHandlers[OT_POLYGRID].ModuleTerm (frees the polygrid vertex buffer 0x10070448..); Jupiter drawpolygrid.cpp d3d_TermPolyGridDraw', 'medium', 'jupiter'),
    (0x10023ce0, 'd3d_ProcessLineSystem', 'func', 'drawlinesystem', 'g_ObjectHandlers[OT_LINESYSTEM].ProcessObjectFn: adds to VS_LINESYSTEMS (+0x2a4); Jupiter drawlinesystem.cpp d3d_ProcessLineSystem', 'high', 'jupiter'),
    (0x10022a9f, 'd3d_ProcessCanvas', 'func', 'draw_canvas', 'g_ObjectHandlers[OT_CANVAS].ProcessObjectFn: BaseObjectSet::Add on VS_CANVASES (+0x2e4); Jupiter draw_canvas.cpp d3d_ProcessCanvas', 'high', 'jupiter'),

    # ---- CRT init/exit tables (positions proven by __cinit 0x1003d681 and __doexit 0x1003d6ce) -----------------------
    (0x10048000, '___xc_a', 'data', 'lib/VC6_LIBCMT_1998/crt0dat', 'start of the C++ initialiser table: __cinit passes 0x10048000..0x1004836c to _initterm; the 218 entries from 0x10048004 are the renderer static initialisers', 'high', 'crt-lib'),
    (0x1004836c, '___xc_z', 'data', 'lib/VC6_LIBCMT_1998/crt0dat', 'end of the C++ initialiser table (_initterm second argument in __cinit)', 'high', 'crt-lib'),
    (0x10048370, '___xi_a', 'data', 'lib/VC6_LIBCMT_1998/crt0dat', 'start of the C initialiser table (entries 1003b7fa ___onexitinit, 1003fe7a, 10044a76, 10044b61)', 'high', 'crt-lib'),
    (0x10048384, '___xi_z', 'data', 'lib/VC6_LIBCMT_1998/crt0dat', 'end of the C initialiser table', 'high', 'crt-lib'),
    (0x10048388, '___xp_a', 'data', 'lib/VC6_LIBCMT_1998/crt0dat', 'pre-terminator table start (__doexit 0x1003d6ce)', 'high', 'crt-lib'),
    (0x10048390, '___xp_z', 'data', 'lib/VC6_LIBCMT_1998/crt0dat', 'pre-terminator table end', 'high', 'crt-lib'),
    (0x10048394, '___xt_a', 'data', 'lib/VC6_LIBCMT_1998/crt0dat', 'terminator table start (__doexit)', 'high', 'crt-lib'),
    (0x1004839c, '___xt_z', 'data', 'lib/VC6_LIBCMT_1998/crt0dat', 'terminator table end', 'high', 'crt-lib'),

    (0x10010e0d, 'd3d_IsNullRenderOn', 'func', 'common_init', 'GetParameter("nullrender") then GetParameterValueFloat != 0; same as Jupiter common_init.cpp d3d_IsNullRenderOn; called from d3d_Init for the windowed/null-render window placement', 'high', 'jupiter'),

    (0x10057aa0, 'g_bRunWindowed', 'data', 'common_stuff', 'set in d3d_Init from the "windowed" console parameter (atoi == 1) in the same order as Jupiter d3d_Init g_bRunWindowed; used by the window-placement code', 'high', 'jupiter'),
    (0x10057998, 'g_hWnd', 'data', 'common_stuff', 'd3d_Init stores RenderStructInit+0x218 (m_hWnd) right after the windowed flag, like Jupiter g_hWnd; passed to SetWindowPos / SetCooperativeLevel', 'high', 'jupiter'),
    (0x10057e34, 'g_ScreenWidth', 'data', 'common_stuff', 'd3d_Init stores pInit->m_Mode.m_Width (+0x208) here (Jupiter g_ScreenWidth); later divided into the RenderStruct width', 'high', 'jupiter'),
    (0x10057f28, 'g_ScreenHeight', 'data', 'common_stuff', 'd3d_Init stores pInit->m_Mode.m_Height (+0x20c) here (Jupiter g_ScreenHeight)', 'high', 'jupiter'),

    # ---- reconciliation of agent disagreements (lead decisions) ---------------------------------------------------
    (0x1002970e, 'd3d_TestAndDrawPS', 'func', 'drawparticles_A', 'queued per particle system by d3d_QueueTranslucentParticles (callback 0x100298da); radius = SystemRadius*max(scale), sets FLAG_INTERNAL1, d3d_GetBlendStates + StateSets, then draws: identical flow to Jupiter drawparticles_A.cpp d3d_TestAndDrawPS', 'medium', 'jupiter'),
    (0x10008ce0, 'd3d_DrawParticleSystem', 'func', 'drawparticles', 'called from d3d_TestAndDrawPS: sets up the system transform (d3d_SetupTransformation), counts ticks in SceneDesc+0x14 (m_pTicks_Render_ParticleSystems), batches particles through 0x10009370 (d3d_DrawParticleBatch): Jupiter drawparticles.cpp d3d_DrawParticleSystem/d3d_DrawParticles', 'medium', 'jupiter'),
    (0x100298b6, 'd3d_QueueTranslucentParticles', 'func', 'drawparticles_A', 'if DrawParticles: BaseObjectSet::Draw(VS_PARTICLESYSTEMS, ..., d3d_TestAndDrawPS) like Jupiter drawparticles_A.cpp d3d_QueueTranslucentParticles; first Queue* call of d3d_FlushObjectQueues', 'high', 'jupiter'),

    (0x10057e30, 'guess_g_SysMemParam', 'data', 'common_init', 'd3d_Init stores ftol(GetParameterValueFloat(GetParameter("SysMem"))) here (engine-defined console parameter, string 0x100486fc)', 'low', 'invented'),
]
