/* Independent native C oracle. Complete original bodies are mechanically
 * derived from protected-original-bondgun.c; receipt records instrumentation.
 * Prefix entry points stay frozen for historical prefix fixtures. */
#include "native_shooting_flow_oracle.h"
#include <stdint.h>
#include <string.h>
typedef int32_t s32; typedef uint32_t u32; typedef float f32;
typedef int8_t s8; typedef s32 bool; typedef s32 OSPri;
enum { false=0, true=1 };
#ifndef VERSION_NTSC_1_0
#define VERSION_NTSC_1_0 1
#endif
#ifndef VERSION
#define VERSION 2
#endif
enum { FUNCFLAG_BURST3=2, FUNCFLAG_BURST2=0x1000,
    FUNCFLAG_BURST5=0x20000, FUNCFLAG_BURST50=0x20,
    FUNCFLAG_NOMUZZLEFLASH=0x2000, HANDSTATEFLAG_00000010=0x10,
    HANDSTATEFLAG_00000020=0x20, WEAPON_SHOTGUN=1, WEAPON_LASER=2,
    WEAPON_MAULER=3, INVENTORYFUNCTYPE_SHOOT=1,
    INVENTORYFUNCTYPE_SHOOT_SINGLE=1, INVENTORYFUNCTYPE_NONE=0,
    HANDATTACKTYPE_SHOOT=1, HANDATTACKTYPE_SHOOTPROJECTILE=2,
    AL_STOPPED=0, AL_SNDP_PITCH_EVT=0x10 };
struct sndstate { s32 token, state; };
struct gset { s32 weaponnum; };
struct hand { struct gset gset; s32 burstbullets,stateframes,shotstotake;
    u32 stateflags; s32 loadedammo[2]; f32 gs_float1,shotremainder;
    bool triggeron; s8 firing,flashon; f32 loadslide,matmot1;
    s32 statevar1,attacktype,allowshootframe,lastshootframe60;
    bool slideinc; struct sndstate *audiohandle2,*audiohandle3; };
struct gunctrl { s32 ammotypes[2]; };
struct player { struct gunctrl gunctrl; bool insightaimmode; struct hand hands[2]; };
struct weaponfunc { s32 type; s8 ammoindex; u32 flags; };
struct weaponfunc_shootauto { struct weaponfunc base; f32 initialrpm,maxrpm; s8 turretaccel,turretdecel; };
struct handweaponinfo { s32 weaponnum; };
typedef struct { u32 flags; bool has_max_rpm,has_initial_rpm,has_turret_accel,has_turret_decel;
    s32 turret_accel,turret_decel; f32 initial_rpm,max_rpm;
    s32 ammo_slot; bool has_function_type_id; s32 function_type_id; } weapon_graph_held_function_t;
static struct { struct player *currentplayer; s32 lvframe60; } g_Vars;
static struct { s32 thread; } g_AudioManager;
static void *var80095200;
static s32 g_BgunGeMuzzleFlashes;
static native_shooting_case *oracle_case;
static struct hand *oracle_hand;
static weapon_graph_held_function_t oracle_graph;
static struct sndstate oracle_audio[NATIVE_SHOOTING_AUDIO_LIMIT];
static int oracle_uses_ammo, oracle_full;
enum { LOG_WARNING=1 };
static void sysLogPrintf(s32 level,const char *message,...) { (void)level;(void)message;oracle_case->program_refused=1; }
static u32 oracle_float_bits(f32 value) { u32 bits;memcpy(&bits,&value,sizeof bits);return bits; }
static s32 oracle_int_bits(u32 value) { s32 bits;memcpy(&bits,&value,sizeof bits);return bits; }

void nativeShootingResetTrace(native_shooting_case *c) { c->trace_count=0;c->trace_overflow=0; }
void nativeShootingRecordEvent(native_shooting_case *s,s32 kind,s32 operation,s32 subject,s32 a,s32 b,s32 c,s32 result) {
    native_shooting_event *e;
    if(s->trace_count<0 || s->trace_count>=NATIVE_SHOOTING_TRACE_LIMIT){s->trace_overflow=1;return;}
    e=&s->trace[s->trace_count++];memset(e,0,sizeof *e);
    e->kind=kind;e->operation=operation;e->subject=subject;e->a=a;e->b=b;e->c=c;e->result=result;
    e->stateframes=s->stateframes;e->shots=s->shots;e->burstbullets=s->burstbullets;
    e->loadedammo=s->loadedammo;e->loadedammo1=s->loadedammo1;e->statevar1=s->statevar1;
    e->attacktype=s->attacktype;e->allowshootframe=s->allowshootframe;e->lastshootframe=s->lastshootframe;
    e->stateflags=s->stateflags;e->spool_bits=oracle_float_bits(s->spool);
    e->remainder_bits=oracle_float_bits(s->remainder);e->loadslide_bits=oracle_float_bits(s->loadslide);
    e->firing=s->firing;e->flashon=s->flashon;e->audio2=s->audio2;e->audio3=s->audio3;e->priority=s->priority;
    memcpy(e->current_hand_slideinc,s->current_hand_slideinc,sizeof e->current_hand_slideinc);
    memcpy(e->current_hand_lastshootframe,s->current_hand_lastshootframe,sizeof e->current_hand_lastshootframe);
    memcpy(e->audio_state,s->audio_state,sizeof e->audio_state);
}
static struct sndstate *oracle_audio_ref(s32 token) {
    if(token==0)return 0;
    if(token<0 || token>NATIVE_SHOOTING_AUDIO_LIMIT){oracle_case->trace_overflow=1;return 0;}
    return &oracle_audio[token-1];
}
static s32 oracle_token(const struct sndstate *state) { return state?state->token:0; }
static void exportFullCase(void) {
    native_shooting_case *c=oracle_case;struct hand *h=oracle_hand;s32 i;
    c->stateframes=h->stateframes;c->shots=h->shotstotake;c->spool=h->gs_float1;
    c->remainder=h->shotremainder;c->firing=h->firing;c->burstbullets=h->burstbullets;
    c->stateflags=h->stateflags;c->loadedammo=h->loadedammo[0];c->loadedammo1=h->loadedammo[1];
    c->flashon=h->flashon;c->loadslide=h->loadslide;c->matmot=h->matmot1;
    c->statevar1=h->statevar1;c->attacktype=h->attacktype;c->allowshootframe=h->allowshootframe;
    c->lastshootframe=h->lastshootframe60;c->audio2=oracle_token(h->audiohandle2);c->audio3=oracle_token(h->audiohandle3);
    for(i=0;i<2;i++){
        c->current_hand_slideinc[i]=g_Vars.currentplayer->hands[i].slideinc;
        c->current_hand_lastshootframe[i]=g_Vars.currentplayer->hands[i].lastshootframe60;
    }
    c->other_lastshootframe=g_Vars.currentplayer->hands[1-c->handnum].lastshootframe60;
    for(i=0;i<NATIVE_SHOOTING_AUDIO_LIMIT;i++)c->audio_state[i]=oracle_audio[i].state;
}
static void oracle_service(s32 service,s32 subject,s32 a,s32 b,s32 c,s32 result) {
    if(!oracle_full)return;
    exportFullCase();nativeShootingRecordEvent(oracle_case,NS_EVENT_SERVICE,service,subject,a,b,c,result);
}
static void oracle_record_store(s32 field,struct hand *hand) {
    (void)hand;if(!oracle_full)return;
    exportFullCase();nativeShootingRecordEvent(oracle_case,NS_EVENT_WRITE,field,-1,0,0,0,0);
}
static const weapon_graph_held_function_t *bgunGetHeldGraph(const struct gset *g) {
    (void)g;oracle_service(NS_RESOLVE_GRAPH,-1,0,0,0,oracle_case->graph_present!=0);
    return oracle_case->graph_present?&oracle_graph:0;
}
static s32 bgunGetAmmoIndexFromGraph(struct weaponfunc *f,const weapon_graph_held_function_t *g) {
    if(!oracle_full)return oracle_case->ammoindex;
    return g&&g->ammo_slot>=0?g->ammo_slot:(f?f->ammoindex:-1);
}
static s32 bgunGetFunctionTypeFromGraph(struct weaponfunc *f,const weapon_graph_held_function_t *g) {
    if(!oracle_full)return oracle_case->functype;
    return g&&g->has_function_type_id?g->function_type_id:(f?f->type:INVENTORYFUNCTYPE_NONE);
}
static f32 oracleClock(void) {
    oracle_case->clock_reads++;oracle_service(NS_CLOCK,-1,0,0,0,oracle_int_bits(oracle_float_bits(oracle_case->clock)));
    return oracle_case->clock;
}
#define LVUPDATE60FREAL() oracleClock()
#define TICKS(value) ((void)(value),oracle_case->shotgundelay)
static void mainOverrideVariable(const char *name,u32 *value) {
    u32 before=*value;bool on=name[1]=='o'&&name[2]=='n';oracle_case->override_calls++;
    if(oracle_full){
        if(on&&oracle_case->override_on_enabled)*value=oracle_case->override_on_value;
        if(!on&&oracle_case->override_off_enabled)*value=oracle_case->override_off_value;
        if(on)oracle_case->rontime_state=*value;else oracle_case->rofftime_state=*value;
        oracle_service(on?NS_OVERRIDE_ON:NS_OVERRIDE_OFF,-1,oracle_int_bits(before),0,0,oracle_int_bits(*value));
    }
}
static void bgunStartSlide(s32 handnum) {
    g_Vars.currentplayer->hands[handnum].slideinc=true;
    oracle_service(NS_START_SLIDE,handnum,0,0,0,0);
}
static void bgunRumble(s32 handnum,s32 weaponnum) { oracle_service(NS_RUMBLE,handnum,weaponnum,0,0,0); }
static s32 oracle_script(s32 *values,s32 count,s32 *index,s32 fallback) {
    s32 value=fallback;
    if(count<0||count>NATIVE_SHOOTING_SCRIPT_LIMIT||*index<0){oracle_case->trace_overflow=1;return fallback;}
    if(*index<count)value=values[*index];
    if(*index<INT32_MAX)(*index)++;else oracle_case->trace_overflow=1;
    return value;
}
static s32 gsetGetFireslotDuration(struct gset *g) {
    s32 value;(void)g;value=oracle_script(oracle_case->fireslot_script,oracle_case->fireslot_count,&oracle_case->fireslot_index,oracle_case->fireslot_default);
    oracle_service(NS_FIRESLOT_DURATION,-1,0,0,0,value);return value;
}
static s32 gsetGetSingleShootSound(struct gset *g) {
    s32 value;(void)g;value=oracle_script(oracle_case->sound_script,oracle_case->sound_count,&oracle_case->sound_index,oracle_case->sound_default);
    oracle_service(NS_SINGLE_SOUND,-1,0,0,0,value);return value;
}
static OSPri osGetThreadPri(void *thread) {
    s32 value=thread?oracle_case->audio_priority:oracle_case->priority;
    oracle_service(thread?NS_AUDIO_PRIORITY:NS_CURRENT_PRIORITY,thread?1:0,0,0,0,value);return value;
}
static void osSetThreadPri(void *thread,OSPri value) {
    (void)thread;oracle_case->priority=value;oracle_service(NS_SET_PRIORITY,0,value,0,0,0);
}
static s32 sndGetState(struct sndstate *handle) {
    oracle_service(NS_SOUND_STATE,oracle_token(handle),oracle_token(handle),0,0,handle->state);return handle->state;
}
static void audioStop(struct sndstate *handle) {
    handle->state=AL_STOPPED;oracle_service(NS_SOUND_STOP,oracle_token(handle),oracle_token(handle),0,0,0);
}
static struct sndstate *sndStart(void *manager,s32 sound,struct sndstate **slot,s32 a,s32 b,s32 c,s32 d,s32 e) {
    struct sndstate *assigned=oracle_audio_ref(oracle_case->start_slot_token);
    struct sndstate *returned=oracle_audio_ref(oracle_case->start_return_token);
    s32 slotnum=slot==&oracle_hand->audiohandle2?2:3;
    (void)manager;(void)a;(void)b;(void)c;(void)d;(void)e;
    *slot=assigned;if(assigned)assigned->state=oracle_case->start_state;
    oracle_service(NS_SOUND_START,slotnum,sound,oracle_token(assigned),oracle_token(returned),oracle_token(returned));return returned;
}
static void audioPostEvent(struct sndstate *handle,s32 event,s32 bits) {
    oracle_case->last_pitch_token=oracle_token(handle);oracle_case->last_pitch_event=event;oracle_case->last_pitch_bits=bits;
    oracle_service(NS_SOUND_EVENT,oracle_token(handle),event,bits,0,0);
}
static void importCase(native_shooting_case *c,struct hand *h,struct player *p,struct weaponfunc_shootauto *f) {
    oracle_full=0;oracle_case=c;oracle_hand=h;g_Vars.currentplayer=p;
    memset(h,0,sizeof *h);memset(p,0,sizeof *p);memset(f,0,sizeof *f);memset(&oracle_graph,0,sizeof oracle_graph);
    oracle_graph.flags=c->flags;oracle_graph.has_max_rpm=c->automatic;
    oracle_graph.has_initial_rpm=c->graph_initial;oracle_graph.has_turret_accel=c->graph_accel;oracle_graph.has_turret_decel=c->graph_decel;
    oracle_graph.turret_accel=c->turretaccel;oracle_graph.turret_decel=c->turretdecel;
    oracle_graph.initial_rpm=c->initialrpm;oracle_graph.max_rpm=c->maxrpm;
    h->gset.weaponnum=c->shotgun?WEAPON_SHOTGUN:(c->laser?WEAPON_LASER:(c->mauler?WEAPON_MAULER:0));
    h->burstbullets=c->burstbullets;h->stateframes=c->stateframes;h->shotstotake=c->shots;
    h->stateflags=c->stateflags;h->loadedammo[0]=c->loadedammo;h->loadedammo[1]=c->loadedammo1;
    h->gs_float1=c->spool;h->shotremainder=c->remainder;h->triggeron=c->trigger;h->firing=c->firing;
    p->gunctrl.ammotypes[0]=c->ammotype;p->gunctrl.ammotypes[1]=c->ammotype1;p->insightaimmode=c->aim;
    f->base.flags=c->legacy_flags;f->turretaccel=c->legacy_accel;f->turretdecel=c->legacy_decel;
    f->initialrpm=c->legacy_initialrpm;f->maxrpm=c->legacy_maxrpm;
}
static void exportCase(native_shooting_case *c,const struct hand *h) {
    c->stateframes=h->stateframes;c->shots=h->shotstotake;c->spool=h->gs_float1;
    c->remainder=h->shotremainder;c->firing=h->firing;
}
static struct hand *importFull(native_shooting_case *c,struct hand *h,struct player *p,struct weaponfunc_shootauto *f) {
    s32 i;importCase(c,h,p,f);oracle_full=1;
    oracle_graph.ammo_slot=c->graph_ammo_slot;oracle_graph.has_function_type_id=c->graph_has_type;
    oracle_graph.function_type_id=c->graph_function_type;f->base.type=c->legacy_functype;f->base.ammoindex=c->legacy_ammoindex;
    h->flashon=c->flashon;h->loadslide=c->loadslide;h->matmot1=c->matmot;
    h->statevar1=c->statevar1;h->attacktype=c->attacktype;h->allowshootframe=c->allowshootframe;h->lastshootframe60=c->lastshootframe;
    for(i=0;i<NATIVE_SHOOTING_AUDIO_LIMIT;i++){oracle_audio[i].token=i+1;oracle_audio[i].state=c->audio_state[i];}
    h->audiohandle2=oracle_audio_ref(c->audio2);h->audiohandle3=oracle_audio_ref(c->audio3);
    for(i=0;i<2;i++){p->hands[i].slideinc=c->current_hand_slideinc[i];p->hands[i].lastshootframe60=c->current_hand_lastshootframe[i];}
    if(c->passed_is_current){p->hands[c->handnum]=*h;h=&p->hands[c->handnum];h->slideinc=c->current_hand_slideinc[c->handnum];}
    p->hands[1-c->handnum].lastshootframe60=c->other_lastshootframe;
    oracle_hand=h;g_Vars.lvframe60=c->frame60;g_BgunGeMuzzleFlashes=c->ge_flashes;
    if(!c->overrides_initialized){c->rontime_state=2;c->rofftime_state=4;c->overrides_initialized=1;}
    return h;
}


s32 bgun0f09a3f8(struct hand *hand, struct weaponfunc *func)
{
	bool burst = false;
	bool smallburst = false;
	struct gunctrl *ctrl = &g_Vars.currentplayer->gunctrl;
	const weapon_graph_held_function_t *graph = bgunGetHeldGraph(&hand->gset);
	u32 funcflags = graph ? graph->flags : func->flags;
	s32 ammoindex = bgunGetAmmoIndexFromGraph(func, graph);
	s32 functype = bgunGetFunctionTypeFromGraph(func, graph);
	bool legacyauto = (functype & 0xff00) == 0x100;
	bool isauto = (graph && graph->has_max_rpm) ? true : legacyauto;
	s32 turretaccel = 0;
	s32 turretdecel = 0;

	if (isauto) {
		if (graph && graph->has_turret_accel) {
			turretaccel = graph->turret_accel;
		} else if (legacyauto) {
			struct weaponfunc_shootauto *autofunc = (struct weaponfunc_shootauto *) func;
			turretaccel = autofunc->turretaccel;
		}

		if (graph && graph->has_turret_decel) {
			turretdecel = graph->turret_decel;
		} else if (legacyauto) {
			struct weaponfunc_shootauto *autofunc = (struct weaponfunc_shootauto *) func;
			turretdecel = autofunc->turretdecel;
		}
	}

	if ((funcflags & FUNCFLAG_BURST3) && hand->burstbullets < 3) {
		// Make automatics do single shot when holding aim
		if (!g_Vars.currentplayer->insightaimmode || !isauto) {
			// Not aiming and not an automatic weapon
			smallburst = true;
		}
	}

	if ((funcflags & FUNCFLAG_BURST2) && hand->burstbullets < 2) {
		smallburst = true;
	}

	if ((funcflags & FUNCFLAG_BURST5) && hand->burstbullets < 5) {
		smallburst = true;
	}

	if ((funcflags & FUNCFLAG_BURST50) && hand->burstbullets < 50) {
		burst = true;
	}

	if (smallburst) {
		burst = true;
	}

	if (hand->triggeron || (hand->stateflags & HANDSTATEFLAG_00000010) == 0 || burst) {
		if (ammoindex >= 0
				&& hand->loadedammo[ammoindex] == 0
				&& ctrl->ammotypes[ammoindex] >= 0) {
			// Clip is empty
			return -1;
		}

		if (isauto) {
			if (turretaccel > 0) {
				if (hand->gs_float1 < 1) {
					hand->gs_float1 += LVUPDATE60FREAL() / turretaccel;

					if (hand->gs_float1 > 1) {
						hand->gs_float1 = 1;
						return 1;
					}
				}
			} else {
				hand->gs_float1 = 1;
			}

			return 1;
		}

		hand->gs_float1 = 1;

		if (smallburst) {
			if (hand->burstbullets > 0) {
				s32 delay = 3;

				if (hand->gset.weaponnum == WEAPON_SHOTGUN) {
					delay = TICKS(13);
				}

				if (hand->stateframes < delay) {
					return 0;
				}
			}

			hand->stateframes = 0;
		}

		if ((funcflags & FUNCFLAG_BURST3) && hand->burstbullets == 2) {
			smallburst = false;
		}

		if ((funcflags & FUNCFLAG_BURST2) && hand->burstbullets == 1) {
			smallburst = false;
		}

		if ((funcflags & FUNCFLAG_BURST5) && hand->burstbullets == 4) {
			smallburst = false;
		}

		if (smallburst) {
			return 1;
		}

		return 2;
	}

	if (isauto) {
		if (turretdecel > 0) {
			if (hand->gs_float1 > 0) {
				hand->gs_float1 -= LVUPDATE60FREAL() / turretdecel;

				if (hand->gs_float1 < 0) {
					hand->gs_float1 = 0;
					return -1;
				}

				return 1;
			}
		} else {
			hand->gs_float1 = 0;
		}

		return -1;
	}

	return -1;
}

void bgun0f09a6f8(struct handweaponinfo *info, s32 handnum, struct hand *hand, struct weaponfunc *func)
{
	bool usesammo = true;
	const weapon_graph_held_function_t *graph = bgunGetHeldGraph(&hand->gset);
	u32 funcflags = graph ? graph->flags : func->flags;
	s32 ammoindex = bgunGetAmmoIndexFromGraph(func, graph);
	s32 functype = bgunGetFunctionTypeFromGraph(func, graph);
	bool legacyauto = (functype & 0xff00) == 0x100;
	bool isauto = (graph && graph->has_max_rpm) ? true : legacyauto;

	static u32 rontime = 2;
	static u32 rofftime = 4;

	mainOverrideVariable("rontime", &rontime);
	mainOverrideVariable("rofftime", &rofftime);

	hand->firing = true;

	if (isauto) {
		f32 tmp;
		f32 tmp2;
		f32 initialrpm = graph && graph->has_initial_rpm ? graph->initial_rpm : 0.0f;
		f32 maxrpm = graph && graph->has_max_rpm ? graph->max_rpm : 0.0f;

		if (legacyauto) {
			struct weaponfunc_shootauto *autofunc = (struct weaponfunc_shootauto *) func;
			if (!graph || !graph->has_initial_rpm) {
				initialrpm = autofunc->initialrpm;
			}
			if (!graph || !graph->has_max_rpm) {
				maxrpm = autofunc->maxrpm;
			}
		}

		tmp = initialrpm + (maxrpm - initialrpm) * hand->gs_float1;
		tmp2 = tmp / 60.0f * (LVUPDATE60FREAL() / 60.0f) + hand->shotremainder;

		hand->shotstotake = tmp2;
		hand->shotremainder = tmp2 - hand->shotstotake;

		if (hand->shotstotake <= 0) {
			if ((hand->stateflags & HANDSTATEFLAG_00000010) == 0) {
				hand->shotstotake++;
			} else {
				hand->firing = false;
			}
		}
	} else {
		hand->shotstotake = 1;

		if (hand->gset.weaponnum == WEAPON_LASER) {
			usesammo = false;
		}
	}

	oracle_uses_ammo = usesammo;
}


int32_t nativeShootingControl(native_shooting_case *c) {
    struct hand h;struct player p;struct weaponfunc_shootauto f;
    importCase(c,&h,&p,&f);s32 result=bgun0f09a3f8(&h,(struct weaponfunc *)&f);exportCase(c,&h);return result;
}
int32_t nativeShootingCredit(native_shooting_case *c) {
    struct hand h;struct player p;struct weaponfunc_shootauto f;
    importCase(c,&h,&p,&f);bgun0f09a6f8(0,0,&h,(struct weaponfunc *)&f);exportCase(c,&h);return oracle_uses_ammo;
}



#define bgunexpressioncontext prefix_bgunexpressioncontext
#define bgunExpressionRead prefix_bgunExpressionRead
#define bgunExpressionWrite prefix_bgunExpressionWrite
#define bgun0f09a3f8 converted_bgun0f09a3f8
#define bgun0f09a6f8 converted_bgun0f09a6f8
/* Synchronous gameplay expression bridge. Public archive preparation remains
 * separate; these stock programs preserve the two native control boundaries. */
#include "gameplay_graph_values.h"
struct bgunexpressioncontext {
    struct hand *hand;
    struct gunctrl *ctrl;
    struct weaponfunc *func;
    const weapon_graph_held_function_t *graph;
    u32 flags;
    s32 ammoindex;
    bool legacyauto, automatic;
    s32 acceleration, deceleration;
};
static ggv_value bgunExpressionRead(void *opaque, ggv_binding binding)
{
    struct bgunexpressioncontext *context = opaque;
    struct hand *hand = context->hand;
    ggv_value value = {0};
    switch (binding) {
    case GGV_FUNCTION_FLAGS: value.u = context->flags; break;
    case GGV_AMMO_INDEX: value.i = context->ammoindex; break;
    case GGV_AUTOMATIC: value.b = context->automatic; break;
    case GGV_ACCELERATION: value.i = context->acceleration; break;
    case GGV_DECELERATION: value.i = context->deceleration; break;
    case GGV_TRIGGER: value.b = hand->triggeron; break;
    case GGV_STATE_FLAGS: value.u = hand->stateflags; break;
    case GGV_BURST_BULLETS: value.i = hand->burstbullets; break;
    case GGV_AIM: value.b = g_Vars.currentplayer->insightaimmode; break;
    case GGV_LOADED_AMMO: value.i = hand->loadedammo[context->ammoindex]; break;
    case GGV_AMMO_TYPE: value.i = context->ctrl->ammotypes[context->ammoindex]; break;
    case GGV_SHOTGUN: value.b = hand->gset.weaponnum == WEAPON_SHOTGUN; break;
    case GGV_SHOTGUN_DELAY: value.i = TICKS(13); break;
    case GGV_LASER: value.b = hand->gset.weaponnum == WEAPON_LASER; break;
    case GGV_INITIAL_RPM:
        if (context->graph && context->graph->has_initial_rpm) value.f = context->graph->initial_rpm;
        else if (context->legacyauto) value.f = ((struct weaponfunc_shootauto *)context->func)->initialrpm;
        break;
    case GGV_MAX_RPM:
        if (context->graph && context->graph->has_max_rpm) value.f = context->graph->max_rpm;
        else if (context->legacyauto) value.f = ((struct weaponfunc_shootauto *)context->func)->maxrpm;
        break;
    case GGV_CLOCK: value.f = LVUPDATE60FREAL(); break;
    case GGV_SPOOL: value.f = hand->gs_float1; break;
    case GGV_STATE_FRAMES: value.i = hand->stateframes; break;
    case GGV_SHOTS: value.i = hand->shotstotake; break;
    case GGV_REMAINDER: value.f = hand->shotremainder; break;
    case GGV_FIRING: value.b = hand->firing; break;
    case GGV_BINDING_COUNT: break;
    }
    return value;
}
static void bgunExpressionWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct hand *hand = ((struct bgunexpressioncontext *)opaque)->hand;
    switch (binding) {
    case GGV_SPOOL: hand->gs_float1 = value.f; break;
    case GGV_STATE_FRAMES: hand->stateframes = value.i; break;
    case GGV_SHOTS: hand->shotstotake = value.i; break;
    case GGV_REMAINDER: hand->shotremainder = value.f; break;
    case GGV_FIRING: hand->firing = value.b; break;
    default: break;
    }
}
s32 bgun0f09a3f8(struct hand *hand, struct weaponfunc *func)
{
	struct gunctrl *ctrl = &g_Vars.currentplayer->gunctrl;
	const weapon_graph_held_function_t *graph = bgunGetHeldGraph(&hand->gset);
	u32 funcflags = graph ? graph->flags : func->flags;
	s32 ammoindex = bgunGetAmmoIndexFromGraph(func, graph);
	s32 functype = bgunGetFunctionTypeFromGraph(func, graph);
	bool legacyauto = (functype & 0xff00) == 0x100;
	bool isauto = (graph && graph->has_max_rpm) ? true : legacyauto;
	s32 turretaccel = 0;
	s32 turretdecel = 0;

	if (isauto) {
		if (graph && graph->has_turret_accel) {
			turretaccel = graph->turret_accel;
		} else if (legacyauto) {
			struct weaponfunc_shootauto *autofunc = (struct weaponfunc_shootauto *) func;
			turretaccel = autofunc->turretaccel;
		}

		if (graph && graph->has_turret_decel) {
			turretdecel = graph->turret_decel;
		} else if (legacyauto) {
			struct weaponfunc_shootauto *autofunc = (struct weaponfunc_shootauto *) func;
			turretdecel = autofunc->turretdecel;
		}
	}

	struct bgunexpressioncontext context = {
        hand, ctrl, func, graph, funcflags, ammoindex, legacyauto, isauto,
        turretaccel, turretdecel
    };
    ggv_host host = { &context, bgunExpressionRead, bgunExpressionWrite };
    int32_t result;
    /* The immutable stock program is compile-time validated and total. */
    ggvExecute(ggvStockProgram(GGV_SHOOT_CONTROL), &host, &result);
    return result;
}
void bgun0f09a6f8(struct handweaponinfo *info, s32 handnum, struct hand *hand, struct weaponfunc *func)
{
	bool usesammo = true;
	const weapon_graph_held_function_t *graph = bgunGetHeldGraph(&hand->gset);
	u32 funcflags = graph ? graph->flags : func->flags;
	s32 ammoindex = bgunGetAmmoIndexFromGraph(func, graph);
	s32 functype = bgunGetFunctionTypeFromGraph(func, graph);
	bool legacyauto = (functype & 0xff00) == 0x100;
	bool isauto = (graph && graph->has_max_rpm) ? true : legacyauto;

	static u32 rontime = 2;
	static u32 rofftime = 4;

	mainOverrideVariable("rontime", &rontime);
	mainOverrideVariable("rofftime", &rofftime);

    struct bgunexpressioncontext context = {
        hand, NULL, func, graph, funcflags, ammoindex, legacyauto, isauto, 0, 0
    };
    ggv_host host = { &context, bgunExpressionRead, bgunExpressionWrite };
    int32_t ammoResult;
    /* Overrides remain before firing=true; the native tail remains below. */
    ggvExecute(ggvStockProgram(GGV_SHOT_CREDIT), &host, &ammoResult);
    usesammo = ammoResult;
	oracle_uses_ammo = usesammo;
}
#undef bgun0f09a3f8
#undef bgun0f09a6f8
int32_t nativeGraphShootingControl(native_shooting_case *c) {
    struct hand h;struct player p;struct weaponfunc_shootauto f;
    importCase(c,&h,&p,&f);s32 result=converted_bgun0f09a3f8(&h,(struct weaponfunc *)&f);exportCase(c,&h);return result;
}
int32_t nativeGraphShootingCredit(native_shooting_case *c) {
    struct hand h;struct player p;struct weaponfunc_shootauto f;
    importCase(c,&h,&p,&f);converted_bgun0f09a6f8(0,0,&h,(struct weaponfunc *)&f);exportCase(c,&h);return oracle_uses_ammo;
}

#undef bgunexpressioncontext
#undef bgunExpressionRead
#undef bgunExpressionWrite


s32 oracle_original_full_control(struct hand *hand, struct weaponfunc *func)
{
	bool burst = false;
	bool smallburst = false;
	struct gunctrl *ctrl = &g_Vars.currentplayer->gunctrl;
	const weapon_graph_held_function_t *graph = bgunGetHeldGraph(&hand->gset);
	u32 funcflags = graph ? graph->flags : func->flags;
	s32 ammoindex = bgunGetAmmoIndexFromGraph(func, graph);
	s32 functype = bgunGetFunctionTypeFromGraph(func, graph);
	bool legacyauto = (functype & 0xff00) == 0x100;
	bool isauto = (graph && graph->has_max_rpm) ? true : legacyauto;
	s32 turretaccel = 0;
	s32 turretdecel = 0;

	if (isauto) {
		if (graph && graph->has_turret_accel) {
			turretaccel = graph->turret_accel;
		} else if (legacyauto) {
			struct weaponfunc_shootauto *autofunc = (struct weaponfunc_shootauto *) func;
			turretaccel = autofunc->turretaccel;
		}

		if (graph && graph->has_turret_decel) {
			turretdecel = graph->turret_decel;
		} else if (legacyauto) {
			struct weaponfunc_shootauto *autofunc = (struct weaponfunc_shootauto *) func;
			turretdecel = autofunc->turretdecel;
		}
	}

	if ((funcflags & FUNCFLAG_BURST3) && hand->burstbullets < 3) {
		// Make automatics do single shot when holding aim
		if (!g_Vars.currentplayer->insightaimmode || !isauto) {
			// Not aiming and not an automatic weapon
			smallburst = true;
		}
	}

	if ((funcflags & FUNCFLAG_BURST2) && hand->burstbullets < 2) {
		smallburst = true;
	}

	if ((funcflags & FUNCFLAG_BURST5) && hand->burstbullets < 5) {
		smallburst = true;
	}

	if ((funcflags & FUNCFLAG_BURST50) && hand->burstbullets < 50) {
		burst = true;
	}

	if (smallburst) {
		burst = true;
	}

	if (hand->triggeron || (hand->stateflags & HANDSTATEFLAG_00000010) == 0 || burst) {
		if (ammoindex >= 0
				&& hand->loadedammo[ammoindex] == 0
				&& ctrl->ammotypes[ammoindex] >= 0) {
			// Clip is empty
			return -1;
		}

		if (isauto) {
			if (turretaccel > 0) {
				if (hand->gs_float1 < 1) {
					hand->gs_float1 += LVUPDATE60FREAL() / turretaccel; oracle_record_store(NS_WRITE_SPOOL, hand);

					if (hand->gs_float1 > 1) {
						hand->gs_float1 = 1; oracle_record_store(NS_WRITE_SPOOL, hand);
						return 1;
					}
				}
			} else {
				hand->gs_float1 = 1; oracle_record_store(NS_WRITE_SPOOL, hand);
			}

			return 1;
		}

		hand->gs_float1 = 1; oracle_record_store(NS_WRITE_SPOOL, hand);

		if (smallburst) {
			if (hand->burstbullets > 0) {
				s32 delay = 3;

				if (hand->gset.weaponnum == WEAPON_SHOTGUN) {
					delay = TICKS(13);
				}

				if (hand->stateframes < delay) {
					return 0;
				}
			}

			hand->stateframes = 0; oracle_record_store(NS_WRITE_STATE_FRAMES, hand);
		}

		if ((funcflags & FUNCFLAG_BURST3) && hand->burstbullets == 2) {
			smallburst = false;
		}

		if ((funcflags & FUNCFLAG_BURST2) && hand->burstbullets == 1) {
			smallburst = false;
		}

		if ((funcflags & FUNCFLAG_BURST5) && hand->burstbullets == 4) {
			smallburst = false;
		}

		if (smallburst) {
			return 1;
		}

		return 2;
	}

	if (isauto) {
		if (turretdecel > 0) {
			if (hand->gs_float1 > 0) {
				hand->gs_float1 -= LVUPDATE60FREAL() / turretdecel; oracle_record_store(NS_WRITE_SPOOL, hand);

				if (hand->gs_float1 < 0) {
					hand->gs_float1 = 0; oracle_record_store(NS_WRITE_SPOOL, hand);
					return -1;
				}

				return 1;
			}
		} else {
			hand->gs_float1 = 0; oracle_record_store(NS_WRITE_SPOOL, hand);
		}

		return -1;
	}

	return -1;
}

void oracle_original_full_commit(struct handweaponinfo *info, s32 handnum, struct hand *hand, struct weaponfunc *func)
{
	bool usesammo = true;
	const weapon_graph_held_function_t *graph = bgunGetHeldGraph(&hand->gset);
	u32 funcflags = graph ? graph->flags : func->flags;
	s32 ammoindex = bgunGetAmmoIndexFromGraph(func, graph);
	s32 functype = bgunGetFunctionTypeFromGraph(func, graph);
	bool legacyauto = (functype & 0xff00) == 0x100;
	bool isauto = (graph && graph->has_max_rpm) ? true : legacyauto;

	u32 rontime = oracle_case->rontime_state;
	u32 rofftime = oracle_case->rofftime_state;

	mainOverrideVariable("rontime", &rontime);
	mainOverrideVariable("rofftime", &rofftime);

	hand->firing = true; oracle_record_store(NS_WRITE_FIRING, hand);

	if (isauto) {
		f32 tmp;
		f32 tmp2;
		f32 initialrpm = graph && graph->has_initial_rpm ? graph->initial_rpm : 0.0f;
		f32 maxrpm = graph && graph->has_max_rpm ? graph->max_rpm : 0.0f;

		if (legacyauto) {
			struct weaponfunc_shootauto *autofunc = (struct weaponfunc_shootauto *) func;
			if (!graph || !graph->has_initial_rpm) {
				initialrpm = autofunc->initialrpm;
			}
			if (!graph || !graph->has_max_rpm) {
				maxrpm = autofunc->maxrpm;
			}
		}

		tmp = initialrpm + (maxrpm - initialrpm) * hand->gs_float1;
		tmp2 = tmp / 60.0f * (LVUPDATE60FREAL() / 60.0f) + hand->shotremainder;

		hand->shotstotake = tmp2; oracle_record_store(NS_WRITE_SHOTS, hand);
		hand->shotremainder = tmp2 - hand->shotstotake; oracle_record_store(NS_WRITE_REMAINDER, hand);

		if (hand->shotstotake <= 0) {
			if ((hand->stateflags & HANDSTATEFLAG_00000010) == 0) {
				hand->shotstotake++; oracle_record_store(NS_WRITE_SHOTS, hand);
			} else {
				hand->firing = false; oracle_record_store(NS_WRITE_FIRING, hand);
			}
		}
	} else {
		hand->shotstotake = 1; oracle_record_store(NS_WRITE_SHOTS, hand);

		if (hand->gset.weaponnum == WEAPON_LASER) {
			usesammo = false;
		}
	}

	hand->burstbullets += hand->shotstotake; oracle_record_store(NS_WRITE_BURST, hand);

	if (funcflags & FUNCFLAG_NOMUZZLEFLASH) {
		hand->flashon = false; oracle_record_store(NS_WRITE_FLASH, hand);
	} else {
		if (g_BgunGeMuzzleFlashes) {
			if ((!isauto && (functype & 0xff) == INVENTORYFUNCTYPE_SHOOT)
					|| functype == INVENTORYFUNCTYPE_SHOOT_SINGLE
					|| (hand->shotstotake & 1)) {
				hand->flashon = true; oracle_record_store(NS_WRITE_FLASH, hand);
			}
		} else {
			hand->flashon = true; oracle_record_store(NS_WRITE_FLASH, hand);
		}
	}

	bgunStartSlide(handnum);

	hand->loadslide = 0; oracle_record_store(NS_WRITE_LOAD_SLIDE, hand);

	if (hand->firing) {
		hand->statevar1 = hand->stateframes; oracle_record_store(NS_WRITE_STATE_VAR1, hand);
		hand->stateflags |= HANDSTATEFLAG_00000020; oracle_record_store(NS_WRITE_STATE_FLAGS, hand);
		hand->stateflags |= HANDSTATEFLAG_00000010; oracle_record_store(NS_WRITE_STATE_FLAGS, hand);

		bgunRumble(handnum, info->weaponnum);

		if (usesammo && ammoindex >= 0) {
			hand->loadedammo[ammoindex] -= hand->shotstotake; oracle_record_store(NS_WRITE_LOADED_AMMO, hand);

			if (hand->loadedammo[ammoindex] < 0) {
				// Note: loadedammo is negative
				hand->shotstotake += hand->loadedammo[ammoindex]; oracle_record_store(NS_WRITE_SHOTS, hand);
				hand->loadedammo[ammoindex] = 0; oracle_record_store(NS_WRITE_LOADED_AMMO, hand);
			}
		}

		switch (functype & 0xff00) {
		case 0:
		case 0x100:
			hand->attacktype = HANDATTACKTYPE_SHOOT; oracle_record_store(NS_WRITE_ATTACK_TYPE, hand);
			break;
		case 0x200:
			hand->attacktype = HANDATTACKTYPE_SHOOTPROJECTILE; oracle_record_store(NS_WRITE_ATTACK_TYPE, hand);
			break;
		}
	}

	if (hand->firing) {
		bool playsound = false;

		if (gsetGetFireslotDuration(&hand->gset) > 0) {
			if (g_Vars.lvframe60 != g_Vars.currentplayer->hands[1 - handnum].lastshootframe60
					&& g_Vars.lvframe60 > hand->allowshootframe) {
				hand->allowshootframe = g_Vars.lvframe60 + gsetGetFireslotDuration(&hand->gset); oracle_record_store(NS_WRITE_ALLOW_FRAME, hand);
				playsound = true;
			}
		} else {
			if (hand->firing) {
				playsound = true;
			}
		}

		if (playsound) {
#if VERSION >= VERSION_NTSC_1_0
			OSPri prevpri = osGetThreadPri(0);
			osSetThreadPri(0, osGetThreadPri(&g_AudioManager.thread) + 1);
#endif

			if (hand->audiohandle2 && sndGetState(hand->audiohandle2) != AL_STOPPED) {
				audioStop(hand->audiohandle2);
			}

			if (hand->audiohandle3 && sndGetState(hand->audiohandle3) != AL_STOPPED) {
				audioStop(hand->audiohandle3);
			}

			if (gsetGetSingleShootSound(&hand->gset)) {
				struct sndstate *handle = NULL;

				if (hand->audiohandle2 == NULL) {
					handle = sndStart(var80095200, gsetGetSingleShootSound(&hand->gset), &hand->audiohandle2, -1, -1, -1, -1, -1);
				} else if (hand->audiohandle3 == NULL) {
					handle = sndStart(var80095200, gsetGetSingleShootSound(&hand->gset), &hand->audiohandle3, -1, -1, -1, -1, -1);
				}

				hand->lastshootframe60 = g_Vars.lvframe60; oracle_record_store(NS_WRITE_LAST_FRAME, hand);

				if (hand->gset.weaponnum == WEAPON_MAULER && handle) {
					s32 matmot = hand->matmot1;
					f32 tmp;
					f32 frac = matmot / 3.0f;

					if (frac > 1.0f) {
						frac = 1.0f;
					}

					tmp = 1.0f - frac * 0.4f;

					audioPostEvent(handle, AL_SNDP_PITCH_EVT, *(s32 *) &tmp);
				}

			}

#if VERSION >= VERSION_NTSC_1_0
			osSetThreadPri(0, prevpri);
#endif
		}
	}

    oracle_uses_ammo = usesammo;
}


#define bgun0f09a3f8 oracle_connected_full_control
#define bgun0f09a6f8 oracle_connected_full_commit
/* Synchronous typed composition. Catalog generation/host/audio witnesses stay
 * borrowed for this invocation only. Public-source profile selection remains
 * a separately owned propagation gate; SLIDE/RUMBLE bodies are unconverted. */
#include "gameplay_graph_values.h"
struct bgunexpressioncontext {
    struct hand *hand;
    struct gunctrl *ctrl;
    struct weaponfunc *func;
    const weapon_graph_held_function_t *graph;
    u32 flags;
    s32 ammoindex;
    bool legacyauto, automatic;
    s32 acceleration, deceleration;
    s32 functype, handnum;
    struct handweaponinfo *info;
    u32 *rontime, *rofftime;
};
static ggv_value bgunExpressionRead(void *opaque, ggv_binding binding)
{
    struct bgunexpressioncontext *context = opaque;
    struct hand *hand = context->hand;
    ggv_value value = {0};
    switch (binding) {
    case GGV_FUNCTION_FLAGS: value.u = context->flags; break;
    case GGV_AMMO_INDEX: value.i = context->ammoindex; break;
    case GGV_AUTOMATIC: value.b = context->automatic; break;
    case GGV_ACCELERATION: value.i = context->acceleration; break;
    case GGV_DECELERATION: value.i = context->deceleration; break;
    case GGV_TRIGGER: value.b = hand->triggeron; break;
    case GGV_STATE_FLAGS: value.u = hand->stateflags; break;
    case GGV_BURST_BULLETS: value.i = hand->burstbullets; break;
    case GGV_AIM: value.b = g_Vars.currentplayer->insightaimmode; break;
    case GGV_LOADED_AMMO: value.i = hand->loadedammo[context->ammoindex]; break;
    case GGV_AMMO_TYPE: value.i = context->ctrl->ammotypes[context->ammoindex]; break;
    case GGV_SHOTGUN: value.b = hand->gset.weaponnum == WEAPON_SHOTGUN; break;
    case GGV_SHOTGUN_DELAY: value.i = TICKS(13); break;
    case GGV_LASER: value.b = hand->gset.weaponnum == WEAPON_LASER; break;
    /* Kept for the separately addressable legacy prefix programs only. */
    case GGV_INITIAL_RPM:
        if (context->graph && context->graph->has_initial_rpm) value.f = context->graph->initial_rpm;
        else if (context->legacyauto) value.f = ((struct weaponfunc_shootauto *)context->func)->initialrpm;
        break;
    case GGV_MAX_RPM:
        if (context->graph && context->graph->has_max_rpm) value.f = context->graph->max_rpm;
        else if (context->legacyauto) value.f = ((struct weaponfunc_shootauto *)context->func)->maxrpm;
        break;
    case GGV_CLOCK: value.f = LVUPDATE60FREAL(); break;
    case GGV_SPOOL: value.f = hand->gs_float1; break;
    case GGV_STATE_FRAMES: value.i = hand->stateframes; break;
    case GGV_SHOTS: value.i = hand->shotstotake; break;
    case GGV_REMAINDER: value.f = hand->shotremainder; break;
    case GGV_FIRING: value.b = hand->firing; break;
    case GGV_GRAPH_FLAGS: value.u = context->graph->flags; break;
    case GGV_LEGACY_FLAGS: value.u = context->func->flags; break;
    case GGV_GRAPH_AMMO_SLOT: value.i = context->graph->ammo_slot; break;
    case GGV_LEGACY_AMMO_INDEX: value.i = context->func ? context->func->ammoindex : -1; break;
    case GGV_GRAPH_HAS_TYPE: value.b = context->graph->has_function_type_id; break;
    case GGV_GRAPH_FUNCTION_TYPE: value.i = context->graph->function_type_id; break;
    case GGV_LEGACY_FUNCTION_TYPE: value.i = context->func ? context->func->type : INVENTORYFUNCTYPE_NONE; break;
    case GGV_FUNCTION_TYPE: value.i = context->functype; break;
    case GGV_LEGACY_AUTOMATIC: value.b = context->legacyauto; break;
    case GGV_GRAPH_HAS_MAX_RPM: value.b = context->graph->has_max_rpm; break;
    case GGV_GRAPH_HAS_ACCEL: value.b = context->graph->has_turret_accel; break;
    case GGV_GRAPH_HAS_DECEL: value.b = context->graph->has_turret_decel; break;
    case GGV_GRAPH_ACCEL: value.i = context->graph->turret_accel; break;
    case GGV_GRAPH_DECEL: value.i = context->graph->turret_decel; break;
    case GGV_LEGACY_ACCEL: value.i = ((struct weaponfunc_shootauto *)context->func)->turretaccel; break;
    case GGV_LEGACY_DECEL: value.i = ((struct weaponfunc_shootauto *)context->func)->turretdecel; break;
    case GGV_GRAPH_HAS_INITIAL_RPM: value.b = context->graph->has_initial_rpm; break;
    case GGV_GRAPH_INITIAL_RPM: value.f = context->graph->initial_rpm; break;
    case GGV_GRAPH_MAX_RPM: value.f = context->graph->max_rpm; break;
    case GGV_LEGACY_INITIAL_RPM: value.f = ((struct weaponfunc_shootauto *)context->func)->initialrpm; break;
    case GGV_LEGACY_MAX_RPM: value.f = ((struct weaponfunc_shootauto *)context->func)->maxrpm; break;
    case GGV_GE_FLASH: value.b = g_BgunGeMuzzleFlashes; break;
    case GGV_FLASH: value.b = hand->flashon; break;
    case GGV_LOAD_SLIDE: value.f = hand->loadslide; break;
    case GGV_STATE_VAR1: value.i = hand->statevar1; break;
    case GGV_ATTACK_TYPE: value.i = hand->attacktype; break;
    case GGV_BIND_ATTACK_SHOOT: value.i = HANDATTACKTYPE_SHOOT; break;
    case GGV_ATTACK_PROJECTILE: value.i = HANDATTACKTYPE_SHOOTPROJECTILE; break;
    case GGV_FUNCTION_SHOOT_LOW: value.i = INVENTORYFUNCTYPE_SHOOT; break;
    case GGV_FUNCTION_SHOOT_SINGLE: value.i = INVENTORYFUNCTYPE_SHOOT_SINGLE; break;
    case GGV_CURRENT_FRAME: value.i = g_Vars.lvframe60; break;
    case GGV_OTHER_LAST_FRAME: value.i = g_Vars.currentplayer->hands[1-context->handnum].lastshootframe60; break;
    case GGV_ALLOW_SHOOT_FRAME: value.i = hand->allowshootframe; break;
    case GGV_LAST_SHOOT_FRAME: value.i = hand->lastshootframe60; break;
    case GGV_AUDIO_SLOT_2: value.ref = hand->audiohandle2; break;
    case GGV_AUDIO_SLOT_3: value.ref = hand->audiohandle3; break;
    case GGV_MAULER: value.b = hand->gset.weaponnum == WEAPON_MAULER; break;
    case GGV_MATMOT: value.f = hand->matmot1; break;
    case GGV_PRIORITY_ENABLED:
#if VERSION >= VERSION_NTSC_1_0
        value.b = true;
#endif
        break;
    case GGV_SOUND_STOPPED: value.i = AL_STOPPED; break;
    case GGV_BINDING_COUNT: break;
    }
    return value;
}
static void bgunExpressionWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunexpressioncontext *context = opaque;
    struct hand *hand = context->hand;
    switch (binding) {
    case GGV_FUNCTION_FLAGS: context->flags = value.u; break;
    case GGV_AMMO_INDEX: context->ammoindex = value.i; break;
    case GGV_FUNCTION_TYPE: context->functype = value.i; break;
    case GGV_LEGACY_AUTOMATIC: context->legacyauto = value.b; break;
    case GGV_AUTOMATIC: context->automatic = value.b; break;
    case GGV_ACCELERATION: context->acceleration = value.i; break;
    case GGV_DECELERATION: context->deceleration = value.i; break;
    case GGV_SPOOL: hand->gs_float1 = value.f; oracle_record_store(NS_WRITE_SPOOL, hand); break;
    case GGV_STATE_FRAMES: hand->stateframes = value.i; oracle_record_store(NS_WRITE_STATE_FRAMES, hand); break;
    case GGV_SHOTS: hand->shotstotake = value.i; oracle_record_store(NS_WRITE_SHOTS, hand); break;
    case GGV_REMAINDER: hand->shotremainder = value.f; oracle_record_store(NS_WRITE_REMAINDER, hand); break;
    case GGV_FIRING: hand->firing = value.b; oracle_record_store(NS_WRITE_FIRING, hand); break;
    case GGV_BURST_BULLETS: hand->burstbullets = value.i; oracle_record_store(NS_WRITE_BURST, hand); break;
    case GGV_FLASH: hand->flashon = value.b; oracle_record_store(NS_WRITE_FLASH, hand); break;
    case GGV_LOAD_SLIDE: hand->loadslide = value.f; oracle_record_store(NS_WRITE_LOAD_SLIDE, hand); break;
    case GGV_STATE_VAR1: hand->statevar1 = value.i; oracle_record_store(NS_WRITE_STATE_VAR1, hand); break;
    case GGV_STATE_FLAGS: hand->stateflags = value.u; oracle_record_store(NS_WRITE_STATE_FLAGS, hand); break;
    case GGV_LOADED_AMMO: hand->loadedammo[context->ammoindex] = value.i; oracle_record_store(NS_WRITE_LOADED_AMMO, hand); break;
    case GGV_ATTACK_TYPE: hand->attacktype = value.i; oracle_record_store(NS_WRITE_ATTACK_TYPE, hand); break;
    case GGV_ALLOW_SHOOT_FRAME: hand->allowshootframe = value.i; oracle_record_store(NS_WRITE_ALLOW_FRAME, hand); break;
    case GGV_LAST_SHOOT_FRAME: hand->lastshootframe60 = value.i; oracle_record_store(NS_WRITE_LAST_FRAME, hand); break;
    default: break;
    }
}
static ggv_value bgunExpressionCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value c)
{
    struct bgunexpressioncontext *context = opaque;
    struct hand *hand = context->hand;
    ggv_value value = {0};
    (void)c;
    switch (service) {
    case GGV_RESOLVE_GRAPH:
        context->graph = bgunGetHeldGraph(&hand->gset);
        value.b = context->graph != NULL;
        break;
    case GGV_OVERRIDE_ON: mainOverrideVariable("rontime", context->rontime); break;
    case GGV_OVERRIDE_OFF: mainOverrideVariable("rofftime", context->rofftime); break;
    case GGV_START_SLIDE: bgunStartSlide(context->handnum); break;
    case GGV_RUMBLE: bgunRumble(context->handnum, context->info->weaponnum); break;
    case GGV_FIRESLOT_DURATION: value.i = gsetGetFireslotDuration(&hand->gset); break;
    case GGV_SINGLE_SOUND: value.i = gsetGetSingleShootSound(&hand->gset); break;
    case GGV_CURRENT_PRIORITY: value.i = osGetThreadPri(0); break;
    case GGV_AUDIO_PRIORITY: value.i = osGetThreadPri(&g_AudioManager.thread); break;
    case GGV_SET_PRIORITY: osSetThreadPri(0, a.i); break;
    case GGV_SOUND_STATE: value.i = a.ref ? sndGetState(a.ref) : AL_STOPPED; break;
    case GGV_SOUND_STOP: if (a.ref) audioStop(a.ref); break;
    case GGV_SOUND_START:
        if (a.i == 2) value.ref = sndStart(var80095200, b.i, &hand->audiohandle2, -1, -1, -1, -1, -1);
        else if (a.i == 3) value.ref = sndStart(var80095200, b.i, &hand->audiohandle3, -1, -1, -1, -1, -1);
        break;
    case GGV_SOUND_EVENT: if (a.ref) audioPostEvent(a.ref, AL_SNDP_PITCH_EVT, b.i); break;
    case GGV_SERVICE_COUNT: break;
    }
    return value;
}
s32 bgun0f09a3f8(struct hand *hand, struct weaponfunc *func)
{
    struct bgunexpressioncontext context = {0};
    context.hand = hand;
    context.ctrl = &g_Vars.currentplayer->gunctrl;
    context.func = func;
    ggv_host host = { &context, bgunExpressionRead, bgunExpressionWrite, bgunExpressionCall };
    int32_t result = -1;
    /* Immutable preparation and service dependency checks precede effects. */
    if (!ggvExecute(ggvStockProgram(GGV_SHOOT_CONTROL_FULL), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: shooting control refused immutable program");
    }
    return result;
}
void bgun0f09a6f8(struct handweaponinfo *info, s32 handnum, struct hand *hand, struct weaponfunc *func)
{
    u32 rontime = oracle_case->rontime_state;
    u32 rofftime = oracle_case->rofftime_state;
    struct bgunexpressioncontext context = {0};
    context.hand = hand;
    context.func = func;
    context.handnum = handnum;
    context.info = info;
    context.rontime = &rontime;
    context.rofftime = &rofftime;
    ggv_host host = { &context, bgunExpressionRead, bgunExpressionWrite, bgunExpressionCall };
    int32_t usesammo;
    if (!ggvExecute(ggvStockProgram(GGV_SHOT_COMMIT_FULL), &host, &usesammo)) {
        sysLogPrintf(LOG_WARNING, "GGV: shooting commit refused immutable program");
    }

    oracle_uses_ammo = oracle_case->program_refused ? INT32_MIN : usesammo;
}


#undef bgun0f09a3f8
#undef bgun0f09a6f8
int32_t nativeShootingFullControl(native_shooting_case *c) {
    struct hand separate;struct player p;struct weaponfunc_shootauto f;
    struct hand *h=importFull(c,&separate,&p,&f);
    s32 result=oracle_original_full_control(h,c->legacy_function_absent ? NULL : (struct weaponfunc *)&f);exportFullCase();return result;
}
int32_t nativeShootingFullCommit(native_shooting_case *c) {
    struct hand separate;struct player p;struct weaponfunc_shootauto f;struct handweaponinfo info={c->info_weaponnum};
    struct hand *h=importFull(c,&separate,&p,&f);
    oracle_original_full_commit(&info,c->handnum,h,c->legacy_function_absent ? NULL : (struct weaponfunc *)&f);exportFullCase();return oracle_uses_ammo;
}
int32_t nativeGraphShootingFullControl(native_shooting_case *c) {
    struct hand separate;struct player p;struct weaponfunc_shootauto f;
    struct hand *h=importFull(c,&separate,&p,&f);
    s32 result=oracle_connected_full_control(h,c->legacy_function_absent ? NULL : (struct weaponfunc *)&f);exportFullCase();return result;
}
int32_t nativeGraphShootingFullCommit(native_shooting_case *c) {
    struct hand separate;struct player p;struct weaponfunc_shootauto f;struct handweaponinfo info={c->info_weaponnum};
    struct hand *h=importFull(c,&separate,&p,&f);
    oracle_connected_full_commit(&info,c->handnum,h,c->legacy_function_absent ? NULL : (struct weaponfunc *)&f);exportFullCase();return oracle_uses_ammo;
}


/* BEGIN complete hand oracle V4 appended source */
/* N-0640: complete hand function fixtures, appended without changing V3 C.
 * Current supported PC ROM variants are 2/4/5, all >= NTSC_1_0. The raw
 * lower-version original remains protected; it is not qualified here. */
#if VERSION < VERSION_NTSC_1_0
#error Hand oracle V4 qualifies only the supported PC version branch
#endif
enum { HAND_LEFT=1, CONTROLMODE_21=4, CONTROLMODE_PC=8,
       PAKTYPE_RUMBLE=1, WEAPONFLAG_DUALWIELD=0x1000 };
struct hand_oracle_playerstats { s32 mpindex; };
static struct {
    struct player *currentplayer;
    s32 currentplayernum;
    struct hand_oracle_playerstats *currentplayerstats;
} hand_oracle_vars;
static struct hand_oracle_playerstats hand_oracle_stats;
static void hand_oracle_export(void) {
    exportFullCase();
    oracle_case->passed_slideinc=oracle_hand->slideinc;
}
static void hand_oracle_count(s32 *count) {
    if(*count>=0 && *count<INT32_MAX) ++*count;
    else oracle_case->trace_overflow=1;
}
/* These are leaf service fixtures, with the real native API argument widths.
 * They record actual calls and four pulse operands, not gameplay counters. */
static void hand_oracle_joyGetContpadNumsForPlayer(s8 playernum,s32 *pad1,s32 *pad2) {
    *pad1=oracle_case->contpad1;*pad2=oracle_case->contpad2;
    hand_oracle_count(&oracle_case->joy_pad_queries);
    oracle_service(NS_JOY_PADS,playernum,*pad1,*pad2,0,0);
}
static s32 hand_oracle_optionsGetControlMode(s32 mpindex) {
    s32 value=oracle_case->controlmode;
    hand_oracle_count(&oracle_case->control_mode_queries);
    oracle_service(NS_CONTROL_MODE,mpindex,0,0,0,value);return value;
}
static s32 hand_oracle_pakGetType(s8 pad) {
    s32 fallback=0,value;
    if(pad==(s8)oracle_case->contpad1) fallback=oracle_case->paktype1;
    else if(pad==(s8)oracle_case->contpad2) fallback=oracle_case->paktype2;
    value=oracle_script(oracle_case->paktype_script,oracle_case->paktype_count,
        &oracle_case->paktype_index,fallback);
    hand_oracle_count(&oracle_case->pak_type_queries);
    oracle_service(NS_PAK_TYPE,pad,pad,0,0,value);return value;
}
static bool hand_oracle_weaponHasFlag(s32 weaponnum,u32 flag) {
    bool value=oracle_case->weapon_dualwield!=0;
    hand_oracle_count(&oracle_case->weapon_flag_queries);
    oracle_service(NS_WEAPON_FLAG,weaponnum,oracle_int_bits(flag),0,0,value);return value;
}
static void hand_oracle_pakRumble(s32 pad,f32 amplitude,s32 on,s32 off) {
    hand_oracle_count(&oracle_case->rumble_pulses);
    oracle_service(NS_PAK_RUMBLE,pad,oracle_int_bits(oracle_float_bits(amplitude)),on,off,0);
}
static void hand_oracle_record_slide(s32 handnum) {
    hand_oracle_export();
    nativeShootingRecordEvent(oracle_case,NS_EVENT_WRITE,NS_WRITE_SLIDEINC,handnum,0,0,0,0);
}
static void hand_oracle_import(native_shooting_case *c,struct hand *separate,
        struct player *player,struct weaponfunc_shootauto *func) {
    u32 saved_on=c->rontime_state,saved_off=c->rofftime_state;
    s32 saved_initialized=c->overrides_initialized;
    struct hand *h=importFull(c,separate,player,func);
    /* Hand-only entries do not initialize unrelated override fixtures. */
    c->rontime_state=saved_on;c->rofftime_state=saved_off;
    c->overrides_initialized=saved_initialized;
    if(!c->passed_is_current)h->slideinc=c->passed_slideinc;
    hand_oracle_vars.currentplayer=player;
    hand_oracle_vars.currentplayernum=c->current_playernum;
    hand_oracle_stats.mpindex=c->current_mpindex;
    hand_oracle_vars.currentplayerstats=&hand_oracle_stats;
}
/* Rename only symbols. The original body and actual adapter are included
 * below from separately protected, source-hashed files. */
#define g_Vars hand_oracle_vars
#define joyGetContpadNumsForPlayer hand_oracle_joyGetContpadNumsForPlayer
#define optionsGetControlMode hand_oracle_optionsGetControlMode
#define pakGetType hand_oracle_pakGetType
#define weaponHasFlag hand_oracle_weaponHasFlag
#define pakRumble hand_oracle_pakRumble

#define bgunRumble hand_original_rumble
#define bgunStartSlide hand_original_slide
void bgunRumble(s32 handnum, s32 weaponnum)
{
#if VERSION >= VERSION_NTSC_1_0
	u32 stack;
	s32 contpadtouse1;
	s32 contpadtouse2;
	bool singlewield = false;
	s32 contpad1;
	s32 contpad2;
	s32 contpad1hasrumble;
	s32 contpad2hasrumble;

	joyGetContpadNumsForPlayer(g_Vars.currentplayernum, &contpad1, &contpad2);

	if (optionsGetControlMode(g_Vars.currentplayerstats->mpindex) >= CONTROLMODE_21
			&& contpad1 >= 0 && contpad2 >= 0) {
		contpad1hasrumble = pakGetType(contpad1) == PAKTYPE_RUMBLE;
		contpad2hasrumble = pakGetType(contpad2) == PAKTYPE_RUMBLE;

		if (!weaponHasFlag(weaponnum, WEAPONFLAG_DUALWIELD)) {
			singlewield = true;
		}

		if (contpad1hasrumble && contpad2hasrumble) {
			if (singlewield) {
				pakRumble(contpad1, 0.2f, 2, 4);
				pakRumble(contpad2, 0.2f, 2, 4);
			} else {
				s32 contpadtouse1 = contpad1;

				if (handnum == HAND_LEFT) {
					contpadtouse1 = contpad2;
				}

				pakRumble(contpadtouse1, 0.2f, 2, 4);
			}
		} else {
			s32 contpadtouse2 = contpad1;

			if (contpad2hasrumble) {
				contpadtouse2 = contpad2;
			}

			pakRumble(contpadtouse2, 0.2f, 2, 4);
		}
	} else {
		if (contpad1 >= 0) {
			pakRumble(contpad1, 0.2f, 2, 4);
		}
	}
#else
	s32 stack1;
	s32 stack2;
	s8 contpad1;
	s8 contpad2;
	bool contpad1hasrumble;
	bool contpad2hasrumble;
	s32 contpadtouse1;
	s32 contpadtouse2;
	s32 controlmode = optionsGetControlMode(g_Vars.currentplayerstats->mpindex);

	if (controlmode >= CONTROLMODE_21 && controlmode < CONTROLMODE_PC) {
		contpad1hasrumble = pakGetType(g_Vars.currentplayernum) == PAKTYPE_RUMBLE;
		contpad2hasrumble = pakGetType(g_Vars.currentplayernum + PLAYERCOUNT()) == PAKTYPE_RUMBLE;

		if (contpad1hasrumble && contpad2hasrumble) {
			contpadtouse1 = g_Vars.currentplayernum;

			if (handnum == HAND_LEFT) {
				contpadtouse1 += PLAYERCOUNT();
			}

			pakRumble(contpadtouse1, 0.2f, 2, 4);
		} else {
			contpadtouse2 = g_Vars.currentplayernum;

			if (contpad2hasrumble) {
				contpadtouse2 += PLAYERCOUNT();
			}

			pakRumble(contpadtouse2, 0.2f, 2, 4);
		}
	} else {
		pakRumble(g_Vars.currentplayernum, 0.2f, 2, 4);
	}
#endif
}
void bgunStartSlide(s32 handnum)
{
	g_Vars.currentplayer->hands[handnum].slideinc = true; hand_oracle_record_slide(handnum);
}
#undef bgunRumble
#undef bgunStartSlide

#define bgunRumble hand_connected_rumble
#define bgunStartSlide hand_connected_slide
/* GGV hand services begin: borrowed current-player/device context. */
#include "gameplay_graph_values.h"
struct bgunhandcontext {
    s32 handnum, weaponnum, contpad1, contpad2;
};
static ggv_value bgunHandRead(void *opaque, ggv_binding binding)
{
    struct bgunhandcontext *context = opaque;
    ggv_value value = {0};
    switch (binding) {
    case GGV_HAND_NUMBER: value.i = context->handnum; break;
    case GGV_HAND_LEFT_NUMBER: value.i = HAND_LEFT; break;
    case GGV_PAD_1: value.i = context->contpad1; break;
    case GGV_PAD_2: value.i = context->contpad2; break;
    case GGV_CONTROL_MODE_21: value.i = CONTROLMODE_21; break;
    case GGV_PAK_RUMBLE_TYPE: value.i = PAKTYPE_RUMBLE; break;
    case GGV_CURRENT_HAND_SLIDEINC:
        value.b = g_Vars.currentplayer->hands[context->handnum].slideinc;
        break;
    default: break;
    }
    return value;
}
static void bgunHandWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunhandcontext *context = opaque;
    if (binding == GGV_CURRENT_HAND_SLIDEINC) {
        g_Vars.currentplayer->hands[context->handnum].slideinc = value.b; hand_oracle_record_slide(context->handnum);
    }
}
static ggv_value bgunHandCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value c)
{
    struct bgunhandcontext *context = opaque;
    ggv_value value = {0};
    (void)b; (void)c;
    switch (service) {
    case GGV_GET_PADS:
        joyGetContpadNumsForPlayer(g_Vars.currentplayernum,
            &context->contpad1, &context->contpad2);
        break;
    case GGV_CONTROL_MODE:
        value.i = optionsGetControlMode(g_Vars.currentplayerstats->mpindex);
        break;
    /* The native s8 API narrowing occurs at the same leaf call boundary. */
    case GGV_PAK_TYPE: value.i = pakGetType(a.i); break;
    case GGV_WEAPON_DUALWIELD:
        value.b = weaponHasFlag(context->weaponnum, WEAPONFLAG_DUALWIELD);
        break;
    default: break;
    }
    return value;
}
static ggv_value bgunHandCall4(void *opaque, ggv_service4 service,
        ggv_value a, ggv_value b, ggv_value c, ggv_value d)
{
    ggv_value value = {0};
    (void)opaque;
    if (service == GGV_PAK_RUMBLE) {
        /* PC VERSION>=NTSC1 device is s32; do not reuse pakGetType's s8. */
        pakRumble(a.i, b.f, c.i, d.i);
    }
    return value;
}
void bgunRumble(s32 handnum, s32 weaponnum)
{
#if VERSION >= VERSION_NTSC_1_0
    struct bgunhandcontext context = {0};
    context.handnum = handnum;
    context.weaponnum = weaponnum;
    ggv_host host = { &context, bgunHandRead, bgunHandWrite, bgunHandCall, bgunHandCall4 };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_HAND_RUMBLE), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: hand rumble refused immutable program");
    }
#else
	s32 stack1;
	s32 stack2;
	s8 contpad1;
	s8 contpad2;
	bool contpad1hasrumble;
	bool contpad2hasrumble;
	s32 contpadtouse1;
	s32 contpadtouse2;
	s32 controlmode = optionsGetControlMode(g_Vars.currentplayerstats->mpindex);

	if (controlmode >= CONTROLMODE_21 && controlmode < CONTROLMODE_PC) {
		contpad1hasrumble = pakGetType(g_Vars.currentplayernum) == PAKTYPE_RUMBLE;
		contpad2hasrumble = pakGetType(g_Vars.currentplayernum + PLAYERCOUNT()) == PAKTYPE_RUMBLE;

		if (contpad1hasrumble && contpad2hasrumble) {
			contpadtouse1 = g_Vars.currentplayernum;

			if (handnum == HAND_LEFT) {
				contpadtouse1 += PLAYERCOUNT();
			}

			pakRumble(contpadtouse1, 0.2f, 2, 4);
		} else {
			contpadtouse2 = g_Vars.currentplayernum;

			if (contpad2hasrumble) {
				contpadtouse2 += PLAYERCOUNT();
			}

			pakRumble(contpadtouse2, 0.2f, 2, 4);
		}
	} else {
		pakRumble(g_Vars.currentplayernum, 0.2f, 2, 4);
	}
#endif
}
void bgunStartSlide(s32 handnum)
{
    struct bgunhandcontext context = {0};
    context.handnum = handnum;
    ggv_host host = { &context, bgunHandRead, bgunHandWrite, bgunHandCall, bgunHandCall4 };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_HAND_START_SLIDE), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: hand slide refused immutable program");
    }
}
#undef bgunRumble
#undef bgunStartSlide

#undef g_Vars
#undef joyGetContpadNumsForPlayer
#undef optionsGetControlMode
#undef pakGetType
#undef weaponHasFlag
#undef pakRumble

void nativeShootingRumble(native_shooting_case *c) {
    struct hand separate;struct player p;struct weaponfunc_shootauto f;
    hand_oracle_import(c,&separate,&p,&f);
    hand_original_rumble(c->handnum,c->info_weaponnum);hand_oracle_export();
}

void nativeShootingSlide(native_shooting_case *c) {
    struct hand separate;struct player p;struct weaponfunc_shootauto f;
    hand_oracle_import(c,&separate,&p,&f);
    hand_original_slide(c->handnum);hand_oracle_export();
}

void nativeGraphShootingRumble(native_shooting_case *c) {
    struct hand separate;struct player p;struct weaponfunc_shootauto f;
    hand_oracle_import(c,&separate,&p,&f);
    hand_connected_rumble(c->handnum,c->info_weaponnum);hand_oracle_export();
}

void nativeGraphShootingSlide(native_shooting_case *c) {
    struct hand separate;struct player p;struct weaponfunc_shootauto f;
    hand_oracle_import(c,&separate,&p,&f);
    hand_connected_slide(c->handnum);hand_oracle_export();
}

/* BEGIN complete attack/recovery oracle V5 */
/* V5 appended accessed-field models; no native layout/ABI claim. */
#ifndef VERSION_JPN_FINAL
#define VERSION_JPN_FINAL 5
#endif
#ifndef PAL
#define PAL (VERSION == 4)
#endif
#if VERSION != 2 && VERSION != 4 && VERSION != 5
#error Attack oracle V5 qualifies supported PC versions 2/4/5 only
#endif
struct attack_coord { union { struct { f32 x,y,z; }; f32 f[3]; }; };
typedef struct { f32 f[4][4]; } attack_matrix;
struct attack_hand {
    struct hand base;
    s32 stateminor,statecycles,animmode;
    bool triggerreleased,useposrot;
    uint8_t unk0cc8_01:1;
    f32 rotxstart,rotxend,rotxoffset,matmot2;
    struct attack_coord posstart,posend,posoffset,aimpos;
    attack_matrix posrotmtx;
    f32 upgrademult[12],finalmult[12];
};
struct attack_cmd { s32 token; };
struct attack_weapon { f32 posz; };
struct attack_info { s32 weaponnum;struct attack_weapon *definition; };
struct attack_func { s32 type;s8 ammoindex;struct attack_cmd *fire_animation;u32 flags; };
struct attack_shoot {
    struct attack_func base;s8 recoverytime60,unk24,unk25,unk26,unk27;
    f32 recoildist,recoilangle;
};
struct attack_auto { struct attack_shoot base;f32 *vibrationstart,*vibrationmax; };
typedef struct {
    s32 has_recoil_anim_unk24,has_recoil_anim_unk25,has_recoil_anim_unk26,has_recoil_anim_unk27;
    s32 recoil_anim_unk24,recoil_anim_unk25,recoil_anim_unk26,recoil_anim_unk27;
    s32 has_recoverytime_ticks60,recoverytime_ticks60,has_recoildist,has_recoilangle;
    f32 recoildist,recoilangle;
} attack_graph_t;
static native_shooting_attack_case *attack_case;
static struct attack_hand *attack_hand_current;
static struct attack_auto attack_function;
static attack_graph_t attack_graph;
static struct attack_cmd attack_command={1};
static void attack_bits(uint32_t *out,const f32 *in,unsigned count) {
    unsigned i;for(i=0;i<count;i++)out[i]=oracle_float_bits(in[i]);
}
static void attack_shooting_snapshot(native_shooting_event *e,const native_shooting_case *s) {
    memset(e,0,sizeof *e);
    e->stateframes=s->stateframes;e->shots=s->shots;e->burstbullets=s->burstbullets;
    e->loadedammo=s->loadedammo;e->loadedammo1=s->loadedammo1;e->statevar1=s->statevar1;
    e->attacktype=s->attacktype;e->allowshootframe=s->allowshootframe;e->lastshootframe=s->lastshootframe;
    e->stateflags=s->stateflags;e->spool_bits=oracle_float_bits(s->spool);
    e->remainder_bits=oracle_float_bits(s->remainder);e->loadslide_bits=oracle_float_bits(s->loadslide);
    e->firing=s->firing;e->flashon=s->flashon;e->audio2=s->audio2;e->audio3=s->audio3;e->priority=s->priority;
    memcpy(e->current_hand_slideinc,s->current_hand_slideinc,sizeof e->current_hand_slideinc);
    memcpy(e->current_hand_lastshootframe,s->current_hand_lastshootframe,sizeof e->current_hand_lastshootframe);
    memcpy(e->audio_state,s->audio_state,sizeof e->audio_state);
}
void nativeShootingAttackResetTrace(native_shooting_attack_case *c) {
    c->trace_count=0;c->trace_overflow=0;
}
void nativeShootingAttackRecordEvent(native_shooting_attack_case *c,s32 kind,s32 operation,s32 subject,
        const s32 scalar[4],const f32 *input0,u32 count0,const f32 *input1,u32 count1,
        const f32 *output,u32 countout) {
    native_shooting_attack_event *e;native_shooting_attack_snapshot *s;
    if(c->trace_count<0||c->trace_count>=NATIVE_SHOOTING_ATTACK_TRACE_LIMIT
            ||count0>16||count1>16||countout>16
            ||(count0&&!input0)||(count1&&!input1)||(countout&&!output)) {
        c->trace_overflow=1;return;
    }
    e=&c->trace[c->trace_count++];memset(e,0,sizeof *e);
    e->kind=kind;e->operation=operation;e->subject=subject;
    if(scalar)memcpy(e->scalar,scalar,sizeof e->scalar);
    e->input0_count=count0;e->input1_count=count1;e->output_count=countout;
    attack_bits(e->input0_bits,input0,count0);attack_bits(e->input1_bits,input1,count1);
    attack_bits(e->output_bits,output,countout);s=&e->state;
    s->stateframes=c->shooting.stateframes;s->statevar1=c->shooting.statevar1;
    s->burstbullets=c->shooting.burstbullets;s->stateflags=c->shooting.stateflags;
    s->spool_bits=oracle_float_bits(c->shooting.spool);
    s->matmot1_bits=oracle_float_bits(c->shooting.matmot);s->matmot2_bits=oracle_float_bits(c->matmot2);
    s->stateminor=c->stateminor;s->statecycles=c->statecycles;s->animmode=c->animmode;
    s->triggerreleased=c->triggerreleased;s->animation_latch=c->animation_latch;s->useposrot=c->useposrot;
    s->rotx_bits[0]=oracle_float_bits(c->rotxstart);s->rotx_bits[1]=oracle_float_bits(c->rotxend);
    s->rotx_bits[2]=oracle_float_bits(c->rotxoffset);
    attack_bits(s->posstart_bits,c->posstart,3);attack_bits(s->posend_bits,c->posend,3);
    attack_bits(s->posoffset_bits,c->posoffset,3);attack_bits(s->posrotmtx_bits,c->posrotmtx,16);
    attack_bits(s->upgrademult_bits,c->upgrademult,12);attack_bits(s->finalmult_bits,c->finalmult,12);
    attack_shooting_snapshot(&s->shooting,&c->shooting);
}
void nativeShootingAttackForwardShootingTrace(native_shooting_attack_case *c,s32 start) {
    s32 i;
    if(start<0||start>c->shooting.trace_count||c->shooting.trace_count>NATIVE_SHOOTING_TRACE_LIMIT) {
        c->trace_overflow=1;return;
    }
    for(i=start;i<c->shooting.trace_count;i++) {
        const native_shooting_event *old=&c->shooting.trace[i];
        s32 scalars[4]={old->a,old->b,old->c,old->result};s32 before=c->trace_count;
        nativeShootingAttackRecordEvent(c,old->kind==NS_EVENT_WRITE?NTA_EVENT_SHOOTING_WRITE:
            NTA_EVENT_SHOOTING_SERVICE,old->operation,old->subject,scalars,0,0,0,0,0,0);
        if(c->trace_count==before+1) {
            native_shooting_attack_snapshot *s=&c->trace[before].state;
            s->shooting=*old;s->stateframes=old->stateframes;s->statevar1=old->statevar1;
            s->burstbullets=old->burstbullets;s->stateflags=old->stateflags;s->spool_bits=old->spool_bits;
        }
    }
}
static void attack_export(void) {
    native_shooting_attack_case *c=attack_case;struct attack_hand *a=attack_hand_current;
    struct hand *h=&a->base;native_shooting_case *s=&c->shooting;
    s->stateframes=h->stateframes;s->statevar1=h->statevar1;s->burstbullets=h->burstbullets;
    s->stateflags=h->stateflags;s->spool=h->gs_float1;s->matmot=h->matmot1;
    s->shots=h->shotstotake;s->remainder=h->shotremainder;s->firing=h->firing;s->flashon=h->flashon;
    s->loadslide=h->loadslide;s->loadedammo=h->loadedammo[0];s->loadedammo1=h->loadedammo[1];
    s->attacktype=h->attacktype;s->allowshootframe=h->allowshootframe;s->lastshootframe=h->lastshootframe60;
    s->audio2=oracle_token(h->audiohandle2);s->audio3=oracle_token(h->audiohandle3);
    c->stateminor=a->stateminor;c->statecycles=a->statecycles;c->triggerreleased=a->triggerreleased;
    c->animation_latch=a->unk0cc8_01;c->animmode=a->animmode;c->useposrot=a->useposrot;
    c->matmot2=a->matmot2;c->rotxstart=a->rotxstart;c->rotxend=a->rotxend;c->rotxoffset=a->rotxoffset;
    memcpy(c->posstart,a->posstart.f,sizeof c->posstart);memcpy(c->posend,a->posend.f,sizeof c->posend);
    memcpy(c->posoffset,a->posoffset.f,sizeof c->posoffset);memcpy(c->aimpos,a->aimpos.f,sizeof c->aimpos);
    memcpy(c->posrotmtx,&a->posrotmtx,sizeof c->posrotmtx);
    memcpy(c->upgrademult,a->upgrademult,sizeof c->upgrademult);memcpy(c->finalmult,a->finalmult,sizeof c->finalmult);
}
static void attack_record(s32 op,s32 subject,s32 a,s32 b,s32 c,s32 result,
        const f32 *i0,u32 n0,const f32 *i1,u32 n1,const f32 *out,u32 no) {
    s32 scalars[4]={a,b,c,result};attack_export();
    nativeShootingAttackRecordEvent(attack_case,NTA_EVENT_SERVICE,op,subject,scalars,i0,n0,i1,n1,out,no);
}
static void attack_store(s32 field) {
    attack_export();nativeShootingAttackRecordEvent(attack_case,NTA_EVENT_WRITE,field,-1,0,0,0,0,0,0,0);
}
static void attack_load_base(struct attack_hand *a) {
    native_shooting_case *s=&attack_case->shooting;struct hand *h=&a->base;s32 i;
    h->gset.weaponnum=attack_case->weaponnum;h->stateframes=s->stateframes;h->statevar1=s->statevar1;
    h->burstbullets=s->burstbullets;h->stateflags=s->stateflags;h->gs_float1=s->spool;h->matmot1=s->matmot;
    h->shotstotake=s->shots;h->shotremainder=s->remainder;h->firing=s->firing;h->flashon=s->flashon;
    h->loadslide=s->loadslide;h->triggeron=s->trigger;h->loadedammo[0]=s->loadedammo;h->loadedammo[1]=s->loadedammo1;
    h->attacktype=s->attacktype;h->allowshootframe=s->allowshootframe;h->lastshootframe60=s->lastshootframe;
    h->slideinc=s->passed_slideinc;oracle_case=s;
    for(i=0;i<NATIVE_SHOOTING_AUDIO_LIMIT;i++){oracle_audio[i].token=i+1;oracle_audio[i].state=s->audio_state[i];}
    h->audiohandle2=oracle_audio_ref(s->audio2);h->audiohandle3=oracle_audio_ref(s->audio3);
}
static void attack_import(native_shooting_attack_case *c,struct attack_hand *h,
        struct attack_info *info,struct attack_weapon *definition,bool driver) {
    attack_case=c;attack_hand_current=h;memset(h,0,sizeof *h);memset(&attack_function,0,sizeof attack_function);
    c->shooting.shotgun=c->weaponnum==19;c->shooting.mauler=c->weaponnum==6;c->shooting.laser=c->weaponnum==29;
    c->shooting.legacy_function_absent=c->function_absent;
    c->shooting.current_hand_lastshootframe[1-c->shooting.handnum]=c->shooting.other_lastshootframe;
    if(c->shooting.passed_is_current){
        c->shooting.current_hand_lastshootframe[c->shooting.handnum]=c->shooting.lastshootframe;
        c->shooting.passed_slideinc=c->shooting.current_hand_slideinc[c->shooting.handnum];
    }
    attack_load_base(h);h->stateminor=c->stateminor;h->statecycles=c->statecycles;
    h->triggerreleased=c->triggerreleased;h->unk0cc8_01=c->animation_latch;h->animmode=c->animmode;
    h->useposrot=c->useposrot;h->rotxstart=c->rotxstart;h->rotxend=c->rotxend;h->rotxoffset=c->rotxoffset;h->matmot2=c->matmot2;
    memcpy(h->posstart.f,c->posstart,sizeof c->posstart);memcpy(h->posend.f,c->posend,sizeof c->posend);
    memcpy(h->posoffset.f,c->posoffset,sizeof c->posoffset);memcpy(h->aimpos.f,c->aimpos,sizeof c->aimpos);
    memcpy(&h->posrotmtx,c->posrotmtx,sizeof c->posrotmtx);
    memcpy(h->upgrademult,c->upgrademult,sizeof c->upgrademult);memcpy(h->finalmult,c->finalmult,sizeof c->finalmult);
    c->animation_latch=h->unk0cc8_01;
    attack_function.base.base.type=c->shooting.legacy_functype;
    attack_function.base.base.ammoindex=c->shooting.legacy_ammoindex;attack_function.base.base.flags=c->shooting.legacy_flags;
    attack_function.base.base.fire_animation=c->fire_animation_present?&attack_command:0;
    attack_function.base.unk24=c->legacy_unk24;attack_function.base.unk25=c->legacy_unk25;
    attack_function.base.unk26=c->legacy_unk26;attack_function.base.unk27=c->legacy_unk27;
    attack_function.base.recoverytime60=c->legacy_recovery;
    attack_function.base.recoildist=c->legacy_recoildist;attack_function.base.recoilangle=c->legacy_recoilangle;
    attack_function.vibrationstart=c->vibration_start_present?c->vibration_start:0;
    attack_function.vibrationmax=c->vibration_max_present?c->vibration_max:0;
    attack_graph.has_recoil_anim_unk24=c->graph_has_unk24;attack_graph.recoil_anim_unk24=c->graph_unk24;
    attack_graph.has_recoil_anim_unk25=c->graph_has_unk25;attack_graph.recoil_anim_unk25=c->graph_unk25;
    attack_graph.has_recoil_anim_unk26=c->graph_has_unk26;attack_graph.recoil_anim_unk26=c->graph_unk26;
    attack_graph.has_recoil_anim_unk27=c->graph_has_unk27;attack_graph.recoil_anim_unk27=c->graph_unk27;
    attack_graph.has_recoverytime_ticks60=c->graph_has_recovery;attack_graph.recoverytime_ticks60=c->graph_recovery;
    attack_graph.has_recoildist=c->graph_has_recoildist;attack_graph.recoildist=c->graph_recoildist;
    attack_graph.has_recoilangle=c->graph_has_recoilangle;attack_graph.recoilangle=c->graph_recoilangle;
    definition->posz=c->definition_posz;info->definition=definition;info->weaponnum=c->shooting.info_weaponnum;
    if(driver&&!c->gkef_initialized){c->gkef_state=99;c->gkef_initialized=1;}
}
static const attack_graph_t *attack_resolve_graph(const struct gset *set) {
    attack_record(NTA_RESOLVE_GRAPH,-1,set->weaponnum,0,0,attack_case->shooting.graph_present!=0,0,0,0,0,0,0);
    return attack_case->shooting.graph_present?&attack_graph:0;
}
static struct attack_func *attack_resolve_function(const struct gset *set) {
    attack_record(NTA_RESOLVE_FUNCTION,-1,set->weaponnum,0,0,!attack_case->function_absent,0,0,0,0,0,0);
    return attack_case->function_absent?0:&attack_function.base.base;
}
static void attack_override(const char *name,u32 *value) {
    u32 before=*value;(void)name;if(attack_case->gkef_override_enabled)*value=attack_case->gkef_override_value;
    attack_case->gkef_state=*value;
    attack_record(NTA_OVERRIDE_GKEF,-1,oracle_int_bits(before),0,0,oracle_int_bits(*value),0,0,0,0,0,0);
}
static f32 attack_sin(f32 a){f32 out=attack_case->sin_result;attack_record(NTA_SIN,-1,oracle_int_bits(oracle_float_bits(a)),0,0,oracle_int_bits(oracle_float_bits(out)),0,0,0,0,0,0);return out;}
static f32 attack_cos(f32 a){f32 out=attack_case->cos_result;attack_record(NTA_COS,-1,oracle_int_bits(oracle_float_bits(a)),0,0,oracle_int_bits(oracle_float_bits(out)),0,0,0,0,0,0);return out;}
static f32 attack_tween(f32 a,f32 b,f32 c){f32 out=attack_case->tween_result;attack_record(NTA_TWEEN,-1,oracle_int_bits(oracle_float_bits(a)),oracle_int_bits(oracle_float_bits(b)),oracle_int_bits(oracle_float_bits(c)),oracle_int_bits(oracle_float_bits(out)),0,0,0,0,0,0);return out;}
static f32 attack_aim_x(s32 handnum){f32 out=attack_case->aim_x_result;attack_record(NTA_AIM_X,handnum,0,0,0,oracle_int_bits(oracle_float_bits(out)),0,0,0,0,0,0);return out;}
static void attack_matrix_x(f32 angle,attack_matrix *out){memcpy(out,attack_case->matrix_x_result,sizeof *out);attack_record(NTA_MATRIX_X,-1,oracle_int_bits(oracle_float_bits(angle)),0,0,0,0,0,0,0,&out->f[0][0],16);}
static void attack_matrix_translation(struct attack_coord *coord,attack_matrix *out){f32 before[16];memcpy(before,out,sizeof before);out->f[3][0]=attack_case->matrix_translation_result[12];out->f[3][1]=attack_case->matrix_translation_result[13];out->f[3][2]=attack_case->matrix_translation_result[14];attack_record(NTA_MATRIX_TRANSLATE,-1,0,0,0,0,coord->f,3,before,16,&out->f[0][0],16);}
static void attack_matrix_identity(attack_matrix *out){memcpy(out,attack_case->matrix_identity_result,sizeof *out);attack_record(NTA_MATRIX_IDENTITY,-1,0,0,0,0,0,0,0,0,&out->f[0][0],16);}
static void attack_animation(struct attack_cmd *cmd,s32 handnum,struct attack_hand *h){(void)h;attack_record(NTA_START_ANIMATION,-1,cmd?cmd->token:0,handnum,0,0,0,0,0,0,0,0);}
static bool attack_ready(struct attack_hand *h,s32 time){bool out=attack_case->animation_ready_result!=0;(void)h;attack_record(NTA_ANIMATION_READY,-1,time,0,0,out,0,0,0,0,0,0);return out;}
static void attack_vibration_mix(f32 *start,f32 *max,f32 scale,f32 *out){memcpy(out,attack_case->vibration_mix_result,2*sizeof *out);attack_record(NTA_VIBRATION_MIX,-1,oracle_int_bits(oracle_float_bits(scale)),0,0,0,start,2,max,2,out,2);}
static void attack_vibration_multiply(f32 *upgrade,f32 *scratch,f32 *out){memcpy(out,attack_case->vibration_multiply_result,2*sizeof *out);attack_record(NTA_VIBRATION_MULTIPLY,-1,0,0,0,0,upgrade,2,scratch,2,out,2);}
static s32 attack_call_control(struct attack_hand *h,struct attack_func *func,bool connected) {
    native_shooting_case *s=&attack_case->shooting;s32 start,result,initialized;u32 on,off;
    attack_export();start=s->trace_count;initialized=s->overrides_initialized;on=s->rontime_state;off=s->rofftime_state;
    result=connected?nativeGraphShootingFullControl(s):nativeShootingFullControl(s);
    s->overrides_initialized=initialized;s->rontime_state=on;s->rofftime_state=off;
    if(s->passed_is_current)s->passed_slideinc=s->current_hand_slideinc[s->handnum];
    attack_load_base(h);nativeShootingAttackForwardShootingTrace(attack_case,start);
    attack_record(NTA_CONTROL,-1,func->type,0,0,result,0,0,0,0,0,0);return result;
}
static void attack_call_commit(struct attack_info *info,s32 handnum,struct attack_hand *h,struct attack_func *func,bool connected) {
    native_shooting_case *s=&attack_case->shooting;s32 start;
    attack_export();start=s->trace_count;
    if(connected)nativeGraphShootingFullCommit(s);else nativeShootingFullCommit(s);
    if(s->passed_is_current)s->passed_slideinc=s->current_hand_slideinc[s->handnum];
    attack_load_base(h);nativeShootingAttackForwardShootingTrace(attack_case,start);
    attack_record(NTA_COMMIT,-1,handnum,info->weaponnum,func->type,0,0,0,0,0,0,0);
}
static s32 attack_native_control(struct attack_hand *h,struct attack_func *f){return attack_call_control(h,f,0);}
static s32 attack_connected_control(struct attack_hand *h,struct attack_func *f){return attack_call_control(h,f,1);}
static void attack_native_commit(struct attack_info *i,s32 n,struct attack_hand *h,struct attack_func *f){attack_call_commit(i,n,h,f,0);}
static void attack_connected_commit(struct attack_info *i,s32 n,struct attack_hand *h,struct attack_func *f){attack_call_commit(i,n,h,f,1);}
static void attack_log(s32 level,const char *message,...){(void)level;(void)message;attack_case->program_refused=1;}

static bool attack_native_recovery_call(struct attack_hand *,struct attack_info *,s32,struct attack_shoot *);
static bool attack_connected_recovery_call(struct attack_hand *,struct attack_info *,s32,struct attack_shoot *);
/* Original-expression macro constants, never new tuning. */
#undef TICKS
#if PAL
#define TICKS(value) ((value)*50/60)
#else
#define TICKS(value) (value)
#endif
#undef M_PI
#define M_PI 3.141592741f
#define M_BADPI 3.141092641f
#define M_BADTAU (M_BADPI*2)
#define WEAPON_SHOTGUN 19
#define WEAPON_MAULER 6
#define HANDANIMMODE_BUSY 2
#define HANDSTATEFLAG_00000040 0x40
#define HANDSTATEFLAG_00000080 0x80
#define HANDSTATEMINOR_ATTACK_SHOOT_0 0
#define HANDSTATEMINOR_ATTACK_SHOOT_1 1
#define HANDSTATEMINOR_ATTACK_SHOOT_2 2
/* Macro aliases retain raw body text; the old accessed hand is a typed member,
 * not an ABI reinterpret_cast or packed native-layout substitute. */
#define hand attack_hand
#define handweaponinfo attack_info
#define weapon attack_weapon
#define weaponfunc attack_func
#define weaponfunc_shoot attack_shoot
#define weaponfunc_shootauto attack_auto
#define weapon_graph_held_function_t attack_graph_t
#define gset base.gset
#define stateflags base.stateflags
#define stateframes base.stateframes
#define statevar1 base.statevar1
#define burstbullets base.burstbullets
#define gs_float1 base.gs_float1
#define triggeron base.triggeron
#define matmot1 base.matmot1
#define bgunGetHeldGraph attack_resolve_graph
#define gsetGetWeaponFunction attack_resolve_function
#define mainOverrideVariable attack_override
#define sinf attack_sin
#define cosf attack_cos
#define modelTweenRotAxis attack_tween
#define func0f0b131c attack_aim_x
#define mtx4LoadXRotation attack_matrix_x
#define mtx4SetTranslation attack_matrix_translation
#define mtx4LoadIdentity attack_matrix_identity
#define bgunStartAnimation attack_animation
#define bgun0f098a44 attack_ready
#define func0f097b64 attack_vibration_mix
#define func0f097b40 attack_vibration_multiply
#define sysLogPrintf attack_log

#define bgun0f09aba4 attack_native_recovery_raw
bool bgun0f09aba4(struct hand *hand, struct handweaponinfo *info, s32 handnum, struct weaponfunc_shoot *func)
{
	s32 unk24;
	s32 unk25;
	s32 sum;
	s32 unk26;
	s32 unk27;
	s32 recoverytime60;
	s32 frames;
	struct weapon *weapondef;
	f32 mult1;
	f32 recoildist;
	f32 recoilangle;
	f32 mult2;
	u32 stack;
	const weapon_graph_held_function_t *graph = bgunGetHeldGraph(&hand->gset);

#if PAL
	unk24 = graph && graph->has_recoil_anim_unk24 ? graph->recoil_anim_unk24 : func->unk24;
	unk25 = graph && graph->has_recoil_anim_unk25 ? graph->recoil_anim_unk25 : func->unk25;
	unk26 = graph && graph->has_recoil_anim_unk26 ? graph->recoil_anim_unk26 : func->unk26;
	unk27 = graph && graph->has_recoil_anim_unk27 ? graph->recoil_anim_unk27 : func->unk27;
	recoverytime60 = graph && graph->has_recoverytime_ticks60 ? graph->recoverytime_ticks60 : func->recoverytime60;
	weapondef = info->definition;

	if (unk24 >= 4) {
		unk24 = TICKS(unk24);
	}

	if (unk25 >= 4) {
		unk25 = TICKS(unk25);
	}

	if (unk26 >= 4) {
		unk26 = TICKS(unk26);
	}

	if (unk27 >= 4) {
		unk27 = TICKS(unk27);
	}

	if (recoverytime60 >= 4) {
		recoverytime60 = TICKS(recoverytime60);
	}

	sum = unk24 + unk25;
#elif VERSION >= VERSION_JPN_FINAL
	unk24 = graph && graph->has_recoil_anim_unk24 ? graph->recoil_anim_unk24 : func->unk24;
	unk25 = graph && graph->has_recoil_anim_unk25 ? graph->recoil_anim_unk25 : func->unk25;
	unk26 = graph && graph->has_recoil_anim_unk26 ? graph->recoil_anim_unk26 : func->unk26;
	unk27 = graph && graph->has_recoil_anim_unk27 ? graph->recoil_anim_unk27 : func->unk27;
	recoverytime60 = graph && graph->has_recoverytime_ticks60 ? graph->recoverytime_ticks60 : func->recoverytime60;
	weapondef = info->definition;
	sum = unk24 + unk25;
#else
	unk24 = graph && graph->has_recoil_anim_unk24 ? graph->recoil_anim_unk24 : func->unk24;
	unk25 = graph && graph->has_recoil_anim_unk25 ? graph->recoil_anim_unk25 : func->unk25;
	sum = unk24 + unk25;
	unk26 = graph && graph->has_recoil_anim_unk26 ? graph->recoil_anim_unk26 : func->unk26;
	unk27 = graph && graph->has_recoil_anim_unk27 ? graph->recoil_anim_unk27 : func->unk27;
	recoverytime60 = graph && graph->has_recoverytime_ticks60 ? graph->recoverytime_ticks60 : func->recoverytime60;
	weapondef = info->definition;
#endif

	frames = hand->stateframes - hand->statevar1;

	if (sum < 1) {
		sum = 0;
	} else {
		if (hand->triggerreleased
				&& hand->triggeron
				&& frames >= unk26
				&& unk26 > 0
				&& unk27 >= 0
				&& (hand->stateflags & HANDSTATEFLAG_00000040) == 0
				&& frames + unk27 < sum) {
			hand->stateflags |= HANDSTATEFLAG_00000040; attack_store(NTA_WRITE_STATE_FLAGS);
			hand->statevar1 = frames; attack_store(NTA_WRITE_STATE_VAR1);

			hand->rotxstart = hand->rotxoffset; attack_store(NTA_WRITE_ROTX_START);
			hand->rotxend = 0; attack_store(NTA_WRITE_ROTX_END);

			hand->posend.x = 0; attack_store(NTA_WRITE_POS_END_X);
			hand->posend.y = 0; attack_store(NTA_WRITE_POS_END_Y);
			hand->posend.z = 0; attack_store(NTA_WRITE_POS_END_Z);

			hand->posstart.x = hand->posoffset.x; attack_store(NTA_WRITE_POS_START_X);
			hand->posstart.y = hand->posoffset.y; attack_store(NTA_WRITE_POS_START_Y);
			hand->posstart.z = hand->posoffset.z; attack_store(NTA_WRITE_POS_START_Z);
		}

		if (hand->stateflags & HANDSTATEFLAG_00000040) {
			if (unk27 > frames - hand->statevar1) {
				mult1 = cosf((f32)(unk27 - frames + hand->statevar1) * 1.5707963705063f / (f32)unk27) * 0.5f + 0.5f;

				hand->rotxoffset = modelTweenRotAxis(hand->rotxstart, hand->rotxend, mult1); attack_store(NTA_WRITE_ROTX_OFFSET);
				hand->useposrot = true; attack_store(NTA_WRITE_USE_POS_ROT);

				hand->posoffset.x = (hand->posend.x - hand->posstart.x) * mult1 + hand->posstart.x; attack_store(NTA_WRITE_POS_OFFSET_X);
				hand->posoffset.y = (hand->posend.y - hand->posstart.y) * mult1 + hand->posstart.y; attack_store(NTA_WRITE_POS_OFFSET_Y);
				hand->posoffset.z = (hand->posend.z - hand->posstart.z) * mult1 + hand->posstart.z; attack_store(NTA_WRITE_POS_OFFSET_Z);

				mtx4LoadXRotation(hand->rotxoffset, &hand->posrotmtx);
				mtx4SetTranslation(&hand->posoffset, &hand->posrotmtx);
			} else {
				mtx4LoadIdentity(&hand->posrotmtx);
				hand->useposrot = false; attack_store(NTA_WRITE_USE_POS_ROT);
				return true;
			}
		}

		if (frames < sum && (hand->stateflags & HANDSTATEFLAG_00000040) == 0) {
			recoildist = graph && graph->has_recoildist ? graph->recoildist : func->recoildist;
			recoilangle = graph && graph->has_recoilangle ? graph->recoilangle : func->recoilangle;

			if ((hand->stateflags & HANDSTATEFLAG_00000080) == 0) {
				hand->stateflags |= HANDSTATEFLAG_00000080; attack_store(NTA_WRITE_STATE_FLAGS);
				hand->rotxstart = hand->rotxoffset; attack_store(NTA_WRITE_ROTX_START);
				hand->posstart.x = hand->posoffset.x; attack_store(NTA_WRITE_POS_START_X);
				hand->posstart.y = hand->posoffset.y; attack_store(NTA_WRITE_POS_START_Y);
				hand->posstart.z = hand->posoffset.z; attack_store(NTA_WRITE_POS_START_Z);
			}

			hand->rotxend = M_BADTAU - (recoilangle * M_BADTAU) / 360.0f; attack_store(NTA_WRITE_ROTX_END);

			hand->posend.x = (func0f0b131c(handnum) - hand->aimpos.x) * recoildist / 1000.0f; attack_store(NTA_WRITE_POS_END_X);
			hand->posend.y = 0; attack_store(NTA_WRITE_POS_END_Y);
			hand->posend.z = (weapondef->posz - hand->aimpos.z) * recoildist / 1000.0f; attack_store(NTA_WRITE_POS_END_Z);

			if (frames < unk24) {
				mult2 = sinf(frames * 1.5707963705063f / (f32)unk24);
			} else {
				mult2 = cosf((f32)(frames - unk24) * M_PI / (f32)unk25) * 0.5f + 0.5f;
			}

			hand->rotxoffset = modelTweenRotAxis(hand->rotxstart, hand->rotxend, mult2); attack_store(NTA_WRITE_ROTX_OFFSET);
			hand->useposrot = true; attack_store(NTA_WRITE_USE_POS_ROT);

			hand->posoffset.x = (hand->posend.x - hand->posstart.x) * mult2 + hand->posstart.x; attack_store(NTA_WRITE_POS_OFFSET_X);
			hand->posoffset.y = (hand->posend.y - hand->posstart.y) * mult2 + hand->posstart.y; attack_store(NTA_WRITE_POS_OFFSET_Y);
			hand->posoffset.z = (hand->posend.z - hand->posstart.z) * mult2 + hand->posstart.z; attack_store(NTA_WRITE_POS_OFFSET_Z);

			mtx4LoadXRotation(hand->rotxoffset, &hand->posrotmtx);
			mtx4SetTranslation(&hand->posoffset, &hand->posrotmtx);
		}
	}

	if (sum <= frames) {
		if (unk27 >= 0 && hand->triggerreleased && hand->triggeron) {
			return true;
		} else if (sum + recoverytime60 <= frames) {
			return true;
		}
	}

	return false;
}
#undef bgun0f09aba4
#define bgun0f09aba4 attack_native_recovery_call
#define bgun0f09a3f8 attack_native_control
#define bgun0f09a6f8 attack_native_commit
#define bgunTickIncAttackingShoot attack_native_driver_raw
bool bgunTickIncAttackingShoot(struct handweaponinfo *info, s32 handnum, struct hand *hand)
{
	u32 var80070128 = attack_case->gkef_state;

	struct weaponfunc *func = gsetGetWeaponFunction(&hand->gset);
	bool sp68;
	s32 sp64;
	s32 sp60;

	if (func == NULL) {
		return true;
	}

	if (hand->stateminor == HANDSTATEMINOR_ATTACK_SHOOT_0) {
		sp64 = 1;

		mainOverrideVariable("gkef", &var80070128);

		if (hand->statecycles == 0) {
			hand->gs_float1 = 0; attack_store(NTA_WRITE_SPOOL);

			if (func->fire_animation) {
				bgunStartAnimation(func->fire_animation, handnum, hand);
				hand->unk0cc8_01 = true; attack_store(NTA_WRITE_ANIMATION_LATCH);
			}

			hand->burstbullets = 0; attack_store(NTA_WRITE_BURST);
		}

		if (!bgun0f098a44(hand, 2)) {
			sp64 = 0;
		}

		if (sp64) {
			hand->stateminor = HANDSTATEMINOR_ATTACK_SHOOT_1; attack_store(NTA_WRITE_STATE_MINOR);
		}

		hand->matmot2 = hand->gs_float1; attack_store(NTA_WRITE_MATMOT2);
	}

	if (hand->stateminor == HANDSTATEMINOR_ATTACK_SHOOT_1) {
		sp60 = bgun0f09a3f8(hand, func);

		if ((func->type & 0xff00) == 0x100) {
			struct weaponfunc_shootauto *autofunc = (struct weaponfunc_shootauto *) func;
			f32 floats[12];

			if (autofunc->vibrationstart != NULL && autofunc->vibrationmax != NULL) {
				func0f097b64(autofunc->vibrationstart, autofunc->vibrationmax, hand->gs_float1, floats);
				func0f097b40(hand->upgrademult, floats, hand->finalmult);
			}
		}

		if (sp60 > 0) {
			bgun0f09a6f8(info, handnum, hand, func);
		}

		if (sp60 < 0 || sp60 == 2) {
			hand->stateminor = HANDSTATEMINOR_ATTACK_SHOOT_2; attack_store(NTA_WRITE_STATE_MINOR);
		}

		hand->matmot2 = hand->gs_float1; attack_store(NTA_WRITE_MATMOT2);

		if (hand->triggeron && hand->matmot2 < 0.4f) {
			hand->matmot2 = 0.4f; attack_store(NTA_WRITE_MATMOT2);
		}

		if (hand->triggerreleased) {
			hand->unk0cc8_01 = false; attack_store(NTA_WRITE_ANIMATION_LATCH);
		}

		return false;
	}

	if (hand->stateminor == HANDSTATEMINOR_ATTACK_SHOOT_2) {
		if (hand->stateflags & HANDSTATEFLAG_00000020) {
			sp68 = bgun0f09aba4(hand, info, handnum, (struct weaponfunc_shoot *) func);
		} else {
			sp68 = true;
		}

		if (hand->gset.weaponnum == WEAPON_SHOTGUN && hand->animmode == HANDANIMMODE_BUSY) {
			sp68 = false;
		}

		hand->matmot2 = hand->gs_float1; attack_store(NTA_WRITE_MATMOT2);

		if (sp68 && !hand->triggeron) {
			hand->matmot2 = 0; attack_store(NTA_WRITE_MATMOT2);
		}

		if (hand->gset.weaponnum == WEAPON_MAULER) {
			hand->matmot1 = 0; attack_store(NTA_WRITE_MATMOT1);
		}

		return sp68;
	}

	return false;
}
#undef bgun0f09aba4
#undef bgun0f09a3f8
#undef bgun0f09a6f8
#undef bgunTickIncAttackingShoot
#define bgun0f09aba4 attack_connected_recovery_call
#define bgun0f09a3f8 attack_connected_control
#define bgun0f09a6f8 attack_connected_commit
/* GGV attack services begin: synchronous borrowed recoil/animation context. */
struct bgunattackcontext {
    struct hand *hand; struct handweaponinfo *info; s32 handnum;
    struct weaponfunc *func; const weapon_graph_held_function_t *graph;
    struct weapon *definition; u32 *gkef; f32 vibration[12];
};
static ggv_value bgunAttackRead(void *opaque, ggv_binding binding)
{
    struct bgunattackcontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_RECOVERY_PAL: v.b = PAL; break;
    case GGV_RECOVERY_JPN: v.b = VERSION >= VERSION_JPN_FINAL; break;
    case GGV_GRAPH_RECOIL_24_HAS: v.b = c->graph->has_recoil_anim_unk24; break;
    case GGV_GRAPH_RECOIL_24: v.i = c->graph->recoil_anim_unk24; break;
    case GGV_LEGACY_RECOIL_24: v.i = ((struct weaponfunc_shoot *) c->func)->unk24; break;
    case GGV_GRAPH_RECOIL_25_HAS: v.b = c->graph->has_recoil_anim_unk25; break;
    case GGV_GRAPH_RECOIL_25: v.i = c->graph->recoil_anim_unk25; break;
    case GGV_LEGACY_RECOIL_25: v.i = ((struct weaponfunc_shoot *) c->func)->unk25; break;
    case GGV_GRAPH_RECOIL_26_HAS: v.b = c->graph->has_recoil_anim_unk26; break;
    case GGV_GRAPH_RECOIL_26: v.i = c->graph->recoil_anim_unk26; break;
    case GGV_LEGACY_RECOIL_26: v.i = ((struct weaponfunc_shoot *) c->func)->unk26; break;
    case GGV_GRAPH_RECOIL_27_HAS: v.b = c->graph->has_recoil_anim_unk27; break;
    case GGV_GRAPH_RECOIL_27: v.i = c->graph->recoil_anim_unk27; break;
    case GGV_LEGACY_RECOIL_27: v.i = ((struct weaponfunc_shoot *) c->func)->unk27; break;
    case GGV_GRAPH_RECOIL_TIME_HAS: v.b = c->graph->has_recoverytime_ticks60; break;
    case GGV_GRAPH_RECOIL_TIME: v.i = c->graph->recoverytime_ticks60; break;
    case GGV_LEGACY_RECOIL_TIME: v.i = ((struct weaponfunc_shoot *) c->func)->recoverytime60; break;
    case GGV_GRAPH_RECOIL_DIST_HAS: v.b = c->graph->has_recoildist; break;
    case GGV_GRAPH_RECOIL_DIST: v.f = c->graph->recoildist; break;
    case GGV_LEGACY_RECOIL_DIST: v.f = ((struct weaponfunc_shoot *) c->func)->recoildist; break;
    case GGV_GRAPH_RECOIL_ANGLE_HAS: v.b = c->graph->has_recoilangle; break;
    case GGV_GRAPH_RECOIL_ANGLE: v.f = c->graph->recoilangle; break;
    case GGV_LEGACY_RECOIL_ANGLE: v.f = ((struct weaponfunc_shoot *) c->func)->recoilangle; break;
    case GGV_TRIGGER_RELEASED: v.b = c->hand->triggerreleased; break;
    case GGV_ROTX_START: v.f = c->hand->rotxstart; break;
    case GGV_ROTX_END: v.f = c->hand->rotxend; break;
    case GGV_ROTX_OFFSET: v.f = c->hand->rotxoffset; break;
    case GGV_USE_POSROT: v.b = c->hand->useposrot; break;
    case GGV_STATE_MINOR: v.i = c->hand->stateminor; break;
    case GGV_ANIMATION_LATCH: v.b = c->hand->unk0cc8_01; break;
    case GGV_MATMOT2: v.f = c->hand->matmot2; break;
    case GGV_MATMOT1: v.f = c->hand->matmot1; break;
    case GGV_POS_START_X: v.f = c->hand->posstart.x; break;
    case GGV_POS_START_Y: v.f = c->hand->posstart.y; break;
    case GGV_POS_START_Z: v.f = c->hand->posstart.z; break;
    case GGV_POS_END_X: v.f = c->hand->posend.x; break;
    case GGV_POS_END_Y: v.f = c->hand->posend.y; break;
    case GGV_POS_END_Z: v.f = c->hand->posend.z; break;
    case GGV_POS_OFFSET_X: v.f = c->hand->posoffset.x; break;
    case GGV_POS_OFFSET_Y: v.f = c->hand->posoffset.y; break;
    case GGV_POS_OFFSET_Z: v.f = c->hand->posoffset.z; break;
    case GGV_AIM_POS_X: v.f = c->hand->aimpos.x; break;
    case GGV_AIM_POS_Z: v.f = c->hand->aimpos.z; break;
    case GGV_DEFINITION_POS_Z: v.f = c->definition->posz; break;
    case GGV_RECOIL_PI: v.f = M_PI; break;
    case GGV_RECOIL_BAD_TAU: v.f = M_BADTAU; break;
    case GGV_POS_MATRIX: v.ref = &c->hand->posrotmtx; break;
    case GGV_POS_COORD: v.ref = &c->hand->posoffset; break;
    case GGV_STATE_CYCLES: v.i = c->hand->statecycles; break;
    case GGV_ANIMATION_BUSY: v.b = c->hand->animmode == HANDANIMMODE_BUSY; break;
    case GGV_FIRE_ANIMATION: v.ref = c->func->fire_animation; break;
    case GGV_PASSED_HAND: v.ref = c->hand; break;
    case GGV_VIBRATION_START: v.ref = ((struct weaponfunc_shootauto *) c->func)->vibrationstart; break;
    case GGV_VIBRATION_MAX: v.ref = ((struct weaponfunc_shootauto *) c->func)->vibrationmax; break;
    case GGV_VIBRATION_SCRATCH: v.ref = c->vibration; break;
    case GGV_VIBRATION_UPGRADE: v.ref = c->hand->upgrademult; break;
    case GGV_VIBRATION_FINAL: v.ref = c->hand->finalmult; break;
    case GGV_TRIGGER: v.b = c->hand->triggeron; break;
    case GGV_STATE_FLAGS: v.u = c->hand->stateflags; break;
    case GGV_STATE_VAR1: v.i = c->hand->statevar1; break;
    case GGV_STATE_FRAMES: v.i = c->hand->stateframes; break;
    case GGV_SPOOL: v.f = c->hand->gs_float1; break;
    case GGV_BURST_BULLETS: v.i = c->hand->burstbullets; break;
    case GGV_LEGACY_FUNCTION_TYPE: v.i = c->func->type; break;
    case GGV_HAND_NUMBER: v.i = c->handnum; break;
    case GGV_SHOTGUN: v.b = c->hand->gset.weaponnum == WEAPON_SHOTGUN; break;
    case GGV_MAULER: v.b = c->hand->gset.weaponnum == WEAPON_MAULER; break;
    default: break;
    } return v;
}
static void bgunAttackWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunattackcontext *c = opaque;
    switch (binding) {
    case GGV_ROTX_START: c->hand->rotxstart = value.f; attack_store(NTA_WRITE_ROTX_START); break;
    case GGV_ROTX_END: c->hand->rotxend = value.f; attack_store(NTA_WRITE_ROTX_END); break;
    case GGV_ROTX_OFFSET: c->hand->rotxoffset = value.f; attack_store(NTA_WRITE_ROTX_OFFSET); break;
    case GGV_USE_POSROT: c->hand->useposrot = value.b; attack_store(NTA_WRITE_USE_POS_ROT); break;
    case GGV_STATE_MINOR: c->hand->stateminor = value.i; attack_store(NTA_WRITE_STATE_MINOR); break;
    case GGV_ANIMATION_LATCH: c->hand->unk0cc8_01 = value.b; attack_store(NTA_WRITE_ANIMATION_LATCH); break;
    case GGV_MATMOT2: c->hand->matmot2 = value.f; attack_store(NTA_WRITE_MATMOT2); break;
    case GGV_MATMOT1: c->hand->matmot1 = value.f; attack_store(NTA_WRITE_MATMOT1); break;
    case GGV_POS_START_X: c->hand->posstart.x = value.f; attack_store(NTA_WRITE_POS_START_X); break;
    case GGV_POS_START_Y: c->hand->posstart.y = value.f; attack_store(NTA_WRITE_POS_START_Y); break;
    case GGV_POS_START_Z: c->hand->posstart.z = value.f; attack_store(NTA_WRITE_POS_START_Z); break;
    case GGV_POS_END_X: c->hand->posend.x = value.f; attack_store(NTA_WRITE_POS_END_X); break;
    case GGV_POS_END_Y: c->hand->posend.y = value.f; attack_store(NTA_WRITE_POS_END_Y); break;
    case GGV_POS_END_Z: c->hand->posend.z = value.f; attack_store(NTA_WRITE_POS_END_Z); break;
    case GGV_POS_OFFSET_X: c->hand->posoffset.x = value.f; attack_store(NTA_WRITE_POS_OFFSET_X); break;
    case GGV_POS_OFFSET_Y: c->hand->posoffset.y = value.f; attack_store(NTA_WRITE_POS_OFFSET_Y); break;
    case GGV_POS_OFFSET_Z: c->hand->posoffset.z = value.f; attack_store(NTA_WRITE_POS_OFFSET_Z); break;
    case GGV_STATE_FLAGS: c->hand->stateflags = value.u; attack_store(NTA_WRITE_STATE_FLAGS); break;
    case GGV_STATE_VAR1: c->hand->statevar1 = value.i; attack_store(NTA_WRITE_STATE_VAR1); break;
    case GGV_SPOOL: c->hand->gs_float1 = value.f; attack_store(NTA_WRITE_SPOOL); break;
    case GGV_BURST_BULLETS: c->hand->burstbullets = value.i; attack_store(NTA_WRITE_BURST); break;
    default: break;
    }
}
static ggv_value bgunAttackCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunattackcontext *c = opaque; ggv_value v = {0};
    switch (service) {
    case GGV_RECOIL_TICKS: v.i = TICKS(a.i); break;
    case GGV_RECOIL_SIN: v.f = sinf(a.f); break;
    case GGV_RECOIL_COS: v.f = cosf(a.f); break;
    case GGV_RECOIL_TWEEN: v.f = modelTweenRotAxis(a.f, b.f, d.f); break;
    case GGV_RECOIL_AIM_X: v.f = func0f0b131c(c->handnum); break;
    case GGV_CAPTURE_DEFINITION: c->definition = c->info->definition; break;
    case GGV_MATRIX_X: mtx4LoadXRotation(a.f, b.ref); break;
    case GGV_MATRIX_TRANSLATION: mtx4SetTranslation(a.ref, b.ref); break;
    case GGV_MATRIX_IDENTITY: mtx4LoadIdentity(a.ref); break;
    case GGV_RESOLVE_FUNCTION: c->func = gsetGetWeaponFunction(&c->hand->gset); v.b = c->func != NULL; break;
    case GGV_OVERRIDE_GKEF: mainOverrideVariable("gkef", c->gkef); break;
    case GGV_FIRE_ANIMATION_START: bgunStartAnimation(a.ref, b.i, d.ref); break;
    case GGV_ATTACK_READY: v.b = bgun0f098a44(a.ref, b.i); break;
    case GGV_ATTACK_CONTROL: v.i = bgun0f09a3f8(c->hand, c->func); break;
    case GGV_ATTACK_COMMIT: bgun0f09a6f8(c->info, c->handnum, c->hand, c->func); break;
    case GGV_ATTACK_RECOVERY: v.b = bgun0f09aba4(c->hand, c->info, c->handnum, (struct weaponfunc_shoot *) c->func); break;
    case GGV_VIBRATION_MULTIPLY: func0f097b40(a.ref, b.ref, d.ref); break;
    case GGV_RESOLVE_GRAPH: c->graph = bgunGetHeldGraph(&c->hand->gset); v.b = c->graph != NULL; break;
    default: break;
    } return v;
}
static ggv_value bgunAttackCall4(void *opaque, ggv_service4 service, ggv_value a, ggv_value b, ggv_value c, ggv_value d)
{
    (void) opaque; ggv_value value = {0};
    if (service == GGV_VIBRATION_INTERPOLATE) func0f097b64(a.ref, b.ref, c.f, d.ref);
    return value;
}
/* GGV attack services end. */
#undef bgun0f09aba4
#define bgun0f09aba4 attack_connected_recovery_raw
bool bgun0f09aba4(struct hand *hand, struct handweaponinfo *info, s32 handnum, struct weaponfunc_shoot *func)
{
    struct bgunattackcontext context = {0};
    context.hand = hand; context.info = info; context.handnum = handnum;
    context.func = (struct weaponfunc *) func;
    ggv_host host = { &context, bgunAttackRead, bgunAttackWrite, bgunAttackCall };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_RECOIL_RECOVERY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: recoil recovery refused immutable program");
        return false;
    }
    return result;
}
#undef bgun0f09aba4
#define bgunTickIncAttackingShoot attack_connected_driver_raw
bool bgunTickIncAttackingShoot(struct handweaponinfo *info, s32 handnum, struct hand *hand)
{
    u32 var80070128 = attack_case->gkef_state;
    struct bgunattackcontext context = {0};
    context.hand = hand; context.info = info; context.handnum = handnum;
    context.gkef = &var80070128;
    ggv_host host = { &context, bgunAttackRead, bgunAttackWrite, bgunAttackCall, bgunAttackCall4 };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_ATTACK_SHOOT), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: attack driver refused immutable program");
        return false;
    }
    return result;
}
#undef bgunTickIncAttackingShoot
#undef bgun0f09a3f8
#undef bgun0f09a6f8

#undef hand
#undef handweaponinfo
#undef weapon
#undef weaponfunc
#undef weaponfunc_shoot
#undef weaponfunc_shootauto
#undef weapon_graph_held_function_t
#undef gset
#undef stateflags
#undef stateframes
#undef statevar1
#undef burstbullets
#undef gs_float1
#undef triggeron
#undef matmot1
#undef bgunGetHeldGraph
#undef gsetGetWeaponFunction
#undef mainOverrideVariable
#undef sinf
#undef cosf
#undef modelTweenRotAxis
#undef func0f0b131c
#undef mtx4LoadXRotation
#undef mtx4SetTranslation
#undef mtx4LoadIdentity
#undef bgunStartAnimation
#undef bgun0f098a44
#undef func0f097b64
#undef func0f097b40
#undef sysLogPrintf
#undef M_PI
#undef M_BADPI
#undef M_BADTAU
#undef WEAPON_SHOTGUN
#undef WEAPON_MAULER
#undef HANDANIMMODE_BUSY
#undef HANDSTATEFLAG_00000040
#undef HANDSTATEFLAG_00000080
#undef HANDSTATEMINOR_ATTACK_SHOOT_0
#undef HANDSTATEMINOR_ATTACK_SHOOT_1
#undef HANDSTATEMINOR_ATTACK_SHOOT_2
#undef TICKS

static bool attack_native_recovery_call(struct attack_hand *h,struct attack_info *info,s32 handnum,struct attack_shoot *func) {
    bool result=attack_native_recovery_raw(h,info,handnum,func);
    attack_record(NTA_RECOVERY,-1,handnum,info->weaponnum,func->base.type,result,0,0,0,0,0,0);return result;
}
static bool attack_connected_recovery_call(struct attack_hand *h,struct attack_info *info,s32 handnum,struct attack_shoot *func) {
    bool result=attack_connected_recovery_raw(h,info,handnum,func);
    attack_record(NTA_RECOVERY,-1,handnum,info->weaponnum,func->base.type,result,0,0,0,0,0,0);return result;
}

int32_t nativeShootingRecovery(native_shooting_attack_case *c) {
    struct attack_hand h;struct attack_info info;struct attack_weapon definition;s32 result;
    attack_import(c,&h,&info,&definition,0);
    result=attack_native_recovery_raw(&h,&info,c->shooting.handnum,&attack_function.base);attack_export();return result;
}

int32_t nativeGraphShootingRecovery(native_shooting_attack_case *c) {
    struct attack_hand h;struct attack_info info;struct attack_weapon definition;s32 result;
    attack_import(c,&h,&info,&definition,0);
    result=attack_connected_recovery_raw(&h,&info,c->shooting.handnum,&attack_function.base);attack_export();return result;
}

int32_t nativeShootingAttack(native_shooting_attack_case *c) {
    struct attack_hand h;struct attack_info info;struct attack_weapon definition;s32 result;
    attack_import(c,&h,&info,&definition,1);
    result=attack_native_driver_raw(&info,c->shooting.handnum,&h);attack_export();return result;
}

int32_t nativeGraphShootingAttack(native_shooting_attack_case *c) {
    struct attack_hand h;struct attack_info info;struct attack_weapon definition;s32 result;
    attack_import(c,&h,&info,&definition,1);
    result=attack_connected_driver_raw(&info,c->shooting.handnum,&h);attack_export();return result;
}

/* BEGIN complete throw oracle V8 */
struct throw_gset { uint8_t weaponnum,unk0639,unk063a,weaponfunc; };
struct throw_hand {
    struct throw_gset gset;
    s32 stateminor,statecycles,stateframes;
    int16_t primetimer60;
    bool triggeron,triggerreleased;
    uint8_t unk0cc8_01:1;
    int8_t firing;
    s32 attacktype,loadedammo[2];
};
struct throw_cmd { s32 token; };
struct throw_func {
    s32 type; int8_t ammoindex; struct throw_cmd *fire_animation; u32 flags;
};
struct throw_function { struct throw_func base;int16_t activatetime60;s32 recoverytime60; };
typedef struct {
    u32 flags; s32 ammo_slot;
    bool has_activation_time_ticks60,has_recovery_time_ticks60;
    s32 activation_time_ticks60,recovery_time_ticks60;
} throw_graph_t;
struct throw_gunctrl { uint8_t throwing:1; };
struct throw_player { struct throw_gunctrl gunctrl; };
static struct { struct throw_player *currentplayer; } throw_vars;
static struct throw_player throw_players[2];
static native_throw_case *throw_case;
static struct throw_hand *throw_passed_hand;
static struct throw_function throw_function_record;
static throw_graph_t throw_graph_record;
static struct throw_cmd throw_command={1};
static s32 throw_player_index(void) {
    return throw_vars.currentplayer==&throw_players[1]?1:0;
}
static void throw_export(void) {
    native_throw_case *c=throw_case;struct throw_hand *h=throw_passed_hand;
    c->weaponnum=h->gset.weaponnum;c->weaponfunc=h->gset.weaponfunc;
    c->minor=h->stateminor;c->cycles=h->statecycles;c->frames=h->stateframes;
    c->prime=h->primetimer60;c->trigger=h->triggeron;c->released=h->triggerreleased;
    c->latch=h->unk0cc8_01;c->firing=h->firing;c->attacktype=h->attacktype;
    c->loadedammo[0]=h->loadedammo[0];c->loadedammo[1]=h->loadedammo[1];
    c->current_player_index=throw_player_index();
    c->current_player_throwing[0]=throw_players[0].gunctrl.throwing;
    c->current_player_throwing[1]=throw_players[1].gunctrl.throwing;
}
void nativeThrowResetTrace(native_throw_case *c) { c->trace_count=0;c->trace_overflow=0; }
void nativeThrowRecordEvent(native_throw_case *c,s32 kind,s32 operation,s32 subject,const s32 scalar[4]) {
    native_throw_event *e;native_throw_snapshot *s;
    if(c->trace_count<0||c->trace_count>=NATIVE_THROW_TRACE_LIMIT){c->trace_overflow=1;return;}
    e=&c->trace[c->trace_count++];memset(e,0,sizeof *e);e->kind=kind;e->operation=operation;e->subject=subject;
    if(scalar)memcpy(e->scalar,scalar,sizeof e->scalar);s=&e->state;
    s->weaponnum=c->weaponnum;s->weaponfunc=c->weaponfunc;s->minor=c->minor;s->cycles=c->cycles;
    s->frames=c->frames;s->prime=c->prime;s->trigger=c->trigger;s->released=c->released;
    s->latch=c->latch;s->firing=c->firing;s->attacktype=c->attacktype;
    memcpy(s->loadedammo,c->loadedammo,sizeof s->loadedammo);
    s->current_player_index=c->current_player_index;
    memcpy(s->current_player_throwing,c->current_player_throwing,sizeof s->current_player_throwing);
}
static void throw_record(s32 operation,s32 a,s32 b,s32 d,s32 result) {
    s32 scalars[4]={a,b,d,result};throw_export();
    nativeThrowRecordEvent(throw_case,NT_EVENT_SERVICE,operation,-1,scalars);
}
static void throw_store(s32 operation,s32 subject) {
    throw_export();nativeThrowRecordEvent(throw_case,NT_EVENT_WRITE,operation,subject,0);
}
static void throw_import(native_throw_case *c,struct throw_hand *h) {
    throw_case=c;throw_passed_hand=h;memset(h,0,sizeof *h);
    h->gset.weaponnum=c->weaponnum;h->gset.weaponfunc=c->weaponfunc;
    h->stateminor=c->minor;h->statecycles=c->cycles;h->stateframes=c->frames;
    h->primetimer60=c->prime;h->triggeron=c->trigger;h->triggerreleased=c->released;
    h->unk0cc8_01=c->latch;h->firing=c->firing;h->attacktype=c->attacktype;
    memcpy(h->loadedammo,c->loadedammo,sizeof h->loadedammo);
    throw_players[0].gunctrl.throwing=c->current_player_throwing[0];
    throw_players[1].gunctrl.throwing=c->current_player_throwing[1];
    /* Qualified fixture requires current_player_index0/1 and selected ammo
     * negative or0/1. No malformed index/pointer clamp or gameplay refusal. */
    throw_vars.currentplayer=&throw_players[c->current_player_index];
    memset(&throw_function_record,0,sizeof throw_function_record);
    throw_function_record.base.ammoindex=c->legacy_ammo_index;
    throw_function_record.base.flags=c->legacy_flags;
    throw_function_record.base.fire_animation=c->fire_animation_present?&throw_command:0;
    throw_function_record.activatetime60=c->legacy_activation;
    throw_function_record.recoverytime60=c->legacy_recovery;
    throw_graph_record.flags=c->graph_flags;throw_graph_record.ammo_slot=c->graph_ammo_slot;
    throw_graph_record.has_activation_time_ticks60=c->graph_has_activation;
    throw_graph_record.has_recovery_time_ticks60=c->graph_has_recovery;
    throw_graph_record.activation_time_ticks60=c->graph_activation;
    throw_graph_record.recovery_time_ticks60=c->graph_recovery;
    throw_export();
}
static struct throw_func *throw_resolve_function(const struct throw_gset *set) {
    throw_record(NT_RESOLVE_FUNCTION,set->weaponnum,set->weaponfunc,0,!throw_case->function_absent);
    return throw_case->function_absent?0:&throw_function_record.base;
}
static const throw_graph_t *throw_resolve_graph(const struct throw_gset *set) {
    throw_record(NT_RESOLVE_GRAPH,set->weaponnum,set->weaponfunc,0,throw_case->graph_present!=0);
    return throw_case->graph_present?&throw_graph_record:0;
}
static void throw_remove_item(s32 weaponnum) { throw_record(NT_REMOVE_ITEM,weaponnum,0,0,0); }
static void throw_switch_previous(void) { throw_record(NT_SWITCH_PREVIOUS,0,0,0,0); }
static void throw_auto_switch(void) { throw_record(NT_AUTO_SWITCH,0,0,0,0); }
static void throw_animation(struct throw_cmd *cmd,s32 handnum,struct throw_hand *hand) {
    (void)hand;throw_record(NT_START_ANIMATION,cmd?cmd->token:0,handnum,0,0);
}
static bool throw_ready(struct throw_hand *hand,s32 time) {
    bool result=throw_case->ready_result;(void)hand;throw_record(NT_READY,time,0,0,result);return result;
}
static void throw_reset_animation(struct throw_hand *hand) {
    /* Real reset clears the modeled latch. Its seven other animation fields
     * remain outside this narrow fixture; this is not a complete callee. */
    hand->unk0cc8_01=false;throw_record(NT_RESET_ANIMATION,0,0,0,0);
}
static bool throw_secondary(void) {
    bool result=throw_case->secondary_result;throw_record(NT_SECONDARY,0,0,0,result);return result;
}
static void throw_log(s32 level,const char *message,...) {
    (void)level;(void)message;throw_case->program_refused=1;
}
#define hand throw_hand
#define weaponfunc_throw throw_function
#define weapon_graph_held_function_t throw_graph_t
#define g_Vars throw_vars
#define gsetGetWeaponFunction throw_resolve_function
#define bgunGetHeldGraph throw_resolve_graph
#define invRemoveItemByNum throw_remove_item
#define bgunSwitchToPrevious throw_switch_previous
#define bgunAutoSwitchWeapon throw_auto_switch
#define bgunStartAnimation throw_animation
#define bgun0f098a44 throw_ready
#define bgunResetAnim throw_reset_animation
#define bgunIsUsingSecondaryFunction throw_secondary
#define sysLogPrintf throw_log
#define FUNCFLAG_DISCARDWEAPON 0x40000u
#define HANDSTATEMINOR_ATTACK_THROW_0 0
#define HANDSTATEMINOR_ATTACK_THROW_1 1
#define HANDSTATEMINOR_ATTACK_THROW_2 2
#define HANDSTATEMINOR_ATTACK_THROW_GRENADEWAIT 55
#define HANDATTACKTYPE_THROWPROJECTILE 3
#define WEAPON_REMOTEMINE 34
#define WEAPON_GRENADE 30
#define FUNC_PRIMARY 0
#undef TICKS
#if PAL
#define TICKS(value) ((value)*50/60)
#else
#define TICKS(value) (value)
#endif

#define bgunGetAmmoIndexFromGraph throw_ammo_index
static s32 bgunGetAmmoIndexFromGraph(struct throw_func *func,
		const weapon_graph_held_function_t *graph)
{
	if (graph && graph->ammo_slot >= 0) {
		return graph->ammo_slot;
	}
	return func ? func->ammoindex : -1;
}
#define bgunTickIncAttackingThrow throw_native_raw
bool bgunTickIncAttackingThrow(s32 handnum, struct hand *hand)
{
	struct weaponfunc_throw *func = (struct weaponfunc_throw *) gsetGetWeaponFunction(&hand->gset);
	const weapon_graph_held_function_t *graph = bgunGetHeldGraph(&hand->gset);
	u32 funcflags;
	s32 ammoindex;
	s32 activationtime60;
	s32 recoverytime60;

	if (func == NULL) {
		return true;
	}

	funcflags = graph ? graph->flags : func->base.flags;
	ammoindex = bgunGetAmmoIndexFromGraph(&func->base, graph);
	activationtime60 = graph && graph->has_activation_time_ticks60 ?
		graph->activation_time_ticks60 : func->activatetime60;
	recoverytime60 = graph && graph->has_recovery_time_ticks60 ?
		graph->recovery_time_ticks60 : func->recoverytime60;

	if (hand->stateminor == HANDSTATEMINOR_ATTACK_THROW_0) {
		if (hand->statecycles == 0) {
			if (funcflags & FUNCFLAG_DISCARDWEAPON) {
				invRemoveItemByNum(hand->gset.weaponnum);
				g_Vars.currentplayer->gunctrl.throwing = true; throw_store(NT_WRITE_CURRENT_THROWING,throw_player_index());
#if VERSION >= VERSION_NTSC_1_0
				bgunSwitchToPrevious();
#else
				bgunAutoSwitchWeapon();
#endif
				hand->primetimer60 = 0; throw_store(NT_WRITE_PRIME,-1);
				return true;
			}

			if (func->base.fire_animation) {
				bgunStartAnimation(func->base.fire_animation, handnum, hand);
				hand->unk0cc8_01 = true; throw_store(NT_WRITE_LATCH,-1);
			}
		}

		if (func->base.fire_animation) {
			if (hand->triggerreleased) {
				hand->unk0cc8_01 = false; throw_store(NT_WRITE_LATCH,-1);
			}

			if (bgun0f098a44(hand, 2)) {
				hand->stateminor = HANDSTATEMINOR_ATTACK_THROW_1; throw_store(NT_WRITE_MINOR,-1);
				hand->unk0cc8_01 = false; throw_store(NT_WRITE_LATCH,-1);
			}
		} else {
			hand->stateminor = HANDSTATEMINOR_ATTACK_THROW_1; throw_store(NT_WRITE_MINOR,-1);
		}
	}

	if (hand->stateminor == HANDSTATEMINOR_ATTACK_THROW_1) {
		hand->firing = true; throw_store(NT_WRITE_FIRING,-1);
		hand->attacktype = HANDATTACKTYPE_THROWPROJECTILE; throw_store(NT_WRITE_ATTACK_TYPE,-1);
		if (ammoindex >= 0) {
			hand->loadedammo[ammoindex]--; throw_store(NT_WRITE_LOADED_AMMO,ammoindex);
		}
		hand->stateminor = HANDSTATEMINOR_ATTACK_THROW_2; throw_store(NT_WRITE_MINOR,-1);
		return false;
	}

	if (hand->stateminor == HANDSTATEMINOR_ATTACK_THROW_2) {
		if (hand->stateframes > TICKS(recoverytime60)) {
			return true;
		}

		if (hand->gset.weaponnum == WEAPON_REMOTEMINE
				&& bgunIsUsingSecondaryFunction() == true
				&& hand->triggerreleased
				&& hand->triggeron) {
			return true;
		}

		return false;
	}

	// This state is only used after having a grenade explode in the player's
	// hand. It waits 4 seconds before finishing, which means the player won't
	// pull out another grenade until the flames have cleared.
	if (hand->stateminor == HANDSTATEMINOR_ATTACK_THROW_GRENADEWAIT) {
		bgunResetAnim(hand);

		if (hand->stateframes > TICKS(activationtime60 + 240)) {
			return true;
		}

		return false;
	}

	hand->primetimer60 = hand->stateframes; throw_store(NT_WRITE_PRIME,-1);

	// If held a grenade too long, force throw it and enter the wait state
	if (hand->gset.weaponnum == WEAPON_GRENADE
			&& hand->gset.weaponfunc == FUNC_PRIMARY
			&& hand->primetimer60 > TICKS(activationtime60)) {
		hand->firing = true; throw_store(NT_WRITE_FIRING,-1);
		hand->attacktype = HANDATTACKTYPE_THROWPROJECTILE; throw_store(NT_WRITE_ATTACK_TYPE,-1);
		if (ammoindex >= 0) {
			hand->loadedammo[ammoindex]--; throw_store(NT_WRITE_LOADED_AMMO,ammoindex);
		}
		hand->stateminor = HANDSTATEMINOR_ATTACK_THROW_GRENADEWAIT; throw_store(NT_WRITE_MINOR,-1);

		return false;
	}

	return false;
}

#undef bgunTickIncAttackingThrow
/* GGV throw services begin: synchronous borrowed throw context. */
struct bgunthrowcontext {
    struct hand *hand; s32 handnum; struct weaponfunc_throw *func;
    const weapon_graph_held_function_t *graph; s32 ammoindex;
};
static ggv_value bgunThrowRead(void *opaque, ggv_binding binding)
{
    struct bgunthrowcontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_THROW_GRAPH_HAS_ACTIVATION: v.b = c->graph->has_activation_time_ticks60; break;
    case GGV_THROW_GRAPH_ACTIVATION: v.i = c->graph->activation_time_ticks60; break;
    case GGV_THROW_LEGACY_ACTIVATION: v.i = c->func->activatetime60; break;
    case GGV_THROW_GRAPH_HAS_RECOVERY: v.b = c->graph->has_recovery_time_ticks60; break;
    case GGV_THROW_GRAPH_RECOVERY: v.i = c->graph->recovery_time_ticks60; break;
    case GGV_THROW_LEGACY_RECOVERY: v.i = c->func->recoverytime60; break;
    case GGV_PRIME_TIMER: v.i = c->hand->primetimer60; break;
    case GGV_CURRENT_PLAYER_THROWING: v.b = g_Vars.currentplayer->gunctrl.throwing; break;
    case GGV_WEAPON_NUMBER: v.i = c->hand->gset.weaponnum; break;
    case GGV_WEAPON_FUNCTION: v.i = c->hand->gset.weaponfunc; break;
    case GGV_VERSION_1_OR_LATER: v.b = VERSION >= VERSION_NTSC_1_0; break;
    case GGV_GRAPH_FLAGS: v.u = c->graph->flags; break;
    case GGV_LEGACY_FLAGS: v.u = c->func->base.flags; break;
    case GGV_GRAPH_AMMO_SLOT: v.i = c->graph->ammo_slot; break;
    case GGV_LEGACY_AMMO_INDEX: v.i = c->func->base.ammoindex; break;
    case GGV_AMMO_INDEX: v.i = c->ammoindex; break;
    case GGV_STATE_MINOR: v.i = c->hand->stateminor; break;
    case GGV_STATE_CYCLES: v.i = c->hand->statecycles; break;
    case GGV_STATE_FRAMES: v.i = c->hand->stateframes; break;
    case GGV_FIRE_ANIMATION: v.ref = c->func->base.fire_animation; break;
    case GGV_PASSED_HAND: v.ref = c->hand; break;
    case GGV_HAND_NUMBER: v.i = c->handnum; break;
    case GGV_TRIGGER_RELEASED: v.b = c->hand->triggerreleased; break;
    case GGV_TRIGGER: v.b = c->hand->triggeron; break;
    case GGV_LOADED_AMMO: v.i = c->hand->loadedammo[c->ammoindex]; break;
    default: break;
    } return v;
}
static void bgunThrowWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunthrowcontext *c = opaque;
    switch (binding) {
    case GGV_AMMO_INDEX: c->ammoindex = value.i; break;
    case GGV_STATE_MINOR: c->hand->stateminor = value.i; throw_store(NT_WRITE_MINOR,-1); break;
    case GGV_ANIMATION_LATCH: c->hand->unk0cc8_01 = value.b; throw_store(NT_WRITE_LATCH,-1); break;
    case GGV_FIRING: c->hand->firing = value.b; throw_store(NT_WRITE_FIRING,-1); break;
    case GGV_ATTACK_TYPE: c->hand->attacktype = value.i; throw_store(NT_WRITE_ATTACK_TYPE,-1); break;
    case GGV_LOADED_AMMO: c->hand->loadedammo[c->ammoindex] = value.i; throw_store(NT_WRITE_LOADED_AMMO,c->ammoindex); break;
    case GGV_PRIME_TIMER: c->hand->primetimer60 = value.i; throw_store(NT_WRITE_PRIME,-1); break;
    case GGV_CURRENT_PLAYER_THROWING: g_Vars.currentplayer->gunctrl.throwing = value.b; throw_store(NT_WRITE_CURRENT_THROWING,throw_player_index()); break;
    default: break;
    }
}
static ggv_value bgunThrowCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunthrowcontext *c = opaque; ggv_value value = {0};
    switch (service) {
    case GGV_RESOLVE_FUNCTION: c->func = (struct weaponfunc_throw *) gsetGetWeaponFunction(&c->hand->gset); value.b = c->func != NULL; break;
    case GGV_RESOLVE_GRAPH: c->graph = bgunGetHeldGraph(&c->hand->gset); value.b = c->graph != NULL; break;
    case GGV_THROW_REMOVE_ITEM: invRemoveItemByNum(a.i); break;
    case GGV_THROW_SWITCH_PREVIOUS: bgunSwitchToPrevious(); break;
    case GGV_THROW_AUTO_SWITCH: bgunAutoSwitchWeapon(); break;
    case GGV_THROW_RESET_ANIMATION: bgunResetAnim(a.ref); break;
    case GGV_THROW_SECONDARY_VALUE: value.i = bgunIsUsingSecondaryFunction(); break;
    case GGV_FIRE_ANIMATION_START: bgunStartAnimation(a.ref, b.i, d.ref); break;
    case GGV_ATTACK_READY: value.b = bgun0f098a44(a.ref, b.i); break;
    case GGV_RECOIL_TICKS: value.i = TICKS(a.i); break;
    default: break;
    } return value;
}
/* GGV throw services end. */
#define bgunTickIncAttackingThrow throw_connected_raw
bool bgunTickIncAttackingThrow(s32 handnum, struct hand *hand)
{
    struct bgunthrowcontext context = {0};
    context.hand = hand; context.handnum = handnum;
    ggv_host host = { &context, bgunThrowRead, bgunThrowWrite, bgunThrowCall };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_THROW_CONTROL), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: throw driver refused immutable program");
        return false;
    }
    return result;
}
#undef bgunTickIncAttackingThrow
#undef bgunGetAmmoIndexFromGraph
#undef hand
#undef weaponfunc_throw
#undef weapon_graph_held_function_t
#undef g_Vars
#undef gsetGetWeaponFunction
#undef bgunGetHeldGraph
#undef invRemoveItemByNum
#undef bgunSwitchToPrevious
#undef bgunAutoSwitchWeapon
#undef bgunStartAnimation
#undef bgun0f098a44
#undef bgunResetAnim
#undef bgunIsUsingSecondaryFunction
#undef sysLogPrintf
#undef FUNCFLAG_DISCARDWEAPON
#undef HANDSTATEMINOR_ATTACK_THROW_0
#undef HANDSTATEMINOR_ATTACK_THROW_1
#undef HANDSTATEMINOR_ATTACK_THROW_2
#undef HANDSTATEMINOR_ATTACK_THROW_GRENADEWAIT
#undef HANDATTACKTYPE_THROWPROJECTILE
#undef WEAPON_REMOTEMINE
#undef WEAPON_GRENADE
#undef FUNC_PRIMARY
#undef TICKS

int32_t nativeThrowControl(native_throw_case *c) {
    struct throw_hand hand;s32 result;throw_import(c,&hand);
    result=throw_native_raw(c->handnum,&hand);throw_export();return result;
}

int32_t nativeGraphThrowControl(native_throw_case *c) {
    struct throw_hand hand;s32 result;throw_import(c,&hand);
    result=throw_connected_raw(c->handnum,&hand);throw_export();return result;
}

/* BEGIN complete Melee/MinClip oracle V9 */
struct melee_gset { uint8_t weaponnum,unk0639,unk063a,weaponfunc; };
struct melee_hand {
    struct melee_gset gset;
    s32 stateminor,statecycles,stateframes,burstbullets;
    bool triggeron,triggerreleased;
    uint8_t unk0cc8_01:1;
    int8_t firing;
    s32 attacktype,loadedammo[2],animmode;
    f32 matmot2;
};
struct melee_cmd { s32 token; };
struct melee_func { s32 type;int8_t ammoindex;struct melee_cmd *fire_animation;u32 flags; };
typedef struct { s32 ammo_slot; } melee_graph_t;
static native_melee_case *melee_case;
static struct melee_hand *melee_passed_hand;
static struct melee_func melee_function_record;
static melee_graph_t melee_graph_record;
static struct melee_cmd melee_command={1};
static s32 melee_utility_refused;
static u32 melee_float_bits(f32 value) {u32 bits;memcpy(&bits,&value,sizeof bits);return bits;}
static s32 melee_bits_as_i(u32 bits) {s32 value;memcpy(&value,&bits,sizeof value);return value;}
static void melee_export(void) {
    native_melee_case *c=melee_case;struct melee_hand *h=melee_passed_hand;
    c->weaponnum=h->gset.weaponnum;c->weaponfunc=h->gset.weaponfunc;
    c->minor=h->stateminor;c->cycles=h->statecycles;c->frames=h->stateframes;c->burst=h->burstbullets;
    c->trigger=h->triggeron;c->released=h->triggerreleased;c->latch=h->unk0cc8_01;
    c->firing=h->firing;c->attacktype=h->attacktype;c->motor=h->matmot2;c->animmode=h->animmode;
    memcpy(c->loadedammo,h->loadedammo,sizeof c->loadedammo);
}
void nativeMeleeResetTrace(native_melee_case *c) {c->trace_count=0;c->trace_overflow=0;}
void nativeMeleeRecordEvent(native_melee_case *c,s32 kind,s32 operation,s32 subject,const s32 scalar[4]) {
    native_melee_event *e;native_melee_snapshot *s;
    if(c->trace_count<0||c->trace_count>=NATIVE_MELEE_TRACE_LIMIT){c->trace_overflow=1;return;}
    e=&c->trace[c->trace_count++];memset(e,0,sizeof *e);e->kind=kind;e->operation=operation;e->subject=subject;
    if(scalar)memcpy(e->scalar,scalar,sizeof e->scalar);s=&e->state;
    s->weaponnum=c->weaponnum;s->weaponfunc=c->weaponfunc;s->minor=c->minor;s->cycles=c->cycles;
    s->frames=c->frames;s->burst=c->burst;s->trigger=c->trigger;s->released=c->released;
    s->latch=c->latch;s->firing=c->firing;s->attacktype=c->attacktype;s->animmode=c->animmode;
    memcpy(s->loadedammo,c->loadedammo,sizeof s->loadedammo);s->motor_bits=melee_float_bits(c->motor);
}
static void melee_record(s32 operation,s32 a,s32 b,s32 d,s32 result) {
    s32 scalars[4]={a,b,d,result};melee_export();
    nativeMeleeRecordEvent(melee_case,NM_EVENT_SERVICE,operation,-1,scalars);
}
static void melee_store(s32 operation,s32 subject) {
    melee_export();nativeMeleeRecordEvent(melee_case,NM_EVENT_WRITE,operation,subject,0);
}
static void melee_import(native_melee_case *c,struct melee_hand *h) {
    melee_case=c;melee_passed_hand=h;melee_utility_refused=0;memset(h,0,sizeof *h);
    h->gset.weaponnum=c->weaponnum;h->gset.weaponfunc=c->weaponfunc;
    h->stateminor=c->minor;h->statecycles=c->cycles;h->stateframes=c->frames;h->burstbullets=c->burst;
    h->triggeron=c->trigger;h->triggerreleased=c->released;h->unk0cc8_01=c->latch;
    h->firing=c->firing;h->attacktype=c->attacktype;h->matmot2=c->motor;h->animmode=c->animmode;
    memcpy(h->loadedammo,c->loadedammo,sizeof h->loadedammo);
    memset(&melee_function_record,0,sizeof melee_function_record);
    melee_function_record.ammoindex=c->legacy_ammo_index;
    melee_function_record.fire_animation=c->fire_animation_present?&melee_command:0;
    melee_graph_record.ammo_slot=c->graph_ammo_slot;
    /* Qualified ammo is negative or0/1, handnum0/1; ready cursor/count in0..8.
     * Invalid witnesses/indices and C signed-overflow inputs are not clamped. */
    melee_export();
}
static struct melee_func *melee_resolve_function(const struct melee_gset *set) {
    melee_record(NM_RESOLVE_FUNCTION,set->weaponnum,set->weaponfunc,0,!melee_case->function_absent);
    return melee_case->function_absent?0:&melee_function_record;
}
static const melee_graph_t *melee_resolve_graph(const struct melee_gset *set) {
    melee_record(NM_RESOLVE_GRAPH,set->weaponnum,set->weaponfunc,0,melee_case->graph_present!=0);
    return melee_case->graph_present?&melee_graph_record:0;
}
static void melee_animation(struct melee_cmd *cmd,s32 handnum,struct melee_hand *hand) {
    (void)hand;melee_record(NM_START_ANIMATION,cmd?cmd->token:0,handnum,0,0);
}
static bool melee_ready(struct melee_hand *hand,s32 time) {
    s32 index=melee_case->ready_cursor;bool result=melee_case->ready_default;(void)hand;
    if(index<melee_case->ready_count) {result=melee_case->ready_values[index];melee_case->ready_cursor=index+1;}
    melee_record(NM_READY,time,0,0,result);return result;
}
static bool melee_cheat(s32 cheat) {
    bool result=melee_case->cheat_result;melee_record(NM_CHEAT_ACTIVE,cheat,0,0,result);return result;
}
static f32 melee_clock(void) {
    f32 result;
#if VERSION >= 3
    result=melee_case->clock60freal;
#else
    result=melee_case->clock60f;
#endif
    melee_record(NM_CLOCK,0,0,0,melee_bits_as_i(melee_float_bits(result)));return result;
}
static void melee_log(s32 level,const char *message,...) {
    (void)level;(void)message;melee_utility_refused=1;if(melee_case)melee_case->program_refused=1;
}
s32 melee_minclip_native_raw(s32 weaponnum,s32 funcnum);
s32 melee_minclip_connected_raw(s32 weaponnum,s32 funcnum);
static s32 melee_native_minclip_query(s32 weaponnum,s32 funcnum) {
    s32 result=melee_minclip_native_raw(weaponnum,funcnum);
    melee_record(NM_MIN_CLIP,weaponnum,funcnum,0,result);return result;
}
static s32 melee_graph_minclip_query(s32 weaponnum,s32 funcnum) {
    s32 result=melee_minclip_connected_raw(weaponnum,funcnum);
    melee_record(NM_MIN_CLIP,weaponnum,funcnum,0,result);return result;
}
static bool melee_busy_raw(struct melee_hand *hand);
static bool melee_busy(struct melee_hand *hand) {
    bool result=melee_busy_raw(hand);melee_record(NM_ANIM_BUSY,0,0,0,result);return result;
}
#define weapon_graph_held_function_t melee_graph_t
#define gsetGetWeaponFunction melee_resolve_function
#define bgunGetHeldGraph melee_resolve_graph
#define bgunStartAnimation melee_animation
#define bgun0f098a44 melee_ready
#define cheatIsActive melee_cheat
#define bgunIsAnimBusy melee_busy
#define sysLogPrintf melee_log
#define LVUPDATE60FREAL() melee_clock()
#define WEAPON_REAPER 20
#define WEAPON_TRANQUILIZER 28
#define WEAPON_UNARMED 1
#define FUNC_SECONDARY 1
#define CHEAT_HURRICANEFISTS 0
#define HANDATTACKTYPE_MELEE 4
#define HANDATTACKTYPE_MELEENOUNCLOAK 10
#define HANDSTATEMINOR_ATTACK_MELEE_0 0
#define HANDSTATEMINOR_ATTACK_MELEE_1 1
#define HANDSTATEMINOR_ATTACK_MELEE_2 2
#define HANDSTATEMINOR_ATTACK_MELEE_3 3
#define HANDANIMMODE_IDLE 0
#define TICKS(value) MELEE_TICKS(value)
#if PAL
#define MELEE_TICKS(value) ((value)*50/60)
#else
#define MELEE_TICKS(value) (value)
#endif

#undef bgunIsAnimBusy
#define bgunIsAnimBusy melee_busy_raw
bool bgunIsAnimBusy(struct melee_hand *hand)
{
	return hand->animmode != HANDANIMMODE_IDLE;
}
#undef bgunIsAnimBusy
#define bgunIsAnimBusy melee_busy

#define bgunGetAmmoIndexFromGraph melee_ammo_index
static s32 bgunGetAmmoIndexFromGraph(struct melee_func *func,
		const weapon_graph_held_function_t *graph)
{
	if (graph && graph->ammo_slot >= 0) {
		return graph->ammo_slot;
	}
	return func ? func->ammoindex : -1;
}
#define bgunGetMinClipQty melee_minclip_native_raw
s32 bgunGetMinClipQty(s32 weaponnum, s32 funcnum)
{
	if (weaponnum == WEAPON_TRANQUILIZER && funcnum == FUNC_SECONDARY) {
		return 4;
	}

	return 1;
}

#undef bgunGetMinClipQty
/* GGV min-clip services begin: pure borrowed scalar utility arguments. */
struct bgunminclipcontext { s32 weaponnum; s32 funcnum; };
static ggv_value bgunMinClipRead(void *opaque, ggv_binding binding)
{
    struct bgunminclipcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_WEAPON_NUMBER: value.i = c->weaponnum; break;
    case GGV_WEAPON_FUNCTION: value.i = c->funcnum; break;
    default: break;
    } return value;
}
/* GGV min-clip services end. */
static void bgunMinClipWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    (void) opaque; (void) binding; (void) value; /* Prepared utility has no stores. */
}
#define bgunGetMinClipQty melee_minclip_connected_raw
s32 bgunGetMinClipQty(s32 weaponnum, s32 funcnum)
{
    struct bgunminclipcontext context = { weaponnum, funcnum };
    ggv_host host = { &context, bgunMinClipRead, bgunMinClipWrite, NULL };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_MIN_CLIP_QTY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: min-clip utility refused immutable program");
        return 0;
    }
    return result;
}
#undef bgunGetMinClipQty

#define bgunGetMinClipQty melee_native_minclip_query
#define bgunTickIncAttackingMelee melee_native_raw
bool bgunTickIncAttackingMelee(s32 handnum, struct melee_hand *hand)
{
	struct melee_func *func = gsetGetWeaponFunction(&hand->gset);
	const weapon_graph_held_function_t *graph = bgunGetHeldGraph(&hand->gset);
	s32 ammoindex;

	if (func == NULL) {
		return true;
	}

	ammoindex = bgunGetAmmoIndexFromGraph(func, graph);

	if (hand->gset.weaponnum == WEAPON_REAPER) {
		if (hand->statecycles == 0) {
			hand->matmot2 = 0.1f; melee_store(NM_WRITE_MOTOR,-1);
			hand->burstbullets = 0; melee_store(NM_WRITE_BURST,-1);
		}

		hand->firing = true; melee_store(NM_WRITE_FIRING,-1);
		hand->attacktype = HANDATTACKTYPE_MELEE; melee_store(NM_WRITE_ATTACK_TYPE,-1);
		hand->burstbullets++; melee_store(NM_WRITE_BURST,-1);

		if (hand->triggeron) {
			hand->matmot2 += 0.01f * LVUPDATE60FREAL(); melee_store(NM_WRITE_MOTOR,-1);

			if (hand->matmot2 > 1) {
				hand->matmot2 = 1; melee_store(NM_WRITE_MOTOR,-1);
			}
		} else {
			hand->matmot2 = 0; melee_store(NM_WRITE_MOTOR,-1);
			return true;
		}

		return false;
	}

	if (hand->stateminor == HANDSTATEMINOR_ATTACK_MELEE_0) {
		if (hand->statecycles == 0) {
			hand->firing = true; melee_store(NM_WRITE_FIRING,-1);
			hand->attacktype = HANDATTACKTYPE_MELEENOUNCLOAK; melee_store(NM_WRITE_ATTACK_TYPE,-1);

			if (func->fire_animation) {
				bgunStartAnimation(func->fire_animation, handnum, hand);
				hand->unk0cc8_01 = true; melee_store(NM_WRITE_LATCH,-1);
			}
		}

		if (func->fire_animation) {
			if (hand->triggerreleased) {
				hand->unk0cc8_01 = false; melee_store(NM_WRITE_LATCH,-1);
			}

			if (bgun0f098a44(hand, 2)) {
				hand->stateminor = HANDSTATEMINOR_ATTACK_MELEE_1; melee_store(NM_WRITE_MINOR,-1);
				hand->unk0cc8_01 = false; melee_store(NM_WRITE_LATCH,-1);
			}
		} else {
			hand->stateminor = HANDSTATEMINOR_ATTACK_MELEE_1; melee_store(NM_WRITE_MINOR,-1);
		}
	}

	if (hand->stateminor == HANDSTATEMINOR_ATTACK_MELEE_3 && bgun0f098a44(hand, 3)) {
		hand->stateminor = HANDSTATEMINOR_ATTACK_MELEE_1; melee_store(NM_WRITE_MINOR,-1);
		hand->unk0cc8_01 = false; melee_store(NM_WRITE_LATCH,-1);
	}

	if (hand->stateminor == HANDSTATEMINOR_ATTACK_MELEE_1) {
		hand->firing = true; melee_store(NM_WRITE_FIRING,-1);
		hand->attacktype = HANDATTACKTYPE_MELEE; melee_store(NM_WRITE_ATTACK_TYPE,-1);

		if (hand->gset.weaponnum == WEAPON_TRANQUILIZER && ammoindex >= 0) {
			if (hand->loadedammo[ammoindex] > bgunGetMinClipQty(WEAPON_TRANQUILIZER, FUNC_SECONDARY)) {
				hand->loadedammo[ammoindex] -= bgunGetMinClipQty(WEAPON_TRANQUILIZER, FUNC_SECONDARY); melee_store(NM_WRITE_LOADED_AMMO,ammoindex);
			} else {
				hand->loadedammo[ammoindex] = 0; melee_store(NM_WRITE_LOADED_AMMO,ammoindex);
			}
		}

		if (func->fire_animation) {
			if (func->fire_animation && !bgun0f098a44(hand, 3)) {
				hand->stateminor = HANDSTATEMINOR_ATTACK_MELEE_3; melee_store(NM_WRITE_MINOR,-1);
			} else {
				hand->stateminor = HANDSTATEMINOR_ATTACK_MELEE_2; melee_store(NM_WRITE_MINOR,-1);
			}
		}

		if (cheatIsActive(CHEAT_HURRICANEFISTS)) {
			hand->stateminor = HANDSTATEMINOR_ATTACK_MELEE_2; melee_store(NM_WRITE_MINOR,-1);
		}

		return false;
	}

	if (hand->stateminor == HANDSTATEMINOR_ATTACK_MELEE_2) {
		if (!bgunIsAnimBusy(hand)) {
			return true;
		}

		if (cheatIsActive(CHEAT_HURRICANEFISTS) && hand->gset.weaponnum == WEAPON_UNARMED) {
			return true;
		}

		if (hand->stateframes > TICKS(60)) {
			return true;
		}

		return false;
	}

	return false;
}

#undef bgunGetMinClipQty
#undef bgunTickIncAttackingMelee
#define bgunGetMinClipQty melee_graph_minclip_query
/* GGV melee services begin: synchronous borrowed hand/configuration witnesses. */
struct bgunmeleecontext {
    struct melee_hand *hand; s32 handnum; struct melee_func *func;
    const weapon_graph_held_function_t *graph; s32 ammoindex;
};
static ggv_value bgunMeleeRead(void *opaque, ggv_binding binding)
{
    struct bgunmeleecontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_GRAPH_AMMO_SLOT: value.i = c->graph->ammo_slot; break;
    case GGV_LEGACY_AMMO_INDEX: value.i = c->func->ammoindex; break;
    case GGV_AMMO_INDEX: value.i = c->ammoindex; break;
    case GGV_WEAPON_NUMBER: value.i = c->hand->gset.weaponnum; break;
    case GGV_STATE_MINOR: value.i = c->hand->stateminor; break;
    case GGV_STATE_CYCLES: value.i = c->hand->statecycles; break;
    case GGV_STATE_FRAMES: value.i = c->hand->stateframes; break;
    case GGV_MATMOT2: value.f = c->hand->matmot2; break;
    case GGV_BURST_BULLETS: value.i = c->hand->burstbullets; break;
    case GGV_TRIGGER: value.b = c->hand->triggeron; break;
    case GGV_TRIGGER_RELEASED: value.b = c->hand->triggerreleased; break;
    case GGV_FIRE_ANIMATION: value.ref = c->func->fire_animation; break;
    case GGV_PASSED_HAND: value.ref = c->hand; break;
    case GGV_HAND_NUMBER: value.i = c->handnum; break;
    case GGV_LOADED_AMMO: value.i = c->hand->loadedammo[c->ammoindex]; break;
    default: break;
    } return value;
}
static void bgunMeleeWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunmeleecontext *c = opaque;
    switch (binding) {
    case GGV_AMMO_INDEX: c->ammoindex = value.i; break;
    case GGV_MATMOT2: c->hand->matmot2 = value.f; melee_store(NM_WRITE_MOTOR,-1); break;
    case GGV_BURST_BULLETS: c->hand->burstbullets = value.i; melee_store(NM_WRITE_BURST,-1); break;
    case GGV_FIRING: c->hand->firing = value.b; melee_store(NM_WRITE_FIRING,-1); break;
    case GGV_ATTACK_TYPE: c->hand->attacktype = value.i; melee_store(NM_WRITE_ATTACK_TYPE,-1); break;
    case GGV_STATE_MINOR: c->hand->stateminor = value.i; melee_store(NM_WRITE_MINOR,-1); break;
    case GGV_ANIMATION_LATCH: c->hand->unk0cc8_01 = value.b; melee_store(NM_WRITE_LATCH,-1); break;
    case GGV_LOADED_AMMO: c->hand->loadedammo[c->ammoindex] = value.i; melee_store(NM_WRITE_LOADED_AMMO,c->ammoindex); break;
    default: break;
    }
}
static ggv_value bgunMeleeCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunmeleecontext *c = opaque; ggv_value value = {0};
    switch (service) {
    case GGV_RESOLVE_FUNCTION: c->func = gsetGetWeaponFunction(&c->hand->gset); value.b = c->func != NULL; break;
    case GGV_RESOLVE_GRAPH: c->graph = bgunGetHeldGraph(&c->hand->gset); value.b = c->graph != NULL; break;
    case GGV_MELEE_MIN_CLIP: value.i = bgunGetMinClipQty(a.i, b.i); break;
    case GGV_MELEE_CLOCK: value.f = LVUPDATE60FREAL(); break;
    case GGV_MELEE_CHEAT_ACTIVE: value.b = cheatIsActive(a.i); break;
    case GGV_MELEE_ANIM_BUSY: value.b = bgunIsAnimBusy(a.ref); break;
    case GGV_FIRE_ANIMATION_START: bgunStartAnimation(a.ref, b.i, d.ref); break;
    case GGV_ATTACK_READY: value.b = bgun0f098a44(a.ref, b.i); break;
    case GGV_RECOIL_TICKS: value.i = TICKS(a.i); break;
    default: break;
    } return value;
}
/* GGV melee services end. */
#define bgunTickIncAttackingMelee melee_connected_raw
bool bgunTickIncAttackingMelee(s32 handnum, struct melee_hand *hand)
{
    struct bgunmeleecontext context = {0};
    context.hand = hand; context.handnum = handnum;
    ggv_host host = { &context, bgunMeleeRead, bgunMeleeWrite, bgunMeleeCall };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_MELEE_CONTROL), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: melee driver refused immutable program");
        return false;
    }
    return result;
}
#undef bgunTickIncAttackingMelee
#undef bgunGetMinClipQty
#undef bgunGetAmmoIndexFromGraph
#undef weapon_graph_held_function_t
#undef gsetGetWeaponFunction
#undef bgunGetHeldGraph
#undef bgunStartAnimation
#undef bgun0f098a44
#undef cheatIsActive
#undef bgunIsAnimBusy
#undef sysLogPrintf
#undef LVUPDATE60FREAL
#undef WEAPON_REAPER
#undef WEAPON_TRANQUILIZER
#undef WEAPON_UNARMED
#undef FUNC_SECONDARY
#undef CHEAT_HURRICANEFISTS
#undef HANDATTACKTYPE_MELEE
#undef HANDATTACKTYPE_MELEENOUNCLOAK
#undef HANDSTATEMINOR_ATTACK_MELEE_0
#undef HANDSTATEMINOR_ATTACK_MELEE_1
#undef HANDSTATEMINOR_ATTACK_MELEE_2
#undef HANDSTATEMINOR_ATTACK_MELEE_3
#undef HANDANIMMODE_IDLE
#undef TICKS
#undef MELEE_TICKS

int32_t nativeMeleeControl(native_melee_case *c) {struct melee_hand hand;s32 result;melee_import(c,&hand);result=melee_native_raw(c->handnum,&hand);melee_export();return result;}
int32_t nativeGraphMeleeControl(native_melee_case *c) {struct melee_hand hand;s32 result;melee_import(c,&hand);result=melee_connected_raw(c->handnum,&hand);melee_export();return result;}
int32_t nativeMinClipControl(native_minclip_case *c) {melee_case=0;melee_utility_refused=0;c->result=melee_minclip_native_raw(c->weaponnum,c->funcnum);c->program_refused=melee_utility_refused;return c->result;}
int32_t nativeGraphMinClipControl(native_minclip_case *c) {melee_case=0;melee_utility_refused=0;c->result=melee_minclip_connected_raw(c->weaponnum,c->funcnum);c->program_refused=melee_utility_refused;return c->result;}

/* BEGIN complete Special/Empty oracle V10 */
struct se_gset {uint8_t weaponnum,unk0639,unk063a,weaponfunc;};
struct se_hand {
    struct se_gset gset;s32 state,stateminor,stateframes,statecycles,statelastframe;
    bool triggeron;int8_t firing;s32 attacktype,loadedammo[2],animmode,count60,count;
    uint8_t unk0cc8_01:1;u32 stateflags,mode;
};
struct se_cmd {s32 token;};
struct se_func {s32 type;int8_t ammoindex;struct se_cmd *fire_animation;u32 flags;};
struct se_special_func {struct se_func base;s32 specialfunc,recoverytime60;};
typedef struct {s32 ammo_slot;bool has_specialfunc,has_recovery_time_ticks60;s32 specialfunc,recovery_time_ticks60;} se_graph_t;
struct se_info {s32 weaponnum;void *definition;};
struct se_sound {s32 token;};
static native_special_empty_case *se_case;
static struct se_hand *se_passed_hand;
static struct se_hand se_current_hands[2];
static struct se_info *se_info_ptr;
static struct se_special_func se_function_record;
static se_graph_t se_graph_record;
static struct se_cmd se_command={1};
static s32 se_definition;static u32 se_audio_bank;
static struct {s32 thread;} se_audio_manager;
static struct se_sound se_sounds[NATIVE_SE_SOUND_LIMIT];
static u32 se_float_bits(f32 f) {u32 v;memcpy(&v,&f,sizeof v);return v;}
static s32 se_bits_i(u32 u) {s32 v;memcpy(&v,&u,sizeof v);return v;}
static void se_export_hand(native_se_hand_state *s,const struct se_hand *h) {
    s->weaponnum=h->gset.weaponnum;s->weaponfunc=h->gset.weaponfunc;s->state=h->state;
    s->minor=h->stateminor;s->frames=h->stateframes;s->cycles=h->statecycles;s->lastframe=h->statelastframe;
    s->trigger=h->triggeron;s->firing=h->firing;s->attacktype=h->attacktype;
    memcpy(s->loadedammo,h->loadedammo,sizeof s->loadedammo);s->animmode=h->animmode;s->latch=h->unk0cc8_01;
    s->count60=h->count60;s->count=h->count;s->flags=h->stateflags;s->mode=h->mode;
}
static void se_export(void) {
    se_export_hand(&se_case->passed,se_passed_hand);
    se_export_hand(&se_case->current_hands[0],&se_current_hands[0]);
    se_export_hand(&se_case->current_hands[1],&se_current_hands[1]);se_case->info_weapon=se_info_ptr->weaponnum;
}
void nativeSpecialEmptyResetTrace(native_special_empty_case *c) {c->trace_count=0;c->trace_overflow=0;}
void nativeSpecialEmptyRecordEvent(native_special_empty_case *c,s32 kind,s32 operation,s32 subject,const s32 scalar[10]) {
    native_se_event *e;
    if(c->trace_count<0||c->trace_count>=NATIVE_SE_TRACE_LIMIT){c->trace_overflow=1;return;}
    e=&c->trace[c->trace_count++];memset(e,0,sizeof *e);e->kind=kind;e->operation=operation;e->subject=subject;
    if(scalar)memcpy(e->scalar,scalar,sizeof e->scalar);
    e->state.passed=c->passed;memcpy(e->state.current_hands,c->current_hands,sizeof e->state.current_hands);
    e->state.info_weapon=c->info_weapon;e->state.priority=c->priority;
}
static void se_record(s32 op,s32 a,s32 b,s32 d,s32 result) {
    s32 scalar[10]={a,b,d,result};se_export();nativeSpecialEmptyRecordEvent(se_case,NSE_EVENT_SERVICE,op,-1,scalar);
}
static void se_store(s32 op,s32 subject) {se_export();nativeSpecialEmptyRecordEvent(se_case,NSE_EVENT_WRITE,op,subject,0);}
static void se_import_hand(struct se_hand *h,const native_se_hand_state *s) {
    memset(h,0,sizeof *h);h->gset.weaponnum=s->weaponnum;h->gset.weaponfunc=s->weaponfunc;h->state=s->state;
    h->stateminor=s->minor;h->stateframes=s->frames;h->statecycles=s->cycles;h->statelastframe=s->lastframe;
    h->triggeron=s->trigger;h->firing=s->firing;h->attacktype=s->attacktype;memcpy(h->loadedammo,s->loadedammo,sizeof h->loadedammo);
    h->animmode=s->animmode;h->unk0cc8_01=s->latch;h->count60=s->count60;h->count=s->count;h->stateflags=s->flags;h->mode=s->mode;
}
static void se_import(native_special_empty_case *c,struct se_hand *detached,struct se_info *info) {
    se_case=c;se_info_ptr=info;se_import_hand(&se_current_hands[0],&c->current_hands[0]);se_import_hand(&se_current_hands[1],&c->current_hands[1]);
    /* Qualified passed index-1/0/1,handnum0/1,ammo negative or0/1,sound cursor
     * plus at most2 starts<=4,tokens0..4. No native invalid-domain repair. */
    se_passed_hand=c->passed_hand_index<0?detached:&se_current_hands[c->passed_hand_index];se_import_hand(se_passed_hand,&c->passed);
    info->weaponnum=c->info_weapon;info->definition=c->definition_present?&se_definition:0;
    memset(&se_function_record,0,sizeof se_function_record);se_function_record.base.ammoindex=c->legacy_ammo_index;
    se_function_record.base.fire_animation=c->fire_animation_present?&se_command:0;
    se_function_record.specialfunc=c->legacy_special;se_function_record.recoverytime60=c->legacy_recovery;
    se_graph_record.ammo_slot=c->graph_ammo_slot;se_graph_record.has_specialfunc=c->graph_has_special;
    se_graph_record.specialfunc=c->graph_special;se_graph_record.has_recovery_time_ticks60=c->graph_has_recovery;
    se_graph_record.recovery_time_ticks60=c->graph_recovery;se_audio_bank=c->audio_bank;
    for(s32 i=0;i<NATIVE_SE_SOUND_LIMIT;++i)se_sounds[i].token=i+1;se_export();
}
static struct se_func *se_resolve_function(const struct se_gset *g) {
    se_record(NSE_RESOLVE_FUNCTION,g->weaponnum,g->weaponfunc,0,!se_case->function_absent);
    return se_case->function_absent?0:&se_function_record.base;
}
static const se_graph_t *se_resolve_graph(const struct se_gset *g) {
    se_record(NSE_RESOLVE_GRAPH,g->weaponnum,g->weaponfunc,0,se_case->graph_present!=0);return se_case->graph_present?&se_graph_record:0;
}
static void se_reset(struct se_hand *h) {
    /* Only two accessed outcomes of the eight-store native reset. Six other
     * fields and real animation lifecycle remain unqualified. */
    h->animmode=0;h->unk0cc8_01=false;se_record(NSE_RESET_ANIMATION,0,0,0,0);
}
static void se_animation(struct se_cmd *cmd,s32 handnum,struct se_hand *h) {
    (void)h;se_record(NSE_START_ANIMATION,cmd?cmd->token:0,handnum,0,0);
}
static bool se_ready(struct se_hand *h,s32 time) {
    bool result=se_case->ready_result;(void)h;se_record(NSE_READY,time,0,0,result);return result;
}
static OSPri se_get_priority(void *thread) {
    s32 audio=thread==&se_audio_manager.thread;OSPri result=audio?se_case->audio_priority:se_case->priority;
    se_record(audio?NSE_AUDIO_PRIORITY:NSE_CURRENT_PRIORITY,audio,0,0,result);return result;
}
static void se_set_priority(void *thread,OSPri value) {
    s32 audio=thread==&se_audio_manager.thread;if(audio)se_case->audio_priority=value;else se_case->priority=value;
    se_record(NSE_SET_PRIORITY,audio,value,0,0);
}
static struct se_sound *se_sound_start(s32 bank,int16_t sound,struct se_sound **out,s32 volume,s32 pan,f32 pitch,s32 fxbus,s32 fxmix) {
    s32 token=se_case->sound_tokens[se_case->sound_cursor++];struct se_sound *result=token?&se_sounds[token-1]:0;
    s32 scalar[10]={bank,sound,out!=0,volume,pan,se_bits_i(se_float_bits(pitch)),fxbus,fxmix,token,0};
    /* Original calls pass NULL; non-NULL out behavior is not qualified here. */
    se_export();nativeSpecialEmptyRecordEvent(se_case,NSE_EVENT_SERVICE,NSE_SOUND_START,-1,scalar);return result;
}
static void se_sound_event(struct se_sound *handle,int16_t type,s32 pitch) {se_record(NSE_SOUND_EVENT,handle->token,type,pitch,0);}
static bool se_set_state(s32 handnum,s32 state) {
    bool result=se_case->setstate_result;struct se_hand *h=&se_current_hands[handnum];
    /* Supplied result (false is synthetic for IDLE). Accepted six stores are
     * native-source-bound fixture effects; engine query/logging are absent. */
    if(result){h->state=state;h->stateframes=0;h->stateflags=0;h->statecycles=0;h->stateminor=0;h->statelastframe=0;}
    s32 scalar[10]={handnum,state,0,result};se_export();nativeSpecialEmptyRecordEvent(se_case,NSE_EVENT_SERVICE,NSE_SET_STATE,handnum,scalar);return result;
}
static void se_log(s32 level,const char *message,...) {(void)level;(void)message;se_case->program_refused=1;}
#define weapon_graph_held_function_t se_graph_t
#define gsetGetWeaponFunction se_resolve_function
#define bgunGetHeldGraph se_resolve_graph
#define bgunResetAnim se_reset
#define bgunStartAnimation se_animation
#define bgun0f098a44 se_ready
#define bgunSetState se_set_state
#define osGetThreadPri se_get_priority
#define osSetThreadPri se_set_priority
#define sndStart se_sound_start
#define audioPostEvent se_sound_event
#define g_AudioManager se_audio_manager
#define var80095200 se_audio_bank
#define sysLogPrintf se_log
#define HANDSTATEMINOR_ATTACK_SPECIAL_START 0
#define HANDSTATEMINOR_ATTACK_SPECIAL_EXECUTE 1
#define HANDSTATEMINOR_ATTACK_SPECIAL_RECOVER 2
#define HANDMODE_13 13
#define HANDMODE_NONE 0
#define HANDANIMMODE_BUSY 2
#define HANDSTATE_IDLE 0
#undef TICKS
#if PAL
#define TICKS(value) ((value)*50/60)
#else
#define TICKS(value) (value)
#endif

#define WEAPON_CALLISTO 12
#define WEAPON_CMP150 10
#define WEAPON_COMBATBOOST 35
#define WEAPON_COMBATKNIFE 26
#define WEAPON_CYCLONE 11
#define WEAPON_DY357LX 9
#define WEAPON_DY357MAGNUM 8
#define WEAPON_FALCON2 2
#define WEAPON_FALCON2_SCOPE 4
#define WEAPON_FALCON2_SILENCER 3
#define WEAPON_FARSIGHT 22
#define WEAPON_GRENADE 30
#define WEAPON_LAPTOPGUN 14
#define WEAPON_MAGSEC4 5
#define WEAPON_MAULER 6
#define WEAPON_NBOMB 31
#define WEAPON_PHOENIX 7
#define WEAPON_PROXIMITYMINE 33
#define WEAPON_PSYCHOSISGUN 36
#define WEAPON_RCP120 13
#define WEAPON_REAPER 20
#define WEAPON_REMOTEMINE 34
#define WEAPON_TIMEDMINE 32
#define WEAPON_TRANQUILIZER 28
#define WEAPON_UNARMED 1
#define SFX_FIREEMPTY 32850
#define SFX_HIT_WATER 32896

#define bgunGetAmmoIndexFromGraph se_ammo_index
static s32 bgunGetAmmoIndexFromGraph(struct se_func *func,
		const weapon_graph_held_function_t *graph)
{
	if (graph && graph->ammo_slot >= 0) {
		return graph->ammo_slot;
	}
	return func ? func->ammoindex : -1;
}
#define bgunTickIncAttackingSpecial se_special_native_raw
bool bgunTickIncAttackingSpecial(struct se_hand *hand)
{
	struct se_special_func *func = (struct se_special_func *) gsetGetWeaponFunction(&hand->gset);
	const weapon_graph_held_function_t *graph = bgunGetHeldGraph(&hand->gset);
	s32 specialfunc;
	s32 ammoindex;
	s32 recoverytime60;

	if (!func) {
		return true;
	}

	specialfunc = graph && graph->has_specialfunc ? graph->specialfunc : func->specialfunc;
	ammoindex = bgunGetAmmoIndexFromGraph(&func->base, graph);
	recoverytime60 = graph && graph->has_recovery_time_ticks60 ?
		graph->recovery_time_ticks60 : func->recoverytime60;

	if (hand->stateminor == HANDSTATEMINOR_ATTACK_SPECIAL_START) {
		hand->stateminor = HANDSTATEMINOR_ATTACK_SPECIAL_EXECUTE; se_store(NSE_WRITE_MINOR,-1);
	}

	if (hand->stateminor == HANDSTATEMINOR_ATTACK_SPECIAL_EXECUTE) {
		hand->firing = true; se_store(NSE_WRITE_FIRING,-1);
		hand->attacktype = specialfunc; se_store(NSE_WRITE_ATTACK_TYPE,-1);

		if (ammoindex >= 0) {
			hand->loadedammo[ammoindex]--; se_store(NSE_WRITE_LOADED_AMMO,ammoindex);
		}

		hand->stateminor = HANDSTATEMINOR_ATTACK_SPECIAL_RECOVER; se_store(NSE_WRITE_MINOR,-1);
		return false;
	}

	if (hand->stateminor == HANDSTATEMINOR_ATTACK_SPECIAL_RECOVER) {
		if (hand->stateframes > TICKS(recoverytime60)) {
			return true;
		}

		return false;
	}

	return false;
}

#undef bgunTickIncAttackingSpecial
/* GGV Special services begin: borrowed synchronous context. */
struct bgunspecialcontext { struct se_hand *hand; struct se_special_func *func; const weapon_graph_held_function_t *graph; s32 ammoindex; };
static ggv_value bgunSpecialRead(void *opaque, ggv_binding binding)
{
    struct bgunspecialcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_SPECIAL_GRAPH_HAS_VALUE: value.b = c->graph->has_specialfunc; break;
    case GGV_SPECIAL_GRAPH_VALUE: value.i = c->graph->specialfunc; break;
    case GGV_SPECIAL_LEGACY_VALUE: value.i = c->func->specialfunc; break;
    case GGV_SPECIAL_GRAPH_HAS_RECOVERY: value.b = c->graph->has_recovery_time_ticks60; break;
    case GGV_SPECIAL_GRAPH_RECOVERY: value.i = c->graph->recovery_time_ticks60; break;
    case GGV_SPECIAL_LEGACY_RECOVERY: value.i = c->func->recoverytime60; break;
    case GGV_GRAPH_AMMO_SLOT: value.i = c->graph->ammo_slot; break;
    case GGV_LEGACY_AMMO_INDEX: value.i = c->func->base.ammoindex; break;
    case GGV_AMMO_INDEX: value.i = c->ammoindex; break;
    case GGV_STATE_MINOR: value.i = c->hand->stateminor; break;
    case GGV_STATE_FRAMES: value.i = c->hand->stateframes; break;
    case GGV_LOADED_AMMO: value.i = c->hand->loadedammo[c->ammoindex]; break;
    default: break;
    } return value;
}
static void bgunSpecialWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunspecialcontext *c = opaque;
    switch (binding) {
    case GGV_AMMO_INDEX: c->ammoindex = value.i; break;
    case GGV_STATE_MINOR: c->hand->stateminor = value.i; se_store(NSE_WRITE_MINOR,-1); break;
    case GGV_FIRING: c->hand->firing = value.b; se_store(NSE_WRITE_FIRING,-1); break;
    case GGV_ATTACK_TYPE: c->hand->attacktype = value.i; se_store(NSE_WRITE_ATTACK_TYPE,-1); break;
    case GGV_LOADED_AMMO: c->hand->loadedammo[c->ammoindex] = value.i; se_store(NSE_WRITE_LOADED_AMMO,c->ammoindex); break;
    default: break;
    }
}
static ggv_value bgunSpecialCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunspecialcontext *c = opaque; ggv_value value = {0};
    switch (service) {
    case GGV_RESOLVE_FUNCTION: c->func = (struct se_special_func *) gsetGetWeaponFunction(&c->hand->gset); value.b = c->func != NULL; break;
    case GGV_RESOLVE_GRAPH: c->graph = bgunGetHeldGraph(&c->hand->gset); value.b = c->graph != NULL; break;
    case GGV_RECOIL_TICKS: value.i = TICKS(a.i); break;
    default: break;
    } return value;
}
/* GGV Special services end. */
#define bgunTickIncAttackingSpecial se_special_connected_raw
bool bgunTickIncAttackingSpecial(struct se_hand *hand)
{
    struct bgunspecialcontext context = {0}; context.hand = hand;
    ggv_host host = { &context, bgunSpecialRead, bgunSpecialWrite, bgunSpecialCall };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_SPECIAL_CONTROL), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: special driver refused immutable program");
        return false;
    }
    return result;
}
#undef bgunTickIncAttackingSpecial

#define bgunTickIncAttackEmpty se_empty_native_raw
s32 bgunTickIncAttackEmpty(struct se_info *info, s32 handnum, struct se_hand *hand, s32 lvupdate)
{
	u32 stack;
	bool playsound = false;

	switch (info->weaponnum) {
	case WEAPON_FALCON2:
	case WEAPON_FALCON2_SILENCER:
	case WEAPON_FALCON2_SCOPE:
	case WEAPON_MAGSEC4:
	case WEAPON_MAULER:
	case WEAPON_PHOENIX:
	case WEAPON_DY357MAGNUM:
	case WEAPON_DY357LX:
	case WEAPON_CMP150:
	case WEAPON_CYCLONE:
	case WEAPON_CALLISTO:
	case WEAPON_RCP120:
	case WEAPON_LAPTOPGUN:
	case WEAPON_REAPER:
	case WEAPON_TRANQUILIZER:
		// These weapons are weapons with visible finger trigger animations
		if (hand->stateframes > TICKS(25)) {
			hand->stateframes -= TICKS(25); se_store(NSE_WRITE_FRAMES,-1);
			hand->stateflags = 0; se_store(NSE_WRITE_FLAGS,-1);

			bgunResetAnim(hand);
		}

		if (hand->animmode != HANDANIMMODE_BUSY) {
			bool restartedanim = false;

			if ((hand->stateflags & HANDSTATEFLAG_00000010) == 0) {
				struct se_func *func = NULL;

				if (info->definition) {
					func = gsetGetWeaponFunction(&hand->gset);
				}

				if (func && func->fire_animation) {
					bgunStartAnimation(func->fire_animation, handnum, hand);
					restartedanim = true;
				}
			}

			if (!restartedanim && hand->stateframes > TICKS(25)) {
				playsound = true;
			}
		} else if (bgun0f098a44(hand, 5)) {
			playsound = true;
		}
		break;
	default:
		// Weapons without visible trigger animations must
		// still play the click sound every 25 frames
		if (hand->stateframes > TICKS(25)) {
			playsound = true;

			hand->stateframes -= TICKS(25); se_store(NSE_WRITE_FRAMES,-1);
			hand->stateflags = 0; se_store(NSE_WRITE_FLAGS,-1);

			bgunResetAnim(hand);
		}
	}

	hand->mode = HANDMODE_13; se_store(NSE_WRITE_MODE,-1);
	hand->count60 = 0; se_store(NSE_WRITE_COUNT60,-1);
	hand->count = 0; se_store(NSE_WRITE_COUNT,-1);

	if (playsound && (hand->stateflags & HANDSTATEFLAG_00000010) == 0) {
		hand->stateflags |= HANDSTATEFLAG_00000010; se_store(NSE_WRITE_FLAGS,-1);

		switch (info->weaponnum) {
		case WEAPON_PHOENIX:
		case WEAPON_CALLISTO:
		case WEAPON_FARSIGHT:
			{
				// Maian weapons have a wet sounding click effect
				f32 speed = 2.07f;

#if VERSION >= VERSION_NTSC_1_0
				OSPri prevpri = osGetThreadPri(0);
				struct se_sound *handle;
				osSetThreadPri(0, osGetThreadPri(&g_AudioManager.thread) + 1);
#else
				struct se_sound *handle;
#endif

				handle = sndStart(var80095200, SFX_HIT_WATER, NULL, -1, -1, -1, -1, -1);

				if (handle) {
					audioPostEvent(handle, AL_SNDP_PITCH_EVT, *(s32 *)&speed);
				}

#if VERSION >= VERSION_NTSC_1_0
				osSetThreadPri(0, prevpri);
#endif
			}
			// fall-through - unsure if intentional
		case WEAPON_TRANQUILIZER:
		case WEAPON_PSYCHOSISGUN:
			{
				// The tranquliser and psychosis gun use the standard click
				// effect but slightly faster.
				f32 speed = 1.5f;

#if VERSION >= VERSION_NTSC_1_0
				OSPri prevpri = osGetThreadPri(0);
				struct se_sound *handle;
				osSetThreadPri(0, osGetThreadPri(&g_AudioManager.thread) + 1);
#else
				struct se_sound *handle;
#endif

				handle = sndStart(var80095200, SFX_FIREEMPTY, NULL, -1, -1, -1, -1, -1);

				if (handle) {
					audioPostEvent(handle, AL_SNDP_PITCH_EVT, *(s32 *)&speed);
				}

#if VERSION >= VERSION_NTSC_1_0
				osSetThreadPri(0, prevpri);
#endif
			}
			break;
		case WEAPON_UNARMED:
		case WEAPON_COMBATKNIFE:
		case WEAPON_GRENADE:
		case WEAPON_NBOMB:
		case WEAPON_TIMEDMINE:
		case WEAPON_PROXIMITYMINE:
		case WEAPON_REMOTEMINE:
		case WEAPON_COMBATBOOST:
			// No sound effect
			break;
		default:
			// Default click sound effect
			sndStart(var80095200, SFX_FIREEMPTY, NULL, -1, -1, -1, -1, -1);
			break;
		}
	}

	// Handle releasing trigger
	if (!hand->triggeron) {
		hand->mode = HANDMODE_NONE; se_store(NSE_WRITE_MODE,-1);
		hand->count60 = 0; se_store(NSE_WRITE_COUNT60,-1);
		hand->count = 0; se_store(NSE_WRITE_COUNT,-1);

		if (bgunSetState(handnum, HANDSTATE_IDLE)) {
			return lvupdate;
		}

		bgunResetAnim(hand);
	}

	return 0;
}

#undef bgunTickIncAttackEmpty
/* GGV Empty services begin: borrowed synchronous context. */
struct bgunemptycontext { struct se_info *info; struct se_hand *hand; s32 handnum; s32 lvupdate; struct se_func *func; };
static ggv_value bgunEmptyRead(void *opaque, ggv_binding binding)
{
    struct bgunemptycontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_WEAPON_NUMBER: value.i = c->info->weaponnum; break;
    case GGV_STATE_FRAMES: value.i = c->hand->stateframes; break;
    case GGV_STATE_FLAGS: value.u = c->hand->stateflags; break;
    case GGV_EMPTY_ANIM_MODE: value.i = c->hand->animmode; break;
    case GGV_EMPTY_DEFINITION: value.ref = c->info->definition; break;
    case GGV_FIRE_ANIMATION: value.ref = c->func->fire_animation; break;
    case GGV_PASSED_HAND: value.ref = c->hand; break;
    case GGV_HAND_NUMBER: value.i = c->handnum; break;
    case GGV_EMPTY_LVUPDATE: value.i = c->lvupdate; break;
    case GGV_TRIGGER: value.b = c->hand->triggeron; break;
    case GGV_PRIORITY_ENABLED: value.b = VERSION >= VERSION_NTSC_1_0; break;
    case GGV_EMPTY_WATER_SOUND: value.i = SFX_HIT_WATER; break;
    case GGV_EMPTY_CLICK_SOUND: value.i = SFX_FIREEMPTY; break;
    default: break;
    } return value;
}
static void bgunEmptyWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunemptycontext *c = opaque;
    switch (binding) {
    case GGV_STATE_FRAMES: c->hand->stateframes = value.i; se_store(NSE_WRITE_FRAMES,-1); break;
    case GGV_STATE_FLAGS: c->hand->stateflags = value.u; se_store(NSE_WRITE_FLAGS,-1); break;
    case GGV_EMPTY_MODE: c->hand->mode = value.u; se_store(NSE_WRITE_MODE,-1); break;
    case GGV_EMPTY_COUNT60: c->hand->count60 = value.i; se_store(NSE_WRITE_COUNT60,-1); break;
    case GGV_EMPTY_COUNT: c->hand->count = value.i; se_store(NSE_WRITE_COUNT,-1); break;
    default: break;
    }
}
static ggv_value bgunEmptyCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunemptycontext *c = opaque; ggv_value value = {0};
    switch (service) {
    case GGV_RESOLVE_FUNCTION: c->func = gsetGetWeaponFunction(&c->hand->gset); value.b = c->func != NULL; break;
    case GGV_RECOIL_TICKS: value.i = TICKS(a.i); break;
    case GGV_EMPTY_RESET_ANIM: bgunResetAnim(a.ref); break;
    case GGV_FIRE_ANIMATION_START: bgunStartAnimation(a.ref, b.i, d.ref); break;
    case GGV_ATTACK_READY: value.b = bgun0f098a44(a.ref, b.i); break;
    case GGV_EMPTY_SOUND_START: value.ref = sndStart(var80095200, a.i, NULL, -1, -1, -1, -1, -1); break;
    case GGV_SOUND_EVENT: audioPostEvent(a.ref, AL_SNDP_PITCH_EVT, b.i); break;
    case GGV_CURRENT_PRIORITY: value.i = osGetThreadPri(0); break;
    case GGV_AUDIO_PRIORITY: value.i = osGetThreadPri(&g_AudioManager.thread); break;
    case GGV_SET_PRIORITY: osSetThreadPri(0, a.i); break;
    case GGV_EMPTY_SET_STATE: value.b = bgunSetState(a.i, b.i); break;
    default: break;
    } return value;
}
/* GGV Empty services end. */
#define bgunTickIncAttackEmpty se_empty_connected_raw
s32 bgunTickIncAttackEmpty(struct se_info *info, s32 handnum, struct se_hand *hand, s32 lvupdate)
{
    struct bgunemptycontext context = {0};
    context.info = info; context.hand = hand; context.handnum = handnum; context.lvupdate = lvupdate;
    ggv_host host = { &context, bgunEmptyRead, bgunEmptyWrite, bgunEmptyCall };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_EMPTY_ATTACK), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: empty driver refused immutable program");
        return 0;
    }
    return result;
}
#undef bgunTickIncAttackEmpty
#undef weapon_graph_held_function_t
#undef gsetGetWeaponFunction
#undef bgunGetHeldGraph
#undef bgunResetAnim
#undef bgunStartAnimation
#undef bgun0f098a44
#undef bgunSetState
#undef osGetThreadPri
#undef osSetThreadPri
#undef sndStart
#undef audioPostEvent
#undef g_AudioManager
#undef var80095200
#undef sysLogPrintf
#undef HANDSTATEMINOR_ATTACK_SPECIAL_START
#undef HANDSTATEMINOR_ATTACK_SPECIAL_EXECUTE
#undef HANDSTATEMINOR_ATTACK_SPECIAL_RECOVER
#undef HANDMODE_13
#undef HANDMODE_NONE
#undef HANDANIMMODE_BUSY
#undef HANDSTATE_IDLE
#undef TICKS
#undef bgunGetAmmoIndexFromGraph
#undef WEAPON_CALLISTO
#undef WEAPON_CMP150
#undef WEAPON_COMBATBOOST
#undef WEAPON_COMBATKNIFE
#undef WEAPON_CYCLONE
#undef WEAPON_DY357LX
#undef WEAPON_DY357MAGNUM
#undef WEAPON_FALCON2
#undef WEAPON_FALCON2_SCOPE
#undef WEAPON_FALCON2_SILENCER
#undef WEAPON_FARSIGHT
#undef WEAPON_GRENADE
#undef WEAPON_LAPTOPGUN
#undef WEAPON_MAGSEC4
#undef WEAPON_MAULER
#undef WEAPON_NBOMB
#undef WEAPON_PHOENIX
#undef WEAPON_PROXIMITYMINE
#undef WEAPON_PSYCHOSISGUN
#undef WEAPON_RCP120
#undef WEAPON_REAPER
#undef WEAPON_REMOTEMINE
#undef WEAPON_TIMEDMINE
#undef WEAPON_TRANQUILIZER
#undef WEAPON_UNARMED
#undef SFX_FIREEMPTY
#undef SFX_HIT_WATER

int32_t nativeSpecialControl(native_special_empty_case *c) {struct se_hand detached;struct se_info info;s32 result;se_import(c,&detached,&info);result=se_special_native_raw(se_passed_hand);se_export();return result;}

int32_t nativeGraphSpecialControl(native_special_empty_case *c) {struct se_hand detached;struct se_info info;s32 result;se_import(c,&detached,&info);result=se_special_connected_raw(se_passed_hand);se_export();return result;}

int32_t nativeEmptyControl(native_special_empty_case *c) {struct se_hand detached;struct se_info info;s32 result;se_import(c,&detached,&info);result=se_empty_native_raw(&info,c->handnum,se_passed_hand,c->lvupdate);se_export();return result;}

int32_t nativeGraphEmptyControl(native_special_empty_case *c) {struct se_hand detached;struct se_info info;s32 result;se_import(c,&detached,&info);result=se_empty_connected_raw(&info,c->handnum,se_passed_hand,c->lvupdate);se_export();return result;}

/* BEGIN complete Dispatcher/SetState oracle V11 */
#include <stdarg.h>
struct dp_gset {uint8_t weaponnum,unk0639,unk063a,weaponfunc;};
struct dp_hand {
    struct dp_gset gset;s32 state,stateminor,stateframes,statecycles,statelastframe;
    s32 attacktype,loadedammo[2],animmode,count60,count,burstbullets,lastshootframe60;
    int16_t primetimer60;int8_t firing,inuse;bool triggeron,triggerreleased,slideinc;
    uint8_t unk0cc8_01:1;u32 stateflags,mode;f32 matmot2;
};
struct dp_player {struct dp_hand hands[2];};
struct dp_func {s32 type;};
typedef struct {bool has_function_type_id;s32 function_type_id;} dp_graph_t;
struct dp_info {s32 weaponnum;void *definition;};
static struct {struct dp_player *currentplayer;s32 currentplayernum;} dp_vars;
static struct dp_player dp_players[2];
static native_attack_dispatch_case *dp_case;
static struct dp_hand *dp_passed;
static struct dp_info *dp_info_ptr;
static struct dp_func dp_function,dp_alternate;
static dp_graph_t dp_graph;
static s32 dp_definition;
static s32 dp_current_index(void) {return dp_vars.currentplayer==&dp_players[1]?1:0;}
static s32 dp_subject(const struct dp_hand *h) {
    for(s32 p=0;p<2;++p)for(s32 i=0;i<2;++i)if(h==&dp_players[p].hands[i])return p*2+i;return -1;
}
static void dp_export_hand(native_dispatch_hand_state *s,const struct dp_hand *h) {
    native_se_hand_state *b=&s->base;b->weaponnum=h->gset.weaponnum;b->weaponfunc=h->gset.weaponfunc;
    b->state=h->state;b->minor=h->stateminor;b->frames=h->stateframes;b->cycles=h->statecycles;b->lastframe=h->statelastframe;
    b->trigger=h->triggeron;b->firing=h->firing;b->attacktype=h->attacktype;memcpy(b->loadedammo,h->loadedammo,sizeof b->loadedammo);
    b->animmode=h->animmode;b->latch=h->unk0cc8_01;b->count60=h->count60;b->count=h->count;b->flags=h->stateflags;b->mode=h->mode;
    s->inuse=h->inuse;s->prime=h->primetimer60;s->burst=h->burstbullets;s->released=h->triggerreleased;s->slideinc=h->slideinc;s->lastshootframe=h->lastshootframe60;s->motor=h->matmot2;
}
static void dp_import_hand(struct dp_hand *h,const native_dispatch_hand_state *s) {
    const native_se_hand_state *b=&s->base;memset(h,0,sizeof *h);h->gset.weaponnum=b->weaponnum;h->gset.weaponfunc=b->weaponfunc;
    h->state=b->state;h->stateminor=b->minor;h->stateframes=b->frames;h->statecycles=b->cycles;h->statelastframe=b->lastframe;
    h->triggeron=b->trigger;h->firing=b->firing;h->attacktype=b->attacktype;memcpy(h->loadedammo,b->loadedammo,sizeof h->loadedammo);
    h->animmode=b->animmode;h->unk0cc8_01=b->latch;h->count60=b->count60;h->count=b->count;h->stateflags=b->flags;h->mode=b->mode;
    h->inuse=s->inuse;h->primetimer60=s->prime;h->burstbullets=s->burst;h->triggerreleased=s->released;h->slideinc=s->slideinc;h->lastshootframe60=s->lastshootframe;h->matmot2=s->motor;
}
static void dp_export(void) {
    dp_export_hand(&dp_case->passed,dp_passed);for(s32 p=0;p<2;++p)for(s32 i=0;i<2;++i)dp_export_hand(&dp_case->players[p][i],&dp_players[p].hands[i]);
    dp_case->current_player_index=dp_current_index();dp_case->current_playernum=dp_vars.currentplayernum;dp_case->info_weapon=dp_info_ptr->weaponnum;
}
void nativeAttackDispatchResetTrace(native_attack_dispatch_case *c) {c->trace_count=0;c->trace_overflow=0;}
void nativeAttackDispatchRecordEvent(native_attack_dispatch_case *c,s32 kind,s32 operation,s32 subject,const s32 scalar[8]) {
    native_dispatch_event *e;if(c->trace_count<0||c->trace_count>=NATIVE_DISPATCH_TRACE_LIMIT){c->trace_overflow=1;return;}
    e=&c->trace[c->trace_count++];memset(e,0,sizeof *e);e->kind=kind;e->operation=operation;e->subject=subject;
    if(scalar)memcpy(e->scalar,scalar,sizeof e->scalar);e->has_top_snapshot=1;e->state.passed=c->passed;
    memcpy(e->state.players,c->players,sizeof e->state.players);memcpy(e->state.players_throwing,c->players_throwing,sizeof e->state.players_throwing);
    e->state.current_player_index=c->current_player_index;e->state.current_playernum=c->current_playernum;e->state.info_weapon=c->info_weapon;
}
static void dp_record(s32 op,s32 a,s32 b,s32 d,s32 result) {s32 scalar[8]={a,b,d,result};dp_export();nativeAttackDispatchRecordEvent(dp_case,ND_EVENT_SERVICE,op,-1,scalar);}
static void dp_store(s32 op,struct dp_hand *h) {dp_export();nativeAttackDispatchRecordEvent(dp_case,ND_EVENT_WRITE,op,dp_subject(h),0);}
static void dp_child(s32 kind,const void *event) {
    native_dispatch_event *e;if(dp_case->trace_count<0||dp_case->trace_count>=NATIVE_DISPATCH_TRACE_LIMIT){dp_case->trace_overflow=1;return;}
    e=&dp_case->trace[dp_case->trace_count++];memset(e,0,sizeof *e);e->kind=kind;e->subject=-1;
    /* No false post-call top snapshot: these are immediate typed child events. */
    if(kind==ND_CHILD_SHOOT)e->child.shoot=*(const native_shooting_attack_event *)event;
    else if(kind==ND_CHILD_THROW)e->child.throwing=*(const native_throw_event *)event;
    else if(kind==ND_CHILD_MELEE)e->child.melee=*(const native_melee_event *)event;
    else if(kind==ND_CHILD_SPECIAL)e->child.special=*(const native_se_event *)event;
}
static void dp_import(native_attack_dispatch_case *c,struct dp_hand *detached,struct dp_info *info) {
    dp_case=c;dp_info_ptr=info;for(s32 p=0;p<2;++p)for(s32 i=0;i<2;++i)dp_import_hand(&dp_players[p].hands[i],&c->players[p][i]);
    /* Qualified indices: current player0/1,handnum0/1;passed player-1/0/1,
     * passed hand0/1; alternate switch-1/0/1. No corrupt-domain repair. */
    dp_vars.currentplayer=&dp_players[c->current_player_index];dp_vars.currentplayernum=c->current_playernum;
    dp_passed=c->passed_player_index<0?detached:&dp_players[c->passed_player_index].hands[c->passed_hand_index];dp_import_hand(dp_passed,&c->passed);
    info->weaponnum=c->info_weapon;info->definition=c->definition_present?&dp_definition:0;
    dp_function.type=c->legacy_type;dp_graph.has_function_type_id=c->graph_has_type;dp_graph.function_type_id=c->graph_type;dp_export();
}
static struct dp_func *dp_resolve_function(const struct dp_gset *set) {
    dp_record(ND_RESOLVE_FUNCTION,set->weaponnum,set->weaponfunc,0,!dp_case->function_absent);return dp_case->function_absent?0:&dp_function;
}
static const dp_graph_t *dp_resolve_graph(const struct dp_gset *set) {
    dp_record(ND_RESOLVE_GRAPH,set->weaponnum,set->weaponfunc,0,dp_case->graph_present!=0);return dp_case->graph_present?&dp_graph:0;
}
static struct dp_func *dp_alternate_function(const struct dp_gset *set,s32 which) {
    s32 weapon=set->weaponnum,function=set->weaponfunc;
    if(dp_case->alternate_switch_player>=0)dp_vars.currentplayer=&dp_players[dp_case->alternate_switch_player];
    if(dp_case->alternate_override_playernum)dp_vars.currentplayernum=dp_case->alternate_playernum;
    dp_record(ND_ALTERNATE_FUNCTION,weapon,function,which,dp_case->alternate_present!=0);return dp_case->alternate_present?&dp_alternate:0;
}
static void dp_log(s32 level,const char *format,...) {
    if(level==1&&strcmp(format,"LOG.WPN.DIAG: bgunSetState player=0 hand=%d %d->%d valid=%d wpn=%d inuse=%d")==0) {
        s32 scalar[8]={level};va_list args;va_start(args,format);for(s32 i=1;i<7;++i)scalar[i]=va_arg(args,int);va_end(args);
        dp_export();nativeAttackDispatchRecordEvent(dp_case,ND_EVENT_SERVICE,ND_LOG,-1,scalar);
    } else dp_case->program_refused=1;
}
static void dp_common_to_throw(native_throw_case *c,const struct dp_hand *h,s32 handnum) {
    c->handnum=handnum;c->weaponnum=h->gset.weaponnum;c->weaponfunc=h->gset.weaponfunc;c->minor=h->stateminor;c->cycles=h->statecycles;c->frames=h->stateframes;
    c->prime=h->primetimer60;c->trigger=h->triggeron;c->released=h->triggerreleased;c->latch=h->unk0cc8_01;c->firing=h->firing;c->attacktype=h->attacktype;
    memcpy(c->loadedammo,h->loadedammo,sizeof c->loadedammo);c->current_player_index=dp_current_index();memcpy(c->current_player_throwing,dp_case->players_throwing,sizeof c->current_player_throwing);
}
static bool dp_throw(s32 handnum,struct dp_hand *h,bool graph) {
    native_throw_case *c=&dp_case->throwing;s32 result;dp_record(ND_NESTED_BEGIN,ND_CHILD_THROW,handnum,0,0);dp_common_to_throw(c,h,handnum);nativeThrowResetTrace(c);
    result=graph?nativeGraphThrowControl(c):nativeThrowControl(c);
    h->stateminor=c->minor;h->stateframes=c->frames;h->primetimer60=c->prime;h->unk0cc8_01=c->latch;h->firing=c->firing;h->attacktype=c->attacktype;
    memcpy(h->loadedammo,c->loadedammo,sizeof h->loadedammo);memcpy(dp_case->players_throwing,c->current_player_throwing,sizeof c->current_player_throwing);
    for(s32 i=0;i<c->trace_count;++i)dp_child(ND_CHILD_THROW,&c->trace[i]);dp_case->trace_overflow|=c->trace_overflow;dp_case->program_refused|=c->program_refused;
    dp_record(ND_NESTED_END,ND_CHILD_THROW,handnum,0,result);return result;
}
static bool dp_melee(s32 handnum,struct dp_hand *h,bool graph) {
    native_melee_case *c=&dp_case->melee;s32 result;dp_record(ND_NESTED_BEGIN,ND_CHILD_MELEE,handnum,0,0);
    c->handnum=handnum;c->weaponnum=h->gset.weaponnum;c->weaponfunc=h->gset.weaponfunc;c->minor=h->stateminor;c->cycles=h->statecycles;c->frames=h->stateframes;c->burst=h->burstbullets;
    c->trigger=h->triggeron;c->released=h->triggerreleased;c->latch=h->unk0cc8_01;c->firing=h->firing;c->attacktype=h->attacktype;c->animmode=h->animmode;c->motor=h->matmot2;memcpy(c->loadedammo,h->loadedammo,sizeof c->loadedammo);
    nativeMeleeResetTrace(c);result=graph?nativeGraphMeleeControl(c):nativeMeleeControl(c);
    h->stateminor=c->minor;h->stateframes=c->frames;h->burstbullets=c->burst;h->unk0cc8_01=c->latch;h->firing=c->firing;h->attacktype=c->attacktype;h->animmode=c->animmode;h->matmot2=c->motor;memcpy(h->loadedammo,c->loadedammo,sizeof h->loadedammo);
    for(s32 i=0;i<c->trace_count;++i)dp_child(ND_CHILD_MELEE,&c->trace[i]);dp_case->trace_overflow|=c->trace_overflow;dp_case->program_refused|=c->program_refused;
    dp_record(ND_NESTED_END,ND_CHILD_MELEE,handnum,0,result);return result;
}
static bool dp_special(struct dp_hand *h,bool graph) {
    native_special_empty_case *c=&dp_case->special;native_dispatch_hand_state temporary;s32 result;dp_record(ND_NESTED_BEGIN,ND_CHILD_SPECIAL,0,0,0);
    dp_export_hand(&temporary,h);c->passed=temporary.base;c->passed_hand_index=-1;c->handnum=dp_case->handnum;
    for(s32 i=0;i<2;++i){dp_export_hand(&temporary,&dp_vars.currentplayer->hands[i]);c->current_hands[i]=temporary.base;}
    nativeSpecialEmptyResetTrace(c);result=graph?nativeGraphSpecialControl(c):nativeSpecialControl(c);
    h->stateminor=c->passed.minor;h->stateframes=c->passed.frames;h->firing=c->passed.firing;h->attacktype=c->passed.attacktype;memcpy(h->loadedammo,c->passed.loadedammo,sizeof h->loadedammo);
    for(s32 i=0;i<c->trace_count;++i)dp_child(ND_CHILD_SPECIAL,&c->trace[i]);dp_case->trace_overflow|=c->trace_overflow;dp_case->program_refused|=c->program_refused;
    dp_record(ND_NESTED_END,ND_CHILD_SPECIAL,0,0,result);return result;
}
static bool dp_shoot(struct dp_info *info,s32 handnum,struct dp_hand *h,bool graph) {
    native_shooting_attack_case *c=&dp_case->shoot;native_shooting_case *s=&c->shooting;s32 result;dp_record(ND_NESTED_BEGIN,ND_CHILD_SHOOT,handnum,0,0);
    c->weaponnum=h->gset.weaponnum;c->stateminor=h->stateminor;c->statecycles=h->statecycles;c->triggerreleased=h->triggerreleased;c->animation_latch=h->unk0cc8_01;c->animmode=h->animmode;c->matmot2=h->matmot2;
    s->handnum=handnum;s->info_weaponnum=info->weaponnum;s->trigger=h->triggeron;s->stateframes=h->stateframes;s->stateflags=h->stateflags;s->burstbullets=h->burstbullets;s->firing=h->firing;s->attacktype=h->attacktype;
    s->loadedammo=h->loadedammo[0];s->loadedammo1=h->loadedammo[1];s->passed_slideinc=h->slideinc;s->lastshootframe=h->lastshootframe60;
    s->passed_is_current=h==&dp_vars.currentplayer->hands[handnum];s->other_lastshootframe=dp_vars.currentplayer->hands[1-handnum].lastshootframe60;
    for(s32 i=0;i<2;++i){s->current_hand_slideinc[i]=dp_vars.currentplayer->hands[i].slideinc;s->current_hand_lastshootframe[i]=dp_vars.currentplayer->hands[i].lastshootframe60;}
    nativeShootingAttackResetTrace(c);nativeShootingResetTrace(s);result=graph?nativeGraphShootingAttack(c):nativeShootingAttack(c);
    h->stateminor=c->stateminor;h->stateframes=s->stateframes;h->statecycles=c->statecycles;h->stateflags=s->stateflags;h->burstbullets=s->burstbullets;h->triggerreleased=c->triggerreleased;h->unk0cc8_01=c->animation_latch;h->animmode=c->animmode;h->matmot2=c->matmot2;
    h->firing=s->firing;h->attacktype=s->attacktype;h->loadedammo[0]=s->loadedammo;h->loadedammo[1]=s->loadedammo1;h->slideinc=s->passed_slideinc;h->lastshootframe60=s->lastshootframe;
    for(s32 i=0;i<2;++i){dp_vars.currentplayer->hands[i].slideinc=s->current_hand_slideinc[i];dp_vars.currentplayer->hands[i].lastshootframe60=s->current_hand_lastshootframe[i];}
    for(s32 i=0;i<c->trace_count;++i)dp_child(ND_CHILD_SHOOT,&c->trace[i]);dp_case->trace_overflow|=c->trace_overflow|s->trace_overflow;dp_case->program_refused|=c->program_refused|s->program_refused;
    dp_record(ND_NESTED_END,ND_CHILD_SHOOT,handnum,0,result);return result;
}
static bool dp_native_shoot(struct dp_info *i,s32 n,struct dp_hand *h){return dp_shoot(i,n,h,false);}
static bool dp_graph_shoot(struct dp_info *i,s32 n,struct dp_hand *h){return dp_shoot(i,n,h,true);}
static bool dp_native_throw(s32 n,struct dp_hand *h){return dp_throw(n,h,false);}
static bool dp_graph_throw(s32 n,struct dp_hand *h){return dp_throw(n,h,true);}
static bool dp_native_melee(s32 n,struct dp_hand *h){return dp_melee(n,h,false);}
static bool dp_graph_melee(s32 n,struct dp_hand *h){return dp_melee(n,h,true);}
static bool dp_native_special(struct dp_hand *h){return dp_special(h,false);}
static bool dp_graph_special(struct dp_hand *h){return dp_special(h,true);}
#define weapon_graph_held_function_t dp_graph_t
#define g_Vars dp_vars
#define gsetGetWeaponFunction dp_resolve_function
#define bgunGetHeldGraph dp_resolve_graph
#define weaponGetFunction dp_alternate_function
#define sysLogPrintf dp_log
#define LOG_NOTE 1
#define LOG_WARNING 2
#define HANDSTATE_CHANGEFUNC 7
#define HANDSTATE_IDLE 0
#define WEAPON_REAPER 20
#define FUNC_SECONDARY 1
#define INVENTORYFUNCTYPE_THROW 2
#define INVENTORYFUNCTYPE_MELEE 3
#define INVENTORYFUNCTYPE_SPECIAL 4

#define bgunGetFunctionTypeFromGraph dp_type
static s32 bgunGetFunctionTypeFromGraph(struct dp_func *func,
		const weapon_graph_held_function_t *graph)
{
	if (graph && graph->has_function_type_id) {
		return graph->function_type_id;
	}
	return func ? func->type : INVENTORYFUNCTYPE_NONE;
}
#define bgunSetState dp_state_native_raw
bool bgunSetState(s32 handnum, s32 state)
{
	bool valid = true;
	struct dp_hand *hand = &g_Vars.currentplayer->hands[handnum];
	s32 prev_state;

	prev_state = (s32)hand->state;

	// Sanity check - don't allow changing function if there is no other
	if (state == HANDSTATE_CHANGEFUNC && weaponGetFunction(&hand->gset, 1 - hand->gset.weaponfunc) == NULL) {
		valid = false;
	}

	if (valid) {
		hand->state = state; dp_store(ND_WRITE_STATE,hand);
		hand->stateframes = 0; dp_store(ND_WRITE_FRAMES,hand);
		hand->stateflags = 0; dp_store(ND_WRITE_FLAGS,hand);
		hand->statecycles = 0; dp_store(ND_WRITE_CYCLES,hand);
		hand->stateminor = 0; dp_store(ND_WRITE_MINOR,hand);
		hand->statelastframe = 0; dp_store(ND_WRITE_LAST_FRAME,hand);
	}

	/* B-246 round-6 instrumentation: state transitions are the load-bearing
	 * signal for the FP rig lifecycle. Log every accepted transition (and
	 * any rejected one). Player 0 only. Low frequency by construction. */
	if (g_Vars.currentplayernum == 0 && (valid || state == HANDSTATE_CHANGEFUNC)) {
		sysLogPrintf(LOG_NOTE,
			"LOG.WPN.DIAG: bgunSetState player=0 hand=%d %d->%d valid=%d wpn=%d inuse=%d",
			(s32)handnum,
			prev_state,
			(s32)state,
			(s32)valid,
			(s32)hand->gset.weaponnum,
			(s32)hand->inuse);
	}

	return valid;
}

#undef bgunSetState
/* GGV State services begin: synchronous borrowed context. */
struct bgunstatecontext { s32 handnum; s32 state; struct dp_hand *hand; s32 logweapon; s32 loginuse; };
static ggv_value bgunStateRead(void *opaque, ggv_binding binding)
{
    struct bgunstatecontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_STATE_REQUEST: value.i = c->state; break;
    case GGV_STATE_VALUE: value.i = c->hand->state; break;
    case GGV_WEAPON_FUNCTION: value.i = c->hand->gset.weaponfunc; break;
    case GGV_STATE_PLAYER_NUMBER: value.i = g_Vars.currentplayernum; break;
    case GGV_HAND_NUMBER: value.i = c->handnum; break;
    case GGV_WEAPON_NUMBER: value.i = c->hand->gset.weaponnum; break;
    case GGV_STATE_INUSE: value.i = c->hand->inuse; break;
    default: break;
    } return value;
}
static void bgunStateWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunstatecontext *c = opaque;
    switch (binding) {
    case GGV_STATE_VALUE: c->hand->state = value.i; dp_store(ND_WRITE_STATE,c->hand); break;
    case GGV_STATE_FRAMES: c->hand->stateframes = value.i; dp_store(ND_WRITE_FRAMES,c->hand); break;
    case GGV_STATE_FLAGS: c->hand->stateflags = value.u; dp_store(ND_WRITE_FLAGS,c->hand); break;
    case GGV_STATE_CYCLES: c->hand->statecycles = value.i; dp_store(ND_WRITE_CYCLES,c->hand); break;
    case GGV_STATE_MINOR: c->hand->stateminor = value.i; dp_store(ND_WRITE_MINOR,c->hand); break;
    case GGV_STATE_LAST_FRAME: c->hand->statelastframe = value.i; dp_store(ND_WRITE_LAST_FRAME,c->hand); break;
    case GGV_STATE_LOG_WEAPON: c->logweapon = value.i; break;
    case GGV_STATE_LOG_INUSE: c->loginuse = value.i; break;
    default: break;
    }
}
static ggv_value bgunStateCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunstatecontext *c = opaque; ggv_value value = {0};
    switch (service) {
    case GGV_STATE_SELECT_HAND: c->hand = &g_Vars.currentplayer->hands[c->handnum]; break;
	// Sanity check - don't allow changing function if there is no other
    case GGV_STATE_ALTERNATE_FUNCTION: value.ref = weaponGetFunction(&c->hand->gset, a.i); break;
	/* B-246 round-6 instrumentation: state transitions are the load-bearing
	 * signal for the FP rig lifecycle. Log every accepted transition (and
	 * any rejected one). Player 0 only. Low frequency by construction. */
    case GGV_STATE_LOG: sysLogPrintf(LOG_NOTE, "LOG.WPN.DIAG: bgunSetState player=0 hand=%d %d->%d valid=%d wpn=%d inuse=%d", a.i, b.i, (s32) c->state, (s32) d.b, (s32) c->logweapon, (s32) c->loginuse); break;
    default: break;
    } return value;
}
/* GGV State services end. */
#define bgunSetState dp_state_connected_raw
bool bgunSetState(s32 handnum, s32 state)
{
    struct bgunstatecontext context = {0}; context.handnum = handnum; context.state = state;
    ggv_host host = { &context, bgunStateRead, bgunStateWrite, bgunStateCall };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_SET_STATE), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: state utility refused immutable program");
        return false;
    }
    return result;
}
#undef bgunSetState
#define bgunSetState dp_state_native_raw
#define bgunTickIncAttackingShoot dp_native_shoot
#define bgunTickIncAttackingThrow dp_native_throw
#define bgunTickIncAttackingMelee dp_native_melee
#define bgunTickIncAttackingSpecial dp_native_special
#define bgunTickIncAttack dp_dispatch_native_raw
s32 bgunTickIncAttack(struct dp_info *info, s32 handnum, struct dp_hand *hand, s32 lvupdate)
{
	u32 stack;
	struct dp_func *func = NULL;
	bool finished = true;
	u32 stack2;
	const weapon_graph_held_function_t *graph = bgunGetHeldGraph(&hand->gset);
	s32 functype;

	if (info->definition) {
		func = gsetGetWeaponFunction(&hand->gset);
	}

	if (func != NULL) {
		functype = bgunGetFunctionTypeFromGraph(func, graph);
		switch (functype & 0xff) {
		case INVENTORYFUNCTYPE_SHOOT:
			finished = bgunTickIncAttackingShoot(info, handnum, hand);
			break;
		case INVENTORYFUNCTYPE_THROW:
			finished = bgunTickIncAttackingThrow(handnum, hand);
			break;
		case INVENTORYFUNCTYPE_MELEE:
			finished = bgunTickIncAttackingMelee(handnum, hand);
			break;
		case INVENTORYFUNCTYPE_SPECIAL:
			finished = bgunTickIncAttackingSpecial(hand);
			break;
		}
	}

	if (finished) {
		if (hand->gset.weaponnum == WEAPON_REAPER && hand->triggeron) {
			hand->gset.weaponfunc = FUNC_SECONDARY; dp_store(ND_WRITE_FUNCTION,hand);
			finished = false;
		}

		if (finished && bgunSetState(handnum, HANDSTATE_IDLE)) {
			return lvupdate;
		}
	}

	if (1);
	if (1);

	return 0;
}

#undef bgunTickIncAttack
#undef bgunSetState
#undef bgunTickIncAttackingShoot
#undef bgunTickIncAttackingThrow
#undef bgunTickIncAttackingMelee
#undef bgunTickIncAttackingSpecial
#define bgunSetState dp_state_connected_raw
#define bgunTickIncAttackingShoot dp_graph_shoot
#define bgunTickIncAttackingThrow dp_graph_throw
#define bgunTickIncAttackingMelee dp_graph_melee
#define bgunTickIncAttackingSpecial dp_graph_special
/* GGV Dispatch services begin: synchronous borrowed context. */
struct bgundispatchcontext { struct dp_info *info; struct dp_hand *hand; s32 handnum; s32 lvupdate; struct dp_func *func; const weapon_graph_held_function_t *graph; };
static ggv_value bgunDispatchRead(void *opaque, ggv_binding binding)
{
    struct bgundispatchcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_EMPTY_DEFINITION: value.ref = c->info->definition; break;
    case GGV_GRAPH_HAS_TYPE: value.b = c->graph->has_function_type_id; break;
    case GGV_GRAPH_FUNCTION_TYPE: value.i = c->graph->function_type_id; break;
    case GGV_LEGACY_FUNCTION_TYPE: value.i = c->func->type; break;
    case GGV_WEAPON_NUMBER: value.i = c->hand->gset.weaponnum; break;
    case GGV_TRIGGER: value.b = c->hand->triggeron; break;
    case GGV_HAND_NUMBER: value.i = c->handnum; break;
    case GGV_EMPTY_LVUPDATE: value.i = c->lvupdate; break;
    default: break;
    } return value;
}
static void bgunDispatchWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgundispatchcontext *c = opaque;
    switch (binding) {
    case GGV_WEAPON_FUNCTION: c->hand->gset.weaponfunc = value.i; dp_store(ND_WRITE_FUNCTION,c->hand); break;
    default: break;
    }
}
static ggv_value bgunDispatchCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgundispatchcontext *c = opaque; ggv_value value = {0};
    switch (service) {
    case GGV_RESOLVE_GRAPH: c->graph = bgunGetHeldGraph(&c->hand->gset); value.b = c->graph != NULL; break;
    case GGV_RESOLVE_FUNCTION: c->func = gsetGetWeaponFunction(&c->hand->gset); value.b = c->func != NULL; break;
    case GGV_DISPATCH_SHOOT: value.b = bgunTickIncAttackingShoot(c->info, c->handnum, c->hand); break;
    case GGV_DISPATCH_THROW: value.b = bgunTickIncAttackingThrow(c->handnum, c->hand); break;
    case GGV_DISPATCH_MELEE: value.b = bgunTickIncAttackingMelee(c->handnum, c->hand); break;
    case GGV_DISPATCH_SPECIAL: value.b = bgunTickIncAttackingSpecial(c->hand); break;
    case GGV_EMPTY_SET_STATE: value.b = bgunSetState(a.i, b.i); break;
    default: break;
    } return value;
}
/* GGV Dispatch services end. */#define bgunTickIncAttack dp_dispatch_connected_raw
s32 bgunTickIncAttack(struct dp_info *info, s32 handnum, struct dp_hand *hand, s32 lvupdate)
{
    struct bgundispatchcontext context = {0};
    context.info = info; context.hand = hand; context.handnum = handnum; context.lvupdate = lvupdate;
    ggv_host host = { &context, bgunDispatchRead, bgunDispatchWrite, bgunDispatchCall };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_ATTACK_DISPATCH), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: attack dispatcher refused immutable program");
        return 0;
    }
    return result;
}
#undef bgunTickIncAttack
#undef bgunSetState
#undef bgunTickIncAttackingShoot
#undef bgunTickIncAttackingThrow
#undef bgunTickIncAttackingMelee
#undef bgunTickIncAttackingSpecial
#undef weapon_graph_held_function_t
#undef g_Vars
#undef gsetGetWeaponFunction
#undef bgunGetHeldGraph
#undef weaponGetFunction
#undef sysLogPrintf
#undef LOG_NOTE
#undef LOG_WARNING
#undef HANDSTATE_CHANGEFUNC
#undef HANDSTATE_IDLE
#undef WEAPON_REAPER
#undef FUNC_SECONDARY
#undef INVENTORYFUNCTYPE_THROW
#undef INVENTORYFUNCTYPE_MELEE
#undef INVENTORYFUNCTYPE_SPECIAL
#undef bgunGetFunctionTypeFromGraph

int32_t nativeAttackDispatch(native_attack_dispatch_case *c) {struct dp_hand detached;struct dp_info info;s32 result;dp_import(c,&detached,&info);result=dp_dispatch_native_raw(&info,c->handnum,dp_passed,c->lvupdate);dp_export();return result;}

int32_t nativeGraphAttackDispatch(native_attack_dispatch_case *c) {struct dp_hand detached;struct dp_info info;s32 result;dp_import(c,&detached,&info);result=dp_dispatch_connected_raw(&info,c->handnum,dp_passed,c->lvupdate);dp_export();return result;}

int32_t nativeStateControl(native_attack_dispatch_case *c) {struct dp_hand detached;struct dp_info info;s32 result;dp_import(c,&detached,&info);result=dp_state_native_raw(c->handnum,c->state_request);dp_export();return result;}

int32_t nativeGraphStateControl(native_attack_dispatch_case *c) {struct dp_hand detached;struct dp_info info;s32 result;dp_import(c,&detached,&info);result=dp_state_connected_raw(c->handnum,c->state_request);dp_export();return result;}

/* BEGIN complete Reset/Busy/Trigger oracle V12 */
struct ai_command {s32 token;};
struct ai_hand {
    s32 animload,animmode;
    uint8_t unk0cc8_01:1,unk0cc8_02:1,incrementalreloading:1,unk0cc8_04:3,unk0cc8_07:1,unk0cc8_08:1;
    uint8_t animloopcount;
    struct ai_command *unk0ce8;
    uint8_t ejectstate:4,ejecttype:3,unk0d0e_07:1;
    bool triggeron,triggerprev,triggerreleased;
};
struct ai_player {struct ai_hand hands[2];};
static struct {struct ai_player *currentplayer;} ai_vars;
static struct ai_player ai_players[2];
static struct ai_command ai_commands[NATIVE_AI_COMMAND_LIMIT];
static native_animation_input_case *ai_case;
static struct ai_hand *ai_passed;
static s32 ai_player_index(const struct ai_player *p) {return p==&ai_players[1]?1:0;}
static s32 ai_subject(const struct ai_hand *h) {
    for(s32 p=0;p<2;++p)for(s32 i=0;i<2;++i)if(h==&ai_players[p].hands[i])return p*2+i;return -1;
}
static void ai_export_hand(native_animation_hand_state *s,const struct ai_hand *h) {
    s->animload=h->animload;s->animmode=h->animmode;s->latch=h->unk0cc8_01;s->incremental=h->incrementalreloading;
    s->command_token=h->unk0ce8?h->unk0ce8->token:0;s->loopcount=h->animloopcount;s->bit02=h->unk0cc8_02;s->bit07=h->unk0d0e_07;
    s->neighbor04=h->unk0cc8_04;s->neighbor07=h->unk0cc8_07;s->neighbor08=h->unk0cc8_08;s->ejectstate=h->ejectstate;s->ejecttype=h->ejecttype;
    s->triggeron=h->triggeron;s->triggerprev=h->triggerprev;s->triggerreleased=h->triggerreleased;
}
static void ai_import_hand(struct ai_hand *h,const native_animation_hand_state *s) {
    memset(h,0,sizeof *h);h->animload=s->animload;h->animmode=s->animmode;h->unk0cc8_01=s->latch;h->incrementalreloading=s->incremental;
    h->unk0ce8=s->command_token?&ai_commands[s->command_token-1]:0;h->animloopcount=s->loopcount;h->unk0cc8_02=s->bit02;h->unk0d0e_07=s->bit07;
    h->unk0cc8_04=s->neighbor04;h->unk0cc8_07=s->neighbor07;h->unk0cc8_08=s->neighbor08;h->ejectstate=s->ejectstate;h->ejecttype=s->ejecttype;
    h->triggeron=s->triggeron;h->triggerprev=s->triggerprev;h->triggerreleased=s->triggerreleased;
}
static void ai_export(void) {
    ai_export_hand(&ai_case->passed,ai_passed);for(s32 p=0;p<2;++p)for(s32 i=0;i<2;++i)ai_export_hand(&ai_case->players[p][i],&ai_players[p].hands[i]);
    ai_case->current_player_index=ai_player_index(ai_vars.currentplayer);
}
void nativeAnimationInputResetTrace(native_animation_input_case *c) {c->trace_count=0;c->trace_overflow=0;}
void nativeAnimationInputRecordEvent(native_animation_input_case *c,s32 kind,s32 operation,s32 subject,const s32 scalar[4]) {
    native_animation_event *e;if(c->trace_count<0||c->trace_count>=NATIVE_AI_TRACE_LIMIT){c->trace_overflow=1;return;}
    e=&c->trace[c->trace_count++];memset(e,0,sizeof *e);e->kind=kind;e->operation=operation;e->subject=subject;
    if(scalar)memcpy(e->scalar,scalar,sizeof e->scalar);e->passed=c->passed;memcpy(e->players,c->players,sizeof e->players);e->current_player_index=c->current_player_index;
}
static void ai_store(s32 op,struct ai_hand *h) {ai_export();nativeAnimationInputRecordEvent(ai_case,NAI_EVENT_WRITE,op,ai_subject(h),0);}
static void ai_import(native_animation_input_case *c,struct ai_hand *detached) {
    ai_case=c;for(s32 i=0;i<NATIVE_AI_COMMAND_LIMIT;++i)ai_commands[i].token=i+1;
    for(s32 p=0;p<2;++p)for(s32 i=0;i<2;++i)ai_import_hand(&ai_players[p].hands[i],&c->players[p][i]);
    /* Qualified current player0/1,handnum0/1,passed player-1/0/1,passed hand0/1,
     * command0..4,switch-1/0/1. No NULL hand/currentplayer acceptance or repair. */
    ai_vars.currentplayer=&ai_players[c->current_player_index];ai_passed=c->passed_player_index<0?detached:&ai_players[c->passed_player_index].hands[c->passed_hand_index];
    ai_import_hand(ai_passed,&c->passed);ai_export();
}
static void ai_input(struct ai_player *player,s32 handnum,s32 pressed,s32 released) {
    s32 p=ai_player_index(player);s32 scalar[4]={p,handnum,pressed,released};
    /* Explicit synthetic callback mutations; actual GraphV2 behavior is not
     * executed or attributed to these responses. Captured hand stays cached. */
    if(ai_case->input_override_release)player->hands[handnum].triggerreleased=ai_case->input_release_value;
    if(ai_case->input_switch_player>=0)ai_vars.currentplayer=&ai_players[ai_case->input_switch_player];
    ai_export();nativeAnimationInputRecordEvent(ai_case,NAI_EVENT_INPUT,0,p*2+handnum,scalar);
}
static void ai_log(s32 level,const char *format,...) {(void)level;(void)format;ai_case->program_refused=1;}
#define g_Vars ai_vars
#define wgV2RuntimeInput ai_input
#define sysLogPrintf ai_log
#define HANDANIMMODE_IDLE 0
#define LOG_WARNING 2

#define bgunResetAnim ai_native_reset_raw
void bgunResetAnim(struct ai_hand *hand)
{
	hand->animload = -1; ai_store(NAI_WRITE_LOAD,hand);
	hand->animmode = HANDANIMMODE_IDLE; ai_store(NAI_WRITE_MODE,hand);
	hand->unk0cc8_01 = false; ai_store(NAI_WRITE_LATCH,hand);
	hand->incrementalreloading = false; ai_store(NAI_WRITE_INCREMENTAL,hand);
	hand->unk0ce8 = NULL; ai_store(NAI_WRITE_COMMAND,hand);
	hand->animloopcount = 0; ai_store(NAI_WRITE_LOOP,hand);
	hand->unk0cc8_02 = false; ai_store(NAI_WRITE_BIT02,hand);
	hand->unk0d0e_07 = false; ai_store(NAI_WRITE_BIT07,hand);
}

#undef bgunResetAnim
#define bgunIsAnimBusy ai_native_busy_raw
bool bgunIsAnimBusy(struct ai_hand *hand)
{
	return hand->animmode != HANDANIMMODE_IDLE;
}

#undef bgunIsAnimBusy
#define bgunSetTriggerOn ai_native_trigger_raw
void bgunSetTriggerOn(s32 handnum, bool on)
{
	struct ai_hand *hand = &g_Vars.currentplayer->hands[handnum];

	hand->triggerprev = hand->triggeron; ai_store(NAI_WRITE_PREVIOUS,hand);
	hand->triggeron = on; ai_store(NAI_WRITE_ON,hand);
	wgV2RuntimeInput(g_Vars.currentplayer, handnum, on && !hand->triggerprev, !on && hand->triggerprev);

	if (!on) {
		hand->triggerreleased = true; ai_store(NAI_WRITE_RELEASED,hand);
	}
}

#undef bgunSetTriggerOn
/* GGV Animation services begin: borrowed hand fields, no cleanup authority. */
static ggv_value bgunAnimationRead(void *opaque, ggv_binding binding)
{
    struct ai_hand *hand = opaque; ggv_value value = {0};
    if (binding == GGV_EMPTY_ANIM_MODE) value.i = hand->animmode;
    return value;
}
static void bgunAnimationWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct ai_hand *hand = opaque;
    switch (binding) {
    case GGV_ANIM_LOAD: hand->animload = value.i; ai_store(NAI_WRITE_LOAD,hand); break;
    case GGV_EMPTY_ANIM_MODE: hand->animmode = value.i; ai_store(NAI_WRITE_MODE,hand); break;
    case GGV_ANIMATION_LATCH: hand->unk0cc8_01 = value.b; ai_store(NAI_WRITE_LATCH,hand); break;
    case GGV_ANIM_INCREMENTAL: hand->incrementalreloading = value.b; ai_store(NAI_WRITE_INCREMENTAL,hand); break;
    case GGV_ANIM_COMMAND: hand->unk0ce8 = value.ref; ai_store(NAI_WRITE_COMMAND,hand); break;
    case GGV_ANIM_LOOP_COUNT: hand->animloopcount = value.i; ai_store(NAI_WRITE_LOOP,hand); break;
    case GGV_ANIM_BIT02: hand->unk0cc8_02 = value.b; ai_store(NAI_WRITE_BIT02,hand); break;
    case GGV_ANIM_BIT07: hand->unk0d0e_07 = value.b; ai_store(NAI_WRITE_BIT07,hand); break;
    default: break;
    }
}
/* GGV Animation services end. */
/* GGV Trigger services begin: raw BOOL32 stores then existing GraphV2 boundary. */
struct bguntriggercontext { s32 handnum; bool on; struct ai_hand *hand; };
static ggv_value bgunTriggerRead(void *opaque, ggv_binding binding)
{
    struct bguntriggercontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_TRIGGER: value.b = c->hand->triggeron; break;
    case GGV_TRIGGER_PREVIOUS: value.b = c->hand->triggerprev; break;
    case GGV_TRIGGER_REQUEST: value.b = c->on; break;
    case GGV_TRIGGER_PLAYER: value.ref = g_Vars.currentplayer; break;
    case GGV_HAND_NUMBER: value.i = c->handnum; break;
    default: break;
    } return value;
}
static void bgunTriggerWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bguntriggercontext *c = opaque;
    switch (binding) {
    case GGV_TRIGGER_PREVIOUS: c->hand->triggerprev = value.b; ai_store(NAI_WRITE_PREVIOUS,c->hand); break;
    case GGV_TRIGGER: c->hand->triggeron = value.b; ai_store(NAI_WRITE_ON,c->hand); break;
    case GGV_TRIGGER_RELEASED: c->hand->triggerreleased = value.b; ai_store(NAI_WRITE_RELEASED,c->hand); break;
    default: break;
    }
}
static ggv_value bgunTriggerCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bguntriggercontext *c = opaque; ggv_value value = {0};
    if (service == GGV_STATE_SELECT_HAND) c->hand = &g_Vars.currentplayer->hands[c->handnum];
    return value;
}
static ggv_value bgunTriggerCall4(void *opaque, ggv_service4 service, ggv_value a, ggv_value b, ggv_value c, ggv_value d)
{
    ggv_value value = {0}; (void) opaque;
    if (service == GGV_TRIGGER_V2_INPUT) wgV2RuntimeInput(a.ref, b.i, c.b, d.b);
    return value;
}
/* GGV Trigger services end. */
#define bgunResetAnim ai_connected_reset_raw
void bgunResetAnim(struct ai_hand *hand)
{
    ggv_host host = { hand, bgunAnimationRead, bgunAnimationWrite, NULL };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_ANIMATION_RESET), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: animation reset refused immutable program");
    }
}
#undef bgunResetAnim

#define bgunIsAnimBusy ai_connected_busy_raw
bool bgunIsAnimBusy(struct ai_hand *hand)
{
    ggv_host host = { hand, bgunAnimationRead, bgunAnimationWrite, NULL };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_ANIMATION_BUSY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: animation busy predicate refused immutable program");
        return false;
    }
    return result;
}
#undef bgunIsAnimBusy

#define bgunSetTriggerOn ai_connected_trigger_raw
void bgunSetTriggerOn(s32 handnum, bool on)
{
    struct bguntriggercontext context = {0}; context.handnum = handnum; context.on = on;
    ggv_host host = { &context, bgunTriggerRead, bgunTriggerWrite, bgunTriggerCall, bgunTriggerCall4 };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_TRIGGER_INPUT), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: trigger input refused immutable program");
    }
}
#undef bgunSetTriggerOn
#undef g_Vars
#undef wgV2RuntimeInput
#undef sysLogPrintf
#undef HANDANIMMODE_IDLE
#undef LOG_WARNING

void nativeAnimationReset(native_animation_input_case *c) {struct ai_hand detached;ai_import(c,&detached);ai_native_reset_raw(ai_passed);ai_export();}

void nativeGraphAnimationReset(native_animation_input_case *c) {struct ai_hand detached;ai_import(c,&detached);ai_connected_reset_raw(ai_passed);ai_export();}

int32_t nativeAnimationBusy(native_animation_input_case *c) {struct ai_hand detached;s32 result;ai_import(c,&detached);result=ai_native_busy_raw(ai_passed);ai_export();return result;}

int32_t nativeGraphAnimationBusy(native_animation_input_case *c) {struct ai_hand detached;s32 result;ai_import(c,&detached);result=ai_connected_busy_raw(ai_passed);ai_export();return result;}

void nativeTriggerInput(native_animation_input_case *c) {struct ai_hand detached;ai_import(c,&detached);ai_native_trigger_raw(c->handnum,c->raw_on);ai_export();}

void nativeGraphTriggerInput(native_animation_input_case *c) {struct ai_hand detached;ai_import(c,&detached);ai_connected_trigger_raw(c->handnum,c->raw_on);ai_export();}

/* BEGIN complete Ammo4 oracle V13 */
struct am_gset {uint8_t weaponnum;};
struct am_hand {struct am_gset gset;s8 inuse;s32 loadedammo[2];};
struct am_gunctrl {s8 ammotypes[2];};
struct am_player {s32 ammoheldarr[NATIVE_AMMO_TYPES];struct am_gunctrl gunctrl;struct am_hand hands[2];};
struct am_func {s32 type;s8 ammoindex;};
struct am_special {struct am_func base;s32 specialfunc;};
struct am_definition {u32 type;};
struct am_weapon {struct am_definition *ammos[2];};
static struct {struct am_player *currentplayer;s32 stagenum;} am_vars;
static struct am_player am_players[2];
static struct am_special am_function;
static struct am_definition am_definitions[2];
static struct am_weapon am_weapon;
static native_ammo_case *am_case;
static s32 am_original_count_raw(s32 ammotype);
static s32 am_connected_count_raw(s32 ammotype);
static s32 am_player_index(const struct am_player *p) {return p==&am_players[1]?1:0;}
static void am_export(void) {
    for(s32 p=0;p<2;++p){memcpy(am_case->players[p].reserve,am_players[p].ammoheldarr,sizeof am_players[p].ammoheldarr);
        for(s32 i=0;i<2;++i){am_case->players[p].equipped_type[i]=am_players[p].gunctrl.ammotypes[i];
            am_case->players[p].hands[i].inuse=am_players[p].hands[i].inuse;am_case->players[p].hands[i].weaponnum=am_players[p].hands[i].gset.weaponnum;
            memcpy(am_case->players[p].hands[i].loadedammo,am_players[p].hands[i].loadedammo,sizeof am_players[p].hands[i].loadedammo);}}
    am_case->current_player_index=am_player_index(am_vars.currentplayer);
}
void nativeAmmoResetTrace(native_ammo_case *c) {c->trace_count=0;c->trace_overflow=0;}
void nativeAmmoRecordEvent(native_ammo_case *c,s32 kind,s32 operation,s32 subject,const s32 scalar[4]) {
    native_ammo_event *e;if(c->trace_count<0||c->trace_count>=NATIVE_AMMO_TRACE_LIMIT){c->trace_overflow=1;return;}
    e=&c->trace[c->trace_count++];memset(e,0,sizeof *e);e->kind=kind;e->operation=operation;e->subject=subject;
    if(scalar)memcpy(e->scalar,scalar,sizeof e->scalar);e->current_player_index=c->current_player_index;
}
static void am_observe(s32 kind,s32 op,s32 subject,s32 a,s32 b,s32 c,s32 d) {
    s32 scalar[4]={a,b,c,d};am_case->current_player_index=am_player_index(am_vars.currentplayer);
    nativeAmmoRecordEvent(am_case,kind,op,subject,scalar);
}
static void am_import(native_ammo_case *c) {
    am_case=c;c->flag_cursor=0;
    for(s32 p=0;p<2;++p){memcpy(am_players[p].ammoheldarr,c->players[p].reserve,sizeof am_players[p].ammoheldarr);
        for(s32 i=0;i<2;++i){am_players[p].gunctrl.ammotypes[i]=(s8)c->players[p].equipped_type[i];
            am_players[p].hands[i].inuse=(s8)c->players[p].hands[i].inuse;am_players[p].hands[i].gset.weaponnum=(uint8_t)c->players[p].hands[i].weaponnum;
            memcpy(am_players[p].hands[i].loadedammo,c->players[p].hands[i].loadedammo,sizeof am_players[p].hands[i].loadedammo);}}
    am_vars.currentplayer=&am_players[c->current_player_index];am_vars.stagenum=c->stage;
    am_function.base.type=c->function_type;am_function.base.ammoindex=(s8)c->ammoindex;am_function.specialfunc=c->specialfunc;
    for(s32 i=0;i<2;++i){am_definitions[i].type=c->definition_types[i];am_weapon.ammos[i]=c->ammo_ref_tokens[i]?&am_definitions[c->ammo_ref_tokens[i]-1]:0;}
    /* Qualified player0/1,request0..32,physical signed8 inputs,definition
     * tokens0..2,flag length0..8/switches0..1. Ammoindex0/1 on dereference;
     * negative indexes remain permitted only on native lazy non-deref paths.
     * No clamps, weapon-null repair, index repair, or saturation is added. */
    am_export();
}
static s32 am_reserve(struct am_player *p,s32 type) {s32 v=p->ammoheldarr[type];am_observe(NAM_EVENT_READ,NAM_RESERVE,am_player_index(p),v,type,0,0);return v;}
static void am_selected(struct am_player *p) {am_observe(NAM_EVENT_SELECT,NAM_SELECT_PLAYER,am_player_index(p),0,0,0,0);}
static s32 am_inuse(struct am_player *p,s32 h) {s32 v=p->hands[h].inuse;am_observe(NAM_EVENT_READ,NAM_INUSE,am_player_index(p)*2+h,v,0,0,0);return v;}
static s32 am_type(struct am_player *p,s32 i) {s32 v=p->gunctrl.ammotypes[i];am_observe(NAM_EVENT_READ,NAM_EQUIPPED_TYPE,am_player_index(p),v,i,0,0);return v;}
static s32 am_weapon_number(struct am_player *p,s32 h) {s32 v=p->hands[h].gset.weaponnum;am_observe(NAM_EVENT_READ,NAM_WEAPON,am_player_index(p)*2+h,v,0,0,0);return v;}
static s32 am_loaded(struct am_player *p,s32 h,s32 i) {s32 v=p->hands[h].loadedammo[i];am_observe(NAM_EVENT_READ,NAM_LOADED,am_player_index(p)*2+h,v,i,0,0);return v;}
static struct am_func *am_function_ref(struct am_func *f) {am_observe(NAM_EVENT_READ,NAM_FUNCTION_REF,-1,f!=0,0,0,0);return f;}
static s32 am_function_type(struct am_func *f) {s32 v=f->type;am_observe(NAM_EVENT_READ,NAM_FUNCTION_TYPE,-1,v,0,0,0);return v;}
static s32 am_special_value(struct am_special *f) {s32 v=f->specialfunc;am_observe(NAM_EVENT_READ,NAM_SPECIAL,-1,v,0,0,0);return v;}
static s32 am_index(struct am_func *f) {s32 v=f->ammoindex;am_observe(NAM_EVENT_READ,NAM_AMMO_INDEX,-1,v,0,0,0);return v;}
static struct am_definition *am_definition_ref(struct am_weapon *w,s32 i) {
    struct am_definition *v=w->ammos[i];s32 token=v==0?0:v==&am_definitions[0]?1:2;
    am_observe(NAM_EVENT_READ,NAM_AMMO_REF,-1,token,i,0,0);return v;
}
static u32 am_definition_type(struct am_definition *f) {u32 v=f->type;am_observe(NAM_EVENT_READ,NAM_DEFINITION_TYPE,-1,oracle_int_bits(v),0,0,0);return v;}
static s32 am_stage(void) {s32 v=am_vars.stagenum;am_observe(NAM_EVENT_READ,NAM_STAGE,-1,v,0,0,0);return v;}
static bool am_has_flag(s32 weaponnum,s32 funcnum,u32 flag) {
    s32 i=am_case->flag_cursor++,v=i<am_case->flag_result_count?am_case->flag_results[i]:am_case->flag_default;
    if(am_case->flag_switch_enabled)am_vars.currentplayer=&am_players[am_case->flag_switch_players[i]];
    am_observe(NAM_EVENT_SERVICE,NAM_HAS_FLAG,-1,weaponnum,funcnum,oracle_int_bits(flag),v);return v;
}
static s32 am_nested_original(s32 type) {s32 r;am_observe(NAM_EVENT_SERVICE,NAM_COUNT_BEGIN,-1,type,0,0,0);r=am_original_count_raw(type);am_observe(NAM_EVENT_SERVICE,NAM_COUNT_END,-1,type,r,0,0);return r;}
static s32 am_nested_connected(s32 type) {s32 r;am_observe(NAM_EVENT_SERVICE,NAM_COUNT_BEGIN,-1,type,0,0,0);r=am_connected_count_raw(type);am_observe(NAM_EVENT_SERVICE,NAM_COUNT_END,-1,type,r,0,0);return r;}
static void am_log(s32 level,const char *format,...) {(void)level;(void)format;am_case->program_refused=1;}
#define g_Vars am_vars
#define weaponHasAmmoFlag am_has_flag
#define sysLogPrintf am_log
#define LOG_WARNING 2

#define AMMOFLAG_NORESERVE         1
#define AMMOTYPE_17           0x17
#define AMMOTYPE_1B           0x1b
#define AMMOTYPE_1C           0x1c
#define AMMOTYPE_1D           0x1d
#define AMMOTYPE_1F           0x1f
#define AMMOTYPE_BUG          0x18
#define AMMOTYPE_ECM_MINE     0x20
#define AMMOTYPE_MICROCAMERA  0x19
#define AMMOTYPE_PLASTIQUE    0x1a
#define AMMOTYPE_PSYCHOSIS    0x16
#define AMMOTYPE_REMOTE_MINE  0x0c
#define AMMOTYPE_TIMED_MINE   0x0e
#define AMMOTYPE_TOKEN        0x1e
#define HANDATTACKTYPE_BOOST           6
#define HANDATTACKTYPE_DETONATE        5
#define HANDATTACKTYPE_REVERTBOOST     7
#define INVENTORYFUNCTYPE_MELEE            0x0003
#define INVENTORYFUNCTYPE_NONE             0x0000
#define INVENTORYFUNCTYPE_SPECIAL          0x0004
#define INVENTORYFUNCTYPE_THROW            0x0002
#define STAGE_AIRFORCEONE   0x31
#define STAGE_CHICAGO       0x1d
#define bgunGetAmmoCount am_original_count_raw
s32 bgunGetAmmoCount(s32 ammotype)
{
	s32 i;
	s32 j;
	s32 total = am_reserve(g_Vars.currentplayer,ammotype);
	struct am_player *player = g_Vars.currentplayer; am_selected(player);

	for (i = 0; i < 2; i++) {
		if (am_inuse(player,i)) {
			for (j = 0; j < 2; j++) {
				if (am_type(player,j) == ammotype) {
					total = total + am_loaded(player,i,j);
				}
			}
		}
	}

	return total;
}

#undef bgunGetAmmoCount
#define bgunGetReservedAmmoCount am_original_reserved_raw
s32 bgunGetReservedAmmoCount(s32 ammotype)
{
	s32 i;
	s32 j;
	s32 total = am_reserve(g_Vars.currentplayer,ammotype);
	struct am_player *player = g_Vars.currentplayer; am_selected(player);

	for (i = 0; i < 2; i++) {
		if (am_inuse(player,i)) {
			for (j = 0; j < 2; j++) {
				if (am_type(player,j) == ammotype && weaponHasAmmoFlag(am_weapon_number(player,i), j, AMMOFLAG_NORESERVE)) {
					total = total + am_loaded(player,i,j);
				}
			}
		}
	}

	return total;
}

#undef bgunGetReservedAmmoCount
#define bgunGetAmmoCount am_nested_original
#define bgun0f0990b0 am_original_eligibility_raw
bool bgun0f0990b0(struct am_func *basefunc, struct am_weapon *weapon)
{
	if (!am_function_ref(basefunc)) {
		return true;
	}

	if (am_function_type(basefunc) == INVENTORYFUNCTYPE_NONE) {
		return true;
	}

	if ((am_function_type(basefunc) & 0xff) == INVENTORYFUNCTYPE_MELEE) {
		return true;
	}

	if ((am_function_type(basefunc) & 0xff) == INVENTORYFUNCTYPE_SPECIAL) {
		struct am_special *func = (struct am_special *)basefunc;

		if (am_special_value(func) != HANDATTACKTYPE_DETONATE
				&& am_special_value(func) != HANDATTACKTYPE_BOOST
				&& am_special_value(func) != HANDATTACKTYPE_REVERTBOOST) {
			return true;
		}
	}

	if ((am_function_type(basefunc) & 0xff) == INVENTORYFUNCTYPE_THROW) {
		if (am_index(basefunc) <= -1) {
			return true;
		}
	}

	if (am_index(basefunc) >= 0
			&& am_definition_ref(weapon,am_index(basefunc))
			&& bgunGetAmmoCount(am_definition_type(am_definition_ref(weapon,am_index(basefunc)))) <= 0) {
		return true;
	}

	return false;
}

#undef bgun0f0990b0
#define bgunAmmotypeAllowsUnlimitedAmmo am_original_unlimited_raw
bool bgunAmmotypeAllowsUnlimitedAmmo(u32 ammotype)
{
	switch (ammotype) {
	case AMMOTYPE_REMOTE_MINE:
		if (am_stage() == STAGE_CHICAGO) {
			return false;
		}
		break;
	case AMMOTYPE_TIMED_MINE:
		if (am_stage() == STAGE_AIRFORCEONE) {
			return false;
		}
		break;
	case AMMOTYPE_PSYCHOSIS:
	case AMMOTYPE_17:
	case AMMOTYPE_BUG:
	case AMMOTYPE_MICROCAMERA:
	case AMMOTYPE_PLASTIQUE:
	case AMMOTYPE_1B:
	case AMMOTYPE_1C:
	case AMMOTYPE_1D:
	case AMMOTYPE_TOKEN:
	case AMMOTYPE_1F:
	case AMMOTYPE_ECM_MINE:
		return false;
	}

	return true;
}

#undef bgunAmmotypeAllowsUnlimitedAmmo
#undef bgunGetAmmoCount
/* GGV AmmoCount services begin: fixed local traversal, borrowed player. */
struct bgunammocountcontext { s32 ammotype; struct am_player *player; s32 handindex; s32 slotindex; };
static ggv_value bgunAmmoCountRead(void *opaque, ggv_binding binding)
{
    struct bgunammocountcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_AMMO_RESERVE: value.i = am_reserve(g_Vars.currentplayer,c->ammotype); break;
    case GGV_AMMO_REQUEST: value.i = c->ammotype; break;
    case GGV_AMMO_HAND_INUSE: value.b = am_inuse(c->player,c->handindex); break;
    case GGV_AMMO_EQUIPPED_TYPE: value.i = am_type(c->player,c->slotindex); break;
    case GGV_AMMO_HAND_WEAPON: value.i = am_weapon_number(c->player,c->handindex); break;
    case GGV_AMMO_LOADED: value.i = am_loaded(c->player,c->handindex,c->slotindex); break;
    default: break;
    } return value;
}
static void bgunAmmoCountWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunammocountcontext *c = opaque;
    if (binding == GGV_AMMO_HAND_INDEX) c->handindex = value.i;
    if (binding == GGV_AMMO_SLOT_INDEX) c->slotindex = value.i;
}
static ggv_value bgunAmmoCountCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunammocountcontext *c = opaque; ggv_value value = {0};
    if (service == GGV_AMMO_SELECT_PLAYER) { c->player = g_Vars.currentplayer; am_selected(c->player); }
    if (service == GGV_AMMO_HAS_FLAG) value.b = weaponHasAmmoFlag(a.i, b.i, d.u);
    return value;
}
/* GGV AmmoCount services end. */
#define bgunGetAmmoCount am_connected_count_raw
s32 bgunGetAmmoCount(s32 ammotype)
{
    struct bgunammocountcontext context = {0}; context.ammotype = ammotype;
    ggv_host host = { &context, bgunAmmoCountRead, bgunAmmoCountWrite, bgunAmmoCountCall };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_AMMO_COUNT), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: ammo count refused immutable program");
        return 0;
    }
    return result;
}
#undef bgunGetAmmoCount
#define bgunGetReservedAmmoCount am_connected_reserved_raw
s32 bgunGetReservedAmmoCount(s32 ammotype)
{
    struct bgunammocountcontext context = {0}; context.ammotype = ammotype;
    ggv_host host = { &context, bgunAmmoCountRead, bgunAmmoCountWrite, bgunAmmoCountCall };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_RESERVED_AMMO_COUNT), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: ammo count refused immutable program");
        return 0;
    }
    return result;
}
#undef bgunGetReservedAmmoCount
#define bgunGetAmmoCount am_nested_connected
/* GGV AmmoEligibility services begin: borrowed metadata, no new index policy. */
struct bgunammoeligibilitycontext { struct am_func *func; struct am_weapon *weapon; };
static ggv_value bgunAmmoEligibilityRead(void *opaque, ggv_binding binding)
{
    struct bgunammoeligibilitycontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_AMMO_FUNCTION_REF: value.ref = am_function_ref(c->func); break;
    case GGV_LEGACY_FUNCTION_TYPE: value.i = am_function_type(c->func); break;
    case GGV_SPECIAL_LEGACY_VALUE: value.i = am_special_value((struct am_special *) c->func); break;
    case GGV_LEGACY_AMMO_INDEX: value.i = am_index(c->func); break;
    case GGV_AMMO_WEAPON_REF: value.ref = am_definition_ref(c->weapon,am_index(c->func)); break;
    case GGV_AMMO_DEFINITION_TYPE: value.u = am_definition_type(am_definition_ref(c->weapon,am_index(c->func))); break;
    default: break;
    } return value;
}
static void bgunAmmoEligibilityWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    (void) opaque; (void) binding; (void) value; /* Complete predicate has no stores. */
}
static ggv_value bgunAmmoEligibilityCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    ggv_value value = {0}; (void) opaque;
    if (service == GGV_AMMO_TOTAL_COUNT) value.i = bgunGetAmmoCount(a.i);
    return value;
}
/* GGV AmmoEligibility services end. */
/* GGV UnlimitedAmmo services begin: scalar input, lazy actual stage. */
static ggv_value bgunUnlimitedAmmoRead(void *opaque, ggv_binding binding)
{
    u32 *ammotype = opaque; ggv_value value = {0};
    if (binding == GGV_AMMO_REQUEST_U32) value.u = *ammotype;
    if (binding == GGV_AMMO_STAGE) value.i = am_stage();
    return value;
}
static void bgunUnlimitedAmmoWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    (void) opaque; (void) binding; (void) value; /* Policy has no stores. */
}
/* GGV UnlimitedAmmo services end. */
#define bgun0f0990b0 am_connected_eligibility_raw
bool bgun0f0990b0(struct am_func *basefunc, struct am_weapon *weapon)
{
    struct bgunammoeligibilitycontext context = { basefunc, weapon };
    ggv_host host = { &context, bgunAmmoEligibilityRead, bgunAmmoEligibilityWrite, bgunAmmoEligibilityCall };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_AMMO_ELIGIBILITY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: ammo exclusion predicate refused immutable program");
        return false;
    }
    return result;
}
#undef bgun0f0990b0
#define bgunAmmotypeAllowsUnlimitedAmmo am_connected_unlimited_raw
bool bgunAmmotypeAllowsUnlimitedAmmo(u32 ammotype)
{
    ggv_host host = { &ammotype, bgunUnlimitedAmmoRead, bgunUnlimitedAmmoWrite, NULL };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_UNLIMITED_AMMO), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: unlimited ammo policy refused immutable program");
        return false;
    }
    return result;
}
#undef bgunAmmotypeAllowsUnlimitedAmmo
#undef bgunGetAmmoCount
#undef g_Vars
#undef weaponHasAmmoFlag
#undef sysLogPrintf
#undef LOG_WARNING
#undef AMMOFLAG_NORESERVE
#undef AMMOTYPE_17
#undef AMMOTYPE_1B
#undef AMMOTYPE_1C
#undef AMMOTYPE_1D
#undef AMMOTYPE_1F
#undef AMMOTYPE_BUG
#undef AMMOTYPE_ECM_MINE
#undef AMMOTYPE_MICROCAMERA
#undef AMMOTYPE_PLASTIQUE
#undef AMMOTYPE_PSYCHOSIS
#undef AMMOTYPE_REMOTE_MINE
#undef AMMOTYPE_TIMED_MINE
#undef AMMOTYPE_TOKEN
#undef HANDATTACKTYPE_BOOST
#undef HANDATTACKTYPE_DETONATE
#undef HANDATTACKTYPE_REVERTBOOST
#undef INVENTORYFUNCTYPE_MELEE
#undef INVENTORYFUNCTYPE_NONE
#undef INVENTORYFUNCTYPE_SPECIAL
#undef INVENTORYFUNCTYPE_THROW
#undef STAGE_AIRFORCEONE
#undef STAGE_CHICAGO

int32_t nativeAmmoCount(native_ammo_case *c) {s32 result;am_import(c);result=am_original_count_raw(c->request);am_export();return result;}

int32_t nativeGraphAmmoCount(native_ammo_case *c) {s32 result;am_import(c);result=am_connected_count_raw(c->request);am_export();return result;}

int32_t nativeReservedAmmoCount(native_ammo_case *c) {s32 result;am_import(c);result=am_original_reserved_raw(c->request);am_export();return result;}

int32_t nativeGraphReservedAmmoCount(native_ammo_case *c) {s32 result;am_import(c);result=am_connected_reserved_raw(c->request);am_export();return result;}

int32_t nativeAmmoExclusion(native_ammo_case *c) {s32 result;am_import(c);result=am_original_eligibility_raw(c->function_absent?0:&am_function.base,c->weapon_absent?0:&am_weapon);am_export();return result;}

int32_t nativeGraphAmmoExclusion(native_ammo_case *c) {s32 result;am_import(c);result=am_connected_eligibility_raw(c->function_absent?0:&am_function.base,c->weapon_absent?0:&am_weapon);am_export();return result;}

int32_t nativeUnlimitedAmmo(native_ammo_case *c) {s32 result;am_import(c);result=am_original_unlimited_raw(c->unlimited_type);am_export();return result;}

int32_t nativeGraphUnlimitedAmmo(native_ammo_case *c) {s32 result;am_import(c);result=am_connected_unlimited_raw(c->unlimited_type);am_export();return result;}

/* BEGIN complete AmmoQuery5 oracle V14 */
struct aq_hand {struct am_gset gset;s32 loadedammo[2],clipsizes[2];};
struct aq_info {s32 weaponnum;struct am_gunctrl *gunctrl;};
struct aq_ammotype {s32 capacity;u32 unk04;f32 unk08;};
static struct aq_hand aq_hand;
static struct aq_info aq_info;
static struct am_gunctrl aq_detached_gunctrl;
static struct am_func aq_func;
static struct am_weapon aq_weapons[2];
static struct am_definition aq_definitions[NATIVE_QUERY_DEFINITIONS];
static native_ammo_query_case *aq_case;
static s32 aq_definition_token(const struct am_definition *d) {
    if(!d)return 0;for(s32 i=0;i<NATIVE_QUERY_DEFINITIONS;++i)if(d==&aq_definitions[i])return i+1;return -1;
}
void nativeAmmoQueryResetTrace(native_ammo_query_case *c) {c->trace_count=0;c->trace_overflow=0;}
void nativeAmmoQueryRecordEvent(native_ammo_query_case *c,s32 kind,s32 op,s32 subject,const s32 scalar[4]) {
    native_query_event *e;if(c->trace_count<0||c->trace_count>=NATIVE_QUERY_TRACE_LIMIT){c->trace_overflow=1;return;}
    e=&c->trace[c->trace_count++];memset(e,0,sizeof *e);e->kind=kind;e->operation=op;e->subject=subject;
    if(scalar)memcpy(e->scalar,scalar,sizeof e->scalar);e->current_player_index=am_player_index(am_vars.currentplayer);
}
static void aq_observe(s32 kind,s32 op,s32 subject,s32 a,s32 b,s32 c,s32 d) {
    s32 scalar[4]={a,b,c,d};nativeAmmoQueryRecordEvent(aq_case,kind,op,subject,scalar);
}
static void aq_export(void) {
    am_export();aq_case->hand.weaponnum=aq_hand.gset.weaponnum;
    memcpy(aq_case->hand.loadedammo,aq_hand.loadedammo,sizeof aq_hand.loadedammo);memcpy(aq_case->hand.clipsizes,aq_hand.clipsizes,sizeof aq_hand.clipsizes);
    if(aq_case->ammo.program_refused)aq_case->program_refused=1;
}
static void aq_import(native_ammo_query_case *c) {
    aq_case=c;nativeAmmoResetTrace(&c->ammo);am_import(&c->ammo);
    aq_hand.gset.weaponnum=(uint8_t)c->hand.weaponnum;memcpy(aq_hand.loadedammo,c->hand.loadedammo,sizeof aq_hand.loadedammo);memcpy(aq_hand.clipsizes,c->hand.clipsizes,sizeof aq_hand.clipsizes);
    for(s32 i=0;i<2;++i)aq_detached_gunctrl.ammotypes[i]=(s8)c->info_equipped_types[i];
    aq_info.weaponnum=c->info_weapon;aq_info.gunctrl=c->info_gunctrl_player<0?&aq_detached_gunctrl:&am_players[c->info_gunctrl_player].gunctrl;
    aq_func.type=0;aq_func.ammoindex=(s8)c->legacy_ammo_index;
    for(s32 i=0;i<NATIVE_QUERY_DEFINITIONS;++i)aq_definitions[i].type=c->definition_types[i];
    for(s32 w=0;w<2;++w)for(s32 i=0;i<2;++i)aq_weapons[w].ammos[i]=c->weapon_ammo_tokens[w][i]?&aq_definitions[c->weapon_ammo_tokens[w][i]-1]:0;
    /* Status hand is detached; info gunctrl may be detached or borrow either
     * live player0/1. No packed-native layout/address alias claim. Qualified
     * slots0/1 on deref,capacity/type0..32 on table/count paths,lookup token0..2,
     * definitions0..4,info-player-1/0/1,live player0/1 and physical signed8.
     * Unchecked weapon capacity requires NONNULL lookup. No repair is added. */
    aq_export();
}
static struct am_func *aq_resolve(struct am_gset *gset,s32 funcnum) {
    struct am_func *f=aq_case->function_absent?0:&aq_func;
    if(aq_case->resolve_switch_enabled)am_vars.currentplayer=&am_players[aq_case->resolve_player];
    aq_observe(NQ_EVENT_SERVICE,NQ_RESOLVE_FUNCTION,-1,gset->weaponnum,funcnum,f!=0,0);return f;
}
static struct am_weapon *aq_find(s32 weaponnum) {
    struct am_weapon *w=aq_case->lookup_weapon_token?&aq_weapons[aq_case->lookup_weapon_token-1]:0;
    if(aq_case->find_switch_enabled)am_vars.currentplayer=&am_players[aq_case->find_player];
    aq_observe(NQ_EVENT_SERVICE,NQ_FIND_WEAPON,-1,weaponnum,aq_case->lookup_weapon_token,0,0);return w;
}
static s32 aq_index(struct am_func *f) {s32 v=f->ammoindex;aq_observe(NQ_EVENT_READ,NQ_INDEX,-1,v,0,0,0);return v;}
static s32 aq_equipped(struct am_gunctrl *g,s32 i) {
    s32 v=g->ammotypes[i],subject=g==&aq_detached_gunctrl?-1:g==&am_players[1].gunctrl?1:0;
    aq_observe(NQ_EVENT_READ,NQ_EQUIPPED_TYPE,subject,v,i,0,0);return v;
}
static s32 aq_loaded(struct aq_hand *h,s32 i) {s32 v=h->loadedammo[i];aq_observe(NQ_EVENT_READ,NQ_LOADED,-1,v,i,0,0);return v;}
static s32 aq_clip(struct aq_hand *h,s32 i) {s32 v=h->clipsizes[i];aq_observe(NQ_EVENT_READ,NQ_CLIP,-1,v,i,0,0);return v;}
static s32 aq_info_weapon(struct aq_info *info) {s32 v=info->weaponnum;aq_observe(NQ_EVENT_READ,NQ_INFO_WEAPON,-1,v,0,0,0);return v;}
static s32 aq_reserve(struct am_player *p,s32 type) {s32 v=p->ammoheldarr[type];aq_observe(NQ_EVENT_READ,NQ_RESERVE,am_player_index(p),v,type,0,0);return v;}
static struct am_definition *aq_ref(struct am_weapon *w,u32 i) {
    struct am_definition *d=w->ammos[i];s32 subject=w==&aq_weapons[1]?1:0;
    aq_observe(NQ_EVENT_READ,NQ_AMMO_REF,subject,aq_definition_token(d),oracle_int_bits(i),0,0);return d;
}
static struct am_definition *aq_ref_signed(struct am_weapon *w,s32 i) {
    struct am_definition *d=w->ammos[i];s32 subject=w==&aq_weapons[1]?1:0;
    aq_observe(NQ_EVENT_READ,NQ_AMMO_REF,subject,aq_definition_token(d),i,0,0);return d;
}
static u32 aq_type(struct am_definition *d) {u32 v=d->type;aq_observe(NQ_EVENT_READ,NQ_TYPE,aq_definition_token(d),oracle_int_bits(v),0,0,0);return v;}
static s32 aq_minclip(s32 weaponnum,s32 funcnum,bool graph) {
    native_minclip_case c={0};s32 result;c.weaponnum=weaponnum;c.funcnum=funcnum;
    aq_observe(NQ_EVENT_SERVICE,NQ_MIN_BEGIN,-1,weaponnum,funcnum,0,0);
    result=graph?nativeGraphMinClipControl(&c):nativeMinClipControl(&c);
    if(c.program_refused)aq_case->program_refused=1;
    aq_observe(NQ_EVENT_SERVICE,NQ_MIN_END,-1,weaponnum,funcnum,result,0);return result;
}
static s32 aq_min_original(s32 w,s32 f) {return aq_minclip(w,f,false);}
static s32 aq_min_connected(s32 w,s32 f) {return aq_minclip(w,f,true);}
static s32 aq_reserved(s32 type,bool graph) {
    s32 result,first=am_case->trace_count;aq_observe(NQ_EVENT_SERVICE,NQ_RESERVED_BEGIN,-1,type,0,0,0);
    result=graph?am_connected_reserved_raw(type):am_original_reserved_raw(type);
    /* Child events were captured at their immediate original/actual reads and
     * services. Forward typed captures before END, with no parent snapshot. */
    for(s32 i=first;i<am_case->trace_count;++i){const native_ammo_event *child=&am_case->trace[i];s32 previous=aq_case->trace_count;
        nativeAmmoQueryRecordEvent(aq_case,NQ_CHILD_RESERVED,child->operation,child->subject,child->scalar);
        if(aq_case->trace_count>previous){native_query_event *e=&aq_case->trace[previous];e->child=*child;e->current_player_index=child->current_player_index;}}
    if(am_case->trace_overflow)aq_case->trace_overflow=1;
    aq_observe(NQ_EVENT_SERVICE,NQ_RESERVED_END,-1,type,result,0,0);return result;
}
static s32 aq_reserved_original(s32 type) {return aq_reserved(type,false);}
static s32 aq_reserved_connected(s32 type) {return aq_reserved(type,true);}
static void aq_log(s32 level,const char *format,...) {(void)level;(void)format;aq_case->program_refused=1;}
#define g_Vars am_vars
#define weaponGetFunction aq_resolve
#define weaponFindById aq_find
#define sysLogPrintf aq_log
#define LOG_WARNING 2
#define WEAPON_SHOTGUN 19
#define WEAPON_TRANQUILIZER 28
#define FUNC_SECONDARY 1
#define g_AmmoTypes aq_capacity_table
#if PAL
#define TICKS(value) ((value)*50/60)
#else
#define TICKS(value) (value)
#endif

struct aq_ammotype g_AmmoTypes[] = {
	{ 0,            0, 0  },
	{ 800,          0, 0  }, // AMMOTYPE_PISTOL
	{ 800,          0, 0  }, // AMMOTYPE_SMG
	{ 69,           0, 0  }, // AMMOTYPE_CROSSBOW
	{ 400,          0, -2 }, // AMMOTYPE_RIFLE
	{ 100,          0, 0  }, // AMMOTYPE_SHOTGUN
	{ 100,          0, 0  }, // AMMOTYPE_FARSIGHT
	{ 12,           0, 0  }, // AMMOTYPE_GRENADE
	{ 3,            0, -2 }, // AMMOTYPE_ROCKET
	{ 10,           0, 0  }, // AMMOTYPE_KNIFE
	{ 200,          0, 0  }, // AMMOTYPE_MAGNUM
	{ 40,           0, 0  }, // AMMOTYPE_DEVASTATOR
	{ 10,           0, 1  }, // AMMOTYPE_REMOTE_MINE
	{ 10,           0, 1  }, // AMMOTYPE_PROXY_MINE
	{ 10,           0, 1  }, // AMMOTYPE_TIMED_MINE
	{ 800,          0, 0  }, // AMMOTYPE_REAPER
	{ 15,           0, -2 }, // AMMOTYPE_HOMINGROCKET
	{ 50,           0, 0  }, // AMMOTYPE_DART
	{ 10,           0, 0  }, // AMMOTYPE_NBOMB
	{ 200,          0, 0  }, // AMMOTYPE_SEDATIVE
	{ TICKS(18000), 0, 0  }, // AMMOTYPE_CLOAK
	{ 4,            0, 0  }, // AMMOTYPE_BOOST
	{ 200,          0, 0  }, // AMMOTYPE_PSYCHOSIS
	{ 2,            0, 0  }, // AMMOTYPE_17
	{ 10,           0, 0  }, // AMMOTYPE_BUG
	{ 10,           0, 0  }, // AMMOTYPE_MICROCAMERA
	{ 10,           0, 0  }, // AMMOTYPE_PLASTIQUE
	{ 1000,         0, 0  }, // AMMOTYPE_1B
	{ 10,           0, 0  }, // AMMOTYPE_1C
	{ 50,           0, -1 }, // AMMOTYPE_1D
	{ 1,            0, 0  }, // AMMOTYPE_TOKEN
	{ 200,          0, 0  }, // AMMOTYPE_1F
	{ 10,           0, 0  }, // AMMOTYPE_ECM_MINE
};
static s32 aq_capacity_i(s32 type) {s32 v=aq_capacity_table[type].capacity;aq_observe(NQ_EVENT_READ,NQ_CAPACITY,-1,v,type,0,0);return v;}
static s32 aq_capacity_u(u32 type) {s32 v=aq_capacity_table[type].capacity;aq_observe(NQ_EVENT_READ,NQ_CAPACITY,-1,v,oracle_int_bits(type),0,0);return v;}
#define bgunGetMinClipQty aq_min_original
#define bgunGetReservedAmmoCount aq_reserved_original
#define bgun0f098ca0 aq_original_status_raw
s32 bgun0f098ca0(s32 funcnum, struct aq_info *info, struct aq_hand *hand)
{
	s32 result = 3;
	struct am_func *func = weaponGetFunction(&hand->gset, funcnum);

	if (!func) {
		return -1;
	}

	if (aq_index(func) != -1) {
		s32 ammoindex = aq_index(func);

		if (aq_equipped(info->gunctrl,ammoindex) >= 0
				&& aq_loaded(hand,ammoindex) < aq_clip(hand,ammoindex)) {
			s32 minqty = 1;

			if (aq_info_weapon(info) == WEAPON_SHOTGUN && funcnum == FUNC_SECONDARY) {
				minqty = 2;
			}

			if (aq_info_weapon(info) == WEAPON_TRANQUILIZER && funcnum == FUNC_SECONDARY) {
				minqty = bgunGetMinClipQty(WEAPON_TRANQUILIZER, FUNC_SECONDARY);
			}

			result = 1;

			if (aq_loaded(hand,ammoindex) < minqty) {
				result = 0;

				if (aq_reserve(g_Vars.currentplayer,aq_equipped(info->gunctrl,ammoindex)) == 0) {
					result = -1;
				}
			} else {
				if (aq_reserve(g_Vars.currentplayer,aq_equipped(info->gunctrl,ammoindex)) == 0) {
					result = 2;
				}
			}
		}
	}

	return result;
}

#undef bgun0f098ca0
#define bgunGetCapacityByAmmotype aq_original_capacity_raw
s32 bgunGetCapacityByAmmotype(s32 ammotype)
{
	return aq_capacity_i(ammotype);
}

#undef bgunGetCapacityByAmmotype
#define bgunGetAmmoTypeForWeapon aq_original_type_raw
u32 bgunGetAmmoTypeForWeapon(u32 weaponnum, u32 func)
{
	struct am_weapon *weapon = weaponFindById(weaponnum);

	if (!weapon) {
		return 0;
	}

	if (!aq_ref(weapon,func)) {
		return 0;
	}

	return aq_type(aq_ref(weapon,func));
}

#undef bgunGetAmmoTypeForWeapon
#define bgunGetAmmoQtyForWeapon aq_original_qty_raw
s32 bgunGetAmmoQtyForWeapon(u32 weaponnum, u32 func)
{
	struct am_weapon *weapon = weaponFindById(weaponnum);

	if (weapon) {
		struct am_definition *ammo = aq_ref(weapon,func);

		if (ammo) {
			return bgunGetReservedAmmoCount(aq_type(ammo));
		}
	}

	return 0;
}

#undef bgunGetAmmoQtyForWeapon
#define bgunGetAmmoCapacityForWeapon aq_original_weaponcapacity_raw
s32 bgunGetAmmoCapacityForWeapon(s32 weaponnum, s32 func)
{
	struct am_weapon *weapon = weaponFindById(weaponnum);
	struct am_definition *ammo = aq_ref_signed(weapon,func);

	if (ammo) {
		return aq_capacity_u(aq_type(ammo));
	}

	return 0;
}

#undef bgunGetAmmoCapacityForWeapon
#undef bgunGetMinClipQty
#undef bgunGetReservedAmmoCount
#define bgunGetMinClipQty aq_min_connected
#define bgunGetReservedAmmoCount aq_reserved_connected
/* GGV AmmoStatus services begin: borrowed original hand/info/function. */
struct bgunammostatuscontext { s32 funcnum; struct aq_info *info; struct aq_hand *hand; struct am_func *func; s32 ammoindex; };
static ggv_value bgunAmmoStatusRead(void *opaque, ggv_binding binding)
{
    struct bgunammostatuscontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_QUERY_FUNCTION_I32: value.i = c->funcnum; break;
    case GGV_LEGACY_AMMO_INDEX: value.i = aq_index(c->func); break;
    case GGV_QUERY_EQUIPPED_TYPE: value.i = aq_equipped(c->info->gunctrl,c->ammoindex); break;
    case GGV_LOADED_AMMO: value.i = aq_loaded(c->hand,c->ammoindex); break;
    case GGV_QUERY_CLIP_CAPACITY: value.i = aq_clip(c->hand,c->ammoindex); break;
    case GGV_WEAPON_NUMBER: value.i = aq_info_weapon(c->info); break;
    case GGV_QUERY_RESERVE: value.i = aq_reserve(g_Vars.currentplayer,aq_equipped(c->info->gunctrl,c->ammoindex)); break;
    default: break;
    } return value;
}
static void bgunAmmoStatusWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunammostatuscontext *c = opaque;
    if (binding == GGV_AMMO_INDEX) c->ammoindex = value.i;
}
static ggv_value bgunAmmoStatusCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunammostatuscontext *c = opaque; ggv_value value = {0};
    if (service == GGV_QUERY_RESOLVE_FUNCTION) { c->func = weaponGetFunction(&c->hand->gset, a.i); value.b = c->func != NULL; }
    if (service == GGV_MELEE_MIN_CLIP) value.i = bgunGetMinClipQty(a.i, b.i);
    return value;
}
/* GGV AmmoStatus services end. */
/* GGV AmmoQuery services begin: declared after the native capacity table. */
struct bgunammoquerycontext { s32 ammotype; u32 weaponnum; s32 signedweapon; u32 funcnum; s32 signedfunc; struct am_weapon *weapon; struct am_definition *ammo; };
static ggv_value bgunAmmoQueryRead(void *opaque, ggv_binding binding)
{
    struct bgunammoquerycontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_QUERY_CAPACITY: value.i = aq_capacity_i(c->ammotype); break;
    case GGV_QUERY_WEAPON_U32: value.u = c->weaponnum; break;
    case GGV_QUERY_WEAPON_I32: value.i = c->signedweapon; break;
    case GGV_QUERY_AMMO_REF_U32: value.ref = aq_ref(c->weapon,c->funcnum); break;
    case GGV_QUERY_AMMO_REF_I32: value.ref = aq_ref_signed(c->weapon,c->signedfunc); break;
    case GGV_QUERY_AMMO_TYPE: value.u = aq_type(aq_ref(c->weapon,c->funcnum)); break;
    case GGV_QUERY_CACHED_TYPE: value.u = aq_type(c->ammo); break;
    case GGV_QUERY_CACHED_CAPACITY: value.i = aq_capacity_u(aq_type(c->ammo)); break;
    default: break;
    } return value;
}
static void bgunAmmoQueryWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunammoquerycontext *c = opaque;
    if (binding == GGV_QUERY_CACHED_AMMO) c->ammo = value.ref;
}
static ggv_value bgunAmmoQueryCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunammoquerycontext *c = opaque; ggv_value value = {0};
    if (service == GGV_QUERY_FIND_WEAPON) { c->weapon = weaponFindById(a.i); value.ref = c->weapon; }
    if (service == GGV_QUERY_RESERVED_COUNT) value.i = bgunGetReservedAmmoCount(a.i);
    return value;
}
/* GGV AmmoQuery services end. */
#define bgun0f098ca0 aq_connected_status_raw
s32 bgun0f098ca0(s32 funcnum, struct aq_info *info, struct aq_hand *hand)
{
    struct bgunammostatuscontext context = {0}; context.funcnum = funcnum; context.info = info; context.hand = hand;
    ggv_host host = { &context, bgunAmmoStatusRead, bgunAmmoStatusWrite, bgunAmmoStatusCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_AMMO_STATUS), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: ammo status refused immutable program"); return -1;
    }
    return result;
}
#undef bgun0f098ca0
#define bgunGetCapacityByAmmotype aq_connected_capacity_raw
s32 bgunGetCapacityByAmmotype(s32 ammotype)
{
    struct bgunammoquerycontext context = {0}; context.ammotype = ammotype;
    ggv_host host = { &context, bgunAmmoQueryRead, bgunAmmoQueryWrite, bgunAmmoQueryCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_AMMO_CAPACITY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: ammo query refused immutable program"); return 0;
    }
    return result;
}
#undef bgunGetCapacityByAmmotype
#define bgunGetAmmoTypeForWeapon aq_connected_type_raw
u32 bgunGetAmmoTypeForWeapon(u32 weaponnum, u32 func)
{
    struct bgunammoquerycontext context = {0}; context.weaponnum = weaponnum; context.funcnum = func;
    ggv_host host = { &context, bgunAmmoQueryRead, bgunAmmoQueryWrite, bgunAmmoQueryCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_AMMO_TYPE_QUERY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: ammo query refused immutable program"); return 0;
    }
    return result;
}
#undef bgunGetAmmoTypeForWeapon
#define bgunGetAmmoQtyForWeapon aq_connected_qty_raw
s32 bgunGetAmmoQtyForWeapon(u32 weaponnum, u32 func)
{
    struct bgunammoquerycontext context = {0}; context.weaponnum = weaponnum; context.funcnum = func;
    ggv_host host = { &context, bgunAmmoQueryRead, bgunAmmoQueryWrite, bgunAmmoQueryCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_AMMO_QTY_QUERY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: ammo query refused immutable program"); return 0;
    }
    return result;
}
#undef bgunGetAmmoQtyForWeapon
#define bgunGetAmmoCapacityForWeapon aq_connected_weaponcapacity_raw
s32 bgunGetAmmoCapacityForWeapon(s32 weaponnum, s32 func)
{
    struct bgunammoquerycontext context = {0}; context.signedweapon = weaponnum; context.signedfunc = func;
    ggv_host host = { &context, bgunAmmoQueryRead, bgunAmmoQueryWrite, bgunAmmoQueryCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_AMMO_WEAPON_CAPACITY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: ammo query refused immutable program"); return 0;
    }
    return result;
}
#undef bgunGetAmmoCapacityForWeapon
#undef bgunGetMinClipQty
#undef bgunGetReservedAmmoCount
#undef g_Vars
#undef weaponGetFunction
#undef weaponFindById
#undef sysLogPrintf
#undef LOG_WARNING
#undef WEAPON_SHOTGUN
#undef WEAPON_TRANQUILIZER
#undef FUNC_SECONDARY
#undef g_AmmoTypes
#undef TICKS

int32_t nativeAmmoStatus(native_ammo_query_case *c) {s32 result;aq_import(c);result=aq_original_status_raw(c->status_func,&aq_info,&aq_hand);aq_export();return result;}

int32_t nativeGraphAmmoStatus(native_ammo_query_case *c) {s32 result;aq_import(c);result=aq_connected_status_raw(c->status_func,&aq_info,&aq_hand);aq_export();return result;}

int32_t nativeAmmoCapacity(native_ammo_query_case *c) {s32 result;aq_import(c);result=aq_original_capacity_raw(c->capacity_request);aq_export();return result;}

int32_t nativeGraphAmmoCapacity(native_ammo_query_case *c) {s32 result;aq_import(c);result=aq_connected_capacity_raw(c->capacity_request);aq_export();return result;}

uint32_t nativeAmmoTypeQuery(native_ammo_query_case *c) {u32 result;aq_import(c);result=aq_original_type_raw(c->weapon_u32,c->func_u32);aq_export();return result;}

uint32_t nativeGraphAmmoTypeQuery(native_ammo_query_case *c) {u32 result;aq_import(c);result=aq_connected_type_raw(c->weapon_u32,c->func_u32);aq_export();return result;}

int32_t nativeAmmoQtyQuery(native_ammo_query_case *c) {s32 result;aq_import(c);result=aq_original_qty_raw(c->weapon_u32,c->func_u32);aq_export();return result;}

int32_t nativeGraphAmmoQtyQuery(native_ammo_query_case *c) {s32 result;aq_import(c);result=aq_connected_qty_raw(c->weapon_u32,c->func_u32);aq_export();return result;}

int32_t nativeAmmoWeaponCapacity(native_ammo_query_case *c) {s32 result;aq_import(c);result=aq_original_weaponcapacity_raw(c->weapon_i32,c->func_i32);aq_export();return result;}

int32_t nativeGraphAmmoWeaponCapacity(native_ammo_query_case *c) {s32 result;aq_import(c);result=aq_connected_weaponcapacity_raw(c->weapon_i32,c->func_i32);aq_export();return result;}
