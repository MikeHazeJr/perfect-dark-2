#include <ultra64.h>
#include "constants.h"
#include "game/activemenu.h"
#include "game/pdmode.h"
#include "game/bondgun.h"
#include "game/game_0b0fd0.h"
#include "game/inv.h"
#include "game/playermgr.h"
#include "game/options.h"
#include "bss.h"
#include "lib/joy.h"
#include "lib/str.h"
#include "data.h"
#include "types.h"
#include "game/player.h"
#include "input.h"
#include "actionmap.h"

void amTick(void)
{
	s32 prevplayernum = g_Vars.currentplayernum;
	s32 i;

	for (i = 0; i < LOCALPLAYERCOUNT(); i++) {
		setCurrentPlayerNum(i);
		g_AmIndex = g_Vars.currentplayernum;

		if (g_AmMenus[g_AmIndex].togglefunc) {
			if (bgunConsiderToggleGunFunction(60, false, true, 0) > 0) {
				g_AmMenus[g_AmIndex].togglefunc = false;
			}
		} else {
			// empty
		}

		if (g_Vars.normmplayerisrunning == false
				&& invGetCount() != g_AmMenus[g_AmIndex].numitems) {
			amAssignWeaponSlots();
		}

		if (g_Vars.currentplayer->activemenumode != AMMODE_CLOSED) {
			/* M0.2: collapsed sub-frame to per-frame */
			s32 playernum = g_Vars.currentplayernum;
			s32 controlmode = optionsGetControlMode(g_Vars.currentplayerstats->mpindex);

			{
				s8 gotonextscreen = false;

				/* M0.2: stick axes — actionAxis gives -1..1, multiply by 80 for s8 range.
				 * Use AIM axis (right stick) so player can hold D-pad left with
				 * left thumb to keep the radial open while selecting with right thumb. */
				f32 ax_x, ax_y;
				actionAxis(playernum, ACTION_AXIS_AIM_X, &ax_x, &ax_y);
				/* Radial selection is screen-space: undo aim Y invert so stick-up
				 * always selects the top row regardless of look inversion. */
				if (actionmapGetStickInvertY()) {
					ax_y = -ax_y;
				}
				s8 cstickx = (s8)(ax_x * 80.0f);
				s8 csticky = (s8)(ax_y * 80.0f);

#ifdef AVOID_UB
				s32 absstickx;
				s32 abssticky;
#else
				s8 absstickx;
				s8 abssticky;
#endif
				/* M0.2: collapsed sub-frame to per-frame */
				s32 held_use       = actionHeld(playernum, ACTION_USE);
				s32 held_fire_mode = actionHeld(playernum, ACTION_FIRE_MODE);
				s32 held_fire_sec  = actionHeld(playernum, ACTION_FIRE_SECONDARY);
				s32 held_dpad_down = actionHeld(playernum, ACTION_DPAD_DOWN);
				s32 held_dpad_up   = actionHeld(playernum, ACTION_DPAD_UP);
				s32 held_dpad_left = actionHeld(playernum, ACTION_DPAD_LEFT);
				s32 held_dpad_right = actionHeld(playernum, ACTION_DPAD_RIGHT);
				s32 held_cbtn_up   = actionHeld(playernum, ACTION_CBUTTON_UP);
				s32 held_cbtn_down = actionHeld(playernum, ACTION_CBUTTON_DOWN);
				s32 held_cbtn_left = actionHeld(playernum, ACTION_CBUTTON_LEFT);
				s32 held_cbtn_right = actionHeld(playernum, ACTION_CBUTTON_RIGHT);
				s32 pressed_fire   = actionPressed(playernum, ACTION_FIRE_PRIMARY);

				bool stickpushed = false;
				s32 slotnum;
				bool stayopen;
				bool toggle;
				s32 row;
				s32 column;

				column = 1;
				row = 1;
				stayopen = false;
				toggle = false;

				g_AmMenus[g_AmIndex].allbots = false;

				s32 newstickx = (s32)cstickx;
				s32 newsticky = (s32)csticky;
				if (g_Vars.currentplayernum == 0 && inputMouseIsLocked()) {
					f32 mdx, mdy;
					struct activemenu *am = &g_AmMenus[g_AmIndex];
					inputMouseGetAbsScaledDelta(&mdx, &mdy);
					if (mdx || mdy) {
						am->mousex += mdx * PLAYER_EXTCFG().radialmenuspeed;
						am->mousey += mdy * PLAYER_EXTCFG().radialmenuspeed;
						am->mousex = (am->mousex > 127.f) ? 127.f : (am->mousex < -128.f) ? -128.f : am->mousex;
						am->mousey = (am->mousey > 127.f) ? 127.f : (am->mousey < -128.f) ? -128.f : am->mousey;
					}
					newstickx += (s32)am->mousex;
					newsticky -= (s32)am->mousey;
				}
				cstickx = (newstickx < -128) ? -128 : (newstickx > 127) ? 127 : newstickx;
				csticky = (newsticky < -128) ? -128 : (newsticky > 127) ? 127 : newsticky;

				if (g_Vars.currentplayer->activemenumode == AMMODE_EDIT) {
					/* M0.2: in edit mode, only the stay-open action passes through */
					s32 edit_stayopen;
					if (controlmode == CONTROLMODE_PC) {
						edit_stayopen = held_dpad_down;
					} else {
						edit_stayopen = held_use;
					}
					cstickx = 0;
					csticky = 0;
					pressed_fire = 0;
					/* Clear all directional helds for edit mode */
					held_dpad_up = 0; held_dpad_down = 0;
					held_dpad_left = 0; held_dpad_right = 0;
					held_cbtn_up = 0; held_cbtn_down = 0;
					held_cbtn_left = 0; held_cbtn_right = 0;
					held_use = 0; held_fire_mode = 0; held_fire_sec = 0;
					/* Restore only the stay-open signal */
					if (controlmode == CONTROLMODE_PC) {
						held_dpad_down = edit_stayopen;
					} else {
						held_use = edit_stayopen;
					}
				}

				/*
				 * M0.2: Stay-open and allbots logic.
				 * Original code had per-controlmode bitmask selection.
				 * With actionmap, bindings handle the mapping; we query
				 * the semantic actions directly.
				 *
				 * JPN fixes the bug documented in amChangeScreen:
				 * controlmodes 13/14 use L_TRIG|R_TRIG for stay-open
				 * and A_BUTTON for allbots.
				 */
				if (controlmode == CONTROLMODE_13 || controlmode == CONTROLMODE_14) {
					if (held_fire_mode || held_fire_sec) {
						stayopen = true;
					}

					if (held_use) {
						if (g_Vars.currentplayer->numaibuddies > 0) {
							g_AmMenus[g_AmIndex].allbots = true;
						}
					}
				} else if (controlmode == CONTROLMODE_PC) {
					/* PC mode: D_JPAD keeps menu open, R_TRIG for allbots */
					if (held_dpad_down) {
						stayopen = true;
					}

					if (held_fire_sec) {
						if (g_Vars.currentplayer->numaibuddies > 0) {
							g_AmMenus[g_AmIndex].allbots = true;
						}
					}
				} else {
					/* Standard: A_BUTTON keeps open, L_TRIG|R_TRIG for allbots */
					if (held_use) {
						stayopen = true;
					}

					if (held_fire_mode || held_fire_sec) {
						if (g_Vars.currentplayer->numaibuddies > 0) {
							g_AmMenus[g_AmIndex].allbots = true;
						}
					}
				}

				// If entering allbots mode, save current screen
				if (g_AmMenus[g_AmIndex].allbots
						&& g_AmMenus[g_AmIndex].screenindex >= 2
						&& g_AmMenus[g_AmIndex].origscreennum == 0) {
					g_AmMenus[g_AmIndex].origscreennum = g_AmMenus[g_AmIndex].screenindex;
					g_AmMenus[g_AmIndex].screenindex = 2;
					amChangeScreen(0);
				}

				// If exiting allbots mode, return to original screen
				if (!g_AmMenus[g_AmIndex].allbots
						&& g_AmMenus[g_AmIndex].origscreennum) {
					g_AmMenus[g_AmIndex].screenindex = g_AmMenus[g_AmIndex].origscreennum;
					g_AmMenus[g_AmIndex].origscreennum = 0;
					amChangeScreen(0);
				}

				/* M0.2: directional held checks — merged D-pad + C-buttons */
				if (held_dpad_up || held_cbtn_up) {
					row = 0;
				}

				if (held_dpad_down || held_cbtn_down) {
					row = 2;
				}

				if (held_dpad_left || held_cbtn_left) {
					column = 0;
				}

				if (held_dpad_right || held_cbtn_right) {
					column = 2;
				}

				/*
				 * M0.2: Dual-controller modes (CONTROLMODE_21-24).
				 * With actionmap, both controllers feed the same player's
				 * action state, so the second-pad reads are already merged
				 * into the queries above. The aim-axis stick processing
				 * below uses ACTION_AXIS_AIM for the second stick.
				 */
				if (controlmode == CONTROLMODE_23
						|| controlmode == CONTROLMODE_24
						|| controlmode == CONTROLMODE_22
						|| controlmode == CONTROLMODE_21) {
					/* M0.2: second stick via aim axis */
					f32 aim_x, aim_y;
					s8 cstickx2, csticky2;
					actionAxis(playernum, ACTION_AXIS_AIM_X, &aim_x, &aim_y);
					if (actionmapGetStickInvertY()) {
						aim_y = -aim_y;
					}
					cstickx2 = (s8)(aim_x * 80.0f);
					csticky2 = (s8)(aim_y * 80.0f);

					/*
					 * M0.2: In dual-controller edit mode, the original code
					 * zeroed the second stick and pressed; with actionmap
					 * the edit-mode zeroing above already covers this since
					 * both pads feed the same player actions.
					 */

					/* Note: stayopen from A_BUTTON on pad2 and toggle from
					 * Z_TRIG on pad2 are already captured by held_use and
					 * pressed_fire above since actionmap merges both pads. */

					absstickx = cstickx2 < 0 ? -cstickx2 : cstickx2;
					abssticky = csticky2 < 0 ? -csticky2 : csticky2;

					if (absstickx > 20 || abssticky > 20) {
						if ((f32)abssticky / (f32)absstickx < 0.268f) {
							row = 1;
							column = cstickx2 < 0 ? 0 : 2;
						} else if ((f32)absstickx / (f32)abssticky < 0.268f) {
							column = 1;
							row = csticky2 < 0 ? 2 : 0;
						} else {
							column = cstickx2 < 0 ? 0 : 2;
							row = csticky2 < 0 ? 2 : 0;
						}

						stickpushed = true;
					}
				}

				absstickx = cstickx < 0 ? -cstickx : cstickx;
				abssticky = csticky < 0 ? -csticky : csticky;

				if (absstickx > 20 || abssticky > 20) {
					stickpushed = true;

					if ((f32)abssticky / (f32)absstickx < 0.268f) {
						column = cstickx < 0 ? 0 : 2;
						row = 1;
					} else if ((f32)absstickx / (f32)abssticky < 0.268f) {
						column = 1;
						row = csticky < 0 ? 2 : 0;
					} else {
						column = cstickx < 0 ? 0 : 2;
						row = csticky < 0 ? 2 : 0;
					}
				}

				if (g_Vars.currentplayer->isdead) {
					stayopen = false;
				}


				if (!stayopen &&
						(g_Vars.currentplayer->activemenumode != AMMODE_EDIT || g_Menus[g_MpPlayerNum].curdialog == NULL)) {
					amClose();
				}

				if (pressed_fire) {
					toggle = true;
				}

				if (toggle) {
					if (g_AmMenus[g_AmIndex].screenindex >= 2) {
						if (g_Vars.numaibuddies && g_MissionConfig.iscoop) {
							// Bot command screen, in coop with AI buddies
							if (g_AmMenus[g_AmIndex].slotnum == 4) {
								gotonextscreen = true;
							} else {
								amApply(g_AmMenus[g_AmIndex].slotnum);
							}
						} else {
							// Bot command screen, in multiplayer
							if (g_AmBotCommands[g_AmMenus[g_AmIndex].slotnum] == AIBOTCMD_ATTACK) {
								amOpenPickTarget();
							} else if (g_AmMenus[g_AmIndex].allbots == false) {
								gotonextscreen = true;
#if VERSION < VERSION_NTSC_1_0
								if (g_AmMenus[g_AmIndex].slotnum != 4) {
									amApply(g_AmMenus[g_AmIndex].slotnum);
								}
#endif
							}

#if VERSION >= VERSION_NTSC_1_0
							if (g_AmMenus[g_AmIndex].slotnum != 4) {
								amApply(g_AmMenus[g_AmIndex].slotnum);
							}
#endif
						}
					} else {
						// Weapon or function screen
						if (g_AmMenus[g_AmIndex].slotnum == 4) {
							gotonextscreen = true;
						} else {
							amApply(g_AmMenus[g_AmIndex].slotnum);
						}
					}
				}

				if (gotonextscreen) {
					amChangeScreen(gotonextscreen);

					// If weapon has no functions, skip past function screen
					if (g_AmMenus[g_AmIndex].screenindex == 1) {
						struct weaponfunc *pri = weaponGetFunction(&g_Vars.currentplayer->hands[0].gset, FUNC_PRIMARY);
						struct weaponfunc *sec = weaponGetFunction(&g_Vars.currentplayer->hands[0].gset, FUNC_SECONDARY);

						if (!pri && !sec) {
							amChangeScreen(gotonextscreen);
						}
					}
				}

				slotnum = column * 1 + row * 3;

				if (g_Vars.currentplayer->activemenumode != AMMODE_EDIT) {
					if (slotnum == 4) {
						if (g_AmMenus[g_AmIndex].returntimer <= 0) {
							g_AmMenus[g_AmIndex].returntimer = 0;
							g_AmMenus[g_AmIndex].slotnum = slotnum;
						} else {
							g_AmMenus[g_AmIndex].returntimer--;
						}
					} else {
						bool gotoslot = true;
						char text[28];
						u32 flags;

						amGetSlotDetails(slotnum, &flags, text);

						if (strcmp(text, "") == 0) {
							gotoslot = false;
						}

						// If focusing a corner slot with C buttons or J pad,
						// set a special timer for the release. The player is
						// unlikely to release both C buttons on the same frame,
						// so this gives a bit of grace and prevents accidental
						// movement to a neighbouring slot.
						if (g_AmMenus[g_AmIndex].slotnum != 4
								&& !stickpushed
								&&
								(g_AmMenus[g_AmIndex].slotnum == 0
								 || g_AmMenus[g_AmIndex].slotnum == 2
								 || g_AmMenus[g_AmIndex].slotnum == 6
								 || g_AmMenus[g_AmIndex].slotnum == 8)) {
							if (slotnum != g_AmMenus[g_AmIndex].fromslotnum) {
								g_AmMenus[g_AmIndex].cornertimer = 2;
								g_AmMenus[g_AmIndex].fromslotnum = slotnum;
								gotoslot = false;
							}

							if (g_AmMenus[g_AmIndex].cornertimer > 0 && gotoslot) {
								gotoslot = false;
								g_AmMenus[g_AmIndex].cornertimer--;
							}
						}

						if (gotoslot) {
							g_AmMenus[g_AmIndex].returntimer = 15;
							g_AmMenus[g_AmIndex].slotnum = slotnum;
						}
					}
				}
			}
		}
		else {
			g_AmMenus[g_AmIndex].mousex = 0.f;
			g_AmMenus[g_AmIndex].mousey = 0.f;
		}

		if (g_Vars.currentplayer->activemenumode != AMMODE_EDIT) {
			s16 dist;
			s16 dstradius;

			if (g_AmMenus[g_AmIndex].dstx != -123) {
				s16 dist;

				// Update selection x/y values
				g_AmMenus[g_AmIndex].selx = (g_AmMenus[g_AmIndex].selx + g_AmMenus[g_AmIndex].dstx) / 2;
				g_AmMenus[g_AmIndex].sely = (g_AmMenus[g_AmIndex].sely + g_AmMenus[g_AmIndex].dsty) / 2;

				dist = g_AmMenus[g_AmIndex].selx - g_AmMenus[g_AmIndex].dstx;

				if (dist <= 1 && dist >= -1) {
					g_AmMenus[g_AmIndex].selx = g_AmMenus[g_AmIndex].dstx;
				}

				dist = g_AmMenus[g_AmIndex].sely - g_AmMenus[g_AmIndex].dsty;

				if (dist <= 1 && dist >= -1) {
					g_AmMenus[g_AmIndex].sely = g_AmMenus[g_AmIndex].dsty;
				}
			}

			// Update x radius (the expanding effect when a new screen is loaded)
			dstradius = g_AmMenus[g_AmIndex].slotwidth + 5;

			g_AmMenus[g_AmIndex].xradius = (g_AmMenus[g_AmIndex].xradius * 3 + dstradius) / 4;

			dist = g_AmMenus[g_AmIndex].xradius - dstradius;

			if (dist <= 1 && dist >= -1) {
				g_AmMenus[g_AmIndex].xradius = dstradius;
			}

			// Update alpha of slots so they fade in
			if (g_AmMenus[g_AmIndex].alphafrac < 1) {
				g_AmMenus[g_AmIndex].alphafrac += (f32)g_Vars.lvupdate240 / (4.f * 30.0f);
			}

			if (g_AmMenus[g_AmIndex].alphafrac > 1) {
				g_AmMenus[g_AmIndex].alphafrac = 1;
			}

			// Make selection border pulsate
			g_AmMenus[g_AmIndex].selpulse += (f32)g_Vars.lvupdate240 / (4.f * 5.0f);

			if (g_AmMenus[g_AmIndex].selpulse > 18.849555969238f) {
				g_AmMenus[g_AmIndex].selpulse -= 18.849555969238f;
			}
		}
	}

	setCurrentPlayerNum(prevplayernum);
}
