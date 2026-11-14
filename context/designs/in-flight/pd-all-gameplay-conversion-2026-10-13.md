# All gameplay conversion: scope and coverage plan

2026-10-26 gun source contract correction following N0828 audit. asset_source_debug defaults ASSET_NONE; its source-only predicate is debug-selected, so the old non-FileProvider return0 allowed normal legacy model loading. fileProviderPath(nonfile)=NULL and modelSourceResolvePath(NULL) rejects; no supported public source can use that branch. Owned gunSourceRouteSource now fails unsupported/non-external requests and all exits return1; retained and supported external routes preserved. This intentionally supersedes original donor continuation under c3844/public-source constraint, not a compatibility claim. Native legacy block untouched but now unreachable through source routing. Updated source-route donor fixtures and added one explicit unsupported-provider case UNRUN. Independent review found no defect. Guard q_20261026205529526_c939af23/diff PASS; other8 pins and ORACLE suffix unchanged. 531 bindings/98 programs;47 whole entries/146 authored cases sinceOct23;zero accepted/complete conversions. No compiler/gameplay execution. Next audit/remove unreachable legacy MODEL/TEXTURES/DLS machinery and obsolete state producers, preserving source artifacts; public graph source/dependencies/ALL closure remain open. Latest .claude/gameplay-gun-source-closed-20261026/source-receipt.json. N-0829

2026-10-26 corrected gun TEXTURES stage COMPONENT connected through GGV_GUN_TEXTURES. Three graph iteration gates retain N0826 cursor-before-budget fix, visit at most3 entries and leave exact-multiple completion to next tick. Captured model/player, fresh texture array after texLoad, s16 count/u32 cursor/u8 mark preserved. uintptr texture words use full64-bit representation; sign gate plus positive-limit comparison implements unsigned cutoff without32-bit truncation. Native limit verified65535. Three donor cases authored UNRUN cover counts0..7, mixed resident entries, exact3 timing, all unsigned high-bit regions and callback array/target changes. Source/fixture review found no correctness/lifetime defect; reviewer suggested count5, added. Guard q_20261026205120546_9a0732a2 PASS; scoped diff/parent delta/ORACLE suffix/six non-core pins preserved. 531 bindings/98 programs;47 whole entries/145 authored cases sinceOct23;zero accepted/complete conversions. No compiler/gameplay execution. Legacy MODEL/DLS, child helper qualification, public authored graph source and ALL closure remain open. Latest .claude/gameplay-gun-textures-20261026/source-receipt.json. N-0827

2026-10-26 pre-conversion TEXTURES root repair: persistent nexttexturetoload now increments before the three-entry budget return, removing the documented third-entry revisit. Both loadable and resident/skipped entries advance; exact multiples3 retain existing next-tick DLS transition. Only this statement/order/comment changed; before/after native donor artifacts retained. Source review confirms fix by inspection. Static guard q_20261026204705964_1b110bb3 and diff PASS; other9 source pins unchanged. Regression requirements authored, no new executable cases this checkpoint; 525 bindings/97 programs,47 whole entries/142 authored cases unchanged. No compiler/gameplay execution; not runtime validated. Next graph conversion must preserve uintptr_t texture/pointer union full unsigned width, s16 numtexconfigs, u32 cursor, captured model/player and fresh texture-array access after texLoad. Legacy MODEL/DLS/dependencies, public graph source and ALL closure remain open. Latest .claude/gameplay-texture-cursor-fix-20261026/source-receipt.json. N-0826

2026-10-26 gun MODEL source-routing COMPONENT connected through GGV_GUN_SOURCE_ROUTE. Preserves usable-handle and external-source gates; failed external load clears u8 g_LoadType before failure handling; successful load sets LOADED then conditionally copies fresh hand file/ID/typed handle/generation; FileProvider rejection cannot fall into preprocessed loading. Return0 retains the existing non-FileProvider continuation, explicitly NOT runtime ROM fallback acceptance. Three donor cases authored UNRUN cover failure order, complete64-byte ID tail/typed handle transfer, u16/U32 widths and captured player with fresh callback source fields. Reviewer found fixture loadType int versus nativeu8; corrected declaration and write narrowing, production already narrows. No other source/fixture defect found. Guard q_20261026204443749_7072fd99/scoped diff PASS after correction; exact parent delta, ORACLE suffix and six non-core pins preserved. 525 bindings/97 programs;47 whole entries/142 authored cases sinceOct23;zero accepted/complete conversions. No compiler/gameplay execution. Legacy path classification, TEXTURES cursor repair/conversion, DLS/helpers and public graph source/ALL closure open. Latest .claude/gameplay-gun-source-20261026/source-receipt.json. N-0825

2026-10-26 retained gun-model MODEL component connected through GGV_GUN_RETAINED_LOAD. Preserves generation and ID query before target/null-ID/strcmp OR checks, failure exit, captured model identity through RW allocation with fresh destination pointer publication, retained clear, U32 texture/allocsize/loadedsize zeroing (allocsize before loadedsize), LOADED before captured-generation hash/ID log. Three donor cases authored UNRUN cover actual strcmp/empty IDs, pointer identity/redirect, full-width counters and per-write traces. Independent source/fixture review found no concrete defect. Guard q_20261026204106642_560d4d6d/scoped diff PASS; exact parent delta, ORACLE suffix and six non-core pins preserved. 521 bindings/96 programs;47 whole entries/139 authored cases sinceOct23;zero accepted/complete conversions. No compiler/gameplay execution. External MODEL route, legacy preprocessed path classification, TEXTURES cursor repair/conversion, DLS/helpers and public graph source/ALL closure remain open. Provider/generation/allocator integration remains unqualified. Latest .claude/gameplay-gun-retained-20261026/source-receipt.json. N-0824

2026-10-26 gun-loader FAILED-state recovery COMPONENT connected through GGV_GUN_FAILURE_RECOVERY. Full U32 catalog generation comparison, diagnostic before captured-player FLUX reset, no stored-generation update; native parent return preserved. Two donor cases authored UNRUN, generation high bits and captured player through synthetic query/log callbacks. Independent source/fixture review found no concrete defect. Guard q_20261026203742102_028d29df and scoped diff PASS; exact parent delta, ORACLE suffix and six non-core pins preserved. 515 bindings/95 programs;47 whole entries/136 authored cases sinceOct23;zero accepted/complete conversions. No compiler/gameplay execution. Retained and external MODEL loading, legacy preprocessed path (not acceptable runtime ROM fallback), documented TEXTURES cursor-repeat bug, DLS and all dependencies remain open. Public source constraint re-read; no new asset representation introduced. Latest .claude/gameplay-gun-failure-20261026/source-receipt.json. N-0823

2026-10-26 whole bgunTickMasterLoad parent source-connected through GGV_MASTER_LOAD_PARENT. Preserves captured admission/preparation, persistent tuple-key diagnostic gate, loaded/type and file/runtime short circuits, hands/unarmed adaptation, exclusive child stage dispatch, CARTS refusal and versioned no-file shortcut. Existing graph components retained. Key bit construction qualified to native-defined weapon shift domain; weaponHasFlag remains a lookup boundary, no fabricated mutations of unsequenced diagnostic arguments. Four parent cases authored UNRUN with scripted children, persistent state, current-player changes and exact call witness. Nested child integration/provider/model acceptance remains open. Independent production/fixture source review found no concrete defect. Guard q_20261026203436173_eab0d983/scoped diff PASS; ORACLE07 suffix and six non-core pins preserved. Bindings515/programs94;whole source-connected entries47 sinceOct23;134 authored cases;zero accepted/complete conversions. No compiler/gameplay execution. Gun loader and helper/dependency conversion, semantic census, public editable graph source and ALL closure remain incomplete. Latest .claude/gameplay-master-parent-20261026/source-receipt.json. N-0822

2026-10-26 master finalization COMPONENT source-connected through GGV_MASTER_FINALIZE. Both hands now graph-dispatch gun/optional hand model initialization, fresh root/cursor command-list calls, captured command pointers, signed bounded sum and byte cursor/unsigned capacity accounting. Preserved strict remaining >50*sizeof(Mtxf) matrix reservation for hand0, hand1 matrix state unchanged, discarded fresh-global capacity call, captured loaded/type/pending writes and fresh-current-player log gate. Typed REF byte offset and three-REF model primitives retain engine child boundary; no hidden branch policy added. Four donor cases authored UNRUN cover explicit order/112-byte offset witnesses, exact 3200 boundary/full U32 scalar capacity, clone-local model/static root witnesses and synthetic callback stress. Model/capacity/allocator dependencies remain unqualified; signed sum cases are native-defined. Independent production/fixture source review found no concrete defect. Parent edit bounded to full finalization block plus four unused locals. Guard q_20261026203108751_8d91862d/scoped diff PASS; ORACLE07 suffix and six non-core pins unchanged. Bindings513/programs93. Whole entries remain46 sinceOct23;130 authored cases;zero accepted/complete conversions. No compiler/gameplay execution. Whole-parent entry diagnostics/outer gates/dispatch, gun loader/dependencies, public editable graph source and ALL closure open. Latest .claude/gameplay-master-finalize-20261026/source-receipt.json. N-0821

2026-10-26 master CARTS-selection COMPONENT source-connected through GGV_MASTER_CARTS_SELECT. Preserved loaded-to-flux reset; cart/null/player-count gates; two-hand ordered function then weapon query including null functions; low-byte s32 function type, s8 ammo index, U32 casing-to-s32 conversion; first eligible casing break including existing cart; MODEL setup and typed queue targets; fresh-global tick then unconditional return when gun state is not FLUX. Four donor cases authored UNRUN, including explicit call-order witnesses and bounded synthetic callback stress. Native array-valid input domains only; this does not qualify source validation or lookup/provider children. Independent production/fixture source review found no concrete defect. Parent edit only selection block plus five unused declarations removed. Guard q_20261026202339823_3a00a4c6/scoped diff PASS; ORACLE07 suffix and six non-core pins unchanged. Bindings499/programs92. Whole entries remain46 sinceOct23;126 authored cases;zero accepted/complete conversions. No compiler/gameplay execution. Final two-hand model setup/command lists/memory accounting and loaded transition, parent entry/dispatch, gun loader/dependencies, public editable graph source and ALL closure open. Latest .claude/gameplay-master-carts-20261026/source-receipt.json. N-0820

2026-10-26 master HANDS-stage COMPONENT source-connected through GGV_MASTER_HANDS_STAGE. Preserved handfile/source-current OR and ID/handle/generation OR short-circuiting, queue/tick refusal exits, captured-player field writes with current-player memory/tick calls, no-hands first-byte/typed-handle reset and unsigned capacity to native signed storage. Four donor cases authored UNRUN cover exact strcmp/handle/generation behavior, native widths, high-bit scalar transfers and callback changes. Independent source review found no concrete defect. Parent changed only at HANDS branch; previous storage-width/header/CMake pins unchanged. Guard q_20261026201701739_ecf8196d/scoped diff PASS; ORACLE07 suffix unchanged. Bindings492/programs91. Whole entries remain46 sinceOct23;122 authored cases;zero accepted/complete conversions. No compiler/gameplay execution. Whole master parent, CARTS/finalization, gun loader/dependencies and public editable graph source/ALL closure open. Latest .claude/gameplay-master-hands-20261026/source-receipt.json. N-0819

2026-10-26 master GUN-stage COMPONENT source-connected through GGV_MASTER_GUN_STAGE. Graph owns hand cursor/capacity transfer, exclusive unarmed/body, V2/retained and static queue paths, failure-before-tick returns, fresh-current gun tick and captured-player LOADED-to-CARTS/reset/log. Queue targets retain N-0817 u8**/u32* repair. Four donor cases authored UNRUN cover exclusive failures, REF/native s32-to-u32 transfer, byte states and callback current/captured distinctions; negative capacities test scalar transfer only, not allocation acceptance. Independent source review found no concrete defect. Parent changed only at this branch; existing target-width/header/CMake pins unchanged. Guard q_20261026201124964_1a320ad2/scoped diff PASS; ORACLE07 suffix unchanged. Bindings490/programs90. Whole entries remain46 sinceOct23;118 authored cases;zero accepted/complete conversions. No compiler/gameplay execution. Whole master parent, HANDS/CARTS/finalization, gun loader/dependencies and public editable graph source/ALL closure open. Latest .claude/gameplay-master-gun-20261026/source-receipt.json. N-0818

2026-10-26 loader storage-width source repair discovered during GUN conversion. uintptr_t capacity targets addressed s32/u32 fields on x64; wide writes could overwrite adjacent master/gun/loadfile state. Queue cursors now u8** and capacities u32*, with matching API/static signatures, all4 production callers and loader pointer conversions; signed hand capacity uses corresponding unsigned alias. Field widths/layout and prior dirty header/CMake content preserved. N-0815/0816 narrow claims. Two actual gunctrl storage/signature regression cases authored UNRUN and connected to explicit pd-tests source list. Independent source review found no concrete defect; guard q_20261026200645567_7fcc32bc PASS; scoped diff and target-list readback PASS. Existing core/parser/fixture source pins and ORACLE07 suffix unchanged. No compiler/gameplay execution. Source repair is NOT runtime validated or a new conversion. Whole entries46, authored cases114, accepted/complete0; bindings488/programs89. Master GUN/HANDS/CARTS/parent and all gameplay/public source closure remain open. Latest .claude/gameplay-loader-target-width-20261026/source-receipt.json. N-0817

2026-10-26 master FLUX-to-HANDS COMPONENT source-connected through GGV_MASTER_FLUX_TRANSITION. Preserved two20-casing passes: matching cart references including null equality are cleared; active clears then writes true for each surviving model. Captured cart/master fields update before fresh-player-gated diagnostic; inputs preserved. Three donor cases authored UNRUN cover null/mixed identity, repeated active writes, captured/current player, casing canaries and diagnostic fields. Independent production/fixture review found no concrete defect. Parent changes bounded to transition call and unused casing local removal. Static guard q_20261026200153205_23dfca34/scoped diff PASS; ORACLE07 suffix unchanged. Bindings488/programs89. Whole entries remain46 sinceOct23;112 authored cases;zero accepted/complete conversions. No compiler/gameplay execution. Whole master loader, remaining hands/gun/carts/finalization, gun loader/dependencies and public editable graph source/ALL closure open. Latest .claude/gameplay-master-flux-20261026/source-receipt.json. N-0814

2026-10-26 master source-preparation COMPONENT source-connected through GGV_MASTER_SOURCE_PREPARE. Graph owns pending weapon capture, three short-circuit body/file/source checks with rollback diagnostic, u16 file outputs, V2 model refresh and independent unarmed/hands source freshness thresholds. Captured player remains distinct from current-player flux target. Three donor cases authored UNRUN cover failures, null/model identity, output widths and callback mutations. Runtime model query treated readonly; no invented mutation of C comparison operands. Independent production/fixture source review found no concrete defect. Retained master tail byte-preserved; whole loader remains OPEN and not counted. Static guard q_20261026195820729_ca4bfd00/scoped diff PASS; ORACLE07 suffix unchanged. Bindings487/programs88. Whole entries remain46 sinceOct23;109 authored cases;zero accepted/complete conversions. No compiler/gameplay execution. Master state dispatch/hands/gun/carts/finalization, gun loader/dependencies and public editable graph source/ALL closure open. Latest .claude/gameplay-master-source-20261026/source-receipt.json. N-0813

2026-10-26 master-loader admission COMPONENT source-connected through GGV_MASTER_LOAD_ADMISSION/bgunMasterLoadReady. Preserved owner OR child-return short-circuit, captured-player pending AND gate, zero-timer admission and signed-byte decrement/readback/reset. Three donor cases authored UNRUN compose actual ownership graph across ticks and callback mutations. Independent source review found no concrete defect. Retained master body byte-preserved; whole master-loader conversion remains OPEN and is not counted. Static guard q_20261026195209875_8f6c5afc/scoped diff PASS; ORACLE07 suffix unchanged. Bindings482/programs87. Whole entries remain46 sinceOct23;106 authored cases;zero accepted/complete conversions. No compiler/gameplay execution. Remaining master/gun loader, dependencies and public editable graph source/ALL closure open. Latest .claude/gameplay-master-admission-20261026/source-receipt.json. N-0812

2026-10-26 bgunEnterFlux source-connected through GGV_ENTER_FLUX. Preserved fresh-player ordered resets after logging, first catalog byte only, complete typed handle zeroing, u32 generation, null model/load pointers, native load states, all20 casing model references then active clear. Handle primitive is one aggregate store with no provider lookup/loading. Three donor cases authored UNRUN include catalog tails, casing boundary canaries/neighbors, callback-selected player and repeated clearing. Independent production/fixture source review found no concrete defect. Static guard q_20261026194826372_44be9c3b/scoped diff PASS; ORACLE07 suffix unchanged. Binding/type/name482; stock/program86. Whole entries46 sinceOct23;103 authored cases;zero accepted/complete conversions. No compiler/gameplay execution. Master/gun loader, dependencies and public editable graph source/ALL gameplay closure remain open. Latest .claude/gameplay-flux-20261026/source-receipt.json. N-0811

2026-10-26 bgunLoadAll source-connected through GGV_LOAD_ALL. Preserved PAL-beta+ Eyespy/inhibit mask, fresh player after flux, pending assignment before type early return, actual owner recheck independent of ownership-child return, lockscreen retry, uncapped tick-before-loaded loop and callback-current loadall clear. Four donor cases authored UNRUN including real GGV_IS_LOADED child composition; required write callback supplied for read-only child. Independent production/fixture review found no concrete defect. Static guard q_20261026194437375_90b563cf/scoped diff PASS; ORACLE07 suffix unchanged. Binding/type/name472; stock/program85. Whole entries45 sinceOct23;100 authored cases;zero accepted/complete conversions. No compiler/gameplay execution. Flux/master/gun loader, native dependencies and public editable graph source/ALL gameplay closure remain open. Latest .claude/gameplay-load-all-20261026/source-receipt.json. N-0810

2026-10-26 bgunChangeGunMem source-connected through GGV_CHANGE_MEMORY_OWNER. Graph owns pair/wild/success diagnostic throttles, post-callback state rechecks, full signed request pair bits, signed-byte timer decrement/readback, u8 owner assignment, exclusive owner switch and captured-player loadall/unlock stores after fresh-global flux/body removal. Four donor cases authored UNRUN with repeated calls, raw boolean fields and u32 multiplayer high bit. Review found/corrected fixed-register type conflicts and nonexistent builder helper; re-review found no concrete remaining defect. Static guard q_20261026194043811_9f46c0f8/scoped diff PASS; ORACLE07 suffix unchanged. Binding/type/name471; stock/program84. Whole entries44 sinceOct23;96 authored cases;zero accepted/complete conversions. No compiler/gameplay execution. Flux/load-all/master loader, dependencies and public editable graph source/ALL gameplay closure remain open. Latest .claude/gameplay-memory-owner-20261026/source-receipt.json. N-0809

2026-10-26 memory control: bgunFreeGunMem, bgunSetGunMemWeapon, bgunTickLoad source-connected through three graph programs. Preserved fresh-player free after logging/unconditional texture reset, captured-player pending assignment with native u8 states/s8 timer/s32 request, callback-fresh signed budget/eight-tick stride. Three donor cases authored UNRUN, bounded to defined loop arithmetic. Source review found/fixed malformed service label; root fixed type-table separator. Static guard q_20261026193522461_b04b1bed/scoped diff PASS; ORACLE07 suffix unchanged. Bindings463/programs83;43 whole entries sinceOct23;92 authored cases;zero accepted/complete conversions. No compiler/gameplay execution. Master loader, ownership transitions, remaining gameplay/dependencies and public editable graph source/ALL closure remain open. Latest .claude/gameplay-memory-control-20261026/source-receipt.json. N-0808

2026-10-26 equip/init source-connected: bgunEquipWeapon, bgunEquipWeapon2, bgunInitHandAnims. Preserved post-log predicate recheck, captured inner owner/fresh outer player, left dual versus non-left clamp, and callback-fresh hand selection with captured model links. Five donor cases authored UNRUN; production/fixture source review and static guard q_20261026193105736_c8c69e00/scoped diff PASS. ORACLE07 suffix unchanged. Bindings456/programs80;40 whole entries sinceOct23;89 authored cases;zero accepted/complete conversions. No compiler/gameplay execution. Public graph source and dependency/ALL gameplay closure remain open. Latest .claude/gameplay-equip-init-20261026/source-receipt.json. N-0807

2026-10-26 bgunFreeWeapon source-connected through GGV_FREE_WEAPON. Preserved captured player/hand and pre-callback clip space, regional JPN/PAL/older counter formulas and u16 storage, fresh loaded/ammo-target reads, source compound assignment, full u32 multiplayer truth, delete/rocket/retire ordering and fresh log gates. No direct inuse write invented from stale comment. Four donor cases authored UNRUN; native arithmetic inputs bounded to defined domains (negative space only PAL). Independent production/fixture/native-width review and static guard q_20261026192125369_02d1acda/scoped diff PASS; original ORACLE07 suffix preserved. Bindings446/programs77. Whole entries37 sinceOct23;84 new authored cases;zero accepted/complete conversions. No compiler/gameplay execution. Latest .claude/gameplay-free-weapon-20261026/source-receipt.json. Equip/init/load/inventory/model/native-child and ALL gameplay/public graph source closure remain open. N-0806

2026-10-26 hand configuration bgun0f0abd30 and ammo-display bgunResetAbmag source-connected through GGV_HAND_CONFIGURE/GGV_ABMAG_RESET. Preserved optional source definitions, right ammo clearing, u32 ammo type to native s8, s16 clip to s32 hand, fresh captured-hand remote check, only first2of12 multipliers, captured gauge targets and alignment. Five donor cases authored UNRUN, including actual graph-child composition. Native declaration audit corrected earlier SwitchParentHost widened fields: hand inuse/ctrl ammotypes s8, ctrl dual/prev/invert/throw one-bit, use timer s16, disguised one-bit; impossible dual2 data removed. Production parent adapter already used native fields; earlier raw-dual fixture claims superseded. Independent production/fixture/type review and static guard q_20261026191051403_7e3b83be/scoped diff PASS. Bindings438/programs76; original ORACLE07 suffix preserved. Whole entries36 sinceOct23;80 new authored cases;zero accepted/complete conversions. No compiler/gameplay execution. Latest .claude/gameplay-hand-config-20261026/source-receipt.json. Free/equip/init/load/inventory/model dependencies and ALL gameplay/public graph source closure remain open. N-0805

2026-10-26 bgunTickSwitch2 whole parent source-connected through GGV_SWITCH_DRIVER, composing the prior hand reset component. Preserves signed diagnostic cadence, diagnostic versus short-circuit readiness calls, captured player/controller/old weapon, fresh global scanner/reset accesses, reserve compound target, fresh pending after memory setup, previous/dual state, captured character before disguise sound, inventory/cycle ordering and raw no-pending dual assignment. Four donor cases authored UNRUN; independent production/fixture source reviews found no concrete defect, then root added retained-dual and device-lookup mutation coverage. Static guard q_20261026185913755_d10553a9 and scoped diff PASS; binding/type/name423 and stock/program74 agree. Original ORACLE07 suffix preserved. Whole entries34 sinceOct23;75 new authored cases;zero accepted/complete conversions. No compiler/gameplay execution. Latest .claude/gameplay-switch-parent-20261026/parent-source-receipt.json. Native hand configuration bgun0f0abd30, free/load/inventory/model dependencies and ALL gameplay/public graph source closure remain open. N-0804

2026-10-26 switch-hand reset component source-connected through GGV_SWITCH_HAND_RESET inside native bgunTickSwitch2. Preserved17 stores, signed controller weapon to unsigned gset byte, upgrade nibbles, captured hand/controller, fresh-global bgun0f0abd30 boundary, captured-hand animInit and fresh audio handles before state/stop. Three donor cases authored UNRUN; independent production/fixture review and static source guard q_20261026184412384_584844d6/scoped diff PASS. Original ORACLE07 suffix preserved; binding/type/name count381, stock/program count73 agree. Whole-entry count remains33;71 new authored cases sinceOct23;zero accepted/complete conversions. Parent switch decisions/disguise/load and ALL gameplay/public-source/dependency closure remain open. No compiler/gameplay execution. Latest .claude/gameplay-switch-parent-20261026/hand-reset-source-receipt.json; same owner continues parent. N-0803

2026-10-26 visibility source checkpoint: bgunExecuteGunVisCommands, bgunSetPartVisible and bgun0f098030 now dispatch typed graph traversal/part storage/ammo visibility programs. Preserved three independent operation checks and callback-fresh fields, u16 command part to s16 setter narrowing, raw s32 visibility storage, captured weapon and fresh s32 clip/loaded queries. Six independent donor cases authored UNRUN, including mutation and40000 clip boundary; child/engine calls remain separately unvalidated. Independent production+fixture source review, static source guard q_20261026183347515_3a412698 and scoped diff PASS. Static binding/type/name count371 and stock/program count72 agree. Whole entries33 since Oct23;68 new authored cases;zero accepted/complete conversions. Original ORACLE07 suffix preserved. No compiler/gameplay execution. Latest .claude/gameplay-visibility-20261026/source-receipt.json. Native switch/load/dependency and ALL gameplay/public graph source closure remain open. N-0802

2026-10-26 N0796 repeat admission source fix: public parser rejects full packed target>=slot (including nonzero high word); valid stock backward loops unchanged. Both catalog generation and loader_pool copy full words without later span checks, so source rejection reaches both routes before materialization. Reader rejection/boundary and edited-public-source-to-production-first-pass graph fixtures authored UNRUN (mirrored scalar transfer; live catalog/provider integration unvalidated). Independent source review and static source guard PASS q_20261026181610038_aba70941; original ORACLE07 test suffix preserved. Whole entries30;62 new authored cases total;zero accepted/complete conversions. No compiler/gameplay execution. Latest .claude/gameplay-repeat-admission-20261026/source-receipt.json. ALL gameplay/public graph source/dependency closure remains open. N-0801

2026-10-26 animation root bgun0f0981e8 now dispatches GGV_ANIMATION_DRIVER, composing converted first traversal, dynamic part accumulation and sound traversal. Setup/RNG/reverse animation/regional increments/model ticking/post-tick bounds are source-connected; invocation cleanup remains outside graph. Callee audit restored legacy tick version guard because modelTickAnim is absent in newer builds. Three root donor cases authored UNRUN; independent source review/static asset guard/scoped diff PASS. Original ORACLE07 suffix preserved. Source-connected whole entries30;60 new authored cases;zero completed/accepted conversions. No compiler/gameplay tests. Latest .claude/gameplay-animation-commands-20261026/driver-source-receipt.json. N0796 public repeat admission, native child/dependency qualification and full ALL-gameplay/public-source closure remain open; same owner continues.

2026-10-26 first animation traversal now dispatches GGV_ANIMATION_FIRST_COMMANDS: initial/zero-corrected frames, ordered visibility accumulation/application, release clamps with repeated speed queries, distinct regional repeat formulas, cursor advancement and callback stores. Checked I64 arithmetic and explicit modern backward/progress checks reject undefined arithmetic/nonterminating repeat inputs; no iteration cap or rollback claim. Four donor/rejection cases authored UNRUN. Independent source review/static guard/diff PASS; original ORACLE07 suffix preserved. Parent setup/model tick remains native, whole-entry count stays29;57 new authored cases,zero accepted conversions. No compiler/gameplay execution. Latest .claude/gameplay-animation-commands-20261026/first-source-receipt.json. N0796 parser admission gap remains open despite runtime failure guard. Same owner continues root setup/tick conversion.

2026-10-26 animation continuation: part lookup/update now dispatches GGV_ANIMATION_PART_COMMAND. Native15-part scratch replaced with checked growable invocation storage; preserves first-seen order, last-match lookup, full-width comparison before s32 insertion narrowing, and strict newer-frame replacement. Failure stops invocation and frees retained storage; no rollback claim. Three cases authored UNRUN: native<=15 parity, separate1024-entry extension, mocked growth failure. Production allocation/cleanup have source review only. Independent source review/static asset guard/scoped diff PASS; original ORACLE07 body preserved. Interpreter setup/visibility traversal/wait/repeat/tick still native, whole-entry count29;53 new authored cases,zero accepted conversions. No compiler/gameplay execution. Latest receipt .claude/gameplay-animation-commands-20261026/part-source-receipt.json. Repeat source-admission gap N0796 remains open. Same owner continues full conversion.

2026-10-26 13:29 ET: bgun0f0981e8 sound/speed/output second traversal now dispatches GGV_ANIMATION_SOUND_COMMANDS. Full-width signed speed conversion, separate integer/float frame domains, version-specific latch lifetime, engine sound-ID narrowing, captured cursor and three-bit wrap preserved. Three donor cases authored UNRUN; source review/static guard PASS; original ORACLE07 body preserved. Interpreter setup/visibility/wait/repeat/model tick still native, so whole-entry count remains29;50 new authored cases,zero completed conversions. No compiler/gameplay execution. Receipt .claude/gameplay-animation-commands-20261026/sound-source-receipt.json. New open finding: repeat_until_full public parser accepts packed targets without backward-span validation; inspect complete admission and ownership before fixing. Dynamic visibility scratch and repeat-domain work next; same owner continues.

2026-10-26 13:15 ET continuation: bgunTickIncChangeGun now dispatches GGV_CHANGE_GUN (411 source operations), including all five consecutive phases and the separate GGV_EQUIP_SOUND component. Preserves captured/fresh definitions, raw true checks, regional pitch timing, LOAD diagnostics and mode6-to7 deferral, callback ownership and early returns. Five parent plus two sound donor cases authored UNRUN; review gaps for flag80 and animation mutation filled. Independent source review, static asset guard and scoped diff check PASS; ORACLE-07 original body unchanged. 29 session entries source-connected,47 new authored cases,zero completed/accepted conversions. Dependency/public editable source and ALL-gameplay closure remain open; no compiler/gameplay execution. Receipt .claude/gameplay-change-gun-20261026/parent-source-receipt.json. Owner codex-pd-resume-20261023 continues animation command execution.

2026-10-26 resume: verified prior seven source pins, unchanged HEAD a8ab67f3,63 tracked dirty files,65140 untracked and empty index. Workbench N-0792 reuses exact owner. bgunAutoSwitchWeapon now dispatches GGV_AUTO_SELECT_WEAPON; preserves preference-array rereads, repeated SuperDragon checks, full fallback scan, captured current weapon and fresh dualwield/equip/wantammo ownership. Three donor-derived cases authored UNRUN. Independent source review and asset guard PASS; compilation still gated. This brings source-connected entries since Oct23 to28, with zero completed/accepted conversions. Full gameplay conversion and public-source/dependency closure remain open. Receipt: .claude/gameplay-auto-selector-20261026/source-receipt.json. Previous pause lifted by direct user resume; next change-gun and command execution.


2026-10-23 18:00 ET: PAUSED at Mike's requested stop. No builds, agents, gameplay tests or native launches remain running for this lane. Resume requires explicit user direction. Final source receipt: .claude/gameplay-all-inventory-20261023-v4/session-continuation-receipt.json; next-source-units.md carries the next native dependencies. Workbench N-0791 remains the durable source checkpoint; status stays partial.


2026-10-23 late-session checkpoint: 27 native entries are source-connected to GGV during this resume, including parent hand/idle/autoswitch dispatch, readiness/ammo/info predicates, rocket cleanup, stored-function query/set/eligibility, inversion reset and function-toggle policy. Idle and subsequent differential fixtures are now authored (37 new cases total), UNRUN. Independent source reviews completed; two concrete review findings corrected. Source guard and scoped diff check PASS. The v4 census maps these 27 separately from 34 older dispatches awaiting semantic review; zero complete/accepted conversions. Original ORACLE07 pins and prior test body preserved; no definitions removed. Compiler remains unstarted because the retained child capability is unreleased. ALL conversion/public-source/dependency closure stays open and no gameplay tests ran. Current receipt: .claude/gameplay-all-inventory-20261023-v4/session-continuation-receipt.json; dependency-frontier.json records remaining calls. Work continues until Mike's18:00 ET stop. Earlier checkpoints below retain their original time/scope; their pending idle-fixture statements are superseded here.


2026-10-23 continuation: bgunTickInc, bgunTickHand, bgunTickIncAutoSwitch and bgunTickIncIdle now dispatch stock graph programs. Preserved deferred delivery, captured owners, regional integer/float timing, 21-call cap, same-call autoswitch fallthrough and idle/V2 policy. Independent source review and source guard PASS; parent/autoswitch tests authored UNRUN, idle fixture still pending. Receipt: .claude/gameplay-all-inventory-20261023-v2/parent-idle-autoswitch-continuation.json. Remaining dependency/public-source/ALL closure is open; no compiler or gameplay execution.


2026-10-23 16:00 ET: Animation wait, wait-key and recursive start now dispatch typed graph programs. Repeat preserves initialized registers without claiming termination; I64 comparisons, regional float gates, RNG, recursion and diagnostic/store order retain donor semantics. Differential cases authored, unrun. Independent source review and asset source guard passed; no compiler or gameplay execution. Receipt: .claude/gameplay-all-inventory-20261023-v2/animation-wait-start-continuation.json. Parent/idle/autoswitch, public source and ALL closure remain open. Mike requested continued work until 18:00 ET.


## October 23 resumed source checkpoint

Continuation at 15:38 ET: bgun0f09815c now dispatches GGV_ANIMATION_FRAME.
The VM appends signed I64 and typed I64_LT without changing existing enum
ordinals; the native intptr_t command value retains all 64 bits. The graph
owns busy/null/direction decisions and float arithmetic, using only model
frame-count/current-frame backend queries. The result uses an invocation-local
F32 output binding. Existing e4 signatures still cannot accept I64.
An independent C donor oracle and boundary/refusal tests are authored, not
executed. Read-only independent review found no concrete defect; the source
guard passed. Snapshot: .claude/gameplay-all-inventory-20261023-v2/
animation-frame-continuation.json. This supersedes the earlier snapshot only
for its listed source files. Seven entries are source-connected; full semantic
inventory, public source, animation wait/start, parent and ALL closure remain
open. No new build attempt or gameplay test occurred.

Mike directly resumed development in chat 01a169d9-64ed-7123-9cb1-269349bd90b6,
retaining ALL gameplay conversion before gameplay tests. Current integration
owner is codex-pd-resume-20261023 under T-MODDING-011. The prior owner lines
below describe the historical October 13 scope, not a newly active writer.

Verified canonical dev HEAD a8ab67f31c8d482af15db85b3ebd8ecefa3ddc02,
63 dirty tracked paths, 65,096 untracked paths and empty index before work.
Workbench was restarted and its canonical root/data identity verified.
Prior integration/coordinator chats were idle, no Claude executable was
present, and the resource queue was empty. Unrelated Blender and project
sessions were preserved.

All six ORACLE-07 candidate hashes and its freeze manifest matched. The four
existing canonical targets matched the candidate's baseline after newline
normalization, allowing its reviewed delta to integrate without dropping
intervening changes. Reload, ChangeFunc, arm pitch, loaded and reloading
entries now dispatch the candidate stock programs. The C oracle is registered
in pd-tests. Authored differential cases compare return values, field state,
ordered stores/services and pointer identity, including callback owner changes.
Animation command eligibility bgun0f098884 is additionally source-connected;
its exact signed inuse == 1 comparison and lazy reads are preserved.

Independent source review found no concrete mismatch and identified three
coverage gaps (RAISE flag/pitch, null/invalid MAIN inputs, already-transferred
flags), now addressed and re-reviewed. No tests have executed. The isolated
retained build attempt stopped before compilation at Build-Retention.ps1:578
because no reviewed child capability is released. No alternate launch was used.

Current census and six-entry source mapping:
.claude/gameplay-all-inventory-20261023-v2/summary.json,
native-to-graph-coverage.json and lifecycle-integration-receipt.json.
The census has 1,313 files, 15,888 lexical definition candidates, 360 parser
diagnostics and 1,464 function-like macros. These are not a complete semantic
denominator. Six connected entries remain unaccepted with dependency/public
source gaps; graph_complete remains zero in this ledger.

Next source fronts: animation frame/wait/start and parent hand/idle/autoswitch
closure; graph-publication wiring; full call/dispatch/macro reconciliation and
remaining gameplay domains below. Compile qualification is separately blocked
by the shared retained-build capability. No commit, push, gameplay test,
client launch, visual, multiplayer or whole-product acceptance is claimed.

Owner: codex-pd-local-cue-20261010, thread01a127d5-8c01-7577-a098-ef5111b86057.
Workbench N-0626 records the requested sequencing and new inventory-only
ownership. This successor does not overwrite Astra's October10 handoff.
Current user direction is ALL gameplay behavior inventoried, unified and
converted before the gameplay test phase. Internal dependency fronts are
implementation organization; intermediate subsets do not fulfill that scope.
Necessary source/build sanity, tooling qualification and safe admission still
apply. Every intermediate gameplay result is unaccepted.

## Actual first source census

The read-only lexical scan covers 1,293 C/C++ source/header files under
src/game, src/lib, src/include, port/src and port/include. It records 15,705
definition candidates, including 5,207 under src/game. Every scanned file and
candidate body has its own source hash and location. The ledger explicitly
holds 354 parser diagnostics and 1,464 function-like macros for reconciliation.
These counts are lexical candidates, not a compiler-resolved function count
or an assertion that the gameplay inventory is complete.

Artifacts are under
.claude/pd-retained-native-candidate-20261012/parent-closeout-v1/
gameplay-all-inventory-20261013-v1: source-files.json,
native-to-graph-coverage.json and summary.json. The source tool is
gameplay-inventory-tool-v1.py in its parent directory. No source changed in
this census; no compiler, native factory or gameplay test executed.

The new ledger currently verifies ZERO complete graph conversions. This means
existing graph code has not yet been reconciled into this all-behavior ledger,
not that the repository contains no graph implementation. Every candidate is
explicitly outstanding; no wrapper or embedded shooting prefix counts as
whole-behavior completion. The existing seven-file shooting candidate is
SOURCE_PASS but uncompiled, and its parent native functions retain substantial
native tails. Those functions remain source-partial until their entire behavior
is represented and connected. Source-converted-unaccepted is a separate state:
it requires complete source coverage and review, without passing gameplay tests.
Only after the ALL source checkpoint do actual parity results establish
parity-validated status; runtime/manual acceptance remains separate. This avoids
requiring passing gameplay tests as a condition for starting those same tests.

## Complete inventory contract

Before claiming completeness, reconcile every lexical diagnostic, conditional
branch, function-like macro, inline/member/constructor/operator definition,
function-pointer dispatch table and external engine callback. Join declarations
to definitions and exact CMake source selection. Keep excluded/generated or
engine-only code explicit with the reason, owning interface and source hashes.
Do not silently omit an unknown or classify a renderer/physics/transport helper
as outside gameplay merely from its filename.

Every gameplay behavior record must include native definition/entry points,
caller/phase and eligible modes, state inputs/outputs and lifetime, ordered
side effects, time/float/integer/RNG behavior, network authority and payload
semantics, persistence/reset/cancellation, graph source/program entry and
operation coverage, parity reference/test, owner and acceptance state.
Record partial sections at statement/behavior boundaries while leaving the
whole native behavior outstanding. Indirect and event-driven reachability
must be mapped; symbol presence alone is insufficient.

Classify reusable parameterized operations by exact semantics: typed state
loads/stores and references, arithmetic/casts/wrap/storage boundaries, lazy
predicates, ordered branches/loops/calls, bounded containers, explicit random
draws, clocks/waits, object/actor generation selection, lifecycle transactions,
authority/save events and engine service calls. Similar names do not establish
semantic equivalence. Genuinely unique behavior is authored as graph composition
or a uniquely specified operation with source-backed reason and coverage.
A generic node that calls the original gameplay function is an adapter, not a
completed conversion.

Native engine services may remain behind explicit interfaces for rendering,
audio, animation decoding, collision/physics, allocation, asset/cache access,
platform input and transport. Their boundaries require review: gameplay
selection/order/state logic must move to graph programs; backend services must
not hide an unconverted gameplay branch behind a renamed wrapper.

## One full conversion, internal dependency fronts

The next requested bulk unit is the ENTIRE inventoried gameplay scope. It is
not a shooting-only release or a sequence of accepted small subsets.

1. Reconcile inventory, mode/phase dispatch and existing graph coverage.
2. Specify unified typed operations/state bindings and native engine interfaces.
3. Extend graph compilation/execution and public editable source contracts.
4. Convert all gameplay programs: weapons/hand actions and projectiles,
   actors/AI/bots, objectives/scenarios/campaign, multiplayer rules/authority,
   movement/interaction/damage, vehicles/devices, menus/HUD/game flow, save/load
   and Theater-dependent gameplay. Include non-default paths and reset/failure
   behavior; preserve every relevant original entry point through explicit
   graph entry dispatch.
5. Prepare source-derived native references and parity tests concurrently.
   Keep time/order/RNG/state/side effects and network/save traces exact.
6. Only after ALL conversion coverage is reconciled and the complete integrated
   source is frozen/reviewed: execute the gameplay parity/aggregate/full phase,
   then ordinary runtime, capture/performance/manual acceptance and release.

These fronts are dependencies, not permission to run intermediate gameplay
acceptance or call a subset complete. Cheap parser/source/build sanity may run
when needed. Current runner source/component/native qualification is tooling
work, not the gameplay test phase. The retained build/capability gates remain
closed until their independent evidence and integrated authority qualify.

## Writer and review boundaries

Root currently owns its exact shooting/graph and tooling scopes. New ownership
here is inventory/planning only. Pipeline retains T-MODDING-002/-009 umbrella,
catalog/glTF T-ASSETS-052 and its exact source surfaces; existing movement,
networking and observer owners remain protected. Before any bulk source edit,
obtain exact live writer/hunk handoff and update Workbench ownership/dependencies.
Stop on an active conflict; an all-gameplay goal does not transfer those claims.
Do not contact external Claude agents or expand a source export to solve it.

Independent local inventory/architecture review may proceed. Claude planning
and source review must follow actual verified permissions, exact recipients
and data scope. A reported custom-rule revision is not direct verification:
this thread currently exposes no supported rule-reader tool. The pending
shooting packet remains local and unchanged; broader inventory is not added
to that export. No alternative recipient/account or security route is used.

All conversion and acceptance counts must derive from the durable coverage
ledger, not wrapper count, installed-program count, source review, prepared
tests or historical CI. Update Workbench and this plan as actual evidence changes.
