# Theme runtime validation — 2026-08-08

Workbench item: `T-ASSETS-030` (partial)

Source-frozen automated evidence:

- client and `pd-tests` compiled successfully;
- focused T030 coverage passed 218 assertions in 6 cases;
- `.pdtheme` coverage passed 321 assertions in 14 cases;
- the retained full receipt passed 47,986 assertions in 890 cases;
- strict archive conformance passed 28 roots, 52 recursive archives, and all 27 families;
- the native-public-source guard passed.

The final ordinary-client smoke passed 24/24 assertions and exited normally. It loaded the packed typed-example `.pdmod`, registered the nested UI/font/SFX/music dependencies, applied `example:tri_theme`, compiled the nested `sequence.json` without a legacy fallback, processed MKB Down/Up through SDL, rendered two 1280x720 frames, and shut down cleanly. B-998 reduced activation to exactly two intentional applies.

This is not a validated verdict. `T-CATALOG-002` remains a critical lifecycle blocker because stage cleanup released an active feature-owned theme closure. `T-MENUS-002` remains open because the MKB/controller action footer is visibly clipped in both frames. Real controller/device switching, restart persistence, and real peer distribution are also unproven.

Durable artifacts are stored in `context/evidence/2026-08-08-theme-runtime-validation/`.
