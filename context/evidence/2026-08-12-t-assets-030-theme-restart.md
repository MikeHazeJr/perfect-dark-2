# T-ASSETS-030 preserved-install theme restart

Date: 2026-08-12

The ordinary-client baseline installed and enabled the typed example package,
selected `example:tri_theme`, applied its nested UI/font/SFX/music closure, and
exited cleanly. The restart scenario then launched a second ordinary client
against the same installation with no fixture copy, pack, removal, or
replacement step.

The second process passed 31/31 assertions and exited `0`. It rediscovered the
same theme and four nested dependencies, restored `ActiveTheme=example:tri_theme`,
loaded the vector font, semantic click SFX, and menu sequence, applied the live
theme, processed real SDL Down/Up events, and released all five catalog owners
from ref 1 to 0 at shutdown.

Preservation checks after the second process:

- installed package SHA-256 remained
  `3C9E640BFC9CEA1C4BA04141AAEFB435687AA84A13C9023A08DCEB99BC7CE62E`,
  with its original `2026-08-12T12:16:12.5388943Z` timestamp and 118,368-byte
  length;
- `mods-enabled.json` SHA-256 remained
  `A3322B5C38B0CF6DAF7EC31262A10F409DC522E5D6AD86D4ADA0FFBEC1F4B8FA`,
  with its original timestamp and 34-byte length;
- the normally rewritten `pd.ini` still contained exact
  `ActiveTheme=example:tri_theme` and `SeenMods=example:tri_theme` values.

Durable result: `context/evidence/2026-08-12-t-assets-030-theme-restart-result.json`.
The complete current-install log remains under
`.claude/smoke-verify-install/logs/game client/pd-client.log`.

This closes the preserved-install restart gate only. T-ASSETS-030 remains
partial for real-peer package distribution/admission, physical-controller
navigation, and live keyboard/controller device-switch glyph proof.
