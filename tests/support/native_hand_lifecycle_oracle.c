/* native_hand_lifecycle_oracle.c
 *
 * TEST-ONLY native C reference oracle. UNCOMPILED, UNRUN, SOURCE_PARTIAL.
 * Donor identities are listed in the header. Context sources:
 *   struct hand/gset/player/gunctrl/weaponfunc/weapon/inventory_ammo/
 *     handweaponinfo/g_vars ORIGIN
 *     5b60d3803e8fe99fd1f3087e23dc0a8a649f62523b3427b4ace469264bb1ee38
 *   types.h bool/Mtxf 0B0EB0110BF4E63D9B9494A85E4A97FB2B637E0CABD6A853661D6DEEDA3BD285
 *   constants.h B50E9C4AA6A7DCD131B2FE9009AFB807291885D82863E2536A2B34E86F7C8736
 *     (macro index 1B42069E3BB60F906389C35331E74CC419902E1C6C160F869C28304DEBEAA2FC,
 *      TICKS conditional 45355C007A4E1882D4E434615D1DF3F74F48CB2DF9E3E12FC4F0988E1F03E7E9)
 *   versions.h 0262AD3563176C3AEFB0816F4D75E51F73B20F6872F73FF334EDF6EFE9B50CAA
 *   bondgun.c prefix (MAX_PITCH, MASTERLOADSTATE_LOADED)
 *     A57695B13CEB67A24750283931DF3E422C227768FD5778594D391198A1A95A34
 *   enum weaponnum C8C09BD0D16B3FBC532C3257A7498D28AC331641A3E52FDE18D550D79BE51710
 *   sfx.h 2bb7885f6b5602777c40ba94a558e6413abffe436c6abb4b6c4ab28e739f4c30
 *   snd.h 18BF379E2D06C42B3E174E87E58A6C8C561D6EFC3A3BD1F42D7CFF7B8B8A057B
 *   mtx.h F15649FE78F0DA28A872B2B385A838CF35268CC2A5F15AA7B3B1707A69DAD438
 *   bondgun.h 59964A3C485A493BB8A4017976FB275C9E4D6EF89E0368C74E529E21E35B88A1
 *   lookup signatures 69B55ADE2A78F958FE94962AEB4C688F801D3961AE32B056E40DBA535C085F86
 *   g_PlayerInvincible 7C34852FFE5A65EF7361F75CD495E0EC6333AFFA1C1BCD06A8941F41CB524740
 *   var80095200 1B0998487B649E4D3FD9F6AB7CBB283ED46F36DD3AD40E1EDE9C6D4A860B1025
 *
 * Explicit transformations of the native bodies (all others are verbatim):
 * T1  bgunX -> nhloX, with a leading nhlo_oracle *o parameter.
 * T2  struct X -> struct nhlo_X narrow fixtures; s32/u32/... -> stdint.
 * T3  g_Vars, g_PlayerInvincible, var80095200 keep their spelling as macros
 *     over oracle fields.
 * T4  Callees -> nhloStub_<native name>(o, same arguments); see header.
 * T5  Every native store statement is followed by NHLO_STORED, which reads
 *     back the stored lvalue and logs it. No other statement is added.
 * T6  Each otherhand declaration is followed by a SELECT_OTHERHAND log.
 * T7  #if VERSION >= VERSION_JPN_FINAL -> if (o->version >= VERSION_JPN_FINAL).
 * T8  TICKS(val) chooses between the two supplied definitions by o->pal.
 * T9  case WEAPON_BRIEFCASE2 cannot be a constant label with a runtime
 *     version: the switch operand is read once into nhlo_weaponnum and the
 *     default arm first breaks (no sound) when it equals
 *     nhloWeaponBriefcase2(o->version).
 * T10 true/false -> NHLO_TRUE/NHLO_FALSE (1/0); native definitions were not
 *     supplied.
 * T11 Reload's unread decomp `u32 stack;` is omitted; ChangeFunc gains a
 *     nonsemantic (void)info.
 * T12 SFX_RELOAD_DEFAULT = 0x804f: SFX_8000 = 0x8000 then sequential to
 *     SFX_804E, before the first VERSION conditional in enum sfx. Passing it
 *     to the s16 parameter, and var80095200 to the s32 parameter, keeps the
 *     native implementation-defined conversions (0x804f -> -32689 on two's
 *     complement targets). Compilers may warn here.
 */
#include "native_hand_lifecycle_oracle.h"

#include <string.h>

int nhloUsingSecondary(int stored, int invert)
{
    if (stored) return invert == 0;
    return invert == 1;
}
static int nhloRangeWeapon(nhlo_ready_case *s)
{
    ++s->slot_calls; ++s->weapon_calls;
    if (s->flip_throwing) s->throwing = 1;
    return s->range_weapon;
}
int nhloReadyToSwitch(nhlo_ready_case *s)
{
    if (s->range_valid && nhloRangeWeapon(s) == s->right_weapon && s->throwing == 0) return 0;
    if (s->handnum == 0 && s->left_inuse && s->left_state == 8 && s->left_minor == 0) return 0;
    if (s->pending_weapon >= 0) return 1;
    if (s->handnum == 1) {
        if (s->handnum == 1) {
            if (s->right_state == 1) return 0;
            if (s->right_state == 7) return 0;
            if (s->right_state == 4) return 0;
        }
        if (s->inuse && !s->dualwield) return 1;
        if (!s->inuse && s->dualwield) return 1;
    }
    return 0;
}

void nhloTickHand(nhlo_tick_frame_case *s)
{
    int32_t lvupdate;
    int32_t i = 20;
    ++s->info_calls;
    s->update60 = s->update_after_info;
    lvupdate = s->update60;
    s->increment = s->update60;
    if (s->modern) {
        ++s->speed_calls;
        s->increment_real = s->speed * (s->pal ? s->increment * 1.2f : s->increment);
    } else s->increment_legacy += s->update60;
    while (i >= 0) {
        s->child_inputs[s->child_calls] = lvupdate;
        lvupdate = s->child_returns[s->child_calls++];
        --i;
        if (lvupdate <= 0) break;
    }
}

static void nhloIdleEvent(nhlo_idle_case *s,int kind,int arg) {
    if(s->count<64)s->events[s->count]=(nhlo_auto_event){kind,arg,s->current,0};
    ++s->count;
}
static int nhloIdleFunction(nhlo_idle_case *s) {nhloIdleEvent(s,NHLO_IDLE_FUNCTION,0);return s->gunfunc;}
static int nhloIdleReady(nhlo_idle_case *s) {nhloIdleEvent(s,NHLO_IDLE_READY,0);return s->ready;}
static int nhloIdleSet(nhlo_idle_case *s,int state) {
    nhloIdleEvent(s,NHLO_IDLE_SETSTATE,state);
    if(!(s->accept_states&(1u<<state)))return 0;
    s->hand.state=state;s->hand.stateminor=0;s->hand.stateframes=0;return 1;
}
static int nhloIdleAmmo(nhlo_idle_case *s,int function) {nhloIdleEvent(s,NHLO_IDLE_AMMO,function);return s->ammo[function];}
static int nhloIdleIsV2(nhlo_idle_case *s) {nhloIdleEvent(s,NHLO_IDLE_IS_V2,0);return s->is_v2;}
static int nhloIdleV2(nhlo_idle_case *s) {nhloIdleEvent(s,NHLO_IDLE_V2,0);if(s->flip_v2)s->current=1;return s->v2_idle;}
static int nhloIdleFlag(nhlo_idle_case *s,uint32_t flag) {nhloIdleEvent(s,NHLO_IDLE_FLAG,(int)flag);return (s->flags&flag)!=0;}
static int nhloIdleStored(nhlo_idle_case *s) {nhloIdleEvent(s,NHLO_IDLE_STORED,0);return s->stored;}
static int nhloIdleExcluded(nhlo_idle_case *s,int function) {nhloIdleEvent(s,NHLO_IDLE_EXCLUDED,function);return s->excluded;}
static void nhloIdleCaptureFunction(nhlo_idle_case *s,int function) {nhloIdleEvent(s,NHLO_IDLE_CAPTURE_FUNCTION,function);}
static void nhloIdleSwitch(nhlo_idle_case *s) {nhloIdleEvent(s,NHLO_IDLE_SWITCH,0);if(s->flip_switch)s->current=1;}
/* Saved native idle donor, retaining statement/branch structure; field and
 * boundary substitution and runtime version selection only. */
int32_t nhloIdle(nhlo_idle_case *s)
{
	nhlo_auto_hand *hand = &s->hand;
	int usesec;
	int32_t gunfunc = nhloIdleFunction(s);
	int32_t sp34;
	int32_t sp30;
	int changefunc;
	int32_t next;
	nhlo_auto_hand *lhand;

	hand->lastdirvalid = 0;
	hand->burstbullets = 0;
if (!s->modern) {
	hand->animframeincfreal = hand->animframeinc;
}
	hand->shotremainder = 0;

	// If ready to change gun due to manual switch, just do that
	if (nhloIdleReady(s) && nhloIdleSet(s, 5)) {
		return s->lvupdate;
	}

	if (gunfunc == hand->gset.weaponfunc) {
		hand->unk0cc8_07 = 0;
	}

	hand->unk0cc8_08 = 0;

	if (hand->inuse) {
		sp34 = nhloIdleAmmo(s, hand->gset.weaponfunc);

		// Handle changing gun function
		if (gunfunc != hand->gset.weaponfunc && hand->modenext != 9) {
			changefunc = 1;

			if (hand->unk0cc8_07 && nhloIdleAmmo(s, 1 - hand->gset.weaponfunc) < 0) {
				changefunc = 0;
			}

			if (changefunc && s->weaponnum == 26) {
				if (sp34 == 0) {
					hand->count60 = 0;
					hand->count = 0;
					hand->gset.weaponfunc = gunfunc;

					if (nhloIdleSet(s, 1)) {
						return s->lvupdate;
					}
				} else {
					if (sp34 < 0) {
						changefunc = 0;
					}
				}
			}

			if (changefunc) {
				hand->unk0cc8_07 = 0;

				if (nhloIdleSet(s, 7)) {
					return s->lvupdate;
				}
			}
		}

		if (nhloIdleIsV2(s)) {
			/* Idle ammo is reload policy, never a graph branch selection. */
			if (nhloIdleV2(s)) return s->lvupdate;
			if ((hand->modenext == 9 || (!hand->triggeron && sp34 == 0)) && sp34 >= 0 && sp34 < 2) {
				hand->modenext = 0;
				if (nhloIdleSet(s, 1)) return s->lvupdate;
			}
			return 0;
		}
		if (sp34 < 0) {
			// Attempted to shoot with no ammo

			// Consider switching to another weapon
			if (nhloIdleFlag(s, 1)
					&& (s->weaponnum != 34 || s->handnum != 1)
					&& nhloIdleSet(s, 8)) {
				return s->lvupdate;
			}

			// Consider switching to other gun function
			usesec = nhloIdleStored(s);

			if (usesec == gunfunc) {
				sp30 = nhloIdleAmmo(s, 1 - hand->gset.weaponfunc);

				if (nhloIdleExcluded(s, 1 - hand->gset.weaponfunc)
						&& s->weaponnum != 20) {
					if (s->wantammo) {
						nhloIdleCaptureFunction(s, 1 - hand->gset.weaponfunc);

						if ((s->function_type & 0xff) != 3) {
							sp30 = -1;
						}
					} else {
						sp30 = -1;
					}
				}

				if (sp30 < 0) {
					hand->unk0cc8_08 = 1;
				} else {
					if (!nhloIdleFlag(s, 0x04000000)
							|| hand->gset.weaponfunc == 1) {
						hand->unk0cc8_07 = 1;

						if (nhloIdleSet(s, 7)) {
							return s->lvupdate;
						}
					}
				}
			}
		} else if (sp34 == 0) {
			// Clip is empty
			if (hand->triggeron && s->weaponnum != 0) {
				hand->unk0cc8_01 = 0;

				if (nhloIdleSet(s, 3)) {
					return s->lvupdate;
				}
			} else {
				hand->count60 = 0;
				hand->count = 0;

				if (nhloIdleSet(s, 1)) {
					hand->modenext = 0;
					return s->lvupdate;
				}
			}
		} else {
			// Clip has ammo
			if (hand->triggeron || (hand->activatesecondary && hand->gset.weaponfunc == 1)) {
				if (s->weaponnum != 0) {
					s->autoselect[s->current] = 0;

					hand->mode = 1;
					hand->count = 0;
					hand->count60 = 0;
					hand->triggerreleased = 0;
					hand->activatesecondary = 0;

					if (nhloIdleSet(s, 4)) {
						return s->lvupdate;
					}
				}
			}

			// Not attacking, but the player may have attempted
			// to change guns or reload while firing
			if (hand->modenext != 0) {
				next = hand->modenext;

				hand->mode = hand->modenext;
				hand->count60 = 0;
				hand->count = 0;
				hand->modenext = 0;

				if (next == 9 && sp34 < 2 && sp34 >= 0) {
					if (nhloIdleSet(s, 1)) {
						/* Native no-op has no effects. */
						return s->lvupdate;
					}
				}
			}
		}
	}

	if (s->handnum == 0) {
		if (s->wantammo) {
			nhloIdleSwitch(s);
		} else {
			lhand = &s->other[s->current];

			if ((hand->unk0cc8_08 || !hand->inuse)
					&& (lhand->unk0cc8_08 || !lhand->inuse)
					&& (hand->triggeron || lhand->triggeron)) {
				nhloIdleSwitch(s);
			}

			hand->unk0cc8_08 = lhand->unk0cc8_08 = 0;
		}
	}

	return 0;
}


static void nhloAutoEvent(nhlo_auto_case *s,int kind,int arg,float pitch) {
    if(s->count<64)s->events[s->count]=(nhlo_auto_event){kind,arg,s->current,pitch};
    ++s->count;
}
static int nhloAutoFunction(nhlo_auto_case *s) {nhloAutoEvent(s,NHLO_AUTO_FUNCTION,0,0);return s->gunfunc;}
static int nhloAutoSet(nhlo_auto_case *s,int state) {
    nhloAutoEvent(s,NHLO_AUTO_SETSTATE,state,0);
    if(!(s->accept_states&(1u<<state)))return 0;
    s->hand.state=state;s->hand.stateminor=0;s->hand.stateframes=0;return 1;
}
static int nhloAutoReady(nhlo_auto_case *s) {nhloAutoEvent(s,NHLO_AUTO_READY,0,0);return s->ready;}
static void nhloAutoPitch(nhlo_auto_case *s,float value) {nhloAutoEvent(s,NHLO_AUTO_PITCH,0,value);}
static void nhloAutoDelete(nhlo_auto_case *s) {nhloAutoEvent(s,NHLO_AUTO_DELETE,0,0);}
static void nhloAutoFree(nhlo_auto_case *s) {nhloAutoEvent(s,NHLO_AUTO_FREE,0,0);}
static int nhloAutoAmmo(nhlo_auto_case *s,int function) {nhloAutoEvent(s,NHLO_AUTO_AMMO,function,0);return s->ammo;}
static void nhloAutoCapture(nhlo_auto_case *s) {nhloAutoEvent(s,NHLO_AUTO_CAPTURE,1-s->handnum,0);if(s->flip_capture)s->current=1;}
static int nhloAutoOtherAmmo(nhlo_auto_case *s,nhlo_auto_other *other,int function) {nhloAutoEvent(s,NHLO_AUTO_OTHER_AMMO,function,0);return other->ammo[function];}
static int nhloAutoExcluded(nhlo_auto_case *s,nhlo_auto_other *other,int function) {nhloAutoEvent(s,NHLO_AUTO_EXCLUDED,function,0);return other->excluded;}
static void nhloAutoSwitchWeapon(nhlo_auto_case *s) {nhloAutoEvent(s,NHLO_AUTO_SWITCH,0,0);if(s->flip_switch)s->current=1;}
static int nhloAutoTicks(nhlo_auto_case *s,int value) {return s->pal?value*50/60:value;}
/* Saved native autoswitch with field/service substitution, runtime regional
 * selection and only the unused stack local removed. */
int32_t nhloAutoSwitch(nhlo_auto_case *s)
{
	nhlo_auto_hand *hand = &s->hand;
	int32_t someval;
	int32_t gunfunc = nhloAutoFunction(s);

	if (!hand->inuse && nhloAutoSet(s, 0)) {
		return s->lvupdate;
	}

	if (hand->stateminor == 0) {
		int32_t delay = nhloAutoTicks(s, 16);

		if (s->normal_multi) {
			delay = nhloAutoTicks(s, 12);
		}

		if (hand->stateframes >= delay) {
			hand->stateminor++; // to 1
		} else {
			nhloAutoPitch(s, hand->stateframes * 0.87252569198608f / delay);
		}
	}

	if (hand->stateminor == 1) {
		hand->lastdirvalid = 0;
if (!s->modern) {
		hand->animframeincfreal = hand->animframeinc;
}
		hand->shotremainder = 0;

		if (nhloAutoReady(s) && nhloAutoSet(s, 5)) {
			if (s->multi) {
				nhloAutoDelete(s);
			}

			nhloAutoFree(s);

			hand->mode = 6;
			hand->stateminor = 2;
			hand->count = 0;
			return 0;
		}

		if (hand->inuse) {
			someval = nhloAutoAmmo(s, gunfunc);

			if (s->weaponnum == 32 || s->weaponnum == 33) {
				hand->gset.weaponfunc = gunfunc;
			}

			if (s->weaponnum == 34
					&& gunfunc != hand->gset.weaponfunc
					&& nhloAutoSet(s, 7)) {
				return s->lvupdate;
			}

			if (s->autoselect) {
				nhlo_auto_other *otherhand = &s->other[s->current];
				int ready = 1;

				nhloAutoCapture(s);

				if (otherhand->inuse) {
					if (nhloAutoOtherAmmo(s, otherhand, 0) >= 0) {
						ready = 0;
					}

					if (nhloAutoOtherAmmo(s, otherhand, 1) >= 0) {
						ready = 0;
					}

					if (nhloAutoExcluded(s, otherhand, otherhand->function)) {
						ready = 1;
					}
				}

				if (otherhand->state != 0 && otherhand->state != 8) {
					ready = 0;
				}

				if (ready) {
					nhloAutoSwitchWeapon(s);
				}
			}

			if (someval <= 1 && someval >= 0) {
				if (s->other[s->current].state != 1) {
					hand->count60 = 0;
					hand->count = 0;

					if (nhloAutoSet(s, 1)) {
						if (s->weaponnum == 26) {
							hand->mode = 11;
							hand->pausetime60 = nhloAutoTicks(s, 17);
							hand->count60 = 0;
							hand->count = -1;
							hand->stateminor = 2;
						}

						return s->lvupdate;
					}
				}
			}

			if (hand->modenext) {
				hand->mode = hand->modenext;
				hand->count60 = 0;
				hand->count = 0;
				hand->modenext = 0;
			}
		}

		nhloAutoPitch(s, 0.87252569198608f);
	}

	return 0;
}


static void nhloTickEvent(nhlo_tick_case *s, int event)
{
    if (s->calls < 16) { s->events[s->calls] = event; s->players[s->calls] = s->current; }
    ++s->calls;
}
static int32_t nhloTickChild(nhlo_tick_case *s, nhlo_tick_hand *hand, int event)
{
    nhloTickEvent(s, event);
    hand->state = s->child_state;
    hand->frames = 17;
    hand->cycles = 5;
    return s->child_result;
}
/* Parent donor keeps captured hand distinct from current-player service use. */
int32_t nhloHandTick(nhlo_tick_case *s)
{
    int32_t result = 0;
    nhlo_tick_hand *hand = &s->hands[s->current];
    int32_t prevstate = hand->state;
    nhloTickEvent(s, 100);
    if (s->pending_flip) s->current = 1;
    if (s->pending) return 0;
    hand->firing = 0;
    hand->flash = 0;
    hand->frames += s->update;
    if (s->update240 > 0) { hand->count60 += s->update60; ++hand->count; }
    hand->useposrot = 0;
    switch (hand->state) {
    case 0: result = nhloTickChild(s, hand, 0); break;
    case 1: result = nhloTickChild(s, hand, 1); break;
    case 4:
        nhloTickEvent(s, 101);
        result = s->is_v2 ? nhloTickChild(s, &s->hands[s->current], 102) : nhloTickChild(s, hand, 4);
        break;
    case 2: result = 0; break;
    case 5: result = nhloTickChild(s, hand, 5); break;
    case 3: result = nhloTickChild(s, hand, 3); break;
    case 8: result = nhloTickChild(s, hand, 8); break;
    case 7: result = nhloTickChild(s, hand, 7); break;
    }
    hand->lastframe = hand->frames;
    if (hand->state != prevstate) hand->lastframe = -result;
    else { hand->frames -= result; ++hand->cycles; }
    return result;
}

static void nhloStartEvent(nhlo_start_case *s, int kind, const nhlo_start_command *cmd)
{
    if (s->count < 256) { s->events[s->count].kind = kind; s->events[s->count].command = cmd; }
    ++s->count;
}
/* Native start donor: catalog and eligibility calls replaced only by scripted
 * boundaries. Recursion remains native C, independent of the graph runner. */
void nhloAnimationStart(nhlo_start_case *s, nhlo_start_command *cmd)
{
    if (cmd == NULL) { nhloStartEvent(s, NHLO_START_LOG_NULL, cmd); return; }
    nhloStartEvent(s, NHLO_START_NAME, cmd);
    nhloStartEvent(s, NHLO_START_SOURCE, cmd);
    if (s->name_present && s->source_present) nhloStartEvent(s, NHLO_START_LOG_SOURCE, cmd);
    if (cmd->type != 10) {
        nhlo_start_command *loopcmd = cmd;
        int32_t done = 0;
        uint32_t rand = s->rng_value % 100;
        ++s->rng_calls; nhloStartEvent(s, NHLO_START_RNG, cmd);
        while (loopcmd->type != 0) {
            nhloStartEvent(s, NHLO_START_ELIGIBILITY, loopcmd);
            if (loopcmd->eligible && !done) {
                if (loopcmd->type == 6) {
                    done = 1;
                    nhloStartEvent(s, NHLO_START_CHILD, loopcmd->child);
                    nhloAnimationStart(s, loopcmd->child);
                } else if (loopcmd->type == 7) {
                    if (loopcmd->child != s->previous && loopcmd->key > rand) {
                        done = 1;
                        nhloStartEvent(s, NHLO_START_CHILD, loopcmd->child);
                        nhloAnimationStart(s, loopcmd->child);
                    }
                }
            }
            ++loopcmd;
        }
    } else {
        s->load = cmd->key; nhloStartEvent(s, NHLO_START_STORE_LOAD, cmd);
        s->mode = 0; nhloStartEvent(s, NHLO_START_STORE_MODE, cmd);
        s->latch = 0; nhloStartEvent(s, NHLO_START_STORE_LATCH, cmd);
        s->incremental = 0; nhloStartEvent(s, NHLO_START_STORE_INCREMENTAL, cmd);
        s->command = cmd; nhloStartEvent(s, NHLO_START_STORE_COMMAND, cmd);
        s->loop = 0; nhloStartEvent(s, NHLO_START_STORE_LOOP, cmd);
        s->bit02 = 0; nhloStartEvent(s, NHLO_START_STORE_BIT02, cmd);
        s->bit07 = 0; nhloStartEvent(s, NHLO_START_STORE_BIT07, cmd);
        s->previous = cmd; nhloStartEvent(s, NHLO_START_STORE_PREVIOUS, cmd);
    }
}

static float nhloWaitFrame(nhlo_wait_case *s)
{
    return s->frames[s->frame_queries++];
}
/* Independent transcription of the saved pre-conversion wait donors.
 * Only field substitution, PAL selection and boundary counters are added. */
int32_t nhloAnimationWait(nhlo_wait_case *s)
{
    const nhlo_wait_command *cmd = s->commands;
    int32_t waittimekeyframe = -1, zreleasekeyframe = -1;
    if (s->mode == 0) return s->load == -1;
    while (cmd->type != 0 && waittimekeyframe == -1) {
        if (cmd->type == 3) zreleasekeyframe = cmd->key;
        if (cmd->type == 4 && s->time == cmd->value) waittimekeyframe = cmd->key;
        ++cmd; ++s->advances;
    }
    if (waittimekeyframe >= 0) {
        if (s->pal) {
            if (s->latch && nhloWaitFrame(s) <= zreleasekeyframe) return 0;
            return nhloWaitFrame(s) + s->increment_real >= waittimekeyframe;
        }
        if (s->latch && (int32_t)nhloWaitFrame(s) <= zreleasekeyframe) return 0;
        return nhloWaitFrame(s) + s->increment >= waittimekeyframe;
    }
    return 1;
}
int32_t nhloAnimationWaitKey(nhlo_wait_case *s)
{
    const nhlo_wait_command *cmd = s->commands;
    int32_t keyframe = -1;
    if (s->mode == 0) return 0;
    while (cmd->type != 0 && keyframe == -1) {
        if (cmd->type == 4) {
            if (cmd->value == s->time) keyframe = cmd->key;
        }
        ++cmd; ++s->advances;
    }
    if (keyframe == -1) keyframe = 0;
    return keyframe;
}

static int32_t nhloFrameCount(nhlo_frame_case *s)
{
    ++s->count_queries;
    return s->frame_count;
}
static float nhloCurrentFrame(nhlo_frame_case *s)
{
    ++s->current_queries;
    return s->current_frame;
}
/* Native bgun0f09815c, transcribed before October 23 graph conversion.
 * The fixture replaces only field access and the two model query boundaries. */
float nhloAnimationFrame(nhlo_frame_case *s)
{
    if (s->animmode == NHLO_HANDANIMMODE_BUSY && s->command_present) {
        if (s->command_value < 0) {
            return nhloFrameCount(s) - nhloCurrentFrame(s);
        }
        return nhloCurrentFrame(s);
    }
    return 0;
}

#define VERSION_NTSC_1_0 NHLO_VERSION_NTSC_1_0
#define VERSION_JPN_FINAL NHLO_VERSION_JPN_FINAL
#define HANDSTATE_IDLE NHLO_HANDSTATE_IDLE
#define HANDSTATE_RELOAD NHLO_HANDSTATE_RELOAD
#define HANDSTATEMINOR_RELOAD_MAIN NHLO_HANDSTATEMINOR_RELOAD_MAIN
#define HANDSTATEMINOR_RELOAD_LOWER NHLO_HANDSTATEMINOR_RELOAD_LOWER
#define HANDSTATEMINOR_RELOAD_SOUND NHLO_HANDSTATEMINOR_RELOAD_SOUND
#define HANDSTATEMINOR_RELOAD_RAISE NHLO_HANDSTATEMINOR_RELOAD_RAISE
#define HANDSTATEMINOR_RELOAD_WAIT NHLO_HANDSTATEMINOR_RELOAD_WAIT
#define HANDSTATEFLAG_00000010 NHLO_HANDSTATEFLAG_00000010
#define HANDANIMMODE_IDLE NHLO_HANDANIMMODE_IDLE
#define HANDANIMMODE_BUSY NHLO_HANDANIMMODE_BUSY
#define HANDMODE_NONE NHLO_HANDMODE_NONE
#define HANDMODE_11 NHLO_HANDMODE_11
#define HANDMODE_12 NHLO_HANDMODE_12
#define HAND_RIGHT NHLO_HAND_RIGHT
#define FUNC_PRIMARY NHLO_FUNC_PRIMARY
#define FUNC_SECONDARY NHLO_FUNC_SECONDARY
#define AMMOFLAG_INCREMENTALRELOAD NHLO_AMMOFLAG_INCREMENTALRELOAD
#define EJECTSTATE_INACTIVE NHLO_EJECTSTATE_INACTIVE
#define DEVICE_EYESPY NHLO_DEVICE_EYESPY
#define CAMERAMODE_THIRDPERSON NHLO_CAMERAMODE_THIRDPERSON
#define GUNMEMOWNER_BONDGUN NHLO_GUNMEMOWNER_BONDGUN
#define MASTERLOADSTATE_LOADED NHLO_MASTERLOADSTATE_LOADED
#define WEAPONFLAG_00000040 NHLO_WEAPONFLAG_00000040
#define WEAPONFLAG_00000080 NHLO_WEAPONFLAG_00000080
#define SFX_RELOAD_DEFAULT NHLO_SFX_RELOAD_DEFAULT
#define MAX_PITCH NHLO_MAX_PITCH
#define WEAPON_NONE NHLO_WEAPON_NONE
#define WEAPON_UNARMED NHLO_WEAPON_UNARMED
#define WEAPON_COMBATKNIFE NHLO_WEAPON_COMBATKNIFE
#define WEAPON_LASER NHLO_WEAPON_LASER
#define WEAPON_GRENADE NHLO_WEAPON_GRENADE
#define WEAPON_NBOMB NHLO_WEAPON_NBOMB
#define WEAPON_TIMEDMINE NHLO_WEAPON_TIMEDMINE
#define WEAPON_PROXIMITYMINE NHLO_WEAPON_PROXIMITYMINE
#define WEAPON_REMOTEMINE NHLO_WEAPON_REMOTEMINE
#define WEAPON_ECMMINE NHLO_WEAPON_ECMMINE
#define WEAPON_COMMSRIDER NHLO_WEAPON_COMMSRIDER
#define WEAPON_TRACERBUG NHLO_WEAPON_TRACERBUG
#define WEAPON_TARGETAMPLIFIER NHLO_WEAPON_TARGETAMPLIFIER

#define NHLO_TRUE 1
#define NHLO_FALSE 0

#define g_Vars (o->vars)
#define g_PlayerInvincible (o->playerinvincible)
#define var80095200 (o->var80095200)
#define TICKS(val) (o->pal ? ((val) * 50 / 60) : (val))

#define NHLO_STORED(field, object, lvalue) nhloLogStore(o, (field), (object), (uint32_t)(lvalue))
#define NHLO_STORED_F32(field, object, lvalue) nhloLogStore(o, (field), (object), nhloFloatBits(lvalue))

uint32_t nhloFloatBits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

float nhloBitsFloat(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

int32_t nhloWeaponBriefcase2(int32_t version)
{
    /* WEAPON_SHIELDTECHITEM (0x47) exists only when VERSION >= VERSION_NTSC_1_0,
     * so WEAPON_BRIEFCASE2 is 0x4f there and 0x4e in NTSC beta. */
    return version >= NHLO_VERSION_NTSC_1_0 ? 0x4f : 0x4e;
}

void nhloInit(nhlo_oracle *o)
{
    memset(o, 0, sizeof *o);
    o->version = NHLO_VERSION_NTSC_FINAL;
    o->pal = 0;
}

void nhloClearLog(nhlo_oracle *o)
{
    o->count = 0;
    o->dropped = 0;
}

const char *nhloEventKindName(nhlo_event_kind kind)
{
    switch (kind) {
    case NHLO_CALL_GSET_GET_WEAPON_FUNCTION: return "gsetGetWeaponFunction";
    case NHLO_CALL_BGUN_SET_STATE: return "bgunSetState";
    case NHLO_CALL_BGUN_START_ANIMATION: return "bgunStartAnimation";
    case NHLO_CALL_BGUN_0F098A44: return "bgun0f098a44";
    case NHLO_CALL_BGUN_0F098DF8: return "bgun0f098df8";
    case NHLO_CALL_BGUN_0F098CA0: return "bgun0f098ca0";
    case NHLO_CALL_BGUN_SET_ARM_PITCH: return "bgunSetArmPitch";
    case NHLO_CALL_BGUN_IS_LOADED: return "bgunIsLoaded";
    case NHLO_CALL_SND_START: return "sndStart";
    case NHLO_CALL_WEAPON_GET_FILE_NUM2: return "weaponGetFileNum2";
    case NHLO_CALL_WEAPON_HAS_FLAG: return "weaponHasFlag";
    case NHLO_CALL_GSET_GET_PRI_TO_SEC_ANIM: return "gsetGetPriToSecAnim";
    case NHLO_CALL_GSET_GET_SEC_TO_PRI_ANIM: return "gsetGetSecToPriAnim";
    case NHLO_CALL_MTX4_LOAD_X_ROTATION: return "mtx4LoadXRotation";
    case NHLO_CALL_COSF: return "cosf";
    case NHLO_CALL_SINF: return "sinf";
    case NHLO_EVENT_SELECT_OTHERHAND: return "selectOtherhand";
    case NHLO_EVENT_STORE: return "store";
    default: return "unknown";
    }
}

static int nhloMtxEqual(const nhlo_mtxf *a, const nhlo_mtxf *b)
{
    int r, c;
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            if (nhloFloatBits(a->m[r][c]) != nhloFloatBits(b->m[r][c])) {
                return 0;
            }
        }
    }
    return 1;
}

int nhloHandEqual(const struct nhlo_hand *a, const struct nhlo_hand *b)
{
    return a->gset.weaponnum == b->gset.weaponnum
        && a->gset.unk0639 == b->gset.unk0639
        && a->gset.unk063a == b->gset.unk063a
        && a->gset.weaponfunc == b->gset.weaponfunc
        && a->visible == b->visible
        && a->triggeron == b->triggeron
        && a->count == b->count
        && a->count60 == b->count60
        && a->mode == b->mode
        && a->pausetime60 == b->pausetime60
        && nhloMtxEqual(&a->posrotmtx, &b->posrotmtx)
        && a->useposrot == b->useposrot
        && a->state == b->state
        && a->stateminor == b->stateminor
        && a->stateflags == b->stateflags
        && a->stateframes == b->stateframes
        && a->statecycles == b->statecycles
        && a->statelastframe == b->statelastframe
        && a->gs_int1 == b->gs_int1
        && a->gs_int2 == b->gs_int2
        && a->animload == b->animload
        && a->animmode == b->animmode
        && a->unk0cc8_01 == b->unk0cc8_01
        && a->unk0cc8_02 == b->unk0cc8_02
        && a->incrementalreloading == b->incrementalreloading
        && a->unk0cc8_04 == b->unk0cc8_04
        && a->unk0cc8_07 == b->unk0cc8_07
        && a->unk0cc8_08 == b->unk0cc8_08
        && a->ejectstate == b->ejectstate
        && a->ejecttype == b->ejecttype
        && a->unk0d0e_07 == b->unk0d0e_07
        && a->unk0dd4 == b->unk0dd4;
}

int nhloPlayerEqual(const struct nhlo_player *a, const struct nhlo_player *b)
{
    return a->cameramode == b->cameramode
        && a->devicesactive == b->devicesactive
        && a->isdead == b->isdead
        && nhloHandEqual(&a->hands[0], &b->hands[0])
        && nhloHandEqual(&a->hands[1], &b->hands[1])
        && a->gunctrl.gunmemnew == b->gunctrl.gunmemnew
        && a->gunctrl.gunmemtype == b->gunctrl.gunmemtype
        && a->gunctrl.masterloadstate == b->gunctrl.masterloadstate
        && a->gunctrl.gunmemowner == b->gunctrl.gunmemowner
        && a->doautoselect == b->doautoselect
        && a->devicesinhibit == b->devicesinhibit;
}

static nhlo_event *nhloAppend(nhlo_oracle *o, nhlo_event_kind kind)
{
    nhlo_event *event;
    if (o->count < NHLO_EVENT_CAPACITY) {
        event = &o->events[o->count++];
    } else {
        event = &o->scratch;
        o->dropped++;
    }
    memset(event, 0, sizeof *event);
    event->kind = kind;
    return event;
}

static nhlo_result nhloRespond(nhlo_oracle *o, nhlo_event *event)
{
    event->result = o->response[event->kind];
    if (o->hook) {
        o->hook(o, event, o->hook_user);
    }
    return event->result;
}

static void nhloLogStore(nhlo_oracle *o, nhlo_field field, const void *object, uint32_t bits)
{
    nhlo_event *event = nhloAppend(o, NHLO_EVENT_STORE);
    event->field = field;
    event->object = object;
    event->stored_bits = bits;
}

static void nhloLogOtherHand(nhlo_oracle *o, const struct nhlo_hand *otherhand, int32_t handnum)
{
    nhlo_event *event = nhloAppend(o, NHLO_EVENT_SELECT_OTHERHAND);
    event->p[0] = otherhand;
    event->p[1] = o->vars.currentplayer;
    event->i[0] = handnum;
}

static struct nhlo_weaponfunc *nhloStub_gsetGetWeaponFunction(nhlo_oracle *o, struct nhlo_gset *gset)
{
    nhlo_event *event = nhloAppend(o, NHLO_CALL_GSET_GET_WEAPON_FUNCTION);
    event->p[0] = gset;
    return (struct nhlo_weaponfunc *)nhloRespond(o, event).p;
}

static int32_t nhloStub_bgunSetState(nhlo_oracle *o, int32_t handnum, int32_t state)
{
    nhlo_event *event = nhloAppend(o, NHLO_CALL_BGUN_SET_STATE);
    event->i[0] = handnum;
    event->i[1] = state;
    return nhloRespond(o, event).i;
}

static void nhloStub_bgunStartAnimation(nhlo_oracle *o, struct nhlo_guncmd *cmd, int32_t handnum, struct nhlo_hand *hand)
{
    nhlo_event *event = nhloAppend(o, NHLO_CALL_BGUN_START_ANIMATION);
    event->p[0] = cmd;
    event->i[0] = handnum;
    event->p[1] = hand;
    (void)nhloRespond(o, event);
}

static int32_t nhloStub_bgun0f098a44(nhlo_oracle *o, struct nhlo_hand *hand, int32_t time)
{
    nhlo_event *event = nhloAppend(o, NHLO_CALL_BGUN_0F098A44);
    event->p[0] = hand;
    event->i[0] = time;
    return nhloRespond(o, event).i;
}

static void nhloStub_bgun0f098df8(nhlo_oracle *o, int32_t weaponfunc, struct nhlo_handweaponinfo *info,
        struct nhlo_hand *hand, uint8_t onebullet, uint8_t checkunequipped)
{
    nhlo_event *event = nhloAppend(o, NHLO_CALL_BGUN_0F098DF8);
    event->i[0] = weaponfunc;
    event->p[0] = info;
    event->p[1] = hand;
    event->u[0] = onebullet;
    event->u[1] = checkunequipped;
    (void)nhloRespond(o, event);
}

static int32_t nhloStub_bgun0f098ca0(nhlo_oracle *o, int32_t arg0, struct nhlo_handweaponinfo *info, struct nhlo_hand *hand)
{
    nhlo_event *event = nhloAppend(o, NHLO_CALL_BGUN_0F098CA0);
    event->i[0] = arg0;
    event->p[0] = info;
    event->p[1] = hand;
    return nhloRespond(o, event).i;
}

static void nhloStub_bgunSetArmPitch(nhlo_oracle *o, struct nhlo_hand *hand, float angle)
{
    nhlo_event *event = nhloAppend(o, NHLO_CALL_BGUN_SET_ARM_PITCH);
    event->p[0] = hand;
    event->f = angle;
    event->f_bits = nhloFloatBits(angle);
    (void)nhloRespond(o, event);
    if (o->nest_arm_pitch) {
        nhloSetArmPitch(o, hand, angle);
    }
}

static int32_t nhloStub_bgunIsLoaded(nhlo_oracle *o)
{
    nhlo_event *event = nhloAppend(o, NHLO_CALL_BGUN_IS_LOADED);
    int32_t result = nhloRespond(o, event).i;
    if (o->nest_is_loaded) {
        result = nhloIsLoaded(o);
    }
    return result;
}

static struct nhlo_sndstate *nhloStub_sndStart(nhlo_oracle *o, int32_t arg0, int16_t sound,
        struct nhlo_sndstate **handle, int32_t volume, int32_t pan, float pitch, int32_t fxbus, int32_t fxmix)
{
    nhlo_event *event = nhloAppend(o, NHLO_CALL_SND_START);
    event->i[0] = arg0;
    event->i[1] = sound;
    event->p[0] = handle;
    event->i[2] = volume;
    event->i[3] = pan;
    event->f = pitch;
    event->f_bits = nhloFloatBits(pitch);
    event->i[4] = fxbus;
    event->i[5] = fxmix;
    return (struct nhlo_sndstate *)nhloRespond(o, event).p;
}

static uint16_t nhloStub_weaponGetFileNum2(nhlo_oracle *o, int32_t weaponnum)
{
    nhlo_event *event = nhloAppend(o, NHLO_CALL_WEAPON_GET_FILE_NUM2);
    event->i[0] = weaponnum;
    return (uint16_t)nhloRespond(o, event).i;
}

static int32_t nhloStub_weaponHasFlag(nhlo_oracle *o, int32_t itemid, uint32_t flag)
{
    nhlo_event *event = nhloAppend(o, NHLO_CALL_WEAPON_HAS_FLAG);
    event->i[0] = itemid;
    event->u[0] = flag;
    return nhloRespond(o, event).i;
}

static struct nhlo_guncmd *nhloStub_gsetGetPriToSecAnim(nhlo_oracle *o, struct nhlo_gset *gset)
{
    nhlo_event *event = nhloAppend(o, NHLO_CALL_GSET_GET_PRI_TO_SEC_ANIM);
    event->p[0] = gset;
    return (struct nhlo_guncmd *)nhloRespond(o, event).p;
}

static struct nhlo_guncmd *nhloStub_gsetGetSecToPriAnim(nhlo_oracle *o, struct nhlo_gset *gset)
{
    nhlo_event *event = nhloAppend(o, NHLO_CALL_GSET_GET_SEC_TO_PRI_ANIM);
    event->p[0] = gset;
    return (struct nhlo_guncmd *)nhloRespond(o, event).p;
}

static void nhloStub_mtx4LoadXRotation(nhlo_oracle *o, float radians, nhlo_mtxf *mtx)
{
    nhlo_event *event = nhloAppend(o, NHLO_CALL_MTX4_LOAD_X_ROTATION);
    event->f = radians;
    event->f_bits = nhloFloatBits(radians);
    event->p[0] = mtx;
    (void)nhloRespond(o, event);
}

static float nhloStub_cosf(nhlo_oracle *o, float x)
{
    nhlo_event *event = nhloAppend(o, NHLO_CALL_COSF);
    event->f = x;
    event->f_bits = nhloFloatBits(x);
    return nhloRespond(o, event).f;
}

static float nhloStub_sinf(nhlo_oracle *o, float x)
{
    nhlo_event *event = nhloAppend(o, NHLO_CALL_SINF);
    event->f = x;
    event->f_bits = nhloFloatBits(x);
    return nhloRespond(o, event).f;
}

void nhloSetArmPitch(nhlo_oracle *o, struct nhlo_hand *hand, float angle)
{
    hand->useposrot = NHLO_TRUE;
    NHLO_STORED(NHLO_FIELD_USEPOSROT, hand, hand->useposrot);

    nhloStub_mtx4LoadXRotation(o, angle, &hand->posrotmtx);

    hand->posrotmtx.m[3][0] = 0;
    NHLO_STORED_F32(NHLO_FIELD_POSROTMTX_M30, hand, hand->posrotmtx.m[3][0]);
    hand->posrotmtx.m[3][1] = (1.0f - nhloStub_cosf(o, angle)) * -80.0f;
    NHLO_STORED_F32(NHLO_FIELD_POSROTMTX_M31, hand, hand->posrotmtx.m[3][1]);
    hand->posrotmtx.m[3][2] = nhloStub_sinf(o, angle) * 15.0f;
    NHLO_STORED_F32(NHLO_FIELD_POSROTMTX_M32, hand, hand->posrotmtx.m[3][2]);
}

int32_t nhloIsReloading(nhlo_oracle *o, struct nhlo_hand *hand)
{
    (void)o;

    if (hand->state == HANDSTATE_RELOAD) {
        return NHLO_TRUE;
    }

    return NHLO_FALSE;
}

int32_t nhloTickIncReload(nhlo_oracle *o, struct nhlo_handweaponinfo *info, int32_t handnum,
        struct nhlo_hand *hand, int32_t lvupdate)
{
    struct nhlo_weaponfunc *func = nhloStub_gsetGetWeaponFunction(o, &hand->gset);

    if (g_Vars.currentplayer->isdead) {
        hand->animmode = HANDANIMMODE_IDLE;
        NHLO_STORED(NHLO_FIELD_ANIMMODE, hand, hand->animmode);
        hand->animload = -1;
        NHLO_STORED(NHLO_FIELD_ANIMLOAD, hand, hand->animload);

        if (nhloStub_bgunSetState(o, handnum, HANDSTATE_IDLE)) {
            return lvupdate;
        }
    }

    if (hand->statecycles == 0) {
        struct nhlo_hand *otherhand = &g_Vars.currentplayer->hands[1 - handnum];
        nhloLogOtherHand(o, otherhand, handnum);

        hand->gs_int1 = -1;
        NHLO_STORED(NHLO_FIELD_GS_INT1, hand, hand->gs_int1);
        hand->gs_int2 = 0;
        NHLO_STORED(NHLO_FIELD_GS_INT2, hand, hand->gs_int2);

        if (otherhand->state == HANDSTATE_RELOAD && otherhand->stateframes < TICKS(20)) {
            hand->stateminor = HANDSTATEMINOR_RELOAD_WAIT;
            NHLO_STORED(NHLO_FIELD_STATEMINOR, hand, hand->stateminor);
        }
    }

    if (hand->stateminor == HANDSTATEMINOR_RELOAD_WAIT) {
        struct nhlo_hand *otherhand = &g_Vars.currentplayer->hands[1 - handnum];
        nhloLogOtherHand(o, otherhand, handnum);

        if (otherhand->state == HANDSTATE_RELOAD && otherhand->stateframes < TICKS(20)) {
            return 0;
        }

        hand->stateframes = 0;
        NHLO_STORED(NHLO_FIELD_STATEFRAMES, hand, hand->stateframes);
        hand->statecycles = 0;
        NHLO_STORED(NHLO_FIELD_STATECYCLES, hand, hand->statecycles);
        hand->stateminor = HANDSTATEMINOR_RELOAD_MAIN;
        NHLO_STORED(NHLO_FIELD_STATEMINOR, hand, hand->stateminor);
        hand->statelastframe = 0;
        NHLO_STORED(NHLO_FIELD_STATELASTFRAME, hand, hand->statelastframe);
    }

    if (hand->stateminor == HANDSTATEMINOR_RELOAD_MAIN) {
        if (hand->statecycles == 0) {
            if (func && (func->ammoindex == 0 || func->ammoindex == 1)) {
                if (info->definition->ammos[func->ammoindex]->reload_animation
                        && info->weaponnum != WEAPON_COMBATKNIFE) {
                    nhloStub_bgunStartAnimation(o, info->definition->ammos[func->ammoindex]->reload_animation, handnum, hand);

                    hand->unk0d0e_07 = NHLO_TRUE;
                    NHLO_STORED(NHLO_FIELD_UNK0D0E_07, hand, hand->unk0d0e_07);

                    if (info->definition->ammos[func->ammoindex]->flags & AMMOFLAG_INCREMENTALRELOAD) {
                        hand->incrementalreloading = NHLO_TRUE;
                        NHLO_STORED(NHLO_FIELD_INCREMENTALRELOADING, hand, hand->incrementalreloading);
                    }

                    if (info->weaponnum == WEAPON_GRENADE || info->weaponnum == WEAPON_NBOMB) {
                        hand->ejectstate = EJECTSTATE_INACTIVE;
                        NHLO_STORED(NHLO_FIELD_EJECTSTATE, hand, hand->ejectstate);
                    }
                } else {
                    hand->stateminor++; /* to HANDSTATEMINOR_RELOAD_LOWER */
                    NHLO_STORED(NHLO_FIELD_STATEMINOR, hand, hand->stateminor);
                }
            } else {
                if (nhloStub_bgunSetState(o, handnum, HANDSTATE_IDLE)) {
                    return lvupdate;
                }
            }
        } else {
            if (info->definition->ammos[func->ammoindex]->flags & AMMOFLAG_INCREMENTALRELOAD) {
                if (nhloStub_bgun0f098a44(o, hand, 1)) {
                    if ((hand->stateflags & HANDSTATEFLAG_00000010) == 0) {
                        int32_t value;

                        nhloStub_bgun0f098df8(o, hand->gset.weaponfunc, info, hand, 1, 0);
                        hand->stateflags |= HANDSTATEFLAG_00000010;
                        NHLO_STORED(NHLO_FIELD_STATEFLAGS, hand, hand->stateflags);
                        value = nhloStub_bgun0f098ca0(o, hand->gset.weaponfunc, info, hand);

                        if (value >= 2) {
                            hand->incrementalreloading = NHLO_FALSE;
                            NHLO_STORED(NHLO_FIELD_INCREMENTALRELOADING, hand, hand->incrementalreloading);
                        }

                        if (value == -1) {
                            hand->incrementalreloading = NHLO_FALSE;
                            NHLO_STORED(NHLO_FIELD_INCREMENTALRELOADING, hand, hand->incrementalreloading);
                        }
                    }
                } else {
                    hand->stateflags = 0;
                    NHLO_STORED(NHLO_FIELD_STATEFLAGS, hand, hand->stateflags);
                }

                if (hand->triggeron) {
                    hand->incrementalreloading = NHLO_FALSE;
                    NHLO_STORED(NHLO_FIELD_INCREMENTALRELOADING, hand, hand->incrementalreloading);
                }

                if (o->version >= VERSION_JPN_FINAL) { /* T7: #if VERSION >= VERSION_JPN_FINAL */
                    if (g_Vars.currentplayer->devicesactive & ~g_Vars.currentplayer->devicesinhibit & DEVICE_EYESPY) {
                        hand->incrementalreloading = NHLO_FALSE;
                        NHLO_STORED(NHLO_FIELD_INCREMENTALRELOADING, hand, hand->incrementalreloading);
                    }
                }
            } else {
                if ((hand->stateflags & HANDSTATEFLAG_00000010) == 0) {
                    if (nhloStub_bgun0f098a44(o, hand, 1)) {
                        nhloStub_bgun0f098df8(o, hand->gset.weaponfunc, info, hand, 0, 0);
                        hand->stateflags |= HANDSTATEFLAG_00000010;
                        NHLO_STORED(NHLO_FIELD_STATEFLAGS, hand, hand->stateflags);
                    }
                }
            }

            if (hand->animmode != HANDANIMMODE_BUSY) {
                if (nhloStub_bgunSetState(o, handnum, HANDSTATE_IDLE)) {
                    return lvupdate;
                }
            }

            if (1);
        }
    }

    if (hand->stateminor == HANDSTATEMINOR_RELOAD_LOWER) {
        if (hand->count60 > TICKS(15) || !hand->visible) {
            hand->mode = HANDMODE_11;
            NHLO_STORED(NHLO_FIELD_MODE, hand, hand->mode);
            hand->stateminor++; /* to HANDSTATEMINOR_RELOAD_SOUND */
            NHLO_STORED(NHLO_FIELD_STATEMINOR, hand, hand->stateminor);
            hand->pausetime60 = TICKS(17);
            NHLO_STORED(NHLO_FIELD_PAUSETIME60, hand, hand->pausetime60);
            hand->count60 = 0;
            NHLO_STORED(NHLO_FIELD_COUNT60, hand, hand->count60);
            hand->count = 0;
            NHLO_STORED(NHLO_FIELD_COUNT, hand, hand->count);
        } else {
            nhloStub_bgunSetArmPitch(o, hand, hand->count60 * MAX_PITCH / TICKS(16));
        }
    }

    if (hand->stateminor == HANDSTATEMINOR_RELOAD_SOUND) {
        if (hand->count == 0) {
            if (info->weaponnum == WEAPON_COMBATKNIFE
                    && func->ammoindex >= 0
                    && info->definition->ammos[func->ammoindex]->reload_animation) {
                nhloStub_bgunStartAnimation(o, info->definition->ammos[func->ammoindex]->reload_animation, handnum, hand);
                hand->unk0cc8_02 = NHLO_TRUE;
                NHLO_STORED(NHLO_FIELD_UNK0CC8_02, hand, hand->unk0cc8_02);
            }

            if ((hand->stateflags & HANDSTATEFLAG_00000010) == 0) {
                nhloStub_bgun0f098df8(o, hand->gset.weaponfunc, info, hand, 0, 0);
            }

            if (g_Vars.lvupdate240 > 0
                    && g_Vars.currentplayer->cameramode != CAMERAMODE_THIRDPERSON
                    && nhloStub_bgunIsLoaded(o)
                    && !g_PlayerInvincible
                    && !g_Vars.currentplayer->isdead) {
                const int32_t nhlo_weaponnum = info->weaponnum; /* T9 */

                switch (nhlo_weaponnum) {
                case WEAPON_NONE:
                case WEAPON_UNARMED:
                case WEAPON_COMBATKNIFE:
                case WEAPON_LASER:
                case WEAPON_GRENADE:
                case WEAPON_TIMEDMINE:
                case WEAPON_PROXIMITYMINE:
                case WEAPON_REMOTEMINE:
                case WEAPON_ECMMINE:
                case WEAPON_COMMSRIDER:
                case WEAPON_TRACERBUG:
                case WEAPON_TARGETAMPLIFIER:
                    /* No reload sound */
                    break;
                default:
                    if (nhlo_weaponnum == nhloWeaponBriefcase2(o->version)) {
                        /* case WEAPON_BRIEFCASE2: No reload sound */
                        break;
                    }
                    nhloStub_sndStart(o, var80095200, SFX_RELOAD_DEFAULT, 0, -1, -1, -1, -1, -1);
                    break;
                }
            }
        }

        if (hand->count60 >= hand->pausetime60 && hand->count >= 2) {
            hand->mode = HANDMODE_12;
            NHLO_STORED(NHLO_FIELD_MODE, hand, hand->mode);
            hand->stateminor++; /* to HANDSTATEMINOR_RELOAD_RAISE */
            NHLO_STORED(NHLO_FIELD_STATEMINOR, hand, hand->stateminor);
            hand->count60 = 0;
            NHLO_STORED(NHLO_FIELD_COUNT60, hand, hand->count60);
            hand->count = 0;
            NHLO_STORED(NHLO_FIELD_COUNT, hand, hand->count);
        } else {
            nhloStub_bgunSetArmPitch(o, hand, MAX_PITCH);
        }
    }

    if (hand->stateminor == HANDSTATEMINOR_RELOAD_RAISE) {
        if (info->weaponnum == WEAPON_COMBATKNIFE) {
            hand->animmode = HANDANIMMODE_IDLE;
            NHLO_STORED(NHLO_FIELD_ANIMMODE, hand, hand->animmode);
        }

        if (hand->count == 0) {
            g_Vars.currentplayer->doautoselect = NHLO_FALSE;
            NHLO_STORED(NHLO_FIELD_DOAUTOSELECT, g_Vars.currentplayer, g_Vars.currentplayer->doautoselect);
        }

        if (hand->count60 >= TICKS(23)
                || !nhloStub_weaponGetFileNum2(o, info->weaponnum)
                || !nhloStub_weaponHasFlag(o, info->weaponnum, WEAPONFLAG_00000040)
                || nhloStub_weaponHasFlag(o, info->weaponnum, WEAPONFLAG_00000080)) {
            hand->mode = HANDMODE_NONE;
            NHLO_STORED(NHLO_FIELD_MODE, hand, hand->mode);
            hand->count60 = 0;
            NHLO_STORED(NHLO_FIELD_COUNT60, hand, hand->count60);
            hand->count = 0;
            NHLO_STORED(NHLO_FIELD_COUNT, hand, hand->count);

            if (nhloStub_bgunSetState(o, handnum, HANDSTATE_IDLE)) {
                return lvupdate;
            }
        } else {
            nhloStub_bgunSetArmPitch(o, hand, (TICKS(23) - hand->count60) * MAX_PITCH / TICKS(23));
        }
    }

    return 0;
}

int32_t nhloTickIncChangeFunc(nhlo_oracle *o, struct nhlo_handweaponinfo *info, int32_t handnum,
        struct nhlo_hand *hand, int32_t lvupdate)
{
    struct nhlo_guncmd *cmd;
    int32_t more = NHLO_FALSE;

    (void)info;

    if (hand->statecycles == 0) {
        if (hand->gset.weaponfunc == FUNC_PRIMARY) {
            cmd = nhloStub_gsetGetPriToSecAnim(o, &hand->gset);
            hand->gset.weaponfunc = FUNC_SECONDARY;
            NHLO_STORED(NHLO_FIELD_WEAPONFUNC, hand, hand->gset.weaponfunc);
        } else {
            cmd = nhloStub_gsetGetSecToPriAnim(o, &hand->gset);
            hand->gset.weaponfunc = FUNC_PRIMARY;
            NHLO_STORED(NHLO_FIELD_WEAPONFUNC, hand, hand->gset.weaponfunc);
        }

        more = NHLO_FALSE;

        if (cmd != NULL) {
            nhloStub_bgunStartAnimation(o, cmd, handnum, hand);
            more = NHLO_TRUE;
            g_Vars.currentplayer->hands[HAND_RIGHT].unk0dd4 = -1;
            NHLO_STORED(NHLO_FIELD_UNK0DD4, &g_Vars.currentplayer->hands[HAND_RIGHT],
                    g_Vars.currentplayer->hands[HAND_RIGHT].unk0dd4);
        }
    } else {
        if (hand->animmode == HANDANIMMODE_BUSY) {
            more = NHLO_TRUE;
        }
    }

    if (!more && nhloStub_bgunSetState(o, handnum, HANDSTATE_IDLE)) {
        return lvupdate;
    }

    return 0;
}

int32_t nhloIsLoaded(nhlo_oracle *o)
{
    if (g_Vars.currentplayer->gunctrl.gunmemowner != GUNMEMOWNER_BONDGUN) {
        return NHLO_FALSE;
    }

    return g_Vars.currentplayer->gunctrl.gunmemtype == WEAPON_NONE
        || (g_Vars.currentplayer->gunctrl.gunmemnew < 0
                && g_Vars.currentplayer->gunctrl.masterloadstate == MASTERLOADSTATE_LOADED);
}
