# Tasks

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

2026-10-26 change-gun continuation: extracted equip sound policy into GGV_EQUIP_SOUND at its original parent gate, preserving lazy loaded/camera/dead reads, every weapon case, version-dependent necklace/briefcase2 IDs, sound allocation, pitch bits and priority restoration. Two native-donor cases authored UNRUN; independent source review and static asset guard PASS. Original ORACLE-07 test body preserved. Parent change-gun state machine remains native/incomplete; full gameplay/dependency/public-source closure open. No compiler or gameplay tests run. Receipt .claude/gameplay-change-gun-20261026/source-receipt.json. Same owner codex-pd-resume-20261023 continues parent phases.

2026-10-26 resume: verified prior seven source pins, unchanged HEAD a8ab67f3,63 tracked dirty files,65140 untracked and empty index. Workbench N-0792 reuses exact owner. bgunAutoSwitchWeapon now dispatches GGV_AUTO_SELECT_WEAPON; preserves preference-array rereads, repeated SuperDragon checks, full fallback scan, captured current weapon and fresh dualwield/equip/wantammo ownership. Three donor-derived cases authored UNRUN. Independent source review and asset guard PASS; compilation still gated. This brings source-connected entries since Oct23 to28, with zero completed/accepted conversions. Full gameplay conversion and public-source/dependency closure remain open. Receipt: .claude/gameplay-auto-selector-20261026/source-receipt.json. Previous pause lifted by direct user resume; next change-gun and command execution.


2026-10-23 18:00 ET: PAUSED at Mike's requested stop. No builds, agents, gameplay tests or native launches remain running for this lane. Resume requires explicit user direction. Final source receipt: .claude/gameplay-all-inventory-20261023-v4/session-continuation-receipt.json; next-source-units.md carries the next native dependencies. Workbench N-0791 remains the durable source checkpoint; status stays partial.


2026-10-23 late-session checkpoint: 27 native entries are source-connected to GGV during this resume, including parent hand/idle/autoswitch dispatch, readiness/ammo/info predicates, rocket cleanup, stored-function query/set/eligibility, inversion reset and function-toggle policy. Idle and subsequent differential fixtures are now authored (37 new cases total), UNRUN. Independent source reviews completed; two concrete review findings corrected. Source guard and scoped diff check PASS. The v4 census maps these 27 separately from 34 older dispatches awaiting semantic review; zero complete/accepted conversions. Original ORACLE07 pins and prior test body preserved; no definitions removed. Compiler remains unstarted because the retained child capability is unreleased. ALL conversion/public-source/dependency closure stays open and no gameplay tests ran. Current receipt: .claude/gameplay-all-inventory-20261023-v4/session-continuation-receipt.json; dependency-frontier.json records remaining calls. Work continues until Mike's18:00 ET stop. Earlier checkpoints below retain their original time/scope; their pending idle-fixture statements are superseded here.


2026-10-23 continuation: bgunTickInc, bgunTickHand, bgunTickIncAutoSwitch and bgunTickIncIdle now dispatch stock graph programs. Preserved deferred delivery, captured owners, regional integer/float timing, 21-call cap, same-call autoswitch fallthrough and idle/V2 policy. Independent source review and source guard PASS; parent/autoswitch tests authored UNRUN, idle fixture still pending. Receipt: .claude/gameplay-all-inventory-20261023-v2/parent-idle-autoswitch-continuation.json. Remaining dependency/public-source/ALL closure is open; no compiler or gameplay execution.


2026-10-23 16:00 ET: Animation wait, wait-key and recursive start now dispatch typed graph programs. Repeat preserves initialized registers without claiming termination; I64 comparisons, regional float gates, RNG, recursion and diagnostic/store order retain donor semantics. Differential cases authored, unrun. Independent source review and asset source guard passed; no compiler or gameplay execution. Receipt: .claude/gameplay-all-inventory-20261023-v2/animation-wait-start-continuation.json. Parent/idle/autoswitch, public source and ALL closure remain open. Mike requested continued work until 18:00 ET.


2026-10-23 15:38 ET continuation: T-MODDING-011 additionally connects native
animation-frame evaluation through typed graph control/arithmetic, including
full-width signed command values. Independent source review/source guard pass;
three tests authored and unrun. Current seven-entry work remains source-only.
Receipt: .claude/gameplay-all-inventory-20261023-v2/animation-frame-continuation.json.

2026-10-23 direct user resume: T-MODDING-011 owner codex-pd-resume-20261023
integrated the hash-verified six-file ORACLE-07 candidate, registered its C
oracle, authored differential lifecycle coverage, and connected the animation
eligibility predicate. Independent source review completed; compiled/runtime
acceptance remains absent. Retained build refused before compiler launch due
to unreleased child capability. ALL gameplay conversion remains required
before gameplay tests. Current census/mapping and evidence:
.claude/gameplay-all-inventory-20261023-v2/lifecycle-integration-receipt.json.
See designs/in-flight/pd-all-gameplay-conversion-2026-10-13.md for remaining
animation/parent/public-source and full inventory closure. Older pause entries
below remain historical; this resume does not restart other project chats.

2026-10-13 V6 images now DELIVERED (N-0702) to Mike's explicitly requested
new PDthread1791904634.919999: side/threequarter/front/clay native finalized,
four IDs and verified links in finalized-slack-delivery-v2.json. No duplicates,
source or DOCX upload. Earlier rejection preserved; verified new human request
resolved PNG authorization. Parent frozen source/pixel critique packet
29a205ab... ready; actual V6 Claude review remains pending. Visual/10hidden
pose/native-owner/ALL-gameplay gates unchanged; no next correction begun.

2026-10-13 pistol-only production exception N-0699/N-0700: same existing writer
authored frozen V6 artist source32278129, source reviewdd37f731 PASS, driver
reviewf095067a PASS and corrected finite operator V3fb5d8cf7 PASS. Actual
queued inspectionq_20261013145742116_133a7276 produced17PNG/source-study.blend
b0396497, full indexed save/reopen/restoration parity, native exit0/ownedhandle
close28.492seconds and normal terminal exit0/controller absence. Corrected raw
UTC firstobserver interval172.363239seconds within180; failed arithmeticV1
retained. VISUAL HOLD: flat cheek/thin lens/tubular grip/lower muzzle prongs.
10hidden evaluatedtransform/BVH rows unverified;25visible rows agree. Parent
actual frozen-source/pixel Claude review and bounded correction remain next.
Native hand scale/shared33matrix/clip integration and protected051/modding002/009
ownership remain open; no game-ready/runtime/manual claim. ALL conversion
beforegameplaytesting preserved; no vehicle/veteran/bulk resume or commit/push.

2026-10-13 final native attachment attempt: parent explicitly requested current
Word to C0C5S3S2F33/thread1791894735.889689. Permission-window prose clarified;
final DOCX13675bytes SHAe6cd4b3b, MD9c5ca7e7. Slack allocatedF0C8VAD73LJ but
automatic approval rejected byte POST before process creation: internal paths,
hashes, implementation/capability contents lacked exact-payload authorization.
No bytes/finalization/permalink, no retry/workaround; explicit approval question
pending. Local final plan and rejectionreceipt retained; parent has full text.

2026-10-13 13:48 final planning update: actualClaude crystal consult PID71056
Opus5.5 exit0, response9fdef3ff full text read/hash verified. Joint rigid
local-axis retract/sequential-return preference; conceal before hide,
closed pocket sweep/silhouette and nozero-scale/degen guards. Native commit
granularity governs rises; accepted secondarystart irrevocable. Final DOCX
13602bytes SHA3dfea244 and MDe20a3c38; upload NOT performed, parent requested
immediate fulltext return for inspection/publication before14:00UTC.
Package-manifesta4b14040/reconciliation records final; no consult remains
pending and no production resumed. Earlier pending entries are history.

2026-10-13 daytime planning exception (N-0694 through N-0697): three-asset
joint rework plan at .claude/pd-maian-models-20261013/deep-rework-plan-20261013/
joint-reconciled-plan.md and readable DOCX. Actual parent-supplied Opus5.5
image-six B2215424/PID70036 and text B832037D/PID72240 exit0 reconciled with
Codex full native audit58d22120/db2e267e (42 unchanged source rows) and 68
preserved art inputs. Rebuild primary veteran/vehicle forms and pistol
receiver/grip/roots; same native rig and complete animation/runtime required.
Mike's new crystal-collapse/sequential-return mechanism1791898282.903189
supersedes carrier exchange; rigid per-part motion first, unsupported vertex
deformation explicit. Accepted secondary START is irreversible, including
death during charge; no cancelable post-accept windup. Existing canonical
ea978834 binds small proxy pops/secondaryzero/directindividualcontact.
Actual selected public stock rest/head/body/hand/clip bytes and production
presenters/tandem/committed-release adapters remain unqualified. No production
resumed, builds/tests/game/Blender/native/graph/source writes or new Claude
dispatch. DOCX XML/package checked only, not application rendered. Requested
Slack thread1791894735.889689 delivery and parent shortmechanismcritique
closeout pending; parent owns night resumption and protected native owners.

2026-10-13 night handoff: PD production PAUSED by Mike's latest schedule;
resume about20:30 America/New_York through parent. No new implementation,
build, render, game or Claude dispatch. Original V7 native cohort completed;
independent45ce289c confirms NATIVE_COHORT_PASS6/6 on exact aggregate60b6480f.
Same caller73308 normalexit0/absence, native roots57340/63072 absent and both
FIFOs done/cleared; no owned PD workload remains. Capability production/release
still awaits integrated review, and ALL/gameplay tests remain held/unrun.
Saved handoff: .claude/pd-retained-native-candidate-20261012/parent-closeout-v1/
paused-night-handoff-20261013-v1.md. All failures, dirty source and model WIP
preserved; no new commit/push/security/cleanup. VeteranV4 is actual readbackPASS/
VISUAL_HOLD; vehicleV5/weaponV5 source reviewed, matched V8 captures UNRUN.
Parent's remainder delivery is ready but absolute path/import unresolved at
pause; three reserved files remain untouched pending night exact import/review.

2026-10-13 11:49 UTC (N-0686 through N-0688): UTC8 actual selected-PS7
data-only qualification passed8/8, normal exit0; independent review967e4d0a
confirmed the original74abf990 receipt. Original expired1043 remained DATA.
A wrong root storage reservation failed before owner/holder publication and
closed normally as failed; corrected smoke-storage/test-runner leases created
a fresh nine-minute1135 owner and original publisher exited0 with two closed
streams. Actual V6 native choreography then ran creation/IPC/adoption/accounting
and conflicts/closeouts, but six-case packaging failed with "Supervisor contract
field set differs." No aggregate or native/capability PASS exists. Original
Unknown40917334 and holderfec06f8f are retained. Independent a603be49 supported
manual failed closeout: same caller12208 exited1, positive owned actors absent,
both FIFOs failed/cleared at11:46:07. All helpers, admissions, handles, capture
and publications were reconciled before exit. No renewal/replay/global kill.

N0688 assigns only a minimal private V7 producer-boundary DTO correction and
pure12 regression source fixture, then independent source/actual qualification.
Canonical nine-source runner bundle remainsbc49c442; capability/build/gameplay
admission stays closed. Recovery/Shoot2 consolidated source review717d87a6 and
Quantity4 source review34b373e8 passed; all gameplay groups remain UNCOMPILED/
UNRUN. Earlier BLOCKs and failed epochs remain preserved. Quantity4 current
four-file freeze73c3dfc2 is source-reviewed only. No push or cleanup.

2026-10-13 11:24 UTC (N-0672 through N-0685): SHADOWBANE/canonical reads
verified; HEAD a8ab67f31c8d482af15db85b3ebd8ecefa3ddc02, empty index, no push.
The source-only fifteen-file helper checkpoint preserved all 62 peer dirty
hashes. ShootingV2 SOURCE_PASS remains incorporated N0617. Query5, Reload3
and CountdownVisibility2 have independent source passes; Query oracleV14 now
has SOURCE_DERIVATION_PASS. Recovery/Shoot2 consolidated successor review is
pending; earlier BLOCK remains historical truth. All authored gameplay tests
remain UNCOMPILED/UNRUN under Mike's ALL-before-gameplay-testing instruction.
The next exact four getter/ammo-setter source scope has one writer under N0685.
GDS/B32 exact imports are source-reviewed; only GDS has the bounded CMake and
reload VM source connection. No native ABI/runtime/manual acceptance follows.

Actual PS51 producer/reader fixtureV4 passed18/18 with independent review.
Fresh V3 owner preparation succeeded and the original publisher closed with
normal exit0. Run launcherV1 then refused before child creation because PS7
automatic DateTime conversion lost the strict UTC/fractional admission input.
Original owner1043 expired; both queues closed as failed. LauncherV2 has
independent SOURCE_PASS; pure UTC8 fixture source/recipe qualification is next
before any fresh nine-minute native owner. All six native-family proofs and
current exact-source PS51 capability qualification remain open. Historical
PS7/PS51 receipts never qualify this dirty source checkout.

Three native WIP image attachments actually finalized in #updates at11:02:07
UTC; official delivery receipts are retained, and parent owns report prose.
VeteranV4 actual twelve-view Blender5.2.2 capture completed with normal exit0,
actor absence and done/cleared render queue; independent pixel/data review is
pending. Vehicle/weapon shared driverV7 SOURCE_BLOCK remains; V8 frozen source
corrects actual lighting readback and f64 custom-property parity, with review
pending. Visual/game acceptance stays held. Protected graph/pipeline/assets051
owners remain unchanged; no cleanup, worktrees, global kills, security changes,
duplicate external worker, gameplay workload, or push.

2026-10-13 09:51 UTC (N-0659 through N-0671): Shadowbane/canonical cwd and
repository readability reverified; HEAD2a66629d retained, no push. Completed
shootingV2 SOURCE_PASS remains incorporated N0617. Bool/effect2/effect1 exact
standalone imports have independent source approval; all gameplay groups remain
UNCOMPILED/UNRUN and ALL remains false. Throw/Melee/Special/Empty/Attack/SetState/
Reset/Busy/Trigger source conversions are independently reviewed at their frozen
epochs. OracleV7 correction and V8-V10 derivations passed source review; V11/V12
are frozen for independent review. Ammo4 f626 source is frozen, independent
review and complete original/current V13 oracle assigned under N0671. No native
provider, ABI/lifetime, gameplay, save/network or manual acceptance follows.

V6 managed Inspect retains its narrow actual load/type identity pass. Separate
PS51 admission fixture passed30 mock component cases with independent review0212.
Actual V2 owner preparation failed before publication on OrderedDictionary
reference fields; original failure/normalexit1 and two failed FIFO closeouts are
preserved. V3 source correction and eighteen-case producer/reader fixture have
independent source approval. Actual PS51 fixture at09:46 failed1/18 (R08:
Supervisor JSON is not canonical lossless data), normalexit1; original receipt
qualification-root-20261013T0946.json retained and test FIFO cleared as failed.
Independent diagnosis is assigned; no new owner/preparation/native family ran.
All six native family proofs and current capability/build/game admission remain
open. Earlier PS7/PS51 counts never substitute for current dirty-source proof.

Current veteranV3, vehicleV4 and weaponV4 WIP screenshots were actually delivered
to the matching PD thread; initial auto-review denial and the same-route accepted
retry with unchanged human transcript evidence remain recorded. All three retain
independent VISUAL_HOLD. Actual twelve-view veteran diagnostic completed under
Blender5.2.2 exit0; source55d09050 and seven inputs unchanged/restoration true.
Independent diagnostic05eefb confirms normal saved-byte quantization, substantial
lit shading brightness and the authored eight-pixel checker formula using only
three of five palette entries. No shader decode or production color-space fix
is approved. Protected graph/pipeline/assets051 owners unchanged; no cleanup,
worktree, global kill, security changes or duplicate external worker.

2026-10-13 08:02 UTC (N-0651 through N-0658): SHADOWBANE/canonical host and
repository reads reverified; no setup-refresh failure. Source-only six-file
helper checkpoint2a66629d preserved all62 tracked peer dirty hashes and empty
index. It remains unpushed: dev CI automatically runs gameplay tests, contrary
to Mike's later ALL-before-gameplay-testing instruction. Completed shootingV2
SOURCE_PASS already incorporated N0617; stale pending-review heartbeat corrected.
Float and effect3 exact genuine Claude files imported with standalone source
PASS3d04c1ad/8f64fd04, authored tests UNCOMPILED/UNRUN and caller links open.
Bool genuine handoff patch6dde009d now imported/reviewed by veteran_art under
N0656; no completed import claimed yet. Next anonymous effect2 contract author
model_corrections writes packet only, parent sole external coordinator relay.
Complete Throw163-node implementation has independent SOURCE_PASS6946d1f1;
full original/current oracle V8 writer classify_flow continues. N0657 explicitly
reconciles frozen Throw epochd589 against separately authorized live Melee drift
using exact relevant-unit comparisons, not current whole-file parity. Complete
Melee171 and MinClip10 source writer review_install_cap is freezing four-path
successor; independent review/oracle remain pending. Matrix testV6 scoped source
PASS29f66dfb; oracleV7 corrected three-cell footprint/header inverse still awaits
independent root supplemental audit. Every gameplay test remains uncompiled/unrun.
Actual PS51 V6 retained Inspect managed/type identity PASS result9ee27583,
independent f0f480fa, PID64500 normalexit0 and absence verified. Three held DLLs,
nine types, four bounded admission observations; no native factory/capability.
Changed-path tooling fixture author vehicle_motion now writes source under
N0657; changed-component, actual six-family/native/capability proof remain open.
Actual corrected veteran studyV3 Blender5.2.2 exit0: editable blend55d09050,
nine captures plus source/readback and exact packed-material/UV reopen retained;
queueq_20261013074139123_8e9f56f8 finished after actor absence. Independent pixels
review active, no native/assets051/export/manual approval. Prior failed source
run and two probes remain; first current gate failed before writes/start from
operator's truncated fixture hash, full reviewed fixture hash was unchanged.
Actual vehicleV3 has independent VISUAL_HOLD4fd19531: canoe/slab forms and three
duplicate capture pairs. VehicleV4 substantive source has SOURCE_PLAN_PASS30dbc72d;
root byte-bound driver6 three-pin successor1522e644 is independently reviewed
under N0658 before execution, preserving loader1 and rejected cached-route history.
WeaponV4 standalone actual study remains pending. New Slack WIP artifact grant
is not established; Mike's verified approval binds the old three delivered
previews only. No new upload/egress, cleanup, worktree, global kill or security
change. Protected graph/pipeline/assets051 owners unchanged; ALL remains false.

2026-10-13 06:35 UTC (N-0647 through N-0650): SHADOWBANE/canonical execution
reverified; HEAD8f9a3530/index empty preserved. Completed shootingV2 SOURCE_PASS
was already incorporated N0617. Exact effect4 R2 helper3 imported with applied
standalone source PASS dde63692; root added one CMake implementation entry,
before874eae0f/currentb3f5fd93, exact inverse. Five standalone groups remain
uncompiled/unrun and are not registered Catch coverage. Attack V4 authored
evidence SOURCE_BLOCK43afe7d9 is preserved. Test successor938a6245 has independent
scoped SOURCE_PASScd8f2f9e: native vibration extent2/tail canaries and genuine
held-shot negative control. Oracle V6 c0cc/bc60 is source-frozen with byte-exact
inverse; root audit pending. Eight attack groups remain UNRUN, arithmetic mocked.
Native V5 SOURCE_PLAN_BLOCK278ac22e identified stale FIFO during blocking memory;
sole prior writer now authors new V6 source under N0648, independent review and
all actual loader/native/capability gates open. Managed3/inert49 historical
same-source receipts retain their distinct scope. Float generic4754B contract
e25783d5 published in permitted Temp; parent reports relay to sole Claude
coordinator admitted, no duplicate invocation. VeteranV2/vehicleV3/weaponV4
complete authored handoffs survived provider capacity errors. Independent art
review found texture-driver integration and private cached-bytecode gaps;
new byte-bound adapters are frozen for review, builders/caches untouched.
No new Blender/build/gameplay test/native/game, commit/push or image upload.
ALL conversion incomplete; Mike's source-before-gameplay-test ordering active.

2026-10-13 05:33 UTC (N-0641/N-0642/N-0643): Exact PS51 retained managed
Inspect passed three definition compiles; separately queued inert tooling passed
49 checks with one retained DLL. Both processes exited0 and FIFOs finished.
No native factory, six-case proof, capability or gameplay test ran. Native V4
startup is SOURCE_PLAN_BLOCK for early admission and caller/fixture provenance;
the independently assessed retained-DLL successor V5 is being authored, unrun.
Full hand implementation has independent scoped source PASS; root oracle
derivation PASS checks all25 R3 references and exact source recovery. R2 stale
prelude reference and audit-script diagnostics remain preserved. Parent's next
source pair is bgun0f09aba4 recoil/recovery plus bgunTickIncAttackingShoot:
review_install_cap owns implementation4 hunks, classify_flow disjoint oracle2,
both running under N0642. Vehicle_motion owns only NEW private native V5 loader
sources/manifest, independent reviewer review_amendment_plan, under N0643.
Genuine Claude effect4 PID30456 closedexit0; its sole author test-only correction
is pending, canonical helper import held. Previous hand/full/graph freezes and
peer owners preserved; no commit/push/cleanup/security change. ALL conversion
remains incomplete and gameplay execution stays held by Mike's ordering.

2026-10-13 04:50 UTC (N-0639/N-0640): Exact human-approved weaponV2,
soldierV1 and vehicleV1 previews delivered once to PD thread using native file
uploads, with honest modeling-study labels. Later weaponV3/vehicleV2 remain
local and independently visually held; source geometry is not game acceptance.
Full shooting configuration/commit/audio four-file candidate now has an
independent scoped SOURCE_PASS; oracle2 mechanically derives original/current
seams. Four parity groups and expanded integer16/32 coverage are authored UNRUN.
Next disjoint hand-program/oracle writers own rumble/slide only; anonymous
effect4 contract is complete in permitted Temp for the sole existing dispatcher.
No duplicate launch/private source transfer. ALL inventory/conversion incomplete.
Native V3 recipe SOURCE_PLAN_PASS, but managed Inspect first attempt failed
Add-Type parameter validation before compilation. Fixed wrapperV3/componentV4
source qualification remains pending. Current exact native capability/child/
gameplay/build admission closed. Source snapshots/reviews never imply runtime.
Detailed paths, receipts, current writers and blockers:
.claude/pd-retained-native-candidate-20261012/parent-closeout-v1/scoped-handoff-20261013T0450.md.

2026-10-13 03:40 UTC (N-0636): Genuine Claude integer component integrated at
three exact new paths and independently STANDALONE_SOURCE_PASS; seven authored
groups remain uncompiled/unrun. Exact installed freeze/scoped diff are in
.claude/pd-retained-native-candidate-20261012/parent-closeout-v1/.
Next actual GGV caller assignment is frozen locally in
C:/Users/mikeh/AppData/Local/Temp/pd-owned-integer-caller-20261013-v1;
no provider dispatch/private source transmission occurred. Its minimal
pd-tests link dependency needs the separately claimed CMake entry.
Three semantic reports preserve explicit remaining obligations, including
64 omitted scenario .inc definitions and a recovered active bgunRender.
ALL conversion and gameplay acceptance remain incomplete.

Native Slack attachment intake resolved all three concept references without
changing the failed Library helper. Actual editable Blender source studies now
exist for weapon/veteran/vehicle. Vehicle study-v1 has 3,436 triangles and 147
sampled clearance positions; independent VISUAL_HOLD identifies the tall mast
and overhead gunner reach. Weapon study-v2 has a separately saved 684-triangle
belly/grip correction; independent VISUAL_HOLD finds substantial improvement,
but crystal seating, emitter size and jaw/eye contours still need correction.
Neither geometry nor static clearance proves engine, creator or gameplay use.
Automatic approval review rejected publishing two preview images to PD Slack;
specific approval remains pending, and no images were delivered.

Current supervisor V5's exact thirteen-source binding remains unchanged and
SOURCE_PASS. Historical physical proof belongs to the older V3 bundle only.
Private native recipe V1 remains SOURCE_DRAFT_BLOCK; a separate worker owns
new V2 recipe repairs only, with no native workload authorization. Shooting
V2 SOURCE_PASS was already consumed in N-0617; its authored differential
cohort remains UNRUN under the ALL-before-gameplay-tests instruction.
HEAD is 8f9a35307f9ad7bfea018c1ac9ad163a7bf3c3a1, index empty, 63 tracked
dirty paths preserved. No commit/push, cleanup, worktree or security change.

2026-10-13 02:49 UTC: ALL gameplay inventory V2 corrections independently
SOURCE_REVIEW_PASS. Frozen r3 census1602 files/19736 definition candidates,
23112 unresolved obligations; actual parser30 PASS and1602-file/CMake/no-drift
recheck. This is incomplete semantic inventory, not complete conversion.
Source-converted-unaccepted and parity-validated are distinct; gameplay tests
remain after ALL source conversion/review. Current peer owners unchanged.
Genuine Claude vehicle component integrated byte-exact at three new paths,
independently STANDALONE_SOURCE_PASS: boost new-shot refusal/no stow, symbolic
slower locked cadence and target-generation-aware irreversible guidance loss.
Two new port files/eight candidates form a separate census delta; original
r3 epoch remains preserved. Twelve standalone test groups unrun; main/test
registration, active graph/engine/lifecycle and vehicle policy remain open.
Existing coordinator is sole dispatcher; next integer primitive proposal
gameplay-integer-api-packet-v1.md is ready, no second worker or invocation.
Weapon/character isolated model-source scopes N0630 are recorded; actual
reference pixels/modeling blocked by Library helper Windows os.setxattr error.
Runner V5 SOURCE_PASS remains exact; native cohort draft is SOURCE_DRAFT_BLOCK
for refusal-task retention, atomic control publication and lock cleanup races.
No current V5 native qualification/capability, build/game/GPU/model/manual
acceptance, commit/push or cleanup. See parent-closeout-v1 current handoff.

2026-10-13 00:54 UTC: T-TOOLING-006 amended native driver V3 and its preliminary
actual Windows PS51 slice passed independent source-plan/evidence reviews.
Same broker73284/operation2c1c5ca489515cd39eee0f2b8643ae80 showed job membership,
transferred witnesses, accounted receipt, both negative ACK refusals with
root alive, then exit0/job0/checked handles closed. Original helper/RPC/probes
completed before ordinary controller exit and done FIFO finish. Source23
regressions pass; production nine-source V3 remains unchanged. The failed
earlier attempt remains preserved. Full six-case qualification, capability
publication and retained child/client gates remain closed; shooting C/C++
differential and game/GPU/manual acceptance remain unrun. Workbench N-0623,
parent-closeout-v1/scoped-handoff-20261013.md and exact independent native audit.
Next continuation critique is DESIGN_BLOCK for creation-boundary admission,
Fixture Unknown retention, collision contexts and checked lock lifetime;
same-object recovery is supported. No next executable recipe is admitted.

2026-10-13 00:42 UTC: T-TOOLING-006 first actual receipt slice reached owned
broker/job membership, handle transfer and accounted publication, then failed
on a PS51 driver Mode-variable collision before negative ACK probes. Original
controller completed checked physical closeout (exit0/job0/handles closed)
before normal exit; failed queue and all evidence preserved. V3 driver fixes
the name collision and generic-object journal conversion; PS51 source23 PASS,
independent review pending. Production V3 sources remain unchanged. No six-case
native qualification, capability, C/C++ differential run or release is claimed.
Workbench N-0622, native-preflight-9b5c852d376d45bcade72a0ee22fac93.

2026-10-12 23:50 UTC: T-TOOLING-006 retained supervisor V3 is independently
SOURCE_PASS on nine raw source dependencies, including the loaded storage
guard. Actual PS5.1 and PS7 source/component fixtures each pass102 capability
and171 retention checks; broker29 and native-source guard pass. These are
managed/inert/accounting checks, not native job/IPC or C/C++ proof. Native
qualification recipe/driver, six physical lifecycle cases, reviewed accounted
capability publication and retained child/client admission remain open. All
prior failures and eight-file freezes are preserved; no commit/push/release.
T-MODDING-011 shooting V2 SOURCE_PASS is consumed; its five authored
differential cases remain unrun. Default programs are a partial archive/profile
bridge. Claude source export is frozen locally, but approval review rejected
handoff messages and direct scoped human authorization is pending. Exact
receipts/ownership/next gates: Workbench N-0617 through N-0620 and
`.claude/pd-retained-native-candidate-20261012/parent-closeout-v1/`.

2026-10-12: Pipeline resumed the full asset/graph goal with T-ASSETS-052,
consistent glTF required-feature admission across provider, scene, animation
and archive paths. Python20/20, archive selftest and native-source guard pass
on unchanged nine-file freeze-v2. Canonical retained build refused before
compiler launch because the reviewed child capability is unreleased. C++
contracts and client/runtime proof remain unrun; candidate stays uncommitted.
Graph13/native shooting remains the other session's sole-writer batch. No
overall extraction/runtime/graph/creator-pack completion is claimed.
[Checkpoint](audits/2026/gltf-feature-admission-2026-10-12.md).

2026-10-10: T-TOOLING-011 startup repair follows published cue commit 2e1fbecf
and successful exact CI 38094051174 (1835 cases / 334167 assertions). Approved
scope replaces immediate Process.Path ownership reads with retained-handle PID,
Win32 image path and creation time, and adds independent registration-failure
closeout at all three callers. PS5.1 and PS7.6.6 startup proof passes 90 checks
each, including 20 immediate launches per host and actual production failure,
lease and verdict gates. Ownership/cue/console/reuse 22/47/35/19 each and Python
reuse 17 pass. Candidate stays uncommitted for independent Astra review; no
game or native/controller/storage-policy edits. Actual caller desktop preflight
passed session 1 / WinSta0 / Default; this executor exposes no native app API,
app permission and player 0 remain unverified. Evidence:
`.claude/pd-initial-integration/20261010-startup-repair/`. T-TOOLING-011 and V-014
remain partial; ordinary game and four-frame manual acceptance have not run.

2026-10-10: T-TOOLING-011 optional retained-profile ownership cue is connected
and locally tested, pending Astra integrated review. Cue 47 checks each PS5.1/7;
ownership/reuse/console 22/19/35 each host, Python reuse 17 and safe storage subset
2 pass. Full storage/reset aggregates remain blocked by the no-deletion boundary.
No native product build, real game, manual acceptance or publication. V-014 and
ordinary retained-profile acceptance remain partial/failed at their historical
receipt. Mike's temporary takeover is N-0422, ending by October 10 20:15 EDT.
Review evidence: `.claude/pd-initial-integration/20261010-cue/`.

2026-10-04 04:20 UTC: T-TOOLING-011 native retained-profile isolation and
single-process wrapper verified locally. Final client/explicit-tests builds and
22 cases/321 assertions pass; actual locked-consumer query passes PS5.1/7.6.
Six guard cases,789 configured units/1477 files/64 routes and public-native-source
guard pass. Boot debug-root routing and inherited outputs fixed; fresh per-run
result/captures retained. Heavy released04:03, no game/copy/cleanup.
Published c6b3e2ec; exact CI37176234687 success04:18:51UTC: clean client/tests,
full1835 cases334167 assertions,4/6/20/17 Python,19 storage+adapter eachPS host,
hosted790units/1478files/64routes and artifact upload. Ordinary retained-profile
game/controller/app/capture acceptance remains partial.
[Native reuse checkpoint](audits/2026/pd-native-reuse-isolation-2026-10-04.md).

2026-10-03 20:24 Eastern / 2026-10-04 00:24 UTC: exacte74a6795 CI37164271400
completedSUCCESS:4dependency/20storage/17reusePython,19checks eachhostedPS5/7,
cleanclient/tests/full1813cases333814assertions/uploadPASS. NativeUTF8/BOM wire
repair accepted; priorhostedfailure retained. Read-only lease/profile foundation
validated, currentgame reuseREFUSED untilnativebasewrite isolation. No realgame,
fullcopy/delete or caps/pins change. Initialpartial/V014fail11/29,1/17,0captures,
watcherunexecuted; actualcontroller/app/window stillneeded. [Closeout](audits/2026/pd-existing-base-reuse-2026-10-03.md).

2026-10-03 20:08 Eastern / 2026-10-04 00:08 UTC: hosted d6b59982 reuse gate
failedPS5 JSON transport after4dependency/20storage/16Python passes; preserved.
ExactBOM/Unicode corruption reproduced. BackendUTF8-sig plus nativeProcess
UTF8 byte transport now passes17Python +19checks eachPS5/7, independent of
callerencoding. Scopedfollowup/requiredexactCIcompletion pending; no greenclaim.
Native immutable-base refusal and11/29,1/17,0capture acceptance truth unchanged.
[Wire repair](audits/2026/pd-existing-base-reuse-2026-10-03.md).

2026-10-03 19:46 Eastern / 23:46 UTC: read-only reuse lease/profile foundation
validated16Python +14checks eachPS5.1/7, preserving failed base/receipts and
existing caps/pins. Current run.ps1 reuse refuses before launch: native base
write isolation is absent for extraction sidecars/.pdextract-cache. No new
build/game/fullcopy/delete. Initialscopepartial/V014fail stays11/29,1/17,0captures;
controller/app/window and supported immutable-base contract remain needed.
[Reuse unit](audits/2026/pd-existing-base-reuse-2026-10-03.md).

2026-10-03 18:56 Eastern / 22:56 UTC: renewed admission passed; actual current
client fixture failed11/29,1/17events,0captures after85-second menu readiness.
Xbox One controller occupied player0; virtual attach guard correctly refused.
NativeSky found realwindow3082418, then it vanished before appaccess; no app
permission denial/requestID. Game17056 and CLI13744 absence verified22:56:18,
all FIFOs finished before23:07. Failedcopy/recipe preserved,3fullcopies/2pins.
Immediate verified ownership-cue watcher prepared only; no relaunch/deletion.
Initial scopepartial, V014fail. [Actual runtime](audits/2026/pd-approved-workspace-recycle-2026-10-03.md).

2026-10-03 18:33 Eastern / 22:33 UTC: Mike reaffirmed permission and directed
correction of the authority summary: **commit and push our project work**.
His direct instruction is authoritative; D-013 remains decided. Historical
review claims that the trusted transcript withheld push permission must not
be replayed as current user preference. Scoped verification, peer preservation
and the serialized Git lane continue. Runtime admission remains a separate
recorded blocker. [Current authority](working-preferences.md).

2026-10-03 21:48 UTC: current runtime attempt refused before launch; no game,
app request, native assertions or captures. Readiness BLOCKED: combined
canonical preflight rejected before execution with no reason/request ID;
worker scoped CIM query Access denied, preserved without retry. Historical
admission/cleanup checks cannot prove fresh restricted-actor permission.
Prepared initialization/admission-before-window protocol only; no new worker,
game, build or test. V-014 remains blocked, initial scope partial; D-012 intact.
[Admission record](audits/2026/pd-approved-workspace-recycle-2026-10-03.md).

2026-10-03 21:00 UTC: Mike cancelled today's Plex pause only, through usual
07:00 Eastern night end; future defaults remain. All project deletions require
Recycle Bin recovery. Supported local Computer Use capability now PROVEN:
official app CLI0.159.0-alpha.12.1, native node_repl/Sky, win32/Shadowbane,
list_apps40 PASS; no PerfectDark target returned. Worker exited0/children0,
no heavy/game/state/input/capture/grant edits. D-010 consent already decided;
actual app access still unverified. D-012 recoverable removal remains complete.
Actual app prompt/window access precedes fresh15m coordinated ordinary gate.
[Capability continuation](audits/2026/pd-approved-workspace-recycle-2026-10-03.md).

2026-10-03 20:23 UTC: Mike approved D-010 verification and the exact D-012
old-copy removal, Recycle Bin preferred. The single unpinned failed shared/client
copy was recoverably recycled; all 13,450 files and 12,389 blobs verified,
archive/two pins/current builds preserved. Managed full copies now two;
no disk space reclaimed. D-012 handling complete, D-010 consent decided but
actual app access unverified because this executor lacks Computer Use tools.
No game/GUI launched; all resources released. Initial T-TOOLING-008 stays
partial. Route ordinary gate to a capable executor, then fresh 15m window
ending before today's 20:45 UTC cutoff.
[Verified disposition](audits/2026/pd-approved-workspace-recycle-2026-10-03.md).

2026-10-03 17:47 UTC: T-TESTS-004 exact source620456a7 CI37141120356 PASS:
four dependency fixtures, 20 native Windows storage cases, configure/clean
client+tests, hosted full1,813cases/333,814assertions and client artifact upload.
Original20 contract failures repaired; both intervening junction CI failures
preserved. Local1,817/335,458 is a separate peer-inclusive proof. T-TESTS-004
and T-TOOLING-010 qualify as validated; initial T-TOOLING-008 stays partial.
All local resources zero; ordinary-client D-010/D-012 gates remain followed by
a fresh finite15m shared runtime window. Peer edits/binary witnesses preserved.
[Passing acceptance](audits/2026/pd-source-contract-ci-repair-2026-10-03.md).

2026-10-03 17:16 UTC: T-TESTS-004 repairs all original 20 failing cases:
16 obsolete expectations, one ambiguous function selector and three catalog/API
boundary violations. Full native PASS1,817cases/335,458assertions; queued client
and tests builds, 20 storage tests and refreshed source guard PASS. The reader
normalizes CRLF pairs; boundary bans and meaningful rejection/rollback checks
remain. All 14 frozen source hashes match, owned native processes zero and both
heavy windows released early. Scoped commit/push and exact hosted CI still
pending. Initial runtime D-010/D-012 gates remain separate.
[Acceptance audit](audits/2026/pd-source-contract-ci-repair-2026-10-03.md).

2026-10-03 16:31 UTC: exact published 950561fa CI is terminal. Four dependency
fixtures, selected runner dependency root and clean client/tests build PASS;
full suite fails 20 source-contract assertions. T-TESTS-004 records exact failure
inventory and unchanged tests/src/port/smoke trees versus d37aee06. No passing
baseline from the earlier configure failure, no assertion weakening or new
broader implementation scope. T-TOOLING-010 remains implemented, published;
all local resources zero. D-010/D-012 runtime gates still pending, followed by
a fresh finite 15-minute shared preview window. [Terminal evidence](audits/2026/pd-mingw-ci-portability-2026-10-03.md).

2026-10-03 16:14 UTC: directuser Commitandpush instruction accepted by normal
review. Exact950561fa pushed; freshorigin/dev match16:13:03/indexempty/Gitreleased,
allresources0. D-013 decided; priorhandoff withholding/denials historical and
not a blanketpersonal instruction fromMike. ExactPD2CIrun37136015560 nowrunning;
native4fixtures+targets+1655/50+PE/DLL/guardPASS remainsseparate. FollowingCI to
terminal. NoGUI/deletion/cap/grantchange; D-010/D-012 unchanged.

2026-10-03 15:46 UTC: new Mike response explicitly authorizing parent to relay
exact950561fa publication was supplied. One normalpush attempt again rejected
before execution: automaticreview says no trusteduser-authored exactauthorization
is present. No publicpendingapprovalID returned; stoppedfurtherattempts/Gitreleased.
Local950561fa/indexempty/remoteold/resources0 preserved. D-013 is an approval
review recognition blocker despite relayeduserapproval, not an inferred lack of
userapproval. ExactnewCI cannotstart untilpublication; nativePASS remains valid.

2026-10-03 15:20 UTC: parent requested one same-scope push retry using later
standing publication instructions. Normal `git push origin dev` was again
rejected before execution: trusted transcript withholds push permission and no
trusted user-authored exact reauthorization is present. No public pendingapproval
ID returned. Further attempts stopped; Git lane released, local950561fa retained,
indexempty/remoted37aee06. D-013 remainsopen; exactnewCI cannotrun untilpublication.
Native validation remainsPASS. No scopechange or indirect execution.

2026-10-03 15:14 UTC: T-TOOLING-010 native portability validation PASS: queued
client/tests targets, focused1655assertions/50cases, fourCMakefixtures, PE static
client imports/matching test DLL, source guard. Heavy15:01:52->15:04:55 released
early after native0; allresources0. Exact5file localcommit950561fa/indexempty.
Automaticreview rejected combinedcommit/push before execution for earlier
withheld push permission/missing exact reauthorization and unverified destination.
Read-only destination now verified origin MikeHazeJr/perfect-dark-2/dev, remote
stilld37aee06; no push retry. D-013 needs explicit superseding authorization for
950561fa or keep-local. ExactnewCI pending. Old68d9client/verifier preserved and
ordinary gate path corrected to retained binary; D-010/D-012 unchanged.
[Actual results](audits/2026/pd-mingw-ci-portability-2026-10-03.md).

2026-10-03 14:56 UTC: T-TOOLING-010 owns the parent-authorized hosted CI repair.
Both d37aee06 and prior0ca5f1ab fail configuration at the same hard-coded local
MinGW archive path; no diagnostic regression attribution. Selected-compiler
dependency root is connected, exact static/DLL errors preserved, four tiny CMake
contracts PASS. Native client/tests linking and exact new CI remain pending a
fresh 15-minute shared headless window, then scoped publication. No heavy lease,
game/GUI/deletion or storage budget changes; D-010/D-012 remain open.
[Portability evidence](audits/2026/pd-mingw-ci-portability-2026-10-03.md).

2026-10-03 13:44 UTC: no unrun prepared nonGUI acceptance gate; no repeatpasses
or newfamilyproducer scope. Nextordinary checklist saved with currenttested
68d9a501 client/d37aee06, exactconditional wrappercommand and fourcaptures.
D-010 user-initiatedlivePerfectDarkappapproval; D-012 nextcopydisposition at3/3
fullcopies (2pinned failures+failedshared), no silentautomaticcompaction/deletion.
Two activeREADME floorreferences corrected; temporaryPD200/run204/build206 kept.
No workers/game/UI/capture/scans/leases; pause awaitinggates. Broaderstrictaudio/
animationmembers/corpus/fidelity staypending. N-0364;
[prepared ordinary checklist](audits/2026/pd-readiness-diagnostics-2026-10-03.md).

2026-10-03 13:15 UTC: Mike's temporary PD floor200GiB applied, onlymin_free_gib
changed; build4GiB+safety2GiB kept =>206GiB admission. MandatorybackendPASS;
D-011 decided perSlack #perfect-dark1791031891.654889, noexpiry/global/Astralrule.
Finiteheadless13:00:30->13:07:46released early: queuedclient/updater andexplicit
tests buildsPASS; focused1498assertions/49casesPASS; explicit+commitguardPASS.
Exact6diagnostic files publishedd37aee06, freshorigin/dev verified13:14:27UTC;
indexempty/31peerhashes unchanged/Gitlane released. Temporarypolicy/context/
Workbench excludedfromcommit; configappliedlocally. Native0owned13:07:21UTC.
D-010GUI/fidelity pending; no deniedretry/game/capture/cleanup. Newbuildretained
as exactwitness. N-0362; [actual validation/publication](audits/2026/pd-readiness-diagnostics-2026-10-03.md).
Todaydaytime through16:45Eastern/20:45UTC; future night-only unchanged.

2026-10-03 12:47 UTC: Mike explicitly authorized PD daytime TODAY October 3
alongside Astral (Slack #updates1791030991.918729). Today's 07:00 Eastern stop
superseded; daily pause remains 16:45 Eastern / 20:45 UTC, future night-only
unchanged. Current bounded disk gate12:41:45UTC refuses252.044GiB free versus
306GiB required:53.956GiB deficit. No build loop/cleanup/policy/GUI retry.
Six source hashes match freeze, HEAD0ca5f1ab/indexempty; all resources released.
Independent21,927-byte retained ledger/texture receipt consistency PASS;
draw/fidelity/listening/playedposes/devices/fullcorpus remain pending. GroupA
last parent-verified deletion0; no assumed approval/completion. D-011/D-010 open.
N-0360/0361; [current authorization and evidence](audits/2026/pd-readiness-diagnostics-2026-10-03.md).
Native next: approved headroom, then fresh finite shared30m headless turn.

2026-10-03 03:07 UTC: bounded read-only PD storage investigation stopped at
250,000 metadata entries; FIFO released. No safe sufficient 40.429 GiB removal
set: historical compiler products 2.504 GiB are conditional review only,
current pd1001init 0.347 GiB stays, Build 0.164 GiB is outside removal scope.
Exact per-file metadata and empty proposal saved; previews, receipts, pins,
archives, previous cleanup and all source edits preserved. No deletion/move,
scan extension or floor/budget/grant/settings change. D-011 open; builds
paused, six-file Sol 6.1 unit frozen/unbuilt/untested/unpublished. D-010 separate.
Need approved headroom then fresh finite 30-minute headless turn. N-0358/0359;
[bounded findings](audits/2026/pd-readiness-diagnostics-2026-10-03.md).
Coordinator gpt-6; stop/checkpoint by 11:00 UTC / 07:00 Eastern.

2026-10-03 02:47 UTC: parent-confirmed Astral early release granted PD finite
headless turn. Actual start02:42:55UTC; mandatory isolated-build storage
preflight refused before compiler/tests (265.57GiB free,300GiB floor+6GiB
reserve/growth; observed additional40.43GiB needed). Native0ownedbuild/test/game
verified02:44:41; allPDqueues/heavyreleased02:44:43UTC, Astral notified. Six-file
Sol6.1 diagnostics stillfrozen/unbuilt/untested/unpublished; indexempty/HEAD0ca5f1ab.
No repeatcleanup orbudget/pin/grant/settings change; evidence/peer edits preserved.
D-011 restoreapprovedheadroom vsdefervalidation; D-010 GUIapproval separate.
N-0356/0357 supersede earlierqueuedwait; [actual audit and resume evidence](audits/2026/pd-readiness-diagnostics-2026-10-03.md).
Need restoredstorage andfreshfinite turn beforebuildretry; no automatic launch.
Stop/checkpoint by11:00UTC /07:00Eastern. Sourcegpt-6.1-sol/coordinatorgpt-6.

2026-10-03 02:30 UTC: actual Sol6.1 six-file readiness diagnostics frozen;
source executor exited0 and all owned launcher PIDs verified absent. Root
hash/lines/LF/newline/footer/diff/source guard PASS; client/tests unbuilt/unrun.
T008 ownership returned to root via N-0354; diagnostic admission remains strict.
Heavy request q_20261003021948054_99e2bbeb queued, not started; next30m finite
headless all+focused union awaits Astral verified release of03:20UTC window.
D-010 still requires valid PerfectDark-specific app approval or explicit GUI
deferral; no live prompt/public request ID, no denied retry/settings change.
No new commit/push; HEAD0ca5f1ab, index empty. Failed evidence and peer edits
preserved. [Dated audit](audits/2026/pd-readiness-diagnostics-2026-10-03.md) and
03night/diagnostic-source/next-headless-batch.json carry the resume instructions.
Hard stop11:00UTC /07:00Eastern, implementation gpt-6.1-sol/coordinator gpt-6.

2026-10-03 02:21 UTC: parent resumed the authorized night scope; daytime pause
superseded. Actual Sol6.1 diagnosis completed, focus LOST before Agent Select
supports intended readiness gate, denial causality unproved. Sole Sol6.1 source
worker codex-785df03e-ef90554c started six-file read-only rejection diagnostics;
source unit unbuilt/untested, predicates/fixture unchanged. Astral heavy window
through03:20UTC; PD30m headless request q_20261003021948054_99e2bbeb queued only,
after verified release/FIFO-next. D-010 valid app approval remains absent; no
GUI retry/settings/grants/Git/new game. HEAD/fresh remote0ca5f1ab verified, peer
edits and incomplete evidence preserved. N-0349..0352 current. Details:
[readiness diagnostics audit](audits/2026/pd-readiness-diagnostics-2026-10-03.md).
Hard stop11:00UTC /07:00Eastern. Model implementation gpt-6.1-sol, coordinator gpt-6.

Daytime pause checkpoint 2026-10-02 11:00 UTC / 07:00 Eastern, model gpt-6:
user-directed pause is active. Native-account verification at 11:02:37.914 UTC
confirms zero owned known workers/wrappers/disposables, managed games and session
build processes; root plus its three closed workers own zero leases/queue entries.
HEAD and fresh origin/dev are 0ca5f1ab471d2675ec14be2b9a900e84c3852efa; index empty.
No new implementation, test, build, preview, permission change or publication.
D-010 remains open; V-014 partial/blocked, 11/29 assertions, 0/17 events, 0 captures.
Failed evidence/private installs, corrected 06:36:04 release, historical false-zero
correction, retained build and peer edits preserved. No live app approval prompt
or public pending request ID is confirmed. Ownership retained; N-0348 incorporated.
Remain paused until next authorized 22:00 Eastern Oct2 / 02:00 UTC Oct3 or later
explicit instruction; GUI work also needs changed app approval and a fresh parent
window. Astral/unrelated sessions untouched. Durable proof directory:
.claude/pd-initial-integration/20261002-night/daytime-pause/ (pause-checkpoint.json
and native-process-check.json).

2026-10-02 08:01 UTC: fivefilecleanup/auditunit0ca5f1ab publishedorigin/dev,
remoteverified,indexempty,37othermodifiedfiles unchanged;Gitlane released,
0ownedresources/processes.44disposablechecks total PASS; sourceguard andinitial
lightreview pass with fidelityboundaries pending. D010 actualappapproval remains
open; no deniedUIretry/configchange. Exactnormaldesktop prompt Allow step verified,
no publicpendingrequestID surfaced/livepromptconfirmed. Source/context handoff
N0347 incorporated; parentcheckpointreturned.11:00UTC/07:00Eastern pause intact.

2026-10-02 07:42 UTC: exact owned-process cleanup is connected to bothwrapper
launches and file-backed stop entry;22actualdisposable checks eachPS5.1/PS7 PASS,
0Game/UI,otherprocess preserved,guard/parser/diffPASS. Five ownedfiles sourcefreeze
queued for scoped Git publication; peer/mixeddata excluded. RealPD cancellation
unperformed. Lightcurrentgraph27/67/53 lexicalcheck, four retainedtexture closure/
slot/RGBA joins and16rule Settingssource snapshot pass; draw/fidelity stillpending.
Malformed assetreview export retainedinvalid and superseded by field-checkedrows.
Officialone-time appprompt button Allow verified; no publicpendingrequestID
surfaced/livehostprompt confirmed. D010/failed11of29/0events/0images/corrected
06:36:04release unchanged. No deniedappretry/grant/configchanges or newheavywork.

2026-10-02 06:45 UTC: default private shared wrapper's full gate admitted actual
PD launch. Native Sky get_window_state denied "Computer Use was not approved to
use perfectdark" at06:32:12.869Z; no activation/retry/alternate UI. D010 requires
specific app approval or GUI deferral. Actualstart06:31:06/upper06:46:06; final
verifiedzero/release06:36:04, worker quiescent06:38:20. Early06:33:13 zero claim
was wrong after failed path/time termination checks; corrected receipt is final.
Native readiness180082ms timedout0/17events,11/29assertions,exit1,0captures.
Failed result/recipe/logs/fullinstall preserved; cleanup filters need repair and
confirmed disappearance before future lease release. No source/Git/budget/pin
changes. V014 partial/blocked; original visual/input/fidelity scope remains open.

2026-10-02 06:10 UTC: second native Sky preview attempt released05:54:45 after
storage refusal before game. Worker added PerTestInstall, requesting4 copies
against limit3. Light metadata admission plan confirms safe default replacement
route: two pinned failures plus completed unpinned shared/client on the same seed;
matching marker/complete recipe, previous wrapper absent, count3/byte and disk
headroom pass. Full byte/exclusive verification remains the canonical wrapper's
gate in the next window. Attempt3 exact default command/new evidence directory
prepared; no budget/pin/data changes, no active worker/game/FIFO. Astral gets the
next window; fresh parent15minute relay needed after its release. V014 latest
attempt blocked; all ordinary visual/input/fidelity acceptance stays open.

2026-10-02 05:45 UTC: freshsuppliedCLI0.159.2 exposesnativeNodeREPL/Sky/listapps,
resolving earlierPATHmismatch. V014preflightstopped05:33:58 underroot-added strict
timingcondition, noactualPDaccess/denial/game/install/lease/capture. Workerexited
05:34:19; unusedgrantreleased05:42:32,0ownedqueue/process, noactualstart/deadline.
Correctednextprompt attempts authorizedunchangedfixture andrecords actualapp/input
ordering; actualdenial stillstops,noalternateUI route. Needfreshparentwindow.
V014rootoperationhandoff/N0335-0336 current; parentnative/fidelitygoals remainopen.

2026-10-02 05:05 UTC: single requested samepush retry accepted newer explicitly
supersedinginstruction; origin/dev999cbf13 verified. D009resolved, priorapproval
question needsnoresponse; olderrejectsbelow historical. Lightpreviewpack4284bytes
preservescanonical14606867/readiness180s/cap240s/fourcaptures/seed28b2cf25. Current
tools lacknativeNodeREPL/CUA; installedCLI0.126.0-alpha.8 rejects --approve-for-me
--help(exit2), no workercreated. Parentexactworkinglauncher/nativeexecutor plus
separatePDapp/inputapproval and15m previewwindow required. Astralhasheavy; no new
desktopcall/game/capture/config/grantchanges. Native23/20/6proof remains separate
fromvisual/audible/ordinarydevice acceptance. NormalreadinessJSON unchanged.

2026-10-02 04:50 UTC: correctedprobe unit999cbf13 committed locally26paths,
guardPASS/indexempty; both normalorigin/dev push attempts auto-review rejected.
VerifiedoriginalMikeSlacknightinstruction doesnot satisfy retrievedcontent
authorization underreviewrules; D-009 requires directtrustedMikeapproval here.
No bypass/remotemutation. Heavyreleased04:33:44UTC,0ownedheavyprocess/FIFO.
Native23/20/6 and build/focusedproof below remain valid. InteractiveCUA/app-input
executor plus parent15m canonicalpreview still needed; no furtherheavyuntilrelay.

2026-10-02 04:33 UTC: corrected native gates pass audio23/23, clip-bounded
animation20/20 and MP36/6 with10nativecases on client7CB077E3/seed28b2cf25.
Queued client/tests pass; focused12cases488assert pluspreview1/4 pass. Console
drains710587bytes with131072retained; scheduler1006ms/nativeexit0. Prior transport
abort stays failed/pinned. Actual heavy04:19:15-04:33:44, upper04:49:15; released
zeroownedprocess/FIFO. Four-record operational ledger9781bytes/hash6792ccca reads
receipt/seedmetadata only; strictmembers/draw/fidelity pending. Light scoped probe,
harness and current Workbench publication follows six pushed units latest8bda4483.
Supported CUA/app-input executor plus parent15m canonical preview remains needed;
keep180s readiness/240s nativecap/fourcaptures. UI/restart/physicaldevices and full
corpus/contact acceptance remain open. Deadline11:00UTC; no cleanup/bulk staging.

2026-10-02 02:56 UTC: six scoped initial units are pushed and remote verified,
latest8bda4483: graphb0db47d7, Settings81fc120d, preview289d6ca1, texturesa2648384,
native producer382b50af, bounded tooling8bda4483. Fresh93/19/17 tiny checks and
27/10/5 eachPS5.1/7 pass; retained native client4AE22FA3/seed6b8d0268 proof unchanged.
Current Workbench summaries preserve history and clearly leave actual draw,
contact sheets, animation/audio/nonvisual fidelity and ordinary UI/devices open.
PD0heavy processes/leases; parent requested Astral10m window02:51UTC and PD yielded.
NextPD15m canonical sound-enabled audio source gate awaits explicit handoff;
preview follows separate native app approval/window. Deadline07:00Eastern/11:00UTC.
See audits/2026/pd-initial-night-checkpoint-2026-10-02.md. No cleanup/bulk staging.

2026-10-02 02:20 UTC: night work resumed21:51:51Eastern under N0315; release
deadline07:00Eastern/11:00UTC. Graph inventory four-file unit b0db47d7 is pushed
and verified on origin/dev after11 tests/source checker/guardPASS. Settings unit
review/publication is active under N0316; retained native config/profile proof
remains separate from unrun ordinary UI/restart/device acceptance. No heavy
launch this night yet; awaiting bounded15m audio/source and separate preview
handoff with Astral. No unrelated source, historical cleanup or broad staging.

2026-10-01 21:19 UTC: desktop runner recovery option recorded from parent transcript
01a0f915-4c31-7792-81e2-4d1c1b9300ef. Official ephemeral codex exec --approve-for-me
under Mike's account reportedly preserves workspace-write; 186-byte receipt
corroborates mcp__node_repl__js exposure, @oai/sky import and Edge enumeration.
Earlier standalone CLI used CodexSandboxOffline profile. No PD foreground/input
proof follows. Consider only at next approved PD interactive window, coordinated
with transcript research; no desktop/heavy operation occurred in this handoff.
Evidence20261001-desktop-recovery/summary.json. Initial acceptance remains partial.

2026-10-01 18:25 UTC: normal-caller readiness result is now saved and PASS. Repo
readiness/latest.json reports caller/input Default, WinSta0, error0, foreground
present. No game/focus/desktop change. Keep separate from18:23 child's saved
CodexSandboxDesktop/error5 failure, which describes agent context only. No probe
retry occurred here; files/history untouched. Menu/game input acceptance still
unrun; missing-user-JSON blocker is removed. Parent can read latest.json directly.

2026-10-01 18:06 UTC N0308/0309: texture dependency native cohort accepted. Queued
client/tests PASS, five C++ cases/48 assertions and guard PASS. Client4AE22FA3,
new seed6b8d0268, unchanged public source. Single canonical gate27/27, native/runner
exit0; seven strict traces verify five archives and all four texture source joins.
Five load passes; draw/fidelity remain pending. Heavy finished18:06:23.4809UTC;
wrapper33728 gone, no owned games/FIFO, Astral notified before documentation.
Evidence20261001-dependency-native/summary.json. Broader initial scope partial.
Readiness follow-up N0310: old probe saved stdout only; new user-run
tools/smoke-verify/run-interactive-readiness.cmd keeps console visible and saves
.claude/pd-initial-integration/readiness/latest.json plus history. Five synthetic
file checks and safe CompileOnly pass each PS5.1/7; no desktop probe executed.

2026-10-01 17:10 UTC N0306: lightweight texture dependency candidate source complete,
unbuilt/unrun. Actual texture consumer bytes/generation bind to parent source hash/
slot; strict join keeps draw/fidelity pending. Final93 Python +27helper assertions
eachPS5.1/7 +guardPASS; five C++cases48asserts await newbuild. Standard MTL catalog
reference omission fixed. Retained C5 native model proof bound to five inventory
rows:1loadPASS/4dependency sources pending. Metadata9018archives/3labeledpending
views/0images/3,855,105bytes, no payloadreads/installcopies. Next single unchanged
animation/CCTV gate adds4texturewitnesses;11staticchecksPASS. Source draw-seam
inventory only; no drawhook. Request new20m headlesswindow after Astralhandoff;
no heavylaunch, identical readinessprobe retry, Git or cleanup. Summary/evidence
.claude/asset-inspection/20261001-dependency-prep/. Broader initial scopepartial.

2026-10-01 16:10 UTC N0304/0305: corrected archive-owner native proof accepted.
Client/tests buildPASS; five C++cases39assertions/guardPASS. Same single animation/
CCTV gate23/23/nativeexit0, strict trace7members/6522eventbytes/rom_fallback=false.
ClientC5AEA61C/seed1f3a0480, publicsource46807f6b unchanged. Four texturedependencies,
draw/fidelity and broader ordinary UI/device/corpus gates remainopen. Heavyreleased
16:10:06.2659071Z (6.266s finalizer overrun), ownedprocesses/queues zero; Astralnotified.
No additionalruns. Compact20261001-owner-retry/summary.json; firstfailure retained.

2026-10-01 14:20 UTC N0303: supported native CUA installed/enabled, but required
node_repl/@oai/sky is absent from this delegation's334 callable tools. Ordinary
desktop readiness unobserved/unrun; shell denied5 does not establish all-route
denial. Parent with exposed native CUA can perform already-authorized exact probe;
do not automatically require Mike to do it. Runtime guidance and exact tool list
retained20261001-desktop-capability/. No workaround/game/capture/settings change.
Corrected member source frozen/unbuilt; new15m headless grant still pending.

2026-10-01 headless12:52-13:27 T-TOOLING-008/009: client/tests builds PASS, five
C++cases35assertions PASS. Prepared gate normalexit0 but22/23FAIL, no member receipt;
selected member locator was confused with typed archive owner. Root correction
source-frozen/unbuilt; four new assertions within same five C++cases pending.
Python82/guard/diffPASS. Heavy ended13:19:23UTC; unstarted rebuild cancelled,
zero owned heavyprocess/lease, runner18484 gone. Astral may proceed. New15m
headless window requested, not granted; no extra batch/input/capture. Failedrecipe
and binaries retained; native-proof/summary.json + corrected-source-freeze/.

2026-10-01 12:12 UTC T-TOOLING-008/009: opt-in native model member producer
connected in source; five C++ cases registered/unbuilt under Astral heavy hold.
Private runner verifies frozen binary/public-set/archive identities and requires
complete events. Actual CCTV inventory:22 members/four texture IDs,75,349 bytes
read, zero installs; load/render/dependency pending. Python81 +22 synthetic helper
assertions each PS5.1/7 PASS; guard/diffPASS. Original native/63-character receipts
unchanged; contacts are preparation. Parent35m queued build/headless need;
ordinary user preflight then parent15m preview. Full asset/animation/audio/
nonvisual fidelity, Settings edit/restart and physical devices remain open.
ClaimsN0297-0299; proof-boundaries audit and native-emitter evidence directory.
12:21 supplement: actual CCTV plus four texture sources inventoried, five archives/
66 members/121,076 bytes read; all load/fidelity pending, no native texture/image proof.
12:29 final tool suite82 PASS and guardPASS; source frozen, native build pending.

2026-10-01 10:32 UTC T-TOOLING-008/009 lightweight continuation: strict event
converter27 tests, seed/contact planner17 and existing ledger22 PASS; guardPASS.
Metadata-only9018 top-level archive index and12 labeled pending capture slots;
no nested members/catalog identity/capture claims. Original63-character/native
texture proof preserved against clientD9CFC0B8/seed8227ba09. No engine emitter yet,
so strict receipts still pending. Read-only readiness helper compile-onlyPS5.1/7
passes, desktop probe notrun. Smallest nextstep ordinary authorized caller preflight,
then parent15m canonical preview gate (no rebuild), not same sandbox denial retry.
Keyboard traversal follows separately; Settings edit/restart/physical devices and
full corpus/listening/fidelity remain open. Astral retains heavy lane; no native
launch, source payload scan, Git or cleanup. See asset-inspection-proof-boundaries
audit and .claude/asset-inspection/20261001-receipt-prep/prepared-final/.

2026-10-01 09:53 UTC T-TOOLING-008/009: bounded native texture repair validated.
Queued client/updater and explicit tests pass; texture12 cases/189649 assertions,
source6/6 cases (15 assertions), base animation19/19, body/head13/13 and all63
character templates (6 assertions) pass. Two residual4095 material bind checks
and explicit CMake test registration were fixed from first-wave evidence. Final
clientD9CFC0B8/new seed8227ba09; old source4/6/animationAV and earlier35/1/27
character remain historical receipts. Operational63-record ledger is not strict
member/render/fidelity proof. Heavy released09:40:56.8137169UTC, zero own games
or heavy queues; Astral notified. Ordinary UI/device/Settings/captures require an
authorized interactive executor; strict member/corpus/contact-sheet and fidelity
remain open. No publication or repeated cleanup. Final source guard passes.
Proof .claude/pd-initial-integration/20261001-native-validation/summary.json.

2026-10-01 08:23 UTC T-TOOLING-008: lightweight source repair complete, native
validation awaits parent heavy window. Texture range and full native carrier chain
widened consistently; snapshot theft fixed, lifetimes preserved, diagnostics added.
Native cases prepared, unrun. Frozen-seed path passes Python19/19 and PS17 tiny
checks; normal imports still hash current sources. Inner phases will measure full
setup next window. Own sandbox desktop/input read denied5 prevents foreground
acceptance; authorized interactive executor required for menus/captures. Request
75-minute queued all rebuild/tests/source/animation/body-head/character window.
Prior35pass/1fail/27untested retained. No active leases/publication; initial scope
incomplete. Proof20261001-texture-repair/summary.json and next-window-plan.json.

2026-10-01 07:22 UTC T-TOOLING-008 renewed native window: corrected preview
11/29 fails actual foreground readiness after bootstrap; zero inputs/captures.
Independent character 2/6 fails native texture slot allocation at fbiguy after
35 template passes (one failed, 27 untested). Completed receipts/full installs
retained. Seed/native/complete: character 340.046/100.515/67.607s; whole 512.831s.
Animation/keyboard unrun because a retained failed workspace forces a new private
checkout and full canonical limits could not fit. Batch ended 07:12:54; all heavy
leases released 07:14:30 UTC, no owned native process. PS5.1 deep-JSON witness
metadata caused growing finalizer, contained and recovered without relaunch;
plain-text witness fix has ten passing checks in both PS5.1/7. Source diagnosis
finds 593 shared private texture slots under 12-bit packing; engine repair/build
and foreground-compatible ordinary acceptance remain blockers. Per-template
operational ledger is not strict member/fidelity proof. Current evidence:
.claude/pd-initial-integration/20261001-corrected-run/summary.json.

2026-10-01 06:38 UTC T-TOOLING-008 runtime handoff: preview failed 10/29
assertions with 0/17 input events and no screenshots. The retained shortened
30-second readiness wait expired during weapon bootstrap, before main-menu
arming. Keyboard, character and animation were not launched. Heavy leases
released 06:28:48 UTC; no current owned build/game/capture. New private variants
restore canonical input sequences and 180-second readiness, preview 240-second
and keyboard 280-second overall limits. Canonical contracts, actual ordered
discovery and source guard pass. Request a new 35-minute parent window, no rebuild.
Warm overhead measured 153.878s; conservative budget retains 318.099s per fixture
plus 760s native caps and 67.606s margin. Body/head 13/13 pass remains valid;
ordinary menus/render, per-member receipts and family/device fidelity stay open.
Evidence: .claude/pd-initial-integration/20261001-combined-run/summary.json and
.claude/pd-initial-integration/20261001-readiness-corrected-prep/proof.json.

2026-10-01 06:14 UTC T-TOOLING-008 lightweight preparation: Astral owns heavy
resources; no new game/build/asset scan. Read-only seed/recipe comparison shows
13,105/13,119 files and 99.99949% seed bytes reusable through existing verified
private reset. One ordered preview/keyboard/character/animation batch is prepared
and selected by actual existing discovery/selector functions; canonical scenario,
assertion and boot contracts preserved. Next request now35m combined window,
superseding two15m estimates: cold318.099s overhead x4 +595s native caps +232.606s
margin. Verification/isolation/receipt controls unchanged; warm cost unmeasured.
Archive identity witness cannot attest consumed members; new exact negative
regression makes22 ledger cases pass0.122s. Plan/selection proof:
.claude/pd-initial-integration/20261001-combined-prep/. Per-member producer and
actual remaining family/menu/capture acceptance still open.

2026-10-01 05:51 UTC T-TOOLING-008 native checkpoint: granted bounded window
produced body/head PASS 13/13 assertions, ten native cases, clean exit 0 on exact
retained client. Managed seeding/completion now has real native proof: 13,119 files,
355,636,638 seed bytes, completed reconstruction receipt. Whole command 366.535s,
native 48.437s. Heavy lane and all runtime/capture FIFOs released 05:51:17 UTC.
Character/animation/preview/keyboard fixtures UNRUN: insufficient remaining budget
after storage setup/finalization. Next parent windows: 15m preview+keyboard, then
15m character+animation, no rebuild. No screenshots or per-member ledger producer;
family fidelity, audio/nonvisual and ordinary Settings/device acceptance stay open.
Proof: .claude/pd-initial-integration/20261001-runtime-window/summary.json.

2026-10-01 04:48 UTC T-TOOLING-008 takeover: current user delegation continues
the approved initial asset/menu scope; the former child's pause is historical.
Native Windows Python routing is fixed in storage and the related ledger driver
without altering canonical TEMP/TMP/PATH. Tiny wrapper 13, storage 14 and ledger 21
checks pass; synthetic inventory, parsers and source guard pass. Existing client,
pd-tests and Settings-save builds and native gates remain passing: focused 10/82,
broad 246/10,033 and Settings-save 14/236 (overlapping selectors). Real runtime has
not retried. Astral holds heavy resources; parent 15-minute sequential runtime/
capture window requested before body/head, character, animation and menu fixtures.
Actual-read per-member receipt producer, labeled fidelity views, full family
behavior and ordinary Settings/device journeys remain open. Model read-capture
seam reviewed, production unchanged. All dirty source/evidence preserved; no
cleanup repeat or Git operations. Audit: audits/2026/pd-initial-integration-2026-10-01.md.

2026-10-01 initial source wave (T-TOOLING-008; Sol 6.1 workers, GPT-6
integration): Mike's 03:09 UTC "approved, begin" supersedes the old Plex pause.
T-TOOLING-009 adds bounded per-asset inventory/receipt attribution and a storage
planner; 21 tiny cases pass. T-ASSETS-051 repairs preview framebuffer mapping,
deferred viewport/projection state and failed-selection readiness; native proof
pending. T-MENUS-007 preserves all sixteen device rules through Settings and
startup config/restart; eight native cases written, not run. T-MODDING-010 adds
27 reviewed operation boundaries/67 source references/53 definitions and an
11-case passing checker; full graph migration remains open. Test registrations
and an explicit queued/DLL-safe Settings test target are integrated. No build,
game, extraction, capture or further cleanup ran; parent must serialize native
validation with Astral. Source candidates remain partial until those gates pass.
Audit: audits/2026/pd-initial-integration-2026-10-01.md.

2026-09-30 T-TOOLING-006 (GPT-6): Mike-approved prospective smoke controls
implemented in production runner and install wrappers: immutable exact-content
archive, private mutation copies, exclusive verified/reset shared reuse, compact
reconstructable evidence, 3 full installs/8 GiB + 8 GiB archive budgets and
configurable 300 GiB free-space floor with pending growth. Historical evidence
untouched; completed cleanup not repeated. Active/pinned/changed/locked/aliased/
incomplete evidence refuses more copies. Tiny gates: 14 Python cases in 2.687s,
11 PowerShell checks in 1.586s; source guard and PowerShell parsing pass.
Five 10,880-byte synthetic runs: only 3 full installs, archive 5,333 bytes;
modified receipt 927 bytes. No native game/build, Git stage/commit/push or other
session control. Direct build-session starts now check the policy; standalone package/extraction adoption is separate;
preflight is not an OS quota. Audit: audits/2026/smoke-storage-prevention-2026-09-30.md.


Sep30 T-TOOLING-005: authorized bounded historical `.claude` cleanup completed:
3,333,693 files / 99,925,340,147 bytes (93.063 GiB) removed; 48,633 files /
1.415 GiB excluded. Retained-evidence metadata/identity checks (71,691 files) and
3294 source SHA-256 comparisons show zero drift. No active-use or permission/
approval blocker. C: free 365.3 GiB; Steam uninstalls are separate. Mike stopped
exhaustive deduplication and knowingly accepted loss of ignored outputs.
Exact receipt: .claude/cleanup-manifests/20260930T164808Z-approved-cleanup/summary.json.
Automatic age cleaner is now a no-op; new manifest tool never deletes. Shared
asset/private-copy reuse, compact receipts and bounded smoke budgets are now implemented by T-TOOLING-006.
Audit: context/audits/2026/smoke-storage-cleanup-2026-09-30.md.

Sep29 22:32 T-MENUS-003/004 continuation: first mapped Accept now reveals
native navigation, Agent Select requests its selected row on entry, and the
virtual SDL fixture maps every advertised button (including Touchpad) without
changing hardware mappings. Combined client/tests and broad241/9995 pass.
Current full1857 cases/141173 assertions retains the same18 failing names;
zero source drift. Both old and new full receipts remain; no passing full gate.
Isolated873C ordinary run reaches Create with one A, but fails26/29 because
background Agent Select consumes the second Back (results-20260930T022335Z).
The shared menuRenderDialog admission now queues only menu->curdialog.
Client2649B339 and the current broad241/9995 pass with3408 source paths unchanged.
The unchanged runtime retry times out11/29 before attachment because Windows
reports foreground=0; it does not test the new Back fix. Receipt:
results-20260930T023149Z; immutable source/binaries and sep29-r3 receipts under
.claude/menu0927. Normal captures show a black character preview and stretched
background geometry, handed to asset owner. 150% fixture prepared, not run;
Settings keyboard traversal, save/restart/failure, other reachable menus and
physical devices remain open. User resumed work; complete-plan acceptance is
still partial. A foreground-access question is pending; all client lanes freed.

Sep29 T-MENUS-005 Settings/input candidate passes shared isolated client/tests
builds, broad238 cases/9829 assertions and real settings-save11/123, with3405
source paths unchanged. Full suite remains red18/140987 across1853 cases;
the18 failure names match the prior receipt. Capture loss, checked Video/Game/
mouse saves, Agent dirty invalidation, retired split removal, local P1 save/load
slot0 and obsolete co-op swap removal are connected. Ordinary save/restart/
return, physical controller and scaled acceptance remain open. Artifacts:
.claude/menu0927/sep29-validation.json and sep29-full-result.json. Frozen older
Agent805E2213 virtual run now attaches player0 with process-local SDL native
driver exclusion plus explicit touchpad mapping, but A does not enter Create:
red15/29, results-20260930T015959Z.json. It overlapped the asset session's separate
resource named game, so focus isolation is unproven; screenshot inspected.
No hardware/system assignment changed. Goal remains active and partial.

2026-09-29 T-MODDING-002 first production v2 profile passes isolated client/tests,
148 native cases/4941 assertions, source guard, and installed41/41 with26/26
harness cases. Frozen3408 source paths show zero drift; binary873C547A.
Captured public source selects real native single-shot actions, per-hand state
and ammo, with neutral idle equipment, exact model/command/audio/casing leases,
deferred shot retention, failed-replacement rollback and death retirement.
held_single_shot.v1 currently admits offline custom weapons only. Base overrides,
automatic/burst/beam/charge/throw/melee/device, projectile/entity cutover,
network/bot/Theater execution and v2-to-v1 demotion remain open. Actual world-hit,
render/controller/Modding Hub acceptance and the full six-condition asset goal
remain partial. Next: base single-shot parity and ordinary world-hit delivery.
Receipt: context/audits/2026/graph-production-cutover-2026-09-29.md.

2026-09-29 T-MODDING-002 retained equipment batch passes client/tests0,
native143cases/4754assertions,guard0,installed39/39 and2012-path drift0. Exact
compiled model queue delivers old source after newer same-ID catalog loading;
custom weapon identity follows equipment/deferred lifetime through reset and
atomic rollback. Public explicit idle ammo metadata never chooses a branch;
hand selection clear preserves deferred copies. Full v2 scanner/catalog/hand/
action/shot wiring and all-family closure remain open. Receipt:
context/audits/2026/equipment-native-lifetime-2026-09-29.md.

2026-09-29 T-RUNTIME-001 model slot lifetime follow-up passes isolated client/
tests builds, native123/4163, body/head8/322, guard and installed38/38. Real
source generations retain exact private slot identity through reset/rollback;
2,011 recorded sources unchanged. Next: exact v2 queued model generation,
weapon slot lifetime and complete ordinary catalog/idle/hand/deferred-hit
cutover. Receipt: context/audits/2026/model-slot-lifetime-2026-09-29.md.

2026-09-29 asset/graph goal resumed after canonical Workbench recovery.
T-RUNTIME-001 held model stage lifetime passes client/tests builds, 109 cases/
4,040 assertions, guard and installed source/lifetime harness 37/37. Retained
source fingerprint rechecked: 2,011 files, zero changes. Commit only the owned
unit; menu and MP candidates remain preserved and paused. Next ordinary graph
catalog/idle/hand/deferred-hit integration; full closure-plan goal stays partial.
Receipt: context/audits/2026/model-stage-lifetime-2026-09-29.md.

2026-09-27 04:46 ET T-MENUS-005 USER PAUSE. Source remains frozen and
uncommitted in seven owned source/test files (mainmenu, nav_input cpp/h,
prefs_agent c/h, widget-navigation and Settings static tests). Backup patch
and hashes: .claude/menu0927/pause-owned-source.*. Agent checkpoint4db5233a
was already pushed. Capture disconnect passed235 cases/9765 assertions on
asset0923lang with2010 source hashes unchanged; capture-broad.xml is that
receipt. Later Video/Game/mouse save requests, retired split removal and
Agent dirty invalidation are UNVALIDATED, awaiting the peers' combined build.
No new tests/builds/client runs authorized until resume. No owned FIFO remains.
Open persistence finding: Settings indexed legacy options use slot0 but solo
Agent capture/apply uses slot4; repair the adapter, not only save triggers.
Runtime memory guard remains below2GB available commit; no bypass/hardware
changes. Full18-failure receipt and all ordinary/scaled/physical/full-matrix
acceptance remain open. Resume from settings-control-coverage-2026-09-27.md,
refresh ownership/peer build results and exact hashes before further edits.

2026-09-27 04:50 ET T-NETWORKING-011 USER PAUSE: movement ordering source applied,
uncommitted. Integrated builds, focused172/14417 and guard pass; source3432 drift0.
Co-op/Counter-Op/decline runtime queues cancelled before start. No owned run remains.
Full movement authority/reconciliation remains open. Detail and next boundaries:
context/audits/2026/multiplayer-movement-ordering-2026-09-27.md.

2026-09-27 T-MODDING-002 ownership/publication checkpoint: delayed gset copies
retain whole equipment generations; captured archives prepare both modes with
one shared model lease; publication is atomic and removed modes reject stale
candidates. Client/updater/tests builds,108 cases/4,018 assertions and guard pass.
Next: production catalog activation, explicit idle policy, native model/weapon
slot ownership, hand dispatch and deferred shot delivery. Full goal stays partial.
Receipt: context/audits/2026/graph-ownership-2026-09-27.md.

2026-09-27 T-NETWORKING-011 resumed after canonical Workbench recovery.
Integrated builds,194 focused cases/16,838 assertions and native guard pass.
Runtime remains red94/101 (missing Needler effects) and90/96 (obsolete spawn
log assertions, now corrected with both prepare/commit required). Menu source
drift is recorded; fresh current-source qualification remains required after
the graph session's source window. See multiplayer-admission-2026-09-27.md.

Sep27 menu goal resumed. T-MENUS-003 Agent Select default-save failure and
native-focus candidate is applied, with current source-contract expectations
updated. Next: actual Agent save-failure/retry and focus/scroll journeys, then
remaining menu matrix. Builds pass; final r3 broad234/9745 and real-save11/123
pass. Full suite stays red
18/140071; original broad red2/9670 retained. See .claude/menu0927/validation.json.
Copied/deleted row focus and context-menu Load dispatch are repaired and built.
Ordinary Agent journey was refused before launch by B-801 memory guard
(923MB available commit,2048MB floor); no input ran. Retry only after adequate
headroom. r3 receipt: .claude/menu0927/validation-r3.json. Native sources are
preserved under .claude/menu0927/source-snapshot-r3 for the805E2213 binary.
The Sep23 full red and unverified physical/controller/MKB gates remain open.


2026-09-26 T-NETWORKING-011 resumed: connection-bound, deduplicated and
bounded transfer admission plus missing-directory-suffix extraction repair
pass isolated client/tests builds, 80 focused cases/12,431 assertions and the
native guard. The unchanged two-install direct content fixture passes 89/89
(results-20260926T201254Z.json, binary D42279ED; 1,892-file source diff0).
Retain first runtime red60/89 and native PDCA red6 receipts. Next is the
source-confirmed declined-content room audience defect; movement authority,
third peer/fault matrix, relay/WAN, migration and device/audio gates remain open.

2026-09-27 asset goal resumed after canonical Workbench recovery. All three
retained c3842 static failures are resolved against current source contracts.
Isolated tests build, combined asset/graph/audio 130 cases/9,749 assertions,
18 Python archive audio tests and native-source guard pass, with zero drift
across 2,083 compiled/test files. Receipt: context/audits/2026/asset-contracts-2026-09-27.md.
Next: ordinary catalog publication, per-hand input/admission/native lifecycle
and selected-action retirement. Full closure-plan scope remains unchanged;
no ordinary executable graph gameplay proof yet.

2026-09-23 T-MENUS-003/004/006 and T-INPUT-006/007/008 pause checkpoint:
Mike requested that the current menu unit finish validation, commit, and
pause. Source now removes the data-only Room Level Editor tab by his choice,
routes generic dialog open/close actions correctly, removes a false social
profile preview, repairs Forge bot request and live-character teardown, and
isolates smoke cleanup to its own child processes. The previous fixed-source
`menu0923` r7 client, focused menu 846/846, and broad input/menu/settings
9,732/9,732 pass. The newer Forge lifecycle and static-test corrections pass
a final isolated client/updater/tests build, focused menu 846/846, adjacent
menu/tooling/cutscene 89/89, and broad input/menu/settings 9,732/9,732. The
full suite remains red 20/131,286 on asset/catalog and engine/character
source guards; retain both r7 and final full red receipts. Ordinary Agent
virtual-controller input never fired because this
host desktop reported no foreground window, so physical controller, MKB,
scaled captures, full journeys, and all reachable-menu acceptance remain
open. See `context/audits/2026/menu-accessibility-2026-09-06.md` and the
current menu/input pillars. Do not restart menu implementation after this
checkpoint until Mike resumes it.

2026-09-23 asset/graph goal resumed. The executable closure sequence and
definition of done are in
[asset-graph-goal-closure-plan-2026-09-23.md](designs/catalog/asset-graph-goal-closure-plan-2026-09-23.md).
T-ASSETS-042's seven active NTSC locale sources now emit 476/476 public
`.pdlang` archives (68 banks per locale). Independent ROM-to-public-source
parity passed 476/476 with every raw candidate represented; isolated client
and tests builds, focused language C++ 10/10 (291 assertions), Python 17/17,
native-source guard, and conformance selftest passed. The ordinary catalog
registered all 476 language archives. The later upgraded-install repair below
resolved the separate 221 head/body/character registration failures. JPN-final
packed Japanese, native glyphs,
OS locale preference, and visual locale acceptance remain open. Next critical
path is ordinary equipped graph-v2
single-shot gameplay, then all 27 edited-source production witnesses, full
graph lifecycle migration, the requested pack, and a source-frozen closure
campaign. The September 22 pause instruction was explicitly lifted by Mike's
"Continue the goal"; the other paused sessions retain their own holds.

2026-09-23 T-EXTRACTION-001 upgraded-install repair accepted within scope:
the final client passes both fresh ROM-only and upgraded installs with
5,562/5,562 catalog registrations and zero failures. Dependency checks refresh
84 unchanged heads, 68 bodies and 63 characters whose nested public sources
lagged current top-level archives. Six obsolete untouched renderer-effect
placeholders moved to effects/_retired; an edited-source negative kept its
exact SHA-256 and failed explicitly. Focused C++ 18/18 (613 assertions),
native-source guard, conformance selftest, and full fresh 27-family conformance
(9,018 root / 10,019 recursive archives) pass. This resolves the 221
upgrade-only boot failures above; lossless extraction, edited-source runtime
proof for all families, D-006A graph gameplay and final play acceptance remain
open.

2026-09-23 T-EXTRACTION-001 dependency propagation accepted within scope:
weapon mesh/animation/audio and arena/mission scenario parents now check
embedded bytes against current
top-level public archives before reuse. Changed dependencies refresh only
recorded-unmodified parents; edited parents fail intact. Isolated all-target
build, focused 18/18 (613 assertions), guard and conformance selftest pass.
Four source-only edits refreshed 11 weapons, one arena and one mission, with
5/5 nested bytes matching current public dependencies and 5,562/5,562 catalog
admission. Three edited-parent negatives retained exact SHA-256 and stopped
the incomplete boot. Receipt:
`.claude/session-builds/asset0923deps/dependency-batch-receipt.json`.
Graph production cutover and all-family edited-source gameplay remain open.

2026-09-23 D-006A graph audit and pause: v2 compilation, equipped preparation,
per-owner instances and selected-action gset copies have harness coverage,
but no ordinary client caller publishes or binds them. `assetcatalog_load.c`
still registers v1 held records, and `bgunTickIncIdle` admits attacks from
`hand->triggeron`. Resume with one complete public-v2 catalog to per-hand
single-shot lifecycle, proving input/gates, shot/debit/sound/animation,
selected-action copy and retirement using a base gun and edited branching
custom gun. No graph source changed or graph validation ran in this audit.
Mike requested a pause; do not restart this goal until he resumes it.

2026-09-22 root asset/graph session pause checkpoint: Mike requested a scoped
commit and push, then a pause. The owned extraction, public-source runtime,
modding, and modular graph changes are being checkpointed on
`codex/asset-graph-pause-20260922`. Broader gameplay, distribution, fidelity,
and requested mod-pack acceptance remain open; do not resume work until asked.

2026-09-22 T-RUNTIME-001 long-install private cache repair ACCEPTED within
scope: on settled HEAD `e3ffedbb`, isolated client/updater/tests builds passed,
fresh focused catalog-source contract passed 1/81, and the native source guard
passed. Fresh client `76CA2D10` passed the installed per-test long-path
source-only animation smoke 10/10: `base:animation_character_ao` selected
`$H/mc` compact source-hashed private cache and activated through the catalog,
then exited cleanly. Receipt: `results-20260922T233640Z.json`. The first
short shared-install fixture attempt remains RED9/10 and is superseded only
for this specific cache-path gate. Broader extraction/runtime and ordinary
graph gameplay remain partial.

2026-09-22 T-RUNTIME-001 long-path smoke first gate RED9/10 by fixture
configuration: the default shared install was short, so public animation
activation passed without selecting the compact cache. The smoke runner now
honors this fixture's required per-test install layout, which makes the
actual install path long enough to exercise the fallback. Corrected installed
gate is pending; RED9/10 is retained as a non-accepting receipt.

2026-09-22 peer T-INPUT-008 menu/input session PAUSED by user after its
scoped commit `6044d0fa` was pushed to `origin/dev`. Its latest combined
client/updater/tests build and focused menu contracts passed 12 cases / 957
assertions; the earlier broad menu gate remains red4/9679, and ordinary
physical-controller, visual and full UI acceptance remain open. No further
menu build/run/edit should be restarted until Mike explicitly resumes that
session. Root asset work continues on the new HEAD.

2026-09-22 T-RUNTIME-001 private cache path repair APPLIED, pending the
combined verification gate: a long portable install exhausted the Windows
CRT path budget for extracted base animation source, so the compiler now
tries deterministic flat `$H/mc`, `$S/mc`, `$B/mc` private cache roots after
the readable hierarchy. A full SHA-256 key frames catalog category and ID,
asset/source kind, full source digest and compiler versions. The public
`.pdanim`/glTF source and source-only failure policy stay authoritative.
The focused long-install animation smoke must prove source activation and
compact cache selection before this repair is accepted. MP's separate
short-install peer smoke reached the loader but stopped red35/67 at an
invalid weapon-set lobby-start gate before mod transfer; it is not a
distribution pass.

2026-09-22 peer T-NETWORKING-011 F12 distribution budget component
ACCEPTED within scope: isolated client/tests builds, direct allocation/
budget4/4, and asset source guard passed on frozen source3623. Its ordinary
two-process typed pdmod real-peer smoke stopped red20/44 before transfer:
the host could not fit the private cache path for
`base:animation_character_ao` under the long smoke install root. This may
be a product path-budget limit, not solely a fixture issue. MP retained
results-20260922T225929Z.json and will try a byte-identical short-root
fixture after the current combined source gate. No real-peer distribution
acceptance or full multiplayer claim.

2026-09-22 T-MODDING-002 authored v2 state batch ACCEPTED within scope:
`gate.state_bool` and `state.bool_set` now parse strict named boolean
parameters, reject read-only/malformed keys and unsupported output ports,
and route through per-bound-instance state initialized false. An authored
setter may arm a later press or feed a gate in the same activation; an async
native completion can clear state. Focused cases cover independent hands,
source edits, set/clear, binding reset and bad source. The combined
menu0922 client/updater/tests builds passed on unchanged graph source hashes;
freshly built graph-v2 43/43 cases and 582 assertions plus the asset native
source guard passed. Ordinary equipped v2 gameplay
and complete base behavior profiles remain open.

2026-09-22 peer T-INPUT-008 raw key-source component PARTIAL: isolated
client/updater/tests builds and focused direct raw836 plus input/actionmap/
glyph/custom1582 assertions passed. The broader input/menu/settings gate
stopped red on four of 9,679 assertions: protocol59, catalog reset, Agent
save literal, and shared delete OK literal. The menu owner is auditing stale
source-string expectations and one glyph winner-source label edge in a later
coordinated window. Root folded this reported result; no controller/UI
acceptance is inferred from the focused passes.

2026-09-22 peer T-NETWORKING-011 voice VAD/device capability component
ACCEPTED within scope: isolated clean client/tests builds, native voice17/17
including synthetic SDL Opus/Ed25519 and no-codec branch, and native asset
guard passed on frozen source2156. This is component evidence only; audible
device/UI/socket and full multiplayer acceptance remain open. Root folded
the peer's reported result; MP owns its audit, connectivity pillar, bugs and
UNRELEASED evidence.

2026-09-22 T-MODDING-002 v2 identity/function bridge ACCEPTED within scope:
the executable graph catalog now admits exact extracted `base:` identities,
and selected/copied native `gset` actions check their primary or secondary
catalog function instead of hard-coding primary. Focused cases cover base ID
admission and rejection of ambiguous IDs, cross-mode rejection, secondary
copy/retirement, and independently retained source generations. Isolated
client/updater and explicit tests builds passed, freshly rebuilt graph-v2
41/41 cases and 547 assertions passed, and source guard passed. The earlier
39/501 run used a stale tests binary and is not evidence for the new cases.
Ordinary equipped weapon activation and full base graph migration remain open.

2026-09-22 T-RUNTIME-001 model catalog-mutation proof ACCEPTED within scope:
the installed generation fixture now registers an ordinary ASSET_MODEL row,
loads the edited public OBJ through catalogLoadStageAsset, verifies the native
model matches its independently retained generation, releases stage ownership,
then checks that a bad replacement image cannot reactivate the row. Isolated
client/updater/tests builds, source guard, and installed nested25/25 plus
smoke35/35 passed with clean exit. Runtime evidence:
.claude/smoke-verify-runs/results-20260922T222736Z.json. This proves ordinary
catalog model source mutation and fail-closed activation, not rendered parity
or complete equipped graphs. Peer T-NETWORKING-011 separately
reports localhost signed file transfer smoke6/6 with 4097-byte bidirectional
payloads, final-ACK replay, installed SHA/JSON and one publication; UI/WAN and
full multiplayer acceptance remain open.

2026-09-22 T-MODDING-002/T-RUNTIME-001 model-generation source batch ACCEPTED
within scope: the ordinary catalog model loader now builds public OBJ/glTF/GLB
through an owned generation, captures selected archive/mesh/optional metadata
reads, retains each source or catalog texture generation, rejects a concurrent
source edit, and retires the native model only after the last lease. The installed
weapon-mesh fixture proves edited-image/old-generation retention, source hash
change, bad-image rejection and retirement. Isolated client/updater/tests builds
and native source guard pass; installed nested25/25 and smoke35/35 pass with
clean exit. Evidence: .claude/smoke-verify-runs/results-20260922T220654Z.json
and .claude/session-builds/asset0905b/_build-session.out.log. A dedicated
ordinary catalog-entry mutation case remains open. This is the model lifetime
prerequisite, not graph gameplay publication or all-family parity.
Peer T-NETWORKING-011 receiver storage component separately reported clean
client/tests builds, five focused cases and source guard; socket proof remains open.

2026-09-22 T-MODDING-002 secondary held-graph component ACCEPTED within source scope:
the v2 native action now accepts authored primary/secondary modes and ammo slots
-1..1; catalog candidates/publication retain separate function generations,
with exact function acquisition/retirement and whole-weapon retirement. This
remains source preparation/ownership only. Ordinary gameplay publication,
input/attack/copy callers, bot/net/Theater parity, full graph vocabulary and
requested mod pack remain open. Isolated incremental client/tests builds passed;
focused graph-v2 39 cases/501 assertions and native source guard passed. A fresh
parallel clean build failed from MinGW out-of-memory before source diagnostics;
its red log is retained, not counted as a project-code pass. Evidence:
.claude/asset0905-continuation/batch23-{graph-v2-focused,native-source-guard}.log
and .claude/session-builds/asset0905b/_build-session.out.log. No ordinary
gameplay, bot/net/Theater or rendered acceptance is claimed.

2026-09-08 23:06 batch22 connected texture/source pipeline ACCEPTED within scope (source35).
Full2145 24440303AB0D8D1EB47802A8BA52AA3D71F7D10FCFB818CFDCDB1135EE097BD2;
changed488 BAF7FC71; client6D4FE54E/tests233ECA5C unchanged through final audits.
Client/tests build0, native26 PASS (unchanged helper inputs), affected source-contract1
PASS, source guard0, warm installed24 PASS35/35 in39.1s using completed fresh
extraction20260909T025106Z. Actual texLoad pixels, descriptor edit, stage modes,
GBI tile fields, rejection/reset, sparse reverse index, custom menu texture and
suppressed/enabled base-overlay image/material selection all pass. Audit PASS all
3503 original texture material records,169 stage assignments,87 standalone+45 nested
scenario graphs,132 unchanged nonmaterial graph comparisons and coherent hashes.
All110 public command IDs/593 commands exact, no source/fixup failures.
Additive atomic upgrades preserve edited descriptors/images/graph topology and
versioned omissions; incomplete archive views reject rewrite. All12 occupied-count
index-bound mistakes fixed or removed across8 extraction/loading paths. Suppressed
base overlays now select public base sources through shared image/material policy.
Earlier compile/link reds, initial legacy-DMA crash, safe rejections and120s extraction
timeout remain in batch22 receipts; timeout allowance is now bounded240s. This is
native/source acceptance, not rendered appearance, full graph gameplay or all-family
parity. Full equipped model-generation ownership/graph publication, palette/mipmap
parity, larger textures and requested pack remain open. No staging/commit/push.
Evidence: .claude/asset0905-continuation/batch22-r9-{client-build,tests-build,
affected1,native-guard,texture-pipeline-installed24}-exit.json; batch22-combined26
JUnit/name audit; batch22-r9-{texture-material-audit,base-command-coverage,
post-runtime-freeze}.json; results-20260909T030500Z.json. Root queues finished;
source holds released. MP storage remains private/paused; menu native work idle.


2026-09-08 23:04 batch22 r9 source35 frozen: full2145 24440303/changed488 BAF7FC71; client6D4FE54E/tests233ECA5C builds0; affected source-contract1 PASS and source guard0. Previous native26 actual helper tests remain unchanged/accepted. Full warm installed24 live queueq_20260909030404070_ef1f010e/session98579 using completed extraction20260909T025106Z. All12 sparse catalog index bounds corrected or removed; shared public source image/material selection tests custom and suppressed base overrides. No final runtime acceptance yet.


2026-09-08 22:59 source33 settled r8: all12 occupied-count/index-bound mistakes repaired or removed across8 loading/extraction paths. Shared ordinary texture selection now chooses public base sources when overlays are suppressed; image/material policies match, cache tracks suppression mode. Installed24 fixture checks sparse reverse index before texLoad and exercises custom/menu source plus enabled/suppressed base overlay pixels/properties. Client/guard/warm24 next; native26 inputs unchanged. No full equipped-graph or rendered acceptance.


2026-09-08 22:57 batch22 r7 installed warm24 TERMINAL RED32/35: custom slot now safely rejected, not legacy DMA crash. Actual sparse catalog root cause:12 index loops used occupied count instead of pool high-water index; valid later rows skipped after removal. Corrected full class across8 sources (root additionally owns base_extended/langmanifest/romextract_pdmeta), each already checks occupied rows. Fixture enables its texture and next asserts actual reverse-index binding before texLoad. Native26 unchanged; source33 pending combined client/guard/installed24. Prior r3/r4/r5/r6/r7 reds preserved.


2026-09-08 22:49 batch22 corrected r5 client build0 (03B54A7A), guard0; full2145 30B57F69/changed48815549604 frozen. All-material audit PASS3503 texture records,169 original assignments,87 standalone+45 embedded scenarios,132 graph topology/data comparisons and hash records. Native26 remains accepted unchanged. Installed24 r5 running under q_20260909024753538_4668b684, install20260909T024805Z. Earlier r3 runtime crash and r4 missing system.h compile red retained; source30 corrected, runtime acceptance pending.


2026-09-08 22:45 batch22 r3 client/tests build0, combined native26 PASS, source guard0. Installed24 TERMINAL RED32/35 (98.9s): new ordinary custom texture case reached legacy DMA after g_NotLoadMod suppressed its public source. Exact addr2line traces texLoad:2361 from harnessTextureRuntime:839. Corrected custom-versus-base-overlay suppression and added a custom-slot boundary before any legacy directory lookup. Source30 now settled for r4 client/guard/installed24; native26 inputs unaffected, no unchanged rerun. Original red receipts retained; full texture/material acceptance pending.


2026-09-08 22:33 batch22 first client build TERMINAL RED (exit1, full98841D3E unchanged), no tests/guard/runtime run. Compiler caught legacy bool/stdio header ordering in new C++ runtime adapter and missing texWriteTileFromDefinition declaration. Corrected only adapter include boundary plus public tex.h prototype. Source29 now frozen: r2 full2145 B16F3654/changed488 D193D99A. Build queue q_20260909023309010_5df40974 running via asset0905b official wrapper. Preserve first red; same combined26/guard/installed24 remains pending after successful builds.

2026-09-08 22:30 N-0191 source28 connected texture swath SETTLED, UNVERIFIED, batch22 gates pending. Added atomic source-preserving texture and stage graph migrations (including nested scenarios), public material versioning, ordinary native property lookup, mode-scoped graph rules, level transition reset, effective texture provider selection, invalid tile rejection and complete-view archive rewrite protection. Removed 169 authored C surface assignments after moving extraction defaults into a dedicated emitter. New tests cover descriptors, archives, nested topology, hash records and mode/transaction semantics; installed24 adds actual ordinary texLoad, edited metadata, native tile command and stage reset. One combined client/tests/native26/guard/installed24 campaign; no per-change tests run. Full original pipeline/graph/pack goal remains active.

2026-09-08 22:08 N-0191: Root begins one larger connected texture batch: source-preserving extraction upgrades, versioned material properties, ordinary runtime metadata, stage-owned public overrides and propagation. User explicitly requests implementation of the connected swath before a single combined test campaign. Source changes below are UNVERIFIED until that combined batch; batch21 remains prior accepted evidence. No root build/test/runtime is live.

2026-09-08 21:58 texture generation/source-properties component ACCEPTED (batch21).
Source20 r2 83677B33; full2136 CC3BAE06/changed4731F8F2D7F/clientE23B7EEF/
tests5D62ADA3 unchanged. Client/tests build0, native texture7 PASS, source guard0,
fresh installed23 PASS33/33 in75.3s with DummyAudio. Actual catalog acquisitions
retain image+descriptor hashes, pixels/native identities and tile/surface data;
image-only and property-only edits coexist; retirement/reset/empty pool preserve
old users; actual model G_SETTIMG/G_SETTILE use selected retained generations.
Malformed image, bad/duplicate/wrong-ID properties and incomplete bundled legacy
metadata reject; final release permits reuse. Base110 command sources/593 commands
exact, no missing/source/fixup failures. Receipts batch21-* in asset0905-continuation;
install 20260909T015641Z-weapon_mesh_ingress_smoke, results-20260909T015757Z.json.
All root queues finished and source holds released. MP storage6 stays private,
paused and excluded. Base texture metadata migration, ordinary/stage override
source authority, palette/mipmap parity, larger images, complete equipped graph
and requested pack remain open. This proves native consumers, not rendered output.

2026-09-08 21:18 model source-input component ACCEPTED (batch20).
Explicit read/texture/pool inputs feed the actual model compiler, external glTF
buffers and sidecars; ordinary image loading shares the exact-byte RGBA decoder.
Public mesh.ini alone supplies skeleton/scale; loose source sidecars now resolve.
Fresh installed22 PASS31/31 in72.9s with DummyAudio: captured OBJ/INI/MTL/TGA
remain geometry2/scale2/red after disk edits; a second capture becomes7/3/blue;
actual G_SETTIMG points to each supplied image, failed resolver rejects, malformed
image returns an error. Reads4,4/textures1,1/private_reads0,0. Prior command/clip/
audio generation regressions pass, base110 command sources/593 commands exact,
zero missing/source/fixup failures. Full2125 049E558F/changed456075DB3DE/client
811BE779/tests38BED245 unchanged. Root focused4 within common12 and source guard
PASS. Three stale contract reds retained; same-case CRLF normalization fixed the
last source-read mismatch. Receipts .claude/asset0905-continuation/batch20-*;
installed20260909T011559Z, results20260909T011712Z. No complete retained model/
texture ownership, equipped graph gameplay, rendered or hardware acceptance.

Menu activation component also ACCEPTED: actual core2 PASS35/35 in79.5s, same
full/client; real new/already-enabled persistence retries, late missing-archive
failure retaining original loaded1 restart baseline, FT retained pending retry
and explicit no-rollback discard with saved JSON readback. Core1 setup red
(fopen exclusive mode) retained; explicit CRT exclusive-open correction verified.
N-0185, activation-focused12-acceptance.json and activation-core2-exit.json.
Ordinary UI/input/Agent-switch/network-transfer/process-restart and pre-install
existing-archive rescan proof remain open. Root installed queue finished; all
root holds released. MP owns next exact FT wire8/CMake repair ahead of VAD;
root native QUIESCENT while folding context and preparing private texture work.

Root next: public texture properties and stage-owned overrides (169 hardcoded
writes/90 native identities), source-preserving migration, retained native texture
identity/pixels and complete model closure/slot routing, then equipped publication/
input/attack/copy/retirement. Private drafts in .claude/asset0908-drafts/texture-generation
are UNAPPLIED/UNCOMPILED. Palette/mipmap utilization, inherited image-size limits,
all-family extraction/runtime parity and full authored graph behavior remain open.
D006A is explicitly approved; requested T-MODDING-009 pack remains required.
No full goal completion or Git mutation. Model GPT-6 / Codex.

2026-09-08 20:22 command generation component ACCEPTED (batch19). Full2124
19F5FAEF / changed451 71720393 / clientA88A88A3 / tests86BFB2D2. Client/tests
build0 (3s each), exact MP playout13/701 assertions and source guard0. Fresh
installed21 PASS29/29 in78.0s, SDL dummy audio. Actual catalogGraphNativeResolve
prepares native actions with immutable dynamic command arrays and retained
clip/audio generations. Diamond child identity, separate commands.json/glTF
buffer/WAV edits, cycle/missing/type rejection, late decode cleanup and native
consumer use after catalog retirement all pass. Motion300->600 and ordinary
sndStart/mixer620,1259->2480,3149 remain distinct; final native-owner release
frees old slots, final new-owner release frees new slots. Base110 public source
IDs/593 commands exact, zero source/fixup failures, full source/client/tests
unchanged. Receipts .claude/asset0905-continuation/batch19-*; installed run
20260909T001936Z-weapon_mesh_ingress_smoke. No equipped graph gameplay/rendered/
hardware claim. Legacy scanner command-pool capacity remains migration debt.

Root next: immutable model source closure, native model ownership/slot routing,
then complete equipped graph publication/input/attack/copy/retirement. All-family
extraction/runtime parity and full authored graph behavior remain required.
Command closure is complete for this component; do not restart its prior draft.
Next handoff .claude/asset0908-drafts/command-generation/accepted-next.md.

Peer menu Core2 actual API PASS16/16 in64.1s, full67A85250/clientC2A2A4B4 stable;
failed real component save preserved state and the same-plan retry completed.
Initial60s readiness timeout retained red, no UI/input/restart claim. Menu owns
next helper late-persistence/restart-baseline repair; root native QUIESCENT for
that exact window, next model work PRIVATE. MP playout13/701 now accepted on
batch19, no microphone/socket/device claim. No Git mutation. Model gpt-6.

User goal extension Sep8 17:58: after completing the original asset/extraction/
modding/behavior foundations, deliver T-MODDING-009: a Needler-like weapon, a
Ghost-like hover vehicle with alternating blasters, a Warthog-like vehicle with
functional driver/passenger/turret positions, and four characters inspired
by Master Chief, Sergeant Johnson, a Brute and a Grunt. Existing Needler work
T-MODDING-003 is explicitly promoted as the weapon foundation; preserve its
catalog identity and refresh final source/gameplay evidence. The new pack is
not implemented. This expands the goal; it does not replace the original scope.

> Live punch list only. The canonical Workbench is durable project truth for
> status, dependencies, ownership, notes, and evidence. Completed narratives
> belong in [session-log.md](session-log.md).
>
> The pre-consolidation task ledger is preserved verbatim at
> [_old/tasks/2026/tasks-through-2026-08-12-pre-v1-consolidation.md](_old/tasks/2026/tasks-through-2026-08-12-pre-v1-consolidation.md)
> with SHA-256
> `2DDF4541E7B2FD753F296158EB8991831BF4572834D38C00A40545868087F9DF`.

Last updated: 2026-09-08

Current18:52 checkpoint above supersedes historical checkpoints below. Root
owns T-MODDING-002 and the shared task/log lane. Complete explicit command-import
admission is verified; next is real immutable dependency/slot ownership and
coherent native input/attack/copy retirement hooks for executable graphs.
D006A remains decided; no new graph-design confirmation needed. Menu N-0138 and
MP N-0137 preserve their full fronts; no Git operation or full-goal completion.

Historical checkpoints (retained failure and intermediate evidence):

Current16:46 checkpoint: visibility consumer propagation fixed and installed
weapon14/16 outer assertions pass54.0s, with tests/client/guard green. Current
source399/078794E7 (full native2090/8B2CFF2A), client980529BE/tests0F79510B.
Root completed equipped-source preparation7/248 assertions in prior unchanged
batch10. Both units are integrated; full v2 gameplay still requires actual provider
leases, slot retirement, atomic native publication and complete gameplay/copy hooks.
Root queues are released. MP pair-r2 owns the next runtime window on this frozen
source; menu legacy enabled-list4 is draft-only awaiting its terminal release.
Root owns graph/dependency lane and shared tasks/session-log; no Git mutations.

Current16:36 checkpoint: equipped-source7/248 assertions plus tests/client/guard
pass on399/7C31C708 (full native2090/830F0AE4), client1D528826/tests13E9848E.
Public settings owns ammo/aim/visibility/commands; descriptor owns model placement.
Same-generation catalog candidate prepares equipment and actions transactionally.
Actual provider immutable command/clip/audio/model leases and gameplay/copy hooks
remain open. Empty visibility consumer propagation defect is recorded in bugs;
exact bondgun function/harness fix waits for the current runtime source hold.
Peer social9/defaults2 and catalog7 are integrated; MP27/menu11 native and guard
pass independently. Matching client includes them; MP runtime continues separately.
Root owns shared tasks/session-log and graph/weapon adapter lane. No Git mutation.

Current16:12 checkpoint: all planned batch9 gates complete. Graph kernel37,
decoder25, audio conformance18, menu persistence14, native guard and tests/client
builds pass on387/6C79C9F1, tests56796CE5/clientE36F3211. Installed weapon14/15outer
55.1s and strict MP3 scheduler10/6outer55.3s pass unchanged. Fresh8610root/9611total
corpus across27families passes. MP Create commit/assignment/Room OPEN has18/18
assertion proof from its separate task. Shared runtime/source hold released;
no root queues remain. Keep asset0905b while peer tasks/continuation need it.
Next root work: complete v2 equipped public-source/native dependency ownership
and coherent gameplay/copy lifecycle attachment; then base/all-mode parity.
PCM indexing requires edit-preserving migration before application. Packed
audio caller identity, chains, Arena metadata, character/language/UV source
gaps remain explicit. Goal is active/partial, not complete or blocked.

Current16:09: graph kernel37/472assertions, decoder25, audio conformance18,
native guard, menu persistence14 and matching client build PASS on387/6C79C9F1,
tests56796CE5/clientE36F3211. Corrected weapon14 passes15/15outer55.1s; fresh
8610root/9611total corpus across27families has zero conformance errors.
MP Create acceptance is running, then root exact asset_source_mp3_smoke10.
Graph gameplay attachment and complete extraction/runtime/migration remain open.
Menu owns draft review of checked assetcatalog.c/.h Apply seams; no source edits
until the shared runtime freeze is released. MP social transport9 remains draft.

Current15:56: configured sound reverse lookup repair passes installed6/14 outer.
Weapon14 passes source assertions but five cases fail cleanup ordering; the
harness now retires payloads with the graph intact, detaches edges before row
deletion, and reports final cleanup verdicts. Graph kernel14/37 and strict MP3
framing4 are applied; menu persistence4 plus two CMake lines are applied. Joint
source freeze/tests build next. Graph gameplay attachment remains unimplemented.
MP Create acceptance is held until corrected weapon14; no unchanged reruns.

Current15:48: audio seed repair8 remains accepted. Public-audio6 exposed packed
identity32928 beyond the4096 reverse table; full16-bit table correction and
invalid-range harness checks build/pass guard on371/9D551466/client5A39CAD6.
Installed6 repair is running; weapon14 and MP Create acceptance remain held.
MP host Create-open/Cancel has separate14/14 graphical/SDL proof on21C5/F0A104.
Graph kernel14/37 and strict MP3 framing4 remain prepared/unapplied, with menu
ModManager reader/writer draft being coordinated for the next source window.

Current15:37: audio seed identity repair PASSES installed8/15 outer, focused8,
native guard and matching client build on unchanged371/F0A104AC/client21C5C296.
MP lobby5+affected36 also pass. Runtime FIFO now gives the MP owner one host UI
capture, then root public-audio6 and weapon14. All sources remain frozen.
Prior audio8 preflight reds are resolved; full PCM indexing/migration/chains and
graph-v2 gameplay attachment remain open.

Current 15:33: source371/F0A104AC audio-seed2 plus MP lobby14 tests build passes.
Audio identity8 exact focused cases and native source guard pass unchanged;
matching installed8 is pending. MP lobby5 receives matching tests99B6BB23.
Menu changed-source diagnostic remains unvalidated after focus-access failures;
no startup retries planned. Graph kernel14/37 tests is prepared and authorized
under D-006A, held until the bounded seed verification completes.

Current 15:29 audio seed correction: diagnostic confirms candidates=0 SFX,
voice/music admitted. Base seed2 now initializes native reverse sound identity
and configured MP3 file identity before public source replacement. Applied hashes:
batch8-audio-seed-applied.json; not yet compiled/validated. Joint source freeze
awaits the MP owner's reviewed lobby14 integration. Menu changed-source diagnostic
is terminal9/22 with inaccessible focus/no injected events; menu2 only compiles.
Root next: focused audio identity cases, guard/client build and installed8 retry.

D-006 decided 15:22: Mike selected option A, executable modular graphs. Authored
events/connections/gates/state must drive reusable native modules. Graph-v2
review4 is authorized for implementation, still unapplied/unvalidated; full
native adapters and base gameplay parity remain required. This supersedes all
older mentions of D-006 as open.

Current 15:17 first-red: numeric7, relocation8 and MP3 scheduler10 pass on
unchanged360/F43883E4/client1B938609. Audio identity8 stops before its first case
(0/8,3/15 outer,64.292s); public-audio6 and weapon ingress14 remain unrun. Root
released the freeze for one-path audio identity target-admission diagnostics,
then a joint client build/single diagnostic retry. No audio identity correction
is accepted yet. Native indexing15-path candidate remains scratch-only because
generation refusal is not a complete source-preserving upgrade.

Current 15:07 checkpoint (supersedes the historical checkpoints below): asset
direct-model installed10/13 outer and numeric7/6 outer pass on unchanged
client1B938609/source360 F43883E4. Relocation8 and four audio/weapon probes remain
queued or unrun. Audio identity/chains, character selection, language and D006
remain open. MP13+49 direct cases pass on this source; live peer qualification
remains open. Menu350 cumulative, actual save11 and FR4 pass independently;
Agent150 reached the menu but lost focus. A separate focus attempt could not
access the sandbox-owned game window; the bounded desktop-account launch is
next in FIFO. Source/build freeze remains active. Root owns shared tasks/session
log updates while peers use their lane audits; no full runtime/hardware claim.

Current14:33 multiplayer: T-NETWORKING-011 first20-path candidate integrated
under the released edit window, matching reviewed hashes. Exact settled receipt:
`.claude/mp0908-repairs/source-room-safety-settled.json` (1F0EA26C). Thirteen
focused cases await one joint frozen build/gate with the asset owner's disjoint
direct-model unit. No verification claim; swarm follow-up remains draft-only.
The full repair requirements remain open in the multiplayer repair ledger.

Current14:30: direct menu350+real-save11+actual-FR4 PASS. ONE unchanged Agent
runtime RED before menu admission at90s (boot0,0of10 events,no captures,8of22),
on unchanged clientBB6F/source347EFF4FE2C. current-agent-exit.json retains proof.
Source/build/runtime released for promised MP20 integration; root menu edits
held. Settings/virtual/ordinary acceptance await a new coordinated unit/boot gate.

Current14:24 menu direct checkpoint:350 exact selected regression cases, real
Settings/config save11/11 and actual FR renderer4/4 pass. Evidence:
`.claude/menu0908/accepted350.json`. Menu source/tests frozen; asset owner has
build/runtime lane for guard/client/installed9 after metadata owner correction.
Root next: unchanged Agent Create/Cancel, then Settings and virtual-controller
journeys. Full acceptance and remaining core ModManager/input work stay open.

Current14:02 menu boundary: client A05B4141 builds,198of350 exact menu cases
retained,152 outstanding. Pointer Actions still red after hover-frame retry;
native hit-state diagnostics are authored but uncompiled. Await explicit
asset0905b incremental-build ownership handoff; no active root gate. Acceptance
map: `.claude/menu0908/accepted198-partial.json`. Save11/FR4 and runtime remain.

Latest menu gate: shared tests build passes;350 selected cases stopped after
140 passes at one stale right-stick source assertion, with209 unrun and zero
source drift. Test-only correction awaits incremental rebuild/210 remaining.
Dedicated save11/FR4 and new client/visual/controller journeys remain open.

Menu continuation: the37-path integration and seven read-only Settings readiness
conditions are applied; new native/policy cases and the disposable keyboard
fixture await the common source freeze and shared compilation. Run no duplicate
build. Asset boot and unchanged Agent Cancel remain runtime prerequisites.

Asset continuation: T-ASSETS-048 has implemented exact hand-source identity
through Bondgun hand/fists queues and reset/cache paths. All fourteen affected
C++ cases passed on source347 376CCD11/tests416F174E, with an independently
recovered source receipt after the runner's Git ownership error. The nine-case
installed probe passes9/9 with12/12 outer assertions on clientBB6F044D,
unchanged347/EFF4FE2C. It verifies selected hand/fists queue geometry, alias and
stale/missing-source handling, public scanner/walker/received edits and rollback.
The duplicate metadata scan overwriting specialized body/head sources is removed.
The direct typed-model source unit now passes4 affected source contracts and the
expanded installed10 probe (13/13 outer assertions) on client1B938609,
unchanged360/F43883E4. It covers actual catalog/preview/Bondgun custom-file and
nested-archive loading, private echo independence, and invalid-source rejection.
The ordinary menu startup fixture remains a separate gate. Audio native indexing, graph
execution, complete language extraction and character selection remain open.

## User-directed multiplayer audit, 2026-09-08

T-NETWORKING-010 delivered the [multiplayer source audit](audits/2026/multiplayer-audit-2026-09-08.md)
with 18 prioritized findings across lobby, settings, gameplay, content,
social transport and continuity. Mike authorized the full recommended repair
program under T-NETWORKING-011; see the [repair ledger](audits/2026/multiplayer-repairs-2026-09-08.md).
The Sep23 `origin/dev` checkpoints `39502184` and `0176cfda` connect
consent-before-transfer and bounded ENet content pacing. The final combined
source passed isolated client/tests builds, focused MP tests, native-source
guard, and independent-install direct Needler smoke 89/89 on binary
`3B1ECF9D...` with an unchanged 1,678-file source manifest. See the repair
ledger for the retained earlier red receipts and exact evidence. F11 large
transfer measurements, third peer, movement authority, WAN relay, migration,
voice hardware and the full acceptance matrix remain open; T-NETWORKING-011
stays `partial`. Mike requested pause after this gate. Resume only on his
instruction and recheck peer ownership/source before another build or run.

## User-directed menu accessibility work, 2026-09-06

Sep8 13:07 ET: the reviewed37-path next menu unit is now integrated, including
Settings UI/save/departure guards, remaining navigation and Results, controller
keyboard, virtual-controller fixture and two dedicated native test targets.
`.claude/menu0908/integration-verified.json` confirms cumulative candidate text.
Compilation and all current-unit checks remain pending; the prior302 is not a
pass for these sources. Asset hand-source boot failure still blocks ordinary
menu runtime. ModManager core and raw-controller VK-domain work remain separate.

T-MENUS-003 owns the complete menu functionality/accessibility goal across
controller and MKB. The first coherent batch repairs shared navigation-key
ownership, destructive confirmation focus (T-MENUS-004), modal Settings binding
capture and complete slot visibility (T-MENUS-005), and authoritative glyph
resolution plus controller scrolling (T-INPUT-005). All 235 selected affected
cases pass, including actual headless ImGui behavior and the production binding
resolver. Shared client build passes (`C3A64691`, 207-path `02A5C91A`). The first
ordinary Agent Create/Cancel gate is red before the intended route: Select at
50s preceded Agent Select readiness at 53.10s. Three captures and unchanged
source hashes are retained; the fixture must wait for actual menu readiness.
See [menu acceptance audit](audits/2026/menu-accessibility-2026-09-06.md).
The next batch is integrated: stable popup
callers, complete binding/profile persistence (T-INPUT-006), device-family and
capability glyphs (T-INPUT-008), and native widget/opening input ownership.
Mod Manager staging/Hub departure guards (T-MENUS-006) are also integrated,
with core Apply failure reporting and disabled-component persistence still open.
At13:05 ET all302 selected cases are accepted across108+68 retained and126
corrected/unrun passes. Native readiness now receives persistent application
focus from input authority, because ImGui clears its transient loss flag at
EndFrame. The final312-path46DAC86C / testsEA58CA28 gate has no drift, missing
or extra cases. Receipt: `.claude/menu0906-next/accepted302.json`. Shared client
2E4448B3 builds in37s. Revised Agent smoke is red8/22 before menu admission:
boot remains incomplete at90s with repeated BODYHEAD.PUBLIC_SOURCE nested mesh
hash failures;0/10 events fired and no captures. Asset owner diagnoses the exact
loader failure; menu sources remain frozen. Receipt `next-agent-smoke-exit.json`.
The subsequent asset313-path1A3EC5A6/client3D1EBB6A direct gate removes those
body/head warnings but stops boot on209 other catalog registrations before any
harness case. Asset owner diagnoses; the unchanged menu fixture remains pending.
At13:59 ET the subsequent SFX correction313-path9CB05A8E/client9C2057D9 admits
all1768 sounds and its dependent effect, but installed body/head retry red3/9
still reaches0/7 harness cases: CamSpy body108 encounters an existing native
model alias. Asset owner repairs that exact primary-identity policy while
retaining strict hand/private bridge ownership. Menu302 and its frozen sources
remain recorded; no menu retry until the direct asset boot gate clears.
At14:10 ET314-pathDAB3EDD8/client72FBED1D has the all-owner check and preserves
explicit public Room-preview sources. Five affected contracts and guard pass,
but installed body/head gate red3/10 at100.5s reaches0/8 cases: three native
hand aliases expose the remaining filenum-only consumer. Asset lane owns exact
public hand source propagation into bondgun and a direct geometry witness.
Menu302 is unchanged. Eight next-menu patches rehearse across37 paths without
conflicts; all remain unapplied pending the common source-freeze release.
The full menu inventory, ordinary-client outcomes, software-keyboard access,
remaining Mod Manager actions, physical device switching and visual acceptance
remain open. Existing asset and release ownership is preserved.

## User-directed asset pipeline work, 2026-09-05

Current Sep6 evidence supersedes the intermediate red narratives below: batch3
157-path `0CF630E3` and client `4F016CF4` pass 18/18 fresh scene assertions and
full 8,610-root/9,611-total conformance across all 27 families with zero errors.
Cumulative affected C++ coverage is 306 (305 plus corrected example 688 assertions).
The asset umbrellas remain partial. Root is integrating T048 complete-body
identity and explicit character policies, plus T049 real bounded Vorbis decoding.
The new source batch is not built/tested yet; graph execution, regional fonts,
complete glTF semantics, base audio replacements, and all-family source use remain.

Mike's current request selects extraction completeness, production source use,
standard mod formats, and modular behavior graphs. T-ASSETS-038 repairs the
B-1107 colmesh/runtime lifecycle; T-ASSETS-039 repairs standard glTF buffer
source and animation-tail validation; T-ASSETS-040 repairs graph catalog
references and typed parameter validation. These are a coordinated batch under
the existing asset umbrellas. D-006 records the larger graph execution-model
choice. Full graph control-flow and glTF visual-semantic preservation remain
open; narrow fixes must not be reported as all-family completion.

Current findings and verification state:
[asset source/runtime audit](audits/2026/asset-source-runtime-contract-2026-09-05.md).
The existing frozen release lanes remain retained in the canonical checkout.

Continuation ownership: T-ASSETS-041 consumes graph tunables with strict numeric
and unit validation; T-ASSETS-042 repairs language source round trips before
regional extraction/selection and font work; T-ASSETS-043 shares standard glTF
scene selection, hierarchy transforms, and instancing between rendering and
collision. T-ASSETS-043 now has 18/18 actual source-consumer smoke assertions,
with separate CPU/model/collision evidence and no GPU or human visual claim.
T-ASSETS-041/042 focused numeric and language contracts pass; regional language
selection, glyph coverage, and the larger graph execution contract remain open.
The fresh extracted-corpus conformance gate stopped at 2,700 diagnostics.
T-ASSETS-044 owns MP3/WAV format-specific validation; T-ASSETS-045 owns strict
graph JSON and export parsing. T-ASSETS-042 also owns the provenance-based
language corpus round-trip check. Root owns complete diagnostic JSON reporting.
The graph, regional-language, and glTF material designs linked from the audit
define the remaining production work without narrowing the user's full request.

Latest corpus boundary: seven audio cases, 16 verifier cases, selftest, and
actual 68/68 English-table parity pass. The corrected full corpus retains 92
diagnostics across 8,610 root / 9,611 total archives. T-ASSETS-047 corrects the
18 bot-profile manifests and 37 explicit unbound-head source contracts;
T-ASSETS-048 owns those character templates' separate missing runtime selection
semantics. T-ASSETS-045 also closes oversized strings at final native capture
boundaries before its first build. Decoder wiring already exists for private
MP3 sound slots; base MP3 scheduling, reverse replacement mappings, and advertised
Vorbis decoding remain separate concrete follow-up gaps.

T-ASSETS-045 test build passes and affected C++ coverage is 305/306; all 11 new
strict-parser/capture cases pass. The lone terminal red exposed string-only
contexts silently ignored in two shipped weapon generators. Both producers and
only the matching context member of each weapon archive are corrected; the exact
example/context-retention gate is pending on source `0CF630E3...`. The earlier
guard failure was corrected without introducing private voice metadata copies.
Review-only drafts for T-ASSETS-049 Vorbis/mixer, T-ASSETS-048 integrated identity,
and D-006 executable graphs are isolated under `.claude/asset0905-drafts/` and are
not yet production changes. Full fallback removal remains T-RUNTIME-001/c3844.

---

## Canonical 1.0 session route

D-004 defines 1.0 as a complete, stable base game plus usable Theater for
Campaign and Combat Simulator, with one minimal graph-backed sample weapon in
custom Match Setup slots. Forge maps/map variants, broad creator and modding
pipeline tooling, Studio, Needler-specific polish, Queue Match, GPU swarm
extensions, and authority migration are post-1.0.

The canonical trigger is:

> goal: Let's get to 1.0. While working, state which milestone you are working
> on, it's current progress, and ensure you are working to build things
> structurally / infrastructurally instead of hacking things together. We are
> seeking long term extensibility and seamless function, rather than just
> 'technically it works'.

When a session receives that prompt:

1. Complete the repository, context-manager, Workbench, and coordination
   preflight before editing.
2. Read D-004 and T-RELEASE-001, then recompute dependency progress for
   T-RELEASE-002 through T-RELEASE-006 from the canonical Workbench.
3. Select the lowest-numbered incomplete milestone unless it is blocked or Mike
   explicitly changes priority. Claim one unowned dependency that materially
   advances that milestone.
4. Begin every progress message with one plain statement containing the
   milestone, current progress, what is being done, the task goal, and why it
   matters.
5. Implement at shared architectural boundaries with explicit ownership,
   versioning, lifecycle, rollback, and reusable production consumers. A local
   patch that only makes one smoke pass does not satisfy the milestone.
6. Keep statuses partial until their exact completion contracts pass. Reject
   overlapped or source-drifted receipts and record durable evidence as truth
   changes.
7. Never route this prompt to `post-1.0` or `historical-cut` items without Mike's
   explicit scope change.

The exact user instruction is also stored as incorporated Workbench note
N-0036 on T-RELEASE-001.

---

## Milestone progress

Progress below was recomputed from the canonical Workbench on 2026-08-27.
Recompute it at the start of each session rather than repeating stale numbers.

| Order | Workbench item | Status | Current dependency progress | Completion |
|-------|----------------|--------|-----------------------------|------------|
| 1 | T-RELEASE-002 | partial | V-010 has 3 validated, 9 partial, and 4 missing leaf gates out of 16 | V-010 validated/pass |
| 2 | T-RELEASE-003 | partial | 0 of 2 closed; T-THEATER-001 partial, V-011 missing | Theater implementation plus V-011 validated/pass |
| 3 | T-RELEASE-004 | missing | 0 of 2 closed | T-MODDING-008 and V-012 complete |
| 4 | T-RELEASE-005 | partial | 2 closed, 4 partial, and 3 missing out of 9 | Friend-play/NAT/co-op/performance dependencies closed |
| 5 | T-RELEASE-006 | missing | V-013 not run | One source-frozen V-013 release candidate validated/pass |

T-RELEASE-001 closes only through D-004 and T-RELEASE-006. It is not a parallel
sixth work queue.

---

## Milestone 1: complete base game and graphs

B-1107's atomic catalog lifecycle correction is implemented in T-ASSETS-038.
The 2026-09-05 ordinary client passed all 19 Scenario transaction cases and
started the match on unchanged source fingerprint `85609312...`. The complete
replacement smoke now passes 32/32 after T-TESTS-003 corrected the supported
top-level field to `timeout_seconds=125` in both V-006 fixtures. The explicit
continuation used unchanged client `2097E1C8...`; source manifest `44186EA3...`
stayed unchanged across the run. Receipt: `.claude/asset0905-continuation/v006-result.json`.
The original rejected receipt remains retained. Strict Scenario readiness is
unchanged; remaining asset and graph fronts are in the audit and open D-006.

Parallel production gate: V-010/B-1086 with B-1106 as its production blocker.
The generic render-stream predecessor is automation- and extraction-accepted on
exact client `36EA05D1...`: schema v2 preserves ordered geometry state and per-load
colour ownership; recursion tracks strict 64-slot ownership; the shared
parameter-byte `G_VTX` decoder, full star domain, Type-3 pair continuity, and
room-collision destination slots all use the same command semantics; legacy v1
remains exact; and complete cache v23/v26 payloads are required. Focused 97/7,
complete 67,361/1,230, conformance, recursive examples/workflow,
native-source guard, fixture parsing, and unchanged 10-product/20-verifier
source manifests all pass. Ordinary-client receipt
`results-20260826T193629Z.json` then cleanly wrote 733/733 meshes and exited,
accepting the former `vertex_load` and `triangle_slot` repair.

That receipt is retained/rejected at 20/26 for visual closure. Confirmed
B-1106 makes `base:sp_body_118` select its nested `base:model_cchicrob`
external-body cache and then fail modeldef conversion; closure row 0 rolls back
64 prior loads, the Main Menu manifest remains unchanged, neither target door
activates, and all six captures stay outside Carrington. Exact tracing found 33
failing corners across 11 faces in three Type-3 groups: the paired lists share
vertex slots but reset geometry knowledge, which schema v2 could not represent.
Current source adds schema-v3 exact vertex-cache provenance: every source
`G_VTX` becomes an ordered `vertex_load`, every triangle names three bounded
`vertex_cache_slots`, Type-3 pairs retain the 64-slot table through one
`vertex_scope`, and non-Type-3 pairs clear it through one
`vertex_cache_reset`. C/Python validators simulate the same table, reject
unloaded or forged snapshots and malformed arrays, bound all matrix carriers
to 32766 before s16 publication, and require every relocated load to have a
restorable triangle-time known-state domain. The Type-3 baseline supplies only
inherited lighting/texture-generation state; caller-owned fog remains
fail-closed. Schema v2 remains strict, while schema v3 permits load/draw
divergence only when an explicit ordered load proves it. Export/cache v25/v28,
modeldef cache v12, validators, and focused conformance contracts are connected.
Exact frozen client `7EF396C9...` / tests `4D14886D...` pass focused 143/8,
complete 67,451/1,231, all conformance/workflow/source/native guards, and zero
source overlap. Do not bypass the remaining production gate in the fixture.
Run clean 733/733 extraction and true 3440-wide Carrington proof, then retain
representative Campaign/Combat Simulator propagation with the V-009
no-obstruction gate.

Most recently completed unit: V-010/B-1105/SP-74. One generic ordered-material
planner now preserves the complete `model.mtl` domain, one request-owned latch
prevents permanent gun-load retry, and one typed source planner/binder serves
top-level and nested `.pdmesh` ingress with canonical alias order, identity,
rollback, and legacy-pass preservation. Luna xhigh's propagation audit covered
733 archives and found the Mauler plus `base_model_cheaddarkaqua` as the only
true material-order drifts; no Mauler-specific source or render special case
was added.

Exact client `395D48A6...` passes the final isolated build, focused 132/14,
full 67,152/1,223, native-source guard, two-archive verification, fixture
validation, `git diff --check`, and 23/23 unchanged production-source hashes.
Strict fresh-cache ordinary receipt
`.claude/smoke-verify-runs/results-20260826T152922Z.json` passes 54/54 in 33.8
seconds with stable gameplay, one nested 13-material compile/activation/load/
render path, 11 real Mauler shots, no runtime native/ROM route or endscreen, and
clean exit. Direct review of all four gameplay captures passes V-009 placement,
silver/green color, and no-obstruction gates. B-1105 and SP-74 are regression
gates; superseded clients and rejected receipts remain diagnostic history only.

Current completed unit: T-ENGINE-004/B-1076. `mpStartMatch` is now the single
Combat Simulator stage-request owner and releases the complete menu/input pool
before stage publication for offline, listen-authority, and receiving-client
starts. Exact client `04DB220E...` passes the ordinary title -> Agent Select ->
Main Menu/Play graph -> Room -> two gameplay/endscreen cycles with real Play
Again 55/55, balanced Agent Select/Room ownership, no watchdog repair, and clean
exit. The same binary preserves the invitee-authority D-003 route at 170/170:
one listen host, one signed typed match-server route, one join, and no
probe/relay handoff. The first ordinary receipt remains rejected at 51/55 only
for its stale `pass` versus emitted `satisfied` verifier strings; both product
cycles completed. B-1076 is now a regression gate.

The preceding completed unit was T-ENGINE-004/B-1067/B-1103/B-1104. The prepared-roster,
ready-gate settings rollback, multi-player stage unwind, and model-complete NPC
wire boundaries now share the protocol-v58 inactive/waiting/release/active
lifecycle. START/READY carry one nonzero epoch, the listen authority and exact
remote peers own separate post-load latches, RELEASE queues one complete fresh
baseline from dedicated reliable storage, and ACTIVE alone admits incrementals.
The exact full NPC resync carries its immediate canonical digest; periodic
mutable-state checksums are gone. Current source-frozen automation is accepted:
all isolated targets build, exact product `03a65922...` (2,719 files) and
verifier `f5f9ba01...` (408 files) remain frozen, final full tests pass
66,421/1,200, and the native-source guard passes. Exact client is `5DA75BE6...`
and the automation-build tests were `9E56A336...`. Current-v58 production
receipts now accept co-op 96/96, Counter-Op 98/98, later-player rollback 60/60,
settings rollback 43/43, initiator authority 214/214, reconnect 99/99, and the
focus-independent invitee-authority route 170/170. The route proof uses the
same exact v58 client and establishes one elected in-client listen authority,
one separately signed typed match-server route, one idempotent initiator join,
and no probe or relay descriptor handoff. Its immutable raw receipt remains
rejected at 169/170 because the original verifier rejected valid single-digit
epoch 1; the corrected definition and separately hashed retained logs pass all
170 assertions, and the compiled route/reconnect contract passes 108 assertions
in 2 cases. Final verifier aggregate `726a2c96...` covers 409 files and exact
tests binary `38C37DC4...`; its only pre-route delta is the corrected route
fixture and static contract, while product `03a65922...` has zero drift. The
reconnect verifier now selects typed `base:cyclone` and expects its actual
weapon 11 instead of coupling a random spawn to stale weapon 5; its focused
compiled contract passes 63 assertions. The integrated invitee-authority run
reaches 210/218 and proves the route/start/gameplay path, but is rejected because
this execution desktop exposes no foreground HWND for B-1085's fresh SDL focus
witness; that independent focus/visual fixture remains unchanged and is no
longer the D-003 route gate. B-1104 is now a production regression gate, while
T-ENGINE-004 is validated after its finite section 9.10 closure audit.
Milestone 1 therefore has 3 validated, 9 partial, and 4 missing leaves out of
16: 19 percent production-validated. T-TOOLING-003 moved from missing to
source-accepted/partial after its corrected 11-test evidence-index unit, but no
gameplay fixture or coverage cell has run. T-ENGINE-005 owns the parallel B-249,
B-242, B-174, and B-183 Combat Simulator defect cohort without displacing the
V-010 Carrington production owner. B-1105/SP-74 is now a production regression gate;
the active focus is B-1086's source-connected render-state repair and production
proof under V-010.
The active Milestone 1 source wave bumps the wire to v59 for typed, ordered
server-authoritative hoverbike mount/dismount state. The accepted v58 receipts
above remain historical regression evidence, not current-v59 product proof;
focused protocol, vehicle, initiator-authority, and invitee-authority verification
is pending the consolidated source freeze. No further D-003 source work is
pending, but its two ordinary-client role smokes are part of that v59 verification.
B-1101/B-1102 remain regression gates on accepted frozen
automation and Combat Simulator evidence, and accepted Campaign, D-003, and
reconnect receipts remain retained without rerun.
T-TESTS-002 remains validated
after D-005 option A, B-1061/B-1062/B-1063, and the 17-mission restart gate.

Decided Workbench item D-005 owns the architecture: one Agent Profile v3 JSON is
the sole per-agent source for campaign state and preferences. The global
`pd.ini` remains the machine-default source before sign-in. Legacy v2 Agent JSON
and its optional `prefs_<agent>.ini` overlay enter one validated candidate and
atomic current-document replacement; current documents never consult a
sidecar. Create, copy, activate, save, and delete share this lifecycle, and an
active Agent cannot be deleted without a future explicit sign-out transaction.

Asset source invariant for this milestone: Public typed archives are the game-facing source.
Engine-ready products are source-hashed cache only.
Runtime ROM/RomProvider fallback after extraction is an asset-chain failure tracked by c3844 and T-RUNTIME-001.

| Item | Baseline | Required outcome |
|------|----------|------------------|
| A-ASSETS-001 | partial | Current-tree audit of all 27 family rows and every retained field, backed by T-ASSETS-001 cross-family work. |
| T-EXTRACTION-001 | partial | Lossless editable source for every production semantic, with versioned fail-closed emitters. |
| T-RUNTIME-001 | partial | Every accepted public source and graph drives production with no hidden native, loose, or ROM fallback. |
| T-MODDING-002 | partial | Every accepted base weapon/projectile/entity graph deliberately selects its production behavior with parity proof. |
| T-MODINFRASTRUCTURE-003 | missing | Remove AllInOne content from base authority and retain it only as optional typed mods. |
| T-ENGINE-004 | validated | Protocol-v58 stage, roster, rollback, friend-play, reconnect, Combat Simulator, Campaign, and player-allocation lifecycles are production-verified. The durable closure matrix is `context/evidence/2026-08-26-t-engine-004-closure.md`; accepted receipts and exact binaries remain regression evidence without redundant reruns. B-1105 is separately routed to V-010 and now passes its final `395D48A6...` nested-source Mauler gate at 54/54. | Retain the finite section 9.10 matrix and rerun only when a covered production boundary changes. |
| T-TESTS-002 | validated | D-005 option A is production-proven: exact v2 plus optional INI migration to v3, complete-schema rejection, activation rollback, active-delete rejection, ordinary Agent Select, fail-closed invalid CLI, all 17 missions through live Credits, and second-client v3 persistence. Final full automation passes 57,277 assertions in 1,070 cases. |
| T-VEHICLES-002 | partial | Typed input, physics, lifecycle, protocol-v59 authority, and readiness-relative smoke source are in the current source-freeze wave. Focused build/tests plus offline and ordinary-client runtime proof remain. |
| T-TOOLING-003 | missing | Durable indexed release evidence instead of transient build-folder claims. |
| V-003 | partial | All-family catalog/provider production use. |
| V-004 | partial | Real MKB/controller navigation, rebinding, glyph switching, and gameplay input. |
| V-005 | missing | One controlled public-source edit changes production behavior for each of 27 families. |
| V-006 | partial | B-1107 lifecycle repair passes the 19-case Scenario matrix, match start, and clean scripted exit: corrected fixture passes 32/32 on unchanged client2097E1C8 and source44186EA3. Retain both the original timeout and accepted continuation receipts; this gate does not independently revalidate every extraction/save failure contract. |
| V-008 | missing | Custom game-mode and bot-profile identity through save and listen-host networking. |
| V-009 | validated/pass | Preserve edited effect/weapon gameplay, distribution, lifecycle, and visual proof. |

Current T-ENGINE-004 checkpoint: B-1067/B-1103/B-1104 production gates pass at
protocol v58. The prepared roster aborts before a preparing
participant's valid settings publication, and a later co-op/Counter-Op player
failure reverse-unwinds every committed runtime owner in authenticated
client-ID order. The rejected v57-era runtime batch drove an explicit
inactive/waiting/release/active stage-publication lifecycle: START/READY share a
nonzero epoch, the listen authority and exact remote peers own separate
post-load latches, release publishes only a fresh baseline, and ordinary
gameplay begins on the next ACTIVE frame. NPC convergence is one reliable full
resync followed immediately by a canonical sync-ID-sorted digest of that exact
applied snapshot; periodic asynchronous checksum traffic is removed. The
baseline has dedicated packet storage, consumes its complete pending mask only
after a successful room queue, and cannot release with a missing or partially
built transaction. The digest uses the exact serialized target and
sentinel-terminated room fields. Fixtures assert semantic endpoint roles with
seed-independent pools and baseline-before-ACTIVE ordering. Exact v58 product
`03a65922...` and verifier `f5f9ba01...` pass isolated builds, complete tests
66,421/1,200, and the native-source guard with unchanged post-run manifests.
Accepted ordinary-client receipts cover co-op 96/96, Counter-Op 98/98,
later-player rollback 60/60, settings rollback 43/43, initiator authority
214/214, reconnect 99/99, and the focus-independent invitee-authority route
170/170. The immutable raw route receipt remains rejected at 169/170 for its
stale two-digit epoch regex; corrected static coverage passes 108/2 and the
same retained exact-client logs pass 170/170 without product changes. B-1104 is
a regression gate. The completed closure review promotes T-ENGINE-004 to
validated without rerunning these accepted receipts.

B-1076 is also a regression gate. `mpStartMatch` owns one pre-publication
`menuStop` plus `RELEASE_MENU_POOL` boundary for offline, listen-authority, and
receiving-client Combat Simulator starts. Exact client `04DB220E...` passes the
ordinary Agent Select/Main Menu/Play/Room/two-match/Play Again fixture 55/55 and
the invitee-authority route fixture 170/170. The first ordinary 51/55 receipt is
retained and rejected only for stale verifier wording; both product cycles ran.
Milestone totals are now 3 validated, 9 partial, and 4 missing out of 16 after
the corrected T-TOOLING-003 evidence index became source-accepted/partial; it
does not move V-010 because no gameplay fixture or cell has run. T-ENGINE-005
remains the owned Combat Simulator defect cohort. Durable
T-ENGINE-004 matrix: [2026-08-26-t-engine-004-closure.md](evidence/2026-08-26-t-engine-004-closure.md).

V-010 also owns the consolidated regression matrix from [bugs.md](bugs.md),
including campaign and Combat Simulator lifecycle, bot spawn and attribution,
collision, physical input, vehicles, character geometry, long-session stress,
and rendering. Its active defects and retained regression gates include B-919, B-249, B-242, B-174,
B-183, B-1064, B-1065, B-1066, B-1067, B-1068, B-1069, B-1070, B-1071,
B-1072, B-1073, B-1074, B-1075, B-1076, B-1077, B-1078, B-1079, B-1080,
B-1081, B-1082, B-1083, B-1084, B-1085, B-1086, B-1087, B-1088, B-1089, B-1090, B-1091, B-1092, B-1093, B-1094, B-1095, B-1096, B-1097, B-1098, B-1099, B-1100, B-1101, B-1102, B-1103, B-1104, B-1105, B-1106, and B-1107. B-1061, B-1062, and B-1063 are locked release
regression gates.

---

## Milestone 2: usable Theater

- **T-THEATER-001, partial:** Replace the participant-only `.pdth` path with a
  versioned, bounded, fail-closed authoritative recording format and lifecycle.
  Record enough match identity, configuration, state, events, and checkpoints
  to reconstruct Campaign and Combat Simulator sessions after process restart.
- **V-011, missing:** Record, stop, restart, list, load, play, pause, seek, change
  camera, stop, and return cleanly for one Campaign session and one Combat
  Simulator session. Cover complete, interrupted, corrupt, truncated,
  oversized, and version-mismatched files. Include listen-host capture when its
  authoritative path differs.

Theater must be watchable replay, not only ghost participant transforms.

---

## Milestone 3: sample graph-backed weapon

- **T-MODDING-008, missing:** Ship one small public-source base-gun variant,
  enabled by default, with a three-round burst or alternate projectile.
- **V-012, missing:** Prove discovery by permanent catalog ID, every supported
  custom weapon slot, setup save/reload, local gameplay, two-client
  listen-host parity, malformed-source rejection, and clean shutdown.

Needler is not the release sample. Every gameplay capture must still preserve
B-1054/B-1055/B-1056: no huge white first-person obstruction, correct held
placement/scale/material colour, and readable effect colour.

---

## Milestone 4: friend play and performance

D-003 and T-NETWORKING-004 are closed. The social/group topology remains
peer-to-peer, but each match has exactly one elected in-client listen authority.
Presence v5 carries a separately typed, signed, fresh match-server route. Every
non-authority performs one idempotent join. LAN/STUN/UPnP/ICE probes and relay
descriptors are auxiliary and must never reach `netStartClient` as routes.

Remaining 1.0 work:

- T-NETWORKING-001: signed peer ICE-candidate exchange and real-NAT proof.
- T-NETWORKING-002: distinct signed STUN reflexive candidate transport.
- T-NETWORKING-003: correct UPnP mapping for required probe transports.
- T-NETWORKING-005: configurable, authenticated public TURN fallback.
- T-NETWORKING-006: one tier/candidate owner after candidate semantics are
  production-correct, without reintroducing probe-to-client handoff.
- T-NETWORKING-007: co-op continuity, drop-out/reconnect, and Combat Simulator
  start/return behavior.
- P-001: explicit cold, warm, validation, rebuild, activation, creator, stage,
  and match timing budgets with raw artifacts.

Host authority migration after authority departure remains post-1.0 under
T-NETWORKING-008 unless Mike promotes it.

---

## Milestone 5: source-frozen release candidate

V-013 is the only closure gate. From one frozen revision it must prove:

- clean install, upgrade, failed-update rollback, and save migration;
- full automated suites, native-source guard, and archive conformance;
- complete campaign and representative Combat Simulator/base-game paths;
- real friend play across the required NAT matrix;
- Campaign and Combat Simulator Theater record/restart/load/playback;
- sample-weapon custom slots and local/network gameplay;
- clean disconnect and shutdown with no crash, fatal, or forbidden fallback;
- final visual captures retaining the B-1054/B-1055/B-1056 regression gate.

No receipt counts if relevant source changed during the run or an exclusive
resource receipt overlapped.

---

## Explicit post-1.0 work

- T-MODDING-005: Forge maps and map variants.
- T-MODDING-001, T-MODINFRASTRUCTURE-001/002, V-007: broad packer, Studio,
  creator, editor, and pipeline tooling.
- T-MODDING-003: Needler-specific feature polish.
- T-INPUT-002 and deferred D-001: Queue Match.
- T-BENCHMARKING-006: GPU swarm benchmark and scalability program.
- T-NETWORKING-008: authority migration and richer friend-play continuity.
- 2.0 renderer (decisions D1 to D7 under D-004; no Workbench item yet, so
  commits use `Refs: D-004`): plan in
  `context/designs/rendering/renderer-2.0-scope.md`. Phase 2a step 1 (classic
  on bgfx's OpenGL backend, `--renderer bgfx`, parity mode) is on branch
  `week-pd/bgfx-classic`, not merged; state and next steps in
  `context/designs/rendering/bgfx-classic-gl.md`.

Items marked `historical-cut` retain their permanent IDs and evidence but are
not live routing targets. They were consolidated into the milestone gates above
with Mike's authority on 2026-08-12.

---

## Verification discipline

- Use isolated queued builds and the required coordination FIFO for tests,
  builds, game runs, smokes, captures, and deployments.
- Record the relevant source fingerprint before a long receipt and compare it
  afterward. Reject drifted or overlapped evidence.
- Static tests prove contracts, not gameplay. Ordinary-client or real-peer
  evidence is required where the completion contract names runtime behavior.
- Update Workbench status/evidence and this concise task summary as truth changes.
- Update the relevant pillar, constraints, bugs/systemic-bugs, session log, and
  `UNRELEASED.md` in the same source-frozen unit when implementation changes.

Asset continuation September 6, 09:45 ET: T048/049 focused C++ passed cumulatively65cases (33 retained +32 corrected/unrun; 8759 assertions), six character-source Python cases, native source guard and conformance selftest passed. Common product/test205path freeze BD2F74FB and tests38C97FB3. Installed-client Vorbis typed-archive/playlist and all63 character-template activation gates await shared menu confirmation correction and client build. Public body/head scalar metadata authority, complete selector/net identity, base MP3/reverse mappings, standard material/animation and graph execution remain open. Broader asset request remains partial; all new graph/audio/body integration copies are draft-only. No device/visual/network/release proof claimed.

### Asset batch4 installed acceptance — 2026-09-06 11:37 ET

Common207-path source02A5C91A9A97E7EC84C7102DF1901432C683B2B3EA5434B1F6466236C0FBD863 remains frozen. Client C3A64691C58EB63C935BBD43DF93299E3674A8C3B990380B41328085626E73C9 builds after vendor-only PCH exclusion; tests DD6141B8 and updater D4F51449 retain passing receipts. Asset C++ cumulative65 distinct cases are exact-name audited (33 retained plus corrected/unrun32); menu owner reports cumulative235 distinct cases. Fresh audio install passes5/5 with4410 stereo frames through public song/catalog/nested ZIP/actual synchronized playlist. Fresh character install passes6/6: all63 extracted typed templates activate,26fixed35random_gender2integrated. Both receipts prove unchanged source and scripted clean exit. See .claude/asset0905-continuation/batch4-audio-smoke-result.json and batch4-character-smoke-result.json plus matching exit/source files. No visual, audible-device, multiplayer or release acceptance. T048/T049 remain partial: public body/head scalar authority, selector/network identity, remaining standard audio paths and behavior graph execution are still open. Menu owner now owns the next bounded installed check on the same client; asset workers prepare unapplied body/head, MP3/reverse-map/conformance and graph closure drafts. No Git operations or completion claim.

### Asset batch5 source integration opened — 2026-09-06 11:45 ET

Menu installed fixture stops8/13 because Return50s precedes agent-select readiness53.10s; source207 remains unchanged, no menu runtime acceptance. Both roots explicitly release the common freeze. Root applies reviewed T046 base-MP3 stereo PCM scheduler/mixer, bounded shared WAV/MP3 loader and actual theme one-shot helper (standard WAV/MP3/OGG) with canonical audio.h declaration; no vendor edits. Root applies T-ASSETS-050 canonical integer-token admission and finite glTF room guard, a real bot-source regression and seven-case installed importer harness. Added sources are unbuilt/unrun. MP3 fixtures require coordinated encoding. CMake/main/harness additions belong to root until reported stable; menu root owns independent next menu/input changes. Body/head public authority, audio reverse-map/public walkers/conformance and obsolete weapon mesh ingress cleanup remain reviewed drafts under active worker ownership. New gates require an explicit joint source freeze.

Batch5 final integration September 6 12:25 ET: catalog core/cache and scanner snapshot fixes are integrated. Registration copies bounded IDs before mutation, reserves hash capacity before row publication, and only publishes initialized rows; exact runtime metadata self markers are rebased during checked pool relocation. Reverse caches store pool indexes and validate reacquired rows. Snapshot comparison normalizes self markers, while preserved metadata rollback rebases them without reviving retired payloads. Five helper regressions and eight real threshold/relocation cases remain unrun. Weapon meshes now share direct/deep public-descriptor preparation, exact selected held source, native identity preservation and model slot rollback; obsolete eight-mesh mutation removed. Portable installed gate includes13mod cases plus fresh Falcon2 identity preservation. Root moved native audio and weapon model probes into mainProc after mainInit/sndInit, so sound/memory pools and boot-failure handling precede proof. Audio timing probe now ticks while active/paused and consumes repeat PCM fully. Remaining static assertions are being aligned; common freeze and all batch5 validation remain pending. No graph-v2 implementation or D006 resolution is claimed.

Batch5 shared runner handoff — 2026-09-06 12:48 ET: two corrected audio source assertions PASS, exact2/2 JUnit coverage with no missing/unmatched/failing names. Source311/23ACF24129D8B278E7788875F1764608E06D92BF67DD900B96DDDE49308F5731 unchanged, tests02294A4B5571652A4FCBBDC57D39B19F1FAAB02CC6F84C284F73B2BA38472EDF. Asset cumulative57passes (46retained +9continuation +2corrected), not a fresh57case run. Remaining17 names saved; menu owner now receives frozen binary and test-runner for its cohort before the asset continuation. Current production/CMake/tests remain held. Full source guard refresh after body parser correction, client build and all batch5 installed probes remain pending; no runtime acceptance or task completion claim.

Asset batch5 checkpoint — 2026-09-06 13:03 ET: all74 selected C++ cases accepted cumulatively with exact JUnit coverage (46+9+2+17), including the corrected native CASS/MAIAN mapping and actual standard audio decoding/mixer helpers. Current source312/825AFB40, tests3CEF96FC; source remained unchanged through the last gate. Python audio15 and earlier source contracts retain passes. Shared menu owner is applying only its diagnosed persistent-focus readiness correction before the next common freeze/build. Native source guard refresh, client build and seven installed asset probes remain pending. Workbench T046/T048/T050 details now reflect current source/helper evidence and explicit runtime limits. Character selection, strict MP3 framing, language lifetime and D006 graph drafts are unapplied. Full asset/graph task remains partial.

Asset batch5 SFX correction boundary — 2026-09-06 13:45 ET: current C++ acceptance is76 distinct cases (64 unaffected plus12 direct; historical74 over-retention withdrawn). Source313/1A3EC5A6 and client3D1EBB6A direct body/head install registers68 bodies,84 heads and63 characters but aborts209 failures before0/7 harness cases:208 SFX packed control bytes incorrectly treated as MIDI ranges, plus dependent explosion profile. Exact six-path public parser/conformance/proof correction is now frozen313/9CB05A8EFF71196219505A1E085E40C7851E20EE4924F3BDFCD16BF1B347FD3C; installed proof original-rate pin is44100, decoded output22050. Tests build active; six affected C++ cases,16 Python contracts, guard/selftest, client and direct boot retry pending. Native source audit independently confirms file-route start delays, linked clips and restart groups remain unimplemented; a semantic catalog-ID public source/runtime draft is authorized, not applied. Menu302 remains independently accepted and source-frozen; unchanged Agent retry follows direct asset boot success. Character selection, language, UV, eligibility and strict MPEG next drafts remain unapplied. D-006 remains open, graph-v2 draft unapplied. No Git, audible, visual, hardware, multiplayer, release or overall completion claim.

Batch5 native mesh alias correction — Sep6 14:04 ET: source314 DAB3EDD889FE7BB58E2DF3D111D407A77863F4EA34659EE91ED57878C5DEABFB freezes seven exact paths. Body/head primary can retain an established native model mapping despite legitimate aliases because it loads selected catalog/provider bytes. Hand and private bridges scan every occupied model row for conflicting ownership; no first-row/order blind spot. Shared modeldefLoadFromHandle preserves an explicit FileProvider rather than replacing it by a filenum sibling, covering ordinary character preview. Eight-case installed fixture adds real CamSpy source ownership, selected synthetic geometry through both body and preview loaders, missing selected-source rejection and both-order hand/private conflicts. No installed verdict yet. First static gate found one stale old audio rollback source pin, corrected to actual transactional registrar; direct5 now pass exact names, source unchanged, cumulative80=77 prior plus3 additional distinct cases (two affected prior cases refreshed). Guards/client/direct installed retry next. Menu source freeze unchanged; no next draft integration or Git.
