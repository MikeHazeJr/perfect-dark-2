# Base Weapon Parameter Matrix

Status: coverage matrix for Kanban `c3814-s2`, generated from the current local base weapon snapshot and cross-checked against runtime behavior notes.

Important: the source snapshot is still named `.pdwpn` locally because the emitter has not been cut over yet. `.pdwpn` is fully deprecated, was never released, and must be removed rather than supported. This document does not approve `.pdwpn` as an input format. The target archive remains `.pdweapon` only.

The table records current function parameters so the graph conversion can preserve base behavior while moving authored data into `.pdweapon`, `.pdprojectile`, and `.pdentity` assets. It is not the final schema naming pass; `c3814-s8` owns the final named-module parameter cleanup.

## Matrix

| Weapon | Primary function parameters | Secondary function parameters | Physical/module notes |
| --- | --- | --- | --- |
| `base:ar34` | auto dmg 1.4, rpm 750/750, spread 8, dur60 5, pen 1, flags 0x00000002 BURST3 | auto dmg 1.4, rpm 750/750, spread 8, dur60 5, pen 1, flags 0x00000002 BURST3 | - |
| `base:autosurgeon` | - | - | - |
| `base:backupdisk` | - | - | - |
| `base:bolt` | fire projectile model 289, speed 0, dist 60, timer60 -1, scale 2.1, reflect 0.05, flags 0x00802200 MAKEDIZZY+NOMUZZLEFLASH+CALCULATETRAJECTORY | fire projectile model 289, speed 0, dist 60, timer60 -1, scale 2.1, reflect 0.05, flags 0x00802000 NOMUZZLEFLASH+CALCULATETRAJECTORY | Current generated projectile-shaped record; confirm whether this remains asset-only or weapon-visible during runtime cutover. |
| `base:briefcase` | - | - | - |
| `base:briefcase2` | none | none | - |
| `base:callisto` | auto dmg 1.2, rpm 900/900, spread 9, dur60 3, pen 1, flags - | auto dmg 2.4, rpm 300/300, spread 9, dur60 3, pen 5, flags - | - |
| `base:camspy` | device EYESPY, flags 0x00002000 NOMUZZLEFLASH | - | - |
| `base:choppergun` | auto dmg 1, rpm 900/900, spread 6, dur60 4, pen 2, flags - | - | - |
| `base:cloakingdevice` | device CLOAKDEVICE, flags 0x00002000 NOMUZZLEFLASH | - | - |
| `base:cmp150` | auto dmg 1, rpm 900/900, spread 9, dur60 3, pen 1, flags - | auto dmg 1, rpm 900/900, spread 9, dur60 3, pen 1, flags - | - |
| `base:combatboost` | special BOOST, rec60 30, sound 1481, flags 0x00002000 NOMUZZLEFLASH | special REVERTBOOST, rec60 30, sound 1481, flags 0x00002000 NOMUZZLEFLASH | - |
| `base:combatknife` | melee dmg 2, range 70, flags 0x00002000 NOMUZZLEFLASH | throw model 271, arm60 240, rec60 60, dmg 1, flags 0x00802000 NOMUZZLEFLASH+CALCULATETRAJECTORY | Needs thrown-knife .pdprojectile with sticky/hit behavior plus melee graph. |
| `base:commsrider` | throw model 18, arm60 240, rec60 60, dmg 0, flags 0x00802040 NOAUTOAIM+NOMUZZLEFLASH+CALCULATETRAJECTORY | - | Needs sticky thrown-device .pdprojectile and attached .pdentity behavior audit. |
| `base:crossbow` | fire projectile model 289, speed 0, dist 60, timer60 -1, scale 2.1, reflect 0.05, flags 0x00802200 MAKEDIZZY+NOMUZZLEFLASH+CALCULATETRAJECTORY | fire projectile model 289, speed 0, dist 60, timer60 -1, scale 2.1, reflect 0.05, flags 0x00802000 NOMUZZLEFLASH+CALCULATETRAJECTORY | Needs sedative/lethal bolt .pdprojectile payloads and sticky hit behavior. |
| `base:cyclone` | auto dmg 0.8, rpm 900/900, spread 6, dur60 4, pen 1, flags - | auto dmg 1.4, rpm 2000/2000, spread 25, dur60 4, pen 1, flags 0x00000020 BURST50 | - |
| `base:datauplink` | special UPLINK, rec60 30, sound 0, flags 0x00102000 NOMUZZLEFLASH+AUTOSWITCHUNSELECTABLE | - | - |
| `base:devastator` | fire projectile model 290, speed 0, dist 40, timer60 1200, scale 1, reflect 0.3, flags 0x30000040 NOAUTOAIM+GRENADE_LAUNCHER+EXPLOSIVE_RELATED | fire projectile model 290, speed 0, dist 40, timer60 360, scale 1, reflect 0.3, flags 0x30000140 NOAUTOAIM+STICKTOWALL+GRENADE_LAUNCHER+EXPLOSIVE_RELATED | Needs grenade-round and wall-hugger .pdprojectile payloads; secondary sticks, waits, falls, then explodes. |
| `base:disguise40` | none | - | - |
| `base:disguise41` | none | - | - |
| `base:doordecoder` | - | - | - |
| `base:dragon` | auto dmg 1.1, rpm 700/700, spread 6, dur60 4, pen 1, flags - | throw model 255, arm60 240, rec60 60, dmg 0, flags 0x00042040 NOAUTOAIM+NOMUZZLEFLASH+DISCARDWEAPON | Primary is auto fire; secondary discards weapon and arms Dragon proxy .pdentity. |
| `base:dy357` | single dmg 2, spread 0, rec60 20, dur60 0, pen 5, flags - | melee dmg 0.9, range 60, flags 0x0041a200 MAKEDIZZY+NOMUZZLEFLASH+BLUNTIMPACT+NOSTUN+F00400000 | - |
| `base:dy357lx` | single dmg 200, spread 0, rec60 30, dur60 0, pen 5, flags - | melee dmg 0.9, range 60, flags 0x0041a200 MAKEDIZZY+NOMUZZLEFLASH+BLUNTIMPACT+NOSTUN+F00400000 | - |
| `base:ecmmine` | throw model 278, arm60 240, rec60 60, dmg 0, flags 0x00802040 NOAUTOAIM+NOMUZZLEFLASH+CALCULATETRAJECTORY | - | Needs sticky thrown-device .pdprojectile and armed ECM .pdentity behavior audit. |
| `base:explosives` | - | - | - |
| `base:falcon2` | single dmg 1, spread 1, rec60 16, dur60 0, pen 1, flags - | melee dmg 0.9, range 60, flags 0x0041a200 MAKEDIZZY+NOMUZZLEFLASH+BLUNTIMPACT+NOSTUN+F00400000 | - |
| `base:falcon2scope` | single dmg 1, spread 1, rec60 16, dur60 0, pen 1, flags - | melee dmg 0.9, range 60, flags 0x0041a200 MAKEDIZZY+NOMUZZLEFLASH+BLUNTIMPACT+NOSTUN+F00400000 | - |
| `base:falcon2silencer` | single dmg 1, spread 1, rec60 16, dur60 0, pen 1, flags 0x00002000 NOMUZZLEFLASH | melee dmg 0.9, range 60, flags 0x0041a200 MAKEDIZZY+NOMUZZLEFLASH+BLUNTIMPACT+NOSTUN+F00400000 | - |
| `base:farsight` | single dmg 100, spread 0, rec60 0, dur60 4, pen 5, flags - | single dmg 100, spread 0, rec60 0, dur60 4, pen 5, flags - | - |
| `base:flightplans` | - | - | - |
| `base:grenade` | throw model 274, arm60 240, rec60 60, dmg 0, flags 0x00002040 NOAUTOAIM+NOMUZZLEFLASH | throw model 274, arm60 90, rec60 60, dmg 0, flags 0x00002040 NOAUTOAIM+NOMUZZLEFLASH | Needs timed grenade plus proxy/pinball grenade physicals; held-too-long primary behavior remains graph parameter. |
| `base:grenaderound` | fire projectile model 290, speed 0, dist 40, timer60 1200, scale 1, reflect 0.3, flags 0x30000040 NOAUTOAIM+GRENADE_LAUNCHER+EXPLOSIVE_RELATED | fire projectile model 290, speed 0, dist 40, timer60 360, scale 1, reflect 0.3, flags 0x30000140 NOAUTOAIM+STICKTOWALL+GRENADE_LAUNCHER+EXPLOSIVE_RELATED | Current generated projectile-shaped record; confirm direct weapon visibility versus nested projectile ownership. |
| `base:hammer` | - | - | - |
| `base:hammer_slot74` | - | - | - |
| `base:hammer_slot83` | - | - | - |
| `base:hammer_slot84` | - | - | - |
| `base:homingrocket` | - | - | - |
| `base:horizonscanner` | none | - | - |
| `base:irscanner` | device IRSCANNER, flags 0x00002000 NOMUZZLEFLASH | - | - |
| `base:k7avenger` | auto dmg 1.5, rpm 950/950, spread 6, dur60 4, pen 1, flags 0x00000002 BURST3 | auto dmg 1.5, rpm 950/950, spread 6, dur60 4, pen 1, flags 0x00082002 BURST3+NOMUZZLEFLASH+THREATDETECTOR | - |
| `base:keycard` | - | - | - |
| `base:keycard_slot62` | - | - | - |
| `base:keycard_slot63` | - | - | - |
| `base:keycard_slot64` | - | - | - |
| `base:keycard_slot65` | - | - | - |
| `base:keycard_slot66` | - | - | - |
| `base:keycard_slot67` | - | - | - |
| `base:keycard_slot68` | - | - | - |
| `base:laptopgun` | auto dmg 1.15, rpm 1000/1000, spread 6, dur60 4, pen 1, flags 0x00000002 BURST3 | throw model 343, arm60 240, rec60 60, dmg 0, flags 0x00842140 NOAUTOAIM+STICKTOWALL+NOMUZZLEFLASH+DISCARDWEAPON+CALCULATETRAJECTORY | Needs thrown Laptop carrier plus deployed autogun .pdentity. Runtime creates autogun first, then applies projectile physics. |
| `base:laser` | single dmg 1, spread 0, rec60 0, dur60 3, pen 1, flags - | auto dmg 0.1, rpm 3600/3600, spread 0, dur60 3, pen 1, flags - | - |
| `base:magsec` | single dmg 1.1, spread 6, rec60 16, dur60 0, pen 1, flags - | single dmg 1.1, spread 10, rec60 16, dur60 0, pen 1, flags 0x00000002 BURST3 | - |
| `base:mauler` | single dmg 1.2, spread 6, rec60 0, dur60 0, pen 1, flags - | single dmg 1.2, spread 6, rec60 0, dur60 0, pen 1, flags - | Secondary needs fire.charge_release module; current data is still shaped as shoot_single. |
| `base:nbomb` | throw model 272, arm60 240, rec60 60, dmg 0, flags 0x00002640 NOAUTOAIM+MAKEDIZZY+DISARM+NOMUZZLEFLASH | throw model 272, arm60 240, rec60 60, dmg 0, flags 0x00002640 NOAUTOAIM+MAKEDIZZY+DISARM+NOMUZZLEFLASH | Needs timed/proxy N-Bomb physicals plus N-Bomb storm entity/effect trigger. |
| `base:necklace` | - | - | - |
| `base:nightvision` | device NIGHTVISION, flags 0x00002000 NOMUZZLEFLASH | - | - |
| `base:nothing` | - | - | - |
| `base:phoenix` | single dmg 1.1, spread 3, rec60 16, dur60 0, pen 1, flags - | single dmg 1.2, spread 5, rec60 16, dur60 0, pen 1, flags 0x00004000 EXPLOSIVESHELLS | - |
| `base:presidentscanner` | device RTRACKER, flags 0x00002000 NOMUZZLEFLASH | - | - |
| `base:proximitymine` | throw model 276, arm60 240, rec60 0, dmg 0, flags 0x00802040 NOAUTOAIM+NOMUZZLEFLASH+CALCULATETRAJECTORY | none | Needs thrown proximity mine plus armed proxy .pdentity and proximity policy. |
| `base:psychosisgun` | single dmg 0.5, spread 3, rec60 16, dur60 0, pen 1, flags 0x00200200 MAKEDIZZY+PSYCHOSIS | - | - |
| `base:rcp120` | auto dmg 1.2, rpm 1100/1100, spread 6, dur60 4, pen 1, flags - | special RCP120CLOAK, rec60 30, sound 0, flags 0x00102000 NOMUZZLEFLASH+AUTOSWITCHUNSELECTABLE | - |
| `base:reaper` | auto dmg 1.2, rpm 60/1800, spread 56, dur60 2, pen 1, flags 0x00000002 BURST3 | melee dmg 0.05, range 80, flags 0x00002000 NOMUZZLEFLASH | - |
| `base:remotemine` | throw model 277, arm60 240, rec60 0, dmg 0, flags 0x00802040 NOAUTOAIM+NOMUZZLEFLASH+CALCULATETRAJECTORY | special DETONATE, rec60 30, sound 0, flags 0x00102000 NOMUZZLEFLASH+AUTOSWITCHUNSELECTABLE | Needs thrown remote mine, armed remote mine .pdentity, and remote detonator special module. |
| `base:researchtape` | - | - | - |
| `base:rocket` | - | - | - |
| `base:rocket_slot80` | - | - | - |
| `base:rocketlauncher` | fire projectile model 287, speed 60, dist 0, timer60 -1, scale 2.1, reflect 0.05, flags 0x08000040 NOAUTOAIM+PROJECTILE_POWERED | fire projectile model 287, speed 0, dist 5, timer60 -1, scale 2.1, reflect 0.05, flags 0x48000040 NOAUTOAIM+PROJECTILE_POWERED+HOMINGROCKET | Needs rocket and homing rocket .pdprojectile payloads; homing uses target prop steering. |
| `base:rocketlauncher_34` | fire projectile model 287, speed 20, dist 0, timer60 -1, scale 2.1, reflect 0.05, flags 0x08000040 NOAUTOAIM+PROJECTILE_POWERED | - | Single projectile variant; confirm whether this becomes a separate .pdweapon or inherited/base variant. |
| `base:rtracker` | device RTRACKER, flags 0x00002000 NOMUZZLEFLASH | - | - |
| `base:shieldtechitem` | - | - | - |
| `base:shotgun` | single dmg 0.6, spread 30, rec60 0, dur60 0, pen 1, flags - | single dmg 0.6, spread 16, rec60 0, dur60 0, pen 1, flags 0x00001000 BURST2 | - |
| `base:skedarbomb` | - | - | - |
| `base:slayer` | fire projectile model 288, speed 10, dist 0, timer60 -1, scale 4.1, reflect 0.05, flags 0x08000040 NOAUTOAIM+PROJECTILE_POWERED | fire projectile model 288, speed 10, dist 0, timer60 -1, scale 4.1, reflect 0.05, flags 0x28000840 NOAUTOAIM+FLYBYWIRE+PROJECTILE_POWERED+EXPLOSIVE_RELATED | Needs powered rocket plus fly-by-wire rocket .pdprojectile; includes player/bot control and self-destruct rules. |
| `base:sniperrifle` | single dmg 1.2, spread 0, rec60 16, dur60 4, pen 1, flags 0x00002000 NOMUZZLEFLASH | special CROUCH, rec60 30, sound 0, flags 0x00102000 NOMUZZLEFLASH+AUTOSWITCHUNSELECTABLE | - |
| `base:suicidepill` | device SUICIDEPILL, flags 0x00002000 NOMUZZLEFLASH | - | - |
| `base:suitcase` | - | - | - |
| `base:superdragon` | auto dmg 1.2, rpm 700/700, spread 6, dur60 4, pen 1, flags - | fire projectile model 291, speed 0, dist 30, timer60 1200, scale 1, reflect 0.1, flags 0x30000040 NOAUTOAIM+GRENADE_LAUNCHER+EXPLOSIVE_RELATED | Primary auto fire plus secondary grenade-round .pdprojectile. |
| `base:targetamplifier` | throw model 433, arm60 240, rec60 60, dmg 0, flags 0x00802040 NOAUTOAIM+NOMUZZLEFLASH+CALCULATETRAJECTORY | - | Needs sticky thrown-device .pdprojectile and attached target-amplifier .pdentity behavior audit. |
| `base:tester` | single dmg 1, spread 6, rec60 16, dur60 0, pen 1, flags - | - | - |
| `base:timedmine` | throw model 275, arm60 240, rec60 0, dmg 0, flags 0x00802040 NOAUTOAIM+NOMUZZLEFLASH+CALCULATETRAJECTORY | none | Needs thrown timed mine plus armed timed mine .pdentity. |
| `base:tracerbug` | throw model 18, arm60 240, rec60 60, dmg 0, flags 0x00802040 NOAUTOAIM+NOMUZZLEFLASH+CALCULATETRAJECTORY | - | Needs sticky thrown-device .pdprojectile and attached tracer .pdentity behavior audit. |
| `base:tranquilizer` | single dmg 0.25, spread 3, rec60 16, dur60 0, pen 1, flags 0x00000200 MAKEDIZZY | melee dmg 100, range 60, flags 0x00002000 NOMUZZLEFLASH | - |
| `base:unarmed` | melee dmg 0.5, range 60, flags 0x0041a200 MAKEDIZZY+NOMUZZLEFLASH+BLUNTIMPACT+NOSTUN+F00400000 | melee dmg 0.3, range 60, flags 0x0041a600 MAKEDIZZY+DISARM+NOMUZZLEFLASH+BLUNTIMPACT+NOSTUN+F00400000 | - |
| `base:watchlaser` | auto dmg 1, rpm 900/900, spread 6, dur60 4, pen 1, flags - | - | - |
| `base:xrayscanner` | device XRAYSCANNER, flags 0x00002000 NOMUZZLEFLASH | - | - |
