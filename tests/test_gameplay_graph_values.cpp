#include "catch.hpp"
#include "gameplay_graph_values.h"
#include "weapon_command_source.h"
#include <memory>
#include <array>
#include "versions.h"
#include "support/native_shooting_flow_oracle.h"
#include "support/native_hand_lifecycle_oracle.h"
#include <bit>
#include <algorithm>
#include <vector>
#include <limits>
#include <cstring>

namespace {
// Independent native donors supply expected control flow. This adapter only
// exposes fixture fields and scripted services to the production graph VM.
// It deliberately does not call the native donors from graph callbacks.
struct HandLifecycleFixture {
    nhlo_oracle oracle{};
    nhlo_player players[2]{};
    nhlo_hand passed{};
    nhlo_weaponfunc function{};
    nhlo_guncmd command{};
    nhlo_inventory_ammo ammo[2]{};
    nhlo_weapon weapon{};
    nhlo_handweaponinfo info{};
    nhlo_weaponfunc *captured = nullptr;
    nhlo_hand *other = nullptr;
    int handnum = 0, update = 7;
    float angle = 0.25f;
    bool switchOnAnimation = false, mutateOnTransfer = false;
    int32_t flags40 = 0, flags80 = 0;

    HandLifecycleFixture() {
        nhloInit(&oracle);
        oracle.vars.currentplayer = &players[0];
        oracle.vars.lvupdate240 = 4;
        oracle.var80095200 = 0x80000007u;
        oracle.hook = hook;
        oracle.hook_user = this;
        weapon.ammos[0] = &ammo[0]; weapon.ammos[1] = &ammo[1];
        info.definition = &weapon; info.weaponnum = NHLO_WEAPON_FALCON2;
        ammo[0].reload_animation = &command;
        ammo[1].reload_animation = &command;
        passed.state = NHLO_HANDSTATE_RELOAD;
        passed.visible = 1; passed.animmode = NHLO_HANDANIMMODE_BUSY;
        players[0].gunctrl.gunmemnew = -1;
        players[0].gunctrl.masterloadstate = NHLO_MASTERLOADSTATE_LOADED;
        oracle.response[NHLO_CALL_GSET_GET_WEAPON_FUNCTION].p = &function;
        oracle.response[NHLO_CALL_GSET_GET_PRI_TO_SEC_ANIM].p = &command;
        oracle.response[NHLO_CALL_GSET_GET_SEC_TO_PRI_ANIM].p = &command;
        oracle.response[NHLO_CALL_BGUN_SET_STATE].i = 1;
        oracle.response[NHLO_CALL_BGUN_IS_LOADED].i = 1;
        oracle.response[NHLO_CALL_WEAPON_GET_FILE_NUM2].i = 1;
        oracle.response[NHLO_CALL_COSF].f = 0.75f;
        oracle.response[NHLO_CALL_SINF].f = -0.25f;
    }
    HandLifecycleFixture(const HandLifecycleFixture&) = delete;
    HandLifecycleFixture& operator=(const HandLifecycleFixture&) = delete;

    static void hook(nhlo_oracle *o, nhlo_event *e, void *p) {
        auto &h = *static_cast<HandLifecycleFixture *>(p);
        if (e->kind == NHLO_CALL_WEAPON_HAS_FLAG)
            e->result.i = e->u[0] == 0x40 ? h.flags40 : h.flags80;
        if (e->kind == NHLO_CALL_BGUN_START_ANIMATION && h.switchOnAnimation)
            o->vars.currentplayer = &h.players[1];
        if (e->kind == NHLO_CALL_BGUN_0F098DF8 && h.mutateOnTransfer) {
            h.passed.gset.weaponfunc = 1;
            h.function.ammoindex = 1;
            o->vars.currentplayer = &h.players[1];
        }
    }
    nhlo_event &event(nhlo_event_kind kind) {
        REQUIRE(oracle.count < NHLO_EVENT_CAPACITY);
        auto &e = oracle.events[oracle.count++];
        e = {}; e.kind = kind; return e;
    }
    nhlo_result respond(nhlo_event &e) {
        e.result = oracle.response[e.kind];
        hook(&oracle, &e, this);
        return e.result;
    }
    static ggv_value read(void *p, ggv_binding b) {
        auto &h = *static_cast<HandLifecycleFixture *>(p);
        auto &s = h.passed; auto &player = *h.oracle.vars.currentplayer;
        ggv_value v{};
        switch (b) {
#define HL_READ(binding, member, expression) case binding: v.member = (expression); break
        HL_READ(GGV_HAND_NUMBER, i, h.handnum);
        HL_READ(GGV_PASSED_HAND, ref, &s);
        HL_READ(GGV_EMPTY_LVUPDATE, i, h.update);
        HL_READ(GGV_WEAPON_NUMBER, i, h.info.weaponnum);
        HL_READ(GGV_WEAPON_FUNCTION, i, s.gset.weaponfunc);
        HL_READ(GGV_STATE_VALUE, i, s.state);
        HL_READ(GGV_STATE_CYCLES, i, s.statecycles);
        HL_READ(GGV_STATE_MINOR, i, s.stateminor);
        HL_READ(GGV_STATE_FLAGS, u, s.stateflags);
        HL_READ(GGV_EMPTY_ANIM_MODE, i, s.animmode);
        HL_READ(GGV_TRIGGER, b, s.triggeron);
        HL_READ(GGV_RELOAD_PLAYER_DEAD, b, player.isdead);
        HL_READ(GGV_RELOAD_OTHER_STATE, i, h.other->state);
        HL_READ(GGV_RELOAD_OTHER_STATEFRAMES, i, h.other->stateframes);
        HL_READ(GGV_RELOAD_FUNC_AMMO_INDEX, i, h.captured->ammoindex);
        HL_READ(GGV_RELOAD_AMMO_ANIMATION, ref, h.info.definition->ammos[h.captured->ammoindex]->reload_animation);
        HL_READ(GGV_RELOAD_FUNC_AMMO_FLAGS, u, h.info.definition->ammos[h.captured->ammoindex]->flags);
        HL_READ(GGV_RELOAD_JPN, b, h.oracle.version >= NHLO_VERSION_JPN_FINAL);
        HL_READ(GGV_RELOAD_DEVICES_ACTIVE, u, player.devicesactive);
        HL_READ(GGV_RELOAD_DEVICES_INHIBIT, u, player.devicesinhibit);
        HL_READ(GGV_RELOAD_COUNT60, i, s.count60);
        HL_READ(GGV_RELOAD_COUNT, i, s.count);
        HL_READ(GGV_RELOAD_VISIBLE, b, s.visible);
        HL_READ(GGV_RELOAD_PAUSETIME60, i, s.pausetime60);
        HL_READ(GGV_RELOAD_LVUPDATE240, i, h.oracle.vars.lvupdate240);
        HL_READ(GGV_RELOAD_CAMERA_MODE, i, player.cameramode);
        HL_READ(GGV_RELOAD_PLAYER_INVINCIBLE, b, h.oracle.playerinvincible);
        HL_READ(GGV_RELOAD_WEAPON_BRIEFCASE2, i, nhloWeaponBriefcase2(h.oracle.version));
        HL_READ(GGV_ARM_PITCH_ANGLE, f, h.angle);
        HL_READ(GGV_ARM_PITCH_MATRIX, ref, &s.posrotmtx);
        HL_READ(GGV_LOADED_GUNMEMOWNER, i, player.gunctrl.gunmemowner);
        HL_READ(GGV_LOADED_GUNMEMTYPE, i, player.gunctrl.gunmemtype);
        HL_READ(GGV_LOADED_GUNMEMNEW, i, player.gunctrl.gunmemnew);
        HL_READ(GGV_LOADED_MASTERLOADSTATE, i, player.gunctrl.masterloadstate);
#undef HL_READ
        default: FAIL("Unexpected lifecycle read"); break;
        }
        return v;
    }
    static void write(void *p, ggv_binding b, ggv_value v) {
        auto &h = *static_cast<HandLifecycleFixture *>(p); auto &s = h.passed;
        auto &e = h.event(NHLO_EVENT_STORE); e.object = &s;
        switch (b) {
#define HL_WRITE(binding, fieldId, member, target) case binding: target = v.member; e.field = fieldId; e.stored_bits = uint32_t(target); break
        HL_WRITE(GGV_EMPTY_ANIM_MODE, NHLO_FIELD_ANIMMODE, i, s.animmode);
        HL_WRITE(GGV_ANIM_LOAD, NHLO_FIELD_ANIMLOAD, i, s.animload);
        HL_WRITE(GGV_RELOAD_GS_INT1, NHLO_FIELD_GS_INT1, i, s.gs_int1);
        HL_WRITE(GGV_RELOAD_GS_INT2, NHLO_FIELD_GS_INT2, i, s.gs_int2);
        HL_WRITE(GGV_STATE_MINOR, NHLO_FIELD_STATEMINOR, i, s.stateminor);
        HL_WRITE(GGV_STATE_FRAMES, NHLO_FIELD_STATEFRAMES, i, s.stateframes);
        HL_WRITE(GGV_STATE_CYCLES, NHLO_FIELD_STATECYCLES, i, s.statecycles);
        HL_WRITE(GGV_STATE_LAST_FRAME, NHLO_FIELD_STATELASTFRAME, i, s.statelastframe);
        HL_WRITE(GGV_STATE_FLAGS, NHLO_FIELD_STATEFLAGS, u, s.stateflags);
        HL_WRITE(GGV_RELOAD_UNK0D0E_07, NHLO_FIELD_UNK0D0E_07, b, s.unk0d0e_07);
        HL_WRITE(GGV_RELOAD_INCREMENTAL, NHLO_FIELD_INCREMENTALRELOADING, b, s.incrementalreloading);
        HL_WRITE(GGV_RELOAD_EJECT_STATE, NHLO_FIELD_EJECTSTATE, i, s.ejectstate);
        HL_WRITE(GGV_RELOAD_MODE, NHLO_FIELD_MODE, u, s.mode);
        HL_WRITE(GGV_RELOAD_PAUSETIME60, NHLO_FIELD_PAUSETIME60, i, s.pausetime60);
        HL_WRITE(GGV_RELOAD_COUNT60, NHLO_FIELD_COUNT60, i, s.count60);
        HL_WRITE(GGV_RELOAD_COUNT, NHLO_FIELD_COUNT, i, s.count);
        HL_WRITE(GGV_RELOAD_UNK0CC8_02, NHLO_FIELD_UNK0CC8_02, b, s.unk0cc8_02);
        HL_WRITE(GGV_WEAPON_FUNCTION, NHLO_FIELD_WEAPONFUNC, i, s.gset.weaponfunc);
        HL_WRITE(GGV_USE_POSROT, NHLO_FIELD_USEPOSROT, b, s.useposrot);
#undef HL_WRITE
        case GGV_RELOAD_DOAUTOSELECT:
            h.oracle.vars.currentplayer->doautoselect = v.i;
            e.object = h.oracle.vars.currentplayer; e.field = NHLO_FIELD_DOAUTOSELECT;
            e.stored_bits = uint32_t(v.i); break;
        case GGV_RIGHT_HAND_UNK0DD4:
            h.oracle.vars.currentplayer->hands[0].unk0dd4 = v.i;
            e.object = &h.oracle.vars.currentplayer->hands[0]; e.field = NHLO_FIELD_UNK0DD4;
            e.stored_bits = uint32_t(v.i); break;
        case GGV_ARM_PITCH_M30: case GGV_ARM_PITCH_M31: case GGV_ARM_PITCH_M32: {
            const int column = int(b) - int(GGV_ARM_PITCH_M30);
            s.posrotmtx.m[3][column] = v.f;
            e.field = nhlo_field(int(NHLO_FIELD_POSROTMTX_M30) + column);
            e.stored_bits = nhloFloatBits(v.f); break;
        }
        default: FAIL("Unexpected lifecycle write"); break;
        }
    }
    static ggv_value call(void *p, ggv_service service, ggv_value a, ggv_value b, ggv_value c) {
        auto &h = *static_cast<HandLifecycleFixture *>(p); ggv_value v{};
        if (service == GGV_RECOIL_TICKS) {
            v.i = h.oracle.pal ? a.i * 50 / 60 : a.i; return v;
        }
        if (service == GGV_RELOAD_SELECT_OTHER_HAND) {
            h.other = &h.oracle.vars.currentplayer->hands[a.i];
            auto &e = h.event(NHLO_EVENT_SELECT_OTHERHAND);
            e.p[0] = h.other; e.p[1] = h.oracle.vars.currentplayer; e.i[0] = h.handnum;
            return v;
        }
        nhlo_event_kind kind = NHLO_EVENT_KIND_COUNT;
        switch (service) {
        case GGV_RELOAD_CAPTURE_FUNCTION: kind = NHLO_CALL_GSET_GET_WEAPON_FUNCTION; break;
        case GGV_EMPTY_SET_STATE: kind = NHLO_CALL_BGUN_SET_STATE; break;
        case GGV_HAND_START_ANIMATION: kind = NHLO_CALL_BGUN_START_ANIMATION; break;
        case GGV_HAND_ANIM_A44: kind = NHLO_CALL_BGUN_0F098A44; break;
        case GGV_RELOAD_CHILD_TRANSFER: kind = NHLO_CALL_BGUN_0F098DF8; break;
        case GGV_RELOAD_AMMO_STATUS: kind = NHLO_CALL_BGUN_0F098CA0; break;
        case GGV_RELOAD_SET_ARM_PITCH: kind = NHLO_CALL_BGUN_SET_ARM_PITCH; break;
        case GGV_RELOAD_IS_LOADED: kind = NHLO_CALL_BGUN_IS_LOADED; break;
        case GGV_RELOAD_SOUND_START: kind = NHLO_CALL_SND_START; break;
        case GGV_RELOAD_FILENUM2: kind = NHLO_CALL_WEAPON_GET_FILE_NUM2; break;
        case GGV_RELOAD_WEAPON_HAS_FLAG: kind = NHLO_CALL_WEAPON_HAS_FLAG; break;
        case GGV_CHANGEFUNC_PRI_TO_SEC_ANIM: kind = NHLO_CALL_GSET_GET_PRI_TO_SEC_ANIM; break;
        case GGV_CHANGEFUNC_SEC_TO_PRI_ANIM: kind = NHLO_CALL_GSET_GET_SEC_TO_PRI_ANIM; break;
        case GGV_ARM_PITCH_X_ROTATION: kind = NHLO_CALL_MTX4_LOAD_X_ROTATION; break;
        case GGV_ARM_PITCH_COSF: kind = NHLO_CALL_COSF; break;
        case GGV_ARM_PITCH_SINF: kind = NHLO_CALL_SINF; break;
        default: FAIL("Unexpected lifecycle service"); return v;
        }
        auto &e = h.event(kind);
        switch (kind) {
        case NHLO_CALL_GSET_GET_WEAPON_FUNCTION:
        case NHLO_CALL_GSET_GET_PRI_TO_SEC_ANIM:
        case NHLO_CALL_GSET_GET_SEC_TO_PRI_ANIM: e.p[0] = &h.passed.gset; break;
        case NHLO_CALL_BGUN_SET_STATE: e.i[0] = a.i; e.i[1] = b.i; break;
        case NHLO_CALL_BGUN_START_ANIMATION: e.p[0] = a.ref; e.i[0] = b.i; e.p[1] = c.ref; break;
        case NHLO_CALL_BGUN_0F098A44: e.p[0] = a.ref; e.i[0] = b.i; break;
        case NHLO_CALL_BGUN_0F098DF8:
            e.u[0] = uint8_t(b.i); e.u[1] = uint8_t(c.i);
            [[fallthrough]];
        case NHLO_CALL_BGUN_0F098CA0: e.i[0] = a.i; e.p[0] = &h.info; e.p[1] = &h.passed; break;
        case NHLO_CALL_BGUN_SET_ARM_PITCH: e.p[0] = &h.passed; e.f = a.f; break;
        case NHLO_CALL_SND_START:
            e.i[0] = std::bit_cast<int32_t>(h.oracle.var80095200);
            e.i[1] = int16_t(NHLO_SFX_RELOAD_DEFAULT);
            for (int i = 2; i < 6; ++i) e.i[i] = -1;
            e.f = -1.0f; break;
        case NHLO_CALL_WEAPON_GET_FILE_NUM2: e.i[0] = a.i; break;
        case NHLO_CALL_WEAPON_HAS_FLAG: e.i[0] = a.i; e.u[0] = b.u; break;
        case NHLO_CALL_MTX4_LOAD_X_ROTATION: e.p[0] = b.ref; e.f = a.f; break;
        case NHLO_CALL_COSF: case NHLO_CALL_SINF: e.f = a.f; break;
        default: break;
        }
        e.f_bits = nhloFloatBits(e.f);
        const auto result = h.respond(e);
        switch (kind) {
        case NHLO_CALL_GSET_GET_WEAPON_FUNCTION:
            h.captured = static_cast<nhlo_weaponfunc *>(result.p);
            v.ref = result.p; break;
        case NHLO_CALL_GSET_GET_PRI_TO_SEC_ANIM:
        case NHLO_CALL_GSET_GET_SEC_TO_PRI_ANIM: v.ref = result.p; break;
        case NHLO_CALL_COSF: case NHLO_CALL_SINF: v.f = result.f; break;
        case NHLO_CALL_WEAPON_GET_FILE_NUM2: v.i = uint16_t(result.i); break;
        default: v.i = result.i; break;
        }
        return v;
    }
    int graph(ggv_stock_program program) {
        ggv_host host{this, read, write, call};
        int32_t result = -987;
        REQUIRE(ggvExecute(ggvStockProgram(program), &host, &result));
        return result;
    }
    int native(ggv_stock_program program) {
        switch (program) {
        case GGV_TICK_INC_RELOAD: return nhloTickIncReload(&oracle, &info, handnum, &passed, update);
        case GGV_TICK_INC_CHANGE_FUNC: return nhloTickIncChangeFunc(&oracle, &info, handnum, &passed, update);
        case GGV_SET_ARM_PITCH: nhloSetArmPitch(&oracle, &passed, angle); return 0;
        case GGV_IS_LOADED: return nhloIsLoaded(&oracle);
        case GGV_IS_RELOADING: return nhloIsReloading(&oracle, &passed);
        default: FAIL("Unexpected native donor"); return 0;
        }
    }
    // Compare identities by named fixture object, never by copied addresses.
    int identity(const void *p) const {
        if (!p) return 0;
        if (p == &passed) return 1;
        if (p == &passed.gset) return 1; // first member has the same native address
        if (p == &passed.posrotmtx) return 2;
        if (p == &function) return 3;
        if (p == &command) return 4;
        if (p == &info) return 5;
        for (int i = 0; i < 2; ++i) {
            if (p == &players[i]) return 10 + i;
            for (int j = 0; j < 2; ++j)
                if (p == &players[i].hands[j]) return 20 + i * 2 + j;
        }
        FAIL("Unknown fixture pointer"); return -1;
    }
    void compare(const HandLifecycleFixture &n) const {
        REQUIRE(nhloHandEqual(&passed, &n.passed));
        for (int i = 0; i < 2; ++i) REQUIRE(nhloPlayerEqual(&players[i], &n.players[i]));
        REQUIRE(identity(oracle.vars.currentplayer) == n.identity(n.oracle.vars.currentplayer));
        REQUIRE(oracle.dropped == 0); REQUIRE(n.oracle.dropped == 0);
        REQUIRE(oracle.count == n.oracle.count);
        for (uint32_t i = 0; i < oracle.count; ++i) {
            const auto &a = oracle.events[i]; const auto &b = n.oracle.events[i];
            INFO("event " << i << " " << nhloEventKindName(b.kind));
            REQUIRE(a.kind == b.kind); REQUIRE(a.field == b.field);
            REQUIRE(identity(a.object) == n.identity(b.object));
            REQUIRE(a.stored_bits == b.stored_bits);
            for (int j = 0; j < 2; ++j) {
                REQUIRE(identity(a.p[j]) == n.identity(b.p[j]));
                REQUIRE(a.u[j] == b.u[j]);
            }
            for (int j = 0; j < 6; ++j) REQUIRE(a.i[j] == b.i[j]);
            REQUIRE(a.f_bits == b.f_bits);
        }
    }
};

template<class Configure>
void handLifecycleParity(ggv_stock_program program, Configure configure) {
    HandLifecycleFixture native, graph;
    configure(native); configure(graph); // each fixture binds its own pointers
    REQUIRE(graph.graph(program) == native.native(program));
    graph.compare(native);
}
}

TEST_CASE("Hand lifecycle change function preserves raw state and callback owner", "[ggv][hand-lifecycle]") {
    for (int cycles : {0, 1}) for (int function : {0, 1, 255})
    for (int mode : {0, 1, 2, 3}) for (int accepted : {0, 1, -1})
    for (bool command : {false, true}) {
        handLifecycleParity(GGV_TICK_INC_CHANGE_FUNC, [&](auto &h) {
            h.handnum = 1; h.passed.statecycles = cycles;
            h.passed.gset.weaponfunc = uint8_t(function); h.passed.animmode = mode;
            h.oracle.response[NHLO_CALL_BGUN_SET_STATE].i = accepted;
            if (!command) {
                h.oracle.response[NHLO_CALL_GSET_GET_PRI_TO_SEC_ANIM].p = nullptr;
                h.oracle.response[NHLO_CALL_GSET_GET_SEC_TO_PRI_ANIM].p = nullptr;
            }
            h.switchOnAnimation = true;
        });
    }
}

TEST_CASE("Hand lifecycle loaded reloading and pitch match native donors", "[ggv][hand-lifecycle]") {
    for (int owner : {0, 1, 255}) for (int type : {-1, 0, 1})
    for (int pending : {-1, 0, 1}) for (int load : {0, 3, 4, 255})
        handLifecycleParity(GGV_IS_LOADED, [&](auto &h) {
            auto &g = h.players[0].gunctrl;
            g.gunmemowner = uint8_t(owner); g.gunmemtype = type;
            g.gunmemnew = pending; g.masterloadstate = uint8_t(load);
        });
    for (int state : {-1, 0, 1, 2, 7})
        handLifecycleParity(GGV_IS_RELOADING, [&](auto &h) { h.passed.state = state; });
    for (float angle : {-0.0f, 0.0f, -0.5f, NHLO_MAX_PITCH})
        handLifecycleParity(GGV_SET_ARM_PITCH, [&](auto &h) { h.angle = angle; });
}

TEST_CASE("Hand lifecycle reload covers fallthrough refusal and incremental order", "[ggv][hand-lifecycle]") {
    for (int minor : {0, 1, 2, 3, 9}) for (int cycles : {0, 1})
    for (int count : {0, 2, 24}) for (int accepted : {0, 1, -1})
    for (int incremental : {0, int(NHLO_AMMOFLAG_INCREMENTALRELOAD)})
    for (int ready : {0, 1, -1}) for (int status : {-1, 0, 2}) {
        handLifecycleParity(GGV_TICK_INC_RELOAD, [&](auto &h) {
            h.passed.stateminor = minor; h.passed.statecycles = cycles;
            h.passed.count = count; h.passed.count60 = count;
            h.ammo[0].flags = uint8_t(incremental);
            h.oracle.response[NHLO_CALL_BGUN_SET_STATE].i = accepted;
            h.oracle.response[NHLO_CALL_BGUN_0F098A44].i = ready;
            h.oracle.response[NHLO_CALL_BGUN_0F098CA0].i = status;
        });
    }
}

TEST_CASE("Hand lifecycle reload retains regional timing lazy sound and captured references", "[ggv][hand-lifecycle]") {
    for (int version : {NHLO_VERSION_NTSC_BETA, NHLO_VERSION_NTSC_FINAL, NHLO_VERSION_JPN_FINAL})
    for (int pal : {0, 1}) for (int weapon = 0; weapon <= 0x50; ++weapon)
    for (int gate = 0; gate < 6; ++gate) {
        handLifecycleParity(GGV_TICK_INC_RELOAD, [&](auto &h) {
            h.oracle.version = version; h.oracle.pal = pal; h.info.weaponnum = weapon;
            h.passed.stateminor = NHLO_HANDSTATEMINOR_RELOAD_SOUND; h.passed.statecycles = 1;
            h.passed.pausetime60 = 17;
            if (gate == 1) h.oracle.vars.lvupdate240 = 0;
            if (gate == 2) h.players[0].cameramode = NHLO_CAMERAMODE_THIRDPERSON;
            if (gate == 3) h.oracle.response[NHLO_CALL_BGUN_IS_LOADED].i = 0;
            if (gate == 4) h.oracle.playerinvincible = -1;
            if (gate == 5) {
                h.players[0].isdead = -1;
                h.oracle.response[NHLO_CALL_BGUN_SET_STATE].i = 0;
            }
        });
    }
    for (int hand : {0, 1}) for (int frames : {0, 16, 19, 20})
    for (int pal : {0, 1}) {
        handLifecycleParity(GGV_TICK_INC_RELOAD, [&](auto &h) {
            h.handnum = hand; h.oracle.pal = pal;
            h.passed.stateminor = NHLO_HANDSTATEMINOR_RELOAD_WAIT;
            h.players[0].hands[1 - hand].state = NHLO_HANDSTATE_RELOAD;
            h.players[0].hands[1 - hand].stateframes = frames;
        });
    }
    for (int inhibit : {0, int(NHLO_DEVICE_EYESPY)}) for (int trigger : {0, -1})
        handLifecycleParity(GGV_TICK_INC_RELOAD, [&](auto &h) {
            h.oracle.version = NHLO_VERSION_JPN_FINAL;
            h.passed.statecycles = 1; h.passed.triggeron = trigger;
            h.ammo[0].flags = NHLO_AMMOFLAG_INCREMENTALRELOAD;
            h.oracle.response[NHLO_CALL_BGUN_0F098A44].i = -1;
            h.mutateOnTransfer = true;
            h.players[1].devicesactive = NHLO_DEVICE_EYESPY;
            h.players[1].devicesinhibit = inhibit;
        });
}

TEST_CASE("Hand lifecycle reload raises with ordered flag queries and nullable animation", "[ggv][hand-lifecycle]") {
    for (uint32_t flags : {0u, 0x10u, 0x80u, 0x90u})
    for (int incremental : {0, int(NHLO_AMMOFLAG_INCREMENTALRELOAD)})
    for (int ready : {0, 1, -1})
        handLifecycleParity(GGV_TICK_INC_RELOAD, [&](auto &h) {
            h.passed.statecycles = 1; h.passed.stateflags = flags;
            h.passed.incrementalreloading = 1;
            h.ammo[0].flags = uint8_t(incremental);
            h.oracle.response[NHLO_CALL_BGUN_0F098A44].i = ready;
        });
    for (int count : {-2, 0, 18, 22, 23, 24}) for (int pal : {0, 1})
    for (int file : {0, 1, 65536}) for (int first : {0, 1, -1})
    for (int second : {0, 1, -1})
        handLifecycleParity(GGV_TICK_INC_RELOAD, [&](auto &h) {
            h.passed.stateminor = NHLO_HANDSTATEMINOR_RELOAD_RAISE;
            h.passed.statecycles = 1; h.passed.count60 = count;
            h.oracle.pal = pal;
            h.oracle.response[NHLO_CALL_WEAPON_GET_FILE_NUM2].i = file;
            h.flags40 = first; h.flags80 = second;
        });
    for (int slot : {-1, 0, 1, 2}) for (bool function : {false, true})
    for (bool animation : {false, true}) for (int accepted : {0, 1})
        handLifecycleParity(GGV_TICK_INC_RELOAD, [&](auto &h) {
            h.function.ammoindex = int8_t(slot);
            if (!function) h.oracle.response[NHLO_CALL_GSET_GET_WEAPON_FUNCTION].p = nullptr;
            if (!animation) {
                h.ammo[0].reload_animation = nullptr; h.ammo[1].reload_animation = nullptr;
            }
            h.oracle.response[NHLO_CALL_BGUN_SET_STATE].i = accepted;
        });
}

namespace {
struct AnimationEligibilityHost {
    uint8_t condition = 0, function = 0;
    int8_t leftInuse = 0;
    std::vector<ggv_binding> reads;
    static ggv_value read(void *p, ggv_binding binding) {
        auto &h = *static_cast<AnimationEligibilityHost *>(p); h.reads.push_back(binding);
        ggv_value v{};
        switch (binding) {
        case GGV_ANIM_ELIGIBILITY_KIND: v.i = h.condition; break;
        case GGV_ANIM_ELIGIBILITY_LEFT_INUSE: v.i = h.leftInuse; break;
        case GGV_WEAPON_FUNCTION: v.i = h.function; break;
        default: FAIL("Unexpected animation eligibility field"); break;
        }
        return v;
    }
    static void write(void *, ggv_binding, ggv_value) { FAIL("Eligibility cannot store"); }
    // Native bgun0f098884 donor, preserving independent ifs and raw == true.
    int native() const {
        int result = 0;
        if (condition == 0) return 1;
        if (condition == 1 && leftInuse == 1) result = 1;
        if (condition == 2 && function == 1) result = 1;
        return result;
    }
};
}
TEST_CASE("Animation eligibility keeps exact native bool equality and lazy field reads", "[ggv][hand-lifecycle]") {
    for (int condition : {0, 1, 2, 3, 255}) for (int inuse : {-128, -1, 0, 1, 2, 127})
    for (int function : {0, 1, 2, 255}) {
        AnimationEligibilityHost h;
        h.condition = uint8_t(condition); h.leftInuse = int8_t(inuse); h.function = uint8_t(function);
        ggv_host host{&h, AnimationEligibilityHost::read, AnimationEligibilityHost::write};
        int32_t result = -1;
        REQUIRE(ggvExecute(ggvStockProgram(GGV_ANIMATION_ELIGIBILITY), &host, &result));
        REQUIRE(result == h.native());
        std::vector<ggv_binding> expected{GGV_ANIM_ELIGIBILITY_KIND};
        if (condition != 0) {
            expected.push_back(GGV_ANIM_ELIGIBILITY_KIND);
            if (condition == 1) expected.push_back(GGV_ANIM_ELIGIBILITY_LEFT_INUSE);
            expected.push_back(GGV_ANIM_ELIGIBILITY_KIND);
            if (condition == 2) expected.push_back(GGV_WEAPON_FUNCTION);
        }
        REQUIRE(h.reads == expected);
    }
}

namespace {
struct AnimationFrameHost {
    nhlo_frame_case state{};
    float result = -999.0f;
    int commandToken = 0;
    std::vector<ggv_binding> reads;
    int writes = 0;
    static ggv_value read(void *p, ggv_binding binding) {
        auto &h = *static_cast<AnimationFrameHost *>(p);
        h.reads.push_back(binding); ggv_value value{};
        switch (binding) {
        case GGV_EMPTY_ANIM_MODE: value.i = h.state.animmode; break;
        case GGV_ANIM_FRAME_COMMAND:
            value.ref = h.state.command_present ? &h.commandToken : nullptr; break;
        case GGV_ANIM_FRAME_COMMAND_VALUE: value.i64 = h.state.command_value; break;
        default: FAIL("Unexpected frame binding"); break;
        }
        return value;
    }
    static void write(void *p, ggv_binding binding, ggv_value value) {
        auto &h = *static_cast<AnimationFrameHost *>(p);
        REQUIRE(binding == GGV_ANIM_FRAME_RESULT);
        ++h.writes; h.result = value.f;
    }
    static ggv_value call(void *p, ggv_service service, ggv_value, ggv_value, ggv_value) {
        auto &h = *static_cast<AnimationFrameHost *>(p); ggv_value value{};
        if (service == GGV_MODEL_ANIM_FRAME_COUNT) {
            ++h.state.count_queries; value.i = h.state.frame_count;
        } else if (service == GGV_MODEL_ANIM_CURRENT_FRAME) {
            ++h.state.current_queries; value.f = h.state.current_frame;
        } else FAIL("Unexpected frame service");
        return value;
    }
};
}
TEST_CASE("Animation frame preserves full-width direction and native float results", "[ggv][hand-lifecycle]") {
    const int64_t commands[] = {INT64_MIN, -4294967296LL, -1, 0, 1, 4294967296LL, INT64_MAX};
    for (int mode : {0, 1, 2, 3}) for (int present : {0, 1})
    for (int64_t command : commands) for (int count : {-1, 0, 17, 16777217})
    for (float current : {-0.0f, 0.0f, -0.5f, 1.25f, 23.0f}) {
        AnimationFrameHost h;
        h.state = {mode, present, command, count, current, 0, 0};
        auto native = h.state;
        const float expected = nhloAnimationFrame(&native);
        ggv_host host{&h, AnimationFrameHost::read, AnimationFrameHost::write, AnimationFrameHost::call};
        int32_t status = -1;
        REQUIRE(ggvExecute(ggvStockProgram(GGV_ANIMATION_FRAME), &host, &status));
        REQUIRE(status == 0); REQUIRE(h.writes == 1);
        REQUIRE(nhloFloatBits(h.result) == nhloFloatBits(expected));
        REQUIRE(h.state.count_queries == native.count_queries);
        REQUIRE(h.state.current_queries == native.current_queries);
        std::vector<ggv_binding> reads{GGV_EMPTY_ANIM_MODE};
        if (mode == 2) {
            reads.push_back(GGV_ANIM_FRAME_COMMAND);
            if (present) reads.push_back(GGV_ANIM_FRAME_COMMAND_VALUE);
        }
        REQUIRE(h.reads == reads);
    }
}
TEST_CASE("Signed64 graph comparison is typed and preserves high bits", "[ggv][integer]") {
    const int64_t values[] = {INT64_MIN, -4294967296LL, -1, 0, 1, 4294967296LL, INT64_MAX};
    for (int64_t left : values) for (int64_t right : values) {
        ggv_source source{}; source.count = 4;
        for (unsigned i = 0; i < 2; ++i) {
            source.nodes[i].op = GGV_CONST; source.nodes[i].type = GGV_I64;
            source.nodes[i].dst = i;
        }
        source.nodes[0].literal.i64 = left; source.nodes[1].literal.i64 = right;
        source.nodes[2].op = GGV_I64_LT; source.nodes[2].dst = 2;
        source.nodes[2].a = 0; source.nodes[2].b = 1;
        source.nodes[3].op = GGV_RETURN_BOOL; source.nodes[3].a = 2;
        ggv_program program{};
        REQUIRE(ggvPrepare(&source, &program, nullptr, 0));
        AnimationFrameHost h;
        ggv_host host{&h, AnimationFrameHost::read, AnimationFrameHost::write};
        int32_t result = -1;
        REQUIRE(ggvExecute(&program, &host, &result)); REQUIRE(result == (left < right));
        REQUIRE(h.reads.empty()); REQUIRE(h.writes == 0);
        for (ggv_type bad : {GGV_I32, GGV_U32, GGV_F32, GGV_BOOL, GGV_REF}) {
            auto invalid = source;
            invalid.nodes[0].type = bad; invalid.nodes[0].literal = {};
            REQUIRE_FALSE(ggvPrepare(&invalid, &program, nullptr, 0));
        }
    }
}

TEST_CASE("Animation frame refuses missing backend without reading or publishing", "[ggv][hand-lifecycle]") {
    AnimationFrameHost h;
    ggv_host host{&h, AnimationFrameHost::read, AnimationFrameHost::write};
    int32_t result = 1234;
    REQUIRE_FALSE(ggvExecute(ggvStockProgram(GGV_ANIMATION_FRAME), &host, &result));
    REQUIRE(result == 1234); REQUIRE(h.result == -999.0f);
    REQUIRE(h.reads.empty()); REQUIRE(h.writes == 0);
    REQUIRE(h.state.count_queries == 0); REQUIRE(h.state.current_queries == 0);
}

namespace {
struct AnimationWaitHost {
    nhlo_wait_case state{};
    const nhlo_wait_command *cursor = nullptr;
    std::vector<ggv_binding> reads;
    static ggv_value read(void *p, ggv_binding b) {
        auto &h=*static_cast<AnimationWaitHost *>(p);h.reads.push_back(b);ggv_value v{};
        switch(b) {
        case GGV_EMPTY_ANIM_MODE:v.i=h.state.mode;break;
        case GGV_ANIM_LOAD:v.i=h.state.load;break;
        case GGV_ANIMATION_LATCH:v.b=h.state.latch;break;
        case GGV_RECOVERY_PAL:v.b=h.state.pal;break;
        case GGV_ANIM_WAIT_TIME:v.i=h.state.time;break;
        case GGV_ANIM_WAIT_CURSOR:v.ref=const_cast<nhlo_wait_command *>(h.cursor);break;
        case GGV_ANIM_WAIT_TYPE:v.i=h.cursor->type;break;
        case GGV_ANIM_WAIT_KEY:v.i=h.cursor->key;break;
        case GGV_ANIM_WAIT_VALUE:v.i64=h.cursor->value;break;
        case GGV_ANIM_WAIT_INCREMENT:v.i=h.state.increment;break;
        case GGV_ANIM_WAIT_INCREMENT_REAL:v.f=h.state.increment_real;break;
        default:FAIL("unexpected wait binding");
        }return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v) {
        REQUIRE(b==GGV_ANIM_WAIT_CURSOR);
        static_cast<AnimationWaitHost *>(p)->cursor=static_cast<nhlo_wait_command *>(v.ref);
    }
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value,ggv_value) {
        auto &h=*static_cast<AnimationWaitHost *>(p);ggv_value v{};
        if(service==GGV_ANIM_CURSOR_NEXT) {
            v.ref=static_cast<nhlo_wait_command *>(a.ref)+1;++h.state.advances;
        }else if(service==GGV_ANIM_WAIT_FRAME) {
            REQUIRE(h.state.frame_queries<2);v.f=h.state.frames[h.state.frame_queries++];
        }else FAIL("unexpected wait service");
        return v;
    }
};
TEST_CASE("animation waits preserve native sentinel traversal and regional frame checks","[gameplay][graph][animation][parity]") {
    const nhlo_wait_command commands[]={{3,1,0},{4,2,INT64_C(4294967296)},
        {4,4,-1},{3,7,0},{4,65535,2},{0,0,0}};
    for(int pal:{0,1})for(int mode:{0,1,2})for(int load:{-1,0})for(int latch:{0,1})
    for(int time:{-1,0,2})for(float frame:{-0.0f,1.9f,4.0f,65535.0f})
    for(auto program:{GGV_ANIMATION_WAIT,GGV_ANIMATION_WAIT_KEY}) {
        AnimationWaitHost h;h.state.commands=commands;h.cursor=commands;
        h.state.pal=pal;h.state.mode=mode;h.state.load=load;h.state.latch=latch;
        h.state.time=time;h.state.increment=1;h.state.increment_real=0.25f;
        h.state.frames[0]=frame;h.state.frames[1]=frame+0.5f;
        auto native=h.state;
        int expected=program==GGV_ANIMATION_WAIT?nhloAnimationWait(&native):nhloAnimationWaitKey(&native);
        ggv_host host{&h,AnimationWaitHost::read,AnimationWaitHost::write,AnimationWaitHost::call};
        int32_t result=-99;REQUIRE(ggvExecute(ggvStockProgram(program),&host,&result));
        REQUIRE(result==expected);REQUIRE(h.state.advances==native.advances);
        REQUIRE(h.state.frame_queries==native.frame_queries);
        REQUIRE(h.cursor==commands+native.advances);
        if(mode==0) REQUIRE(std::find(h.reads.begin(),h.reads.end(),GGV_ANIM_WAIT_TYPE)==h.reads.end());
    }
    // Native iteration count is not bounded by the graph's instruction budget.
    std::vector<nhlo_wait_command> longList(2049,{1,0,0});longList.back()={0,0,0};
    AnimationWaitHost h;h.state.commands=longList.data();h.cursor=longList.data();h.state.mode=1;
    ggv_host host{&h,AnimationWaitHost::read,AnimationWaitHost::write,AnimationWaitHost::call};
    int32_t result=-99;REQUIRE(ggvExecute(ggvStockProgram(GGV_ANIMATION_WAIT_KEY),&host,&result));
    REQUIRE(result==0);REQUIRE(h.state.advances==2048);
}
TEST_CASE("animation wait preserves ordered float comparisons for NaN","[gameplay][graph][animation]") {
    const nhlo_wait_command commands[]={{3,1,0},{4,4,0},{0,0,0}};
    for(int latch:{0,1}) {
        AnimationWaitHost h;h.cursor=commands;h.state.commands=commands;h.state.mode=1;
        h.state.pal=1;h.state.latch=latch;
        h.state.frames[0]=h.state.frames[1]=std::numeric_limits<float>::quiet_NaN();
        auto native=h.state;auto expected=nhloAnimationWait(&native);
        ggv_host host{&h,AnimationWaitHost::read,AnimationWaitHost::write,AnimationWaitHost::call};
        int32_t result=-99;REQUIRE(ggvExecute(ggvStockProgram(GGV_ANIMATION_WAIT),&host,&result));
        REQUIRE(result==expected);REQUIRE(result==0);REQUIRE(h.state.frame_queries==native.frame_queries);
    }
}
TEST_CASE("repeat rejects invalid control and first-iteration uninitialized reads","[gameplay][graph][validation]") {
    auto source=ggvStockProgram(GGV_ANIMATION_WAIT_KEY)->source;
    unsigned tail=0;for(unsigned i=0;i<source.count;++i)if(source.nodes[i].op==GGV_REPEAT_WHILE)tail=i;
    REQUIRE(tail>0);ggv_program prepared{};char error[128]{};
    REQUIRE(ggvPrepare(&source,&prepared,error,sizeof(error)));
    for(unsigned target:{tail,tail+1,unsigned(GGV_NODE_LIMIT)}) {
        auto bad=source;bad.nodes[tail].target=target;
        REQUIRE_FALSE(ggvPrepare(&bad,&prepared,error,sizeof(error)));
    }
    auto bad=source;bad.nodes[tail].a=0; // key is I32, never BOOL
    REQUIRE_FALSE(ggvPrepare(&bad,&prepared,error,sizeof(error)));
    bad=source;bad.nodes[0].op=GGV_I_INCREMENT;bad.nodes[0].a=0;
    REQUIRE_FALSE(ggvPrepare(&bad,&prepared,error,sizeof(error)));
}
struct AnimationStartHost {
    nhlo_start_case &state;
    nhlo_start_command *command, *cursor;
    void event(int kind,const nhlo_start_command *cmd) {
        REQUIRE(state.count<256);state.events[state.count++]={kind,cmd};
    }
    static ggv_value read(void *p,ggv_binding b) {
        auto &h=*static_cast<AnimationStartHost *>(p);ggv_value v{};
        switch(b) {
        case GGV_ANIM_START_COMMAND:v.ref=h.command;break;
        case GGV_ANIM_WAIT_CURSOR:v.ref=h.cursor;break;
        case GGV_ANIM_WAIT_TYPE:v.i=h.cursor->type;break;
        case GGV_ANIM_WAIT_KEY:v.i=h.cursor->key;break;
        case GGV_ANIM_START_CHILD:v.ref=h.cursor->child;break;
        case GGV_ANIM_START_PREVIOUS:v.ref=h.state.previous;break;
        case GGV_HAND_NUMBER:v.i=1;break;
        case GGV_PASSED_HAND:v.ref=&h.state;break;
        default:FAIL("unexpected animation start read");
        }return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v) {
        auto &h=*static_cast<AnimationStartHost *>(p);int kind=-1;
        switch(b) {
        case GGV_ANIM_WAIT_CURSOR:h.cursor=static_cast<nhlo_start_command *>(v.ref);return;
        case GGV_ANIM_LOAD:h.state.load=v.i;kind=NHLO_START_STORE_LOAD;break;
        case GGV_EMPTY_ANIM_MODE:h.state.mode=v.i;kind=NHLO_START_STORE_MODE;break;
        case GGV_ANIMATION_LATCH:h.state.latch=v.b;kind=NHLO_START_STORE_LATCH;break;
        case GGV_ANIM_INCREMENTAL:h.state.incremental=v.b;kind=NHLO_START_STORE_INCREMENTAL;break;
        case GGV_ANIM_COMMAND:h.state.command=static_cast<nhlo_start_command *>(v.ref);kind=NHLO_START_STORE_COMMAND;break;
        case GGV_ANIM_LOOP_COUNT:h.state.loop=v.i;kind=NHLO_START_STORE_LOOP;break;
        case GGV_ANIM_BIT02:h.state.bit02=v.b;kind=NHLO_START_STORE_BIT02;break;
        case GGV_ANIM_BIT07:h.state.bit07=v.b;kind=NHLO_START_STORE_BIT07;break;
        case GGV_ANIM_START_PREVIOUS:h.state.previous=static_cast<nhlo_start_command *>(v.ref);kind=NHLO_START_STORE_PREVIOUS;break;
        default:FAIL("unexpected animation start write");
        }h.event(kind,h.command);
    }
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value b,ggv_value c) {
        auto &h=*static_cast<AnimationStartHost *>(p);ggv_value v{};
        switch(service) {
        case GGV_ANIM_START_NAME:
            REQUIRE(a.ref==h.command);h.event(NHLO_START_NAME,h.command);
            if(h.state.name_present)v.ref=&h.state.name_present;break;
        case GGV_ANIM_START_SOURCE:
            REQUIRE(a.ref==h.command);h.event(NHLO_START_SOURCE,h.command);
            if(h.state.source_present)v.ref=&h.state.source_present;break;
        case GGV_ANIM_START_LOG_NULL:h.event(NHLO_START_LOG_NULL,nullptr);break;
        case GGV_ANIM_START_LOG_SOURCE:
            REQUIRE(a.ref==&h.state.name_present);REQUIRE(b.ref==&h.state.source_present);
            h.event(NHLO_START_LOG_SOURCE,h.command);break;
        case GGV_RNG_U32:h.event(NHLO_START_RNG,h.command);++h.state.rng_calls;v.u=h.state.rng_value;break;
        case GGV_ANIM_CURSOR_NEXT:v.ref=static_cast<nhlo_start_command *>(a.ref)+1;break;
        case GGV_ANIM_START_ELIGIBILITY:
            h.event(NHLO_START_ELIGIBILITY,static_cast<nhlo_start_command *>(a.ref));
            v.b=static_cast<nhlo_start_command *>(a.ref)->eligible;break;
        case GGV_HAND_START_ANIMATION:
            REQUIRE(b.i==1);REQUIRE(c.ref==&h.state);
            h.event(NHLO_START_CHILD,static_cast<nhlo_start_command *>(a.ref));
            run(h.state,static_cast<nhlo_start_command *>(a.ref));break;
        default:FAIL("unexpected animation start service");
        }return v;
    }
    static void run(nhlo_start_case &state,nhlo_start_command *command) {
        AnimationStartHost context{state,command,command};
        ggv_host host{&context,read,write,call};int32_t result=-99;
        REQUIRE(ggvExecute(ggvStockProgram(GGV_ANIMATION_START),&host,&result));REQUIRE(result==0);
    }
};
TEST_CASE("animation start preserves recursion RNG diagnostics and PLAY stores","[gameplay][graph][animation][parity]") {
    nhlo_start_command play{10,65535,1,nullptr};
    nhlo_start_command empty[]={{0,0,0,nullptr}};
    nhlo_start_command inner[]={{6,0,1,&play},{7,100,1,nullptr},{0,0,0,nullptr}};
    nhlo_start_command outer[]={{7,50,1,&play},{6,0,1,inner},{6,0,1,nullptr},{0,0,0,nullptr}};
    nhlo_start_command nullChild[]={{6,0,1,nullptr},{6,0,1,&play},{0,0,0,nullptr}};
    for(int name:{0,1})for(int source:{0,1})for(int eligibility:{0,1})
    for(uint32_t rng:{0u,49u,50u,99u,100u,UINT32_MAX})for(int samePrevious:{0,1})
    for(auto *command:{static_cast<nhlo_start_command *>(nullptr),&play,empty,inner,outer,nullChild}) {
        outer[0].eligible=eligibility;
        nhlo_start_case graph{};graph.name_present=name;graph.source_present=source;graph.rng_value=rng;
        graph.load=42;graph.mode=1;graph.latch=graph.incremental=graph.bit02=graph.bit07=1;
        graph.loop=13;graph.command=empty;graph.previous=samePrevious?&play:nullptr;
        auto native=graph;nhloAnimationStart(&native,command);AnimationStartHost::run(graph,command);
        REQUIRE(graph.load==native.load);REQUIRE(graph.mode==native.mode);REQUIRE(graph.latch==native.latch);
        REQUIRE(graph.incremental==native.incremental);REQUIRE(graph.loop==native.loop);
        REQUIRE(graph.bit02==native.bit02);REQUIRE(graph.bit07==native.bit07);
        REQUIRE(graph.command==native.command);REQUIRE(graph.previous==native.previous);
        REQUIRE(graph.rng_calls==native.rng_calls);REQUIRE(graph.count==native.count);
        for(unsigned i=0;i<graph.count;++i) {
            REQUIRE(graph.events[i].kind==native.events[i].kind);
            REQUIRE(graph.events[i].command==native.events[i].command);
        }
    }
}
TEST_CASE("unsigned remainder rejects zero divisor before any callbacks","[gameplay][graph][validation]") {
    auto source=ggvStockProgram(GGV_ANIMATION_START)->source;
    bool changed=false;for(unsigned i=0;i<source.count;++i)if(source.nodes[i].op==GGV_U_MOD_LITERAL) {
        source.nodes[i].literal.u=0;changed=true;
    }
    REQUIRE(changed);ggv_program prepared{};char error[128]{};
    REQUIRE_FALSE(ggvPrepare(&source,&prepared,error,sizeof(error)));
}
struct HandTickHost {
    nhlo_tick_case state{};
    nhlo_tick_hand *hand=nullptr;
    void event(int kind) {
        REQUIRE(state.calls<16);state.events[state.calls]=kind;state.players[state.calls++]=state.current;
    }
    static ggv_value read(void *p,ggv_binding b) {
        auto &h=*static_cast<HandTickHost *>(p);ggv_value v{};
        switch(b) {
        case GGV_STATE_VALUE:v.i=h.hand->state;break;
        case GGV_STATE_FRAMES:v.i=h.hand->frames;break;
        case GGV_STATE_CYCLES:v.i=h.hand->cycles;break;
        case GGV_EMPTY_LVUPDATE:v.i=h.state.update;break;
        case GGV_RELOAD_LVUPDATE240:v.i=h.state.update240;break;
        case GGV_COUNTDOWN_UPDATE60:v.i=h.state.update60;break;
        case GGV_RELOAD_COUNT60:v.i=h.hand->count60;break;
        case GGV_RELOAD_COUNT:v.i=h.hand->count;break;
        default:FAIL("unexpected parent tick read");
        }return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v) {
        auto &h=*static_cast<HandTickHost *>(p);
        switch(b) {
        case GGV_FIRING:h.hand->firing=v.b;break;
        case GGV_FLASH:h.hand->flash=v.b;break;
        case GGV_STATE_FRAMES:h.hand->frames=v.i;break;
        case GGV_STATE_LAST_FRAME:h.hand->lastframe=v.i;break;
        case GGV_STATE_CYCLES:h.hand->cycles=v.i;break;
        case GGV_RELOAD_COUNT60:h.hand->count60=v.i;break;
        case GGV_RELOAD_COUNT:h.hand->count=v.i;break;
        case GGV_USE_POSROT:h.hand->useposrot=v.b;break;
        default:FAIL("unexpected parent tick write");
        }
    }
    static ggv_value call(void *p,ggv_service service,ggv_value,ggv_value,ggv_value) {
        auto &h=*static_cast<HandTickHost *>(p);ggv_value v{};int event=-1;
        switch(service) {
        case GGV_TICK_DELIVERY_PENDING:
            h.event(100);if(h.state.pending_flip)h.state.current=1;v.b=h.state.pending;return v;
        case GGV_TICK_IS_V2:h.event(101);v.b=h.state.is_v2;return v;
        case GGV_TICK_ADVANCE_V2:event=102;break;
        case GGV_TICK_IDLE:event=0;break;
        case GGV_TICK_RELOAD:event=1;break;
        case GGV_TICK_ATTACK:event=4;break;
        case GGV_TICK_CHANGE_GUN:event=5;break;
        case GGV_TICK_EMPTY:event=3;break;
        case GGV_TICK_AUTO_SWITCH:event=8;break;
        case GGV_TICK_CHANGE_FUNC:event=7;break;
        default:FAIL("unexpected parent tick service");
        }
        h.event(event);auto *target=service==GGV_TICK_ADVANCE_V2?&h.state.hands[h.state.current]:h.hand;
        target->state=h.state.child_state;target->frames=17;target->cycles=5;
        v.i=h.state.child_result;return v;
    }
};
TEST_CASE("hand tick preserves captured ownership deferred delivery and state transitions","[gameplay][graph][hand][parity]") {
    for(int state:{-1,0,1,2,3,4,5,6,7,8,9})for(int pending:{0,1})for(int flip:{0,1})
    for(int v2:{0,1})for(int update240:{-1,0,4})for(int result:{-3,0,2})for(int transition:{0,1}) {
        HandTickHost h;h.state.hands[0]={state,11,3,7,8,9,1,1,1};
        h.state.hands[1]={42,43,44,45,46,47,1,1,1};h.hand=&h.state.hands[0];
        h.state.pending=pending;h.state.pending_flip=flip;h.state.is_v2=v2;
        h.state.update=2;h.state.update60=3;h.state.update240=update240;
        h.state.child_result=result;h.state.child_state=transition?state+1:state;
        auto native=h.state;int expected=nhloHandTick(&native);
        ggv_host host{&h,HandTickHost::read,HandTickHost::write,HandTickHost::call};int32_t actual=-99;
        REQUIRE(ggvExecute(ggvStockProgram(GGV_HAND_TICK),&host,&actual));REQUIRE(actual==expected);
        REQUIRE(h.state.current==native.current);REQUIRE(h.state.calls==native.calls);
        for(unsigned i=0;i<native.calls;++i) {
            REQUIRE(h.state.events[i]==native.events[i]);REQUIRE(h.state.players[i]==native.players[i]);
        }
        for(unsigned i=0;i<2;++i) {
            const auto &a=h.state.hands[i], &b=native.hands[i];
            REQUIRE(a.state==b.state);REQUIRE(a.frames==b.frames);REQUIRE(a.cycles==b.cycles);
            REQUIRE(a.lastframe==b.lastframe);REQUIRE(a.count60==b.count60);REQUIRE(a.count==b.count);
            REQUIRE(a.firing==b.firing);REQUIRE(a.flash==b.flash);REQUIRE(a.useposrot==b.useposrot);
        }
    }
}
struct TickHandHost {
    nhlo_tick_frame_case state{};
    static ggv_value read(void *p,ggv_binding b) {
        auto &h=*static_cast<TickHandHost *>(p);ggv_value v{};
        switch(b) {
        case GGV_COUNTDOWN_UPDATE60:v.i=h.state.update60;break;
        case GGV_ANIM_WAIT_INCREMENT:v.i=h.state.increment;break;
        case GGV_RECOVERY_PAL:v.b=h.state.modern;break;
        case GGV_RELOAD_PAL:v.b=h.state.pal;break;
        case GGV_ANIM_TICK_INCREMENT_LEGACY:v.u=h.state.increment_legacy;break;
        default:FAIL("unexpected frame tick read");
        }return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v) {
        auto &h=*static_cast<TickHandHost *>(p);
        switch(b) {
        case GGV_ANIM_WAIT_INCREMENT:h.state.increment=v.i;break;
        case GGV_ANIM_WAIT_INCREMENT_REAL:h.state.increment_real=v.f;break;
        case GGV_ANIM_TICK_INCREMENT_LEGACY:h.state.increment_legacy=v.u;break;
        default:FAIL("unexpected frame tick write");
        }
    }
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value,ggv_value) {
        auto &h=*static_cast<TickHandHost *>(p);ggv_value v{};
        switch(service) {
        case GGV_HAND_INFO_CAPTURE:++h.state.info_calls;h.state.update60=h.state.update_after_info;break;
        case GGV_MODEL_ABS_ANIM_SPEED:++h.state.speed_calls;v.f=h.state.speed;break;
        case GGV_HAND_TICK_CHILD:
            REQUIRE(h.state.child_calls<21);h.state.child_inputs[h.state.child_calls]=a.i;
            v.i=h.state.child_returns[h.state.child_calls++];break;
        default:FAIL("unexpected frame tick service");
        }return v;
    }
};
TEST_CASE("hand frame parent retains regional storage and at most twenty-one child calls","[gameplay][graph][hand][parity]") {
    for(int modern:{0,1})for(int pal:{0,1})for(int stop:{0,1,20,21})for(int final:{-1,0})
    for(int update:{-2,0,1,16777217})for(float speed:{0.0f,0.5f,1.0f,1.25f}) {
        if(!modern&&pal)continue; // no pre-PAL version is PAL
        TickHandHost h;h.state.modern=modern;h.state.pal=pal;h.state.update60=999;
        h.state.update_after_info=update;h.state.speed=speed;h.state.increment_legacy=UINT32_MAX;
        h.state.increment_real=-1.0f;
        for(int i=0;i<21;++i)h.state.child_returns[i]=i==stop?final:21-i;
        auto native=h.state;nhloTickHand(&native);
        ggv_host host{&h,TickHandHost::read,TickHandHost::write,TickHandHost::call};int32_t result=-99;
        REQUIRE(ggvExecute(ggvStockProgram(GGV_TICK_HAND),&host,&result));REQUIRE(result==0);
        REQUIRE(h.state.info_calls==native.info_calls);REQUIRE(h.state.speed_calls==native.speed_calls);
        REQUIRE(h.state.child_calls==native.child_calls);REQUIRE(h.state.increment==native.increment);
        REQUIRE(h.state.increment_legacy==native.increment_legacy);
        REQUIRE(nhloFloatBits(h.state.increment_real)==nhloFloatBits(native.increment_real));
        for(unsigned i=0;i<native.child_calls;++i)REQUIRE(h.state.child_inputs[i]==native.child_inputs[i]);
    }
}
struct AutoSwitchHost {
    nhlo_auto_case state{};
    nhlo_auto_other *other=nullptr;
    void event(int kind,int arg=0,float pitch=0) {
        REQUIRE(state.count<64);state.events[state.count++]={kind,arg,state.current,pitch};
    }
    static ggv_value read(void *p,ggv_binding b) {
        auto &h=*static_cast<AutoSwitchHost *>(p);ggv_value v{};
        switch(b) {
        case GGV_STATE_INUSE:v.i=h.state.hand.inuse;break;
        case GGV_STATE_MINOR:v.i=h.state.hand.stateminor;break;
        case GGV_STATE_FRAMES:v.i=h.state.hand.stateframes;break;
        case GGV_WEAPON_FUNCTION:v.i=h.state.hand.gset.weaponfunc;break;
        case GGV_WEAPON_NUMBER:v.i=h.state.weaponnum;break;
        case GGV_EMPTY_LVUPDATE:v.i=h.state.lvupdate;break;
        case GGV_ANIM_WAIT_INCREMENT:v.i=h.state.hand.animframeinc;break;
        case GGV_HAND_MODE_NEXT:v.u=h.state.hand.modenext;break;
        case GGV_RELOAD_PAL:v.b=h.state.pal;break;
        case GGV_RECOVERY_PAL:v.b=h.state.modern;break;
        case GGV_NORMAL_MULTIPLAYER:v.b=h.state.normal_multi;break;
        case GGV_MULTIPLAYER:v.b=h.state.multi;break;
        case GGV_RELOAD_DOAUTOSELECT:v.i=h.state.autoselect;break;
        case GGV_AUTO_OTHER_INUSE:REQUIRE(h.other);v.i=h.other->inuse;break;
        case GGV_AUTO_OTHER_FUNCTION:REQUIRE(h.other);v.i=h.other->function;break;
        case GGV_RELOAD_OTHER_STATE:REQUIRE(h.other);v.i=h.other->state;break;
        case GGV_AUTO_FRESH_OTHER_STATE:v.i=h.state.other[h.state.current].state;break;
        default:FAIL("unexpected autoswitch read");
        }return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v) {
        auto &h=*static_cast<AutoSwitchHost *>(p);
        switch(b) {
        case GGV_STATE_MINOR:h.state.hand.stateminor=v.i;break;
        case GGV_HAND_LASTDIRVALID:h.state.hand.lastdirvalid=v.b;break;
        case GGV_ANIM_TICK_INCREMENT_LEGACY:h.state.hand.animframeincfreal=v.u;break;
        case GGV_REMAINDER:h.state.hand.shotremainder=v.f;break;
        case GGV_WEAPON_FUNCTION:h.state.hand.gset.weaponfunc=v.i;break;
        case GGV_RELOAD_MODE:h.state.hand.mode=v.u;break;
        case GGV_HAND_MODE_NEXT:h.state.hand.modenext=v.u;break;
        case GGV_RELOAD_COUNT:h.state.hand.count=v.i;break;
        case GGV_RELOAD_COUNT60:h.state.hand.count60=v.i;break;
        case GGV_RELOAD_PAUSETIME60:h.state.hand.pausetime60=v.i;break;
        default:FAIL("unexpected autoswitch write");
        }
    }
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value,ggv_value) {
        auto &h=*static_cast<AutoSwitchHost *>(p);ggv_value v{};
        switch(service) {
        case GGV_HAND_CURRENT_FUNCTION:h.event(NHLO_AUTO_FUNCTION);v.i=h.state.gunfunc;break;
        case GGV_HAND_SET_STATE:
            h.event(NHLO_AUTO_SETSTATE,a.i);REQUIRE(a.i>=0);REQUIRE(a.i<32);
            v.b=(h.state.accept_states&(1u<<a.i))!=0;
            if(v.b){h.state.hand.state=a.i;h.state.hand.stateminor=0;h.state.hand.stateframes=0;}break;
        case GGV_HAND_READY_SWITCH:h.event(NHLO_AUTO_READY);v.b=h.state.ready;break;
        case GGV_RELOAD_SET_ARM_PITCH:h.event(NHLO_AUTO_PITCH,0,a.f);break;
        case GGV_HAND_DELETE_WEAPON:h.event(NHLO_AUTO_DELETE);break;
        case GGV_HAND_FREE_ROCKET:h.event(NHLO_AUTO_FREE);break;
        case GGV_RELOAD_AMMO_STATUS:h.event(NHLO_AUTO_AMMO,a.i);v.i=h.state.ammo;break;
        case GGV_AUTO_CAPTURE_OTHER:
            h.other=&h.state.other[h.state.current];h.event(NHLO_AUTO_CAPTURE,1-h.state.handnum);
            if(h.state.flip_capture)h.state.current=1;break;
        case GGV_AUTO_OTHER_AMMO_STATUS:
            REQUIRE(h.other);REQUIRE(a.i>=0);REQUIRE(a.i<2);
            h.event(NHLO_AUTO_OTHER_AMMO,a.i);v.i=h.other->ammo[a.i];break;
        case GGV_AUTO_OTHER_ELIGIBILITY:
            REQUIRE(h.other);h.event(NHLO_AUTO_EXCLUDED,a.i);v.b=h.other->excluded;break;
        case GGV_AUTO_SWITCH_WEAPON:
            h.event(NHLO_AUTO_SWITCH);if(h.state.flip_switch)h.state.current=1;break;
        default:FAIL("unexpected autoswitch service");
        }return v;
    }
};
static void autoSwitchParity(const nhlo_auto_case &input) {
    AutoSwitchHost h;h.state=input;auto native=input;auto expected=nhloAutoSwitch(&native);
    ggv_host host{&h,AutoSwitchHost::read,AutoSwitchHost::write,AutoSwitchHost::call};int32_t result=-99;
    REQUIRE(ggvExecute(ggvStockProgram(GGV_AUTO_SWITCH),&host,&result));REQUIRE(result==expected);
    const auto &a=h.state.hand,&b=native.hand;
    REQUIRE(a.gset.weaponfunc==b.gset.weaponfunc);REQUIRE(a.inuse==b.inuse);REQUIRE(a.lastdirvalid==b.lastdirvalid);
    REQUIRE(a.state==b.state);REQUIRE(a.stateminor==b.stateminor);REQUIRE(a.stateframes==b.stateframes);
    REQUIRE(a.animframeinc==b.animframeinc);REQUIRE(a.animframeincfreal==b.animframeincfreal);
    REQUIRE(a.count==b.count);REQUIRE(a.count60==b.count60);REQUIRE(a.pausetime60==b.pausetime60);
    REQUIRE(a.mode==b.mode);REQUIRE(a.modenext==b.modenext);
    REQUIRE(nhloFloatBits(a.shotremainder)==nhloFloatBits(b.shotremainder));
    REQUIRE(h.state.current==native.current);REQUIRE(h.state.count==native.count);
    for(unsigned i=0;i<native.count;++i) {
        REQUIRE(h.state.events[i].kind==native.events[i].kind);REQUIRE(h.state.events[i].arg==native.events[i].arg);
        REQUIRE(h.state.events[i].player==native.events[i].player);
        REQUIRE(nhloFloatBits(h.state.events[i].pitch)==nhloFloatBits(native.events[i].pitch));
    }
}
static nhlo_auto_case autoSwitchBaseline() {
    nhlo_auto_case s{};s.hand.inuse=1;s.hand.lastdirvalid=1;s.hand.stateminor=1;s.hand.state=8;
    s.hand.animframeinc=-2;s.hand.animframeincfreal=123;s.hand.shotremainder=0.75f;
    s.hand.count=5;s.hand.count60=7;s.hand.mode=3;s.hand.modenext=9;s.hand.pausetime60=11;
    s.lvupdate=4;s.gunfunc=1;s.ammo=-1;s.weaponnum=26;
    s.other[0]={1,0,1,{-1,-1},0};s.other[1]={1,1,0,{2,3},1};return s;
}
TEST_CASE("autoswitch preserves regional fallthrough refused states and mine function stores","[gameplay][graph][hand][parity]") {
    for(int modern:{0,1})for(int pal:{0,1})for(int minor:{0,1,2})for(int frames:{9,10,12,13,15,16})
    for(int normal:{0,1})for(int ready:{0,1})for(int inuse:{-1,0,1})for(unsigned accepts:{0u,0xffu}) {
        if(!modern&&pal)continue;
        auto s=autoSwitchBaseline();s.modern=modern;s.pal=pal;s.hand.stateminor=minor;s.hand.stateframes=frames;
        s.normal_multi=normal;s.multi=normal;s.ready=ready;s.hand.inuse=inuse;s.accept_states=accepts;autoSwitchParity(s);
    }
    for(int weapon:{26,32,33,34,5})for(int function:{0,1})for(int ammo:{-1,0,1,2})
    for(unsigned accepts:{0u,2u,128u,0xffu})for(uint32_t mode:{0u,9u,UINT32_MAX}) {
        auto s=autoSwitchBaseline();s.weaponnum=weapon;s.gunfunc=function;s.ammo=ammo;
        s.accept_states=accepts;s.hand.modenext=mode;autoSwitchParity(s);
    }
}
TEST_CASE("autoswitch keeps captured other-hand queries separate from fresh reload lookup","[gameplay][graph][hand][parity]") {
    for(int inuse:{0,1})for(int otherState:{0,1,8,4})for(int primary:{-1,0})for(int secondary:{-1,0})
    for(int excluded:{0,1})for(int flipCapture:{0,1})for(int flipSwitch:{0,1})for(int ammo:{-1,0,1}) {
        auto s=autoSwitchBaseline();s.autoselect=1;s.ammo=ammo;s.accept_states=2;
        s.other[0]={inuse,otherState,1,{primary,secondary},excluded};
        s.flip_capture=flipCapture;s.flip_switch=flipSwitch;autoSwitchParity(s);
    }
}
struct IdleHost {
    nhlo_idle_case state{};
    nhlo_auto_hand *other=nullptr;
    void event(int kind,int arg=0) {
        REQUIRE(state.count<64);state.events[state.count++]={kind,arg,state.current,0};
    }
    static ggv_value read(void *p,ggv_binding b) {
        auto &h=*static_cast<IdleHost *>(p);ggv_value v{};
        switch(b) {
        case GGV_STATE_INUSE:v.i=h.state.hand.inuse;break;
        case GGV_HAND_NUMBER:v.i=h.state.handnum;break;
        case GGV_WEAPON_FUNCTION:v.i=h.state.hand.gset.weaponfunc;break;
        case GGV_WEAPON_NUMBER:v.i=h.state.weaponnum;break;
        case GGV_EMPTY_LVUPDATE:v.i=h.state.lvupdate;break;
        case GGV_ANIM_WAIT_INCREMENT:v.i=h.state.hand.animframeinc;break;
        case GGV_HAND_MODE_NEXT:v.u=h.state.hand.modenext;break;
        case GGV_HAND_AUTO_BIT07:v.b=h.state.hand.unk0cc8_07;break;
        case GGV_HAND_AUTO_BIT08:v.b=h.state.hand.unk0cc8_08;break;
        case GGV_HAND_ACTIVATE_SECONDARY:v.b=h.state.hand.activatesecondary;break;
        case GGV_GUNCTRL_WANTAMMO:v.b=h.state.wantammo;break;
        case GGV_TRIGGER:v.b=h.state.hand.triggeron;break;
        case GGV_IDLE_FUNCTION_TYPE:v.u=h.state.function_type;break;
        case GGV_AUTO_OTHER_INUSE:REQUIRE(h.other);v.i=h.other->inuse;break;
        case GGV_AUTO_OTHER_TRIGGER:REQUIRE(h.other);v.b=h.other->triggeron;break;
        case GGV_AUTO_OTHER_BIT08:REQUIRE(h.other);v.b=h.other->unk0cc8_08;break;
        case GGV_RECOVERY_PAL:v.b=h.state.modern;break;
        default:FAIL("unexpected idle read");
        }return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v) {
        auto &h=*static_cast<IdleHost *>(p);
        switch(b) {
        case GGV_HAND_LASTDIRVALID:h.state.hand.lastdirvalid=v.b;break;
        case GGV_BURST_BULLETS:h.state.hand.burstbullets=v.i;break;
        case GGV_ANIM_TICK_INCREMENT_LEGACY:h.state.hand.animframeincfreal=v.u;break;
        case GGV_REMAINDER:h.state.hand.shotremainder=v.f;break;
        case GGV_WEAPON_FUNCTION:h.state.hand.gset.weaponfunc=v.i;break;
        case GGV_RELOAD_MODE:h.state.hand.mode=v.u;break;
        case GGV_HAND_MODE_NEXT:h.state.hand.modenext=v.u;break;
        case GGV_RELOAD_COUNT:h.state.hand.count=v.i;break;
        case GGV_RELOAD_COUNT60:h.state.hand.count60=v.i;break;
        case GGV_RELOAD_DOAUTOSELECT:h.state.autoselect[h.state.current]=v.i;break;
        case GGV_HAND_AUTO_BIT07:h.state.hand.unk0cc8_07=v.b;break;
        case GGV_HAND_AUTO_BIT08:h.state.hand.unk0cc8_08=v.b;break;
        case GGV_HAND_ACTIVATE_SECONDARY:h.state.hand.activatesecondary=v.b;break;
        case GGV_TRIGGER_RELEASED:h.state.hand.triggerreleased=v.b;break;
        case GGV_ANIMATION_LATCH:h.state.hand.unk0cc8_01=v.b;break;
        case GGV_AUTO_OTHER_BIT08:REQUIRE(h.other);h.other->unk0cc8_08=v.b;break;
        default:FAIL("unexpected idle write");
        }
    }
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value b,ggv_value) {
        auto &h=*static_cast<IdleHost *>(p);ggv_value v{};
        switch(service) {
        case GGV_HAND_CURRENT_FUNCTION:h.event(NHLO_IDLE_FUNCTION);v.i=h.state.gunfunc;break;
        case GGV_HAND_READY_SWITCH:h.event(NHLO_IDLE_READY);v.b=h.state.ready;break;
        case GGV_HAND_SET_STATE:
            REQUIRE(a.i>=0);REQUIRE(a.i<32);h.event(NHLO_IDLE_SETSTATE,a.i);
            v.b=(h.state.accept_states&(1u<<a.i))!=0;
            if(v.b){h.state.hand.state=a.i;h.state.hand.stateminor=0;h.state.hand.stateframes=0;}break;
        case GGV_RELOAD_AMMO_STATUS:
            REQUIRE(a.i>=0);REQUIRE(a.i<2);h.event(NHLO_IDLE_AMMO,a.i);v.i=h.state.ammo[a.i];break;
        case GGV_TICK_IS_V2:h.event(NHLO_IDLE_IS_V2);v.b=h.state.is_v2;break;
        case GGV_IDLE_V2:h.event(NHLO_IDLE_V2);if(h.state.flip_v2)h.state.current=1;v.b=h.state.v2_idle;break;
        case GGV_RELOAD_WEAPON_HAS_FLAG:
            REQUIRE(a.i==h.state.weaponnum);h.event(NHLO_IDLE_FLAG,int(b.u));v.b=(h.state.flags&b.u)!=0;break;
        case GGV_IDLE_STORED_FUNCTION:h.event(NHLO_IDLE_STORED);v.i=h.state.stored;break;
        case GGV_IDLE_ELIGIBILITY:h.event(NHLO_IDLE_EXCLUDED,a.i);v.b=h.state.excluded;break;
        case GGV_IDLE_CAPTURE_FUNCTION:h.event(NHLO_IDLE_CAPTURE_FUNCTION,a.i);break;
        case GGV_IDLE_CAPTURE_OTHER:h.other=&h.state.other[h.state.current];break;
        case GGV_AUTO_SWITCH_WEAPON:h.event(NHLO_IDLE_SWITCH);if(h.state.flip_switch)h.state.current=1;break;
        default:FAIL("unexpected idle service");
        }return v;
    }
};
static void idleParity(const nhlo_idle_case &input) {
    IdleHost h;h.state=input;auto native=input;auto expected=nhloIdle(&native);
    ggv_host host{&h,IdleHost::read,IdleHost::write,IdleHost::call};int32_t result=-99;
    REQUIRE(ggvExecute(ggvStockProgram(GGV_IDLE),&host,&result));REQUIRE(result==expected);
    auto sameHand=[](const nhlo_auto_hand &a,const nhlo_auto_hand &b) {
        REQUIRE(a.gset.weaponfunc==b.gset.weaponfunc);REQUIRE(a.inuse==b.inuse);
        REQUIRE(a.lastdirvalid==b.lastdirvalid);REQUIRE(a.burstbullets==b.burstbullets);
        REQUIRE(a.state==b.state);REQUIRE(a.stateminor==b.stateminor);REQUIRE(a.stateframes==b.stateframes);
        REQUIRE(a.count==b.count);REQUIRE(a.count60==b.count60);REQUIRE(a.pausetime60==b.pausetime60);
        REQUIRE(a.mode==b.mode);REQUIRE(a.modenext==b.modenext);
        REQUIRE(a.animframeinc==b.animframeinc);REQUIRE(a.animframeincfreal==b.animframeincfreal);
        REQUIRE(a.triggeron==b.triggeron);REQUIRE(a.triggerreleased==b.triggerreleased);
        REQUIRE(a.activatesecondary==b.activatesecondary);REQUIRE(a.unk0cc8_01==b.unk0cc8_01);
        REQUIRE(a.unk0cc8_07==b.unk0cc8_07);REQUIRE(a.unk0cc8_08==b.unk0cc8_08);
        REQUIRE(nhloFloatBits(a.shotremainder)==nhloFloatBits(b.shotremainder));
    };
    sameHand(h.state.hand,native.hand);for(unsigned i=0;i<2;++i){sameHand(h.state.other[i],native.other[i]);REQUIRE(h.state.autoselect[i]==native.autoselect[i]);}
    REQUIRE(h.state.current==native.current);REQUIRE(h.state.count==native.count);
    for(unsigned i=0;i<native.count;++i) {
        REQUIRE(h.state.events[i].kind==native.events[i].kind);REQUIRE(h.state.events[i].arg==native.events[i].arg);
        REQUIRE(h.state.events[i].player==native.events[i].player);
    }
}
static nhlo_idle_case idleBaseline() {
    nhlo_idle_case s{};s.hand=autoSwitchBaseline().hand;s.hand.stateminor=0;s.hand.state=0;
    s.hand.burstbullets=5;s.hand.unk0cc8_01=1;s.hand.unk0cc8_07=1;s.hand.unk0cc8_08=1;
    s.hand.triggerreleased=1;s.hand.modenext=0;s.other[0].inuse=s.other[1].inuse=1;
    s.other[0].unk0cc8_08=s.other[1].unk0cc8_08=1;s.other[0].triggeron=1;
    s.autoselect[0]=s.autoselect[1]=1;s.lvupdate=4;s.weaponnum=26;s.ammo[0]=s.ammo[1]=1;
    return s;
}
TEST_CASE("idle preserves function changes refusal paths and regional stores","[gameplay][graph][hand][parity]") {
    for(int modern:{0,1})for(int gunfunc:{0,1})for(int inuse:{-1,0,1})for(int ready:{0,1})
    for(int ammo:{-1,0,1,2})for(int alternate:{-1,0,1})for(unsigned accepts:{0u,2u,128u,0x1ffu}) {
        auto s=idleBaseline();s.modern=modern;s.gunfunc=gunfunc;s.hand.inuse=inuse;s.ready=ready;
        s.ammo[0]=ammo;s.ammo[1]=alternate;s.accept_states=accepts;idleParity(s);
    }
}
TEST_CASE("idle V2 paths preserve reload policy and return before legacy tail","[gameplay][graph][hand][parity]") {
    for(int ready:{0,1})for(int trigger:{0,1})for(int ammo:{-1,0,1,2})for(unsigned mode:{0u,9u,UINT32_MAX})
    for(unsigned accepts:{0u,2u})for(int hand:{0,1}) {
        auto s=idleBaseline();s.is_v2=1;s.v2_idle=ready;s.flip_v2=1;s.wantammo=1;
        s.hand.triggeron=trigger;s.ammo[0]=ammo;s.hand.modenext=mode;s.accept_states=accepts;s.handnum=hand;idleParity(s);
    }
}
TEST_CASE("idle legacy alternatives and deferred commands preserve native decisions","[gameplay][graph][hand][parity]") {
    for(int weapon:{0,20,26,34,5})for(int hand:{0,1})for(int ammo:{-1,0,1,2})
    for(int trigger:{0,1})for(unsigned flags:{0u,1u,0x04000000u})for(unsigned accepts:{0u,0x1ffu}) {
        auto s=idleBaseline();s.weaponnum=weapon;s.handnum=hand;s.ammo[0]=ammo;
        s.hand.triggeron=trigger;s.flags=flags;s.accept_states=accepts;s.hand.modenext=9;idleParity(s);
    }
    for(int want:{0,1})for(int excluded:{0,1})for(uint32_t type:{3u,0x103u,4u})for(int altAmmo:{-1,0,1})
    for(int stored:{0,1})for(unsigned accepts:{0u,0x1ffu}) {
        auto s=idleBaseline();s.ammo[0]=-1;s.ammo[1]=altAmmo;s.wantammo=want;s.excluded=excluded;
        s.function_type=type;s.stored=stored;s.accept_states=accepts;idleParity(s);
    }
}
TEST_CASE("idle right-hand tail clears captured other flags after autoswitch callback","[gameplay][graph][hand][parity]") {
    for(int hand:{0,1})for(int want:{0,1})for(int inuse:{0,1})for(int otherInuse:{0,1})
    for(int otherFlag:{0,1})for(int trigger:{0,1})for(int otherTrigger:{0,1}) {
        auto s=idleBaseline();s.handnum=hand;s.wantammo=want;s.hand.inuse=inuse;s.ammo[0]=s.ammo[1]=-1;
        s.other[0].inuse=otherInuse;s.other[0].unk0cc8_08=otherFlag;
        s.hand.triggeron=trigger;s.other[0].triggeron=otherTrigger;s.flip_switch=1;idleParity(s);
    }
}
TEST_CASE("idle secondary activation without trigger preserves accepted and refused attack paths","[gameplay][graph][hand][parity]") {
    for(int function:{0,1})for(int weapon:{0,5})for(unsigned accepts:{0u,16u})for(unsigned mode:{0u,9u}) {
        auto s=idleBaseline();s.gunfunc=function;s.hand.gset.weaponfunc=function;
        s.hand.triggeron=0;s.hand.activatesecondary=1;s.weaponnum=weapon;
        s.accept_states=accepts;s.hand.modenext=mode;idleParity(s);
    }
}
struct ReadySwitchHost {
    nhlo_ready_case state{};
    static ggv_value read(void *p,ggv_binding b) {
        auto &s=static_cast<ReadySwitchHost *>(p)->state;ggv_value v{};
        switch(b) {
        case GGV_HAND_NUMBER:v.i=s.handnum;break;
        case GGV_SWITCH_RANGE_VALID:v.b=s.range_valid;break;
        case GGV_SWITCH_RIGHT_WEAPON:v.i=s.right_weapon;break;
        case GGV_SWITCH_CURRENT_THROWING:v.i=s.throwing;break;
        case GGV_SWITCH_LEFT_INUSE:v.i=s.left_inuse;break;
        case GGV_SWITCH_LEFT_STATE:v.i=s.left_state;break;
        case GGV_SWITCH_LEFT_MINOR:v.i=s.left_minor;break;
        case GGV_SWITCH_PENDING_WEAPON:v.i=s.pending_weapon;break;
        case GGV_SWITCH_RIGHT_STATE:v.i=s.right_state;break;
        case GGV_STATE_INUSE:v.i=s.inuse;break;
        case GGV_SWITCH_DUALWIELD:v.b=s.dualwield;break;
        default:FAIL("unexpected switch readiness read");
        }return v;
    }
    static void write(void *,ggv_binding,ggv_value) {FAIL("switch readiness must not write");}
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value,ggv_value) {
        auto &s=static_cast<ReadySwitchHost *>(p)->state;ggv_value v{};
        if(service==GGV_RANGE_SLOT){++s.slot_calls;v.i=17;}
        else if(service==GGV_RANGE_WEAPON){REQUIRE(s.slot_calls==1);REQUIRE(a.i==17);++s.weapon_calls;if(s.flip_throwing)s.throwing=1;v.i=s.range_weapon;}
        else FAIL("unexpected switch readiness service");return v;
    }
};
TEST_CASE("switch readiness preserves range gates and dual-hand constraints","[gameplay][graph][hand][parity]") {
    for(int hand:{0,1})for(int range:{0,1})for(int match:{0,1})for(int throwing:{-1,0,1,2})
    for(int pending:{-1,0})for(int inuse:{-1,0,1})for(int dual:{0,1,2})for(int rightState:{0,1,4,7,8}) {
        ReadySwitchHost h;h.state.handnum=hand;h.state.range_valid=range;h.state.range_weapon=match?5:6;
        h.state.right_weapon=5;h.state.throwing=throwing;h.state.pending_weapon=pending;
        h.state.inuse=inuse;h.state.dualwield=dual;h.state.right_state=rightState;
        auto native=h.state;int expected=nhloReadyToSwitch(&native);
        ggv_host host{&h,ReadySwitchHost::read,ReadySwitchHost::write,ReadySwitchHost::call};int32_t actual=-99;
        REQUIRE(ggvExecute(ggvStockProgram(GGV_READY_TO_SWITCH),&host,&actual));REQUIRE(actual==expected);
        REQUIRE(h.state.slot_calls==native.slot_calls);REQUIRE(h.state.weapon_calls==native.weapon_calls);
    }
    for(int hand:{0,1})for(int leftInuse:{0,1,-1})for(int leftState:{0,8})for(int minor:{0,1})for(int flip:{0,1}) {
        ReadySwitchHost h;h.state.handnum=hand;h.state.range_valid=1;h.state.flip_throwing=flip;
        h.state.left_inuse=leftInuse;h.state.left_state=leftState;h.state.left_minor=minor;h.state.pending_weapon=4;
        auto native=h.state;int expected=nhloReadyToSwitch(&native);
        ggv_host host{&h,ReadySwitchHost::read,ReadySwitchHost::write,ReadySwitchHost::call};int32_t actual=-99;
        REQUIRE(ggvExecute(ggvStockProgram(GGV_READY_TO_SWITCH),&host,&actual));REQUIRE(actual==expected);
    }
}
struct FunctionPredicateHost {
    int stored=0,invert=0,selected=0,requested=0,excluded=0,ammo[2]{},current=0;
    int function=0,weapon=0;
    std::vector<int> calls,players;
    static ggv_value read(void *p,ggv_binding b) {
        auto &h=*static_cast<FunctionPredicateHost *>(p);ggv_value v{};
        if(b==GGV_FUNCTION_CONTROL_WEAPON)v.i=17;
        else if(b==GGV_FUNCTION_CONTROL_INVERT)v.i=h.invert;
        else if(b==GGV_QUERY_FUNCTION_I32)v.i=h.requested;
        else FAIL("unexpected function predicate read");return v;
    }
    static void write(void *,ggv_binding,ggv_value) {FAIL("function predicates must not write");}
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value b,ggv_value) {
        auto &h=*static_cast<FunctionPredicateHost *>(p);h.calls.push_back(service);h.players.push_back(h.current);ggv_value v{};
        switch(service) {
        case GGV_STORED_FUNCTION:REQUIRE(a.i==17);h.current=1;v.b=h.stored;break;
        case GGV_EXCLUSION_FUNCTION:v.ref=&h.function;break;
        case GGV_EXCLUSION_WEAPON:v.ref=&h.weapon;break;
        case GGV_HAND_CURRENT_FUNCTION:h.current=1;v.i=h.selected;break;
        case GGV_EXCLUSION_CHECK:REQUIRE(a.ref==&h.function);REQUIRE(b.ref==&h.weapon);v.b=h.excluded;break;
        case GGV_HAND_INFO_CAPTURE:break;
        case GGV_RELOAD_AMMO_STATUS:REQUIRE(a.i>=0);REQUIRE(a.i<2);v.i=h.ammo[a.i];h.current=1;break;
        default:FAIL("unexpected function predicate service");
        }return v;
    }
    int run(ggv_stock_program program) {
        ggv_host host{this,read,write,call};int32_t result=-99;REQUIRE(ggvExecute(ggvStockProgram(program),&host,&result));return result;
    }
};
TEST_CASE("function predicates preserve raw equality captured references and lazy ammo queries","[gameplay][graph][hand][parity]") {
    for(int stored:{0,1})for(int invert:{-1,0,1,2,256}) {
        FunctionPredicateHost h;h.stored=stored;h.invert=invert;
        REQUIRE(h.run(GGV_USING_SECONDARY)==nhloUsingSecondary(stored,invert));
        REQUIRE(h.calls==std::vector<int>{GGV_STORED_FUNCTION});
    }
    for(int requested:{0,1,2})for(int selected:{0,1})for(int excluded:{0,1}) {
        FunctionPredicateHost h;h.requested=requested;h.selected=selected;h.excluded=excluded;
        REQUIRE(h.run(GGV_HAND_EXCLUSION)==(requested==selected?0:excluded));
        std::vector<int> expected{GGV_EXCLUSION_FUNCTION,GGV_EXCLUSION_WEAPON,GGV_HAND_CURRENT_FUNCTION};
        if(requested!=selected)expected.push_back(GGV_EXCLUSION_CHECK);REQUIRE(h.calls==expected);
    }
    for(int primary:{-1,0,1,2})for(int secondary:{-1,0,1,2}) {
        FunctionPredicateHost h;h.ammo[0]=primary;h.ammo[1]=secondary;
        REQUIRE(h.run(GGV_ANY_AMMO)==(primary>0||secondary>0));
        std::vector<int> expected{GGV_HAND_INFO_CAPTURE,GGV_RELOAD_AMMO_STATUS};
        if(primary<=0)expected.push_back(GGV_RELOAD_AMMO_STATUS);REQUIRE(h.calls==expected);
        if(primary<=0)REQUIRE(h.players.back()==1);
    }
}
struct WeaponInfoHost {
    int inputWeapon=17,outputWeapon=-1,current=0,classifications=0;
    int v2[2]{},flipEnsure=0,flipFind=0,definitions[3]{},gunctrl[2]{};
    void *definition=nullptr,*control=nullptr;
    std::vector<int> events,players;
    static ggv_value read(void *p,ggv_binding b) {
        auto &h=*static_cast<WeaponInfoHost *>(p);REQUIRE(b==GGV_INFO_GUNCTRL);ggv_value v{};
        v.ref=&h.gunctrl[h.current];return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v) {
        auto &h=*static_cast<WeaponInfoHost *>(p);
        if(b==GGV_INFO_WEAPON_RESULT)h.outputWeapon=v.i;
        else if(b==GGV_INFO_DEFINITION_RESULT)h.definition=v.ref;
        else if(b==GGV_INFO_GUNCTRL)h.control=v.ref;
        else FAIL("unexpected info write");
    }
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value,ggv_value) {
        auto &h=*static_cast<WeaponInfoHost *>(p);h.events.push_back(service);h.players.push_back(h.current);ggv_value v{};
        switch(service) {
        case GGV_INFO_WEAPON_QUERY:v.i=h.inputWeapon;break;
        case GGV_INFO_IS_V2:REQUIRE(a.i==17);REQUIRE(h.classifications<2);v.b=h.v2[h.classifications++];break;
        case GGV_INFO_ENSURE_V2:REQUIRE(h.outputWeapon==17);h.outputWeapon=99;if(h.flipEnsure)h.current=1;break;
        case GGV_INFO_V2_DEFINITION:v.ref=&h.definitions[h.current];break;
        case GGV_QUERY_FIND_WEAPON:REQUIRE(a.i==17);v.ref=&h.definitions[2];if(h.flipFind)h.current=1;break;
        default:FAIL("unexpected info service");
        }return v;
    }
};
TEST_CASE("weapon info retains captured number repeated classification and fresh player outputs","[gameplay][graph][hand][parity]") {
    for(int first:{0,1})for(int second:{0,1})for(int ensureFlip:{0,1})for(int findFlip:{0,1}) {
        WeaponInfoHost h;h.v2[0]=first;h.v2[1]=second;h.flipEnsure=ensureFlip;h.flipFind=findFlip;
        ggv_host host{&h,WeaponInfoHost::read,WeaponInfoHost::write,WeaponInfoHost::call};int32_t result=-99;
        REQUIRE(ggvExecute(ggvStockProgram(GGV_WEAPON_INFO),&host,&result));REQUIRE(result==0);
        std::vector<int> events{GGV_INFO_WEAPON_QUERY,GGV_INFO_IS_V2};
        if(first)events.push_back(GGV_INFO_ENSURE_V2);events.push_back(GGV_INFO_IS_V2);
        events.push_back(second?GGV_INFO_V2_DEFINITION:GGV_QUERY_FIND_WEAPON);REQUIRE(h.events==events);
        REQUIRE(h.outputWeapon==(first?99:17));
        int definitionPlayer=first&&ensureFlip?1:0;
        REQUIRE(h.definition==&h.definitions[second?definitionPlayer:2]);
        int finalPlayer=definitionPlayer||(!second&&findFlip)?1:0;
        REQUIRE(h.control==&h.gunctrl[finalPlayer]);REQUIRE(h.classifications==2);
    }
}
struct SwitchCompletionHost {
    int loaded=0,hand=0,pending=-1,dual=0,inuse=0,memory=-1,other=0;
    std::vector<int> reads;
    static ggv_value read(void *p,ggv_binding b) {
        auto &h=*static_cast<SwitchCompletionHost *>(p);h.reads.push_back(b);ggv_value v{};
        switch(b) {
        case GGV_HAND_NUMBER:v.i=h.hand;break;
        case GGV_SWITCH_PENDING_WEAPON:v.i=h.pending;break;
        case GGV_SWITCH_DUALWIELD_RAW:v.i=h.dual;break;
        case GGV_STATE_INUSE:v.i=h.inuse;break;
        case GGV_LOADED_GUNMEMNEW:v.i=h.memory;break;
        case GGV_RELOAD_OTHER_STATE:v.i=h.other;break;
        default:FAIL("unexpected switch completion read");
        }return v;
    }
    static void write(void *,ggv_binding,ggv_value) {FAIL("switch completion must not write");}
    static ggv_value call(void *p,ggv_service service,ggv_value,ggv_value,ggv_value) {
        REQUIRE(service==GGV_RELOAD_IS_LOADED);ggv_value v{};v.b=static_cast<SwitchCompletionHost *>(p)->loaded;return v;
    }
};
TEST_CASE("switch completion preserves non-short-circuit checks and raw dual equality","[gameplay][graph][hand][parity]") {
    for(int loaded:{0,1})for(int hand:{0,1})for(int pending:{-2,-1,0})for(int dual:{0,1,2})
    for(int inuse:{-1,0,1,2})for(int memory:{-1,0})for(int other:{0,1,8}) {
        SwitchCompletionHost h;h.loaded=loaded;h.hand=hand;h.pending=pending;h.dual=dual;
        h.inuse=inuse;h.memory=memory;h.other=other;
        ggv_host host{&h,SwitchCompletionHost::read,SwitchCompletionHost::write,SwitchCompletionHost::call};int32_t result=-99;
        REQUIRE(ggvExecute(ggvStockProgram(GGV_SWITCH_COMPLETE),&host,&result));
        REQUIRE(result==(loaded&&pending==-1&&(hand!=1||dual==inuse)&&memory<0&&other!=1));
        std::vector<int> expected{GGV_SWITCH_PENDING_WEAPON,GGV_HAND_NUMBER};
        if(hand==1){expected.push_back(GGV_SWITCH_DUALWIELD_RAW);expected.push_back(GGV_STATE_INUSE);}
        expected.push_back(GGV_LOADED_GUNMEMNEW);expected.push_back(GGV_RELOAD_OTHER_STATE);REQUIRE(h.reads==expected);
    }
}
struct CanFreeHost {
    int state=5,minor=2,throwing=0,player=0,hand=0,last[2]{-1,-1};
    std::vector<int> logs;
    static ggv_value read(void *p,ggv_binding b) {
        auto &h=*static_cast<CanFreeHost *>(p);ggv_value v{};
        switch(b) {
        case GGV_STATE_VALUE:v.i=h.state;break;
        case GGV_STATE_MINOR:v.i=h.minor;break;
        case GGV_FREE_THROWING:v.i=h.throwing;break;
        case GGV_STATE_PLAYER_NUMBER:v.i=h.player;break;
        case GGV_HAND_NUMBER:v.i=h.hand;break;
        case GGV_FREE_LAST_RIGHT:v.i=h.last[0];break;
        case GGV_FREE_LAST_LEFT:v.i=h.last[1];break;
        default:FAIL("unexpected can-free read");
        }return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v) {
        auto &h=*static_cast<CanFreeHost *>(p);REQUIRE((b==GGV_FREE_LAST_RIGHT||b==GGV_FREE_LAST_LEFT));
        h.last[b==GGV_FREE_LAST_RIGHT?0:1]=v.i;
    }
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value,ggv_value) {
        auto &h=*static_cast<CanFreeHost *>(p);REQUIRE(service==GGV_FREE_LOG);
        REQUIRE(h.player==0);REQUIRE(h.last[h.hand]==a.i);h.logs.push_back(a.i);return {};
    }
    int run(){ggv_host host{this,read,write,call};int32_t result=-99;REQUIRE(ggvExecute(ggvStockProgram(GGV_CAN_FREE_WEAPON),&host,&result));return result;}
};
TEST_CASE("can-free transition logging remains per-hand and stores before emitting","[gameplay][graph][hand][parity]") {
    CanFreeHost h;
    for(int player:{1,0})for(int hand:{0,1})for(int state:{0,5})for(int minor:{0,2})for(int throwing:{-1,0,1,2}) {
        h.player=player;h.hand=hand;h.state=state;h.minor=minor;h.throwing=throwing;
        int expected=state==5&&minor==2&&throwing==0;auto count=h.logs.size();int previous=h.last[hand];
        REQUIRE(h.run()==expected);REQUIRE(h.logs.size()==count+(player==0&&previous!=expected));
        REQUIRE(h.run()==expected);REQUIRE(h.logs.size()==count+(player==0&&previous!=expected));
    }
}
struct HeldRocketHost {
    int object=0,replacement=0;
    void *hands[2]{};
    int current=0,captured=0;
    std::vector<int> events;
    static ggv_value read(void *p,ggv_binding b) {
        auto &h=*static_cast<HeldRocketHost *>(p);REQUIRE(b==GGV_HELD_ROCKET);
        h.events.push_back(1);ggv_value v{};v.ref=h.hands[h.captured];return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v) {
        auto &h=*static_cast<HeldRocketHost *>(p);REQUIRE(b==GGV_HELD_ROCKET);
        REQUIRE(v.ref==nullptr);h.events.push_back(3);h.hands[h.captured]=v.ref;
    }
    static ggv_value call(void *p,ggv_service s,ggv_value a,ggv_value,ggv_value) {
        auto &h=*static_cast<HeldRocketHost *>(p);REQUIRE(s==GGV_FREE_ROCKET_OBJECT);
        REQUIRE(a.ref==&h.object);REQUIRE(h.hands[h.captured]==a.ref);
        h.events.push_back(2);h.current=1;h.hands[0]=&h.replacement;return {};
    }
    void run() {
        captured=current;ggv_host host{this,read,write,call};int32_t result=-99;
        REQUIRE(ggvExecute(ggvStockProgram(GGV_FREE_HELD_ROCKET),&host,&result));REQUIRE(result==0);
    }
};
TEST_CASE("held rocket release clears the captured hand after the free effect","[gameplay][graph][hand][parity]") {
    HeldRocketHost h;h.hands[1]=&h.replacement;h.run();
    REQUIRE(h.events==std::vector<int>{1});REQUIRE(h.hands[1]==&h.replacement);
    h.events.clear();h.hands[0]=&h.object;h.run();
    REQUIRE(h.events==std::vector<int>{1,1,2,3});REQUIRE(h.current==1);
    REQUIRE(h.hands[0]==nullptr);REQUIRE(h.hands[1]==&h.replacement);
}
struct StoredFunctionHost {
    int weapon=0;
    uint32_t byteindex=99,custom=0;
    uint8_t native[5]{};
    int nativeReads=0,customReads=0;
    static ggv_value read(void *p,ggv_binding b) {
        auto &h=*static_cast<StoredFunctionHost *>(p);ggv_value v{};
        switch(b) {
        case GGV_QUERY_WEAPON_I32:v.i=h.weapon;break;
        case GGV_FUNCTION_NATIVE_FIRST:v.i=1;break;
        case GGV_FUNCTION_NATIVE_LAST:v.i=35;break;
        case GGV_FUNCTION_CUSTOM_FIRST:v.i=86;break;
        case GGV_FUNCTION_CUSTOM_END:v.i=109;break;
        case GGV_FUNCTION_NATIVE_BITS:REQUIRE(h.byteindex<5);++h.nativeReads;v.u=h.native[h.byteindex];break;
        case GGV_FUNCTION_CUSTOM_BITS:++h.customReads;v.u=h.custom;break;
        default:FAIL("Unexpected stored function binding");
        }
        return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v) {
        auto &h=*static_cast<StoredFunctionHost *>(p);REQUIRE(b==GGV_FUNCTION_BYTE_INDEX);h.byteindex=v.u;
    }
    int run() {
        ggv_host host{this,read,write,nullptr};int32_t result=-99;
        REQUIRE(ggvExecute(ggvStockProgram(GGV_STORED_FUNCTION_SECONDARY),&host,&result));return result;
    }
};
TEST_CASE("stored function selection guards ranges before reading native or custom bits","[gameplay][graph][hand][parity]") {
    for(int weapon=-1;weapon<=110;++weapon)for(uint32_t bits:{0u,1u,0x55u,0xaau,0xffffffffu}) {
        StoredFunctionHost h;h.weapon=weapon;h.custom=bits;
        for(auto &byte:h.native)byte=uint8_t(bits);
        bool native=weapon>=1&&weapon<=35,custom=weapon>=86&&weapon<109;
        bool expected=false;
        if(native) {uint32_t index=uint32_t(weapon-1);expected=(h.native[index>>3]&(1u<<(index&7)))!=0;}
        if(custom)expected=(bits&(1u<<uint32_t(weapon-86)))!=0;
        REQUIRE(h.run()==expected);REQUIRE(h.nativeReads==int(native));REQUIRE(h.customReads==int(custom));
        REQUIRE(h.byteindex==(native?uint32_t(weapon-1)>>3:99u));
    }
    for(int weapon:{INT32_MIN,INT32_MAX}) {StoredFunctionHost h;h.weapon=weapon;REQUIRE(h.run()==0);REQUIRE(h.nativeReads==0);REQUIRE(h.customReads==0);}
}
struct StoreFunctionHost : StoredFunctionHost {
    bool secondary=false;
    int nativeCaptures=0,customCaptures=0,stores=0;
    static ggv_value read(void *p,ggv_binding b) {
        auto &h=*static_cast<StoreFunctionHost *>(p);
        if(b==GGV_FUNCTION_SECONDARY_INPUT) {ggv_value v{};v.b=h.secondary;return v;}
        if(b==GGV_FUNCTION_NATIVE_BITS)REQUIRE(h.nativeCaptures==1);
        if(b==GGV_FUNCTION_CUSTOM_BITS)REQUIRE(h.customCaptures==1);
        return StoredFunctionHost::read(static_cast<StoredFunctionHost *>(&h),b);
    }
    static void write(void *p,ggv_binding b,ggv_value v) {
        auto &h=*static_cast<StoreFunctionHost *>(p);
        if(b==GGV_FUNCTION_BYTE_INDEX) {h.byteindex=v.u;return;}
        ++h.stores;
        if(b==GGV_FUNCTION_NATIVE_BITS) {REQUIRE(h.nativeCaptures==1);REQUIRE(h.byteindex<5);h.native[h.byteindex]=uint8_t(v.u);}
        else {REQUIRE(b==GGV_FUNCTION_CUSTOM_BITS);REQUIRE(h.customCaptures==1);h.custom=v.u;}
    }
    static ggv_value call(void *p,ggv_service s,ggv_value,ggv_value,ggv_value) {
        auto &h=*static_cast<StoreFunctionHost *>(p);
        if(s==GGV_FUNCTION_CAPTURE_NATIVE)++h.nativeCaptures;
        else {REQUIRE(s==GGV_FUNCTION_CAPTURE_CUSTOM);++h.customCaptures;}
        return {};
    }
    void runStore() {
        ggv_host host{this,read,write,call};int32_t result=-99;
        REQUIRE(ggvExecute(ggvStockProgram(GGV_STORE_FUNCTION_SELECTION),&host,&result));REQUIRE(result==0);
    }
};
TEST_CASE("function selection stores preserve unrelated native and custom bits","[gameplay][graph][hand][parity]") {
    for(int weapon=-1;weapon<=110;++weapon)for(bool secondary:{false,true})
    for(uint32_t bits:{0u,1u,0x55u,0xaau,0xffffffffu}) {
        StoreFunctionHost h;h.weapon=weapon;h.secondary=secondary;h.custom=bits;
        uint8_t expectedNative[5];for(int i=0;i<5;++i)expectedNative[i]=h.native[i]=uint8_t(bits);
        uint32_t expectedCustom=bits;
        bool native=weapon>=1&&weapon<=35,custom=weapon>=86&&weapon<109;
        if(native) {
            uint32_t index=uint32_t(weapon-1);uint8_t mask=uint8_t(1u<<(index&7));
            if(secondary)expectedNative[index>>3]|=mask;else expectedNative[index>>3]&=uint8_t(~mask);
        } else if(custom) {
            uint32_t mask=1u<<uint32_t(weapon-86);
            if(secondary)expectedCustom|=mask;else expectedCustom&=~mask;
        }
        h.runStore();REQUIRE(h.custom==expectedCustom);
        for(int i=0;i<5;++i)REQUIRE(h.native[i]==expectedNative[i]);
        REQUIRE(h.nativeCaptures==int(native));REQUIRE(h.customCaptures==int(custom));
        REQUIRE(h.stores==int(native||custom));REQUIRE(h.nativeReads==int(native));REQUIRE(h.customReads==int(custom));
    }
    for(int weapon:{INT32_MIN,INT32_MAX}) {
        StoreFunctionHost h;h.weapon=weapon;h.runStore();
        REQUIRE(h.nativeCaptures==0);REQUIRE(h.customCaptures==0);REQUIRE(h.stores==0);
    }
}
TEST_CASE("function selection eligibility queries no backing storage","[gameplay][graph][hand][parity]") {
    auto check=[](int weapon) {
        StoredFunctionHost h;h.weapon=weapon;ggv_host host{&h,StoredFunctionHost::read,StoredFunctionHost::write,nullptr};
        int32_t result=-99;REQUIRE(ggvExecute(ggvStockProgram(GGV_CAN_STORE_FUNCTION),&host,&result));
        REQUIRE(result==int((weapon>=1&&weapon<=35)||(weapon>=86&&weapon<109)));
        REQUIRE(h.nativeReads==0);REQUIRE(h.customReads==0);REQUIRE(h.byteindex==99);
    };
    for(int weapon=-1;weapon<=110;++weapon)check(weapon);
    check(INT32_MIN);check(INT32_MAX);
}
TEST_CASE("unsigned graph bit operations guard shift counts before output stores","[gameplay][graph][types]") {
    for(ggv_op op:{GGV_U_SHL,GGV_U_AND,GGV_U_OR,GGV_U_NOT})
    for(uint32_t a:{0u,1u,0x80000000u,0xffffffffu})for(uint32_t b:{0u,1u,31u,32u,0xffffffffu}) {
        ggv_source source{};source.count=5;
        source.nodes[0].op=GGV_CONST;source.nodes[0].dst=0;source.nodes[0].type=GGV_U32;source.nodes[0].literal.u=a;
        source.nodes[1].op=GGV_CONST;source.nodes[1].dst=1;source.nodes[1].type=GGV_U32;source.nodes[1].literal.u=b;
        source.nodes[2].op=op;source.nodes[2].dst=2;source.nodes[2].a=0;source.nodes[2].b=1;
        source.nodes[3].op=GGV_WRITE;source.nodes[3].binding=GGV_FUNCTION_CUSTOM_BITS;source.nodes[3].a=2;
        source.nodes[4].op=GGV_RETURN;
        struct Output {uint32_t value=0x12345678u;int writes=0;} output;
        ggv_host host{&output,
            [](void *,ggv_binding)->ggv_value {FAIL("Constant bit program should not read a host");return {};},
            [](void *p,ggv_binding b,ggv_value v) {REQUIRE(b==GGV_FUNCTION_CUSTOM_BITS);auto &out=*static_cast<Output *>(p);out.value=v.u;++out.writes;},nullptr};
        ggv_program program{};REQUIRE(ggvPrepare(&source,&program,nullptr,0));int32_t result=-99;
        bool allowed=op!=GGV_U_SHL||b<32;
        REQUIRE(bool(ggvExecute(&program,&host,&result))==allowed);
        if(allowed) {
            uint32_t expected=op==GGV_U_SHL?a<<b:op==GGV_U_AND?a&b:op==GGV_U_OR?a|b:~a;
            REQUIRE(output.value==expected);REQUIRE(output.writes==1);REQUIRE(result==0);
        } else {REQUIRE(output.value==0x12345678u);REQUIRE(output.writes==0);REQUIRE(result==-99);}
        source.nodes[0].type=GGV_I32;REQUIRE_FALSE(ggvPrepare(&source,&program,nullptr,0));
    }
}
struct ResetFunctionHost {
    int weapon=0,current=0;
    bool flipOnQuery=false;
    struct Player {bool ext=false,remote=false,activate=false;int invert=7;} players[2];
    std::vector<int> events;
    static ggv_value read(void *p,ggv_binding b) {
        auto &h=*static_cast<ResetFunctionHost *>(p);auto &player=h.players[h.current];ggv_value v{};
        h.events.push_back(int(b));
        switch(b) {
        case GGV_FUNCTION_EXT_CONTROLS:v.b=player.ext;break;
        case GGV_FUNCTION_REMOTE_PLAYER:v.b=player.remote;break;
        case GGV_HAND_ACTIVATE_SECONDARY:v.b=player.activate;break;
        default:FAIL("Unexpected function reset binding");
        }
        return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v) {
        auto &h=*static_cast<ResetFunctionHost *>(p);REQUIRE(b==GGV_FUNCTION_CONTROL_INVERT);
        h.events.push_back(int(b));h.players[h.current].invert=v.i;
    }
    static ggv_value call(void *p,ggv_service s,ggv_value,ggv_value,ggv_value) {
        auto &h=*static_cast<ResetFunctionHost *>(p);REQUIRE(s==GGV_INFO_WEAPON_QUERY);
        h.events.push_back(-1);if(h.flipOnQuery)h.current=1;ggv_value v{};v.i=h.weapon;return v;
    }
};
TEST_CASE("function inversion reset preserves lazy control queries and current player","[gameplay][graph][hand][parity]") {
    for(int weapon:{-1,0,12,13,14,15,16,33,34,35,86})
    for(bool ext:{false,true})for(bool remote:{false,true})for(bool activate:{false,true})for(bool flip:{false,true}) {
        ResetFunctionHost h;h.weapon=weapon;h.flipOnQuery=flip;
        h.players[int(flip)]={ext,remote,activate,7};
        ggv_host host{&h,ResetFunctionHost::read,ResetFunctionHost::write,ResetFunctionHost::call};int32_t result=-99;
        REQUIRE(ggvExecute(ggvStockProgram(GGV_RESET_FUNCTION_INVERSION),&host,&result));REQUIRE(result==0);
        bool special=weapon==13||weapon==14||weapon==15||weapon==34;
        bool kept=special&&(ext||remote);
        REQUIRE(h.players[int(flip)].invert==((kept||activate)?7:0));
        if(flip)REQUIRE(h.players[0].invert==7);
        std::vector<int> expected{-1};
        if(special) {expected.push_back(GGV_FUNCTION_EXT_CONTROLS);if(!ext)expected.push_back(GGV_FUNCTION_REMOTE_PLAYER);}
        if(!kept) {expected.push_back(GGV_HAND_ACTIVATE_SECONDARY);if(!activate)expected.push_back(GGV_FUNCTION_CONTROL_INVERT);}
        REQUIRE(h.events==expected);
    }
}
struct ToggleFunctionHost {
    struct Player {
        bool ext=false,remote=false,activate=false,stored=false;
        int invert=0,function=0,controlWeapon=2;
        uint32_t devices=0;
    } players[2];
    int current=0,weapon=0,usedowntime=0,threshold=50;
    bool trigpressed=false,fromactivemenu=false,fromdedicatedbutton=false;
    bool flipOnWeapon=false,flipOnCanStore=false,flipOnStored=false;
    bool canStoreResponse=true;int usingResponse=0;
    std::vector<int> events;
    int queryWeapon() {events.push_back(1);if(flipOnWeapon)current=1;return weapon;}
    int currentFunction() {events.push_back(6);return players[current].function;}
    bool canStore(int w) {events.push_back(2);events.push_back(w);if(flipOnCanStore)current=1;return canStoreResponse;}
    bool storedQuery(int w) {events.push_back(3);events.push_back(w);bool result=players[current].stored;if(flipOnStored)current=1;return result;}
    void storeQuery(int w,bool secondary) {events.push_back(4);events.push_back(w);events.push_back(secondary);players[current].stored=secondary;}
    int usingQuery() {events.push_back(5);return usingResponse;}
    static ggv_value read(void *p,ggv_binding b) {
        auto &h=*static_cast<ToggleFunctionHost *>(p);auto &player=h.players[h.current];ggv_value v{};
        switch(b) {
        case GGV_FUNCTION_EXT_CONTROLS:v.b=player.ext;break;
        case GGV_FUNCTION_REMOTE_PLAYER:v.b=player.remote;break;
        case GGV_FUNCTION_USE_TIME:v.i=h.usedowntime;break;
        case GGV_TRIGGER:v.b=h.trigpressed;break;
        case GGV_FUNCTION_ACTIVE_MENU:v.b=h.fromactivemenu;break;
        case GGV_FUNCTION_DEDICATED_BUTTON:v.b=h.fromdedicatedbutton;break;
        case GGV_FUNCTION_HOLD_THRESHOLD:v.i=h.threshold;break;
        case GGV_FUNCTION_CONTROL_INVERT:v.i=player.invert;break;
        case GGV_FUNCTION_CONTROL_WEAPON:v.i=player.controlWeapon;break;
        case GGV_RELOAD_DEVICES_ACTIVE:v.u=player.devices;break;
        default:FAIL("Unexpected toggle read");
        }
        return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v) {
        auto &h=*static_cast<ToggleFunctionHost *>(p);auto &player=h.players[h.current];
        switch(b) {
        case GGV_FUNCTION_CONTROL_INVERT:player.invert=v.i;break;
        case GGV_HAND_ACTIVATE_SECONDARY:player.activate=v.b;break;
        case GGV_RELOAD_DEVICES_ACTIVE:player.devices=v.u;break;
        default:FAIL("Unexpected toggle write");
        }
    }
    static ggv_value call(void *p,ggv_service s,ggv_value a,ggv_value b,ggv_value) {
        auto &h=*static_cast<ToggleFunctionHost *>(p);ggv_value v{};
        switch(s) {
        case GGV_INFO_WEAPON_QUERY:v.i=h.queryWeapon();break;
        case GGV_HAND_CURRENT_FUNCTION:v.i=h.currentFunction();break;
        case GGV_FUNCTION_CAN_STORE:v.b=h.canStore(a.i);break;
        case GGV_STORED_FUNCTION:v.b=h.storedQuery(a.i);break;
        case GGV_FUNCTION_STORE:h.storeQuery(a.i,b.b);break;
        case GGV_FUNCTION_IS_SECONDARY:v.i=h.usingQuery();break;
        default:FAIL("Unexpected toggle service");
        }
        return v;
    }
    int graph() {
        ggv_host host{this,read,write,call};int32_t result=-99;
        REQUIRE(ggvExecute(ggvStockProgram(GGV_CONSIDER_FUNCTION_TOGGLE),&host,&result));return result;
    }
    // Source-derived donor: saved pre-conversion body; fields/services substituted only.
    int native() {
	const bool extcontrols = players[current].ext || players[current].remote;
	bool docontinue;
	switch (queryWeapon()) {
	case 21:
		if (extcontrols && usedowntime < 0) {
			return 0;
		}

		// At 25 ticks (or B+Z), start showing the new function
		players[current].invert = true;

		// B+Z immediately triggers crouch or stand
		if (trigpressed) {
			if (extcontrols) {
				players[current].activate = true;
			}
			return 1;
		}

		if (fromdedicatedbutton) {
			players[current].activate = true;
			return 0;
		}

		if (extcontrols) {
			docontinue = ((usedowntime < 0 ? -usedowntime : usedowntime) < 0);
		} else {
			// Don't do anything if B hasn't been held for 50/60ths of a second
			docontinue = (usedowntime < threshold);
		}
		if (docontinue) {
			return 0;
		}

		if (currentFunction() != 1) {
			return 0;
		}

		// Do crouch or stand
		players[current].activate = true;
		return (extcontrols ? 1 : 2);
	case 13:
		// very special alt-button handling for RCP-120's cloaking
		if (!trigpressed && extcontrols && fromdedicatedbutton) {
			if (players[current].devices & 0x80u) {
				players[current].devices &= ~0x80u;
			} else {
				players[current].devices = (players[current].devices & ~0x80u) | 0x80u;
			}
			players[current].invert = false;
			return 1;
		}
	case 14:
	case 15:
	case 34:
		// These weapons use temporary alt functions
		if (extcontrols) {
			players[current].invert = !players[current].invert;
		} else {
			players[current].invert = true;
		}

		if (fromactivemenu && usingQuery() == true) {
			players[current].activate = true;
		}

		return 1;
	case 6:
	case 10:
	case 16:
	case 17:
	case 22:
	case 32:
		// These weapons disallow B+Z
		if (!trigpressed) {
			if (canStore(players[current].controlWeapon)) {
				if (1 - storedQuery(players[current].controlWeapon)) {
					storeQuery(players[current].controlWeapon, true);
				} else {
					storeQuery(players[current].controlWeapon, false);
				}
			}

			return 1;
		}

		return 0;
	default:
		if (trigpressed) {
			players[current].invert = true;
		} else {
			if (canStore(players[current].controlWeapon)) {
				if (!storedQuery(players[current].controlWeapon)) {
					storeQuery(players[current].controlWeapon, true);
				} else {
					storeQuery(players[current].controlWeapon, false);
				}
			}
		}

		return 1;
	}
}
};
static void compareToggle(ToggleFunctionHost initial) {
    auto native=initial,graph=initial;int expected=native.native();REQUIRE(graph.graph()==expected);
    REQUIRE(graph.current==native.current);REQUIRE(graph.events==native.events);
    for(int p=0;p<2;++p) {
        REQUIRE(graph.players[p].activate==native.players[p].activate);
        REQUIRE(graph.players[p].invert==native.players[p].invert);
        REQUIRE(graph.players[p].devices==native.players[p].devices);
        REQUIRE(graph.players[p].stored==native.players[p].stored);
    }
}
TEST_CASE("function toggle matches native weapon policy and regional hold thresholds","[gameplay][graph][hand][parity]") {
    for(int weapon:{-1,2,6,10,13,14,15,16,17,21,22,32,34,35,86,108,109})
    for(int time:{-51,-1,0,40,41,49,50,51})for(int flags=0;flags<32;++flags)
    for(int variant=0;variant<6;++variant)for(int threshold:{41,50}) {
        ToggleFunctionHost h;h.weapon=weapon;h.usedowntime=time;h.threshold=threshold;
        h.players[0].ext=flags&1;h.players[0].remote=flags&2;h.trigpressed=flags&4;
        h.fromactivemenu=flags&8;h.fromdedicatedbutton=flags&16;
        h.players[0].invert=variant/2;h.players[0].function=variant%2;
        h.players[0].stored=variant%2;h.players[0].devices=0x80000000u|((variant%2)?0x80u:0u);
        h.usingResponse=variant%2;h.canStoreResponse=variant!=5;compareToggle(h);
    }
}
TEST_CASE("function toggle captures controls before callbacks and rereads selection owners","[gameplay][graph][hand][parity]") {
    for(int weapon:{2,6,13,14,21,34})for(int flips=0;flips<8;++flips)for(bool ext:{false,true}) {
        ToggleFunctionHost h;h.weapon=weapon;h.usedowntime=51;h.fromactivemenu=true;h.fromdedicatedbutton=true;
        h.players[0].ext=ext;h.players[0].controlWeapon=2;h.players[1].ext=!ext;h.players[1].controlWeapon=86;
        h.players[1].stored=true;h.players[1].function=1;h.usingResponse=true;
        h.flipOnWeapon=flips&1;h.flipOnCanStore=flips&2;h.flipOnStored=flips&4;compareToggle(h);
    }
    ToggleFunctionHost minimum;minimum.weapon=21;minimum.usedowntime=INT32_MIN;minimum.players[0].ext=true;
    compareToggle(minimum); // Native negative-time exit precedes ABS, including INT32_MIN.
    for(int response:{-1,0,1,2}) {
        ToggleFunctionHost h;h.weapon=14;h.fromactivemenu=true;h.usingResponse=response;compareToggle(h);
    }
}
TEST_CASE("signed absolute graph operation refuses the native overflow domain","[gameplay][graph][types]") {
    for(int32_t value:{INT32_MIN,INT32_MIN+1,-1,0,1,INT32_MAX}) {
        ggv_source source{};source.count=3;
        source.nodes[0].op=GGV_CONST;source.nodes[0].dst=0;source.nodes[0].type=GGV_I32;source.nodes[0].literal.i=value;
        source.nodes[1].op=GGV_I_ABS;source.nodes[1].dst=0;source.nodes[1].a=0;
        source.nodes[2].op=GGV_RETURN_I32;source.nodes[2].a=0;
        ggv_host host{nullptr,[](void *,ggv_binding)->ggv_value{return {};},[](void *,ggv_binding,ggv_value){},nullptr};
        ggv_program program{};REQUIRE(ggvPrepare(&source,&program,nullptr,0));int32_t result=-99;
        REQUIRE(bool(ggvExecute(&program,&host,&result))==(value!=INT32_MIN));
        if(value!=INT32_MIN)REQUIRE(result==(value<0?-value:value));else REQUIRE(result==-99);
        source.nodes[0].type=GGV_U32;REQUIRE_FALSE(ggvPrepare(&source,&program,nullptr,0));
    }
}
struct AutoSelectorHost {
    struct Weapon {int id=0;} weapons[128];
    struct Function {int id=0,kind=0;uint32_t flags=0;bool excluded=false,missing=false;} functions[128][2];
    struct Player {int weapon=2;bool dual=false,wantammo=false;bool owns[128]{};} players[2];
    std::vector<int> primary{13,18,2,18,1},secondary{20,2,1};
    std::vector<int> events;
    int current=0,playerNumber=0,tickmode=0,index=0,equipped=-1;
    bool doubleResult=false,flipOnLog=false,flipOnInventory=false,flipOnDouble=false,flipOnEquip=false,flipOnExclusion=false;
    bool mutatePrimary=false,mutateSecondary=false;int inventoryCalls=0,denyInventoryCall=-1;
    AutoSelectorHost() {for(int w=0;w<128;++w) {weapons[w].id=w;for(int f=0;f<2;++f){functions[w][f].id=w;functions[w][f].kind=f;}}}
    void log(int w) {events.insert(events.end(),{100,w,tickmode,current});if(flipOnLog)current=1;}
    bool hasSingle(int w) {
        REQUIRE(w>=0);REQUIRE(w<128);events.insert(events.end(),{200,w,current});
        bool result=players[current].owns[w];++inventoryCalls;if(inventoryCalls==denyInventoryCall)result=false;
        if(mutatePrimary&&inventoryCalls==1)primary[0]=3;
        if(mutateSecondary&&inventoryCalls==int(primary.size())+1)secondary[0]=3;
        if(flipOnInventory)current=1;return result;
    }
    Weapon *findWeapon(int w) {REQUIRE(w>=0);REQUIRE(w<128);events.insert(events.end(),{300,w});return &weapons[w];}
    Function *findFunction(int w,int f) {REQUIRE(w>=0);REQUIRE(w<128);REQUIRE(f>=0);REQUIRE(f<2);events.insert(events.end(),{400,w,f});return functions[w][f].missing?nullptr:&functions[w][f];}
    bool excluded(Function *f,Weapon *w) {events.insert(events.end(),{500,f?f->id:-1,f?f->kind:-1,w->id});if(flipOnExclusion)current=1;return !f||f->excluded;}
    uint32_t flags(Function *f) {REQUIRE(f!=nullptr);events.insert(events.end(),{600,f->id,f->kind});return f->flags;}
    bool hasDouble(int a,int b) {events.insert(events.end(),{700,a,b,current});if(flipOnDouble)current=1;return doubleResult;}
    void setDual(bool value) {events.insert(events.end(),{800,current,int(value)});players[current].dual=value;}
    void equip(int w) {events.insert(events.end(),{900,w,current});equipped=w;if(flipOnEquip)current=1;}
    void setWantAmmo() {events.insert(events.end(),{1000,current});players[current].wantammo=true;}
    static ggv_value read(void *p,ggv_binding b) {
        auto &h=*static_cast<AutoSelectorHost *>(p);ggv_value v{};
        switch(b) {
        case GGV_FUNCTION_CONTROL_WEAPON:v.i=h.players[h.current].weapon;break;
        case GGV_STATE_PLAYER_NUMBER:v.i=h.playerNumber;break;
        case GGV_SELECT_TICKMODE:v.i=h.tickmode;break;
        case GGV_SELECT_PRIMARY_WEAPON:REQUIRE(h.index>=0);REQUIRE(h.index<int(h.primary.size()));v.i=h.primary[h.index];break;
        case GGV_SELECT_SECONDARY_WEAPON:REQUIRE(h.index>=0);REQUIRE(h.index<int(h.secondary.size()));v.i=h.secondary[h.index];break;
        case GGV_SELECT_PRIMARY_COUNT:v.i=int(h.primary.size());break;
        case GGV_SELECT_SECONDARY_COUNT:v.i=int(h.secondary.size());break;
        default:FAIL("Unexpected selector binding");
        }
        return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v) {
        auto &h=*static_cast<AutoSelectorHost *>(p);
        switch(b) {
        case GGV_SELECT_INDEX:h.index=v.i;break;
        case GGV_SWITCH_DUALWIELD:h.setDual(v.b);break;
        case GGV_GUNCTRL_WANTAMMO:REQUIRE(v.b==1);h.setWantAmmo();break;
        default:FAIL("Unexpected selector store");
        }
    }
    static ggv_value call(void *p,ggv_service s,ggv_value a,ggv_value b,ggv_value) {
        auto &h=*static_cast<AutoSelectorHost *>(p);ggv_value v{};
        switch(s) {
        case GGV_SELECT_LOG:h.log(a.i);break;
        case GGV_SELECT_HAS_SINGLE:v.b=h.hasSingle(a.i);break;
        case GGV_SELECT_HAS_DOUBLE:v.b=h.hasDouble(a.i,b.i);break;
        case GGV_QUERY_FIND_WEAPON:v.ref=h.findWeapon(a.i);break;
        case GGV_SELECT_FIND_FUNCTION:v.ref=h.findFunction(a.i,b.i);break;
        case GGV_EXCLUSION_CHECK:v.b=h.excluded(static_cast<Function *>(a.ref),static_cast<Weapon *>(b.ref));break;
        case GGV_SELECT_FUNCTION_FLAGS:v.u=h.flags(static_cast<Function *>(a.ref));break;
        case GGV_SELECT_EQUIP:h.equip(a.i);break;
        default:FAIL("Unexpected selector service");
        }
        return v;
    }
    void graph() {ggv_host host{this,read,write,call};int32_t result=-99;REQUIRE(ggvExecute(ggvStockProgram(GGV_AUTO_SELECT_WEAPON),&host,&result));REQUIRE(result==0);}
    // Saved native donor, with only types, field accesses and observable adapters substituted.
    void native() {
	int i;
	Weapon *weapon;
	Function *func;
	int weaponnum;
	int newweaponnum = -1;
	int firstweaponnum = -1;
	int foundsuperdragon = 0;
	bool foundcurrent = false;
	int curweaponnum = players[current].weapon;
	bool wantammo = false;

	/* B-246 round-6 instrumentation: auto-switch fires when the current
	 * weapon runs out of ammo. Logging the entry point captures any
	 * unexpected mid-match weapon swap. */
	if (playerNumber == 0) {
		log(curweaponnum);
	}

	if (tickmode == 6) {
		return;
	}

	// Loop through primary, checking which weapons the
	// player has which are usable. Stop when both a usable weapon is found
	// and when the player's current weapon is found. Note the first and last
	// usable weapons.
	i = 0;

	do {
		bool usable = false;

		if (hasSingle(primary[i])) {
			weaponnum = primary[i];
			weapon = findWeapon(weaponnum);
			func = findFunction(weaponnum, 0);

			if (!excluded(func, weapon) && (flags(func) & 0x00100000u) == 0) {
				usable = true;
			}

			if (weaponnum == 18 && !foundsuperdragon) {
				foundsuperdragon++;
			} else {
				func = findFunction(weaponnum, 1);

				if (!excluded(func, weapon) && (flags(func) & 0x00100000u) == 0) {
					usable = true;
				}
			}

			if (weaponnum == curweaponnum) {
				foundcurrent = true;
			} else if (usable) {
				newweaponnum = weaponnum;

				if (firstweaponnum == -1) {
					firstweaponnum = weaponnum;
				}
			}
		}

		if (++i >= int(primary.size())) {
			break;
		}
	} while (newweaponnum == -1 || !foundcurrent);

	if (!foundcurrent) {
		newweaponnum = firstweaponnum;
	}

	if (newweaponnum == -1) {
		newweaponnum = 1;
	}

	if (newweaponnum == 1) {
		bool foundcurrent = false;
		int firstweaponnum = -1;
		int weaponnum;

		// No usable weapon was found in the primary array,
		// so search the secondary array.
		for (i = 0; i < int(secondary.size()); i++) {
			weaponnum = secondary[i];

			if (hasSingle(weaponnum)) {
				if (weaponnum == curweaponnum) {
					foundcurrent = true;
				}

				if (firstweaponnum == -1) {
					firstweaponnum = weaponnum;
				}
			}
		}

		newweaponnum = firstweaponnum;

		if (newweaponnum == -1) {
			newweaponnum = 1;
		}

		if (foundcurrent) {
			newweaponnum = -1;
		}

		wantammo = true;
	}

	// Switch to newweaponnum
	if (newweaponnum >= 0 && newweaponnum != curweaponnum) {
		if (hasDouble(newweaponnum, newweaponnum)) {
			setDual(true);
		} else {
			setDual(false);
		}

		equip(newweaponnum);

		if (wantammo) {
			setWantAmmo();
		}
	}
}
};
static void compareAutoSelector(AutoSelectorHost initial) {
    auto native=initial,graph=initial;native.native();graph.graph();
    REQUIRE(graph.events==native.events);REQUIRE(graph.equipped==native.equipped);REQUIRE(graph.current==native.current);
    REQUIRE(graph.primary==native.primary);REQUIRE(graph.secondary==native.secondary);
    for(int p=0;p<2;++p){REQUIRE(graph.players[p].dual==native.players[p].dual);REQUIRE(graph.players[p].wantammo==native.players[p].wantammo);}
}
TEST_CASE("auto selector preserves ownership, usable priority and SuperDragon duplicates","[gameplay][graph][hand][parity]") {
    for(int current:{1,2,13,18,20,99})for(unsigned owned=0;owned<32;++owned)for(int mode=0;mode<4;++mode) {
        AutoSelectorHost h;h.players[0].weapon=current;int ids[]={1,2,13,18,20};
        for(int i=0;i<5;++i)h.players[0].owns[ids[i]]=(owned&(1u<<i))!=0;
        for(int w:ids)for(int f=0;f<2;++f) {
            h.functions[w][f].excluded=mode==1&&f==0;
            h.functions[w][f].flags=mode==2?0x00100000u:0u;
            h.functions[w][f].missing=mode==3&&f==1;
        }
        h.doubleResult=(owned&1)!=0;h.players[0].wantammo=(owned&2)!=0;compareAutoSelector(h);
    }
}
TEST_CASE("auto selector retains array and current-player reads across callbacks","[gameplay][graph][hand][parity]") {
    for(int flips=0;flips<16;++flips)for(bool mutation:{false,true}) {
        AutoSelectorHost h;h.primary={2,1};h.secondary={20,1};h.players[0].weapon=99;h.players[1].weapon=20;
        for(auto &p:h.players)for(int w:{1,2,3,20})p.owns[w]=true;
        h.flipOnLog=flips&1;h.flipOnInventory=flips&2;h.flipOnDouble=flips&4;h.flipOnEquip=flips&8;
        h.mutatePrimary=mutation;h.doubleResult=true;compareAutoSelector(h);
        for(auto &funcs:h.functions)for(auto &f:funcs)f.excluded=true;
        h.mutatePrimary=false;h.mutateSecondary=mutation;compareAutoSelector(h);
    }
    AutoSelectorHost duplicate;duplicate.primary={18,18,1};duplicate.players[0].weapon=99;
    duplicate.players[0].owns[18]=true;duplicate.denyInventoryCall=1;compareAutoSelector(duplicate);
    AutoSelectorHost eligibility;eligibility.primary={13,2,1};eligibility.players[0].weapon=2;
    eligibility.players[0].owns[13]=true;eligibility.players[1].owns[2]=true;
    eligibility.players[1].weapon=99;eligibility.flipOnExclusion=true;compareAutoSelector(eligibility);
}
TEST_CASE("auto selector preserves cutscene, full fallback scan and finite long arrays","[gameplay][graph][hand][parity]") {
    for(int player:{0,1}) {AutoSelectorHost h;h.playerNumber=player;h.tickmode=6;compareAutoSelector(h);}
    AutoSelectorHost h;h.primary={1};h.players[0].owns[1]=true;h.players[0].owns[20]=true;
    h.secondary={20,1,2};h.players[0].owns[2]=true;h.players[0].weapon=2;compareAutoSelector(h);
    h.players[0].weapon=99;h.secondary={20};compareAutoSelector(h);
    h.secondary.clear();compareAutoSelector(h);
    h.primary.assign(2048,2);h.players[0].owns[2]=false;compareAutoSelector(h);
}
// Saved native sound branch supplies the reference, including both version layouts.
struct EquipSoundHost {
    struct Player { int camera=0, dead=0; } players[2];
    int owner=0, update=1, invincible=0, weapon=2, handnum=0;
    int priority=17, audio=23, nextWeapon=-1;
    bool modern=true, isLoaded=true, hasHandle=true, flipOnLoaded=false, invincibleOnLoaded=false;
    std::vector<int64_t> events;
    bool loaded() { events.push_back(15); if(flipOnLoaded)owner^=1;
        if(invincibleOnLoaded)invincible=7; if(nextWeapon>=0)weapon=nextWeapon; return isLoaded; }
    int currentPriority() { events.push_back(10);return priority; }
    int audioPriority() { events.push_back(11);return audio; }
    void setPriority(int p) {events.insert(events.end(),{12,p});priority=p;}
    void *start(int sound) {events.insert(events.end(),{13,sound});return hasHandle?reinterpret_cast<void*>(uintptr_t{0x1234}):nullptr;}
    void event(void *handle,int bits) {REQUIRE(handle!=nullptr);events.insert(events.end(),{14,bits});}
    template<bool Modern> void nativeVersion() {
			if (update > 0
					&& players[owner].camera != 1
					&& loaded()
					&& !invincible
					&& !players[owner].dead) {
void *handle1, *handle2, *handle3;
float speed1, speed2, speed3;
int prevpri1=0, prevpri2=0, prevpri3=0;

				switch (weapon) {
				case 42:
					speed1 = 3.5f;

if constexpr (Modern) {
					prevpri1 = currentPriority();
					setPriority(audioPriority() + 1);
}
					handle1 = start(101);

					if (handle1) {
						event(handle1, std::bit_cast<int32_t>(speed1));
					}

if constexpr (Modern) {
					setPriority(prevpri1);
}
					break;
				case 29:
					start(102);
					break;
				case 26:
					start(103);
					break;
				case 34:
					if (handnum == 0) {
						start(104);
					}
					break;
				case 32:
				case 33:
				case 45:
				case 46:
				case 47:
				case 48:
				case 49:
				case 50:
				case 53:
				case 54:
				case 55:
					start(104);
					break;
				case 28:
				case 36:
					speed2 = 1.5f;

if constexpr (Modern) {
					prevpri2 = currentPriority();
					setPriority(audioPriority() + 1);
}

					handle2 = start(105);

					if (handle2) {
						event(handle2, std::bit_cast<int32_t>(speed2));
					}

if constexpr (Modern) {
					setPriority(prevpri2);
}
					break;
				case 20:
					speed3 = 0.85f;

if constexpr (Modern) {
					prevpri3 = currentPriority();
					setPriority(audioPriority() + 1);
}

					handle3 = start(105);

					if (handle3) {
						event(handle3, std::bit_cast<int32_t>(speed3));
					}

if constexpr (Modern) {
					setPriority(prevpri3);
}
					break;
				case 0:
				case 1:
				case 14:
				case 27:
				case 30:
				case 31:
				case 35:
				case 41:
				case 51:
				case 52:
				case 56:
				case 57:
				case 58:
				case 59:
				case 60:
				case 61:
				case 62:
				case 63:
				case 64:
				case 65:
				case 66:
				case 67:
				case 68:
				case 69:
				case 70:
				case (Modern ? 72 : 71):
				case (Modern ? 79 : 78):
					// No equip sound
					break;
				default:
					start(105);
					break;
				}
			}
    }
    void native() {if(modern)nativeVersion<true>();else nativeVersion<false>();}
    static ggv_value read(void *opaque,ggv_binding binding) {
        auto &h=*static_cast<EquipSoundHost*>(opaque);ggv_value v{};
        switch(binding) {
        case GGV_RELOAD_LVUPDATE240:v.i=h.update;break;
        case GGV_RELOAD_CAMERA_MODE:v.i=h.players[h.owner].camera;break;
        case GGV_RELOAD_PLAYER_INVINCIBLE:v.b=h.invincible!=0;break;
        case GGV_RELOAD_PLAYER_DEAD:v.b=h.players[h.owner].dead!=0;break;
        case GGV_WEAPON_NUMBER:v.i=h.weapon;break;
        case GGV_HAND_NUMBER:v.i=h.handnum;break;
        case GGV_VERSION_1_OR_LATER:v.b=h.modern;break;
        case GGV_EQUIP_SOUND_SCANNER:v.i=101;break;
        case GGV_EQUIP_SOUND_LASER:v.i=102;break;
        case GGV_EQUIP_SOUND_KNIFE:v.i=103;break;
        case GGV_EQUIP_SOUND_MINE:v.i=104;break;
        case GGV_EQUIP_SOUND_GUN:v.i=105;break;
        case GGV_EQUIP_WEAPON_NECKLACE:v.i=h.modern?72:71;break;
        case GGV_EQUIP_WEAPON_BRIEFCASE2:v.i=h.modern?79:78;break;
        default:FAIL("Unexpected equip-sound read");
        }return v;
    }
    static void write(void*,ggv_binding,ggv_value) {FAIL("Equip sound has no field stores");}
    static ggv_value call(void *opaque,ggv_service service,ggv_value a,ggv_value b,ggv_value) {
        auto &h=*static_cast<EquipSoundHost*>(opaque);ggv_value v{};
        switch(service) {
        case GGV_RELOAD_IS_LOADED:v.b=h.loaded();break;
        case GGV_CURRENT_PRIORITY:v.i=h.currentPriority();break;
        case GGV_AUDIO_PRIORITY:v.i=h.audioPriority();break;
        case GGV_SET_PRIORITY:h.setPriority(a.i);break;
        case GGV_EMPTY_SOUND_START:v.ref=h.start(a.i);break;
        case GGV_SOUND_EVENT:h.event(a.ref,b.i);break;
        default:FAIL("Unexpected equip-sound service");
        }return v;
    }
};
void compareEquipSound(EquipSoundHost input) {
    auto expected=input,actual=input;expected.native();
    ggv_host host{&actual,EquipSoundHost::read,EquipSoundHost::write,EquipSoundHost::call};int32_t result=-1;
    REQUIRE(ggvExecute(ggvStockProgram(GGV_EQUIP_SOUND),&host,&result));REQUIRE(result==0);
    REQUIRE(actual.events==expected.events);REQUIRE(actual.priority==expected.priority);
    REQUIRE(actual.owner==expected.owner);REQUIRE(actual.weapon==expected.weapon);REQUIRE(actual.invincible==expected.invincible);
}
TEST_CASE("Equip sound graph preserves every weapon case, hand, version and nullable handle", "[gameplay_graph][equip_sound]") {
    for(bool modern:{false,true})for(int weapon=-1;weapon<96;++weapon)for(int hand:{0,1})for(bool handle:{false,true}) {
        EquipSoundHost h;h.modern=modern;h.weapon=weapon;h.handnum=hand;h.hasHandle=handle;compareEquipSound(h);
    }
}
TEST_CASE("Equip sound graph preserves lazy gates and fresh callback-owned fields", "[gameplay_graph][equip_sound]") {
    for(int update:{-1,0,1})for(int camera:{0,1,2})for(bool loaded:{false,true})for(int invincible:{0,1,7})for(int dead:{0,1,7}) {
        EquipSoundHost h;h.weapon=42;h.update=update;h.players[0].camera=camera;
        h.isLoaded=loaded;h.invincible=invincible;h.players[0].dead=dead;compareEquipSound(h);
    }
    for(bool modern:{false,true})for(int next:{1,20,28,29,34,36,42,71,72,78,79})for(int dead:{0,7}) {
        EquipSoundHost h;h.modern=modern;h.weapon=2;h.nextWeapon=next;
        h.flipOnLoaded=true;h.players[1].camera=1;h.players[1].dead=dead;compareEquipSound(h);
        h.invincibleOnLoaded=true;compareEquipSound(h);
    }
}

// Reference body below is mechanically adapted from the saved native parent.
struct ChangeGunHost {
    struct Hand {
        int state=5,statecycles=1,stateminor=0,stateframes=0,inuse=1,animmode=0;
        int ejectstate=0,ejecttype=0,count=1,count60=0,pausetime60=0,pausechange=0;
        uint32_t stateflags=0,mode=0;int firing=0,attacktype=0,unk0cc8_02=0;
        struct {int weaponnum=2,weaponfunc=0;} gset;float armPitch=0;
    } passed;
    struct Weapon {void *unequip_animation=nullptr,*equip_animation=nullptr;} definitions[2];
    struct Player {int throwing=0,switchto=-1,gunmemnew=-1,doautoselect=1;Hand hands[2];} players[2];
    uint32_t flags[128]{};int files[128]{};
    int owner=0,definitionIndex=0,weaponNumber=2,handnum=0,lvupdate=7,playerNumber=0,frame=0;
    int ammoResult=0,completeResult=1,scriptEvent=0,scriptOrdinal=1,calls[16]{};
    bool normalMultiplayer=false,multiplayer=false,pal=false,isLoaded=true,acceptState=true;
    bool mutateOwner=false,mutateDefinition=false,mutateCaptured=false;
    int mutateWeapon=-1,mutateMinor=-1,mutateInuse=-1,mutateThrowing=-1;
    int diagnostic[5]{};std::vector<int64_t> events;
    static void *token(int i) {return reinterpret_cast<void*>(static_cast<uintptr_t>(i));}
    static void handSnapshot(std::vector<int64_t> &out,const Hand &h) {
        out.insert(out.end(),{h.state,h.statecycles,h.stateminor,h.stateframes,h.inuse,h.animmode,
            h.ejectstate,h.ejecttype,h.count,h.count60,h.pausetime60,h.pausechange,h.stateflags,h.mode,
            h.firing,h.attacktype,h.unk0cc8_02,h.gset.weaponnum,h.gset.weaponfunc,std::bit_cast<int32_t>(h.armPitch)});
    }
    std::vector<int64_t> snapshot() const {
        std::vector<int64_t> out{owner,definitionIndex,weaponNumber};handSnapshot(out,passed);
        for(const auto &p:players){out.insert(out.end(),{p.throwing,p.switchto,p.gunmemnew,p.doautoselect});for(const auto &h:p.hands)handSnapshot(out,h);}
        for(const auto &w:definitions)out.insert(out.end(),{static_cast<int64_t>(reinterpret_cast<uintptr_t>(w.unequip_animation)),static_cast<int64_t>(reinterpret_cast<uintptr_t>(w.equip_animation))});
        return out;
    }
    void record(int event,int64_t a=0,int64_t b=0) {
        events.insert(events.end(),{event,a,b});auto state=snapshot();events.insert(events.end(),state.begin(),state.end());
    }
    void mutate(int event) {
        if(++calls[event]!=scriptOrdinal||event!=scriptEvent)return;
        if(mutateOwner)owner^=1;if(mutateDefinition)definitionIndex^=1;
        if(mutateCaptured){definitions[0].unequip_animation=token(3);definitions[0].equip_animation=token(4);}
        if(mutateWeapon>=0)weaponNumber=mutateWeapon;if(mutateMinor>=0)passed.stateminor=mutateMinor;
        if(mutateInuse>=0)passed.inuse=mutateInuse;if(mutateThrowing>=0)players[owner].throwing=mutateThrowing;
    }
    bool flag(int weapon,uint32_t mask) {bool result=weapon>=0&&weapon<128&&(flags[weapon]&mask)!=0;record(1,weapon,mask);mutate(1);return result;}
    int ammo(int function) {record(2,function);mutate(2);return ammoResult;}
    void animation(void *anim) {record(3,static_cast<int64_t>(reinterpret_cast<uintptr_t>(anim)));passed.animmode=1;mutate(3);}
    void action(int event) {record(event);mutate(event);}
    void pitch(float value) {record(6,std::bit_cast<int32_t>(value));passed.armPitch=value;mutate(6);}
    bool loaded() {record(7);mutate(7);return isLoaded;}
    int complete() {record(8);mutate(8);return completeResult;}
    bool setState(int state) {record(9,state);if(acceptState){passed.state=state;passed.stateminor=0;passed.stateframes=0;passed.statecycles=0;}mutate(9);return acceptState;}
    int file(int weapon) {int result=weapon>=0&&weapon<128?files[weapon]:0;record(13,weapon);mutate(13);return result;}
    int ticks(int value) const {return pal?value*50/60:value;}
    void log(int loaded,int switchto,int gunmem,int other,int result) {record(14);events.insert(events.end(),{loaded,switchto,gunmem,other,result});mutate(14);}
    int native() {Hand *hand=&passed;

	
	Weapon *weapon = &definitions[definitionIndex];

	if (hand->statecycles == 0) {
		if (normalMultiplayer == false) {
			hand->pausetime60 = 0;
		} else {
			hand->pausetime60 = 0;
		}
	}

	// Handle unequip animation. Wait in this state until the animation is
	// finished, or skip this state if there is no animation to play.
	if (hand->stateminor == 0) {
		bool skipanim = false;

		if (flag(weaponNumber, 0x00000001)
				&& !(weaponNumber == 34 && handnum == 1)
				&& ammo(0) < 0) {
			skipanim = true;
		}

		hand->count = 0;

		if (!skipanim) {
			if (weapon->unequip_animation
					&& hand->inuse == true
					&& !(hand->ejectstate != 0 && hand->ejecttype == 0)) {
				if (hand->statecycles == 0) {
					animation(weapon->unequip_animation);
				} else if (hand->animmode == 0) {
					hand->stateminor++; // to 1
				}
			} else {
				hand->stateflags |= 0x00000001;

				if (hand->ejectstate == 1) {
					return 0;
				}

				hand->stateminor++; // to 1
			}
		} else {
			hand->stateminor++; // to 1
		}

		if (hand->stateminor == 1) {
			hand->stateframes = 0;
		}
	}

	// For classic guns, handle lowering it to offscreen.
	// Throw the gun if that's what the player is doing.
	if (hand->stateminor == 1) {
		int delay = ticks(16);
		bool throwing = false;
		

		hand->count = 0;

		if (normalMultiplayer) {
			delay = ticks(12);
		}

		if (weapon->unequip_animation && (hand->stateflags & 0x00000001) == 0) {
			delay = 1;
		}

		if (!hand->inuse) {
			delay = 1;
		}

		if (hand->ejecttype == 0
				&& (hand->ejectstate == 1 || hand->ejectstate == 2)) {
			throwing = true;
		}

		if (players[owner].throwing == true) {
			throwing = true;
		}

		if (hand->stateframes >= delay) {
			if (!throwing) {
				if (multiplayer) {
					action(4);
				}

				action(5);
				hand->mode = 6;
				hand->stateminor++; // to 2
			} else {
				pitch(0.87252569198608f);

				if (players[owner].throwing == true && hand->inuse) {
					hand->firing = true;
					hand->attacktype = 3;
					hand->gset.weaponfunc = 1;
				}
			}
		} else {
			pitch(hand->stateframes * 0.87252569198608f / delay);
		}
	}

	// Wait for the new gun to be loaded, then start its equip animation
	// (if any) and move on to the next state.
	if (hand->stateminor == 2) {
		hand->animmode = 0;

		/* B-246 round-4 instrumentation: log the LOAD/6 -> 7
		 * decision so the next playtest reveals whether `bgun0f09bf44`
		 * returns false in this codepath and which of its 5 conditions
		 * trips. Player 0, hand 0 only, every ~120 frames while wedged. */
		if (playerNumber == 0 && handnum == 0
				&& (frame % 120) == 13) {
			int cond_loaded = loaded() ? 1 : 0;
			int cond_switchto_neg = (players[owner].switchto == -1) ? 1 : 0;
			int cond_gunmemnew_neg = (players[owner].gunmemnew < 0) ? 1 : 0;
			int cond_other_not_reload = (players[owner].hands[1 - handnum].state != 1) ? 1 : 0;
			int res = complete();
			log(cond_loaded, cond_switchto_neg, cond_gunmemnew_neg, cond_other_not_reload, res);
		}

		if (hand->pausechange == 0 || hand->pausetime60 <= hand->count60) {
			if (hand->mode == 6) {
				if (complete()) {
					hand->mode = 7;

					if (!hand->inuse && setState(0)) {
						return lvupdate;
					}
				}
			} else {
				if (loaded()) {
					if (definitions[definitionIndex].equip_animation) {
						animation(definitions[definitionIndex].equip_animation);
						hand->unk0cc8_02 = true;
					}

					hand->mode = 8;
					hand->stateminor++; // to 3
					hand->count60 = 0;
					hand->count = 0;
				}
			}
		}

		if (hand->mode == 6 || hand->mode == 7) {
			pitch(0.87252569198608f);
		}
	}

	// Handle raising the new gun and playing the equipped sound effect.
	if (hand->stateminor == 3) {
		int delay = ticks(23);

		if (normalMultiplayer) {
			delay = ticks(12);
		}

		if (flag(hand->gset.weaponnum, 0x00004000)) {
			hand->animmode = 0;
		} else if (weapon->equip_animation) {
			delay = 1;
		}

		if (hand->count == 0) {
			if (multiplayer) {
				action(10);
			}

			action(11);

			if (flag(weaponNumber, 0x00000001)
					&& (weaponNumber != 34 || handnum != 1)
					&& ammo(0) < 0
					&& setState(8)) {
				hand->stateminor = 1;
				return lvupdate;
			}

			players[owner].doautoselect = false;

			action(12);
		}

		if (hand->count60 >= delay
				|| !file(weaponNumber)
				|| !flag(weaponNumber, 0x00000040)
				|| flag(weaponNumber, 0x00000080)) {
			hand->mode = 0;
			hand->stateminor++; // to 4

			if (flag(hand->gset.weaponnum, 0x00004000) == 0) {
				hand->unk0cc8_02 = false;
			}

			hand->count60 = 0;
			hand->count = 0;
		} else {
			pitch((delay - hand->count60) * 0.87252569198608f / delay);
		}
	}

	// Wait for equip animation to finish then go to idle state
	if (hand->stateminor == 4) {
		if (definitions[definitionIndex].equip_animation && !flag(hand->gset.weaponnum, 0x00004000)) {
			if (hand->animmode == 0) {
				if (setState(0)) {
					return lvupdate;
				}
			}
		} else {
			if (setState(0)) {
				return lvupdate;
			}
		}
	}

	return 0;

    }
    static ggv_value read(void *opaque,ggv_binding binding) {
        auto &h=*static_cast<ChangeGunHost*>(opaque);ggv_value value{};
switch (binding) {
    case GGV_CHANGE_DEFINITION: value.ref = &h.definitions[h.definitionIndex]; break;
    case GGV_PASSED_HAND: value.ref = &h.passed; break;
    case GGV_HAND_NUMBER: value.i = h.handnum; break;
    case GGV_EMPTY_LVUPDATE: value.i = h.lvupdate; break;
    case GGV_STATE_CYCLES: value.i = h.passed.statecycles; break;
    case GGV_STATE_MINOR: value.i = h.passed.stateminor; break;
    case GGV_STATE_INUSE: value.i = h.passed.inuse; break;
    case GGV_STATE_FRAMES: value.i = h.passed.stateframes; break;
    case GGV_STATE_FLAGS: value.u = h.passed.stateflags; break;
    case GGV_EMPTY_ANIM_MODE: value.i = h.passed.animmode; break;
    case GGV_RELOAD_EJECT_STATE: value.i = h.passed.ejectstate; break;
    case GGV_CHANGE_EJECT_TYPE: value.i = h.passed.ejecttype; break;
    case GGV_WEAPON_NUMBER: value.i = h.weaponNumber; break;
    case GGV_AMMO_HAND_WEAPON: value.i = h.passed.gset.weaponnum; break;
    case GGV_NORMAL_MULTIPLAYER: value.b = h.normalMultiplayer != 0; break;
    case GGV_MULTIPLAYER: value.b = h.multiplayer != 0; break;
    case GGV_SWITCH_CURRENT_THROWING: value.i = h.players[h.owner].throwing; break;
    case GGV_STATE_PLAYER_NUMBER: value.i = h.playerNumber; break;
    case GGV_CURRENT_FRAME: value.i = h.frame; break;
    case GGV_CHANGE_SWITCHTO: value.i = h.players[h.owner].switchto; break;
    case GGV_LOADED_GUNMEMNEW: value.i = h.players[h.owner].gunmemnew; break;
    case GGV_RELOAD_OTHER_STATE: value.i = h.players[h.owner].hands[1 - h.handnum].state; break;
    case GGV_CHANGE_PAUSECHANGE: value.i = h.passed.pausechange; break;
    case GGV_RELOAD_PAUSETIME60: value.i = h.passed.pausetime60; break;
    case GGV_RELOAD_COUNT60: value.i = h.passed.count60; break;
    case GGV_RELOAD_COUNT: value.i = h.passed.count; break;
    case GGV_RELOAD_MODE: value.u = h.passed.mode; break;
    default: FAIL("Unexpected change-gun read");
    }
return value;
    }
    static void write(void *opaque,ggv_binding binding,ggv_value value) {
        auto &h=*static_cast<ChangeGunHost*>(opaque);
switch (binding) {
    case GGV_RELOAD_PAUSETIME60: h.passed.pausetime60 = value.i; break;
    case GGV_RELOAD_COUNT: h.passed.count = value.i; break;
    case GGV_RELOAD_COUNT60: h.passed.count60 = value.i; break;
    case GGV_STATE_FLAGS: h.passed.stateflags = value.u; break;
    case GGV_STATE_MINOR: h.passed.stateminor = value.i; break;
    case GGV_STATE_FRAMES: h.passed.stateframes = value.i; break;
    case GGV_RELOAD_MODE: h.passed.mode = value.u; break;
    case GGV_FIRING: h.passed.firing = value.b; break;
    case GGV_ATTACK_TYPE: h.passed.attacktype = value.i; break;
    case GGV_WEAPON_FUNCTION: h.passed.gset.weaponfunc = value.i; break;
    case GGV_EMPTY_ANIM_MODE: h.passed.animmode = value.i; break;
    case GGV_RELOAD_UNK0CC8_02: h.passed.unk0cc8_02 = value.b; break;
    case GGV_RELOAD_DOAUTOSELECT: h.players[h.owner].doautoselect = value.i; break;
    case GGV_CHANGE_DIAG_LOADED: h.diagnostic[0] = value.b != 0; break;
    case GGV_CHANGE_DIAG_SWITCHTO: h.diagnostic[1] = value.b != 0; break;
    case GGV_CHANGE_DIAG_GUNMEM: h.diagnostic[2] = value.b != 0; break;
    case GGV_CHANGE_DIAG_OTHER: h.diagnostic[3] = value.b != 0; break;
    case GGV_CHANGE_DIAG_RESULT: h.diagnostic[4] = value.i; break;
    default: FAIL("Unexpected change-gun write");
    }
    }
    static ggv_value call(void *opaque,ggv_service service,ggv_value a,ggv_value b,ggv_value d) {
        auto &h=*static_cast<ChangeGunHost*>(opaque);ggv_value value{};
        switch(service) {
        case GGV_CHANGE_UNEQUIP_ANIM:value.ref=static_cast<Weapon*>(a.ref)->unequip_animation;break;
        case GGV_CHANGE_EQUIP_ANIM:value.ref=static_cast<Weapon*>(a.ref)->equip_animation;break;
        case GGV_RELOAD_WEAPON_HAS_FLAG:value.b=h.flag(a.i,b.u);break;
        case GGV_RELOAD_AMMO_STATUS:value.i=h.ammo(a.i);break;
        case GGV_RECOIL_TICKS:value.i=h.ticks(a.i);break;
        case GGV_HAND_START_ANIMATION:REQUIRE(b.i==h.handnum);REQUIRE(d.ref==&h.passed);h.animation(a.ref);break;
        case GGV_HAND_DELETE_WEAPON:h.action(4);break;
        case GGV_HAND_FREE_ROCKET:h.action(5);break;
        case GGV_RELOAD_SET_ARM_PITCH:h.pitch(a.f);break;
        case GGV_RELOAD_IS_LOADED:value.b=h.loaded();break;
        case GGV_CHANGE_COMPLETE:value.i=h.complete();break;
        case GGV_HAND_SET_STATE:value.b=h.setState(a.i);break;
        case GGV_CHANGE_CREATE_WEAPON:h.action(10);break;
        case GGV_CHANGE_ACTIVATE:h.action(11);break;
        case GGV_CHANGE_EQUIP_SOUND:h.action(12);break;
        case GGV_RELOAD_FILENUM2:value.i=h.file(a.i);break;
        case GGV_CHANGE_LOG_LOAD:h.log(h.diagnostic[0],h.diagnostic[1],h.diagnostic[2],h.diagnostic[3],h.diagnostic[4]);break;
        default:FAIL("Unexpected change-gun service");
        }return value;
    }
};
void compareChangeGun(ChangeGunHost input) {
    auto expected=input,actual=input;int wanted=expected.native();
    ggv_host host{&actual,ChangeGunHost::read,ChangeGunHost::write,ChangeGunHost::call};int32_t result=-1;
    REQUIRE(ggvExecute(ggvStockProgram(GGV_CHANGE_GUN),&host,&result));REQUIRE(result==wanted);
    REQUIRE(actual.events==expected.events);REQUIRE(actual.snapshot()==expected.snapshot());
}
TEST_CASE("Change-gun graph preserves unequip gates and same-call lower progression", "[gameplay_graph][change_gun]") {
    for(int cycles:{0,1})for(int inuse:{0,1,2})for(int eject:{0,1,2,3})for(int type:{0,1})for(bool anim:{false,true})for(int busy:{0,1}) {
        ChangeGunHost h;h.passed.statecycles=cycles;h.passed.inuse=inuse;h.passed.ejectstate=eject;h.passed.ejecttype=type;
        h.definitions[0].unequip_animation=anim?h.token(1):nullptr;h.passed.animmode=busy;compareChangeGun(h);
    }
    for(int weapon:{2,34})for(int hand:{0,1})for(int ammo:{-1,0,1}) {
        ChangeGunHost h;h.weaponNumber=weapon;h.handnum=hand;h.flags[weapon]=1;h.ammoResult=ammo;
        h.definitions[0].unequip_animation=h.token(1);compareChangeGun(h);
    }
}
TEST_CASE("Change-gun graph preserves regional lower and raise thresholds and pitch order", "[gameplay_graph][change_gun]") {
    for(bool pal:{false,true})for(bool mp:{false,true})for(int phase:{1,3})for(int count:{-1,0,1,9,10,11,12,13,15,16,19,22,23,24}) {
        ChangeGunHost h;h.pal=pal;h.normalMultiplayer=mp;h.multiplayer=mp;h.passed.stateminor=phase;
        h.passed.stateframes=count;h.passed.count60=count;h.files[2]=1;h.flags[2]=0x40;compareChangeGun(h);
        h.definitions[0].unequip_animation=h.token(1);h.definitions[0].equip_animation=h.token(2);compareChangeGun(h);
        h.passed.stateflags=1;h.passed.inuse=0;compareChangeGun(h);
    }
    for(int throwing:{0,1,2})for(int eject:{0,1,2,3})for(int type:{0,1})for(int inuse:{0,1,2}) {
        ChangeGunHost h;h.passed.stateminor=1;h.passed.stateframes=30;h.players[0].throwing=throwing;
        h.passed.ejectstate=eject;h.passed.ejecttype=type;h.passed.inuse=inuse;compareChangeGun(h);
    }
}
TEST_CASE("Change-gun graph preserves loading diagnostics, pause gates and mode deferral", "[gameplay_graph][change_gun]") {
    for(int frame:{-3,0,13,133})for(int player:{0,1})for(int hand:{0,1})for(int mode:{0,6,7,8})for(bool loaded:{false,true})for(int complete:{0,1,7}) {
        ChangeGunHost h;h.passed.stateminor=2;h.frame=frame;h.playerNumber=player;h.handnum=hand;h.passed.mode=mode;
        h.isLoaded=loaded;h.completeResult=complete;h.definitions[0].equip_animation=h.token(2);compareChangeGun(h);
        h.passed.inuse=0;compareChangeGun(h);h.acceptState=false;compareChangeGun(h);
    }
    for(int pause:{0,1,2})for(int time:{-1,0,1,2}) {
        ChangeGunHost h;h.passed.stateminor=2;h.passed.pausechange=pause;h.passed.pausetime60=time;h.passed.count60=1;h.passed.mode=7;compareChangeGun(h);
    }
}
TEST_CASE("Change-gun graph preserves raise selection and final equip admission", "[gameplay_graph][change_gun]") {
    for(int phase:{3,4})for(int count:{0,1})for(bool accept:{false,true})for(int busy:{0,1})for(uint32_t flags:{0u,1u,0x40u,0x80u,0xC0u,0x4000u,0x4040u,0x40C0u}) {
        ChangeGunHost h;h.passed.stateminor=phase;h.passed.count=count;h.acceptState=accept;h.passed.animmode=busy;
        h.flags[2]=flags;h.files[2]=1;h.ammoResult=-1;h.definitions[0].equip_animation=h.token(2);compareChangeGun(h);
        h.definitions[0].equip_animation=nullptr;compareChangeGun(h);
    }
    for(int hand:{0,1})for(int update:{0,7}) {
        ChangeGunHost h;h.passed.stateminor=3;h.passed.count=0;h.weaponNumber=34;h.flags[34]=1;h.ammoResult=-1;h.handnum=hand;h.lvupdate=update;compareChangeGun(h);
    }
}
TEST_CASE("Change-gun graph preserves callback mutation and captured definition identity", "[gameplay_graph][change_gun]") {
    for(int phase:{0,1,2,3,4})for(int event=1;event<=14;++event)for(int ordinal:{1,2,3}) {
        ChangeGunHost h;h.passed.stateminor=phase;h.passed.stateframes=30;h.passed.count60=0;h.passed.count=0;
        h.frame=13;h.multiplayer=true;h.passed.mode=7;h.flags[2]=0x41;h.files[2]=1;h.ammoResult=-1;
        h.definitions[0].unequip_animation=h.token(1);h.definitions[1].equip_animation=h.token(2);
        h.players[1].switchto=4;h.players[1].gunmemnew=5;h.players[1].hands[1].state=1;
        h.scriptEvent=event;h.scriptOrdinal=ordinal;h.mutateOwner=true;h.mutateDefinition=true;h.mutateWeapon=34;compareChangeGun(h);
        h.mutateCaptured=true;h.mutateMinor=3;h.mutateInuse=2;h.mutateThrowing=1;compareChangeGun(h);
    }
    for(int phase:{0,2})for(int minor:{-1,1,3,4})for(bool captured:{false,true}) {
        ChangeGunHost h;h.passed.stateminor=phase;h.passed.statecycles=0;h.passed.mode=7;
        h.passed.count60=2;h.flags[2]=0x40;h.files[2]=1;h.ammoResult=0;
        h.definitions[0].unequip_animation=h.token(1);h.definitions[0].equip_animation=h.token(2);
        h.scriptEvent=3;h.mutateOwner=true;h.mutateDefinition=true;h.mutateCaptured=captured;
        h.mutateMinor=minor;h.mutateInuse=2;compareChangeGun(h);
    }

}

// Exact native second-pass control flow, with scripted audio and frame domains.
struct AnimationSoundHost {
    struct Command {uint8_t type=0,unk01=0;uint16_t unk02=0;intptr_t unk04=0;};
    std::vector<Command> commands{{0,0,0,0}};
    Command *cursor=nullptr;bool noSource=false,floatFrames=true,modern=true;
    int sourceIndex=0,audioCalls=0,mutateCall=0;bool mutateSource=false,mutateNext=false,mutateOutput=false;
    double from=-1,to=100;unsigned output:3 = 0;
    std::vector<bool> handles{true};std::vector<int64_t> events;
    Command *source() {return noSource?nullptr:commands.data()+sourceIndex;}
    ptrdiff_t cursorIndex() const {return cursor?cursor-commands.data():-1;}
    void record(int kind,int64_t value,int32_t bits) {events.insert(events.end(),{kind,value,bits,output,sourceIndex,cursorIndex()});}
    void mutate() {
        if(audioCalls!=mutateCall)return;
        if(mutateSource)sourceIndex=static_cast<int>(commands.size())-1;
        if(mutateNext&&commands.size()>2)commands[2].unk04=4567;
        if(mutateOutput)output=6;
    }
    void *allocate() {bool handle=handles.empty()?true:handles[static_cast<size_t>(audioCalls)%handles.size()];++audioCalls;mutate();return handle?reinterpret_cast<void*>(uintptr_t{0x1234}):nullptr;}
    void *modernSound(int32_t sound,float speed) {record(1,sound,std::bit_cast<int32_t>(speed));return allocate();}
    void *legacySound(int16_t sound) {record(2,sound,0);return allocate();}
    void event(void *handle,int32_t bits) {REQUIRE(handle!=nullptr);record(3,0,bits);}
    template<bool Modern,typename Frame>void nativeBody(Frame s4,Frame s2) {
		if (source()) {
			bool done = false;
			Command *cmd = source(); cursor = cmd;
			float speed = 1.0f;
			bool hasspeed = false;
void *audiohandle = nullptr;

			if (cmd) {
				do {
					if (cmd->type == 0) {
						done = true;
					} else {
						if (s2 >= cmd->unk02 && s4 < cmd->unk02 && s4 < s2) {
							switch (cmd->type) {
							case 5:
if constexpr (Modern) {
								if (hasspeed) {
									modernSound(cmd->unk04, speed);
									hasspeed = false;
								} else {
									modernSound(cmd->unk04, 1.0f);
								}
} else {
								audiohandle = legacySound(cmd->unk04);

								if (hasspeed && audiohandle) {
									hasspeed = false;
									event(audiohandle, std::bit_cast<int32_t>(speed));
								}
}
								break;
							case 11:
								speed = cmd->unk04 / 1000.0f;
								hasspeed = true;
								break;
							case 9:
								output++;
								break;
							}
						}
					}

					cmd++; cursor = cmd;
				} while (!done);
			}
		}
    }
    template<bool Modern>void nativeFrames() {if(floatFrames)nativeBody<Modern>(float(from),float(to));else nativeBody<Modern>(int32_t(from),int32_t(to));}
    void native() {if(modern)nativeFrames<true>();else nativeFrames<false>();}
    static ggv_value read(void *opaque,ggv_binding binding) {
        auto &h=*static_cast<AnimationSoundHost*>(opaque);ggv_value v{};
        switch(binding) {
        case GGV_ANIM_FRAME_COMMAND:v.ref=h.source();break;
        case GGV_ANIM_WAIT_CURSOR:v.ref=h.cursor;break;
        case GGV_ANIM_WAIT_TYPE:v.i=h.cursor->type;break;
        case GGV_ANIM_WAIT_KEY:v.i=h.cursor->unk02;break;
        case GGV_ANIM_WAIT_VALUE:v.i64=h.cursor->unk04;break;
        case GGV_COMMAND_FLOAT_FRAMES:v.b=h.floatFrames;break;
        case GGV_COMMAND_FROM_FLOAT:v.f=float(h.from);break;
        case GGV_COMMAND_TO_FLOAT:v.f=float(h.to);break;
        case GGV_COMMAND_FROM_INT:v.i=int32_t(h.from);break;
        case GGV_COMMAND_TO_INT:v.i=int32_t(h.to);break;
        case GGV_VERSION_1_OR_LATER:v.b=h.modern;break;
        case GGV_COMMAND_OUTPUT:v.u=h.output;break;
        default:FAIL("Unexpected animation-sound read");
        }return v;
    }
    static void write(void *opaque,ggv_binding binding,ggv_value v) {
        auto &h=*static_cast<AnimationSoundHost*>(opaque);
        switch(binding) {
        case GGV_ANIM_WAIT_CURSOR:h.cursor=static_cast<Command*>(v.ref);break;
        case GGV_COMMAND_OUTPUT:h.output=v.u;break;
        default:FAIL("Unexpected animation-sound write");
        }
    }
    static ggv_value call(void *opaque,ggv_service service,ggv_value a,ggv_value b,ggv_value) {
        auto &h=*static_cast<AnimationSoundHost*>(opaque);ggv_value v{};
        switch(service) {
        case GGV_ANIM_CURSOR_NEXT:v.ref=static_cast<Command*>(a.ref)+1;break;
        case GGV_COMMAND_SOUND_MODERN:v.ref=h.modernSound(a.i64,b.f);break;
        case GGV_COMMAND_SOUND_LEGACY:v.ref=h.legacySound(a.i64);break;
        case GGV_SOUND_EVENT:h.event(a.ref,b.i);break;
        default:FAIL("Unexpected animation-sound service");
        }return v;
    }
};
void compareAnimationSounds(AnimationSoundHost input) {
    auto expected=input,actual=input;expected.cursor=nullptr;actual.cursor=nullptr;expected.native();
    ggv_host host{&actual,AnimationSoundHost::read,AnimationSoundHost::write,AnimationSoundHost::call};int32_t result=-1;
    REQUIRE(ggvExecute(ggvStockProgram(GGV_ANIMATION_SOUND_COMMANDS),&host,&result));REQUIRE(result==0);
    REQUIRE(actual.events==expected.events);REQUIRE(actual.output==expected.output);
    REQUIRE(actual.cursorIndex()==expected.cursorIndex());REQUIRE(actual.audioCalls==expected.audioCalls);REQUIRE(actual.sourceIndex==expected.sourceIndex);
    REQUIRE(actual.commands.size()==expected.commands.size());
    for(size_t i=0;i<actual.commands.size();++i)REQUIRE(actual.commands[i].unk04==expected.commands[i].unk04);
}
TEST_CASE("Animation sound commands preserve version-specific speed latch and audio narrowing", "[gameplay_graph][animation_sound]") {
    for(bool modern:{false,true})for(bool frames:{false,true})for(bool first:{false,true})for(bool second:{false,true}) {
        AnimationSoundHost h;h.modern=modern;h.floatFrames=frames;h.handles={first,second};
        h.commands={{11,0,0,1500},{5,0,1,0x112345678LL},{5,0,2,-40000},{11,0,3,-2500},{5,0,4,9},{5,0,5,10},{0,0,0,0}};
        compareAnimationSounds(h);
    }
    for(int64_t speed:{int64_t{0},int64_t{16777217},int64_t{4294967297},std::numeric_limits<int64_t>::min(),std::numeric_limits<int64_t>::max()}) {
        AnimationSoundHost h;h.commands={{11,0,0,static_cast<intptr_t>(speed)},{5,0,1,2},{0,0,0,0}};
        compareAnimationSounds(h);h.modern=false;compareAnimationSounds(h);
    }
}
TEST_CASE("Animation sound commands retain frame boundaries and three-bit output wrap", "[gameplay_graph][animation_sound]") {
    for(bool frames:{false,true})for(int from:{-1,0,1,2,65534,65535,16777217})for(int to:{-1,0,1,2,65535,16777218})for(unsigned count=0;count<8;++count) {
        AnimationSoundHost h;h.floatFrames=frames;h.from=from;h.to=to;h.output=count;
        h.commands={{9,0,0,0},{9,0,1,0},{9,0,2,0},{9,0,65535,0},{0,0,0,0}};compareAnimationSounds(h);
    }
    for(float from:{-0.5f,0.0f,0.5f,std::numeric_limits<float>::quiet_NaN(),-std::numeric_limits<float>::infinity()}) {
        AnimationSoundHost h;h.from=from;h.to=1.5f;h.commands={{9,0,0,0},{9,0,1,0},{9,0,2,0},{0,0,0,0}};compareAnimationSounds(h);
    }
    AnimationSoundHost upper;upper.to=std::numeric_limits<float>::quiet_NaN();
    upper.commands={{9,0,0,0},{5,0,1,2},{0,0,0,0}};compareAnimationSounds(upper);
    AnimationSoundHost h;h.commands.assign(2048,{9,0,0,0});h.commands.push_back({0,0,0,0});compareAnimationSounds(h);
    h.noSource=true;compareAnimationSounds(h);
}
TEST_CASE("Animation sound commands retain captured cursor and mutable later commands", "[gameplay_graph][animation_sound]") {
    for(bool modern:{false,true})for(bool handle:{false,true}) {
        AnimationSoundHost h;h.modern=modern;h.handles={handle,true};h.mutateCall=1;
        h.mutateSource=true;h.mutateNext=true;h.mutateOutput=true;
        h.commands={{11,0,0,1500},{5,0,1,2},{11,0,2,2000},{9,0,3,0},{5,0,4,3},{0,0,0,0}};compareAnimationSounds(h);
    }
    AnimationSoundHost h;h.commands={{0,0,0,0},{5,0,0,8}};compareAnimationSounds(h);
}

struct AnimationPartHost {
    struct Entry {int32_t number=0,frame=-1;bool visible=false;};
    struct Command {uint8_t type=1;uint16_t unk02=0;intptr_t unk04=0;} command;
    std::vector<Entry> entries;int32_t count=0,index=0;bool allowGrowth=true;int reserveCalls=0;
    void native() {
        REQUIRE(count<=15);int32_t s0=count;int32_t partnums[16]{},partframes[16]{};bool partsvisible[16]{};
        for(int i=0;i<count;++i){partnums[i]=entries[i].number;partframes[i]=entries[i].frame;partsvisible[i]=entries[i].visible;}
        Command *cmd=&command;

							int32_t i;
							index = -1;

							for (i = 0; i < s0; i++) {
								if (cmd->unk04 == partnums[i]) {
									index = i;
								}
							}

							if (index == -1) {
								index = s0;
								s0++;

								

								partnums[index] = cmd->unk04;
								partframes[index] = -1;
							}

							if (cmd->unk02 > partframes[index]) {
								partframes[index] = cmd->unk02;
								partsvisible[index] = cmd->type == 1 ? true : false;
							}
						
        REQUIRE(s0<=15);count=s0;entries.resize(count);
        for(int i=0;i<count;++i)entries[i]={partnums[i],partframes[i],partsvisible[i]};
    }
    static ggv_value read(void *opaque,ggv_binding binding) {
        auto &h=*static_cast<AnimationPartHost*>(opaque);ggv_value v{};
        switch(binding) {
        case GGV_PART_COUNT:v.i=h.count;break;
        case GGV_PART_NUMBER:v.i=h.entries.at(h.index).number;break;
        case GGV_PART_FRAME:v.i=h.entries.at(h.index).frame;break;
        case GGV_ANIM_WAIT_VALUE:v.i64=h.command.unk04;break;
        case GGV_ANIM_WAIT_KEY:v.i=h.command.unk02;break;
        case GGV_ANIM_WAIT_TYPE:v.i=h.command.type;break;
        default:FAIL("Unexpected animation-part read");
        }return v;
    }
    static void write(void *opaque,ggv_binding binding,ggv_value v) {
        auto &h=*static_cast<AnimationPartHost*>(opaque);
        switch(binding) {
        case GGV_PART_COUNT:h.count=v.i;break;
        case GGV_PART_INDEX:h.index=v.i;break;
        case GGV_PART_NUMBER:h.entries.at(h.index).number=v.i;break;
        case GGV_PART_FRAME:h.entries.at(h.index).frame=v.i;break;
        case GGV_PART_VISIBLE:h.entries.at(h.index).visible=v.b!=0;break;
        default:FAIL("Unexpected animation-part write");
        }
    }
    static ggv_value call(void *opaque,ggv_service service,ggv_value a,ggv_value,ggv_value) {
        auto &h=*static_cast<AnimationPartHost*>(opaque);ggv_value v{};REQUIRE(service==GGV_PART_RESERVE);
        ++h.reserveCalls;REQUIRE(a.i>h.count);v.b=h.allowGrowth;if(v.b)h.entries.resize(a.i);return v;
    }
    int graph() {ggv_host host{this,read,write,call};int32_t result=123;
        REQUIRE(ggvExecute(ggvStockProgram(GGV_ANIMATION_PART_COMMAND),&host,&result));return result;}
};
void compareAnimationPart(AnimationPartHost input) {
    auto expected=input,actual=input;expected.native();REQUIRE(actual.graph()==0);
    REQUIRE(actual.count==expected.count);REQUIRE(actual.index==expected.index);
    for(int i=0;i<actual.count;++i){REQUIRE(actual.entries[i].number==expected.entries[i].number);
        REQUIRE(actual.entries[i].frame==expected.entries[i].frame);REQUIRE(actual.entries[i].visible==expected.entries[i].visible);}
}
TEST_CASE("Animation part graph preserves native lookup, last duplicate and strict frame ties", "[gameplay_graph][animation_parts]") {
    for(uint8_t type:{uint8_t{1},uint8_t{2}})for(uint16_t frame:{uint16_t{0},uint16_t{4},uint16_t{5},uint16_t{6},uint16_t{65535}})
    for(int64_t part:{int64_t{-1},int64_t{10},int64_t{20},int64_t{4294967306},std::numeric_limits<int64_t>::min(),std::numeric_limits<int64_t>::max()}) {
        AnimationPartHost h;h.command={type,frame,static_cast<intptr_t>(part)};compareAnimationPart(h);
        h.entries={{10,5,true},{-1,4,false},{10,5,false}};h.count=3;compareAnimationPart(h);
    }
    AnimationPartHost h;
    for(int i=0;i<15;++i){h.command={1,static_cast<uint16_t>(i),i};compareAnimationPart(h);REQUIRE(h.graph()==0);}
    h.command={2,14,14};compareAnimationPart(h);
}
TEST_CASE("Animation part graph supports ordered lists beyond the native fifteen-entry domain", "[gameplay_graph][animation_parts]") {
    AnimationPartHost h;
    for(int i=0;i<1024;++i){h.command={1,5,i};REQUIRE(h.graph()==0);}
    REQUIRE(h.count==1024);
    for(int i=1023;i>=0;--i){h.command={2,5,i};REQUIRE(h.graph()==0);REQUIRE(h.entries[i].visible);
        h.command.unk02=6;REQUIRE(h.graph()==0);}
    REQUIRE(h.count==1024);
    for(int i=0;i<1024;++i){REQUIRE(h.entries[i].number==i);REQUIRE(h.entries[i].frame==6);REQUIRE_FALSE(h.entries[i].visible);}
}
TEST_CASE("Animation part graph reports growth failure before adding or replacing entries", "[gameplay_graph][animation_parts]") {
    AnimationPartHost empty;empty.allowGrowth=false;REQUIRE(empty.graph()==-1);REQUIRE(empty.count==0);REQUIRE(empty.entries.empty());
    AnimationPartHost full;full.entries.assign(16,{12,3,true});full.count=16;full.command={2,4,99};full.allowGrowth=false;
    REQUIRE(full.graph()==-1);REQUIRE(full.count==16);REQUIRE(full.entries.size()==16);
    for(const auto &e:full.entries){REQUIRE(e.number==12);REQUIRE(e.frame==3);REQUIRE(e.visible);}
    int calls=full.reserveCalls;full.command.unk04=12;REQUIRE(full.graph()==0);REQUIRE(full.reserveCalls==calls);
    REQUIRE(full.index==15);REQUIRE_FALSE(full.entries[15].visible);REQUIRE(full.entries[0].visible);
}

struct AnimationFirstHost {
    using Command=AnimationSoundHost::Command;
    struct Hand {int32_t animframeinc=1;float animframeincfreal=1;bool unk0cc8_01=false,incrementalreloading=false;uint8_t animloopcount=0;} handState;
    AnimationPartHost parts;std::vector<Command> commands{{0,0,0,0}};Command *cursor=nullptr;
    bool floating=true,pal=false,noSource=false,useGraphs=false;
    float frame=0,speed=1;double from=0,to=0;int sourceIndex=0,speedCalls=0,frameCalls=0;
    bool changeSourceOnFrame=false,changeOnSetFrame=false,changeNextOnSetFrame=false;
    std::vector<float> speeds;std::vector<int64_t> events;
    Command *source(){return noSource?nullptr:commands.data()+sourceIndex;}
    ptrdiff_t cursorIndex()const{return cursor?cursor-commands.data():-1;}
    void record(int kind,int64_t a=0,int64_t b=0) {
        events.insert(events.end(),{kind,a,b,handState.animframeinc,std::bit_cast<int32_t>(handState.animframeincfreal),handState.animloopcount,
            handState.unk0cc8_01,handState.incrementalreloading,std::bit_cast<int32_t>(frame),sourceIndex,cursorIndex(),parts.count});
        for(int i=0;i<parts.count;++i)events.insert(events.end(),{parts.entries[i].number,parts.entries[i].frame,parts.entries[i].visible});
    }
    float getFrame(){float value=frame;record(1);++frameCalls;if(changeSourceOnFrame)sourceIndex=static_cast<int>(commands.size())-1;return value;}
    float getSpeed(){record(2);float value=speeds.empty()?speed:speeds[std::min(static_cast<size_t>(speedCalls),speeds.size()-1)];++speedCalls;return value;}
    void setFrame(float value){record(3,std::bit_cast<int32_t>(value));frame=value;
        if(changeOnSetFrame){handState.animloopcount=255;handState.animframeinc=7;handState.animframeincfreal=3;sourceIndex=static_cast<int>(commands.size())-1;}
        if(changeNextOnSetFrame&&commands.size()>2)commands[2].unk04=99;
    }
    void setPart(int32_t number,bool visible){record(4,number,visible);}
    bool accumulate(Command *cmd){record(5,cmd->unk04,cmd->unk02);parts.command={cmd->type,cmd->unk02,cmd->unk04};
        if(useGraphs)return parts.graph()==0;parts.native();return true;}
    template<bool Floating,bool Pal,typename Frame>bool nativeBody(){
        Hand *hand=&handState;Frame s4,s2;Command *cmd;bool done;
		s4 = getFrame();

if constexpr (Floating) {
		s2 = hand->animframeincfreal + s4;
} else {
		s2 = hand->animframeinc + s4;
}

		if (s4 == 0 && s2 > 0) {
			s4--;
		}

		if (source()) {
			done = false;
			cmd = source(); cursor = cmd;

			if (cmd) {
				parts.count = 0;

				do {
					if (cmd->type == 0) {
						done = true;
					} else if (cmd->type == 1 || cmd->type == 2) {
						if (s2 >= cmd->unk02) {
                            if (!accumulate(cmd)) {
                                
                                return false;
                            }
                        }
					} else {
						switch (cmd->type) {
						case 3:
							if (hand->unk0cc8_01) {
								if (s2 >= cmd->unk02 && s4 < cmd->unk02 && s4 < s2) {
if constexpr (Floating) {
									float tmp = cmd->unk02 - getFrame();
									tmp /= 2;

									if (hand->animframeincfreal > tmp) {
if constexpr (Pal) {
										hand->animframeinc = tmp * 0.83333333f / getSpeed();
} else {
										hand->animframeinc = tmp / getSpeed();
}
										hand->animframeincfreal = getSpeed() * (Pal ? hand->animframeinc * 1.2f : hand->animframeinc);
									}

									s2 = hand->animframeincfreal + s4;
} else {
									int32_t tmp = cmd->unk02 - (int32_t) getFrame();
									tmp /= 2;

									if (hand->animframeinc > tmp) {
										hand->animframeinc = tmp;
									}

									s2 = hand->animframeinc + s4;
}
								}
							}
							break;
						case 8:
							if (hand->incrementalreloading && s2 >= cmd->unk02 && s4 < cmd->unk02 && s4 < s2) {
 Frame sp78;
if constexpr (Floating) {
								sp78 = s2;

								while (sp78 >= cmd->unk02) {
									sp78 += cmd->unk04 - cmd->unk02;
								}

								s4 = sp78;
								hand->animframeinc = 0;
								hand->animframeincfreal = 0;
} else {
								sp78 = cmd->unk04 + (((int32_t)s2 - cmd->unk02) % ((cmd->unk02 - cmd->unk04) + 1));
								s4 = sp78;
								hand->animframeinc = 0;
}

								setFrame(sp78);
								hand->animloopcount++;
								s2 = sp78;
							}
							break;
						}
					}

					cmd++; cursor = cmd;
				} while (!done);

				if (parts.count > 0) {
					int32_t i;

					for (i = 0; i < parts.count; i++) {
						setPart(parts.entries[i].number, parts.entries[i].visible);
					}
				}
			}
		}

        from=s4;to=s2;return true;
    }
    bool native(){if(floating){if(pal)return nativeBody<true,true,float>();return nativeBody<true,false,float>();}
        return nativeBody<false,false,int32_t>();}
    static ggv_value read(void *opaque,ggv_binding binding){
        auto &h=*static_cast<AnimationFirstHost*>(opaque);ggv_value v{};
        switch(binding){
        case GGV_COMMAND_FLOAT_FRAMES:v.b=h.floating;break;
        case GGV_RELOAD_PAL:v.b=h.pal;break;
        case GGV_COMMAND_FROM_FLOAT:v.f=float(h.from);break;
        case GGV_COMMAND_TO_FLOAT:v.f=float(h.to);break;
        case GGV_COMMAND_FROM_INT:v.i=int32_t(h.from);break;
        case GGV_COMMAND_TO_INT:v.i=int32_t(h.to);break;
        case GGV_ANIM_FRAME_COMMAND:v.ref=h.source();break;
        case GGV_ANIM_WAIT_CURSOR:v.ref=h.cursor;break;
        case GGV_ANIM_WAIT_TYPE:v.i=h.cursor->type;break;
        case GGV_ANIM_WAIT_KEY:v.i=h.cursor->unk02;break;
        case GGV_ANIM_WAIT_VALUE:v.i64=h.cursor->unk04;break;
        case GGV_ANIMATION_LATCH:v.b=h.handState.unk0cc8_01;break;
        case GGV_ANIM_INCREMENTAL:v.b=h.handState.incrementalreloading;break;
        case GGV_ANIM_WAIT_INCREMENT:v.i=h.handState.animframeinc;break;
        case GGV_ANIM_WAIT_INCREMENT_REAL:v.f=h.handState.animframeincfreal;break;
        case GGV_ANIM_LOOP_COUNT:v.i=h.handState.animloopcount;break;
        case GGV_PART_COUNT:v.i=h.parts.count;break;
        case GGV_PART_NUMBER:v.i=h.parts.entries.at(h.parts.index).number;break;
        case GGV_PART_VISIBLE:v.b=h.parts.entries.at(h.parts.index).visible;break;
        default:FAIL("Unexpected animation-first read");
        }return v;
    }
    static void write(void *opaque,ggv_binding binding,ggv_value v){
        auto &h=*static_cast<AnimationFirstHost*>(opaque);
        switch(binding){
        case GGV_COMMAND_FROM_FLOAT:h.from=v.f;break;
        case GGV_COMMAND_TO_FLOAT:h.to=v.f;break;
        case GGV_COMMAND_FROM_INT:h.from=v.i;break;
        case GGV_COMMAND_TO_INT:h.to=v.i;break;
        case GGV_ANIM_WAIT_CURSOR:h.cursor=static_cast<Command*>(v.ref);break;
        case GGV_ANIM_WAIT_INCREMENT:h.handState.animframeinc=v.i;break;
        case GGV_ANIM_WAIT_INCREMENT_REAL:h.handState.animframeincfreal=v.f;break;
        case GGV_ANIM_LOOP_COUNT:h.handState.animloopcount=v.i;break;
        case GGV_PART_COUNT:h.parts.count=v.i;break;
        case GGV_PART_INDEX:h.parts.index=v.i;break;
        default:FAIL("Unexpected animation-first write");
        }
    }
    static ggv_value call(void *opaque,ggv_service service,ggv_value a,ggv_value b,ggv_value){
        auto &h=*static_cast<AnimationFirstHost*>(opaque);ggv_value v{};
        switch(service){
        case GGV_ANIM_WAIT_FRAME:v.f=h.getFrame();break;
        case GGV_MODEL_ABS_ANIM_SPEED:v.f=h.getSpeed();break;
        case GGV_ANIM_CURSOR_NEXT:v.ref=static_cast<Command*>(a.ref)+1;break;
        case GGV_COMMAND_ACCUMULATE_PART:v.b=h.accumulate(h.cursor);break;
        case GGV_COMMAND_SET_FRAME:h.setFrame(a.f);break;
        case GGV_COMMAND_SET_PART:h.setPart(a.i,b.b);break;
        default:FAIL("Unexpected animation-first service");
        }return v;
    }
    bool graph(){useGraphs=true;ggv_host host{this,read,write,call};int32_t result=123;
        return ggvExecute(ggvStockProgram(GGV_ANIMATION_FIRST_COMMANDS),&host,&result)&&result==0;}
};
void compareAnimationFirst(AnimationFirstHost input){
    auto expected=input,actual=input;expected.cursor=nullptr;actual.cursor=nullptr;expected.useGraphs=false;
    REQUIRE(expected.native());REQUIRE(actual.graph());REQUIRE(actual.events==expected.events);
    REQUIRE(std::bit_cast<uint64_t>(actual.from)==std::bit_cast<uint64_t>(expected.from));
    REQUIRE(std::bit_cast<uint64_t>(actual.to)==std::bit_cast<uint64_t>(expected.to));
    REQUIRE(actual.cursorIndex()==expected.cursorIndex());REQUIRE(actual.handState.animframeinc==expected.handState.animframeinc);
    REQUIRE(actual.handState.animloopcount==expected.handState.animloopcount);
    REQUIRE(std::bit_cast<int32_t>(actual.handState.animframeincfreal)==std::bit_cast<int32_t>(expected.handState.animframeincfreal));
    REQUIRE(actual.parts.count==expected.parts.count);
    for(int i=0;i<actual.parts.count;++i){REQUIRE(actual.parts.entries[i].number==expected.parts.entries[i].number);
        REQUIRE(actual.parts.entries[i].frame==expected.parts.entries[i].frame);REQUIRE(actual.parts.entries[i].visible==expected.parts.entries[i].visible);}
}
TEST_CASE("Animation first traversal preserves initial frames, visibility order and equal-frame ties", "[gameplay_graph][animation_first]"){
    for(bool floating:{false,true})for(float frame:{-1.5f,-0.0f,0.0f,0.5f,1.0f,8.0f})for(int inc:{-1,0,1,10}){
        AnimationFirstHost h;h.floating=floating;h.frame=frame;h.handState.animframeinc=inc;h.handState.animframeincfreal=float(inc);
        h.commands={{1,0,0,10},{1,0,1,20},{2,0,1,10},{1,0,1,10},{2,0,9,20},{0,0,0,0}};compareAnimationFirst(h);
        h.noSource=true;compareAnimationFirst(h);
    }
}
TEST_CASE("Animation first traversal preserves regional release clamps and repeated speed queries", "[gameplay_graph][animation_first]"){
    for(bool floating:{false,true})for(bool pal:{false,true})for(bool held:{false,true})for(float frame:{0.0f,1.5f,4.0f,5.0f})for(int inc:{0,1,4,10}){
        if(!floating&&pal)continue;AnimationFirstHost h;h.floating=floating;h.pal=pal;h.frame=frame;
        h.handState.animframeinc=inc;h.handState.animframeincfreal=float(inc);h.handState.unk0cc8_01=held;h.speeds={2,4};
        h.commands={{1,0,0,10},{3,0,4,0},{2,0,3,10},{1,0,5,20},{0,0,0,0}};compareAnimationFirst(h);
    }
}
TEST_CASE("Animation first traversal preserves distinct repeat formulas and callback stores", "[gameplay_graph][animation_first]"){
    for(bool floating:{false,true})for(bool enabled:{false,true})for(float frame:{0.0f,8.0f,10.0f})for(int inc:{0,1,4,10,30}){
        AnimationFirstHost h;h.floating=floating;h.frame=frame;h.handState.animframeinc=inc;h.handState.animframeincfreal=float(inc);
        h.handState.incrementalreloading=enabled;h.commands={{1,0,0,10},{8,0,10,4},{1,0,5,20},{2,0,7,10},{0,0,0,0}};
        compareAnimationFirst(h);h.changeOnSetFrame=true;h.changeNextOnSetFrame=true;compareAnimationFirst(h);
    }
    for(int target:{10,12}){AnimationFirstHost h;h.floating=false;h.frame=8;h.handState.animframeinc=10;
        h.handState.incrementalreloading=true;h.commands={{8,0,10,target},{0,0,0,0}};compareAnimationFirst(h);}
    AnimationFirstHost source;source.changeSourceOnFrame=true;source.commands={{1,0,0,10},{0,0,0,0}};compareAnimationFirst(source);
}
TEST_CASE("Animation first traversal rejects malformed arithmetic, stalled repeats and failed storage", "[gameplay_graph][animation_first]"){
    for(intptr_t target:{intptr_t{10},intptr_t{11},std::numeric_limits<intptr_t>::min()}){
        AnimationFirstHost h;h.frame=8;h.handState.animframeincfreal=10;h.handState.incrementalreloading=true;
        h.commands={{8,0,10,target},{0,0,0,0}};REQUIRE_FALSE(h.graph());REQUIRE(h.handState.animloopcount==0);
    }
    AnimationFirstHost stalled;stalled.handState.animframeincfreal=1.0e20f;stalled.handState.incrementalreloading=true;
    stalled.commands={{8,0,10,9},{0,0,0,0}};REQUIRE_FALSE(stalled.graph());
    AnimationFirstHost zero;zero.floating=false;zero.frame=8;zero.handState.animframeinc=10;zero.handState.incrementalreloading=true;
    zero.commands={{8,0,10,11},{0,0,0,0}};REQUIRE_FALSE(zero.graph());
    AnimationFirstHost storage;storage.parts.allowGrowth=false;storage.commands={{1,0,0,10},{0,0,0,0}};
    REQUIRE_FALSE(storage.graph());REQUIRE(storage.parts.count==0);
}

struct AnimationDriverHost {
    struct Hand {int32_t animmode=0,animload=-1,animframeinc=3;float gangstarot=0,animframeincfreal=3;
        bool unk0d0e_07=false,unk0cc8_02=false;unsigned unk0cc8_04:3 = 5;} handState;
    struct Command {intptr_t unk04=10000;} data;
    bool hasCommand=true,floating=true,pal=false,firstOk=true,failedFirst=false;
    int owner=0,players[2]{1,2},numFrames=20,cleanupCalls=0,scriptEvent=0,scriptOrdinal=1,calls[13]{};
    float frame=2.5f,absSpeed=0.5f,modelSpeed=0,afterTick=4.75f;double from=0,to=0,firstFrom=-0.5;
    uint32_t rng=0x80000000u;bool flipOwner=false,removeCommand=false,setLatch=false;
    int newIncrement=-1,newLoad=-2;std::vector<int64_t> events;
    Command *command(){return hasCommand?&data:nullptr;}
    void record(int kind,int64_t a=0,int64_t b=0){events.insert(events.end(),{kind,a,b,handState.animmode,handState.animload,
        handState.animframeinc,std::bit_cast<int32_t>(handState.animframeincfreal),std::bit_cast<int32_t>(handState.gangstarot),
        handState.unk0d0e_07,handState.unk0cc8_02,handState.unk0cc8_04,owner,hasCommand,data.unk04,std::bit_cast<int32_t>(frame)});}
    void mutate(int event){if(++calls[event]!=scriptOrdinal||event!=scriptEvent)return;
        if(flipOwner)owner^=1;if(removeCommand)hasCommand=false;if(setLatch)handState.unk0cc8_02=true;
        if(newIncrement>=0)handState.animframeinc=newIncrement;if(newLoad>=-1)handState.animload=newLoad;}
    float getFrame(){float out=frame;record(1);mutate(1);return out;}
    int getCount(){int out=numFrames;record(2);mutate(2);return out;}
    uint32_t random(){record(3,rng);mutate(3);return rng;}
    void setAnimation(int number,float speed){record(4,number,std::bit_cast<int32_t>(speed));modelSpeed=speed;frame=0;mutate(4);}
    void setFrame(float value){record(5,std::bit_cast<int32_t>(value));frame=value;mutate(5);}
    float getSpeed(){float out=absSpeed;record(6);mutate(6);return out;}
    bool firstPass(double *previous){record(7);*previous=firstFrom;mutate(7);failedFirst=!firstOk;return firstOk;}
    void playSpeed(float speed){record(8,std::bit_cast<int32_t>(speed));mutate(8);}
    void tick(int kind,int increment){record(kind,increment);frame=afterTick;mutate(kind);}
    void soundPass(double previous,double current){record(11,std::bit_cast<int64_t>(previous),std::bit_cast<int64_t>(current));mutate(11);}
    void cleanup(){record(12);++cleanupCalls;}
    template<bool Floating,bool Pal,typename Frame>void nativeBody(){Hand *hand=&handState;

Frame s2;
	float animspeed;
	float animspeedmult;

    double previousframe;

	hand->unk0cc8_04 = false;

	if (hand->animmode == 1 && getFrame() >= getCount() - 1) {
		hand->animmode = 0;
	}

	// This condition looks like a bug (using | instead of ||), but it happens
	// to make no difference anyway. Brackets added for clarity.
	if ((hand->animmode == (uint32_t)1) | (hand->animload >= 0)) {
		if (hand->gangstarot > 0.0f) {
			hand->animframeinc = 0;
if constexpr (Floating) {
			hand->animframeincfreal = 0.0f;
}
		}

		if (hand->animload >= 0) {
			animspeedmult = 1.0f;
			animspeed = command()->unk04 / 10000.0f;

			if (hand->unk0d0e_07 && players[owner]) {
				animspeedmult = (random() * (1.0f / UINT32_MAX)) * 0.77f + 0.7f;
			}

			if (command() && animspeed < 0.0f) {
				setAnimation(hand->animload, animspeedmult * animspeed);
				setFrame(getCount());
			} else {
				setAnimation(hand->animload, animspeedmult * animspeed);
			}

			hand->animload = -1;
			hand->animmode = 1;
if constexpr (Floating) {
			hand->animframeincfreal = getSpeed() * (Pal ? hand->animframeinc * 1.2f : hand->animframeinc);
}
		}

		if (hand->unk0cc8_02) {
			hand->animframeinc = 0;
if constexpr (Floating) {
			hand->animframeincfreal = 0.0f;
}
		}

        if (!firstPass(&previousframe)) {
            cleanup();
            return;
        }

if constexpr (Floating) {
		playSpeed(Pal ? 4.0f * 1.2f : 4.0f);
		tick(9,hand->animframeinc);
} else {
		tick(10,hand->animframeinc);
}

		s2 = getFrame();

		soundPass(previousframe,s2);
	}
    cleanup();

    }
    void native(){if(floating){if(pal)nativeBody<true,true,float>();else nativeBody<true,false,float>();}
        else nativeBody<false,false,int32_t>();}
    static ggv_value read(void *opaque,ggv_binding binding){auto &h=*static_cast<AnimationDriverHost*>(opaque);ggv_value v{};
        switch(binding){
        case GGV_EMPTY_ANIM_MODE:v.i=h.handState.animmode;break;
        case GGV_ANIM_LOAD:v.i=h.handState.animload;break;
        case GGV_ANIM_GANGSTA:v.f=h.handState.gangstarot;break;
        case GGV_COMMAND_FLOAT_FRAMES:v.b=h.floating;break;
        case GGV_RELOAD_PAL:v.b=h.pal;break;
        case GGV_ANIM_FRAME_COMMAND_VALUE:v.i64=h.command()->unk04;break;
        case GGV_ANIM_FRAME_COMMAND:v.ref=h.command();break;
        case GGV_ANIM_BIT07:v.b=h.handState.unk0d0e_07;break;
        case GGV_ANIM_ELIGIBILITY_LEFT_INUSE:v.i=h.players[h.owner];break;
        case GGV_ANIM_WAIT_INCREMENT:v.i=h.handState.animframeinc;break;
        case GGV_ANIM_BIT02:v.b=h.handState.unk0cc8_02;break;
        default:FAIL("Unexpected animation-driver read");
        }return v;}
    static void write(void *opaque,ggv_binding binding,ggv_value v){auto &h=*static_cast<AnimationDriverHost*>(opaque);
        switch(binding){
        case GGV_COMMAND_OUTPUT:h.handState.unk0cc8_04=v.u;break;
        case GGV_EMPTY_ANIM_MODE:h.handState.animmode=v.i;break;
        case GGV_ANIM_LOAD:h.handState.animload=v.i;break;
        case GGV_ANIM_WAIT_INCREMENT:h.handState.animframeinc=v.i;break;
        case GGV_ANIM_WAIT_INCREMENT_REAL:h.handState.animframeincfreal=v.f;break;
        case GGV_COMMAND_TO_FLOAT:h.to=v.f;break;
        case GGV_COMMAND_TO_INT:h.to=v.i;break;
        default:FAIL("Unexpected animation-driver write");
        }}
    static ggv_value call(void *opaque,ggv_service service,ggv_value a,ggv_value b,ggv_value){auto &h=*static_cast<AnimationDriverHost*>(opaque);ggv_value v{};
        switch(service){
        case GGV_ANIM_WAIT_FRAME:v.f=h.getFrame();break;
        case GGV_MODEL_ANIM_FRAME_COUNT:v.i=h.getCount();break;
        case GGV_MODEL_ABS_ANIM_SPEED:v.f=h.getSpeed();break;
        case GGV_RNG_U32:v.u=h.random();break;
        case GGV_COMMAND_MODEL_ANIMATION:h.setAnimation(a.i,b.f);break;
        case GGV_COMMAND_SET_FRAME:h.setFrame(a.f);break;
        case GGV_COMMAND_FIRST_PASS:v.b=h.firstPass(&h.from);break;
        case GGV_COMMAND_PLAY_SPEED:h.playSpeed(a.f);break;
        case GGV_COMMAND_TICK_QUARTER:h.tick(9,a.i);break;
        case GGV_COMMAND_TICK_LEGACY:h.tick(10,a.i);break;
        case GGV_COMMAND_SOUND_PASS:h.soundPass(h.from,h.to);break;
        default:FAIL("Unexpected animation-driver service");
        }return v;}
    int graph(){ggv_host host{this,read,write,call};int32_t result=123;
        REQUIRE(ggvExecute(ggvStockProgram(GGV_ANIMATION_DRIVER),&host,&result));cleanup();return result;}
};
void compareAnimationDriver(AnimationDriverHost input){auto expected=input,actual=input;expected.native();
    REQUIRE(actual.graph()==(expected.failedFirst?-1:0));REQUIRE(actual.events==expected.events);
    REQUIRE(actual.cleanupCalls==1);REQUIRE(expected.cleanupCalls==1);
    REQUIRE(actual.handState.animmode==expected.handState.animmode);REQUIRE(actual.handState.animload==expected.handState.animload);
    REQUIRE(actual.handState.animframeinc==expected.handState.animframeinc);
    REQUIRE(std::bit_cast<int32_t>(actual.handState.animframeincfreal)==std::bit_cast<int32_t>(expected.handState.animframeincfreal));
    REQUIRE(std::bit_cast<int32_t>(actual.modelSpeed)==std::bit_cast<int32_t>(expected.modelSpeed));
}
TEST_CASE("Animation driver preserves admission, busy completion and regional increment gates", "[gameplay_graph][animation_driver]"){
    for(bool floating:{false,true})for(bool pal:{false,true})for(int mode:{0,1,2})for(int load:{-1,0,7})for(float frame:{0.0f,18.5f,19.0f,20.0f}){
        if(!floating&&pal)continue;AnimationDriverHost h;h.floating=floating;h.pal=pal;h.handState.animmode=mode;h.handState.animload=load;h.frame=frame;
        compareAnimationDriver(h);h.handState.gangstarot=0.5f;compareAnimationDriver(h);h.handState.unk0cc8_02=true;compareAnimationDriver(h);
    }
}
TEST_CASE("Animation driver preserves full-width speed, reverse setup and lazy RNG", "[gameplay_graph][animation_driver]"){
    for(int64_t speed:{int64_t{-10000},int64_t{0},int64_t{10000},int64_t{4294967297},std::numeric_limits<int64_t>::min(),std::numeric_limits<int64_t>::max()})
    for(bool variation:{false,true})for(int left:{0,1,2})for(uint32_t rng:{0u,1u,0x80000000u,UINT32_MAX}){
        AnimationDriverHost h;h.handState.animload=7;h.data.unk04=static_cast<intptr_t>(speed);h.handState.unk0d0e_07=variation;h.players[0]=left;h.rng=rng;compareAnimationDriver(h);
    }
}
TEST_CASE("Animation driver preserves callback rereads, typed post-tick bounds and child failure cleanup", "[gameplay_graph][animation_driver]"){
    for(bool floating:{false,true})for(int event:{3,4,5,6,7,8,9,10})for(bool fail:{false,true}){
        AnimationDriverHost h;h.floating=floating;h.handState.animload=7;h.data.unk04=-10000;h.handState.unk0d0e_07=true;
        h.scriptEvent=event;h.flipOwner=true;h.removeCommand=true;h.newIncrement=9;h.newLoad=17;h.firstOk=!fail;compareAnimationDriver(h);
        h.setLatch=true;compareAnimationDriver(h);
    }
    for(float frame:{-1.5f,0.0f,1.75f,16777216.0f}){AnimationDriverHost h;h.handState.animmode=1;h.afterTick=frame;
        compareAnimationDriver(h);h.floating=false;compareAnimationDriver(h);}
    AnimationDriverHost h;h.hasCommand=false;h.handState.animload=-1;compareAnimationDriver(h);
}

TEST_CASE("Edited public repeat source changes production graph frame results", "[gameplay_graph][animation_first][weapon-command-source]") {
    for(bool floating:{false,true})for(int target:{4,2}) {
        const std::string text=std::string(R"({"source_format":"weapon_animation_commands","name":"Repeat","command_count":2,"commands":[{"command":"repeat_until_full","slot":10,"dont_loop":0,"goto_trigger":)")
            +std::to_string(target)+R"(},{"command":"end"}]})";
        char error[256]{};
        std::unique_ptr<weapon_command_source,decltype(&weaponCommandSourceFree)> source(
            weaponCommandSourceRead("testmod:repeat",text.data(),text.size(),error,sizeof(error)),weaponCommandSourceFree);
        INFO(error);REQUIRE(source);
        AnimationFirstHost h;h.floating=floating;h.frame=8;h.handState.animframeinc=10;
        h.handState.animframeincfreal=10;h.handState.incrementalreloading=true;h.commands.clear();
        for(size_t i=0;i<weaponCommandSourceCount(source.get());++i) {
            weapon_command_source_item item{};REQUIRE(weaponCommandSourceItem(source.get(),i,&item));
            REQUIRE(item.reference_kind==WEAPON_COMMAND_NO_REFERENCE);
            h.commands.push_back({item.type,item.selector,item.trigger,item.value});
        }
        REQUIRE(h.graph());REQUIRE(h.handState.animloopcount==1);
        const float expected=floating?(target==4?6.0f:2.0f):(target==4?5.0f:10.0f);
        REQUIRE(h.frame==expected);REQUIRE(h.from==expected);REQUIRE(h.to==expected);
    }
}

struct VisibilityCommandHost {
    struct Command {uint8_t type,op;uint16_t part;int condition;};
    std::vector<Command> commands{{4,0,10,1},{4,3,20,0},{0,0,0,0}};
    Command *cursor=nullptr,*active=nullptr;bool nullSource=false,cascade=false,changeOnTest=false,endNext=false;
    std::vector<int64_t> events;
    int test(Command *cmd){active=cmd;events.insert(events.end(),{1,cmd-commands.data(),cmd->op,cmd->part,cmd->condition});
        if(changeOnTest){cmd->op=3;cmd->part=65535;}return cmd->condition;}
    void set(int16_t part,int visible){events.insert(events.end(),{2,part,visible});
        if(cascade){if(active->op==0){active->op=1;++active->part;}else if(active->op==1){active->op=3;++active->part;}}
        if(endNext && active+1<commands.data()+commands.size())(active+1)->type=0;
    }
    void native(){auto *cmd=nullSource?nullptr:commands.data();cursor=cmd;if(cmd==nullptr)return;
        while(cmd->type!=0){
            if(test(cmd)){
                if(cmd->op==0)set(cmd->part,true);
                if(cmd->op==1)set(cmd->part,false);
                if(cmd->op==3)set(cmd->part,true);
            }else{if(cmd->op==3)set(cmd->part,false);}
            ++cmd;cursor=cmd;
        }
    }
    static ggv_value read(void *p,ggv_binding binding){auto &h=*static_cast<VisibilityCommandHost*>(p);ggv_value v{};
        switch(binding){case GGV_VIS_CURSOR:v.ref=h.cursor;break;case GGV_VIS_COMMAND_TYPE:v.i=h.cursor->type;break;
        case GGV_VIS_OPERATION:v.i=h.cursor->op;break;case GGV_VIS_COMMAND_PART:v.i=h.cursor->part;break;
        default:FAIL("Unexpected visibility-command read");}return v;}
    static void write(void *p,ggv_binding binding,ggv_value v){auto &h=*static_cast<VisibilityCommandHost*>(p);
        REQUIRE(binding==GGV_VIS_CURSOR);h.cursor=static_cast<Command*>(v.ref);}
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value b,ggv_value){auto &h=*static_cast<VisibilityCommandHost*>(p);ggv_value v{};
        switch(service){case GGV_VIS_TEST:v.b=h.test(static_cast<Command*>(a.ref));break;
        case GGV_VIS_NEXT:v.ref=static_cast<Command*>(a.ref)+1;break;
        case GGV_COMMAND_SET_PART:h.set(static_cast<int16_t>(a.i),b.b);break;
        default:FAIL("Unexpected visibility-command service");}return v;}
    bool graph(){cursor=nullSource?nullptr:commands.data();ggv_host host{this,read,write,call};int32_t result=123;
        return ggvExecute(ggvStockProgram(GGV_VISIBILITY_COMMANDS),&host,&result)&&result==0;}
};
void compareVisibilityCommands(VisibilityCommandHost input){auto expected=input,actual=input;expected.native();REQUIRE(actual.graph());
    REQUIRE(actual.events==expected.events);
    REQUIRE((actual.cursor?actual.cursor-actual.commands.data():-1)==(expected.cursor?expected.cursor-expected.commands.data():-1));
    for(size_t i=0;i<actual.commands.size();++i){REQUIRE(actual.commands[i].type==expected.commands[i].type);
        REQUIRE(actual.commands[i].op==expected.commands[i].op);REQUIRE(actual.commands[i].part==expected.commands[i].part);}
}
TEST_CASE("Visibility command graph preserves null, terminator and conditional operations", "[gameplay_graph][visibility]") {
    VisibilityCommandHost absent;absent.nullSource=true;compareVisibilityCommands(absent);
    VisibilityCommandHost empty;empty.commands={{0,0,0,0}};compareVisibilityCommands(empty);
    for(int condition:{0,1,2})for(uint8_t op:{uint8_t{0},uint8_t{1},uint8_t{2},uint8_t{3},uint8_t{255}}){
        VisibilityCommandHost h;h.commands={{4,op,65535,condition},{0,0,0,0}};compareVisibilityCommands(h);}
}
TEST_CASE("Visibility command graph re-reads fields after predicate and part callbacks", "[gameplay_graph][visibility]") {
    VisibilityCommandHost cascade;cascade.cascade=true;compareVisibilityCommands(cascade);
    VisibilityCommandHost predicate;predicate.changeOnTest=true;compareVisibilityCommands(predicate);
    VisibilityCommandHost stop;stop.endNext=true;compareVisibilityCommands(stop);
}

struct PartVisibilityHost {
    struct Node{uint16_t index;};struct Model{int id;bool missing=false;Node node;};
    Model models[2]{{0,false,{2}},{1,false,{5}}};int handModelIndex=1,handReads=0;
    bool nullHand=false,switchHandOnRead=false;int16_t part=0x35;int visible=1;
    std::array<uint32_t,8> handData{10,11,12,13,14,15,16,17},gunData{20,21,22,23,24,25,26,27};
    std::vector<int64_t> events;
    Model *handModel(){++handReads;auto *result=nullHand?nullptr:&models[handModelIndex];
        if(switchHandOnRead && handReads==1)handModelIndex=0;return result;}
    Node *lookup(Model *model,int partnum){events.insert(events.end(),{1,model->id,partnum});return model->missing?nullptr:&model->node;}
    void save(bool hand,Node *node,int value){events.insert(events.end(),{2,hand,node->index,value});
        (hand?handData:gunData)[node->index]=static_cast<uint32_t>(value);}
    void native(){Node *node;
        if(part==0x35||part==0x36){if(handModel()){node=lookup(handModel(),part);if(node)save(true,node,visible);}}
        else{node=lookup(&models[0],part);if(node)save(false,node,visible);}
    }
    static ggv_value read(void *p,ggv_binding binding){auto &h=*static_cast<PartVisibilityHost*>(p);ggv_value v{};
        switch(binding){case GGV_VIS_PART:v.i=h.part;break;case GGV_VIS_MODELDEF:v.ref=&h.models[0];break;
        case GGV_VIS_HAND_MODEL:v.ref=h.handModel();break;case GGV_VIS_VISIBLE:v.b=h.visible;break;
        default:FAIL("Unexpected part-visibility read");}return v;}
    static void write(void*,ggv_binding,ggv_value){FAIL("Unexpected part-visibility write");}
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value b,ggv_value){auto &h=*static_cast<PartVisibilityHost*>(p);ggv_value v{};
        switch(service){case GGV_VIS_MODEL_PART:v.ref=h.lookup(static_cast<Model*>(a.ref),b.i);break;
        case GGV_VIS_SAVE_HAND:h.save(true,static_cast<Node*>(a.ref),b.b);break;
        case GGV_VIS_SAVE_GUN:h.save(false,static_cast<Node*>(a.ref),b.b);break;
        default:FAIL("Unexpected part-visibility service");}return v;}
    bool graph(){ggv_host host{this,read,write,call};int32_t result=123;
        return ggvExecute(ggvStockProgram(GGV_PART_VISIBILITY),&host,&result)&&result==0;}
};
void comparePartVisibility(PartVisibilityHost input){auto expected=input,actual=input;expected.native();REQUIRE(actual.graph());
    REQUIRE(actual.events==expected.events);REQUIRE(actual.handData==expected.handData);REQUIRE(actual.gunData==expected.gunData);
    REQUIRE(actual.handReads==expected.handReads);REQUIRE(actual.handModelIndex==expected.handModelIndex);
}
TEST_CASE("Part visibility graph preserves storage bank and raw legacy boolean values", "[gameplay_graph][visibility]") {
    for(int16_t part:{int16_t{0x35},int16_t{0x36},int16_t{0x42},int16_t{-1}})
        for(int value:{0,1,2,-1})for(bool absent:{false,true})for(bool missing:{false,true}){
            PartVisibilityHost h;h.part=part;h.visible=value;h.nullHand=absent;h.models[0].missing=missing;h.models[1].missing=missing;
            comparePartVisibility(h);}
}
TEST_CASE("Part visibility graph obtains the hand model again after its existence query", "[gameplay_graph][visibility]") {
    PartVisibilityHost h;h.switchHandOnRead=true;h.visible=2;comparePartVisibility(h);
    h.models[0].missing=true;comparePartVisibility(h);
}

struct AmmoVisibilityHost {
    struct Ammo{bool present=true;uint32_t flags=8;};
    struct Definition{int command;Ammo ammo[2];};
    Definition definitions[2]{{17,{{true,8},{true,8}}},{23,{{true,0},{false,0}}}};
    Definition *captured=nullptr;int currentWeapon=0,slot=0;
    int32_t clips[2]{3,2},loaded[2]{1,2};bool mutateCommands=false,mutateParts=false,mutateLookup=false;
    std::vector<int64_t> events;
    Definition *find(int weapon){events.insert(events.end(),{1,weapon});auto *result=&definitions[weapon];
        if(mutateLookup)currentWeapon=1;return result;}
    void runCommands(void *commands){events.insert(events.end(),{2,*static_cast<int*>(commands)});
        if(mutateCommands){currentWeapon=1;definitions[0].ammo[1].present=true;definitions[0].ammo[1].flags=8;loaded[0]=2;}}
    void set(int16_t part,int visible){events.insert(events.end(),{3,part,visible});
        if(mutateParts){if(part==0x42){clips[0]=4;loaded[0]=2;currentWeapon=1;}
            else if(part==100){clips[0]=2;loaded[0]=0;clips[1]=1;loaded[1]=-1;}}
    }
    void native(){Definition *weapon=find(currentWeapon);
        runCommands(&weapon->command);set(0x42,false);
        for(int i=0;i<2;++i){if(weapon->ammo[i].present && (weapon->ammo[i].flags&8)){
            for(int j=0;j<clips[i];++j){if(j>=loaded[i])set(j+100,false);else set(j+100,true);}
        }}
    }
    static ggv_value read(void *p,ggv_binding binding){auto &h=*static_cast<AmmoVisibilityHost*>(p);ggv_value v{};
        switch(binding){case GGV_VIS_WEAPON_ID:v.i=h.currentWeapon;break;case GGV_VIS_COMMANDS:v.ref=&h.captured->command;break;
        case GGV_VIS_AMMO:v.ref=h.captured->ammo[h.slot].present?&h.captured->ammo[h.slot]:nullptr;break;
        case GGV_VIS_AMMO_FLAGS:v.u=h.captured->ammo[h.slot].flags;break;
        case GGV_VIS_CLIP_SIZE:v.i=h.clips[h.slot];break;case GGV_VIS_LOADED:v.i=h.loaded[h.slot];break;
        default:FAIL("Unexpected ammo-visibility read");}return v;}
    static void write(void *p,ggv_binding binding,ggv_value v){auto &h=*static_cast<AmmoVisibilityHost*>(p);
        switch(binding){case GGV_VIS_WEAPON:h.captured=static_cast<Definition*>(v.ref);break;case GGV_VIS_AMMO_SLOT:h.slot=v.i;break;
        default:FAIL("Unexpected ammo-visibility write");}}
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value b,ggv_value){auto &h=*static_cast<AmmoVisibilityHost*>(p);ggv_value v{};
        switch(service){case GGV_QUERY_FIND_WEAPON:v.ref=h.find(a.i);break;case GGV_VIS_RUN_COMMANDS:h.runCommands(a.ref);break;
        case GGV_COMMAND_SET_PART:h.set(static_cast<int16_t>(a.i),b.b);break;
        default:FAIL("Unexpected ammo-visibility service");}return v;}
    bool graph(){ggv_host host{this,read,write,call};int32_t result=123;
        return ggvExecute(ggvStockProgram(GGV_AMMO_VISIBILITY),&host,&result)&&result==0;}
};
void compareAmmoVisibility(AmmoVisibilityHost input){auto expected=input,actual=input;expected.native();REQUIRE(actual.graph());
    REQUIRE(actual.events==expected.events);REQUIRE(actual.currentWeapon==expected.currentWeapon);
    for(int i=0;i<2;++i){REQUIRE(actual.clips[i]==expected.clips[i]);REQUIRE(actual.loaded[i]==expected.loaded[i]);
        REQUIRE(actual.definitions[0].ammo[i].present==expected.definitions[0].ammo[i].present);
        REQUIRE(actual.definitions[0].ammo[i].flags==expected.definitions[0].ammo[i].flags);}
}
TEST_CASE("Ammo visibility graph preserves both slots, flags and signed clip boundaries", "[gameplay_graph][visibility]") {
    for(bool present:{false,true})for(uint32_t flags:{0u,1u,8u,9u})for(int32_t clip:std::array<int32_t,3>{-1,0,3})
        for(int32_t loaded:std::array<int32_t,6>{INT32_MIN,-1,0,2,5,INT32_MAX}){AmmoVisibilityHost h;
            h.definitions[0].ammo[0]={present,flags};h.clips[0]=clip;h.loaded[0]=loaded;compareAmmoVisibility(h);}
    AmmoVisibilityHost high;high.clips[0]=40000;high.loaded[0]=40000;compareAmmoVisibility(high);
}
TEST_CASE("Ammo visibility graph retains its weapon and re-reads callback-mutated hand state", "[gameplay_graph][visibility]") {
    AmmoVisibilityHost lookup;lookup.mutateLookup=true;compareAmmoVisibility(lookup);
    AmmoVisibilityHost commands;commands.definitions[0].ammo[1].present=false;commands.mutateCommands=true;compareAmmoVisibility(commands);
    AmmoVisibilityHost parts;parts.mutateParts=true;compareAmmoVisibility(parts);
}

struct SwitchHandResetHost {
    struct Hand {
        int ejectstate=3,ejecttype=2,burst=7,allow=19,last=23;
        bool bit=true,secondary=true;
        float mat1=1.25f,mat2=2.25f,mat3=3.25f,angle=4.25f,smoke=5.25f,slide=6.25f,gangsta=7.25f;
        uint8_t function=1,weapon=37,upgrades=0x76;int audio=0,animCount=0;
    } hands[2][2];
    struct Control{int8_t weapon=17;uint8_t upgrades=0xab;} controls[2];
    struct Sound{int id,state;} sounds[3]{{0,1},{1,2},{2,3}};
    int owner=0,ctrlOwner=0,index=0,current=0;
    bool rewriteOnConfigure=false,switchOnConfigure=false,newHandleOnInit=false,swapOnState=false;
    std::vector<int64_t> events;
    Hand &hand(){return hands[owner][index];}
    void record(int event,int argument=0){events.insert(events.end(),{event,argument,current});
        for(auto &player:hands)for(auto &h:player){events.insert(events.end(),{h.ejectstate,h.ejecttype,h.bit,h.secondary,
            std::bit_cast<int32_t>(h.mat1),std::bit_cast<int32_t>(h.mat2),std::bit_cast<int32_t>(h.mat3),
            std::bit_cast<int32_t>(h.angle),std::bit_cast<int32_t>(h.smoke),h.burst,std::bit_cast<int32_t>(h.slide),
            h.allow,h.last,h.function,h.weapon,h.upgrades,std::bit_cast<int32_t>(h.gangsta),h.audio,h.animCount});}
        for(auto &c:controls)events.insert(events.end(),{c.weapon,c.upgrades});
        for(auto &s:sounds)events.push_back(s.state);
    }
    void configure(int i){record(1,i);if(rewriteOnConfigure){hands[current][i].mat1=99;hands[current][i].weapon=91;controls[current].weapon=42;}
        if(switchOnConfigure)current=1-current;}
    void init(){record(2,owner*2+index);++hand().animCount;if(newHandleOnInit)hand().audio=1;}
    int state(int id){record(3,id);int result=sounds[id].state;if(swapOnState)hand().audio=2;return result;}
    void stop(int id){record(4,id);sounds[id].state=0;}
    void native(){auto &h=hand();auto &ctrl=controls[ctrlOwner];
        h.ejectstate=0;h.ejecttype=0;h.bit=false;h.secondary=false;
        h.mat1=0.0f;h.mat2=0.0f;h.mat3=0.0f;h.angle=0.0f;h.smoke=0.0f;
        h.burst=0;h.slide=0.0f;h.allow=0;h.last=0;h.function=0;
        h.weapon=ctrl.weapon;h.upgrades=(ctrl.upgrades>>(index*4))&0xf;h.gangsta=0.0f;
        configure(index);init();if(h.audio>=0 && state(h.audio)!=0)stop(h.audio);
    }
    static ggv_value read(void *p,ggv_binding binding){auto &h=*static_cast<SwitchHandResetHost*>(p);ggv_value v{};
        switch(binding){case GGV_SWITCH_HAND_INDEX:v.i=h.index;break;
        case GGV_SWITCH_RESET_WEAPON:v.i=h.controls[h.ctrlOwner].weapon;break;
        case GGV_SWITCH_UPGRADES:v.u=h.controls[h.ctrlOwner].upgrades;break;
        case GGV_SWITCH_AUDIO_HANDLE:v.ref=h.hand().audio<0?nullptr:&h.sounds[h.hand().audio];break;
        default:FAIL("Unexpected switch reset read");}return v;}
    static void write(void *p,ggv_binding binding,ggv_value v){auto &h=static_cast<SwitchHandResetHost*>(p)->hand();
        switch(binding){case GGV_RELOAD_EJECT_STATE:h.ejectstate=v.i;break;case GGV_CHANGE_EJECT_TYPE:h.ejecttype=v.i;break;
        case GGV_SWITCH_RESET_BIT:h.bit=v.b;break;case GGV_HAND_ACTIVATE_SECONDARY:h.secondary=v.b;break;
        case GGV_MATMOT1:h.mat1=v.f;break;case GGV_MATMOT2:h.mat2=v.f;break;case GGV_SWITCH_MATMOT3:h.mat3=v.f;break;
        case GGV_SWITCH_ANGLE_DAMPER:h.angle=v.f;break;case GGV_SWITCH_SMOKE_POINT:h.smoke=v.f;break;
        case GGV_BURST_BULLETS:h.burst=v.i;break;case GGV_LOAD_SLIDE:h.slide=v.f;break;
        case GGV_ALLOW_SHOOT_FRAME:h.allow=v.i;break;case GGV_LAST_SHOOT_FRAME:h.last=v.i;break;
        case GGV_WEAPON_FUNCTION:h.function=v.i;break;case GGV_SWITCH_HAND_WEAPON:h.weapon=v.i;break;
        case GGV_SWITCH_HAND_UPGRADES:h.upgrades=v.u;break;case GGV_ANIM_GANGSTA:h.gangsta=v.f;break;
        default:FAIL("Unexpected switch reset write");}}
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value,ggv_value){auto &h=*static_cast<SwitchHandResetHost*>(p);ggv_value v{};
        switch(service){case GGV_SWITCH_CONFIGURE_HAND:h.configure(a.i);break;case GGV_SWITCH_INIT_ANIM:h.init();break;
        case GGV_SOUND_STATE:v.i=h.state(static_cast<Sound*>(a.ref)->id);break;
        case GGV_SOUND_STOP:h.stop(static_cast<Sound*>(a.ref)->id);break;
        default:FAIL("Unexpected switch reset service");}return v;}
    bool graph(){ggv_host host{this,read,write,call};int32_t result=123;
        return ggvExecute(ggvStockProgram(GGV_SWITCH_HAND_RESET),&host,&result)&&result==0;}
};
void compareSwitchHandReset(SwitchHandResetHost input){auto expected=input,actual=input;expected.native();REQUIRE(actual.graph());
    expected.record(9);actual.record(9);REQUIRE(actual.events==expected.events);
}
TEST_CASE("Switch hand reset preserves all fields, upgrade nibbles and weapon byte conversion", "[gameplay_graph][switch_reset]") {
    for(int index:{0,1})for(int weapon:{-128,-1,0,127})for(int upgrades=0;upgrades<256;++upgrades){
        SwitchHandResetHost h;h.index=index;h.controls[0].weapon=static_cast<int8_t>(weapon);
        h.controls[0].upgrades=static_cast<uint8_t>(upgrades);compareSwitchHandReset(h);}
}
TEST_CASE("Switch hand reset preserves the captured owner across fresh-global child callbacks", "[gameplay_graph][switch_reset]") {
    for(int owner:{0,1})for(int current:{0,1})for(int ctrl:{0,1})for(int index:{0,1}){
        SwitchHandResetHost h;h.owner=owner;h.current=current;h.ctrlOwner=ctrl;h.index=index;
        h.controls[1].weapon=-1;h.controls[1].upgrades=0x7c;h.rewriteOnConfigure=true;h.switchOnConfigure=true;
        h.newHandleOnInit=true;compareSwitchHandReset(h);}
}
TEST_CASE("Switch hand reset re-reads audio after initialization and state callbacks", "[gameplay_graph][switch_reset]") {
    for(int audio:{-1,0})for(int state:{0,1,2})for(bool init:{false,true})for(bool swap:{false,true}){
        SwitchHandResetHost h;h.hand().audio=audio;h.sounds[0].state=state;h.sounds[1].state=state;
        h.newHandleOnInit=init;h.swapOnState=swap;compareSwitchHandReset(h);}
}

// Native switch donor with the previously isolated hand reset represented as
// a scripted child boundary. Its internal behavior has separate donor cases.
struct SwitchParentHost {
    enum {HAND_RIGHT=0,HAND_LEFT=1,WEAPON_NONE=0,WEAPON_UNARMED=1,WEAPON_RCP120=13,
        WEAPON_REMOTEMINE=34,WEAPON_PSYCHOSISGUN=36,WEAPON_HORIZONSCANNER=42,
        WEAPON_DISGUISE40=56,WEAPON_DISGUISE41=57,DEVICESTATE_UNEQUIPPED=-1,STAGE_RESCUE=53};
    static constexpr uint32_t CHRHFLAG_DISGUISED=0x80000,CHRHFLAG_UNTARGETABLE=0x1000;
    struct Hand{int8_t inuse=1;int32_t state=2,stateminor=3,count=4;float matmot1=2.75f;};
    struct Control{int8_t weaponnum=5,switchtoweaponnum=10,prevweaponnum=3;uint8_t dualwielding:1=0,prevwasdualwielding:1=0,throwing:1=1,invertgunfunc:1=1;
        int8_t ammotypes[2]{0,1};uint8_t fnfader=7,fnstrtimer=8;uint16_t curfnstr=9;};
    struct Chr{uint32_t hidden=0xa0000000;};struct Prop{Chr *chr=nullptr;};
    struct Player{Hand hands[2];Control gunctrl;int32_t ammoheldarr[3]{10,20,30},insightaimmode=1;int16_t usedowntime=8;uint8_t disguised:1=0;
        float zoomintimemax=9,zoomintime=8,zoominfovynew=7,zoominfovy=6;Prop *prop=nullptr;int resets[2]{};} players[2];
    Chr characters[3];Prop props[2];int propChr[2]{0,1};
    struct Vars{Player *currentplayer=nullptr;int32_t currentplayernum=0,lvframe60=100,lvframenum=0,stagenum=0;} vars;
    Player *player=nullptr;Control *ctrl=nullptr;Chr *chr=nullptr;int32_t *reserve=nullptr;
    int currentIndex=0,owner=0,diagRight=0,diagLeft=0,mode=0,readyIndex=0,device=-1,hasDouble=1;
    std::vector<int> readiness{1};std::vector<int64_t> events;
    void prepare(){for(int i=0;i<2;++i){players[i].prop=&props[i];props[i].chr=&characters[propChr[i]];}
        vars.currentplayer=&players[currentIndex];player=vars.currentplayer;ctrl=&player->gunctrl;owner=currentIndex;chr=nullptr;reserve=nullptr;}
    void record(int event,int a=0,int b=0){events.insert(events.end(),{event,a,b,vars.currentplayer-players,vars.currentplayernum,vars.lvframe60,vars.lvframenum,vars.stagenum,readyIndex});
        for(auto &p:players){auto &c=p.gunctrl;events.insert(events.end(),{c.weaponnum,c.switchtoweaponnum,c.prevweaponnum,c.dualwielding,c.prevwasdualwielding,c.throwing,
            c.ammotypes[0],c.ammotypes[1],c.invertgunfunc,c.fnfader,c.fnstrtimer,c.curfnstr,p.ammoheldarr[0],p.ammoheldarr[1],p.ammoheldarr[2],p.insightaimmode,p.usedowntime,p.disguised,
            std::bit_cast<int32_t>(p.zoomintimemax),std::bit_cast<int32_t>(p.zoomintime),std::bit_cast<int32_t>(p.zoominfovynew),std::bit_cast<int32_t>(p.zoominfovy),p.prop->chr-characters,p.resets[0],p.resets[1]});
            for(auto &h:p.hands)events.insert(events.end(),{h.inuse,h.state,h.stateminor,h.count,std::bit_cast<int32_t>(h.matmot1)});}
        for(auto &c:characters)events.push_back(c.hidden);
    }
    void currentOther(){vars.currentplayer=&players[1-owner];vars.currentplayernum=1-owner;}
    void mutate(int event,int argument=0){
        if(mode==1 && event==1 && readyIndex==1)currentOther();
        if(mode==2 && event==5 && argument==HAND_LEFT)currentOther();
        if(mode==3 && event==6)players[owner].gunctrl.switchtoweaponnum=WEAPON_REMOTEMINE;
        if(mode==4 && event==9){currentOther();props[owner].chr=&characters[2];vars.stagenum=STAGE_RESCUE;}
        if(mode==5 && event==12){players[owner].gunctrl.switchtoweaponnum=-1;players[owner].hands[HAND_LEFT].inuse=0;players[owner].gunctrl.dualwielding=1;}
        if(mode==6 && event==2){players[owner].gunctrl.switchtoweaponnum=WEAPON_REMOTEMINE;players[owner].gunctrl.dualwielding=1;}
        if(mode==7 && event==5)players[owner].gunctrl.dualwielding=0;
        if(mode==8 && event==8)players[owner].gunctrl.weaponnum=WEAPON_DISGUISE40;
        if(mode==9 && event==11)players[owner].gunctrl.switchtoweaponnum=4;
    }
    int bgunCanFreeWeapon(int hand){int value=readiness[std::min(static_cast<size_t>(readyIndex),readiness.size()-1)];++readyIndex;record(1,hand,value);mutate(1,hand);return value;}
    int currentPlayerGetDeviceState(int weapon){record(2,weapon);mutate(2,weapon);return device;}
    int invHasDoubleWeaponIncAllGuns(int a,int b){record(3,a,b);mutate(3);return hasDouble;}
    void func0f0d7364(){record(4);mutate(4);}
    void bgunFreeWeapon(int hand){record(5,hand);mutate(5,hand);}
    void bgunSetGunMemWeapon(int weapon){record(6,weapon);mutate(6,weapon);}
    void bgunResetSwitchHand(Player *p,Control *c,int hand){record(7,hand,static_cast<int>(p-players));REQUIRE(c==&p->gunctrl);++p->resets[hand];p->hands[hand].matmot1=0;mutate(7,hand);}
    void invCalculateCurrentIndex(){record(8);mutate(8);}
    void disguiseSound(){record(9);mutate(9);}
    void invRemoveItemByNum(int weapon){record(10,weapon);mutate(10,weapon);}
    void bgunCycleBack(){record(11);mutate(11);}
    void log(Player *p,Control *c,int right,int left){REQUIRE(p==player);REQUIRE(c==ctrl);record(12,right,left);mutate(12);}
    void native(){prepare();

	Player *player = vars.currentplayer;
	Control *ctrl = &vars.currentplayer->gunctrl;
	int32_t i;

	/* B-246 round-2 instrumentation: log per-hand state when there's a queued
	 * switch but the dual-hand bgunCanFreeWeapon gate hasn't passed. Tells us
	 * which hand is wedged and at what stateminor / count. Player 0 only,
	 * first 60 ticks + every ~120 ticks thereafter. */
	if (vars.currentplayernum == 0 && ctrl->switchtoweaponnum >= 0
			&& (vars.lvframe60 < 60 || (vars.lvframenum % 120) == 9)) {
		int32_t r_canfree = bgunCanFreeWeapon(HAND_RIGHT) ? 1 : 0;
		int32_t l_canfree = bgunCanFreeWeapon(HAND_LEFT) ? 1 : 0;
		log(player,ctrl,r_canfree,l_canfree);
	}

	if (ctrl->switchtoweaponnum >= 0) {
		if (bgunCanFreeWeapon(HAND_RIGHT) && bgunCanFreeWeapon(HAND_LEFT)) {
			int32_t weaponnum = player->gunctrl.weaponnum;
			int32_t previnuse = player->hands[HAND_LEFT].inuse;
			Hand *lefthand;
			Hand *righthand;

			if (currentPlayerGetDeviceState(ctrl->switchtoweaponnum) != DEVICESTATE_UNEQUIPPED) {
				ctrl->switchtoweaponnum = WEAPON_UNARMED;
			}


			if (ctrl->dualwielding && !invHasDoubleWeaponIncAllGuns(ctrl->switchtoweaponnum, ctrl->switchtoweaponnum)) {
				ctrl->dualwielding = false;
			}

			func0f0d7364();

			bgunFreeWeapon(HAND_LEFT);
			bgunFreeWeapon(HAND_RIGHT);

			if (weaponnum == WEAPON_HORIZONSCANNER) {
				vars.currentplayer->insightaimmode = false;
			}

			if (weaponnum == WEAPON_RCP120) {
				int32_t amount = player->hands[HAND_RIGHT].matmot1;

				if (amount > player->ammoheldarr[ctrl->ammotypes[0]]) {
					amount = player->ammoheldarr[ctrl->ammotypes[0]];
				}

				player->ammoheldarr[ctrl->ammotypes[0]] -= amount;
			}

			if (weaponnum == WEAPON_HORIZONSCANNER) {
				vars.currentplayer->zoomintimemax = 0;
				vars.currentplayer->zoomintime = vars.currentplayer->zoomintimemax;
				vars.currentplayer->zoominfovynew = 60;
				vars.currentplayer->zoominfovy = vars.currentplayer->zoominfovynew;
			}

			lefthand = &player->hands[HAND_LEFT];
			righthand = &player->hands[HAND_RIGHT];

			if (ctrl->switchtoweaponnum == WEAPON_NONE) {
				lefthand->inuse = false;
				righthand->inuse = false;
				ctrl->weaponnum = WEAPON_NONE;
			} else {
				bgunSetGunMemWeapon(ctrl->switchtoweaponnum);
				ctrl->weaponnum = ctrl->switchtoweaponnum;
				lefthand->inuse = true;
				righthand->inuse = true;
			}

			if (ctrl->weaponnum == WEAPON_REMOTEMINE) {
				ctrl->dualwielding = true;
			}

			if (!ctrl->dualwielding) {
				lefthand->inuse = false;
			}

			if (weaponnum <= WEAPON_PSYCHOSISGUN && weaponnum >= WEAPON_UNARMED) {
				player->gunctrl.prevweaponnum = weaponnum;
			}

			if (previnuse) {
				player->gunctrl.prevwasdualwielding = true;
			} else {
				player->gunctrl.prevwasdualwielding = false;
			}

			vars.currentplayer->gunctrl.invertgunfunc = false;
			vars.currentplayer->usedowntime = -1;

			for(i=0;i<2;++i)bgunResetSwitchHand(player,ctrl,i);

			invCalculateCurrentIndex();

			ctrl->switchtoweaponnum = -1;
			ctrl->fnfader = 0;

			if (ctrl->weaponnum == WEAPON_DISGUISE40 || ctrl->weaponnum == WEAPON_DISGUISE41) {
				Chr *chr = player->prop->chr;

				disguiseSound();

				vars.currentplayer->disguised = true;

				chr->hidden |= CHRHFLAG_DISGUISED;

				if (vars.stagenum == STAGE_RESCUE) {
					chr->hidden |= CHRHFLAG_UNTARGETABLE;
				}

				invRemoveItemByNum(ctrl->weaponnum);
				bgunCycleBack();
			}

			ctrl->curfnstr = 0;
			ctrl->fnstrtimer = 0;
			ctrl->throwing = false;
		}
	} else {
		if (((player->hands[HAND_LEFT].inuse && !player->gunctrl.dualwielding)
					|| (!player->hands[HAND_LEFT].inuse && player->gunctrl.dualwielding))
				&& bgunCanFreeWeapon(HAND_LEFT)) {
			bgunFreeWeapon(HAND_LEFT);
			player->hands[HAND_LEFT].inuse = player->gunctrl.dualwielding;
		}
	}

    }
    static ggv_value read(void *p,ggv_binding binding){auto &h=*static_cast<SwitchParentHost*>(p);ggv_value v{};
        switch(binding){
        case GGV_SW_PLAYER_NUM:v.i=h.vars.currentplayernum;break;
        case GGV_SW_PENDING:v.i=h.ctrl->switchtoweaponnum;break;
        case GGV_SW_FRAME60:v.i=h.vars.lvframe60;break;
        case GGV_SW_FRAME:v.i=h.vars.lvframenum;break;
        case GGV_SW_WEAPON:v.i=h.ctrl->weaponnum;break;
        case GGV_SW_OLD_WEAPON:v.i=h.player->gunctrl.weaponnum;break;
        case GGV_SW_LEFT_INUSE:v.i=h.player->hands[HAND_LEFT].inuse;break;
        case GGV_SW_DUAL:v.i=h.ctrl->dualwielding;break;
        case GGV_SW_PLAYER_DUAL:v.i=h.player->gunctrl.dualwielding;break;
        case GGV_SW_MATMOT:v.f=h.player->hands[HAND_RIGHT].matmot1;break;
        case GGV_SW_RESERVE:v.i=h.player->ammoheldarr[h.ctrl->ammotypes[0]];break;
        case GGV_SW_RESERVE_TARGET:v.i=*(h.reserve = &h.player->ammoheldarr[h.ctrl->ammotypes[0]]);break;
        case GGV_SW_ZOOM_MAX:v.f=h.vars.currentplayer->zoomintimemax;break;
        case GGV_SW_FOV_NEW:v.f=h.vars.currentplayer->zoominfovynew;break;
        case GGV_SW_PLAYER_CHR:v.ref=h.player->prop->chr;break;
        case GGV_SW_CHR_HIDDEN:v.u=h.chr->hidden;break;
        case GGV_SW_STAGE:v.i=h.vars.stagenum;break;
        case GGV_SW_NONE:v.i=WEAPON_NONE;break;
        case GGV_SW_UNARMED:v.i=WEAPON_UNARMED;break;
        case GGV_SW_PSYCHOSIS:v.i=WEAPON_PSYCHOSISGUN;break;
        case GGV_SW_SCANNER:v.i=WEAPON_HORIZONSCANNER;break;
        case GGV_SW_RCP:v.i=WEAPON_RCP120;break;
        case GGV_SW_REMOTE:v.i=WEAPON_REMOTEMINE;break;
        case GGV_SW_DISGUISE_A:v.i=WEAPON_DISGUISE40;break;
        case GGV_SW_DISGUISE_B:v.i=WEAPON_DISGUISE41;break;
        case GGV_SW_RESCUE:v.i=STAGE_RESCUE;break;
        default:FAIL("Unexpected switch parent read");}return v;}
    static void write(void *p,ggv_binding binding,ggv_value v){auto &h=*static_cast<SwitchParentHost*>(p);switch(binding){
        case GGV_SW_PENDING:h.ctrl->switchtoweaponnum=v.i;break;
        case GGV_SW_DIAG_RIGHT:h.diagRight=v.i;break;
        case GGV_SW_DIAG_LEFT:h.diagLeft=v.i;break;
        case GGV_SW_WEAPON:h.ctrl->weaponnum=v.i;break;
        case GGV_SW_LEFT_INUSE:h.player->hands[HAND_LEFT].inuse=v.i;break;
        case GGV_SW_RIGHT_INUSE:h.player->hands[HAND_RIGHT].inuse=v.i;break;
        case GGV_SW_DUAL:h.ctrl->dualwielding=v.i;break;
        case GGV_SW_PREV_WEAPON:h.player->gunctrl.prevweaponnum=v.i;break;
        case GGV_SW_PREV_DUAL:h.player->gunctrl.prevwasdualwielding=v.i;break;
        case GGV_SW_CURRENT_AIM:h.vars.currentplayer->insightaimmode=v.b;break;
        case GGV_SW_RESERVE_TARGET:*h.reserve=v.i;break;
        case GGV_SW_ZOOM_MAX:h.vars.currentplayer->zoomintimemax=v.f;break;
        case GGV_SW_ZOOM_TIME:h.vars.currentplayer->zoomintime=v.f;break;
        case GGV_SW_FOV_NEW:h.vars.currentplayer->zoominfovynew=v.f;break;
        case GGV_SW_FOV:h.vars.currentplayer->zoominfovy=v.f;break;
        case GGV_SW_CURRENT_INVERT:h.vars.currentplayer->gunctrl.invertgunfunc=v.i;break;
        case GGV_SW_USE_TIME:h.vars.currentplayer->usedowntime=v.i;break;
        case GGV_SW_FADER:h.ctrl->fnfader=v.i;break;
        case GGV_SW_CHR:h.chr=static_cast<Chr*>(v.ref);break;
        case GGV_SW_CURRENT_DISGUISED:h.vars.currentplayer->disguised=v.b;break;
        case GGV_SW_CHR_HIDDEN:h.chr->hidden=v.u;break;
        case GGV_SW_CURRENT_STRING:h.ctrl->curfnstr=v.i;break;
        case GGV_SW_STRING_TIMER:h.ctrl->fnstrtimer=v.i;break;
        case GGV_SW_THROWING:h.ctrl->throwing=v.i;break;
        default:FAIL("Unexpected switch parent write");}}
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value b,ggv_value){auto &h=*static_cast<SwitchParentHost*>(p);ggv_value v{};
        switch(service){case GGV_SW_CAN_FREE:v.b=h.bgunCanFreeWeapon(a.i);break;
        case GGV_SW_DEVICE_STATE:v.i=h.currentPlayerGetDeviceState(a.i);break;
        case GGV_SW_HAS_DOUBLE:v.b=h.invHasDoubleWeaponIncAllGuns(a.i,b.i);break;
        case GGV_SW_CLEANUP:h.func0f0d7364();break;case GGV_SW_FREE:h.bgunFreeWeapon(a.i);break;
        case GGV_SW_SET_MEMORY:h.bgunSetGunMemWeapon(a.i);break;
        case GGV_SW_RESET_HAND:h.bgunResetSwitchHand(h.player,h.ctrl,a.i);break;
        case GGV_SW_INVENTORY_INDEX:h.invCalculateCurrentIndex();break;case GGV_SW_DISGUISE_SOUND:h.disguiseSound();break;
        case GGV_SW_REMOVE_ITEM:h.invRemoveItemByNum(a.i);break;case GGV_SW_CYCLE_BACK:h.bgunCycleBack();break;
        case GGV_SW_LOG:h.log(h.player,h.ctrl,h.diagRight,h.diagLeft);break;
        default:FAIL("Unexpected switch parent service");}return v;}
    bool graph(){prepare();ggv_host host{this,read,write,call};int32_t result=123;
        return ggvExecute(ggvStockProgram(GGV_SWITCH_DRIVER),&host,&result)&&result==0;}
};
void compareSwitchParent(SwitchParentHost input){auto expected=input,actual=input;expected.native();REQUIRE(actual.graph());
    expected.record(99);actual.record(99);REQUIRE(actual.events==expected.events);
}
TEST_CASE("Switch parent preserves diagnostic cadence and independent versus short-circuit readiness", "[gameplay_graph][switch_parent]") {
    for(int player:{0,1})for(int frame60:{0,59,60,100})for(int frame:{-7,9,129,130})
        for(const auto &ready:std::vector<std::vector<int>>{{1},{0},{1,0},{2,-1,0,1}}){SwitchParentHost h;
            h.currentIndex=player;h.vars.currentplayernum=player;h.vars.lvframe60=frame60;h.vars.lvframenum=frame;h.readiness=ready;compareSwitchParent(h);}
}
TEST_CASE("Switch parent preserves queued weapon policies and reserve adjustment", "[gameplay_graph][switch_parent]") {
    for(int old:{-1,0,1,13,36,37,42})for(int next:{0,1,10,34,56,57})for(int device:{-1,0})for(int dual:{0,1})for(int owns:{0,2}){
        SwitchParentHost h;auto &p=h.players[0];p.gunctrl.weaponnum=static_cast<int8_t>(old);p.gunctrl.switchtoweaponnum=static_cast<int8_t>(next);
        p.gunctrl.dualwielding=dual;p.hands[1].inuse=2;h.device=device;h.hasDouble=owns;h.vars.stagenum=53;compareSwitchParent(h);}
    for(float amount:{-2.75f,0.f,2.75f,100.f}){SwitchParentHost h;h.players[0].gunctrl.weaponnum=13;h.players[0].hands[0].matmot1=amount;compareSwitchParent(h);}
}
TEST_CASE("Switch parent preserves captured owners and callback-fresh fields", "[gameplay_graph][switch_parent]") {
    for(int mode:{1,2,3,4,5,6,8,9}){SwitchParentHost h;h.mode=mode;
        if(mode==1)h.players[0].gunctrl.weaponnum=13;
        if(mode==2)h.players[0].gunctrl.weaponnum=42;
        if(mode==4||mode==9)h.players[0].gunctrl.switchtoweaponnum=56;
        if(mode==5)h.vars.lvframe60=0;
        compareSwitchParent(h);}
}
TEST_CASE("Switch parent preserves no-pending dual reconciliation and callback-fresh assignment", "[gameplay_graph][switch_parent]") {
    for(int left:{0,1,2,-1})for(int dual:{0,1})for(int ready:{0,1})for(int mode:{0,7}){
        SwitchParentHost h;h.players[0].gunctrl.switchtoweaponnum=-1;h.players[0].hands[1].inuse=left;
        h.players[0].gunctrl.dualwielding=dual;h.readiness={ready};h.mode=mode;compareSwitchParent(h);}
}

struct ConfigureGauge {int16_t loadedammo=-12,timer60=120;int8_t ref=-7,change=11;uint16_t alignment=0xa55a;};
void nativeGaugeReset(ConfigureGauge *g){g->loadedammo=0;g->change=0;g->ref=0;g->timer60=0;}
ggv_value gaugeResetRead(void*,ggv_binding){FAIL("Ammo display reset has no reads");return {};}
void gaugeResetWrite(void *p,ggv_binding binding,ggv_value v){auto &g=*static_cast<ConfigureGauge*>(p);
    switch(binding){case GGV_ABMAG_LOADED:g.loadedammo=static_cast<int16_t>(v.i);break;
    case GGV_ABMAG_CHANGE:g.change=static_cast<int8_t>(v.i);break;case GGV_ABMAG_REF:g.ref=static_cast<int8_t>(v.i);break;
    case GGV_ABMAG_TIMER:g.timer60=static_cast<int16_t>(v.i);break;default:FAIL("Unexpected ammo display reset write");}}
bool graphGaugeReset(ConfigureGauge *g){ggv_host host{g,gaugeResetRead,gaugeResetWrite,nullptr};int32_t result=123;
    return ggvExecute(ggvStockProgram(GGV_ABMAG_RESET),&host,&result)&&result==0;}
TEST_CASE("Ammo display reset clears native scalar fields and preserves alignment", "[gameplay_graph][hand_configure]") {
    for(int16_t loaded:std::array<int16_t,3>{INT16_MIN,7,INT16_MAX})for(int8_t change:std::array<int8_t,3>{INT8_MIN,3,INT8_MAX}){
        ConfigureGauge expected;expected.loadedammo=loaded;expected.change=change;expected.ref=change;expected.timer60=loaded;
        auto actual=expected;nativeGaugeReset(&expected);REQUIRE(graphGaugeReset(&actual));
        REQUIRE(actual.loadedammo==expected.loadedammo);REQUIRE(actual.change==expected.change);REQUIRE(actual.ref==expected.ref);
        REQUIRE(actual.timer60==expected.timer60);REQUIRE(actual.alignment==expected.alignment);}
}
struct HandConfigureHost {
    struct Ammo{uint32_t type=1;int16_t clipsize=12;};
    struct Definition{Ammo *ammos[2]{};} definitions[2];
    Ammo ammo[2][2]{{{1,12},{2,13}},{{7,22},{8,23}}};bool present[2][2]{{true,true},{true,true}};
    struct Hand{struct Gset{uint8_t weaponnum=5;}gset;int32_t clipsizes[2]{31,32},loadedammo[2]{21,22};
        float upgrademult[12]{3,4,5,6,7,8,9,10,11,12,13,14},finalmult[12]{15,16,17,18,19,20,21,22,23,24,25,26};ConfigureGauge abmag;};
    struct Control{int8_t ammotypes[2]{6,7},lastmag=1;ConfigureGauge abmag;};
    struct Player{Hand hands[2];Control gunctrl;}players[2];
    int current=0,index=0,owner=0,slot=0,definitionIndex=0,resetCount=0;
    bool nullWeapon=false,changeOnQuery=false,changeOnReset=false,useGraphs=false;
    Hand *hand=nullptr;Control *ctrl=nullptr;Definition *weapon=nullptr;std::vector<int64_t> events;
    void prepare(){owner=current;hand=&players[owner].hands[index];ctrl=&players[owner].gunctrl;weapon=nullptr;
        for(int d=0;d<2;++d)for(int i=0;i<2;++i)definitions[d].ammos[i]=present[d][i]?&ammo[d][i]:nullptr;}
    void gaugeState(const ConfigureGauge &g){events.insert(events.end(),{g.loadedammo,g.change,g.ref,g.timer60,g.alignment});}
    void record(int event,int argument=0){events.insert(events.end(),{event,argument,current,definitionIndex,resetCount});
        for(auto &p:players){events.insert(events.end(),{p.gunctrl.ammotypes[0],p.gunctrl.ammotypes[1],p.gunctrl.lastmag});gaugeState(p.gunctrl.abmag);
            for(auto &h:p.hands){events.insert(events.end(),{h.gset.weaponnum,h.clipsizes[0],h.clipsizes[1],h.loadedammo[0],h.loadedammo[1]});
                for(float v:h.upgrademult)events.push_back(std::bit_cast<int32_t>(v));for(float v:h.finalmult)events.push_back(std::bit_cast<int32_t>(v));gaugeState(h.abmag);}}
    }
    Definition *find(int id){record(1,id);auto *result=nullWeapon?nullptr:&definitions[definitionIndex];
        if(changeOnQuery){hand->gset.weaponnum=34;current=1-current;definitionIndex=1;}return result;}
    int gaugeId(ConfigureGauge *g){for(int p=0;p<2;++p){if(g==&players[p].gunctrl.abmag)return p*3+2;
        for(int i=0;i<2;++i)if(g==&players[p].hands[i].abmag)return p*3+i;}FAIL("Unexpected gauge identity");return -1;}
    void reset(ConfigureGauge *g){record(2,gaugeId(g));++resetCount;
        if(useGraphs)REQUIRE(graphGaugeReset(g));else nativeGaugeReset(g);
        if(changeOnReset){current=1-current;ctrl->ammotypes[0]=-1;}}
    void native(){prepare();auto *capturedHand=hand;auto *capturedCtrl=ctrl;auto *definition=find(capturedHand->gset.weaponnum);
        for(int i=0;i<2;++i){
            if(index==0)capturedCtrl->ammotypes[i]=-1;
            if(definition && definition->ammos[i]){
                if(index==0)capturedCtrl->ammotypes[i]=static_cast<int8_t>(definition->ammos[i]->type);
                capturedHand->clipsizes[i]=definition->ammos[i]->clipsize;
                if(index==1 && capturedHand->gset.weaponnum==34)capturedHand->clipsizes[i]=0;
                capturedHand->loadedammo[i]=0;
            }
        }
        capturedHand->upgrademult[0]=1;capturedHand->upgrademult[1]=1;capturedHand->finalmult[0]=1;capturedHand->finalmult[1]=1;
        if(capturedCtrl->ammotypes[0]>=0){reset(&capturedHand->abmag);if(index==0)reset(&capturedCtrl->abmag);capturedCtrl->lastmag=false;}
    }
    static ggv_value read(void *p,ggv_binding binding){auto &h=*static_cast<HandConfigureHost*>(p);ggv_value v{};
        switch(binding){case GGV_VIS_WEAPON_ID:v.i=h.hand->gset.weaponnum;break;case GGV_VIS_WEAPON:v.ref=h.weapon;break;
        case GGV_VIS_AMMO:v.ref=h.weapon->ammos[h.slot];break;case GGV_SWITCH_HAND_INDEX:v.i=h.index;break;
        case GGV_CONFIG_AMMO_TYPE:v.u=h.weapon->ammos[h.slot]->type;break;case GGV_CONFIG_AMMO_CLIP:v.i=h.weapon->ammos[h.slot]->clipsize;break;
        case GGV_SW_REMOTE:v.i=34;break;case GGV_CONFIG_FIRST_AMMO:v.i=h.ctrl->ammotypes[0];break;
        case GGV_CONFIG_HAND_ABMAG:v.ref=&h.hand->abmag;break;case GGV_CONFIG_CTRL_ABMAG:v.ref=&h.ctrl->abmag;break;
        default:FAIL("Unexpected hand configure read");}return v;}
    static void write(void *p,ggv_binding binding,ggv_value v){auto &h=*static_cast<HandConfigureHost*>(p);
        switch(binding){case GGV_VIS_WEAPON:h.weapon=static_cast<Definition*>(v.ref);break;case GGV_VIS_AMMO_SLOT:h.slot=v.i;break;
        case GGV_CONFIG_CTRL_AMMO:h.ctrl->ammotypes[h.slot]=static_cast<int8_t>(v.i);break;case GGV_VIS_CLIP_SIZE:h.hand->clipsizes[h.slot]=v.i;break;
        case GGV_VIS_LOADED:h.hand->loadedammo[h.slot]=v.i;break;case GGV_CONFIG_UPGRADE0:h.hand->upgrademult[0]=v.f;break;
        case GGV_CONFIG_UPGRADE1:h.hand->upgrademult[1]=v.f;break;case GGV_CONFIG_FINAL0:h.hand->finalmult[0]=v.f;break;
        case GGV_CONFIG_FINAL1:h.hand->finalmult[1]=v.f;break;case GGV_CONFIG_LASTMAG:h.ctrl->lastmag=static_cast<int8_t>(v.i);break;
        default:FAIL("Unexpected hand configure write");}}
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value,ggv_value){auto &h=*static_cast<HandConfigureHost*>(p);ggv_value v{};
        switch(service){case GGV_QUERY_FIND_WEAPON:v.ref=h.find(a.i);break;case GGV_CONFIG_RESET_ABMAG:h.reset(static_cast<ConfigureGauge*>(a.ref));break;
        default:FAIL("Unexpected hand configure service");}return v;}
    bool graph(){prepare();useGraphs=true;ggv_host host{this,read,write,call};int32_t result=123;
        return ggvExecute(ggvStockProgram(GGV_HAND_CONFIGURE),&host,&result)&&result==0;}
};
void compareHandConfigure(HandConfigureHost input){auto expected=input,actual=input;expected.native();REQUIRE(actual.graph());
    expected.record(9);actual.record(9);REQUIRE(actual.events==expected.events);}
TEST_CASE("Hand configuration preserves optional definitions, remote clips and untouched slots", "[gameplay_graph][hand_configure]") {
    for(int hand:{0,1})for(bool missing:{false,true})for(bool ammo0:{false,true})for(bool ammo1:{false,true})for(int weapon:{5,34})for(int first:{-1,0}){
        HandConfigureHost h;h.index=hand;h.nullWeapon=missing;h.present[0][0]=ammo0;h.present[0][1]=ammo1;
        h.players[0].hands[hand].gset.weaponnum=static_cast<uint8_t>(weapon);h.players[0].gunctrl.ammotypes[0]=static_cast<int8_t>(first);compareHandConfigure(h);}
}
TEST_CASE("Hand configuration preserves unsigned ammo narrowing and signed clip limits", "[gameplay_graph][hand_configure]") {
    for(uint32_t type:{0u,1u,127u,128u,255u,0xffffffffu})for(int16_t clip:std::array<int16_t,3>{INT16_MIN,17,INT16_MAX}){
        HandConfigureHost h;h.ammo[0][0].type=type;h.ammo[0][0].clipsize=clip;compareHandConfigure(h);}
}
TEST_CASE("Hand configuration captures owners and definition before lookup callback changes", "[gameplay_graph][hand_configure]") {
    for(int current:{0,1})for(int hand:{0,1})for(bool missing:{false,true}){HandConfigureHost h;h.current=current;h.index=hand;h.nullWeapon=missing;
        h.changeOnQuery=true;compareHandConfigure(h);}
}
TEST_CASE("Hand configuration composes display resets without rechecking the initial ammo gate", "[gameplay_graph][hand_configure]") {
    for(int current:{0,1})for(int hand:{0,1}){HandConfigureHost h;h.current=current;h.index=hand;h.changeOnReset=true;compareHandConfigure(h);}
}

struct FreeWeaponHost {
    struct Hand{struct Gset{uint8_t weaponnum=5;}gset;int8_t inuse=1;int32_t state=2,stateminor=3,count=4;
        int32_t clipsizes[2]{12,15},loadedammo[2]{2,3};uint16_t gunroundsspent[4]{100,200,300,400};};
    struct Control{int8_t weaponnum=5,ammotypes[2]{0,1};int32_t gunmemtype=8;};
    struct Player{Hand hands[2];Control gunctrl;int32_t ammoheldarr[33]{10,20,30};int retired[2]{};}players[2];
    struct Vars{Player *currentplayer=nullptr;int32_t currentplayernum=0;uint32_t mplayerisrunning=0;}vars;
    Player *player=nullptr;Hand *hand=nullptr;int32_t *reserve=nullptr;
    int currentIndex=0,owner=0,handnum=0,slot=0,reloadIndex=0,indexCalls=0,region=0,mode=0;
    std::vector<int> indices{0,1};std::vector<int64_t> events;
    void prepare(){owner=currentIndex;vars.currentplayer=&players[currentIndex];player=vars.currentplayer;hand=&player->hands[handnum];reserve=nullptr;}
    void current(int p){vars.currentplayer=&players[p];vars.currentplayernum=p;}
    void record(int event,int a=0,int b=0){events.insert(events.end(),{event,a,b,vars.currentplayer-players,vars.currentplayernum,vars.mplayerisrunning,indexCalls});
        for(auto &p:players){events.insert(events.end(),{p.gunctrl.weaponnum,p.gunctrl.ammotypes[0],p.gunctrl.ammotypes[1],p.gunctrl.gunmemtype,p.retired[0],p.retired[1]});
            for(auto &h:p.hands){events.insert(events.end(),{h.gset.weaponnum,h.inuse,h.state,h.stateminor,h.count,h.clipsizes[0],h.clipsizes[1],h.loadedammo[0],h.loadedammo[1]});
                for(auto value:h.gunroundsspent)events.push_back(value);}
            for(auto value:p.ammoheldarr)events.push_back(value);}
    }
    int bgunGetUnequippedReloadIndex(int weapon){int result=indices[std::min(static_cast<size_t>(indexCalls),indices.size()-1)];++indexCalls;record(1,weapon,result);
        if(indexCalls==1){
            if(mode==1){hand->clipsizes[0]=90;hand->loadedammo[0]=5;player->gunctrl.ammotypes[0]=2;}
            if(mode==2){current(1-owner);vars.mplayerisrunning=0x80000000u;}
            if(mode==6)player->gunctrl.ammotypes[1]=-1;
            if(mode==7)player->gunctrl.weaponnum=20;
            if(mode==8)hand->inuse=0;
        }return result;}
    void playermgrDeleteWeapon(int index){record(2,index);if(mode==3)current(1-owner);}
    void bgunFreeHeldRocket(int index){record(3,index);}
    void wgV2RuntimeRetireHand(Player *p,int index,const char *reason){REQUIRE(std::strcmp(reason,"native weapon freed")==0);
        record(4,index,static_cast<int>(p-players));++p->retired[index];if(mode==5)current(0);}
    void logEnter(Player *p,int index){REQUIRE(p==player);record(5,index);if(mode==4)p->hands[index].inuse=0;}
    void logExit(Player *p,int index){REQUIRE(p==player);record(6,index);}
    void native(){prepare();

	Player *player = vars.currentplayer;
	int32_t i;

	/* B-246 round-6 instrumentation: any path that frees a hand resets
	 * inuse=false and disables the visibility gate. Log every call so we
	 * can correlate with subsequent invisibility windows. */
	if (vars.currentplayernum == 0) {
		logEnter(player,handnum);
	}

	if (player->hands[handnum].inuse) {
		for (i = 0; i < 2; i++) {
			if (player->gunctrl.ammotypes[i] >= 0) {
				int32_t spaceinclip = player->hands[handnum].clipsizes[i] - player->hands[handnum].loadedammo[i];
				int32_t index = bgunGetUnequippedReloadIndex(player->gunctrl.weaponnum);

				if (index != -1) {
if(region==2){
					player->hands[handnum].gunroundsspent[index] = (spaceinclip << 8) + 0xff;
}else if(region==1){
					player->hands[handnum].gunroundsspent[index] = spaceinclip * 213 + 212;
}else{
					player->hands[handnum].gunroundsspent[index] = (spaceinclip << 8) | 0xff;
}
				}

				if (player->hands[handnum].loadedammo[i] > 0) {
					player->ammoheldarr[player->gunctrl.ammotypes[i]] += player->hands[handnum].loadedammo[i];
				}

				player->hands[handnum].loadedammo[i] = 0;
			}
		}
	}

	if (vars.mplayerisrunning) {
		playermgrDeleteWeapon(handnum);
	}

	bgunFreeHeldRocket(handnum);
	wgV2RuntimeRetireHand(player, handnum, "native weapon freed");

	if (vars.currentplayernum == 0) {
		logExit(player,handnum);
	}

    }
    static ggv_value read(void *p,ggv_binding binding){auto &h=*static_cast<FreeWeaponHost*>(p);ggv_value v{};
        switch(binding){case GGV_SW_PLAYER_NUM:v.i=h.vars.currentplayernum;break;case GGV_CLEAN_INUSE:v.i=h.hand->inuse;break;
        case GGV_CLEAN_AMMO_TYPE:v.i=h.player->gunctrl.ammotypes[h.slot];break;case GGV_VIS_CLIP_SIZE:v.i=h.hand->clipsizes[h.slot];break;
        case GGV_VIS_LOADED:v.i=h.hand->loadedammo[h.slot];break;case GGV_CLEAN_WEAPON:v.i=h.player->gunctrl.weaponnum;break;
        case GGV_CLEAN_RESERVE:v.i=*(h.reserve=&h.player->ammoheldarr[h.player->gunctrl.ammotypes[h.slot]]);break;
        case GGV_CLEAN_MULTIPLAYER:v.u=h.vars.mplayerisrunning;break;case GGV_SWITCH_HAND_INDEX:v.i=h.handnum;break;
        case GGV_CLEAN_REGION:v.i=h.region;break;default:FAIL("Unexpected cleanup read");}return v;}
    static void write(void *p,ggv_binding binding,ggv_value v){auto &h=*static_cast<FreeWeaponHost*>(p);
        switch(binding){case GGV_VIS_AMMO_SLOT:h.slot=v.i;break;case GGV_CLEAN_RELOAD_INDEX:h.reloadIndex=v.i;break;
        case GGV_CLEAN_ROUNDS:h.hand->gunroundsspent[h.reloadIndex]=static_cast<uint16_t>(v.u);break;
        case GGV_CLEAN_RESERVE:*h.reserve=v.i;break;case GGV_VIS_LOADED:h.hand->loadedammo[h.slot]=v.i;break;
        default:FAIL("Unexpected cleanup write");}}
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value,ggv_value){auto &h=*static_cast<FreeWeaponHost*>(p);ggv_value v{};
        switch(service){case GGV_RELOAD_INDEX_FOR_WEAPON:v.i=h.bgunGetUnequippedReloadIndex(a.i);break;
        case GGV_CLEAN_DELETE:h.playermgrDeleteWeapon(a.i);break;case GGV_CLEAN_ROCKET:h.bgunFreeHeldRocket(a.i);break;
        case GGV_CLEAN_RETIRE:h.wgV2RuntimeRetireHand(h.player,a.i,"native weapon freed");break;
        case GGV_CLEAN_LOG_ENTER:h.logEnter(h.player,h.handnum);break;case GGV_CLEAN_LOG_EXIT:h.logExit(h.player,h.handnum);break;
        default:FAIL("Unexpected cleanup service");}return v;}
    bool graph(){prepare();ggv_host host{this,read,write,call};int32_t result=123;
        return ggvExecute(ggvStockProgram(GGV_FREE_WEAPON),&host,&result)&&result==0;}
};
void compareFreeWeapon(FreeWeaponHost input){auto expected=input,actual=input;expected.native();REQUIRE(actual.graph());
    expected.record(9);actual.record(9);REQUIRE(actual.events==expected.events);}
TEST_CASE("Weapon cleanup preserves regional counter arithmetic within native defined domains", "[gameplay_graph][weapon_cleanup]") {
    for(int region:{0,1,2})for(int index:{-1,0,1,2,3})for(int space:{0,10,400,10000}){
        FreeWeaponHost h;h.region=region;h.indices={index};h.players[0].gunctrl.ammotypes[1]=-1;
        h.players[0].hands[0].clipsizes[0]=space+2;compareFreeWeapon(h);}
    // Negative multiplication is defined in PAL; negative signed shifts are not.
    for(int loaded:{10,100}){FreeWeaponHost h;h.region=1;h.players[0].hands[0].clipsizes[0]=0;
        h.players[0].hands[0].loadedammo[0]=loaded;compareFreeWeapon(h);}
}
TEST_CASE("Weapon cleanup preserves signed hand and ammo gates and full-width multiplayer truth", "[gameplay_graph][weapon_cleanup]") {
    for(int player:{0,1})for(int hand:{0,1})for(int inuse:{-1,0,1})for(int ammo:{-1,0})for(int loaded:{-2,0,2})
        for(uint32_t multiplayer:{0u,1u,0x80000000u,0xffffffffu}){FreeWeaponHost h;h.currentIndex=player;h.vars.currentplayernum=player;
            h.handnum=hand;h.players[player].hands[hand].inuse=static_cast<int8_t>(inuse);h.players[player].gunctrl.ammotypes[0]=static_cast<int8_t>(ammo);
            h.players[player].hands[hand].loadedammo[0]=loaded;h.vars.mplayerisrunning=multiplayer;compareFreeWeapon(h);}
}
TEST_CASE("Weapon cleanup preserves aliased and absent reload counter indices across both slots", "[gameplay_graph][weapon_cleanup]") {
    for(int region:{0,1,2})for(const auto &indices:std::vector<std::vector<int>>{{0,0},{0,1},{-1,3},{3,-1}}){
        FreeWeaponHost h;h.region=region;h.indices=indices;compareFreeWeapon(h);}
}
TEST_CASE("Weapon cleanup captures space and owner while re-reading callback-mutated fields", "[gameplay_graph][weapon_cleanup]") {
    for(int player:{0,1})for(int hand:{0,1})for(int mode:{1,2,3,4,5,6,7,8})for(int region:{0,1,2}){
        FreeWeaponHost h;h.currentIndex=player;h.vars.currentplayernum=player;h.handnum=hand;h.mode=mode;h.region=region;
        if(mode==3)h.vars.mplayerisrunning=1;compareFreeWeapon(h);}
}

struct EquipRequestHost {
    struct Player{struct Control{int8_t weapon=5,pending=-1;uint8_t wantammo:1=1,dual:1=0;}ctrl;uint8_t hands[2]{5,6};}players[2];
    struct Context{EquipRequestHost *world;Player *player;int32_t hand,request,diagnostic=0;};
    int current=0,mode=0;static constexpr int customEnd=0x56+(64-0x29);std::vector<int64_t> events;
    void record(int event,int hand,int request,int diagnostic=0){events.insert(events.end(),{event,hand,request,diagnostic,current});
        for(auto &p:players)events.insert(events.end(),{p.ctrl.weapon,p.ctrl.pending,p.ctrl.wantammo,p.ctrl.dual,p.hands[0],p.hands[1]});}
    void innerLog(Player *captured,int request,int diagnostic){record(1,static_cast<int>(captured-players),request,diagnostic);
        if(mode==2){current=1-current;captured->ctrl.weapon=static_cast<int8_t>(request);captured->ctrl.pending=-1;}
        if(mode==3){current=1-current;captured->ctrl.pending=9;}}
    void enter(int hand,int request){record(2,hand,request);if(mode==1)current=1-current;}
    void leave(int hand,int request){record(3,hand,request);}
    void nativeInner(int32_t request){Player *captured=&players[current];
        if(current==0)innerLog(captured,request,(captured->ctrl.weapon==request && captured->ctrl.pending==-1)?1:0);
        if(captured->ctrl.weapon==request && captured->ctrl.pending==-1)return;
        captured->ctrl.pending=static_cast<int8_t>(request);captured->ctrl.wantammo=false;
    }
    void nativeOuter(int32_t hand,int32_t request){if(current==0)enter(hand,request);
        if(hand==1){if(request==0)players[current].ctrl.dual=false;else players[current].ctrl.dual=true;}
        else{if(request>=customEnd)request=1;nativeInner(request);}
        if(current==0)leave(hand,request);
    }
    static ggv_value read(void *p,ggv_binding binding){auto &c=*static_cast<Context*>(p);auto &h=*c.world;ggv_value v{};
        switch(binding){case GGV_SW_PLAYER_NUM:v.i=h.current;break;case GGV_EQUIP_REQUEST:v.i=c.request;break;
        case GGV_SWITCH_HAND_INDEX:v.i=c.hand;break;case GGV_SW_WEAPON:v.i=c.player->ctrl.weapon;break;
        case GGV_SW_PENDING:v.i=c.player->ctrl.pending;break;case GGV_SW_NONE:v.i=0;break;case GGV_SW_UNARMED:v.i=1;break;
        case GGV_EQUIP_CUSTOM_END:v.i=customEnd;break;default:FAIL("Unexpected equip read");}return v;}
    static void write(void *p,ggv_binding binding,ggv_value v){auto &c=*static_cast<Context*>(p);auto &h=*c.world;
        switch(binding){case GGV_EQUIP_REQUEST:c.request=v.i;break;case GGV_EQUIP_DIAG_SHORTCUT:c.diagnostic=v.i;break;
        case GGV_SW_PENDING:c.player->ctrl.pending=static_cast<int8_t>(v.i);break;case GGV_EQUIP_WANT_AMMO:c.player->ctrl.wantammo=v.b;break;
        case GGV_EQUIP_CURRENT_DUAL:h.players[h.current].ctrl.dual=v.b;break;default:FAIL("Unexpected equip write");}}
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value,ggv_value){auto &c=*static_cast<Context*>(p);auto &h=*c.world;ggv_value v{};
        switch(service){case GGV_EQUIP_CHILD:REQUIRE(h.graphInner(a.i));break;case GGV_EQUIP_LOG:h.innerLog(c.player,c.request,c.diagnostic);break;
        case GGV_EQUIP2_LOG_ENTER:h.enter(c.hand,c.request);break;case GGV_EQUIP2_LOG_EXIT:h.leave(c.hand,c.request);break;
        default:FAIL("Unexpected equip service");}return v;}
    bool run(Context &c,ggv_stock_program program){ggv_host host{&c,read,write,call};int32_t result=123;
        return ggvExecute(ggvStockProgram(program),&host,&result)&&result==0;}
    bool graphInner(int32_t request){Context c{this,&players[current],0,request};return run(c,GGV_EQUIP_WEAPON);}
    bool graphOuter(int32_t hand,int32_t request){Context c{this,nullptr,hand,request};return run(c,GGV_EQUIP_WEAPON_HAND);}
};
void compareEquip(EquipRequestHost input,int hand,int request,bool outer){auto expected=input,actual=input;
    if(outer){expected.nativeOuter(hand,request);REQUIRE(actual.graphOuter(hand,request));}
    else{expected.nativeInner(request);REQUIRE(actual.graphInner(request));}
    expected.record(9,hand,request);actual.record(9,hand,request);REQUIRE(actual.events==expected.events);
}
TEST_CASE("Equip request preserves early return, native byte storage and full-width comparisons", "[gameplay_graph][equip_request]") {
    for(int current:{0,1})for(int weapon:{-128,-1,0,5,127})for(int pending:{-1,7})
        for(int request:{-129,-128,-1,0,1,5,108,109,261,INT32_MAX}){EquipRequestHost h;h.current=current;
            h.players[current].ctrl.weapon=static_cast<int8_t>(weapon);h.players[current].ctrl.pending=static_cast<int8_t>(pending);compareEquip(h,0,request,false);}
}
TEST_CASE("Hand equip request clamps only non-left requests and composes the inner graph", "[gameplay_graph][equip_request]") {
    for(int current:{0,1})for(int hand:{-1,0,1,2,INT32_MAX})for(int request:{-129,-1,0,5,108,109,110,INT32_MAX}){
        EquipRequestHost h;h.current=current;compareEquip(h,hand,request,true);}
}
TEST_CASE("Equip graph rechecks after diagnostics and preserves captured versus current players", "[gameplay_graph][equip_request]") {
    for(int mode:{1,2,3})for(int hand:{0,1})for(int request:{5,7}){
        EquipRequestHost h;h.mode=mode;compareEquip(h,hand,request,true);compareEquip(h,hand,request,false);}
}

struct InitHandAnimsHost {
    struct Anim{int calls=0;};struct Model{Anim *anim=nullptr;};
    struct Hand{float gangstarot=1.25f;int32_t state=5,animload=12,animmode=3;Anim anim;Model gunmodel,handmodel;};
    struct Player{Hand hands[2];}players[2];Anim sentinels[2];Hand *selected=nullptr;
    int current=0,initCalls=0;bool changeBegin=false,changeFirst=false,changeSecond=false;std::vector<int64_t> events;
    void prepare(){selected=nullptr;for(auto &p:players)for(auto &h:p.hands){h.gunmodel.anim=&sentinels[0];h.handmodel.anim=&sentinels[1];}}
    int animId(Anim *a){if(!a)return -1;for(int p=0;p<2;++p)for(int i=0;i<2;++i)if(a==&players[p].hands[i].anim)return p*2+i;
        for(int i=0;i<2;++i)if(a==&sentinels[i])return 100+i;FAIL("Unexpected animation identity");return -2;}
    void record(int event,int arg=0){events.insert(events.end(),{event,arg,current,initCalls});
        for(auto &p:players)for(auto &h:p.hands)events.insert(events.end(),{std::bit_cast<int32_t>(h.gangstarot),h.state,h.animload,h.animmode,h.anim.calls,animId(h.gunmodel.anim),animId(h.handmodel.anim)});}
    void begin(Player *p){record(1,static_cast<int>(p-players));if(changeBegin)current=1-current;}
    void init(Anim *a){record(2,animId(a));++a->calls;++initCalls;if((initCalls==1 && changeFirst)||(initCalls==2 && changeSecond))current=1-current;}
    void log(){record(3);}
    void native(){prepare();begin(&players[current]);for(int i=0;i<2;++i){Hand *hand;
        if(i==0)hand=&players[current].hands[1];else hand=&players[current].hands[0];
        hand->gangstarot=0;hand->state=0;hand->animload=-1;hand->animmode=0;init(&hand->anim);
        hand->gunmodel.anim=&hand->anim;hand->handmodel.anim=&hand->anim;
    }if(current==0)log();}
    static ggv_value read(void *p,ggv_binding binding){auto &h=*static_cast<InitHandAnimsHost*>(p);ggv_value v{};
        switch(binding){case GGV_INIT_PLAYER_REF:v.ref=&h.players[h.current];break;case GGV_INIT_ANIM_REF:v.ref=&h.selected->anim;break;
        case GGV_SW_PLAYER_NUM:v.i=h.current;break;default:FAIL("Unexpected animation initialization read");}return v;}
    static void write(void *p,ggv_binding binding,ggv_value v){auto &h=*static_cast<InitHandAnimsHost*>(p);
        switch(binding){case GGV_INIT_HAND:h.selected=static_cast<Hand*>(v.ref);break;case GGV_ANIM_GANGSTA:h.selected->gangstarot=v.f;break;
        case GGV_STATE_VALUE:h.selected->state=v.i;break;case GGV_ANIM_LOAD:h.selected->animload=v.i;break;
        case GGV_EMPTY_ANIM_MODE:h.selected->animmode=v.i;break;case GGV_INIT_GUN_ANIM:h.selected->gunmodel.anim=static_cast<Anim*>(v.ref);break;
        case GGV_INIT_HAND_ANIM:h.selected->handmodel.anim=static_cast<Anim*>(v.ref);break;default:FAIL("Unexpected animation initialization write");}}
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value,ggv_value){auto &h=*static_cast<InitHandAnimsHost*>(p);ggv_value v{};
        switch(service){case GGV_INIT_PLAYER_BEGIN:h.begin(static_cast<Player*>(a.ref));break;case GGV_INIT_HAND_REF:v.ref=&h.players[h.current].hands[a.i];break;
        case GGV_INIT_ANIM:h.init(static_cast<Anim*>(a.ref));break;case GGV_INIT_LOG:h.log();break;default:FAIL("Unexpected animation initialization service");}return v;}
    bool graph(){prepare();ggv_host host{this,read,write,call};int32_t result=123;
        return ggvExecute(ggvStockProgram(GGV_INIT_HAND_ANIMS),&host,&result)&&result==0;}
};
void compareInitHandAnims(InitHandAnimsHost input){auto expected=input,actual=input;expected.native();REQUIRE(actual.graph());
    expected.record(9);actual.record(9);REQUIRE(actual.events==expected.events);}
TEST_CASE("Hand animation initialization preserves left-right order, reset fields and both model links", "[gameplay_graph][init_hand_anims]") {
    for(int current:{0,1}){InitHandAnimsHost h;h.current=current;compareInitHandAnims(h);}
}
TEST_CASE("Hand animation initialization reselects the current player between captured hand callbacks", "[gameplay_graph][init_hand_anims]") {
    for(int current:{0,1})for(bool begin:{false,true})for(bool first:{false,true})for(bool second:{false,true}){
        InitHandAnimsHost h;h.current=current;h.changeBegin=begin;h.changeFirst=first;h.changeSecond=second;compareInitHandAnims(h);}
}

struct GunMemoryControlHost {
    struct Player{uint8_t owner=0,master=4,gun=3;int8_t timer=7;int32_t pending=-1;}players[2];
    struct Context{GunMemoryControlHost *world;Player *player;int32_t request;};
    int current=0,mode=0,calls=0;int32_t budget=0;std::vector<int64_t> events;
    void record(int event,int request=0){events.insert(events.end(),{event,request,current,budget,calls});
        for(auto &p:players)events.insert(events.end(),{p.owner,p.master,p.gun,p.timer,p.pending});}
    void log(int event,Player *captured,int request){record(event,request);if(event==2)return;
        if(mode==1)current=1-current;
        if(mode==2)captured->owner=0;
        if(mode==3){captured->owner=11;current=1-current;}}
    void reset(){record(3);}
    void tick(){record(4);++calls;if(mode==1)budget=0;else if(mode==2 && calls==1)budget=25;}
    void nativeFree(){if(current==0 && players[current].owner!=11)log(0,&players[current],0);
        players[current].owner=11;reset();}
    void nativeSet(int32_t request){Player *player=&players[current];if(current==0)log(1,player,request);
        if(player->owner==0){player->master=0;player->gun=0;player->pending=request;player->timer=-1;}
        else player->pending=request;
        if(current==0)log(2,player,request);}
    void nativeTick(){for(int32_t i=0;i<budget;i+=8)tick();}
    static ggv_value read(void *p,ggv_binding binding){auto &c=*static_cast<Context*>(p);auto &h=*c.world;ggv_value v{};
        switch(binding){case GGV_SW_PLAYER_NUM:v.i=h.current;break;case GGV_MEM_CURRENT_OWNER:v.i=h.players[h.current].owner;break;
        case GGV_LOADED_GUNMEMOWNER:v.i=c.player->owner;break;case GGV_EQUIP_REQUEST:v.i=c.request;break;
        case GGV_MEM_OWNER_FREE:v.i=11;break;case GGV_MEM_OWNER_BOND:v.i=0;break;case GGV_MEM_MASTER_FLUX:case GGV_MEM_GUN_FLUX:v.i=0;break;
        case GGV_RELOAD_LVUPDATE240:v.i=h.budget;break;default:FAIL("Unexpected gun memory read");}return v;}
    static void write(void *p,ggv_binding binding,ggv_value v){auto &c=*static_cast<Context*>(p);auto &h=*c.world;
        switch(binding){case GGV_MEM_CURRENT_OWNER:h.players[h.current].owner=static_cast<uint8_t>(v.i);break;
        case GGV_LOADED_MASTERLOADSTATE:c.player->master=static_cast<uint8_t>(v.i);break;
        case GGV_MEM_GUNLOADSTATE:c.player->gun=static_cast<uint8_t>(v.i);break;case GGV_LOADED_GUNMEMNEW:c.player->pending=v.i;break;
        case GGV_MEM_LOCK_TIMER:c.player->timer=static_cast<int8_t>(v.i);break;default:FAIL("Unexpected gun memory write");}}
    static ggv_value call(void *p,ggv_service service,ggv_value,ggv_value,ggv_value){auto &c=*static_cast<Context*>(p);auto &h=*c.world;ggv_value v{};
        switch(service){case GGV_MEM_FREE_LOG:h.log(0,&h.players[h.current],0);break;
        case GGV_MEM_SET_LOG_ENTER:h.log(1,c.player,c.request);break;case GGV_MEM_SET_LOG_EXIT:h.log(2,c.player,c.request);break;
        case GGV_MEM_TEXTURE_RESET:h.reset();break;case GGV_MEM_MASTER_TICK:h.tick();break;default:FAIL("Unexpected gun memory service");}return v;}
    bool graph(ggv_stock_program program,int32_t request){Context c{this,program==GGV_SET_GUN_MEMORY_WEAPON?&players[current]:nullptr,request};
        ggv_host host{&c,read,write,call};int32_t result=123;return ggvExecute(ggvStockProgram(program),&host,&result)&&result==0;}
};
void compareGunMemoryControl(GunMemoryControlHost input,ggv_stock_program program,int32_t request=0){auto expected=input,actual=input;
    if(program==GGV_FREE_GUN_MEMORY)expected.nativeFree();else if(program==GGV_SET_GUN_MEMORY_WEAPON)expected.nativeSet(request);else expected.nativeTick();
    REQUIRE(actual.graph(program,request));expected.record(9);actual.record(9);REQUIRE(actual.events==expected.events);
}
TEST_CASE("Gun memory release preserves diagnostic gate and always resets textures after current-owner store", "[gameplay_graph][gun_memory]") {
    for(int current:{0,1})for(int owner:{0,1,11,255})for(int mode:{0,1,2,3}){
        GunMemoryControlHost h;h.current=current;h.mode=mode;h.players[current].owner=static_cast<uint8_t>(owner);compareGunMemoryControl(h,GGV_FREE_GUN_MEMORY);}
}
TEST_CASE("Gun memory weapon assignment preserves full pending value and captured owner after callbacks", "[gameplay_graph][gun_memory]") {
    for(int current:{0,1})for(int owner:{0,1,11,255})for(int mode:{0,1,2,3})for(int request:{INT32_MIN,-1,0,108,INT32_MAX}){
        GunMemoryControlHost h;h.current=current;h.mode=mode;h.players[current].owner=static_cast<uint8_t>(owner);compareGunMemoryControl(h,GGV_SET_GUN_MEMORY_WEAPON,request);}
}
TEST_CASE("Load tick graph preserves eight-tick stride and callback-fresh finite budgets", "[gameplay_graph][gun_memory]") {
    // Donor i+=8 must stay in its defined signed arithmetic domain.
    for(int budget:{-8,0,1,7,8,9,16,24,63})for(int mode:{0,1,2}){
        GunMemoryControlHost h;h.budget=budget;h.mode=mode;compareGunMemoryControl(h,GGV_TICK_LOAD);}
}

struct MemoryOwnerHost {
    struct Player{uint8_t owner=0;int8_t timer=0;int32_t pending=-1,type=5,hasbody=1;uint8_t loadall:1=0;int flux=0,removed=0;}players[2];
    struct Context{MemoryOwnerHost *world;Player *player;int32_t request,wild=0;};
    int current=0,mode=0;uint32_t multiplayer=0;int32_t removeFlag=0,lastPair=-1,lastWild=0,lastOwner=-2;std::vector<int64_t> events;
    void record(int event,int32_t arg=0){events.insert(events.end(),{event,arg,current,lastPair,lastWild,lastOwner,multiplayer,removeFlag});
        for(auto &p:players)events.insert(events.end(),{p.owner,p.timer,p.pending,p.type,p.hasbody,p.loadall,p.flux,p.removed});}
    void log(int event,Player *captured,int32_t request,int32_t wild=0){record(event,event==2?wild:request);
        if(event==1 && mode==1){captured->owner=static_cast<uint8_t>(request);current=1-current;}
        if(event==1 && mode==2)captured->pending=101;
        if(event==2 && mode==3){captured->owner=static_cast<uint8_t>(request);captured->pending=-1;current=1-current;}
        if(event==3 && mode==6){captured->owner=3;captured->timer=7;current=1-current;}}
    void flux(){record(4);++players[current].flux;if(mode==4)current=1-current;}
    void removeBody(){record(5);++players[current].removed;if(mode==5)current=1-current;}
    int native(int32_t request){Player *player=&players[current];
        if(current==0 && player->owner!=request){int32_t pair=(static_cast<int32_t>(player->owner)<<8)|request;
            if(pair!=lastPair){lastPair=pair;log(1,player,request);}}
        if(current==0){int32_t gmn=player->pending;if(gmn < -1 || gmn > 100){if(gmn!=lastWild){lastWild=gmn;log(2,player,request,gmn);}}}
        if(player->owner==request)return 1;
        if(player->timer<0){player->timer=static_cast<int8_t>(player->timer-1);
            if(player->timer < -2){player->timer=0;player->owner=static_cast<uint8_t>(request);
                if(current==0){if(static_cast<int32_t>(player->owner)!=lastOwner){lastOwner=player->owner;log(3,player,request);}}return 1;}}
        else{bool unlock=false;switch(player->owner){case 0:
            if(player->type!=-1)player->pending=player->type;
            player->type=-1;flux();player->loadall=true;unlock=true;break;
        case 2:if(multiplayer)unlock=true;if(!player->hasbody)unlock=true;
            if(request==1 && removeFlag!=0){unlock=true;removeBody();}break;
        case 3:case 10:case 11:unlock=true;break;}
            if(unlock){player->timer=-1;player->owner=10;}return 0;}
        return 0;
    }
    static ggv_value read(void *p,ggv_binding binding){auto &c=*static_cast<Context*>(p);auto &h=*c.world;ggv_value v{};
        switch(binding){case GGV_SW_PLAYER_NUM:v.i=h.current;break;case GGV_LOADED_GUNMEMOWNER:v.i=c.player->owner;break;
        case GGV_LOADED_GUNMEMNEW:v.i=c.player->pending;break;case GGV_LOADED_GUNMEMTYPE:v.i=c.player->type;break;
        case GGV_MEM_LOCK_TIMER:v.i=c.player->timer;break;case GGV_MEM_REQUEST_OWNER:v.i=c.request;break;
        case GGV_MEM_LAST_PAIR:v.i=h.lastPair;break;case GGV_MEM_LAST_WILD:v.i=h.lastWild;break;case GGV_MEM_LAST_OWNER:v.i=h.lastOwner;break;
        case GGV_CLEAN_MULTIPLAYER:v.u=h.multiplayer;break;case GGV_MEM_HAS_BODY:v.i=c.player->hasbody;break;
        case GGV_MEM_REMOVE_BODY_FLAG:v.i=h.removeFlag;break;default:FAIL("Unexpected memory-owner read");}return v;}
    static void write(void *p,ggv_binding binding,ggv_value v){auto &c=*static_cast<Context*>(p);auto &h=*c.world;
        switch(binding){case GGV_LOADED_GUNMEMOWNER:c.player->owner=static_cast<uint8_t>(v.i);break;
        case GGV_LOADED_GUNMEMNEW:c.player->pending=v.i;break;case GGV_LOADED_GUNMEMTYPE:c.player->type=v.i;break;
        case GGV_MEM_LOCK_TIMER:c.player->timer=static_cast<int8_t>(v.i);break;case GGV_MEM_LOAD_ALL:c.player->loadall=v.b;break;
        case GGV_MEM_LAST_PAIR:h.lastPair=v.i;break;case GGV_MEM_LAST_WILD:h.lastWild=v.i;break;case GGV_MEM_LAST_OWNER:h.lastOwner=v.i;break;
        case GGV_MEM_WILD_VALUE:c.wild=v.i;break;default:FAIL("Unexpected memory-owner write");}}
    static ggv_value call(void *p,ggv_service service,ggv_value,ggv_value,ggv_value){auto &c=*static_cast<Context*>(p);auto &h=*c.world;ggv_value v{};
        switch(service){case GGV_MEM_OWNER_LOG_ENTER:h.log(1,c.player,c.request);break;case GGV_MEM_OWNER_LOG_WILD:h.log(2,c.player,c.request,c.wild);break;
        case GGV_MEM_OWNER_LOG_SUCCESS:h.log(3,c.player,c.request);break;case GGV_MEM_ENTER_FLUX:h.flux();break;case GGV_MEM_REMOVE_BODY:h.removeBody();break;
        default:FAIL("Unexpected memory-owner service");}return v;}
    int graph(int32_t request){Context c{this,&players[current],request};ggv_host host{&c,read,write,call};int32_t result=123;
        REQUIRE(ggvExecute(ggvStockProgram(GGV_CHANGE_MEMORY_OWNER),&host,&result));return result;}
};
void compareMemoryOwner(MemoryOwnerHost input,const std::vector<int32_t> &requests){auto expected=input,actual=input;
    for(int32_t request:requests){int e=expected.native(request),a=actual.graph(request);REQUIRE(a==e);
        expected.record(9,e);actual.record(9,a);REQUIRE(actual.events==expected.events);}}
TEST_CASE("Memory ownership graph preserves exclusive owner branches and unlock inputs", "[gameplay_graph][memory_owner]") {
    for(int owner:{0,1,2,3,10,11,255})for(int request:{0,1,2,11})for(uint32_t mp:{0u,1u,0x80000000u})for(int body:{0,1,-1})for(int flag:{0,1,-1}){
        MemoryOwnerHost h;h.players[0].owner=static_cast<uint8_t>(owner);h.multiplayer=mp;h.players[0].hasbody=body;h.removeFlag=flag;compareMemoryOwner(h,{request});}
}
TEST_CASE("Memory ownership timer reads native signed-byte narrowing and full-width request before owner storage", "[gameplay_graph][memory_owner]") {
    for(int current:{0,1})for(int timer:{-128,-127,-3,-2,-1,0,127})for(int request:{INT32_MIN,-1,0,1,256,INT32_MAX}){
        MemoryOwnerHost h;h.current=current;h.players[current].timer=static_cast<int8_t>(timer);compareMemoryOwner(h,{request});}
}
TEST_CASE("Memory ownership diagnostic throttles retain pair wild-value and success state across calls", "[gameplay_graph][memory_owner]") {
    for(int pending:{INT32_MIN,-2,-1,0,100,101,INT32_MAX})for(int type:{-1,0,108}){
        MemoryOwnerHost h;h.players[0].owner=11;h.players[0].pending=pending;h.players[0].type=type;compareMemoryOwner(h,{0,0,0,0,1,1,1,1,0,0,0});}
}
TEST_CASE("Memory ownership rechecks log mutations and retains captured player after engine callbacks", "[gameplay_graph][memory_owner]") {
    for(int mode:{1,2,3,4,5,6})for(int owner:{0,2,11})for(int timer:{-2,0}){
        MemoryOwnerHost h;h.mode=mode;h.players[0].owner=static_cast<uint8_t>(owner);h.players[0].timer=static_cast<int8_t>(timer);
        h.players[0].pending=101;h.removeFlag=1;compareMemoryOwner(h,{1,0,1});}
}

struct LoadAllHost {
    struct Player{int8_t weapon=5;uint8_t owner=0,master=0;int32_t type=-1,pending=-1;uint8_t loadall:1=1;uint32_t active=0,inhibit=0;}players[2];
    int current=0,fluxCalls=0,ticks=0,checks=0,readyAfter=1,ownerAfter=-1;int32_t lockscreen=-7;
    bool pal=true,ownerReturn=false,swapFirstFlux=false,swapSecondFlux=false,swapOwner=false,swapTick=false,swapCheck=false;
    std::vector<int64_t> events;
    void record(int event,int arg=0){events.insert(events.end(),{event,arg,current,fluxCalls,ticks,checks,lockscreen});
        for(auto &p:players)events.insert(events.end(),{p.weapon,p.owner,p.master,p.type,p.pending,p.loadall,p.active,p.inhibit});}
    void flux(){record(1);++fluxCalls;if((fluxCalls==1 && swapFirstFlux)||(fluxCalls==2 && swapSecondFlux))current=1-current;}
    bool changeOwner(int request){record(2,request);if(ownerAfter>=0)players[current].owner=static_cast<uint8_t>(ownerAfter);if(swapOwner)current=1-current;return ownerReturn;}
    void tick(){record(3);++ticks;if(ticks==1 && swapTick)current=1-current;
        if(ticks>=readyAfter){players[current].owner=0;players[current].pending=-1;players[current].master=4;}}
    bool loaded(bool graphChild){record(4);++checks;bool result;
        if(graphChild){ggv_host host{this,read,write,nullptr};int32_t out=123;REQUIRE(ggvExecute(ggvStockProgram(GGV_IS_LOADED),&host,&out));result=out!=0;}
        else result=players[current].owner==0 && (players[current].type==0 || (players[current].pending<0 && players[current].master==4));
        if(checks==1 && swapCheck)current=1-current;return result;}
    int native(){if(pal && (players[current].active & ~players[current].inhibit & 4u)){players[current].loadall=false;return 0;}
        flux();if(players[current].weapon!=0)players[current].pending=players[current].weapon;else return 0;
        if(players[current].type!=-1)return 0;
        if(players[current].owner!=0){changeOwner(0);if(players[current].owner!=0){lockscreen=true;return 1;}}
        flux();do{tick();}while(!loaded(false));players[current].loadall=false;return 0;}
    static ggv_value read(void *p,ggv_binding binding){auto &h=*static_cast<LoadAllHost*>(p);ggv_value v{};
        switch(binding){case GGV_RELOAD_PAL:v.b=h.pal;break;case GGV_RELOAD_DEVICES_ACTIVE:v.u=h.players[h.current].active;break;
        case GGV_RELOAD_DEVICES_INHIBIT:v.u=h.players[h.current].inhibit;break;case GGV_SW_WEAPON:v.i=h.players[h.current].weapon;break;
        case GGV_SW_NONE:v.i=0;break;case GGV_LOADED_GUNMEMTYPE:v.i=h.players[h.current].type;break;
        case GGV_LOADED_GUNMEMOWNER:v.i=h.players[h.current].owner;break;case GGV_MEM_OWNER_BOND:v.i=0;break;
        case GGV_LOADED_GUNMEMNEW:v.i=h.players[h.current].pending;break;case GGV_LOADED_MASTERLOADSTATE:v.i=h.players[h.current].master;break;
        default:FAIL("Unexpected load-all read");}return v;}
    static void write(void *p,ggv_binding binding,ggv_value v){auto &h=*static_cast<LoadAllHost*>(p);
        switch(binding){case GGV_MEM_LOAD_ALL:h.players[h.current].loadall=v.b;break;
        case GGV_LOADED_GUNMEMNEW:h.players[h.current].pending=v.i;break;case GGV_LOAD_LOCKSCREEN:h.lockscreen=v.b;break;
        default:FAIL("Unexpected load-all write");}}
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value,ggv_value){auto &h=*static_cast<LoadAllHost*>(p);ggv_value v{};
        switch(service){case GGV_MEM_ENTER_FLUX:h.flux();break;case GGV_MEM_CHANGE_OWNER:v.b=h.changeOwner(a.i);break;
        case GGV_MEM_MASTER_TICK:h.tick();break;case GGV_RELOAD_IS_LOADED:v.b=h.loaded(true);break;default:FAIL("Unexpected load-all service");}return v;}
    int graph(){ggv_host host{this,read,write,call};int32_t out=123;REQUIRE(ggvExecute(ggvStockProgram(GGV_LOAD_ALL),&host,&out));return out;}
};
void compareLoadAll(LoadAllHost input){auto expected=input,actual=input;int e=expected.native(),a=actual.graph();REQUIRE(a==e);
    expected.record(9,e);actual.record(9,a);REQUIRE(actual.events==expected.events);}
TEST_CASE("Load-all regional Eyespy mask respects inhibition and leaves lockscreen unchanged", "[gameplay_graph][load_all]") {
    for(bool pal:{false,true})for(uint32_t active:{0u,4u,0x80000000u,0x80000004u})for(uint32_t inhibit:{0u,4u,0x80000000u,0xffffffffu}){
        LoadAllHost h;h.pal=pal;h.players[0].active=active;h.players[0].inhibit=inhibit;compareLoadAll(h);}
}
TEST_CASE("Load-all pending assignment precedes type early return and follows flux-selected player", "[gameplay_graph][load_all]") {
    for(bool swap:{false,true})for(int weapon:{-128,-1,0,5,127})for(int type:{INT32_MIN,-1,0,5}){
        LoadAllHost h;h.swapFirstFlux=swap;h.players[swap?1:0].weapon=static_cast<int8_t>(weapon);h.players[swap?1:0].type=type;compareLoadAll(h);}
}
TEST_CASE("Load-all retries from fresh owner state independently of child return", "[gameplay_graph][load_all]") {
    for(bool result:{false,true})for(int ownerAfter:{-1,0,2})for(bool swap:{false,true})for(int otherOwner:{0,11}){
        LoadAllHost h;h.players[0].owner=11;h.players[1].owner=static_cast<uint8_t>(otherOwner);h.ownerReturn=result;h.ownerAfter=ownerAfter;h.swapOwner=swap;compareLoadAll(h);}
}
TEST_CASE("Load-all composes the loaded graph after each tick and clears the callback-current load flag", "[gameplay_graph][load_all]") {
    for(int ready:{1,2,4})for(bool secondFlux:{false,true})for(bool tick:{false,true})for(bool check:{false,true}){
        LoadAllHost h;h.readyAfter=ready;h.swapSecondFlux=secondFlux;h.swapTick=tick;h.swapCheck=check;compareLoadAll(h);}
}

struct EnterFluxHost {
    static inline int token=0;
    struct Handle{const void *provider=&token;uint64_t opaque[2]{0x123456789abcdef0ULL,0xfedcba9876543210ULL};};
    struct Player{uint16_t file=321;std::array<char,64> catalog{};Handle handle;uint32_t generation=0xfedcba98u;
        void *handmodel=&token,*gunmodel=&token,*loadptr=&token;int32_t remaining=-17;uint8_t master=4,gun=3,owner=2;}players[2];
    struct Casing{void *model=&token;uint32_t neighbor=0xa5a5a5a5u;};std::array<Casing,22> casings;
    int current=0,index=-1;int32_t active=-1;bool swapLog=false;std::vector<int64_t> events;
    EnterFluxHost(){for(auto &p:players)for(size_t i=0;i<p.catalog.size();++i)p.catalog[i]=static_cast<char>('A'+i%26);}
    void record(int event,int arg=0){events.insert(events.end(),{event,arg,current,active});
        for(auto &p:players){events.insert(events.end(),{p.file,p.handle.provider!=nullptr,std::bit_cast<int64_t>(p.handle.opaque[0]),std::bit_cast<int64_t>(p.handle.opaque[1]),p.generation,p.handmodel!=nullptr,p.gunmodel!=nullptr,p.loadptr!=nullptr,p.remaining,p.master,p.gun,p.owner});
            for(char c:p.catalog)events.push_back(static_cast<unsigned char>(c));}
        for(auto &c:casings)events.insert(events.end(),{c.model!=nullptr,c.neighbor});}
    void log(){record(1);if(swapLog)current=1-current;}
    void clearHandle(){record(2);players[current].handle={nullptr,{0,0}};}
    void native(){if(current==0)log();players[current].file=0xffff;players[current].catalog[0]=0;clearHandle();
        players[current].generation=0;players[current].handmodel=nullptr;players[current].loadptr=nullptr;players[current].remaining=0;
        players[current].master=0;players[current].gun=0;
        // Slots0/21 are fixture canaries; production's casing array has20 entries.
        for(int i=0;i<20;++i){casings[i+1].model=nullptr;record(3,i);}active=false;
    }
    static ggv_value read(void *p,ggv_binding binding){auto &h=*static_cast<EnterFluxHost*>(p);ggv_value v{};
        switch(binding){case GGV_SW_PLAYER_NUM:v.i=h.current;break;case GGV_MEM_MASTER_FLUX:case GGV_MEM_GUN_FLUX:v.i=0;break;
        case GGV_FLUX_CASING_COUNT:v.i=20;break;default:FAIL("Unexpected flux read");}return v;}
    static void write(void *p,ggv_binding binding,ggv_value v){auto &h=*static_cast<EnterFluxHost*>(p);
        switch(binding){case GGV_FLUX_FILE:h.players[h.current].file=static_cast<uint16_t>(v.i);break;
        case GGV_FLUX_CATALOG_HEAD:h.players[h.current].catalog[0]=static_cast<char>(v.i);break;
        case GGV_FLUX_GENERATION:h.players[h.current].generation=v.u;break;case GGV_FLUX_HAND_MODEL:h.players[h.current].handmodel=v.ref;break;
        case GGV_FLUX_LOAD_PTR:h.players[h.current].loadptr=v.ref;break;case GGV_FLUX_LOAD_REMAINING:h.players[h.current].remaining=v.i;break;
        case GGV_LOADED_MASTERLOADSTATE:h.players[h.current].master=static_cast<uint8_t>(v.i);break;
        case GGV_MEM_GUNLOADSTATE:h.players[h.current].gun=static_cast<uint8_t>(v.i);break;case GGV_FLUX_CASING_INDEX:h.index=v.i;break;
        case GGV_FLUX_CASING_MODEL:h.casings.at(static_cast<size_t>(h.index+1)).model=v.ref;h.record(3,h.index);break;
        case GGV_FLUX_CASINGS_ACTIVE:h.active=v.b;break;default:FAIL("Unexpected flux write");}}
    static ggv_value call(void *p,ggv_service service,ggv_value,ggv_value,ggv_value){auto &h=*static_cast<EnterFluxHost*>(p);ggv_value v{};
        switch(service){case GGV_FLUX_LOG:h.log();break;case GGV_FLUX_CLEAR_HANDLE:h.clearHandle();break;default:FAIL("Unexpected flux service");}return v;}
    void graph(){ggv_host host{this,read,write,call};int32_t out=123;REQUIRE(ggvExecute(ggvStockProgram(GGV_ENTER_FLUX),&host,&out));REQUIRE(out==0);}
};
void compareEnterFlux(EnterFluxHost input,int repetitions=1){auto expected=input,actual=input;
    for(int i=0;i<repetitions;++i){expected.native();actual.graph();expected.record(9);actual.record(9);REQUIRE(actual.events==expected.events);}}
TEST_CASE("Flux graph clears ordered hand fields and exactly twenty casing references without clearing neighbors", "[gameplay_graph][flux]") {
    for(int current:{0,1}){EnterFluxHost h;h.current=current;compareEnterFlux(h);}
}
TEST_CASE("Flux reset follows current player after diagnostic callback", "[gameplay_graph][flux]") {
    EnterFluxHost h;h.swapLog=true;compareEnterFlux(h);
}
TEST_CASE("Repeated flux reset preserves catalog tail bytes and nulls the entire typed handle", "[gameplay_graph][flux]") {
    EnterFluxHost h;h.players[0].handle.opaque[0]=UINT64_MAX;h.players[0].catalog[63]='Z';compareEnterFlux(h,3);
}

struct MasterAdmissionHost : MemoryOwnerHost {
    int nativeAdmission(){Player *captured=&players[current];
        if((captured->owner==0 || native(0)) && captured->pending>=0){
            if(captured->timer==0)return 1;
            captured->timer=static_cast<int8_t>(captured->timer-1);if(captured->timer < -2)captured->timer=0;
        }return 0;
    }
    static ggv_value admissionCall(void *p,ggv_service service,ggv_value a,ggv_value,ggv_value){
        auto &c=*static_cast<Context*>(p);auto &h=*static_cast<MasterAdmissionHost*>(c.world);ggv_value v{};
        switch(service){case GGV_MEM_CHANGE_OWNER:v.b=h.MemoryOwnerHost::graph(a.i);break;default:FAIL("Unexpected master admission service");}return v;
    }
    int graphAdmission(){Context c{this,&players[current],0};ggv_host host{&c,MemoryOwnerHost::read,MemoryOwnerHost::write,admissionCall};int32_t result=123;
        REQUIRE(ggvExecute(ggvStockProgram(GGV_MASTER_LOAD_ADMISSION),&host,&result));return result;}
};
void compareMasterAdmission(MasterAdmissionHost input,int ticks=1){auto expected=input,actual=input;
    for(int i=0;i<ticks;++i){int e=expected.nativeAdmission(),a=actual.graphAdmission();REQUIRE(a==e);
        expected.record(9,e);actual.record(9,a);REQUIRE(actual.events==expected.events);}}
TEST_CASE("Master admission preserves ownership short-circuit, pending gate and native timer storage", "[gameplay_graph][master_admission]") {
    for(int current:{0,1})for(int owner:{0,1,2,3,10,11,255})for(int pending:{-1,0,10})for(int timer:{-128,-3,-2,-1,0,1,127}){
        MasterAdmissionHost h;h.current=current;auto &p=h.players[current];p.owner=static_cast<uint8_t>(owner);p.pending=pending;p.timer=static_cast<int8_t>(timer);
        compareMasterAdmission(h);}
}
TEST_CASE("Master admission composes ownership graph across repeated pending ticks", "[gameplay_graph][master_admission]") {
    for(int owner:{1,2,3,10,11})for(uint32_t multiplayer:{0u,0x80000000u}){
        MasterAdmissionHost h;h.players[0].owner=static_cast<uint8_t>(owner);h.players[0].pending=5;h.multiplayer=multiplayer;compareMasterAdmission(h,5);}
}
TEST_CASE("Master admission retains captured player after ownership diagnostic callbacks", "[gameplay_graph][master_admission]") {
    for(int mode:{1,2,3,6})for(int pending:{-1,0,101})for(int timer:{-2,0,7}){
        MasterAdmissionHost h;h.mode=mode;h.players[0].owner=11;h.players[0].pending=pending;h.players[0].timer=static_cast<int8_t>(timer);
        compareMasterAdmission(h,3);}
}

struct MasterSourceHost {
    static inline int modelTokens[2]{};
    struct Player{int32_t pending=5;uint8_t master=4;void *model=&modelTokens[0];}players[2];
    struct Context{MasterSourceHost *world;Player *player;int32_t weapon=0,body=0,head=0,hf32=0;uint16_t handfile=0,file=0;};
    int current=0,fluxes=0;bool bodyOK=true,fileOK=true,sourceOK=true,v2=true,hasHands=true,sourceCurrent=false,swapChoose=false,swapFlux=false;
    int32_t handFile=123;uint16_t weaponFile=456;void *runtimeModel=&modelTokens[1];std::vector<int64_t> events;
    int modelId(void *p){if(!p)return 0;if(p==&modelTokens[0])return 1;if(p==&modelTokens[1])return 2;FAIL("Unknown model witness");return -1;}
    void record(int event,int arg=0,int captured=-1){events.insert(events.end(),{event,arg,captured,current,fluxes});
        for(auto &p:players)events.insert(events.end(),{p.pending,p.master,modelId(p.model)});}
    bool choose(Context &c){record(1,c.weapon);c.body=7;c.head=8;if(swapChoose){c.player->pending=99;current=1-current;}return bodyOK;}
    bool handFileQuery(Context &c){record(2,c.body);c.hf32=fileOK?handFile:0;return fileOK;}
    bool handSourceQuery(Context &c){record(3,c.body);return sourceOK;}
    int fileQuery(int weapon){record(4,weapon);return weaponFile;}
    bool isV2(int weapon){record(5,weapon);return v2;}
    void *modelQuery(int weapon){record(6,weapon);return runtimeModel;}
    bool handsQuery(int weapon){record(7,weapon);return hasHands;}
    bool currentQuery(Context &c){record(8,c.body,static_cast<int>(c.player-players));return sourceCurrent;}
    void flux(){record(9);++fluxes;if(fluxes==1 && swapFlux)current=1-current;players[current].master=0;}
    void error(){record(10);}
    int native(Context &c){c.weapon=c.player->pending;c.hf32=0;
        if(!choose(c)||!handFileQuery(c)||!handSourceQuery(c)){error();return 0;}
        c.handfile=static_cast<uint16_t>(c.hf32);c.file=static_cast<uint16_t>(fileQuery(c.weapon));
        // RuntimeModel is a readonly query; no mutation of the comparison's other operand is modeled.
        if(isV2(c.weapon) && c.player->master==4 && c.player->model!=modelQuery(c.weapon))flux();
        if((c.weapon==1 || handsQuery(c.weapon)) && c.player->master>=(c.weapon==1?3:2) && !currentQuery(c))flux();
        return 1;
    }
    static ggv_value read(void *p,ggv_binding binding){auto &c=*static_cast<Context*>(p);ggv_value v{};
        switch(binding){case GGV_LOADED_GUNMEMNEW:v.i=c.player->pending;break;case GGV_MASTER_WEAPON:v.i=c.weapon;break;
        case GGV_MASTER_HF32:v.i=c.hf32;break;case GGV_LOADED_MASTERLOADSTATE:v.i=c.player->master;break;
        case GGV_MASTER_MODEL:v.ref=c.player->model;break;default:FAIL("Unexpected master source read");}return v;}
    static void write(void *p,ggv_binding binding,ggv_value v){auto &c=*static_cast<Context*>(p);
        switch(binding){case GGV_MASTER_WEAPON:c.weapon=v.i;break;case GGV_MASTER_HF32:c.hf32=v.i;break;
        case GGV_MASTER_HANDFILE:c.handfile=static_cast<uint16_t>(v.i);break;case GGV_MASTER_FILE:c.file=static_cast<uint16_t>(v.i);break;
        default:FAIL("Unexpected master source write");}}
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value,ggv_value){auto &c=*static_cast<Context*>(p);auto &h=*c.world;ggv_value v{};
        switch(service){case GGV_MASTER_CHOOSE_BODY:v.b=h.choose(c);break;case GGV_MASTER_HAND_FILE:v.b=h.handFileQuery(c);break;
        case GGV_MASTER_HAND_SOURCE:v.b=h.handSourceQuery(c);break;case GGV_MASTER_SOURCE_ERROR:h.error();break;
        case GGV_MASTER_WEAPON_FILE:v.i=h.fileQuery(a.i);break;case GGV_INFO_IS_V2:v.b=h.isV2(a.i);break;
        case GGV_MASTER_RUNTIME_MODEL:v.ref=h.modelQuery(a.i);break;case GGV_MASTER_HAS_HANDS:v.b=h.handsQuery(a.i);break;
        case GGV_MASTER_SOURCE_CURRENT:v.b=h.currentQuery(c);break;case GGV_MEM_ENTER_FLUX:h.flux();break;
        default:FAIL("Unexpected master source service");}return v;}
    std::array<int32_t,6> run(bool graph){Context c{this,&players[current]};int32_t result;
        if(graph){ggv_host host{&c,read,write,call};result=123;REQUIRE(ggvExecute(ggvStockProgram(GGV_MASTER_SOURCE_PREPARE),&host,&result));}
        else result=native(c);
        record(100,result);if(!result)return {0,0,0,0,0,0};return {result,c.weapon,c.handfile,c.file,c.body,c.head};}
};
void compareMasterSource(MasterSourceHost input){auto expected=input,actual=input;auto e=expected.run(false),a=actual.run(true);REQUIRE(a==e);REQUIRE(actual.events==expected.events);}
TEST_CASE("Master source preparation short-circuits identity failures before model refresh", "[gameplay_graph][master_source]") {
    for(bool body:{false,true})for(bool file:{false,true})for(bool source:{false,true})for(int handFile:{0,65535}){
        MasterSourceHost h;h.bodyOK=body;h.fileOK=file;h.sourceOK=source;h.handFile=handFile;compareMasterSource(h);}
}
TEST_CASE("Master source refresh preserves V2 model identity and distinct unarmed attachment thresholds", "[gameplay_graph][master_source]") {
    for(bool v2:{false,true})for(int weapon:{1,5})for(int state:{0,1,2,3,4})for(bool same:{false,true})for(bool hands:{false,true})for(bool current:{false,true}){
        MasterSourceHost h;h.v2=v2;h.players[0].pending=weapon;h.players[0].master=static_cast<uint8_t>(state);h.hasHands=hands;h.sourceCurrent=current;
        if(same)h.runtimeModel=h.players[0].model;compareMasterSource(h);}
    for(int stored:{0,1,2})for(int queried:{0,1,2}){
        MasterSourceHost h;h.players[0].model=stored?&MasterSourceHost::modelTokens[stored-1]:nullptr;
        h.runtimeModel=queried?&MasterSourceHost::modelTokens[queried-1]:nullptr;compareMasterSource(h);}
}
TEST_CASE("Master source preparation retains weapon and player while flux targets the callback-current player", "[gameplay_graph][master_source]") {
    for(bool choose:{false,true})for(bool flux:{false,true})for(int weapon:{1,5}){
        MasterSourceHost h;h.swapChoose=choose;h.swapFlux=flux;h.players[0].pending=weapon;compareMasterSource(h);}
}

struct MasterFluxHost {
    static inline int tokens[3]{};
    struct Player{void *cart=&tokens[0];uint8_t master=0;uint16_t handfile=321;}players[2];
    struct Casing{void *model=&tokens[2];uint32_t neighbor=0x12345678u;};std::array<Casing,22> casings;
    int current=0,captured=0,index=-1;int32_t active=-1,weapon=5,hasHands=1;uint16_t handfile=456;bool swapLog=false;std::vector<int64_t> events;
    int id(void *p){if(!p)return 0;for(int i=0;i<3;++i)if(p==&tokens[i])return i+1;FAIL("Unknown cartridge model witness");return -1;}
    void record(int event,int arg=0){events.insert(events.end(),{event,arg,current,captured,active,weapon,hasHands,handfile});
        for(auto &p:players)events.insert(events.end(),{id(p.cart),p.master,p.handfile});for(auto &c:casings)events.insert(events.end(),{id(c.model),c.neighbor});}
    void setCasing(int i,void *value){casings.at(static_cast<size_t>(i+1)).model=value;record(1,i);}
    void setActive(int value){active=value;record(2,value);}
    void setCart(void *value){players[captured].cart=value;record(3);}
    void setMaster(int value){players[captured].master=static_cast<uint8_t>(value);record(4,value);}
    void log(){record(5);if(swapLog)current=1-current;}
    void native(){for(int i=0;i<20;++i)if(casings[i+1].model==players[captured].cart)setCasing(i,nullptr);
        setActive(false);for(int i=0;i<20;++i)if(casings[i+1].model!=nullptr)setActive(true);
        setCart(nullptr);setMaster(1);if(current==0)log();}
    static ggv_value read(void *p,ggv_binding binding){auto &h=*static_cast<MasterFluxHost*>(p);ggv_value v{};
        switch(binding){case GGV_FLUX_CASING_COUNT:v.i=20;break;case GGV_FLUX_CASING_MODEL:v.ref=h.casings.at(static_cast<size_t>(h.index+1)).model;break;
        case GGV_MASTER_CART_MODEL:v.ref=h.players[h.captured].cart;break;case GGV_SW_PLAYER_NUM:v.i=h.current;break;
        default:FAIL("Unexpected master flux read");}return v;}
    static void write(void *p,ggv_binding binding,ggv_value v){auto &h=*static_cast<MasterFluxHost*>(p);
        switch(binding){case GGV_FLUX_CASING_INDEX:h.index=v.i;break;case GGV_FLUX_CASING_MODEL:h.setCasing(h.index,v.ref);break;
        case GGV_FLUX_CASINGS_ACTIVE:h.setActive(v.b);break;case GGV_MASTER_CART_MODEL:h.setCart(v.ref);break;
        case GGV_LOADED_MASTERLOADSTATE:h.setMaster(v.i);break;default:FAIL("Unexpected master flux write");}}
    static ggv_value call(void *p,ggv_service service,ggv_value,ggv_value,ggv_value){auto &h=*static_cast<MasterFluxHost*>(p);ggv_value v{};
        switch(service){case GGV_MASTER_FLUX_LOG:h.log();break;default:FAIL("Unexpected master flux service");}return v;}
    void graph(){ggv_host host{this,read,write,call};int32_t result=123;REQUIRE(ggvExecute(ggvStockProgram(GGV_MASTER_FLUX_TRANSITION),&host,&result));REQUIRE(result==0);}
};
void compareMasterFlux(MasterFluxHost input){auto expected=input,actual=input;expected.native();actual.graph();expected.record(9);actual.record(9);REQUIRE(actual.events==expected.events);}
TEST_CASE("Master flux clears only matching cartridge witnesses and scans every remaining casing", "[gameplay_graph][master_flux]") {
    for(int cart:{0,1,2,3})for(int pattern:{0,1,2}){
        MasterFluxHost h;h.players[0].cart=cart?&MasterFluxHost::tokens[cart-1]:nullptr;
        for(int i=1;i<=20;++i)h.casings[i].model=pattern==0?nullptr:pattern==1?h.players[0].cart:(i%2?&MasterFluxHost::tokens[0]:&MasterFluxHost::tokens[1]);
        compareMasterFlux(h);}
}
TEST_CASE("Master flux changes captured player while diagnostics use current-player gate", "[gameplay_graph][master_flux]") {
    for(int current:{0,1})for(int captured:{0,1})for(bool swap:{false,true}){
        MasterFluxHost h;h.current=current;h.captured=captured;h.swapLog=swap;h.players[1].cart=&MasterFluxHost::tokens[1];
        for(int i=1;i<=20;++i)h.casings[i].model=i%2?&MasterFluxHost::tokens[0]:&MasterFluxHost::tokens[1];compareMasterFlux(h);}
}
TEST_CASE("Master flux preserves diagnostic inputs and casing boundary neighbors", "[gameplay_graph][master_flux]") {
    for(int weapon:{1,108})for(int hands:{0,1})for(int file:{0,65535}){
        MasterFluxHost h;h.weapon=weapon;h.hasHands=hands;h.handfile=static_cast<uint16_t>(file);h.players[0].handfile=123;
        for(int i=1;i<=20;++i)h.casings[i].model=&MasterFluxHost::tokens[0];compareMasterFlux(h);}
}

struct MasterGunHost {
    static inline int tokens[5]{};
    struct Player{uint8_t gun=0,master=2;void *handptr=&tokens[0],*memptr=&tokens[1],*gunmodel=&tokens[2],*handmodel=&tokens[3];int32_t handremaining=17;uint32_t remaining=9;}players[2];
    struct Context{MasterGunHost *world;Player *player;int32_t weapon,body;uint16_t file;};
    int current=0,captured=0,weapon=5,body=7;uint16_t file=456;bool v2=false,bodyOK=true,retainedOK=true,swapQueue=false,swapTick=false;
    uint8_t tickState=4;void *runtimeModel=&tokens[4];std::vector<int64_t> events;
    int id(void *p){if(!p)return 0;for(int i=0;i<5;++i)if(p==&tokens[i])return i+1;FAIL("Unknown gun-stage witness");return -1;}
    void record(int event,int arg=0){events.insert(events.end(),{event,arg,current,captured,weapon,body,file});
        for(auto &p:players)events.insert(events.end(),{p.gun,p.master,id(p.handptr),id(p.memptr),id(p.gunmodel),id(p.handmodel),p.handremaining,p.remaining});}
    bool queueBody(Context &c){record(1,c.body);c.player->gunmodel=nullptr;c.player->gun=bodyOK?1:5;if(swapQueue)current=1-current;return bodyOK;}
    bool isV2(int request){record(2,request);return v2;}
    void *model(int request){record(3,request);return runtimeModel;}
    bool queueRetained(Context &c,void *source){record(4,id(source));bool ok=retainedOK && source;
        if(ok){c.player->gunmodel=source;c.player->gun=1;}if(swapQueue)current=1-current;return ok;}
    void queueStatic(Context &c){record(5,c.file);c.player->gunmodel=nullptr;if(swapQueue)current=1-current;}
    void tick(){record(6);Player *selected=&players[current];if(swapTick)current=1-current;selected->gun=tickState;
        if(tickState==4)selected->gunmodel=&tokens[4];}
    void log(){record(7);}
    int native(Context &c){if(c.player->gun==0){c.player->memptr=c.player->handptr;c.player->remaining=static_cast<uint32_t>(c.player->handremaining);c.player->gun=1;
        if(c.weapon==1){if(!queueBody(c))return 0;}else if(isV2(c.weapon)){if(!queueRetained(c,model(c.weapon)))return 0;}else queueStatic(c);}
        tick();if(c.player->gun==4){c.player->master=3;c.player->gun=0;if(current==0)log();}return 1;}
    static ggv_value read(void *p,ggv_binding binding){auto &c=*static_cast<Context*>(p);ggv_value v{};
        switch(binding){case GGV_MEM_GUNLOADSTATE:v.i=c.player->gun;break;case GGV_FLUX_LOAD_PTR:v.ref=c.player->handptr;break;
        case GGV_FLUX_LOAD_REMAINING:v.i=c.player->handremaining;break;case GGV_MASTER_WEAPON:v.i=c.weapon;break;
        case GGV_SW_PLAYER_NUM:v.i=c.world->current;break;default:FAIL("Unexpected master gun read");}return v;}
    static void write(void *p,ggv_binding binding,ggv_value v){auto &c=*static_cast<Context*>(p);
        switch(binding){case GGV_MASTER_MEM_PTR:c.player->memptr=v.ref;break;case GGV_MASTER_MEM_REMAINING:c.player->remaining=v.u;break;
        case GGV_MEM_GUNLOADSTATE:c.player->gun=static_cast<uint8_t>(v.i);break;case GGV_LOADED_MASTERLOADSTATE:c.player->master=static_cast<uint8_t>(v.i);break;
        default:FAIL("Unexpected master gun write");}}
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value,ggv_value){auto &c=*static_cast<Context*>(p);auto &h=*c.world;ggv_value v{};
        switch(service){case GGV_MASTER_QUEUE_BODY:v.b=h.queueBody(c);break;case GGV_INFO_IS_V2:v.b=h.isV2(a.i);break;
        case GGV_MASTER_RUNTIME_MODEL:v.ref=h.model(a.i);break;case GGV_MASTER_QUEUE_RETAINED:v.b=h.queueRetained(c,a.ref);break;
        case GGV_MASTER_QUEUE_STATIC:h.queueStatic(c);break;case GGV_MASTER_GUN_TICK:h.tick();break;case GGV_MASTER_GUN_LOG:h.log();break;
        default:FAIL("Unexpected master gun service");}return v;}
    int run(bool graph){Context c{this,&players[captured],weapon,body,file};int32_t result;
        if(graph){ggv_host host{&c,read,write,call};result=123;REQUIRE(ggvExecute(ggvStockProgram(GGV_MASTER_GUN_STAGE),&host,&result));}
        else result=native(c);record(9,result);return result;}
};
void compareMasterGun(MasterGunHost input){auto expected=input,actual=input;int e=expected.run(false),a=actual.run(true);REQUIRE(a==e);REQUIRE(actual.events==expected.events);}
TEST_CASE("Master gun stage selects exclusive queue paths and returns before tick on refusal", "[gameplay_graph][master_gun]") {
    for(int weapon:{1,5})for(bool v2:{false,true})for(bool body:{false,true})for(bool retained:{false,true})for(bool model:{false,true}){
        MasterGunHost h;h.weapon=weapon;h.v2=v2;h.bodyOK=body;h.retainedOK=retained;if(!model)h.runtimeModel=nullptr;compareMasterGun(h);}
}
TEST_CASE("Master gun stage copies pointer and signed capacity before MODEL queue setup", "[gameplay_graph][master_gun]") {
    // Tests component scalar transfer only; negative capacity is not engine allocation acceptance.
    for(int remaining:{INT32_MIN,-1,0,17,INT32_MAX})for(int file:{0,65535}){
        MasterGunHost h;h.players[0].handremaining=remaining;h.file=static_cast<uint16_t>(file);compareMasterGun(h);}
}
TEST_CASE("Master gun stage ticks existing requests and transitions only captured LOADED state", "[gameplay_graph][master_gun]") {
    for(int gun:{1,2,3,4,5,255})for(int tick:{0,1,4,5})for(int current:{0,1})for(int captured:{0,1}){
        MasterGunHost h;h.current=current;h.captured=captured;h.players[captured].gun=static_cast<uint8_t>(gun);h.tickState=static_cast<uint8_t>(tick);compareMasterGun(h);}
}
TEST_CASE("Master gun stage distinguishes queue-current tick and captured parent after callbacks", "[gameplay_graph][master_gun]") {
    for(int weapon:{1,5})for(bool v2:{false,true})for(bool queue:{false,true})for(bool tick:{false,true}){
        MasterGunHost h;h.weapon=weapon;h.v2=v2;h.swapQueue=queue;h.swapTick=tick;compareMasterGun(h);}
}

struct MasterHandsHost {
    static inline int tokens[5]{};
    struct Handle{const void *provider=&tokens[0];uint64_t opaque[2]{11,22};};
    struct Player{uint16_t handfile=123;std::array<char,64> handid{},loadid{};Handle handhandle,loadhandle;
        uint32_t handgen=5,loadgen=7;void *model=&tokens[1],*ptr=&tokens[2];int32_t remaining=-9;uint8_t gun=0,master=1;}players[2];
    struct Context{MasterHandsHost *world;Player *player;std::array<char,64> id{};Handle handle;};
    int current=0,captured=0,weapon=5,body=7;uint16_t handfile=123;bool hasHands=true,currentSource=false,sourceOK=true,queueOK=true;
    bool swapSource=false,swapQueue=false,swapTick=false;uint8_t tickState=4;uint32_t generation=7,capacity=307200;Handle sourceHandle;
    std::array<char,64> sourceId{};std::vector<int64_t> events;
    MasterHandsHost(){std::strcpy(sourceId.data(),"hands/middle");for(auto &p:players){std::strcpy(p.handid.data(),"hands/previous");std::strcpy(p.loadid.data(),sourceId.data());p.handid[63]='Z';}}
    int id(const void *p){if(!p)return 0;for(int i=0;i<5;++i)if(p==&tokens[i])return i+1;FAIL("Unknown hand-stage witness");return -1;}
    void record(int event,int arg=0){events.insert(events.end(),{event,arg,current,captured,weapon,body,handfile,generation,capacity});
        for(auto &p:players){events.insert(events.end(),{p.handfile,id(p.handhandle.provider),std::bit_cast<int64_t>(p.handhandle.opaque[0]),std::bit_cast<int64_t>(p.handhandle.opaque[1]),id(p.loadhandle.provider),std::bit_cast<int64_t>(p.loadhandle.opaque[0]),std::bit_cast<int64_t>(p.loadhandle.opaque[1]),p.handgen,p.loadgen,id(p.model),id(p.ptr),p.remaining,p.gun,p.master});
            for(char c:p.handid)events.push_back(static_cast<unsigned char>(c));for(char c:p.loadid)events.push_back(static_cast<unsigned char>(c));}}
    bool currentCheck(){record(1,body);return currentSource;}
    bool sourceCheck(Context &c){record(2,body);c.id=sourceId;c.handle=sourceHandle;if(swapSource)current=1-current;return sourceOK;}
    int compareId(Context &c){record(3);return std::strcmp(c.player->loadid.data(),c.id.data());}
    bool sameHandle(Context &c){record(4);auto &a=c.player->loadhandle;auto &b=c.handle;return a.provider==b.provider && a.opaque[0]==b.opaque[0] && a.opaque[1]==b.opaque[1];}
    uint32_t getGeneration(){record(5);return generation;}
    void *memory(){record(6);return &tokens[current?4:3];}
    uint32_t getCapacity(){record(7);return capacity;}
    bool queue(Context &c){record(8,body);c.player->model=nullptr;c.player->gun=queueOK?1:5;
        if(queueOK){c.player->loadid=sourceId;c.player->loadhandle=sourceHandle;c.player->loadgen=generation;}
        if(swapQueue)current=1-current;return queueOK;}
    void tick(){record(9);Player *selected=&players[current];if(swapTick)current=1-current;selected->gun=tickState;
        if(tickState==4){selected->model=&tokens[1];selected->handfile=999;selected->handid=selected->loadid;selected->handhandle=selected->loadhandle;selected->handgen=selected->loadgen;}}
    void clearHandle(Context &c){record(10);c.player->handhandle={nullptr,{0,0}};}
    void log(){record(11);}
    int native(Context &c){auto *p=c.player;if(hasHands){if(handfile!=p->handfile || !currentCheck()){
        if(!sourceCheck(c))return 0;
        if(compareId(c)||!sameHandle(c)||p->loadgen!=getGeneration())p->gun=0;
        if(p->gun==0){p->ptr=memory();p->remaining=static_cast<int32_t>(getCapacity());p->gun=1;if(!queue(c))return 0;}
        tick();if(p->gun==4)p->handfile=handfile;else return 0;
    }}else{p->handfile=0;p->handid[0]=0;clearHandle(c);p->handgen=getGeneration();p->model=nullptr;p->ptr=memory();p->remaining=static_cast<int32_t>(getCapacity());}
        p->master=2;p->gun=0;if(current==0)log();return 1;}
    static ggv_value read(void *p,ggv_binding binding){auto &c=*static_cast<Context*>(p);auto &h=*c.world;ggv_value v{};
        switch(binding){case GGV_MASTER_HAS_HANDS_INPUT:v.b=h.hasHands;break;case GGV_MASTER_HANDFILE:v.i=h.handfile;break;
        case GGV_FLUX_FILE:v.i=c.player->handfile;break;case GGV_MASTER_LOAD_GENERATION:v.u=c.player->loadgen;break;
        case GGV_MEM_GUNLOADSTATE:v.i=c.player->gun;break;case GGV_SW_PLAYER_NUM:v.i=h.current;break;default:FAIL("Unexpected master hands read");}return v;}
    static void write(void *p,ggv_binding binding,ggv_value v){auto &c=*static_cast<Context*>(p);
        switch(binding){case GGV_FLUX_FILE:c.player->handfile=static_cast<uint16_t>(v.i);break;case GGV_FLUX_CATALOG_HEAD:c.player->handid[0]=static_cast<char>(v.i);break;
        case GGV_FLUX_GENERATION:c.player->handgen=v.u;break;case GGV_FLUX_HAND_MODEL:c.player->model=v.ref;break;
        case GGV_FLUX_LOAD_PTR:c.player->ptr=v.ref;break;case GGV_FLUX_LOAD_REMAINING:c.player->remaining=v.i;break;
        case GGV_MEM_GUNLOADSTATE:c.player->gun=static_cast<uint8_t>(v.i);break;case GGV_LOADED_MASTERLOADSTATE:c.player->master=static_cast<uint8_t>(v.i);break;
        default:FAIL("Unexpected master hands write");}}
    static ggv_value call(void *p,ggv_service service,ggv_value,ggv_value,ggv_value){auto &c=*static_cast<Context*>(p);auto &h=*c.world;ggv_value v{};
        switch(service){case GGV_MASTER_SOURCE_CURRENT:v.b=h.currentCheck();break;case GGV_MASTER_HAND_SOURCE:v.b=h.sourceCheck(c);break;
        case GGV_MASTER_COMPARE_LOAD_ID:v.i=h.compareId(c);break;case GGV_MASTER_SAME_LOAD_HANDLE:v.b=h.sameHandle(c);break;
        case GGV_MASTER_CATALOG_GENERATION:v.u=h.getGeneration();break;case GGV_MASTER_MEMORY:v.ref=h.memory();break;
        case GGV_MASTER_CAPACITY:v.u=h.getCapacity();break;case GGV_MASTER_QUEUE_HAND:v.b=h.queue(c);break;
        case GGV_MASTER_GUN_TICK:h.tick();break;case GGV_MASTER_CLEAR_HAND_HANDLE:h.clearHandle(c);break;
        case GGV_MASTER_HANDS_LOG:h.log();break;default:FAIL("Unexpected master hands service");}return v;}
    int run(bool graph){Context c{this,&players[captured]};int32_t result;
        if(graph){ggv_host host{&c,read,write,call};result=123;REQUIRE(ggvExecute(ggvStockProgram(GGV_MASTER_HANDS_STAGE),&host,&result));}
        else result=native(c);record(99,result);return result;}
};
void compareMasterHands(MasterHandsHost input){auto expected=input,actual=input;int e=expected.run(false),a=actual.run(true);REQUIRE(a==e);REQUIRE(actual.events==expected.events);}
TEST_CASE("Master hands stage preserves identity gates and source queue or tick failure exits", "[gameplay_graph][master_hands]") {
    for(bool fileMatches:{false,true})for(bool current:{false,true})for(bool source:{false,true})for(bool queue:{false,true})for(int tick:{1,4,5})for(int gun:{0,1,4}){
        MasterHandsHost h;h.players[0].handfile=fileMatches?123:65535;h.currentSource=current;h.sourceOK=source;h.queueOK=queue;h.tickState=static_cast<uint8_t>(tick);h.players[0].gun=static_cast<uint8_t>(gun);compareMasterHands(h);}
}
TEST_CASE("Master hands stage short-circuits ID handle and full unsigned generation refresh checks", "[gameplay_graph][master_hands]") {
    for(int id:{0,1,2})for(int handle:{0,1,2})for(bool same:{false,true})for(uint32_t generation:{0u,0x80000000u,0xffffffffu}){
        MasterHandsHost h;h.players[0].gun=1;h.generation=generation;h.players[0].loadgen=same?generation:generation^1u;
        if(id)std::strcpy(h.players[0].loadid.data(),id==1?"hands/aaa":"hands/zzz");
        if(handle==1)h.players[0].loadhandle.provider=&MasterHandsHost::tokens[1];if(handle==2)h.players[0].loadhandle.opaque[1]^=0x8000000000000000ULL;
        compareMasterHands(h);}
}
TEST_CASE("Master no-hands path clears only catalog head and typed handle before current memory queries", "[gameplay_graph][master_hands]") {
    // Capacity high-bit cases qualify scalar conversion, not backend allocation.
    for(int current:{0,1})for(int captured:{0,1})for(uint32_t capacity:{0u,17u,0x80000000u,0xffffffffu}){
        MasterHandsHost h;h.hasHands=false;h.current=current;h.captured=captured;h.capacity=capacity;h.generation=0xfedcba98u;compareMasterHands(h);}
}
TEST_CASE("Master hands stage preserves captured writes across source queue and tick player changes", "[gameplay_graph][master_hands]") {
    for(int current:{0,1})for(int captured:{0,1})for(int gun:{0,4})for(bool source:{false,true})for(bool queue:{false,true})for(bool tick:{false,true}){
        MasterHandsHost h;h.current=current;h.captured=captured;h.players[captured].gun=static_cast<uint8_t>(gun);h.swapSource=source;h.swapQueue=queue;h.swapTick=tick;compareMasterHands(h);}
}

struct MasterCartsHost {
    static inline int tokens[4]{};
    struct Function {int32_t type=1;int8_t ammo=0;}functions[2];
    struct Weapon {int ammo[2]{0,1};}weapon;
    struct Ammo {uint32_t casing=0;}ammo[2];
    struct Player {uint8_t gun=0,master=3;int8_t weapon=5;void *cart=nullptr,*ptr=&tokens[0];uint32_t remaining=17;uint16_t file=777;bool queued=false;int funcs[2]{0,1};}players[2];
    struct Context {MasterCartsHost *world;Player *player;Function *func=nullptr;Weapon *weapon=nullptr;Ammo *ammo=nullptr;uint16_t file=0;};
    int current=0,captured=0,count=1;bool hasWeapon=true,swapFunction=false,swapWeapon=false,swapQueue=false,swapTick=false,cartOnFunction=false,weaponOnFunction=false;
    uint8_t tickState=4;std::vector<int64_t> events;std::vector<int> calls;
    MasterCartsHost(){functions[1].ammo=1;ammo[1].casing=3;}
    int id(void *p){if(!p)return 0;for(int i=0;i<4;++i)if(p==&tokens[i])return i+1;FAIL("Unknown cartridge witness");return -1;}
    void record(int event,int arg=0){calls.push_back(event);events.insert(events.end(),{event,arg,current,captured,count});
        for(auto &p:players)events.insert(events.end(),{p.gun,p.master,p.weapon,id(p.cart),id(p.ptr),p.remaining,p.file,p.queued,p.funcs[0],p.funcs[1]});
        for(auto &f:functions)events.insert(events.end(),{f.type,f.ammo});events.insert(events.end(),{weapon.ammo[0],weapon.ammo[1],ammo[0].casing,ammo[1].casing});}
    Function *function(Context &c,int hand){record(1,hand);int index=c.player->funcs[hand];
        if(cartOnFunction)c.player->cart=&tokens[1];if(weaponOnFunction)c.player->weapon=-128;if(swapFunction)current=1-current;return index<0?nullptr:&functions[index];}
    Weapon *findWeapon(int request){record(2,request);if(swapWeapon)current=1-current;return hasWeapon?&weapon:nullptr;}
    Ammo *findAmmo(Weapon *w,int index){record(3,index);int selected=w->ammo[index];return selected<0?nullptr:&ammo[selected];}
    uint16_t file(int casing){record(4,casing);constexpr uint16_t files[]{123,456,65535,0};return files[casing];}
    void queue(Context &c){record(5,c.file);c.player->file=c.file;c.player->queued=true;if(swapQueue)current=1-current;}
    void tick(){record(6);auto *selected=&players[current];if(swapTick)current=1-current;selected->gun=tickState;
        if(selected->queued && tickState==4){selected->cart=&tokens[2];selected->ptr=&tokens[3];selected->remaining=7;}}
    int native(Context &c){if(c.player->gun==4)c.player->gun=0;
        if(c.player->gun==0 && c.player->cart==nullptr && count==1){for(int hand=0;hand<2;++hand){
            auto *f=function(c,hand);Function *shoot=nullptr;auto *w=findWeapon(c.player->weapon);int32_t casing=-1;
            if(f){if((f->type&0xff)==1)shoot=f;if(w && shoot){auto *a=f->ammo>=0?findAmmo(w,f->ammo):nullptr;if(a)casing=std::bit_cast<int32_t>(a->casing);}
                if(casing>=0){if(c.player->cart==nullptr){c.file=file(casing);c.player->gun=1;queue(c);break;}break;}}}}
        if(c.player->gun!=0){tick();return 0;}return 1;}
    static ggv_value read(void *p,ggv_binding binding){auto &c=*static_cast<Context*>(p);ggv_value v{};
        switch(binding){case GGV_MEM_GUNLOADSTATE:v.i=c.player->gun;break;case GGV_MASTER_CART_MODEL:v.ref=c.player->cart;break;
        case GGV_CART_PLAYER_COUNT:v.i=c.world->count;break;case GGV_SW_WEAPON:v.i=c.player->weapon;break;
        case GGV_CART_TYPE:v.i=c.func->type;break;case GGV_CART_AMMO_INDEX:v.i=c.func->ammo;break;case GGV_CART_EJECT:v.u=c.ammo->casing;break;
        default:FAIL("Unexpected cartridge read");}return v;}
    static void write(void *p,ggv_binding binding,ggv_value v){auto &c=*static_cast<Context*>(p);
        switch(binding){case GGV_MEM_GUNLOADSTATE:c.player->gun=static_cast<uint8_t>(v.i);break;
        case GGV_CART_FUNC:c.func=static_cast<Function*>(v.ref);break;case GGV_CART_WEAPON:c.weapon=static_cast<Weapon*>(v.ref);break;
        case GGV_CART_AMMO:c.ammo=static_cast<Ammo*>(v.ref);break;case GGV_MASTER_FILE:c.file=static_cast<uint16_t>(v.i);break;
        default:FAIL("Unexpected cartridge write");}}
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value,ggv_value){auto &c=*static_cast<Context*>(p);auto &h=*c.world;ggv_value v{};
        switch(service){case GGV_CART_FUNCTION:v.ref=h.function(c,a.i);break;case GGV_CART_FIND_WEAPON:v.ref=h.findWeapon(a.i);break;
        case GGV_CART_AMMO_REF:v.ref=h.findAmmo(c.weapon,a.i);break;case GGV_CART_FILE:v.i=h.file(a.i);break;case GGV_CART_QUEUE:h.queue(c);break;
        case GGV_MASTER_GUN_TICK:h.tick();break;default:FAIL("Unexpected cartridge service");}return v;}
    int run(bool graph){Context c{this,&players[captured]};int32_t result=123;
        if(graph){ggv_host host{&c,read,write,call};REQUIRE(ggvExecute(ggvStockProgram(GGV_MASTER_CARTS_SELECT),&host,&result));}else result=native(c);
        record(9,result);return result;}
};
void compareMasterCarts(MasterCartsHost input){auto expected=input,actual=input;int e=expected.run(false),a=actual.run(true);REQUIRE(a==e);REQUIRE(actual.calls==expected.calls);REQUIRE(actual.events==expected.events);}
TEST_CASE("Master cartridge selection gates scans and always returns after a non-FLUX tick", "[gameplay_graph][master_carts]") {
    for(int state:{0,1,2,3,4,5,255})for(int count:{0,1,2,4})for(bool cart:{false,true})for(int tick:{0,4,5}){
        MasterCartsHost h;h.players[0].gun=static_cast<uint8_t>(state);h.count=count;if(cart)h.players[0].cart=&MasterCartsHost::tokens[1];h.tickState=static_cast<uint8_t>(tick);compareMasterCarts(h);}
}
TEST_CASE("Master cartridge selection preserves signed indices and low function type byte", "[gameplay_graph][master_carts]") {
    // Native array-valid input domains: nonnegative ammo indices 0/1 and casing indices 0..3.
    // High-bit casing words convert to negative s32 and never index the table. Source validation remains a separate gate.
    for(int type:{0,1,257,std::bit_cast<int32_t>(0x80000001u)})for(int index:{-128,-1,0,1})for(bool weapon:{false,true})for(bool available:{false,true})for(uint32_t casing:{0u,1u,2u,3u,0x80000000u,0xffffffffu}){
        MasterCartsHost h;h.functions[0].type=type;h.functions[0].ammo=static_cast<int8_t>(index);h.hasWeapon=weapon;
        if(!available)h.weapon.ammo[0]=h.weapon.ammo[1]=-1;h.ammo[0].casing=h.ammo[1].casing=casing;compareMasterCarts(h);}
}
TEST_CASE("Master cartridge selection queries null functions and stops at the first eligible hand", "[gameplay_graph][master_carts]") {
    for(int first:{-1,0,1})for(int second:{-1,0,1}){MasterCartsHost h;h.players[0].funcs[0]=first;h.players[0].funcs[1]=second;compareMasterCarts(h);}
    MasterCartsHost missing;missing.players[0].funcs[0]=missing.players[0].funcs[1]=-1;REQUIRE(missing.run(true)==1);
    const std::vector<int> missingCalls{1,2,1,2,9};REQUIRE(missing.calls==missingCalls);
    MasterCartsHost first;REQUIRE(first.run(true)==0);const std::vector<int> firstCalls{1,2,3,4,5,6,9};REQUIRE(first.calls==firstCalls);REQUIRE(first.players[0].file==123);
    MasterCartsHost second;second.ammo[0].casing=0xffffffffu;REQUIRE(second.run(true)==0);const std::vector<int> secondCalls{1,2,3,1,2,3,4,5,6,9};REQUIRE(second.calls==secondCalls);REQUIRE(second.players[0].file==0);
}
TEST_CASE("Master cartridge selection retains captured writes across synthetic host callbacks", "[gameplay_graph][master_carts]") {
    // Callback stress checks the component boundary; it does not qualify engine lookup/provider behavior.
    for(int current:{0,1})for(int captured:{0,1})for(int callbacks=0;callbacks<16;++callbacks)for(bool cart:{false,true}){
        MasterCartsHost h;h.current=current;h.captured=captured;h.swapFunction=callbacks&1;h.swapWeapon=callbacks&2;h.swapQueue=callbacks&4;h.swapTick=callbacks&8;
        h.cartOnFunction=cart;h.weaponOnFunction=true;compareMasterCarts(h);}
}

struct MasterFinalHost {
    static inline uint8_t memory[32768]{};static inline int saves[8]{};
    static inline int roots[3]{};struct Definition {int tag;void *root;};static inline Definition defs[3]{{1,&roots[0]},{2,&roots[1]},{3,&roots[2]}};
    struct Model {int initialized=0,commands=0;};
    struct Hand {Model gun,hand;void *gunSave=nullptr,*handSave=nullptr;uint8_t *gunCmd=memory+100,*handCmd=memory+200,*matrices=memory+300;int32_t matrixState=19;};
    struct Player {Hand hands[2];Definition *gun=&defs[0],*hand=&defs[1];uint8_t *ptr=memory+2048;uint32_t remaining=8192;uint8_t master=3,gunstate=0;int32_t type=-1,pending=5;}players[2];
    struct Context {MasterFinalHost *world;Player *player;Hand *hand=nullptr;int32_t weapon,sum=0;};
    int current=0,captured=0,weapon=5;int32_t lengths[2][2]{{16,24},{32,40}};bool swapInit=false,swapCommand=false,swapCapacity=false,mutateDefs=false,mutateCursor=false;
    std::vector<int64_t> events;std::vector<int> calls;
    MasterFinalHost(){players[1].ptr=memory+8192;for(int p=0;p<2;++p)for(int h=0;h<2;++h){players[p].hands[h].gunSave=&saves[p*4+h*2];players[p].hands[h].handSave=&saves[p*4+h*2+1];}}
    int offset(void *p){return p?static_cast<int>(static_cast<uint8_t*>(p)-memory):-1;}
    int defid(void *p){if(!p)return 0;for(int i=0;i<3;++i)if(p==&defs[i])return i+1;FAIL("Unknown finalization definition");return -1;}
    int rootid(void *p){for(int i=0;i<3;++i)if(p==&roots[i])return i+1;FAIL("Unknown finalization root");return -1;}
    int saveid(void *p){for(int i=0;i<8;++i)if(p==&saves[i])return i;FAIL("Unknown finalization savedata");return -1;}
    int modelid(Model *m){for(int p=0;p<2;++p)for(int h=0;h<2;++h){if(m==&players[p].hands[h].gun)return p*4+h*2;if(m==&players[p].hands[h].hand)return p*4+h*2+1;}FAIL("Unknown finalization model");return -1;}
    void record(Context &c,int event,int a=0,int b=0,int d=0){calls.push_back(event);events.insert(events.end(),{event,a,b,d,current,captured,c.weapon,c.sum});
        for(auto &p:players){events.insert(events.end(),{defid(p.gun),defid(p.hand),offset(p.ptr),p.remaining,p.master,p.gunstate,p.type,p.pending});
            for(auto &h:p.hands)events.insert(events.end(),{h.gun.initialized,h.gun.commands,h.hand.initialized,h.hand.commands,offset(h.gunCmd),offset(h.handCmd),offset(h.matrices),h.matrixState});}}
    void init(Context &c,Model *model,Definition *definition,void *save){record(c,1,modelid(model),defid(definition),saveid(save));++model->initialized;
        if(mutateDefs){c.player->hand=c.player->hand?nullptr:&defs[1];c.player->gun=&defs[2];}if(swapInit)current=1-current;}
    int32_t commands(Context &c,Model *model,void *root,void *cursor){int id=modelid(model);record(c,2,id,rootid(root),offset(cursor));++model->commands;
        if(mutateDefs)c.player->hand=c.player->hand?nullptr:&defs[1];if(mutateCursor){c.player->ptr+=64;c.player->remaining^=32u;}if(swapCommand)current=1-current;return lengths[(id%4)/2][id%2];}
    uint32_t capacity(Context &c){record(c,3);if(swapCapacity){current=1-current;c.player->master=255;c.player->type=INT32_MIN;c.player->pending=INT32_MAX;}return 12345u;}
    void log(Context &c){record(c,4);}
    void advance(Context &c,int32_t value){c.sum+=value;c.player->ptr+=value;c.player->remaining-=value;}
    int native(Context &c){c.sum=0;for(int i=0;i<2;++i){c.hand=&c.player->hands[i];init(c,&c.hand->gun,c.player->gun,c.hand->gunSave);
        if(c.player->hand)init(c,&c.hand->hand,c.player->hand,c.hand->handSave);
        c.hand->gunCmd=c.player->ptr;int32_t value=commands(c,&c.hand->gun,c.player->gun->root,c.player->ptr);advance(c,value);
        if(c.player->hand){c.hand->handCmd=c.player->ptr;value=commands(c,&c.hand->hand,c.player->hand->root,c.player->ptr);advance(c,value);}}
        c.hand=&c.player->hands[0];c.hand->matrixState=-1;if(c.player->remaining>3200u){c.hand->matrices=c.player->ptr;c.player->ptr+=3200;c.player->remaining-=3200u;}else c.hand->matrices=nullptr;
        capacity(c);c.player->master=4;c.player->type=c.weapon;c.player->pending=-1;if(current==0)log(c);return 1;}
    static ggv_value read(void *p,ggv_binding binding){auto &c=*static_cast<Context*>(p);ggv_value v{};
        switch(binding){case GGV_FINAL_SUM:v.i=c.sum;break;case GGV_MASTER_WEAPON:v.i=c.weapon;break;case GGV_SW_PLAYER_NUM:v.i=c.world->current;break;
        case GGV_FINAL_GUN_MODEL:v.ref=&c.hand->gun;break;case GGV_FINAL_HAND_MODEL:v.ref=&c.hand->hand;break;case GGV_FINAL_GUN_DEF:v.ref=c.player->gun;break;
        case GGV_FLUX_HAND_MODEL:v.ref=c.player->hand;break;case GGV_FINAL_GUN_SAVE:v.ref=c.hand->gunSave;break;case GGV_FINAL_HAND_SAVE:v.ref=c.hand->handSave;break;
        case GGV_FINAL_GUN_ROOT:v.ref=c.player->gun->root;break;case GGV_FINAL_HAND_ROOT:v.ref=c.player->hand->root;break;
        case GGV_MASTER_MEM_PTR:v.ref=c.player->ptr;break;case GGV_MASTER_MEM_REMAINING:v.u=c.player->remaining;break;case GGV_FINAL_MATRIX_BYTES:v.u=3200u;break;
        default:FAIL("Unexpected master finalization read");}return v;}
    static void write(void *p,ggv_binding binding,ggv_value v){auto &c=*static_cast<Context*>(p);
        switch(binding){case GGV_FINAL_HAND_INDEX:c.hand=&c.player->hands[v.i];break;case GGV_FINAL_SUM:c.sum=v.i;break;
        case GGV_FINAL_GUN_COMMAND:c.hand->gunCmd=static_cast<uint8_t*>(v.ref);break;case GGV_FINAL_HAND_COMMAND:c.hand->handCmd=static_cast<uint8_t*>(v.ref);break;
        case GGV_FINAL_MATRIX_STATE:c.hand->matrixState=v.i;break;case GGV_FINAL_MATRIX_PTR:c.hand->matrices=static_cast<uint8_t*>(v.ref);break;
        case GGV_MASTER_MEM_PTR:c.player->ptr=static_cast<uint8_t*>(v.ref);break;case GGV_MASTER_MEM_REMAINING:c.player->remaining=v.u;break;
        case GGV_LOADED_MASTERLOADSTATE:c.player->master=static_cast<uint8_t>(v.i);break;case GGV_LOADED_GUNMEMTYPE:c.player->type=v.i;break;
        case GGV_LOADED_GUNMEMNEW:c.player->pending=v.i;break;default:FAIL("Unexpected master finalization write");}}
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value b,ggv_value d){auto &c=*static_cast<Context*>(p);auto &h=*c.world;ggv_value v{};
        switch(service){case GGV_FINAL_MODEL_INIT:h.init(c,static_cast<Model*>(a.ref),static_cast<Definition*>(b.ref),d.ref);break;
        case GGV_FINAL_COMMANDS:v.i=h.commands(c,static_cast<Model*>(a.ref),b.ref,d.ref);break;case GGV_FINAL_PTR_OFFSET:v.ref=static_cast<uint8_t*>(a.ref)+b.i;break;
        case GGV_MASTER_CAPACITY:v.u=h.capacity(c);break;case GGV_FINAL_LOG:h.log(c);break;default:FAIL("Unexpected master finalization service");}return v;}
    int run(bool graph){Context c{this,&players[captured],nullptr,weapon};int32_t result=123;
        if(graph){ggv_host host{&c,read,write,call};REQUIRE(ggvExecute(ggvStockProgram(GGV_MASTER_FINALIZE),&host,&result));}else result=native(c);record(c,9,result);return result;}
};
void compareMasterFinal(MasterFinalHost input){auto expected=input,actual=input;int e=expected.run(false),a=actual.run(true);REQUIRE(a==e);REQUIRE(actual.calls==expected.calls);REQUIRE(actual.events==expected.events);}
TEST_CASE("Master finalization orders both hands and accounts for command storage", "[gameplay_graph][master_finalize]") {
    for(bool hands:{false,true})for(int size:{0,8,64}){MasterFinalHost h;if(!hands)h.players[0].hand=nullptr;for(auto &row:h.lengths)for(auto &v:row)v=size;compareMasterFinal(h);}
    MasterFinalHost h;REQUIRE(h.run(true)==1);const std::vector<int> calls{1,1,2,2,1,1,2,2,3,4,9};REQUIRE(h.calls==calls);
    REQUIRE(h.offset(h.players[0].hands[0].gunCmd)==2048);REQUIRE(h.offset(h.players[0].hands[0].handCmd)==2064);
    REQUIRE(h.offset(h.players[0].hands[1].gunCmd)==2088);REQUIRE(h.offset(h.players[0].hands[1].handCmd)==2120);
    REQUIRE(h.offset(h.players[0].hands[0].matrices)==2160);REQUIRE(h.offset(h.players[0].ptr)==5360);REQUIRE(h.players[0].remaining==4880u);
    REQUIRE(h.players[0].hands[0].matrixState==-1);REQUIRE(h.players[0].hands[1].matrixState==19);
}
TEST_CASE("Master finalization uses strict unsigned matrix-space boundary", "[gameplay_graph][master_finalize]") {
    // Zero-byte command results isolate the unsigned scalar gate; high capacities do not qualify real allocator storage.
    for(uint32_t remaining:{0u,3199u,3200u,3201u,0x7fffffffu,0x80000000u,0xffffffffu}){
        MasterFinalHost h;for(auto &row:h.lengths)for(auto &v:row)v=0;h.players[0].remaining=remaining;compareMasterFinal(h);
        REQUIRE(h.run(true)==1);REQUIRE((h.players[0].hands[0].matrices!=nullptr)==(remaining>3200u));}
}
TEST_CASE("Master finalization keeps captured targets and rereads after synthetic callbacks", "[gameplay_graph][master_finalize]") {
    // This component stress does not qualify modelInit, command writer, capacity helper, or allocator implementations.
    for(int current:{0,1})for(int captured:{0,1})for(int flags=0;flags<32;++flags)for(bool hands:{false,true}){
        MasterFinalHost h;h.current=current;h.captured=captured;if(!hands)h.players[captured].hand=nullptr;h.swapInit=flags&1;h.swapCommand=flags&2;
        h.swapCapacity=flags&4;h.mutateDefs=flags&8;h.mutateCursor=flags&16;compareMasterFinal(h);}
}
TEST_CASE("Master finalization stores full-width weapon state after capacity callback", "[gameplay_graph][master_finalize]") {
    // Scalar storage qualification only; these request values do not establish asset lookup eligibility.
    for(int weapon:{0,1,109,INT32_MAX})for(bool swap:{false,true}){MasterFinalHost h;h.weapon=weapon;h.swapCapacity=swap;compareMasterFinal(h);
        REQUIRE(h.run(true)==1);REQUIRE(h.players[0].master==4);REQUIRE(h.players[0].type==weapon);REQUIRE(h.players[0].pending==-1);}
}

struct MasterParentHost {
    struct Player {uint8_t master=0;int32_t type=-1,pending=5;}players[2];
    struct Context {MasterParentHost *world;Player *player;int32_t weapon=0;uint16_t file=0,handfile=0;bool hands=false,diag=false;};
    int current=0,weapon=5,last=-1;uint16_t file=456,handfile=123;bool ready=true,prepared=true,flag=true,runtime=false,modern=true,childOK=true,swapPrepare=false,swapLog=false,changeLog=false;
    std::vector<int> calls;std::vector<int64_t> events;
    void record(Context &c,int e){calls.push_back(e);events.insert(events.end(),{e,current,last,c.weapon,c.file,c.handfile,c.hands,c.diag});for(auto &p:players)events.insert(events.end(),{p.master,p.type,p.pending});}
    bool admission(Context &c){record(c,1);return ready;}
    bool prepare(Context &c){record(c,2);c.weapon=weapon;c.file=file;c.handfile=handfile;if(swapPrepare)current=1-current;return prepared;}
    bool hasHands(Context &c){record(c,3);return flag;}// Pure lookup: no mutation of other unsequenced native log arguments.
    void entry(Context &c){record(c,4);if(swapLog)current=1-current;if(changeLog){c.player->master=3;c.player->type=-1;}}
    bool model(Context &c){record(c,5);return runtime;}
    bool child(Context &c,int e){record(c,e);c.player->master=static_cast<uint8_t>(c.player->master+1);return childOK;}
    int native(Context &c){if(admission(c)){if(!prepare(c))return 1;
        if(current==0){int key=(c.weapon<<8)|c.player->master;if(key!=last){last=key;c.diag=hasHands(c);entry(c);}}
        if(c.player->master!=4 || c.weapon!=c.player->type){if(c.file || model(c)){c.hands=false;if(c.handfile && hasHands(c))c.hands=true;
            if(c.weapon==1){c.file=c.handfile;c.handfile=0;c.hands=false;}
            if(c.player->master==0)child(c,10);else if(c.player->master==1){if(!child(c,11))return 1;}
            else if(c.player->master==2){if(!child(c,12))return 1;}else if(c.player->master==3){if(!child(c,13))return 1;child(c,14);}}
            else if(modern){c.player->master=4;c.player->type=c.weapon;c.player->pending=-1;if(current==0)record(c,15);}}}return 1;}
    static ggv_value read(void *p,ggv_binding b){auto &c=*static_cast<Context*>(p);auto &h=*c.world;ggv_value v{};
        switch(b){case GGV_SW_PLAYER_NUM:v.i=h.current;break;case GGV_MASTER_WEAPON:v.i=c.weapon;break;case GGV_MASTER_FILE:v.i=c.file;break;
        case GGV_MASTER_HANDFILE:v.i=c.handfile;break;case GGV_PARENT_LAST_KEY:v.i=h.last;break;case GGV_LOADED_MASTERLOADSTATE:v.i=c.player->master;break;
        case GGV_LOADED_GUNMEMTYPE:v.i=c.player->type;break;case GGV_VERSION_1_OR_LATER:v.b=h.modern;break;default:FAIL("Unexpected master parent read");}return v;}
    static void write(void *p,ggv_binding b,ggv_value v){auto &c=*static_cast<Context*>(p);
        switch(b){case GGV_MASTER_FILE:c.file=static_cast<uint16_t>(v.i);break;case GGV_MASTER_HANDFILE:c.handfile=static_cast<uint16_t>(v.i);break;
        case GGV_MASTER_HAS_HANDS_INPUT:c.hands=v.b;break;case GGV_PARENT_DIAG_HANDS:c.diag=v.b;break;case GGV_PARENT_LAST_KEY:c.world->last=v.i;break;
        case GGV_LOADED_MASTERLOADSTATE:c.player->master=static_cast<uint8_t>(v.i);break;case GGV_LOADED_GUNMEMTYPE:c.player->type=v.i;break;
        case GGV_LOADED_GUNMEMNEW:c.player->pending=v.i;break;default:FAIL("Unexpected master parent write");}}
    static ggv_value call(void *p,ggv_service service,ggv_value a,ggv_value,ggv_value){auto &c=*static_cast<Context*>(p);auto &h=*c.world;ggv_value v{};
        switch(service){case GGV_PARENT_READY:v.b=h.admission(c);break;case GGV_PARENT_PREPARE:v.b=h.prepare(c);break;
        case GGV_MASTER_HAS_HANDS:REQUIRE(a.i==c.weapon);v.b=h.hasHands(c);break;case GGV_MASTER_RUNTIME_MODEL:REQUIRE(a.i==c.weapon);v.ref=h.model(c)?&h:nullptr;break;
        case GGV_PARENT_ENTRY_LOG:h.entry(c);break;case GGV_PARENT_FLUX:h.child(c,10);break;case GGV_PARENT_HANDS:v.b=h.child(c,11);break;
        case GGV_PARENT_GUN:v.b=h.child(c,12);break;case GGV_PARENT_CARTS:v.b=h.child(c,13);break;case GGV_PARENT_FINALIZE:h.child(c,14);break;
        case GGV_PARENT_SHORTCUT_LOG:h.record(c,15);break;default:FAIL("Unexpected master parent service");}return v;}
    void run(bool graph){Context c{this,&players[current]};int32_t result=123;if(graph){ggv_host host{&c,read,write,call};REQUIRE(ggvExecute(ggvStockProgram(GGV_MASTER_LOAD_PARENT),&host,&result));}else result=native(c);record(c,19);REQUIRE(result==1);}
};
void compareMasterParent(MasterParentHost input,int repeat=1){auto expected=input,actual=input;for(int i=0;i<repeat;++i){expected.run(false);actual.run(true);}REQUIRE(actual.calls==expected.calls);REQUIRE(actual.events==expected.events);}
TEST_CASE("Master parent preserves admission preparation and exclusive stage dispatch", "[gameplay_graph][master_parent]") {
    for(bool ready:{false,true})for(bool prepared:{false,true})for(int state:{0,1,2,3,4,5,255})for(bool ok:{false,true}){
        MasterParentHost h;h.ready=ready;h.prepared=prepared;h.players[0].master=static_cast<uint8_t>(state);h.childOK=ok;compareMasterParent(h);}
    MasterParentHost h;h.run(true);const std::vector<int> expected{1,2,3,4,3,10,19};REQUIRE(h.calls==expected);
}
TEST_CASE("Master parent preserves file runtime and hands short circuits and unarmed adaptation", "[gameplay_graph][master_parent]") {
    for(int weapon:{1,5})for(int file:{0,65535})for(int handfile:{0,65535})for(bool runtime:{false,true})for(bool flag:{false,true})for(bool modern:{false,true}){
        MasterParentHost h;h.weapon=weapon;h.file=static_cast<uint16_t>(file);h.handfile=static_cast<uint16_t>(handfile);h.runtime=runtime;h.flag=flag;h.modern=modern;compareMasterParent(h);}
}
TEST_CASE("Master parent keeps persistent tuple diagnostics and loaded weapon gate", "[gameplay_graph][master_parent]") {
    // Native-defined signed shift domain only; no claim for invalid weapon lookup values.
    for(int current:{0,1})for(int state:{0,4,255})for(bool same:{false,true})for(bool key:{false,true}){
        MasterParentHost h;h.current=current;h.players[current].master=static_cast<uint8_t>(state);h.players[current].type=same?h.weapon:-1;if(key)h.last=(h.weapon<<8)|state;compareMasterParent(h,3);}
}
TEST_CASE("Master parent retains entry player through prepare and diagnostic callbacks", "[gameplay_graph][master_parent]") {
    // Scripted child boundaries exercise parent control; they do not establish nested graph or provider acceptance.
    for(int current:{0,1})for(bool prep:{false,true})for(bool log:{false,true})for(bool change:{false,true})for(bool child:{false,true}){
        MasterParentHost h;h.current=current;h.swapPrepare=prep;h.swapLog=log;h.changeLog=change;h.childOK=child;compareMasterParent(h);}
}

struct GunFailureHost {
    struct Player {uint32_t generation=7;uint16_t file=65535;uint8_t state=5;}players[2];
    struct Context {GunFailureHost *world;Player *player;};
    int current=0;uint32_t generation=7;bool swapQuery=false,swapLog=false,mutateLog=false;std::vector<int64_t> events;
    void record(int e,uint32_t g){events.insert(events.end(),{e,g,current});for(auto &p:players)events.insert(events.end(),{p.generation,p.file,p.state});}
    uint32_t query(){record(1,generation);if(swapQuery)current=1-current;return generation;}
    void log(Context &c,uint32_t g){record(2,g);if(swapLog)current=1-current;if(mutateLog){c.player->generation=g;c.player->state=255;}}
    static ggv_value read(void *p,ggv_binding b){auto &c=*static_cast<Context*>(p);ggv_value v{};REQUIRE(b==GGV_MASTER_LOAD_GENERATION);v.u=c.player->generation;return v;}
    static void write(void *p,ggv_binding b,ggv_value v){auto &c=*static_cast<Context*>(p);REQUIRE(b==GGV_MEM_GUNLOADSTATE);c.player->state=static_cast<uint8_t>(v.i);}
    static ggv_value call(void *p,ggv_service s,ggv_value a,ggv_value,ggv_value){auto &c=*static_cast<Context*>(p);ggv_value v{};
        if(s==GGV_MASTER_CATALOG_GENERATION)v.u=c.world->query();else if(s==GGV_GUN_FAILURE_LOG)c.world->log(c,a.u);else FAIL("Unexpected failure service");return v;}
    void run(bool graph){Context c{this,&players[current]};if(graph){ggv_host host{&c,read,write,call};int32_t result;REQUIRE(ggvExecute(ggvStockProgram(GGV_GUN_FAILURE_RECOVERY),&host,&result));REQUIRE(result==1);}
        else {uint32_t g=query();if(g!=c.player->generation){log(c,g);c.player->state=0;}}record(9,generation);}
};
void compareGunFailure(GunFailureHost input){auto expected=input,actual=input;expected.run(false);actual.run(true);REQUIRE(actual.events==expected.events);}
TEST_CASE("Gun failure recovery compares all catalog generation bits before resetting", "[gameplay_graph][gun_failure]") {
    for(uint32_t old:{0u,7u,0x80000000u,0xffffffffu})for(uint32_t next:{0u,7u,0x80000000u,0xffffffffu}){
        GunFailureHost h;h.players[0].generation=old;h.generation=next;compareGunFailure(h);h.run(true);REQUIRE(h.players[0].state==(old==next?5:0));REQUIRE(h.players[0].generation==old);}
}
TEST_CASE("Gun failure recovery keeps captured player through query and log callbacks", "[gameplay_graph][gun_failure]") {
    for(int current:{0,1})for(bool query:{false,true})for(bool log:{false,true})for(bool mutate:{false,true}){
        GunFailureHost h;h.current=current;h.generation=0xffffffffu;h.swapQuery=query;h.swapLog=log;h.mutateLog=mutate;compareGunFailure(h);}
}

struct GunRetainedHost {
    static inline int models[3]{};struct Generation {int index;};static inline Generation generations[3]{{0},{1},{2}};
    struct Player {void *retained=&models[0],*slots[2]{&models[1],&models[2]};int target=0;uint32_t texture=0xffffffffu,loaded=0x80000000u,allocated=37;uint8_t state=1;char catalog[64]="model";}players[2];
    struct Context {GunRetainedHost *world;Player *player;};
    int current=0;bool generationOK=true,idOK=true,swapGeneration=false,swapAllocate=false,redirect=false,replaceRetained=false;const char *sourceId="model";
    std::vector<int64_t> events;std::vector<std::string> strings;
    int modelid(void *p){if(!p)return -1;for(int i=0;i<3;++i)if(p==&models[i])return i;FAIL("Unknown retained model");return -2;}
    int genid(void *p){if(!p)return -1;for(int i=0;i<3;++i)if(p==&generations[i])return i;FAIL("Unknown retained generation");return -2;}
    void record(int e,int arg=0){events.insert(events.end(),{e,arg,current});for(auto &p:players){events.insert(events.end(),{modelid(p.retained),modelid(p.slots[0]),modelid(p.slots[1]),p.target,p.texture,p.loaded,p.allocated,p.state});strings.emplace_back(p.catalog);}}
    void *generation(void *model){int i=modelid(model);record(1,i);if(swapGeneration)current=1-current;return generationOK?&generations[i]:nullptr;}
    const char *id(void *g){record(2,genid(g));return g && idOK?sourceId:nullptr;}
    int compare(Context &c,const char *id){record(3);return std::strcmp(id,c.player->catalog);}
    void allocate(Context &c,void *model){record(4,modelid(model));if(redirect)c.player->target=1;if(replaceRetained)c.player->retained=&models[2];if(swapAllocate)current=1-current;}
    const char *hash(void *g){record(5,genid(g));static const char *hashes[]{"ha","hb","hc"};return hashes[genid(g)];}
    void log(const char *id,const char *hash){record(6);strings.emplace_back(id);strings.emplace_back(hash);}
    void fail(Context &c){record(7);if(c.player->target>=0)c.player->slots[c.player->target]=nullptr;c.player->retained=nullptr;c.player->state=5;}
    void native(Context &c){void *model=c.player->retained;void *g=generation(model);const char *source=id(g);
        if(c.player->target<0 || !source || compare(c,source)){fail(c);return;}allocate(c,model);
        c.player->slots[c.player->target]=model;record(10);c.player->retained=nullptr;record(11);c.player->texture=0;record(12);
        c.player->allocated=0;record(13);c.player->loaded=0;record(14);c.player->state=4;record(15);log(source,hash(g));}
    static ggv_value read(void *p,ggv_binding b){auto &c=*static_cast<Context*>(p);ggv_value v{};
        if(b==GGV_GL_RETAINED_MODEL)v.ref=c.player->retained;else if(b==GGV_GL_TARGET)v.ref=c.player->target<0?nullptr:&c.player->slots[c.player->target];else FAIL("Unexpected retained read");return v;}
    static void write(void *p,ggv_binding b,ggv_value v){auto &c=*static_cast<Context*>(p);auto &h=*c.world;
        switch(b){case GGV_GL_TARGET_MODEL:c.player->slots[c.player->target]=v.ref;h.record(10);break;case GGV_GL_RETAINED_MODEL:c.player->retained=v.ref;h.record(11);break;
        case GGV_GL_NEXT_TEXTURE:c.player->texture=v.u;h.record(12);break;case GGV_GL_ALLOC_SIZE:c.player->allocated=v.u;h.record(13);break;
        case GGV_GL_LOADED_SIZE:c.player->loaded=v.u;h.record(14);break;case GGV_MEM_GUNLOADSTATE:c.player->state=static_cast<uint8_t>(v.i);h.record(15);break;default:FAIL("Unexpected retained write");}}
    static ggv_value call(void *p,ggv_service s,ggv_value a,ggv_value b,ggv_value){auto &c=*static_cast<Context*>(p);auto &h=*c.world;ggv_value v{};
        switch(s){case GGV_GL_GENERATION:v.ref=h.generation(a.ref);break;case GGV_GL_GENERATION_ID:v.ref=const_cast<char*>(h.id(a.ref));break;
        case GGV_GL_COMPARE_ID:v.i=h.compare(c,static_cast<const char*>(a.ref));break;case GGV_GL_FAIL:h.fail(c);break;case GGV_GL_ALLOCATE_RW:h.allocate(c,a.ref);break;
        case GGV_GL_GENERATION_HASH:v.ref=const_cast<char*>(h.hash(a.ref));break;case GGV_GL_RETAINED_LOG:h.log(static_cast<const char*>(a.ref),static_cast<const char*>(b.ref));break;default:FAIL("Unexpected retained service");}return v;}
    void run(bool graph){Context c{this,&players[current]};if(graph){ggv_host host{&c,read,write,call};int32_t result;REQUIRE(ggvExecute(ggvStockProgram(GGV_GUN_RETAINED_LOAD),&host,&result));REQUIRE(result==1);}else native(c);record(19);}
};
void compareGunRetained(GunRetainedHost input){auto expected=input,actual=input;expected.run(false);actual.run(true);REQUIRE(actual.events==expected.events);REQUIRE(actual.strings==expected.strings);}
TEST_CASE("Retained gun load queries identity before short circuiting destination and source checks", "[gameplay_graph][gun_retained]") {
    for(bool target:{false,true})for(bool generation:{false,true})for(bool id:{false,true})for(const char *source:{"","aaa","model","zzz"}){
        GunRetainedHost h;if(!target)h.players[0].target=-1;h.generationOK=generation;h.idOK=id;h.sourceId=source;compareGunRetained(h);}
    GunRetainedHost empty;empty.sourceId="";empty.players[0].catalog[0]=0;compareGunRetained(empty);
}
TEST_CASE("Retained gun load publishes the captured model to the refreshed destination", "[gameplay_graph][gun_retained]") {
    // Synthetic callbacks qualify the component boundary, not generation/provider or model allocator implementations.
    for(int current:{0,1})for(bool generation:{false,true})for(bool allocation:{false,true})for(bool redirect:{false,true})for(bool replace:{false,true}){
        GunRetainedHost h;h.current=current;h.swapGeneration=generation;h.swapAllocate=allocation;h.redirect=redirect;h.replaceRetained=replace;compareGunRetained(h);}
}
TEST_CASE("Retained gun load clears full-width counters before hash logging", "[gameplay_graph][gun_retained]") {
    for(int model=0;model<3;++model){GunRetainedHost h;h.players[0].retained=&GunRetainedHost::models[model];compareGunRetained(h);h.run(true);
        REQUIRE(h.players[0].slots[0]==&GunRetainedHost::models[model]);REQUIRE(h.players[0].slots[1]==&GunRetainedHost::models[2]);
        REQUIRE(h.players[0].retained==nullptr);REQUIRE(h.players[0].texture==0u);REQUIRE(h.players[0].allocated==0u);REQUIRE(h.players[0].loaded==0u);REQUIRE(h.players[0].state==4);}
}

struct GunSourceHost {
    static inline int providers[2]{},modelToken;
    struct Handle {void *provider=&providers[0];uint64_t a=0xffffffffffffffffull,b=0x8000000000000001ull;};
    struct Player {uint8_t state=1;int32_t body=1;uint16_t file=65535,handfile=7;uint32_t generation=0xffffffffu,handgeneration=19;Handle load,hand;char loadId[64],handId[64];}players[2];
    struct Context {GunSourceHost *world;Player *player;};
    int current=0;uint8_t loadType=41;bool valid=true,external=true,loadOK=true,swapValid=false,swapExternal=false,swapLoad=false,mutateLoad=false;
    std::vector<uint64_t> events;
    GunSourceHost(){for(auto &p:players){std::memset(p.loadId,'!',64);std::strcpy(p.loadId,"model");std::memset(p.handId,'*',64);std::strcpy(p.handId,"old");p.hand.provider=&providers[1];p.hand.a=3;p.hand.b=9;}}
    int provider(void *p){if(!p)return -1;for(int i=0;i<2;++i)if(p==&providers[i])return i;FAIL("Unknown source provider");return -2;}
    void record(int e){events.push_back(e);events.push_back(current);events.push_back(loadType);
        for(auto &p:players){events.push_back(p.state);events.push_back(static_cast<uint32_t>(p.body));events.push_back(p.file);events.push_back(p.handfile);events.push_back(p.generation);events.push_back(p.handgeneration);
            for(auto *h:{&p.load,&p.hand}){events.push_back(static_cast<uint64_t>(provider(h->provider)));events.push_back(h->a);events.push_back(h->b);}
            for(unsigned char c:p.loadId)events.push_back(c);for(unsigned char c:p.handId)events.push_back(c);}}
    bool canUse(){record(1);if(swapValid)current=1-current;return valid;}
    bool usesExternal(){record(2);if(swapExternal)current=1-current;return external;}
    void *load(Context &c){record(3);if(swapLoad)current=1-current;if(mutateLoad){c.player->body=!c.player->body;c.player->file=42;c.player->generation=0x80000000u;std::strcpy(c.player->loadId,"new");c.player->load.provider=&providers[1];c.player->load.a=11;c.player->load.b=0xffffffffffffffffull;}return loadOK?&modelToken:nullptr;}
    void *fileProvider(){record(4);return &providers[0];}
    void fail(Context &c){record(5);c.player->state=5;}
    void copyId(Context &c){std::strcpy(c.player->handId,c.player->loadId);record(12);}
    void copyHandle(Context &c){c.player->hand=c.player->load;record(13);}
    int native(Context &c){if(!canUse()){fail(c);return 1;}if(usesExternal()){void *model=load(c);if(!model){loadType=0;record(9);fail(c);return 1;}
        c.player->state=4;record(10);if(c.player->body){c.player->handfile=c.player->file;record(11);copyId(c);copyHandle(c);c.player->handgeneration=c.player->generation;record(14);}return 1;}
        fail(c);return 1;}
    static ggv_value read(void *p,ggv_binding b){auto &c=*static_cast<Context*>(p);ggv_value v{};
        switch(b){case GGV_GL_BODY_HAND:v.b=c.player->body!=0;break;case GGV_GL_LOAD_FILE:v.i=c.player->file;break;case GGV_GL_PROVIDER:v.ref=c.player->load.provider;break;
        case GGV_MASTER_LOAD_GENERATION:v.u=c.player->generation;break;default:FAIL("Unexpected source route read");}return v;}
    static void write(void *p,ggv_binding b,ggv_value v){auto &c=*static_cast<Context*>(p);auto &h=*c.world;
        switch(b){case GGV_GL_LOAD_TYPE:h.loadType=static_cast<uint8_t>(v.i);h.record(9);break;case GGV_MEM_GUNLOADSTATE:c.player->state=static_cast<uint8_t>(v.i);h.record(10);break;
        case GGV_FLUX_FILE:c.player->handfile=static_cast<uint16_t>(v.i);h.record(11);break;case GGV_FLUX_GENERATION:c.player->handgeneration=v.u;h.record(14);break;default:FAIL("Unexpected source route write");}}
    static ggv_value call(void *p,ggv_service s,ggv_value,ggv_value,ggv_value){auto &c=*static_cast<Context*>(p);auto &h=*c.world;ggv_value v{};
        switch(s){case GGV_GL_CAN_USE_HANDLE:v.b=h.canUse();break;case GGV_GL_EXTERNAL_SOURCE:v.b=h.usesExternal();break;case GGV_GL_CATALOG_MODEL:v.ref=h.load(c);break;
        case GGV_GL_FILE_PROVIDER:v.ref=h.fileProvider();break;case GGV_GL_FAIL:h.fail(c);break;case GGV_GL_COPY_HAND_ID:h.copyId(c);break;case GGV_GL_COPY_HAND_HANDLE:h.copyHandle(c);break;
        default:FAIL("Unexpected source route service");}return v;}
    int run(bool graph){Context c{this,&players[current]};int32_t result=123;if(graph){ggv_host host{&c,read,write,call};REQUIRE(ggvExecute(ggvStockProgram(GGV_GUN_SOURCE_ROUTE),&host,&result));}else result=native(c);record(19);return result;}
};
void compareGunSource(GunSourceHost input){auto expected=input,actual=input;int e=expected.run(false),a=actual.run(true);REQUIRE(a==e);REQUIRE(actual.events==expected.events);}
TEST_CASE("Gun source routing keeps early failures and external or file-provider gates", "[gameplay_graph][gun_source]") {
    for(bool valid:{false,true})for(bool external:{false,true})for(bool loaded:{false,true})for(int provider:{-1,0,1}){
        GunSourceHost h;h.valid=valid;h.external=external;h.loadOK=loaded;h.players[0].load.provider=provider<0?nullptr:&GunSourceHost::providers[provider];compareGunSource(h);}
    GunSourceHost unsupported;unsupported.external=false;unsupported.players[0].load.provider=&GunSourceHost::providers[1];REQUIRE(unsupported.run(true)==1);REQUIRE(unsupported.players[0].state==5);
}
TEST_CASE("Gun source routing copies complete typed hand identity after LOADED", "[gameplay_graph][gun_source]") {
    for(int body:{-1,0,1,2})for(int file:{0,65535})for(uint32_t generation:{0u,0x80000000u,0xffffffffu})for(const auto &id:std::vector<std::string>{"","model",std::string(63,'x')}){
        GunSourceHost h;h.players[0].body=body;h.players[0].file=static_cast<uint16_t>(file);h.players[0].generation=generation;std::strcpy(h.players[0].loadId,id.c_str());compareGunSource(h);}
}
TEST_CASE("Gun source routing uses captured player and fresh source fields after callbacks", "[gameplay_graph][gun_source]") {
    // Scripted source/provider boundaries do not establish public-source loading or provider acceptance.
    for(int current:{0,1})for(int bits=0;bits<8;++bits)for(bool body:{false,true})for(bool loaded:{false,true}){
        GunSourceHost h;h.current=current;h.swapValid=bits&1;h.swapExternal=bits&2;h.swapLoad=bits&4;h.mutateLoad=true;h.loadOK=loaded;h.players[current].body=body;compareGunSource(h);}
}

struct GunTexturesHost {
    struct Texture {uint64_t word=7;uint8_t mark=19;};
    struct Model {int16_t count=7;int array=0;Texture textures[2][8];}models[2];
    struct Player {int model=0;uint32_t cursor=0;uint8_t state=2;}players[2];
    struct Context {GunTexturesHost *world;Player *player;Model *model=nullptr;int32_t index=0;};
    int current=0;uint64_t limit=65535;bool swapPlayer=false,swapArray=false,swapModel=false,shrink=false;
    std::vector<uint64_t> events;std::vector<int> visits,loads;
    GunTexturesHost(){players[1].model=1;}
    void record(Context &c,int e,int arg=0){events.push_back(e);events.push_back(static_cast<uint32_t>(arg));events.push_back(current);
        for(auto &p:players){events.push_back(p.model);events.push_back(p.cursor);events.push_back(p.state);}for(auto &m:models){events.push_back(static_cast<uint16_t>(m.count));events.push_back(m.array);for(auto &array:m.textures)for(auto &t:array){events.push_back(t.word);events.push_back(t.mark);}}}
    void at(Context &c,int i){visits.push_back(i);record(c,2,i);}
    void load(Context &c){loads.push_back(c.index);record(c,4,c.index);c.model->textures[c.model->array][c.index].word=0x100000007ull;
        if(swapArray)c.model->array=1-c.model->array;if(shrink)c.model->count=static_cast<int16_t>(c.index+1);if(swapModel)c.player->model=1-c.player->model;if(swapPlayer)current=1-current;}
    void native(Context &c){record(c,1);c.model=&models[c.player->model];int n=0;
        for(c.index=std::bit_cast<int32_t>(c.player->cursor);c.index<c.model->count;++c.index){at(c,c.index);
            if(c.model->textures[c.model->array][c.index].word<limit){record(c,3,c.model->count);load(c);c.model->textures[c.model->array][c.index].mark=1;record(c,5);}
            ++n;++c.player->cursor;record(c,6);if(n==3)return;}
        record(c,7);c.player->state=3;record(c,8);}
    static ggv_value read(void *p,ggv_binding b){auto &c=*static_cast<Context*>(p);ggv_value v{};
        switch(b){case GGV_GL_TARGET_MODEL:v.ref=&c.world->models[c.player->model];break;case GGV_GL_NEXT_TEXTURE:v.u=c.player->cursor;break;
        case GGV_GL_TEXTURE_INDEX:v.i=c.index;break;case GGV_GL_TEXTURE_COUNT:v.i=c.model->count;break;case GGV_GL_TEXTURE_WORD:v.i64=std::bit_cast<int64_t>(c.model->textures[c.model->array][c.index].word);break;
        case GGV_GL_TEXTURE_LIMIT:v.i64=static_cast<int64_t>(c.world->limit);break;default:FAIL("Unexpected texture read");}return v;}
    static void write(void *p,ggv_binding b,ggv_value v){auto &c=*static_cast<Context*>(p);auto &h=*c.world;
        switch(b){case GGV_GL_TEXTURE_MODEL:c.model=static_cast<Model*>(v.ref);break;case GGV_GL_TEXTURE_INDEX:c.index=v.i;break;
        case GGV_GL_NEXT_TEXTURE:c.player->cursor=v.u;h.record(c,6);break;case GGV_GL_TEXTURE_MARK:c.model->textures[c.model->array][c.index].mark=static_cast<uint8_t>(v.i);h.record(c,5);break;
        case GGV_MEM_GUNLOADSTATE:c.player->state=static_cast<uint8_t>(v.i);h.record(c,8);break;default:FAIL("Unexpected texture write");}}
    static ggv_value call(void *p,ggv_service s,ggv_value a,ggv_value b,ggv_value){auto &c=*static_cast<Context*>(p);auto &h=*c.world;ggv_value v{};
        switch(s){case GGV_GL_TEXTURE_BEGIN:h.record(c,1);break;case GGV_GL_TEXTURE_AT:h.at(c,a.i);break;
        case GGV_GL_TEXTURE_UNCOMPRESS:REQUIRE(a.i==c.index);h.record(c,3,b.i);break;case GGV_GL_TEXTURE_LOAD:h.load(c);break;case GGV_GL_TEXTURE_DONE:h.record(c,7);break;default:FAIL("Unexpected texture service");}return v;}
    void tick(bool graph){Context c{this,&players[current]};if(graph){ggv_host host{&c,read,write,call};int32_t result;REQUIRE(ggvExecute(ggvStockProgram(GGV_GUN_TEXTURES),&host,&result));REQUIRE(result==1);}else native(c);record(c,19);}
};
void compareGunTextures(GunTexturesHost input,int ticks=1){auto expected=input,actual=input;for(int i=0;i<ticks;++i){if(expected.players[expected.current].state==2)expected.tick(false);if(actual.players[actual.current].state==2)actual.tick(true);}REQUIRE(actual.events==expected.events);REQUIRE(actual.visits==expected.visits);REQUIRE(actual.loads==expected.loads);}
TEST_CASE("Gun texture batches visit each entry once across cursor budget boundaries", "[gameplay_graph][gun_textures]") {
    for(int count:{0,1,2,3,4,5,6,7})for(bool mixed:{false,true}){GunTexturesHost h;h.models[0].count=static_cast<int16_t>(count);
        if(mixed)for(int i=0;i<count;++i)if(i%2)h.models[0].textures[0][i].word=0x100000007ull;compareGunTextures(h,4);
        for(int tick=0;tick<4 && h.players[0].state==2;++tick)h.tick(true);std::vector<int> visits;for(int i=0;i<count;++i)visits.push_back(i);
        REQUIRE(h.visits==visits);REQUIRE(h.players[0].cursor==static_cast<uint32_t>(count));REQUIRE(h.players[0].state==3);}
    GunTexturesHost exact;exact.models[0].count=3;exact.tick(true);REQUIRE(exact.players[0].cursor==3u);REQUIRE(exact.players[0].state==2);exact.tick(true);REQUIRE(exact.players[0].state==3);REQUIRE(exact.visits.size()==3);
}
TEST_CASE("Gun texture cutoff compares full unsigned pointer-width words", "[gameplay_graph][gun_textures]") {
    for(uint64_t word:{0ull,65534ull,65535ull,65536ull,0x100000007ull,0x7fffffffffffffffull,0x8000000000000007ull,0xffffffffffffffffull}){
        GunTexturesHost h;h.models[0].count=1;h.models[0].textures[0][0].word=word;compareGunTextures(h);h.tick(true);REQUIRE(h.loads.size()==(word<h.limit?1u:0u));}
    for(int count:{-1,0,7}){GunTexturesHost h;h.models[0].count=static_cast<int16_t>(count);h.players[0].cursor=INT32_MAX;compareGunTextures(h);}
}
TEST_CASE("Gun textures retain captured model and reread texture arrays after callbacks", "[gameplay_graph][gun_textures]") {
    // Valid array-index domain; synthetic callbacks do not qualify texLoad/provider behavior.
    for(int current:{0,1})for(int flags=0;flags<16;++flags){GunTexturesHost h;h.current=current;h.swapPlayer=flags&1;h.swapArray=flags&2;h.swapModel=flags&4;h.shrink=flags&8;compareGunTextures(h);}
}

TEST_CASE("Gun source routing rejects unsupported providers without legacy continuation", "[gameplay_graph][gun_source]") {
    for(int provider:{-1,0,1}){GunSourceHost h;h.external=false;h.players[0].load.provider=provider<0?nullptr:&GunSourceHost::providers[provider];
        REQUIRE(h.run(true)==1);REQUIRE(h.players[0].state==5);REQUIRE(h.loadType==41);}
}

struct Host {
    native_shooting_case state{};
    std::vector<ggv_binding> reads, writes;
    std::vector<int> events;
    static ggv_value read(void *opaque,ggv_binding b) {
        auto &h=*static_cast<Host *>(opaque);auto &s=h.state;
        h.reads.push_back(b);h.events.push_back(b);ggv_value v{};
        switch(b) {
        case GGV_FUNCTION_FLAGS:v.u=s.graph_present?s.flags:s.legacy_flags;break;
        case GGV_AMMO_INDEX:v.i=s.ammoindex;break;
        case GGV_AUTOMATIC:v.b=(s.graph_present&&s.automatic)||((s.functype&0xff00)==0x100);break;
        case GGV_ACCELERATION:v.i=s.graph_present&&s.graph_accel?s.turretaccel:((s.functype&0xff00)==0x100?int8_t(s.legacy_accel):0);break;
        case GGV_DECELERATION:v.i=s.graph_present&&s.graph_decel?s.turretdecel:((s.functype&0xff00)==0x100?int8_t(s.legacy_decel):0);break;
        case GGV_TRIGGER:v.b=s.trigger;break;
        case GGV_STATE_FLAGS:v.u=s.stateflags;break;
        case GGV_BURST_BULLETS:v.i=s.burstbullets;break;
        case GGV_AIM:v.b=s.aim;break;
        case GGV_LOADED_AMMO: REQUIRE(s.ammoindex>=0);v.i=s.ammoindex?s.loadedammo1:s.loadedammo;break;
        case GGV_AMMO_TYPE: REQUIRE(s.ammoindex>=0);REQUIRE((s.ammoindex?s.loadedammo1:s.loadedammo)==0);v.i=s.ammoindex?s.ammotype1:s.ammotype;break;
        case GGV_SHOTGUN:v.b=s.shotgun;break;
        case GGV_SHOTGUN_DELAY:v.i=s.shotgundelay;break;
        case GGV_LASER:v.b=s.laser;break;
        case GGV_INITIAL_RPM:v.f=s.graph_present&&s.graph_initial?s.initialrpm:((s.functype&0xff00)==0x100?s.legacy_initialrpm:0);break;
        case GGV_MAX_RPM:v.f=s.graph_present&&s.automatic?s.maxrpm:((s.functype&0xff00)==0x100?s.legacy_maxrpm:0);break;
        case GGV_CLOCK:++s.clock_reads;v.f=s.clock;break;
        case GGV_SPOOL:v.f=s.spool;break;
        case GGV_STATE_FRAMES:v.i=s.stateframes;break;
        case GGV_SHOTS:v.i=s.shots;break;
        case GGV_REMAINDER:v.f=s.remainder;break;
        case GGV_FIRING:v.b=s.firing;break;
        case GGV_BINDING_COUNT:FAIL("Invalid binding reached host");break;
        }
        return v;
    }
    static void write(void *opaque,ggv_binding b,ggv_value v) {
        auto &h=*static_cast<Host *>(opaque);h.writes.push_back(b);h.events.push_back(128+b);
        switch(b) {
        case GGV_SPOOL:h.state.spool=v.f;break;
        case GGV_STATE_FRAMES:h.state.stateframes=v.i;break;
        case GGV_SHOTS:h.state.shots=v.i;break;
        case GGV_REMAINDER:h.state.remainder=v.f;break;
        case GGV_FIRING:h.state.firing=v.b;break;
        default:FAIL("Read-only state write reached host");break;
        }
    }
    int32_t run(ggv_stock_program p) {
        ggv_host host{this,read,write};int32_t result=12345;
        REQUIRE(ggvExecute(ggvStockProgram(p),&host,&result));return result;
    }
};
native_shooting_case baseline() {
    native_shooting_case s{};
    s.ammoindex=0;s.loadedammo=10;s.ammotype=1;s.trigger=1;
    s.shotgundelay=13;s.stateframes=3;s.initialrpm=60;s.maxrpm=600;s.clock=1;
    s.graph_present=s.graph_initial=s.graph_accel=s.graph_decel=1;
    s.loadedammo1=3;s.ammotype1=2;s.legacy_initialrpm=123;s.legacy_maxrpm=789;
    return s;
}
void same(const native_shooting_case &a,const native_shooting_case &b) {
    REQUIRE(a.stateframes==b.stateframes);
    REQUIRE(a.shots==b.shots);
    REQUIRE(std::bit_cast<uint32_t>(a.spool)==std::bit_cast<uint32_t>(b.spool));
    REQUIRE(std::bit_cast<uint32_t>(a.remainder)==std::bit_cast<uint32_t>(b.remainder));
    REQUIRE(a.firing==b.firing);
    REQUIRE(a.clock_reads==b.clock_reads);
    REQUIRE(a.override_calls==b.override_calls);
}
void parity(native_shooting_case s,ggv_stock_program p) {
    Host graph;graph.state=s;
    auto native=s;
    auto connected=s;
    int32_t expected;
    if(p==GGV_SHOOT_CONTROL) {
        expected=nativeShootingControl(&native);
        REQUIRE(nativeGraphShootingControl(&connected)==expected);
    } else {
        expected=nativeShootingCredit(&native);graph.state.override_calls+=2;
        REQUIRE(nativeGraphShootingCredit(&connected)==expected);
    }
    REQUIRE(graph.run(p)==expected);
    same(graph.state,native);
    same(connected,native);
}
}

TEST_CASE("shooting control default graph matches source-derived native C decisions","[gameplay][graph][parity]") {
    const uint32_t masks[]={0,2,0x1000,0x20000,0x20,2|0x1000,2|0x20000,0x1000|0x20000,2|0x1000|0x20000|0x20};
    for(auto flags:masks) for(int automatic:{0,1}) for(int aim:{0,1})
    for(int trigger:{0,1}) for(uint32_t stateFlags:{0u,0x10u})
    for(int burst:{-1,0,1,2,3,4,5,49,50}) {
        auto s=baseline();s.flags=flags;s.automatic=automatic;s.aim=aim;
        s.trigger=trigger;s.stateflags=stateFlags;s.burstbullets=burst;
        parity(s,GGV_SHOOT_CONTROL);
    }
}
TEST_CASE("shooting spool and lazy ammo boundaries retain native behavior","[gameplay][graph][parity]") {
    for(int accel:{-1,0,1,2,10}) for(int decel:{-1,0,1,2,10})
    for(float spool:{-0.5f,0.0f,0.5f,1.0f,1.5f}) for(float clock:{0.0f,0.5f,1.0f,20.0f})
    for(int trigger:{0,1}) {
        auto s=baseline();s.automatic=1;s.stateflags=0x10;s.trigger=trigger;
        s.spool=spool;s.clock=clock;s.turretaccel=accel;s.turretdecel=decel;
        parity(s,GGV_SHOOT_CONTROL);
    }
    for(int index:{-1,0,1}) for(int ammo:{-1,0,1}) for(int type:{-1,0})
    for(int frames:{0,2,3,12,13,14}) for(int shotgun:{0,1}) {
        auto s=baseline();s.flags=2;s.burstbullets=1;s.ammoindex=index;
        s.loadedammo=ammo;s.ammotype=type;s.stateframes=frames;s.shotgun=shotgun;
        parity(s,GGV_SHOOT_CONTROL);
    }
}
TEST_CASE("shot credit graph preserves native float storage and first shot remainder","[gameplay][graph][parity]") {
    for(float initial:{-120.0f,0.0f,60.0f,333.0f,600.0f})
    for(float max:{0.0f,600.0f,1200.0f}) for(float spool:{0.0f,0.1f,0.5f,1.0f})
    for(float clock:{0.0f,0.5f,1.0f,60.0f})
    for(float remainder:{-0.75f,0.0f,0.49999997f,0.99999994f})
    for(uint32_t stateFlags:{0u,0x10u}) {
        auto s=baseline();s.automatic=1;s.initialrpm=initial;s.maxrpm=max;
        s.spool=spool;s.clock=clock;s.remainder=remainder;s.stateflags=stateFlags;
        parity(s,GGV_SHOT_CREDIT);
    }
    for(int laser:{0,1}) {
        auto s=baseline();s.laser=laser;parity(s,GGV_SHOT_CREDIT);
    }
    auto s=baseline();s.functype=0x100;parity(s,GGV_SHOT_CREDIT);
}
TEST_CASE("prepared graph edits execute and invalid programs refuse before state access","[gameplay][graph]") {
    auto source=ggvStockProgram(GGV_SHOOT_CONTROL)->source;
    for(unsigned pc=0;pc<source.count;pc++)
        if(source.nodes[pc].op==GGV_RETURN&&source.nodes[pc].literal.i==2) source.nodes[pc].literal.i=23;
    ggv_program prepared{};char error[128]{};
    REQUIRE(ggvPrepare(&source,&prepared,error,sizeof(error)));
    Host h;h.state=baseline();ggv_host host{&h,Host::read,Host::write};int32_t result;
    REQUIRE(ggvExecute(&prepared,&host,&result));REQUIRE(result==23);
    auto reject=[&](ggv_source broken) {
        ggv_program destination{};REQUIRE_FALSE(ggvPrepare(&broken,&destination,error,sizeof(error)));
        Host untouched;ggv_host empty{&untouched,Host::read,Host::write};
        REQUIRE_FALSE(ggvExecute(&destination,&empty,&result));
        REQUIRE(untouched.reads.empty());REQUIRE(untouched.writes.empty());
    };
    auto broken=source;broken.nodes[0].type=GGV_F32;reject(broken);
    broken=source;broken.nodes[0].op=GGV_WRITE;broken.nodes[0].binding=GGV_FUNCTION_FLAGS;reject(broken);
    broken=source;broken.nodes[0].op=GGV_JUMP;broken.nodes[0].target=0;reject(broken);
    broken=source;broken.nodes[0].op=GGV_BRANCH_FALSE;broken.nodes[0].a=31;broken.nodes[0].target=2;reject(broken);
    broken=source;broken.nodes[0].op=static_cast<ggv_op>(255);reject(broken);
    broken=source;broken.nodes[source.count-1].op=GGV_CONST;reject(broken);
    broken=source;broken.count=GGV_NODE_LIMIT+1;reject(broken);
    // A register initialized on one predecessor remains uninitialized at merge.
    broken={};broken.count=5;
    broken.nodes[0].op=GGV_CONST;broken.nodes[0].type=GGV_BOOL;broken.nodes[0].dst=0;broken.nodes[0].literal.b=1;
    broken.nodes[1].op=GGV_BRANCH_FALSE;broken.nodes[1].a=0;broken.nodes[1].target=3;
    broken.nodes[2].op=GGV_CONST;broken.nodes[2].type=GGV_F32;broken.nodes[2].dst=1;broken.nodes[2].literal.f=1;
    broken.nodes[3].op=GGV_WRITE;broken.nodes[3].binding=GGV_SPOOL;broken.nodes[3].a=1;
    broken.nodes[4].op=GGV_RETURN;reject(broken);
}
TEST_CASE("graph bindings preserve fallback precedence and ordered repeated shooting calls","[gameplay][graph][parity]") {
    for(int present:{0,1}) for(int initial:{0,1}) for(int accel:{0,1}) for(int decel:{0,1})
    for(int automatic:{0,1}) for(int legacy:{0,1}) for(int trigger:{0,1,256})
    for(int aim:{0,1,256}) {
        auto s=baseline();s.graph_present=present;s.graph_initial=initial;
        s.graph_accel=accel;s.graph_decel=decel;s.automatic=automatic;
        s.functype=legacy?0x100:0;s.trigger=trigger;s.aim=aim;s.flags=2;
        s.legacy_flags=0x1000;s.turretaccel=3;s.turretdecel=4;
        s.legacy_accel=2;s.legacy_decel=1;s.stateflags=0x10;
        parity(s,GGV_SHOOT_CONTROL);parity(s,GGV_SHOT_CREDIT);
    }
    Host graph;graph.state=baseline();graph.state.automatic=1;
    graph.state.turretaccel=2;graph.state.turretdecel=1;
    auto native=graph.state;
    auto connected=graph.state;
    for(int tick=0;tick<16;tick++) {
        graph.state.trigger=native.trigger=connected.trigger=(tick<8);
        graph.state.stateflags=native.stateflags=connected.stateflags=0x10;
        int32_t control=nativeShootingControl(&native);
        REQUIRE(graph.run(GGV_SHOOT_CONTROL)==control);
        REQUIRE(nativeGraphShootingControl(&connected)==control);
        if(control>0) {
            graph.events.clear();graph.writes.clear();graph.reads.clear();
            nativeShootingCredit(&native);REQUIRE(nativeGraphShootingCredit(&connected)==1);graph.state.override_calls+=2;
            REQUIRE(graph.run(GGV_SHOT_CREDIT)==1);
            REQUIRE(graph.events.front()==128+GGV_FIRING);
            auto firing=std::find(graph.events.begin(),graph.events.end(),128+GGV_FIRING);
            auto rpm=std::find(graph.events.begin(),graph.events.end(),GGV_INITIAL_RPM);
            REQUIRE(firing<rpm);
            REQUIRE(graph.writes.size()>=3);
            REQUIRE(graph.writes[0]==GGV_FIRING);
            REQUIRE(graph.writes[1]==GGV_SHOTS);
            REQUIRE(graph.writes[2]==GGV_REMAINDER);
            // The unchanged native tail adds pre-clamp shot credit; model that
            // boundary identically, without claiming tail implementation parity.
            graph.state.burstbullets+=graph.state.shots;native.burstbullets+=native.shots;
            connected.burstbullets+=connected.shots;
        }
        same(graph.state,native);
        same(connected,native);
    }
    Host lazy;lazy.state=baseline();lazy.state.ammoindex=-1;
    REQUIRE(lazy.run(GGV_SHOOT_CONTROL)==2);
    REQUIRE(std::find(lazy.reads.begin(),lazy.reads.end(),GGV_LOADED_AMMO)==lazy.reads.end());
    REQUIRE(std::find(lazy.reads.begin(),lazy.reads.end(),GGV_AMMO_TYPE)==lazy.reads.end());
    lazy=Host{};lazy.state=baseline();lazy.state.ammoindex=1;
    lazy.state.loadedammo=0;lazy.state.ammotype=0;lazy.state.loadedammo1=5;lazy.state.ammotype1=-1;
    REQUIRE(lazy.run(GGV_SHOOT_CONTROL)==2);
    REQUIRE(std::find(lazy.reads.begin(),lazy.reads.end(),GGV_AMMO_TYPE)==lazy.reads.end());
}

TEST_CASE("integer leaf bridge preserves mask results and aliased increment boundaries","[gameplay][graph][integer]") {
    ggv_source maskSource{};
    maskSource.count=4;
    maskSource.nodes[0].op=GGV_READ;
    maskSource.nodes[0].binding=GGV_FUNCTION_FLAGS;
    maskSource.nodes[0].dst=0;
    maskSource.nodes[1].op=GGV_MASK;
    maskSource.nodes[1].a=0;
    maskSource.nodes[1].dst=1;
    maskSource.nodes[2].op=GGV_WRITE;
    maskSource.nodes[2].binding=GGV_FIRING;
    maskSource.nodes[2].a=1;
    maskSource.nodes[3].op=GGV_RETURN;
    maskSource.nodes[3].literal.i=17;
    struct MaskCase { uint32_t value, mask; int expected; };
    for(const MaskCase c : {MaskCase{0u,0xffffffffu,0}, {0xffffffffu,0u,0},
                           {0x80000000u,0x80000000u,1}, {0x80000000u,0x7fffffffu,0},
                           {0xffffffffu,0xffffffffu,1}, {0x40000000u,0x80000000u,0}}) {
        maskSource.nodes[1].literal.u=c.mask;
        ggv_program program{};
        char error[128]{};
        REQUIRE(ggvPrepare(&maskSource,&program,error,sizeof(error)));
        Host host;
        host.state=baseline();
        host.state.flags=c.value;
        ggv_host bindings{&host,Host::read,Host::write};
        int32_t result=999;
        REQUIRE(ggvExecute(&program,&bindings,&result));
        REQUIRE(result==17);
        REQUIRE(host.state.firing==c.expected);
        const std::vector<int> expected{GGV_FUNCTION_FLAGS,128+GGV_FIRING};
        REQUIRE(host.events==expected);
    }
    // A U32 mask cannot alias its BOOL destination under the unchanged
    // immutable register-type validator. Refuse before any host callbacks.
    maskSource.nodes[1].dst=0;
    maskSource.nodes[2].a=0;
    ggv_program rejected{};
    char error[128]{};
    REQUIRE_FALSE(ggvPrepare(&maskSource,&rejected,error,sizeof(error)));

    ggv_source incrementSource{};
    incrementSource.count=4;
    incrementSource.nodes[0].op=GGV_READ;
    incrementSource.nodes[0].binding=GGV_SHOTS;
    incrementSource.nodes[0].dst=0;
    incrementSource.nodes[1].op=GGV_I_INCREMENT;
    incrementSource.nodes[1].a=0;
    incrementSource.nodes[1].dst=0; // real same-register source/destination
    incrementSource.nodes[2].op=GGV_WRITE;
    incrementSource.nodes[2].binding=GGV_SHOTS;
    incrementSource.nodes[2].a=0;
    incrementSource.nodes[3].op=GGV_RETURN;
    incrementSource.nodes[3].literal.i=19;
    ggv_program program{};
    char error[128]{};
    REQUIRE(ggvPrepare(&incrementSource,&program,error,sizeof(error)));
    const int32_t min=std::numeric_limits<int32_t>::min();
    const int32_t max=std::numeric_limits<int32_t>::max();
    for(const auto c : {std::pair<int32_t,int32_t>{-1,0}, {0,1}, {max,min}, {min,min+1}}) {
        Host host;
        host.state=baseline();
        host.state.shots=c.first;
        ggv_host bindings{&host,Host::read,Host::write};
        int32_t result=999;
        REQUIRE(ggvExecute(&program,&bindings,&result));
        REQUIRE(result==19);
        REQUIRE(host.state.shots==c.second);
        const std::vector<int> expected{GGV_SHOTS,128+GGV_SHOTS};
        REQUIRE(host.events==expected);
    }
}

namespace {
/* Independent prepared-host fixture. Real production C seams are separately
 * mechanically derived by the oracle owner, not simulated by this host. */
struct FullHost {
    native_shooting_case state{};
    uint32_t flags{};
    int32_t ammo{},type{},accel{},decel{};
    int automatic{},legacy{};
    int32_t audioTokens[NATIVE_SHOOTING_AUDIO_LIMIT]{};
    explicit FullHost(native_shooting_case input):state(input) {
        for(int i=0;i<NATIVE_SHOOTING_AUDIO_LIMIT;++i) audioTokens[i]=i+1;
    }
    void normalize() {
        REQUIRE(state.handnum==0||state.handnum==1);
        state.current_hand_lastshootframe[1-state.handnum]=state.other_lastshootframe;
        if(state.passed_is_current) state.current_hand_lastshootframe[state.handnum]=state.lastshootframe;
        if(!state.overrides_initialized) {
            state.overrides_initialized=1;state.rontime_state=2;state.rofftime_state=4;
        }
    }
    void *ref(int token) {
        REQUIRE(token>=0);REQUIRE(token<=NATIVE_SHOOTING_AUDIO_LIMIT);
        return token?&audioTokens[token-1]:nullptr;
    }
    static int token(void *value) { return value?*static_cast<int32_t *>(value):0; }
    void event(int op,int subject=-1,int a=0,int b=0,int c=0,int result=0) {
        nativeShootingRecordEvent(&state,NS_EVENT_SERVICE,op,subject,a,b,c,result);
    }
    static int32_t script(int32_t *values,int count,int &index,int fallback) {
        REQUIRE(count>=0);REQUIRE(count<=NATIVE_SHOOTING_SCRIPT_LIMIT);REQUIRE(index>=0);
        int result=index<count?values[index]:fallback;
        REQUIRE(index<INT32_MAX);++index;return result;
    }
    static ggv_value read(void *opaque,ggv_binding binding) {
        auto &h=*static_cast<FullHost *>(opaque);auto &s=h.state;ggv_value v{};
        switch(binding) {
        case GGV_FUNCTION_FLAGS:v.u=h.flags;break;
        case GGV_AMMO_INDEX:v.i=h.ammo;break;
        case GGV_AUTOMATIC:v.b=h.automatic;break;
        case GGV_ACCELERATION:v.i=h.accel;break;
        case GGV_DECELERATION:v.i=h.decel;break;
        case GGV_TRIGGER:v.b=s.trigger;break;
        case GGV_STATE_FLAGS:v.u=s.stateflags;break;
        case GGV_BURST_BULLETS:v.i=s.burstbullets;break;
        case GGV_AIM:v.b=s.aim;break;
        case GGV_LOADED_AMMO:REQUIRE(h.ammo==0||h.ammo==1);v.i=h.ammo?s.loadedammo1:s.loadedammo;break;
        case GGV_AMMO_TYPE:REQUIRE(h.ammo==0||h.ammo==1);v.i=h.ammo?s.ammotype1:s.ammotype;break;
        case GGV_SHOTGUN:v.b=s.shotgun;break;
        case GGV_SHOTGUN_DELAY:v.i=s.shotgundelay;break;
        case GGV_LASER:v.b=s.laser;break;
        case GGV_INITIAL_RPM:case GGV_MAX_RPM:FAIL("Full program must compose raw RPM selection");break;
        case GGV_CLOCK:
            ++s.clock_reads;v.f=s.clock;h.event(NS_CLOCK,-1,0,0,0,std::bit_cast<int32_t>(s.clock));break;
        case GGV_SPOOL:v.f=s.spool;break;
        case GGV_STATE_FRAMES:v.i=s.stateframes;break;
        case GGV_SHOTS:v.i=s.shots;break;
        case GGV_REMAINDER:v.f=s.remainder;break;
        case GGV_FIRING:v.b=s.firing;break;
        case GGV_GRAPH_FLAGS:REQUIRE(s.graph_present);v.u=s.flags;break;
        case GGV_LEGACY_FLAGS:REQUIRE_FALSE(s.legacy_function_absent);v.u=s.legacy_flags;break;
        case GGV_GRAPH_AMMO_SLOT:REQUIRE(s.graph_present);v.i=s.graph_ammo_slot;break;
        case GGV_LEGACY_AMMO_INDEX:v.i=s.legacy_function_absent?-1:std::bit_cast<int8_t>(uint8_t(s.legacy_ammoindex));break;
        case GGV_GRAPH_HAS_TYPE:REQUIRE(s.graph_present);v.b=s.graph_has_type;break;
        case GGV_GRAPH_FUNCTION_TYPE:REQUIRE(s.graph_present);v.i=s.graph_function_type;break;
        case GGV_LEGACY_FUNCTION_TYPE:v.i=s.legacy_function_absent?0:s.legacy_functype;break;
        case GGV_FUNCTION_TYPE:v.i=h.type;break;
        case GGV_LEGACY_AUTOMATIC:v.b=h.legacy;break;
        case GGV_GRAPH_HAS_MAX_RPM:REQUIRE(s.graph_present);v.b=s.automatic;break;
        case GGV_GRAPH_HAS_ACCEL:REQUIRE(s.graph_present);v.b=s.graph_accel;break;
        case GGV_GRAPH_HAS_DECEL:REQUIRE(s.graph_present);v.b=s.graph_decel;break;
        case GGV_GRAPH_ACCEL:REQUIRE(s.graph_present);v.i=s.turretaccel;break;
        case GGV_GRAPH_DECEL:REQUIRE(s.graph_present);v.i=s.turretdecel;break;
        case GGV_LEGACY_ACCEL:REQUIRE_FALSE(s.legacy_function_absent);v.i=std::bit_cast<int8_t>(uint8_t(s.legacy_accel));break;
        case GGV_LEGACY_DECEL:REQUIRE_FALSE(s.legacy_function_absent);v.i=std::bit_cast<int8_t>(uint8_t(s.legacy_decel));break;
        case GGV_GRAPH_HAS_INITIAL_RPM:REQUIRE(s.graph_present);v.b=s.graph_initial;break;
        case GGV_GRAPH_INITIAL_RPM:REQUIRE(s.graph_present);v.f=s.initialrpm;break;
        case GGV_GRAPH_MAX_RPM:REQUIRE(s.graph_present);v.f=s.maxrpm;break;
        case GGV_LEGACY_INITIAL_RPM:REQUIRE_FALSE(s.legacy_function_absent);v.f=s.legacy_initialrpm;break;
        case GGV_LEGACY_MAX_RPM:REQUIRE_FALSE(s.legacy_function_absent);v.f=s.legacy_maxrpm;break;
        case GGV_GE_FLASH:v.b=s.ge_flashes;break;
        case GGV_FLASH:v.b=s.flashon;break;
        case GGV_LOAD_SLIDE:v.f=s.loadslide;break;
        case GGV_STATE_VAR1:v.i=s.statevar1;break;
        case GGV_ATTACK_TYPE:v.i=s.attacktype;break;
        case GGV_BIND_ATTACK_SHOOT:v.i=1;break;
        case GGV_ATTACK_PROJECTILE:v.i=2;break;
        case GGV_FUNCTION_SHOOT_LOW:case GGV_FUNCTION_SHOOT_SINGLE:v.i=1;break;
        case GGV_CURRENT_FRAME:v.i=s.frame60;break;
        case GGV_OTHER_LAST_FRAME:v.i=s.current_hand_lastshootframe[1-s.handnum];break;
        case GGV_ALLOW_SHOOT_FRAME:v.i=s.allowshootframe;break;
        case GGV_LAST_SHOOT_FRAME:v.i=s.lastshootframe;break;
        case GGV_AUDIO_SLOT_2:v.ref=h.ref(s.audio2);break;
        case GGV_AUDIO_SLOT_3:v.ref=h.ref(s.audio3);break;
        case GGV_MAULER:v.b=s.mauler;break;
        case GGV_MATMOT:v.f=s.matmot;break;
        case GGV_PRIORITY_ENABLED:
#if VERSION >= VERSION_NTSC_1_0
            v.b=1;
#endif
            break;
        case GGV_SOUND_STOPPED:v.i=0;break;
        case GGV_BINDING_COUNT:FAIL("Unknown binding");break;
        }
        return v;
    }
    static void write(void *opaque,ggv_binding binding,ggv_value v) {
        auto &h=*static_cast<FullHost *>(opaque);auto &s=h.state;int op=-1;
        switch(binding) {
        // Internal selected configuration cache is not a gameplay field store.
        case GGV_FUNCTION_FLAGS:h.flags=v.u;return;
        case GGV_AMMO_INDEX:h.ammo=v.i;return;
        case GGV_FUNCTION_TYPE:h.type=v.i;return;
        case GGV_LEGACY_AUTOMATIC:h.legacy=v.b;return;
        case GGV_AUTOMATIC:h.automatic=v.b;return;
        case GGV_ACCELERATION:h.accel=v.i;return;
        case GGV_DECELERATION:h.decel=v.i;return;
        case GGV_SPOOL:s.spool=v.f;op=NS_WRITE_SPOOL;break;
        case GGV_STATE_FRAMES:s.stateframes=v.i;op=NS_WRITE_STATE_FRAMES;break;
        case GGV_SHOTS:s.shots=v.i;op=NS_WRITE_SHOTS;break;
        case GGV_REMAINDER:s.remainder=v.f;op=NS_WRITE_REMAINDER;break;
        case GGV_FIRING:s.firing=int8_t(v.b);op=NS_WRITE_FIRING;break;
        case GGV_BURST_BULLETS:s.burstbullets=v.i;op=NS_WRITE_BURST;break;
        case GGV_FLASH:s.flashon=int8_t(v.b);op=NS_WRITE_FLASH;break;
        case GGV_LOAD_SLIDE:s.loadslide=v.f;op=NS_WRITE_LOAD_SLIDE;break;
        case GGV_STATE_VAR1:s.statevar1=v.i;op=NS_WRITE_STATE_VAR1;break;
        case GGV_STATE_FLAGS:s.stateflags=v.u;op=NS_WRITE_STATE_FLAGS;break;
        case GGV_LOADED_AMMO:
            REQUIRE(h.ammo==0||h.ammo==1);(h.ammo?s.loadedammo1:s.loadedammo)=v.i;op=NS_WRITE_LOADED_AMMO;break;
        case GGV_ATTACK_TYPE:s.attacktype=v.i;op=NS_WRITE_ATTACK_TYPE;break;
        case GGV_ALLOW_SHOOT_FRAME:s.allowshootframe=v.i;op=NS_WRITE_ALLOW_FRAME;break;
        case GGV_LAST_SHOOT_FRAME:
            s.lastshootframe=v.i;if(s.passed_is_current)s.current_hand_lastshootframe[s.handnum]=v.i;
            op=NS_WRITE_LAST_FRAME;break;
        default:FAIL("Read-only full binding written");return;
        }
        nativeShootingRecordEvent(&s,NS_EVENT_WRITE,op,-1,0,0,0,0);
    }
    static ggv_value call(void *opaque,ggv_service service,ggv_value a,ggv_value b,ggv_value) {
        auto &h=*static_cast<FullHost *>(opaque);auto &s=h.state;ggv_value v{};
        switch(service) {
        case GGV_RESOLVE_GRAPH:v.b=s.graph_present!=0;h.event(NS_RESOLVE_GRAPH,-1,0,0,0,v.b);break;
        case GGV_OVERRIDE_ON:case GGV_OVERRIDE_OFF: {
            const bool on=service==GGV_OVERRIDE_ON;auto &stored=on?s.rontime_state:s.rofftime_state;
            const auto old=stored;++s.override_calls;
            if(on?s.override_on_enabled:s.override_off_enabled)stored=on?s.override_on_value:s.override_off_value;
            h.event(on?NS_OVERRIDE_ON:NS_OVERRIDE_OFF,-1,std::bit_cast<int32_t>(old),0,0,std::bit_cast<int32_t>(stored));break;
        }
        case GGV_START_SLIDE:s.current_hand_slideinc[s.handnum]=1;h.event(NS_START_SLIDE,s.handnum);break;
        case GGV_RUMBLE:h.event(NS_RUMBLE,s.handnum,s.info_weaponnum);break;
        case GGV_FIRESLOT_DURATION:
            v.i=script(s.fireslot_script,s.fireslot_count,s.fireslot_index,s.fireslot_default);
            h.event(NS_FIRESLOT_DURATION,-1,0,0,0,v.i);break;
        case GGV_SINGLE_SOUND:
            v.i=script(s.sound_script,s.sound_count,s.sound_index,s.sound_default);
            h.event(NS_SINGLE_SOUND,-1,0,0,0,v.i);break;
        case GGV_CURRENT_PRIORITY:v.i=s.priority;h.event(NS_CURRENT_PRIORITY,0,0,0,0,v.i);break;
        case GGV_AUDIO_PRIORITY:v.i=s.audio_priority;h.event(NS_AUDIO_PRIORITY,1,0,0,0,v.i);break;
        case GGV_SET_PRIORITY:s.priority=a.i;h.event(NS_SET_PRIORITY,0,a.i);break;
        case GGV_SOUND_STATE: {
            const int t=token(a.ref);REQUIRE(t>0);v.i=s.audio_state[t-1];h.event(NS_SOUND_STATE,t,t,0,0,v.i);break;
        }
        case GGV_SOUND_STOP: {
            const int t=token(a.ref);REQUIRE(t>0);s.audio_state[t-1]=0;h.event(NS_SOUND_STOP,t,t);break;
        }
        case GGV_SOUND_START:
            REQUIRE(a.i==2||a.i==3);(a.i==2?s.audio2:s.audio3)=s.start_slot_token;
            if(s.start_slot_token)s.audio_state[s.start_slot_token-1]=s.start_state;
            v.ref=h.ref(s.start_return_token);
            h.event(NS_SOUND_START,a.i,b.i,s.start_slot_token,s.start_return_token,s.start_return_token);break;
        case GGV_SOUND_EVENT:
            s.last_pitch_token=token(a.ref);s.last_pitch_event=0x10;s.last_pitch_bits=b.i;
            h.event(NS_SOUND_EVENT,token(a.ref),0x10,b.i);break;
        case GGV_SERVICE_COUNT:FAIL("Unknown service");break;
        }
        return v;
    }
    int32_t run(ggv_stock_program program) {
        normalize();ggv_host host{this,read,write,call};int32_t result=12345;
        REQUIRE(ggvExecute(ggvStockProgram(program),&host,&result));return result;
    }
};
native_shooting_case fullBaseline() {
    auto s=baseline();s.graph_ammo_slot=0;s.graph_has_type=1;s.graph_function_type=0x101;
    s.legacy_ammoindex=0;s.legacy_functype=0x101;
    s.automatic=1;s.turretaccel=2;s.turretdecel=4;s.legacy_accel=5;s.legacy_decel=7;
    s.legacy_flags=0x1000;s.spool=1;s.frame60=100;s.lastshootframe=70;s.other_lastshootframe=80;
    s.allowshootframe=90;s.priority=10;s.audio_priority=20;s.start_slot_token=1;s.start_return_token=1;
    s.start_state=1;s.sound_default=77;s.info_weaponnum=42;s.attacktype=99;s.flashon=1;s.loadslide=3;
    return s;
}
void sameFull(const native_shooting_case &a,const native_shooting_case &b) {
    same(a,b);
    REQUIRE(a.burstbullets==b.burstbullets);REQUIRE(a.stateflags==b.stateflags);
    REQUIRE(a.loadedammo==b.loadedammo);REQUIRE(a.loadedammo1==b.loadedammo1);
    REQUIRE(a.flashon==b.flashon);REQUIRE(std::bit_cast<uint32_t>(a.loadslide)==std::bit_cast<uint32_t>(b.loadslide));
    REQUIRE(a.statevar1==b.statevar1);REQUIRE(a.attacktype==b.attacktype);
    REQUIRE(a.allowshootframe==b.allowshootframe);REQUIRE(a.lastshootframe==b.lastshootframe);
    REQUIRE(a.audio2==b.audio2);REQUIRE(a.audio3==b.audio3);REQUIRE(a.priority==b.priority);
    REQUIRE(a.fireslot_index==b.fireslot_index);REQUIRE(a.sound_index==b.sound_index);
    REQUIRE(a.rontime_state==b.rontime_state);REQUIRE(a.rofftime_state==b.rofftime_state);
    REQUIRE(a.overrides_initialized==b.overrides_initialized);
    REQUIRE(a.last_pitch_token==b.last_pitch_token);REQUIRE(a.last_pitch_event==b.last_pitch_event);REQUIRE(a.last_pitch_bits==b.last_pitch_bits);
    for(int i=0;i<2;++i) {
        REQUIRE(a.current_hand_slideinc[i]==b.current_hand_slideinc[i]);
        REQUIRE(a.current_hand_lastshootframe[i]==b.current_hand_lastshootframe[i]);
    }
    for(int i=0;i<NATIVE_SHOOTING_AUDIO_LIMIT;++i)REQUIRE(a.audio_state[i]==b.audio_state[i]);
    REQUIRE(a.program_refused==0);REQUIRE(b.program_refused==0);REQUIRE(a.trace_overflow==0);REQUIRE(b.trace_overflow==0);
    REQUIRE(a.trace_count==b.trace_count);
    for(int i=0;i<a.trace_count;++i) {
        INFO("ordered actual effect "<<i);
        REQUIRE(std::memcmp(&a.trace[i],&b.trace[i],sizeof(native_shooting_event))==0);
    }
}
void fullParity(native_shooting_case input,ggv_stock_program program) {
    auto native=input,connected=input;FullHost prepared(input);
    const auto expected=program==GGV_SHOOT_CONTROL_FULL?nativeShootingFullControl(&native):nativeShootingFullCommit(&native);
    const auto actual=program==GGV_SHOOT_CONTROL_FULL?nativeGraphShootingFullControl(&connected):nativeGraphShootingFullCommit(&connected);
    REQUIRE(actual==expected);REQUIRE(prepared.run(program)==expected);
    sameFull(native,connected);sameFull(native,prepared.state);
}
}

TEST_CASE("full shooting programs compose configuration selection and nullable legacy records","[gameplay][graph][parity][full]") {
    for(int graph:{0,1})for(int max:{0,1})for(int initial:{0,1})for(int accel:{0,1})for(int decel:{0,1})
    for(int legacyType:{1,0x101})for(int hasType:{0,1})for(int graphType:{1,0x101,0x201})for(int slot:{-1,0,1}) {
        auto s=fullBaseline();s.graph_present=graph;s.automatic=max;s.graph_initial=initial;s.graph_accel=accel;s.graph_decel=decel;
        s.legacy_functype=legacyType;s.graph_ammo_slot=slot;s.ge_flashes=1;
        s.graph_has_type=hasType;s.graph_function_type=graphType;
        fullParity(s,GGV_SHOOT_CONTROL_FULL);fullParity(s,GGV_SHOT_COMMIT_FULL);
    }
    auto s=fullBaseline();s.legacy_function_absent=1;s.graph_present=1;s.graph_has_type=1;s.graph_function_type=0x201;
    s.graph_ammo_slot=-1;s.graph_initial=0;s.graph_accel=0;s.graph_decel=0;
    fullParity(s,GGV_SHOOT_CONTROL_FULL);fullParity(s,GGV_SHOT_COMMIT_FULL);
    s.graph_has_type=0;fullParity(s,GGV_SHOOT_CONTROL_FULL);fullParity(s,GGV_SHOT_COMMIT_FULL);
    for(int deceleration:{0,4})for(float spool:{0.0f,0.25f,1.0f})for(float clock:{1.0f,8.0f}) {
        s=fullBaseline();s.trigger=0;s.stateflags=0x10;s.flags=0;s.turretdecel=deceleration;s.spool=spool;s.clock=clock;
        fullParity(s,GGV_SHOOT_CONTROL_FULL); // native zero-deceleration returns -1
    }
}
TEST_CASE("full commit preserves flash ammo slots frames sound handles and Mauler bits","[gameplay][graph][parity][full]") {
    for(int hand:{0,1})for(int alias:{0,1})for(int soundCase=0;soundCase<10;++soundCase) {
        auto s=fullBaseline();s.handnum=hand;s.passed_is_current=alias;s.mauler=1;s.matmot=2.9f;
        switch(soundCase) {
        case 0:s.loadedammo=0;break;
        case 1:s.fireslot_count=2;s.fireslot_script[0]=2;s.fireslot_script[1]=9;break;
        case 2:s.other_lastshootframe=s.frame60;s.fireslot_default=2;break;
        case 3:s.allowshootframe=s.frame60;s.fireslot_default=2;break;
        case 4:s.sound_default=0;break;
        case 5:s.audio2=1;s.audio3=2;s.audio_state[0]=1;s.audio_state[1]=2;break;
        case 6:s.audio2=1;s.audio3=0;s.audio_state[0]=0;break;
        case 7:s.start_slot_token=1;s.start_return_token=0;break;
        case 8:s.start_slot_token=1;s.start_return_token=3;break;
        case 9:s.sound_count=2;s.sound_script[0]=77;s.sound_script[1]=88;break;
        }
        fullParity(s,GGV_SHOT_COMMIT_FULL);
    }
    for(float charge:{-1.75f,0.0f,2.9f,3.9f,4.1f}) {
        auto s=fullBaseline();s.mauler=1;s.matmot=charge;fullParity(s,GGV_SHOT_COMMIT_FULL);
    }
    for(uint32_t flags:{0u,0x2000u})for(int type:{1,0x101,0x201,0x501})for(int flash:{0,1}) {
        auto s=fullBaseline();s.flags=flags;s.graph_function_type=type;s.ge_flashes=1;s.flashon=int8_t(flash);
        s.maxrpm=120;s.initialrpm=120;s.clock=60;fullParity(s,GGV_SHOT_COMMIT_FULL);
    }
    auto s=fullBaseline();s.initialrpm=0;s.maxrpm=0;s.remainder=0;s.stateflags=0x10;fullParity(s,GGV_SHOT_COMMIT_FULL);
    s=fullBaseline();s.graph_present=0;s.legacy_functype=1;s.laser=1;fullParity(s,GGV_SHOT_COMMIT_FULL);
    s=fullBaseline();s.graph_ammo_slot=1;s.loadedammo1=0;fullParity(s,GGV_SHOT_COMMIT_FULL);
}
TEST_CASE("full control to commit carries persistent overrides and ordered hand state","[gameplay][graph][parity][full]") {
    auto input=fullBaseline();input.passed_is_current=1;input.handnum=1;input.spool=0.25f;
    input.override_on_enabled=1;input.override_on_value=0xf0000000u;
    input.override_off_enabled=1;input.override_off_value=9;
    auto native=input,connected=input;FullHost prepared(input);
    for(int step=0;step<3;++step) {
        const auto control=nativeShootingFullControl(&native);
        REQUIRE(nativeGraphShootingFullControl(&connected)==control);
        REQUIRE(prepared.run(GGV_SHOOT_CONTROL_FULL)==control);sameFull(native,connected);sameFull(native,prepared.state);
        const auto result=nativeShootingFullCommit(&native);
        REQUIRE(nativeGraphShootingFullCommit(&connected)==result);REQUIRE(prepared.run(GGV_SHOT_COMMIT_FULL)==result);
        sameFull(native,connected);sameFull(native,prepared.state);
        nativeShootingResetTrace(&native);nativeShootingResetTrace(&connected);nativeShootingResetTrace(&prepared.state);
        native.frame60+=3;connected.frame60+=3;prepared.state.frame60+=3;
    }
}
TEST_CASE("service dependencies and reference typing refuse before full shooting callbacks","[gameplay][graph][full]") {
    FullHost untouched(fullBaseline());ggv_host missing{&untouched,FullHost::read,FullHost::write};int32_t result=123;
    REQUIRE_FALSE(ggvExecute(ggvStockProgram(GGV_SHOT_COMMIT_FULL),&missing,&result));
    REQUIRE(result==123);REQUIRE(untouched.state.trace_count==0);REQUIRE(untouched.state.override_calls==0);
    ggv_source source{};source.count=3;source.nodes[0].op=GGV_CONST;source.nodes[0].type=GGV_I32;
    source.nodes[0].dst=0;source.nodes[1].op=GGV_CALL;source.nodes[1].service=GGV_SOUND_STOP;source.nodes[1].a=0;
    source.nodes[2].op=GGV_RETURN;ggv_program program{};char error[128]{};
    REQUIRE_FALSE(ggvPrepare(&source,&program,error,sizeof(error)));
    source.nodes[0].type=GGV_REF;source.nodes[0].literal.ref=&result;
    REQUIRE_FALSE(ggvPrepare(&source,&program,error,sizeof(error))); // authored address is forbidden
    source.nodes[0].literal.ref=nullptr;source.nodes[1].service=static_cast<ggv_service>(999);
    REQUIRE_FALSE(ggvPrepare(&source,&program,error,sizeof(error)));
}

namespace {
struct HandHost {
    native_shooting_case state{};
    int32_t pad1{},pad2{};
    explicit HandHost(native_shooting_case initial):state(initial) {}
    void normalize() {
        REQUIRE(state.handnum==0||state.handnum==1);
        state.current_hand_lastshootframe[1-state.handnum]=state.other_lastshootframe;
        if(state.passed_is_current) {
            state.current_hand_lastshootframe[state.handnum]=state.lastshootframe;
            state.passed_slideinc=state.current_hand_slideinc[state.handnum];
        }
    }
    static int32_t narrow8(int32_t value) {return std::bit_cast<int8_t>(uint8_t(value));}
    void event(int op,int subject,int a=0,int b=0,int c=0,int result=0) {
        nativeShootingRecordEvent(&state,NS_EVENT_SERVICE,op,subject,a,b,c,result);
    }
    static ggv_value read(void *opaque,ggv_binding binding) {
        auto &h=*static_cast<HandHost *>(opaque);ggv_value v{};
        switch(binding) {
        case GGV_HAND_NUMBER:v.i=h.state.handnum;break;
        case GGV_HAND_LEFT_NUMBER:v.i=1;break;
        case GGV_PAD_1:v.i=h.pad1;break;
        case GGV_PAD_2:v.i=h.pad2;break;
        case GGV_CONTROL_MODE_21:v.i=4;break;
        case GGV_PAK_RUMBLE_TYPE:v.i=1;break;
        case GGV_CURRENT_HAND_SLIDEINC:v.b=h.state.current_hand_slideinc[h.state.handnum];break;
        default:FAIL("Hand program read outside its host domain");break;
        }
        return v;
    }
    static void write(void *opaque,ggv_binding binding,ggv_value v) {
        auto &h=*static_cast<HandHost *>(opaque);auto &s=h.state;
        REQUIRE(binding==GGV_CURRENT_HAND_SLIDEINC);
        s.current_hand_slideinc[s.handnum]=v.b;
        if(s.passed_is_current)s.passed_slideinc=v.b;
        nativeShootingRecordEvent(&s,NS_EVENT_WRITE,NS_WRITE_SLIDEINC,s.handnum,0,0,0,0);
    }
    static ggv_value call(void *opaque,ggv_service service,ggv_value a,ggv_value,ggv_value) {
        auto &h=*static_cast<HandHost *>(opaque);auto &s=h.state;ggv_value v{};
        switch(service) {
        case GGV_GET_PADS:
            ++s.joy_pad_queries;h.pad1=s.contpad1;h.pad2=s.contpad2;
            h.event(NS_JOY_PADS,narrow8(s.current_playernum),h.pad1,h.pad2);break;
        case GGV_CONTROL_MODE:
            ++s.control_mode_queries;v.i=s.controlmode;
            h.event(NS_CONTROL_MODE,s.current_mpindex,0,0,0,v.i);break;
        case GGV_PAK_TYPE: {
            const int32_t device=narrow8(a.i);
            const int fallback=device==narrow8(s.contpad1)?s.paktype1:
                (device==narrow8(s.contpad2)?s.paktype2:0);
            v.i=FullHost::script(s.paktype_script,s.paktype_count,s.paktype_index,fallback);
            ++s.pak_type_queries;h.event(NS_PAK_TYPE,device,device,0,0,v.i);break;
        }
        case GGV_WEAPON_DUALWIELD:
            ++s.weapon_flag_queries;v.b=s.weapon_dualwield!=0;
            h.event(NS_WEAPON_FLAG,s.info_weaponnum,0x1000,0,0,v.b);break;
        default:FAIL("Hand program invoked outside its service domain");break;
        }
        return v;
    }
    static ggv_value call4(void *opaque,ggv_service4 service,ggv_value a,ggv_value b,ggv_value c,ggv_value d) {
        auto &h=*static_cast<HandHost *>(opaque);REQUIRE(service==GGV_PAK_RUMBLE);
        ++h.state.rumble_pulses;
        h.event(NS_PAK_RUMBLE,a.i,std::bit_cast<int32_t>(b.f),c.i,d.i);
        return ggv_value{};
    }
    void run(ggv_stock_program program) {
        normalize();ggv_host host{this,read,write,call,call4};int32_t result=987;
        REQUIRE(ggvExecute(ggvStockProgram(program),&host,&result));REQUIRE(result==0);
    }
};
void sameHand(const native_shooting_case &a,const native_shooting_case &b) {
    sameFull(a,b);
    REQUIRE(a.passed_slideinc==b.passed_slideinc);
    REQUIRE(a.joy_pad_queries==b.joy_pad_queries);REQUIRE(a.control_mode_queries==b.control_mode_queries);
    REQUIRE(a.pak_type_queries==b.pak_type_queries);REQUIRE(a.weapon_flag_queries==b.weapon_flag_queries);
    REQUIRE(a.rumble_pulses==b.rumble_pulses);REQUIRE(a.paktype_index==b.paktype_index);
}
void handParity(native_shooting_case initial,ggv_stock_program program) {
    auto native=initial,connected=initial;HandHost prepared(initial);
    if(program==GGV_HAND_RUMBLE){nativeShootingRumble(&native);nativeGraphShootingRumble(&connected);}
    else {nativeShootingSlide(&native);nativeGraphShootingSlide(&connected);}
    prepared.run(program);sameHand(native,connected);sameHand(native,prepared.state);
}
}
TEST_CASE("hand rumble composes mode capabilities dual wield and exact four-input pulses","[gameplay][graph][parity][hand]") {
    for(int mode:{3,4,8})for(int first:{-1,0,1,257})for(int second:{-1,0,1,257})
    for(int capabilities=0;capabilities<4;++capabilities)for(int dual:{0,1,-1,256})for(int hand:{0,1}) {
        auto s=fullBaseline();s.controlmode=mode;s.contpad1=first;s.contpad2=second;
        s.paktype1=capabilities&1;s.paktype2=(capabilities>>1)&1;s.weapon_dualwield=dual;
        s.current_playernum=258;s.current_mpindex=7;s.handnum=hand;s.passed_slideinc=9;
        handParity(s,GGV_HAND_RUMBLE);
    }
    auto s=fullBaseline();s.controlmode=4;s.contpad1=0;s.contpad2=1;s.paktype1=0;s.paktype2=0;
    HandHost neither(s);neither.run(GGV_HAND_RUMBLE);
    REQUIRE(neither.state.rumble_pulses==1);REQUIRE(neither.state.pak_type_queries==2);
    REQUIRE(neither.state.weapon_flag_queries==1); // No invented capability veto.
    const auto &pulse=neither.state.trace[neither.state.trace_count-1];
    REQUIRE(pulse.operation==NS_PAK_RUMBLE);REQUIRE(pulse.subject==0);
    REQUIRE(pulse.a==std::bit_cast<int32_t>(0.2f));REQUIRE(pulse.b==2);REQUIRE(pulse.c==4);
}
TEST_CASE("hand rumble keeps narrowed query identities separate from pulse identities","[gameplay][graph][parity][hand]") {
    auto input=fullBaseline();input.controlmode=8;input.contpad1=257;input.contpad2=1;
    input.paktype_count=8;input.paktype_script[0]=0;input.paktype_script[1]=1;
    input.paktype_script[2]=1;input.paktype_script[3]=0;input.paktype_script[4]=1;input.paktype_script[5]=1;
    input.paktype_script[6]=0;input.paktype_script[7]=0;
    auto native=input,connected=input;HandHost prepared(input);
    for(int step=0;step<4;++step) {
        nativeShootingRumble(&native);nativeGraphShootingRumble(&connected);prepared.run(GGV_HAND_RUMBLE);
        sameHand(native,connected);sameHand(native,prepared.state);
    }
    REQUIRE(prepared.state.paktype_index==8);REQUIRE(prepared.state.rumble_pulses==5);
    // Both queried devices narrow to1, but actual pulse inputs include257.
    bool sawWidePulse=false;
    for(int i=0;i<prepared.state.trace_count;++i) {
        const auto &e=prepared.state.trace[i];
        if(e.operation==NS_PAK_TYPE)REQUIRE(e.subject==1);
        if(e.operation==NS_PAK_RUMBLE&&e.subject==257)sawWidePulse=true;
    }
    REQUIRE(sawWidePulse);
}
TEST_CASE("hand slide writes only the selected current-player hand","[gameplay][graph][parity][hand]") {
    for(int hand:{0,1})for(int alias:{0,1}) {
        auto s=fullBaseline();s.handnum=hand;s.passed_is_current=alias;s.passed_slideinc=-3;
        s.current_hand_slideinc[0]=-7;s.current_hand_slideinc[1]=12;
        handParity(s,GGV_HAND_START_SLIDE);
        HandHost host(s);host.run(GGV_HAND_START_SLIDE);
        REQUIRE(host.state.current_hand_slideinc[hand]==1);
        REQUIRE(host.state.current_hand_slideinc[1-hand]==s.current_hand_slideinc[1-hand]);
        REQUIRE(host.state.passed_slideinc==(alias?1:-3));REQUIRE(host.state.trace_count==1);
        REQUIRE(host.state.joy_pad_queries==0);REQUIRE(host.state.rumble_pulses==0);
    }
}
TEST_CASE("four-input hand services refuse missing callbacks and operand types before effects","[gameplay][graph][hand]") {
    HandHost untouched(fullBaseline());int32_t result=123;
    ggv_host noFourth{&untouched,HandHost::read,HandHost::write,HandHost::call};
    REQUIRE_FALSE(ggvExecute(ggvStockProgram(GGV_HAND_RUMBLE),&noFourth,&result));
    ggv_host noThree{&untouched,HandHost::read,HandHost::write,nullptr,HandHost::call4};
    REQUIRE_FALSE(ggvExecute(ggvStockProgram(GGV_HAND_RUMBLE),&noThree,&result));
    REQUIRE(result==123);REQUIRE(untouched.state.trace_count==0);REQUIRE(untouched.state.joy_pad_queries==0);
    auto source=ggvStockProgram(GGV_HAND_RUMBLE)->source;ggv_program destination{};char error[128]{};
    unsigned first=0;while(first<source.count&&source.nodes[first].op!=GGV_CALL4)++first;
    REQUIRE(first<source.count);
    source.nodes[first].d=31;REQUIRE_FALSE(ggvPrepare(&source,&destination,error,sizeof(error)));
    source=ggvStockProgram(GGV_HAND_RUMBLE)->source;source.nodes[first].d=32;
    REQUIRE_FALSE(ggvPrepare(&source,&destination,error,sizeof(error)));
    source=ggvStockProgram(GGV_HAND_RUMBLE)->source;source.nodes[first].b=10;
    REQUIRE_FALSE(ggvPrepare(&source,&destination,error,sizeof(error))); // I32 cannot silently become F32.
    source=ggvStockProgram(GGV_HAND_RUMBLE)->source;source.nodes[first].service4=static_cast<ggv_service4>(999);
    REQUIRE_FALSE(ggvPrepare(&source,&destination,error,sizeof(error)));
}

#include <memory>
namespace {
/* Independent prepared-program host. Narrow engine responses are explicit
 * fixture inputs. Existing complete control/commit and new recovery programs
 * execute synchronously; neither policy nor RNG is replaced by a counter. */
struct AttackHost {
    native_shooting_attack_case state{};
    int animationToken{1}; float scratch[12]; // Native leaves initialize/read only coefficients0,1.
    bool graphResolved{},definitionCaptured{},functionResolved{};
    explicit AttackHost(const native_shooting_attack_case &input):state(input) {}
    static int32_t bits(float f){return std::bit_cast<int32_t>(f);}
    void event(int operation,int subject=-1,int a=0,int b=0,int c=0,int result=0,
               const float *first=nullptr,unsigned firstCount=0,const float *second=nullptr,unsigned secondCount=0,
               const float *output=nullptr,unsigned outputCount=0) {
        const int32_t scalar[4]={a,b,c,result};
        nativeShootingAttackRecordEvent(&state,NTA_EVENT_SERVICE,operation,subject,scalar,first,firstCount,second,secondCount,output,outputCount);
    }
    void normalize(bool driver) {
        auto &s=state;REQUIRE(s.shooting.handnum==0||s.shooting.handnum==1);
        s.animation_latch=uint32_t(s.animation_latch)&1u;
        s.shooting.shotgun=s.weaponnum==19;s.shooting.mauler=s.weaponnum==6;s.shooting.laser=s.weaponnum==29;
        s.shooting.legacy_function_absent=s.function_absent;
        s.shooting.current_hand_lastshootframe[1-s.shooting.handnum]=s.shooting.other_lastshootframe;
        if(s.shooting.passed_is_current) {
            s.shooting.current_hand_lastshootframe[s.shooting.handnum]=s.shooting.lastshootframe;
            s.shooting.passed_slideinc=s.shooting.current_hand_slideinc[s.shooting.handnum];
        }
        if(driver&&!s.gkef_initialized){s.gkef_initialized=1;s.gkef_state=99;}
    }
    static ggv_value read(void *opaque,ggv_binding binding) {
        auto &h=*static_cast<AttackHost *>(opaque);auto &s=h.state;ggv_value v{};
        switch(binding) {
        case GGV_RECOVERY_PAL:v.b=PAL;break;
        case GGV_RECOVERY_JPN:v.b=VERSION >= VERSION_JPN_FINAL;break;
        case GGV_GRAPH_RECOIL_24_HAS:REQUIRE(h.graphResolved);REQUIRE(s.shooting.graph_present);v.b=s.graph_has_unk24;break;
        case GGV_GRAPH_RECOIL_24:REQUIRE(h.graphResolved);REQUIRE(s.shooting.graph_present);v.i=s.graph_unk24;break;
        case GGV_LEGACY_RECOIL_24:v.i=HandHost::narrow8(s.legacy_unk24);break;
        case GGV_GRAPH_RECOIL_25_HAS:REQUIRE(h.graphResolved);REQUIRE(s.shooting.graph_present);v.b=s.graph_has_unk25;break;
        case GGV_GRAPH_RECOIL_25:REQUIRE(h.graphResolved);REQUIRE(s.shooting.graph_present);v.i=s.graph_unk25;break;
        case GGV_LEGACY_RECOIL_25:v.i=HandHost::narrow8(s.legacy_unk25);break;
        case GGV_GRAPH_RECOIL_26_HAS:REQUIRE(h.graphResolved);REQUIRE(s.shooting.graph_present);v.b=s.graph_has_unk26;break;
        case GGV_GRAPH_RECOIL_26:REQUIRE(h.graphResolved);REQUIRE(s.shooting.graph_present);v.i=s.graph_unk26;break;
        case GGV_LEGACY_RECOIL_26:v.i=HandHost::narrow8(s.legacy_unk26);break;
        case GGV_GRAPH_RECOIL_27_HAS:REQUIRE(h.graphResolved);REQUIRE(s.shooting.graph_present);v.b=s.graph_has_unk27;break;
        case GGV_GRAPH_RECOIL_27:REQUIRE(h.graphResolved);REQUIRE(s.shooting.graph_present);v.i=s.graph_unk27;break;
        case GGV_LEGACY_RECOIL_27:v.i=HandHost::narrow8(s.legacy_unk27);break;
        case GGV_GRAPH_RECOIL_TIME_HAS:REQUIRE(h.graphResolved);REQUIRE(s.shooting.graph_present);v.b=s.graph_has_recovery;break;
        case GGV_GRAPH_RECOIL_TIME:REQUIRE(h.graphResolved);REQUIRE(s.shooting.graph_present);v.i=s.graph_recovery;break;
        case GGV_LEGACY_RECOIL_TIME:v.i=HandHost::narrow8(s.legacy_recovery);break;
        case GGV_GRAPH_RECOIL_DIST_HAS:REQUIRE(h.graphResolved);REQUIRE(s.shooting.graph_present);v.b=s.graph_has_recoildist;break;
        case GGV_GRAPH_RECOIL_DIST:REQUIRE(h.graphResolved);REQUIRE(s.shooting.graph_present);v.f=s.graph_recoildist;break;
        case GGV_LEGACY_RECOIL_DIST:v.f=s.legacy_recoildist;break;
        case GGV_GRAPH_RECOIL_ANGLE_HAS:REQUIRE(h.graphResolved);REQUIRE(s.shooting.graph_present);v.b=s.graph_has_recoilangle;break;
        case GGV_GRAPH_RECOIL_ANGLE:REQUIRE(h.graphResolved);REQUIRE(s.shooting.graph_present);v.f=s.graph_recoilangle;break;
        case GGV_LEGACY_RECOIL_ANGLE:v.f=s.legacy_recoilangle;break;
        case GGV_TRIGGER_RELEASED:v.b=s.triggerreleased;break;
        case GGV_ROTX_START:v.f=s.rotxstart;break;
        case GGV_ROTX_END:v.f=s.rotxend;break;
        case GGV_ROTX_OFFSET:v.f=s.rotxoffset;break;
        case GGV_USE_POSROT:v.b=s.useposrot;break;
        case GGV_STATE_MINOR:v.i=s.stateminor;break;
        case GGV_ANIMATION_LATCH:v.b=s.animation_latch;break;
        case GGV_MATMOT2:v.f=s.matmot2;break;
        case GGV_MATMOT1:v.f=s.shooting.matmot;break;
        case GGV_POS_START_X:v.f=s.posstart[0];break;
        case GGV_POS_START_Y:v.f=s.posstart[1];break;
        case GGV_POS_START_Z:v.f=s.posstart[2];break;
        case GGV_POS_END_X:v.f=s.posend[0];break;
        case GGV_POS_END_Y:v.f=s.posend[1];break;
        case GGV_POS_END_Z:v.f=s.posend[2];break;
        case GGV_POS_OFFSET_X:v.f=s.posoffset[0];break;
        case GGV_POS_OFFSET_Y:v.f=s.posoffset[1];break;
        case GGV_POS_OFFSET_Z:v.f=s.posoffset[2];break;
        case GGV_AIM_POS_X:v.f=s.aimpos[0];break;
        case GGV_AIM_POS_Z:v.f=s.aimpos[2];break;
        case GGV_DEFINITION_POS_Z:REQUIRE(h.definitionCaptured);v.f=s.definition_posz;break;
        case GGV_RECOIL_PI:v.f=3.141592741f;break;
        case GGV_RECOIL_BAD_TAU:v.f=3.141092641f*2;break;
        case GGV_POS_MATRIX:v.ref=s.posrotmtx;break;
        case GGV_POS_COORD:v.ref=s.posoffset;break;
        case GGV_STATE_CYCLES:v.i=s.statecycles;break;
        case GGV_BIND_ANIMATION_BUSY:v.b=s.animmode == 2;break;
        case GGV_FIRE_ANIMATION:v.ref=s.fire_animation_present?&h.animationToken:nullptr;break;
        case GGV_PASSED_HAND:v.ref=&s;break;
        case GGV_VIBRATION_START:v.ref=s.vibration_start_present?s.vibration_start:nullptr;break;
        case GGV_VIBRATION_MAX:v.ref=s.vibration_max_present?s.vibration_max:nullptr;break;
        case GGV_VIBRATION_SCRATCH:v.ref=h.scratch;break;
        case GGV_VIBRATION_UPGRADE:v.ref=s.upgrademult;break;
        case GGV_VIBRATION_FINAL:v.ref=s.finalmult;break;
        case GGV_TRIGGER:v.b=s.shooting.trigger;break;
        case GGV_STATE_FLAGS:v.u=s.shooting.stateflags;break;
        case GGV_STATE_VAR1:v.i=s.shooting.statevar1;break;
        case GGV_STATE_FRAMES:v.i=s.shooting.stateframes;break;
        case GGV_SPOOL:v.f=s.shooting.spool;break;
        case GGV_BURST_BULLETS:v.i=s.shooting.burstbullets;break;
        case GGV_LEGACY_FUNCTION_TYPE:v.i=s.shooting.legacy_functype;break;
        case GGV_HAND_NUMBER:v.i=s.shooting.handnum;break;
        case GGV_SHOTGUN:v.b=s.weaponnum == 0x13;break;
        case GGV_MAULER:v.b=s.weaponnum == 6;break;
        default:FAIL("Attack read outside borrowed host domain");break;
        } return v;
    }
    static void write(void *opaque,ggv_binding binding,ggv_value v) {
        auto &h=*static_cast<AttackHost *>(opaque);auto &s=h.state;int operation=-1;
        switch(binding) {
        case GGV_STATE_FLAGS:s.shooting.stateflags=v.u;operation=NTA_WRITE_STATE_FLAGS;break;
        case GGV_STATE_VAR1:s.shooting.statevar1=v.i;operation=NTA_WRITE_STATE_VAR1;break;
        case GGV_SPOOL:s.shooting.spool=v.f;operation=NTA_WRITE_SPOOL;break;
        case GGV_BURST_BULLETS:s.shooting.burstbullets=v.i;operation=NTA_WRITE_BURST;break;
        case GGV_ROTX_START:s.rotxstart=v.f;operation=NTA_WRITE_ROTX_START;break;
        case GGV_ROTX_END:s.rotxend=v.f;operation=NTA_WRITE_ROTX_END;break;
        case GGV_ROTX_OFFSET:s.rotxoffset=v.f;operation=NTA_WRITE_ROTX_OFFSET;break;
        case GGV_USE_POSROT:s.useposrot=v.b;operation=NTA_WRITE_USE_POS_ROT;break;
        case GGV_STATE_MINOR:s.stateminor=v.i;operation=NTA_WRITE_STATE_MINOR;break;
        case GGV_ANIMATION_LATCH:s.animation_latch=(uint32_t(v.b)&1u);operation=NTA_WRITE_ANIMATION_LATCH;break;
        case GGV_MATMOT2:s.matmot2=v.f;operation=NTA_WRITE_MATMOT2;break;
        case GGV_MATMOT1:s.shooting.matmot=v.f;operation=NTA_WRITE_MATMOT1;break;
        case GGV_POS_START_X:s.posstart[0]=v.f;operation=NTA_WRITE_POS_START_X;break;
        case GGV_POS_START_Y:s.posstart[1]=v.f;operation=NTA_WRITE_POS_START_Y;break;
        case GGV_POS_START_Z:s.posstart[2]=v.f;operation=NTA_WRITE_POS_START_Z;break;
        case GGV_POS_END_X:s.posend[0]=v.f;operation=NTA_WRITE_POS_END_X;break;
        case GGV_POS_END_Y:s.posend[1]=v.f;operation=NTA_WRITE_POS_END_Y;break;
        case GGV_POS_END_Z:s.posend[2]=v.f;operation=NTA_WRITE_POS_END_Z;break;
        case GGV_POS_OFFSET_X:s.posoffset[0]=v.f;operation=NTA_WRITE_POS_OFFSET_X;break;
        case GGV_POS_OFFSET_Y:s.posoffset[1]=v.f;operation=NTA_WRITE_POS_OFFSET_Y;break;
        case GGV_POS_OFFSET_Z:s.posoffset[2]=v.f;operation=NTA_WRITE_POS_OFFSET_Z;break;
        default:FAIL("Attack write outside borrowed host domain");break;
        }
        const int32_t scalar[4]={};
        nativeShootingAttackRecordEvent(&s,NTA_EVENT_WRITE,operation,-1,scalar,nullptr,0,nullptr,0,nullptr,0);
    }
    static ggv_value call(void *opaque,ggv_service service,ggv_value a,ggv_value b,ggv_value c) {
        auto &h=*static_cast<AttackHost *>(opaque);auto &s=h.state;ggv_value v{};
        switch(service) {
        case GGV_RESOLVE_GRAPH:h.graphResolved=true;v.b=s.shooting.graph_present!=0;h.event(NTA_RESOLVE_GRAPH,-1,s.weaponnum,0,0,v.b);break;
        case GGV_RESOLVE_FUNCTION:h.functionResolved=true;v.b=!s.function_absent;h.event(NTA_RESOLVE_FUNCTION,-1,s.weaponnum,0,0,v.b);break;
        case GGV_CAPTURE_DEFINITION:h.definitionCaptured=true;break;
        case GGV_RECOIL_TICKS:
#if PAL
            v.i=std::bit_cast<int32_t>(uint32_t(a.i)*50u)/60;
#else
            v.i=a.i;
#endif
            break;
        case GGV_RECOIL_SIN:v.f=s.sin_result;h.event(NTA_SIN,-1,bits(a.f),0,0,bits(v.f));break;
        case GGV_RECOIL_COS:v.f=s.cos_result;h.event(NTA_COS,-1,bits(a.f),0,0,bits(v.f));break;
        case GGV_RECOIL_TWEEN:v.f=s.tween_result;h.event(NTA_TWEEN,-1,bits(a.f),bits(b.f),bits(c.f),bits(v.f));break;
        case GGV_RECOIL_AIM_X:v.f=s.aim_x_result;h.event(NTA_AIM_X,s.shooting.handnum,0,0,0,bits(v.f));break;
        case GGV_MATRIX_X:
            REQUIRE(b.ref==s.posrotmtx);std::memcpy(s.posrotmtx,s.matrix_x_result,sizeof(s.posrotmtx));
            h.event(NTA_MATRIX_X,-1,bits(a.f),0,0,0,nullptr,0,nullptr,0,s.posrotmtx,16);break;
        case GGV_MATRIX_TRANSLATION: {
            REQUIRE(a.ref==s.posoffset);REQUIRE(b.ref==s.posrotmtx);float prior[16];std::memcpy(prior,s.posrotmtx,sizeof(prior));
            // Mocked numerical response; only the native three-store footprint.
            s.posrotmtx[12]=s.matrix_translation_result[12];
            s.posrotmtx[13]=s.matrix_translation_result[13];
            s.posrotmtx[14]=s.matrix_translation_result[14];
            h.event(NTA_MATRIX_TRANSLATE,-1,0,0,0,0,s.posoffset,3,prior,16,s.posrotmtx,16);break;
        }
        case GGV_MATRIX_IDENTITY:
            REQUIRE(a.ref==s.posrotmtx);std::memcpy(s.posrotmtx,s.matrix_identity_result,sizeof(s.posrotmtx));
            h.event(NTA_MATRIX_IDENTITY,-1,0,0,0,0,nullptr,0,nullptr,0,s.posrotmtx,16);break;
        case GGV_OVERRIDE_GKEF: {
            const auto before=s.gkef_state;if(s.gkef_override_enabled)s.gkef_state=s.gkef_override_value;
            h.event(NTA_OVERRIDE_GKEF,-1,std::bit_cast<int32_t>(before),0,0,std::bit_cast<int32_t>(s.gkef_state));break;
        }
        case GGV_FIRE_ANIMATION_START:
            REQUIRE(a.ref==&h.animationToken);REQUIRE(b.i==s.shooting.handnum);REQUIRE(c.ref==&s);
            h.event(NTA_START_ANIMATION,-1,1,b.i);break;
        case GGV_ATTACK_READY:
            REQUIRE(a.ref==&s);REQUIRE(b.i==2);v.b=s.animation_ready_result!=0;
            h.event(NTA_ANIMATION_READY,-1,b.i,0,0,v.b);break;
        case GGV_VIBRATION_MULTIPLY: {
            REQUIRE(a.ref==s.upgrademult);REQUIRE(b.ref==h.scratch);REQUIRE(c.ref==s.finalmult);
            float upgrade[2];std::memcpy(upgrade,s.upgrademult,sizeof(upgrade));
            // Explicitly mocked arithmetic; the actual native leaf stores only0,1.
            std::memcpy(s.finalmult,s.vibration_multiply_result,2*sizeof(float));
            h.event(NTA_VIBRATION_MULTIPLY,-1,0,0,0,0,upgrade,2,h.scratch,2,s.finalmult,2);break;
        }
        case GGV_ATTACK_CONTROL: {
            const auto start=s.shooting.trace_count;
            const auto initialized=s.shooting.overrides_initialized;const auto on=s.shooting.rontime_state,off=s.shooting.rofftime_state;
            v.i=nativeGraphShootingFullControl(&s.shooting);
            s.shooting.overrides_initialized=initialized;s.shooting.rontime_state=on;s.shooting.rofftime_state=off;
            if(s.shooting.passed_is_current)s.shooting.passed_slideinc=s.shooting.current_hand_slideinc[s.shooting.handnum];
            nativeShootingAttackForwardShootingTrace(&s,start);
            h.event(NTA_CONTROL,-1,s.shooting.legacy_functype,0,0,v.i);break;
        }
        case GGV_ATTACK_COMMIT: {
            const auto start=s.shooting.trace_count;nativeGraphShootingFullCommit(&s.shooting);
            if(s.shooting.passed_is_current)s.shooting.passed_slideinc=s.shooting.current_hand_slideinc[s.shooting.handnum];
            nativeShootingAttackForwardShootingTrace(&s,start);
            h.event(NTA_COMMIT,-1,s.shooting.handnum,s.shooting.info_weaponnum,s.shooting.legacy_functype);break;
        }
        case GGV_ATTACK_RECOVERY: {
            v.b=h.run(GGV_RECOIL_RECOVERY);h.event(NTA_RECOVERY,-1,s.shooting.handnum,s.shooting.info_weaponnum,s.shooting.legacy_functype,v.b);break;
        }
        default:FAIL("Attack service outside narrow host domain");break;
        } return v;
    }
    static ggv_value call4(void *opaque,ggv_service4 service,ggv_value a,ggv_value b,ggv_value c,ggv_value d) {
        auto &h=*static_cast<AttackHost *>(opaque);auto &s=h.state;
        REQUIRE(service==GGV_VIBRATION_INTERPOLATE);REQUIRE(a.ref==s.vibration_start);
        REQUIRE(b.ref==s.vibration_max);REQUIRE(d.ref==h.scratch);
        // Explicitly mocked arithmetic; scratch[2..11] remains uninitialized/unread.
        std::memcpy(h.scratch,s.vibration_mix_result,2*sizeof(float));
        h.event(NTA_VIBRATION_MIX,-1,bits(c.f),0,0,0,s.vibration_start,2,s.vibration_max,2,h.scratch,2);
        return ggv_value{};
    }
    int32_t run(ggv_stock_program program) {
        normalize(program==GGV_ATTACK_SHOOT);ggv_host host{this,read,write,call,call4};int32_t result=987;
        REQUIRE(ggvExecute(ggvStockProgram(program),&host,&result));return result;
    }
};
native_shooting_attack_case attackBaseline() {
    native_shooting_attack_case s{};s.shooting=fullBaseline();s.weaponnum=2;s.stateminor=2;
    s.shooting.stateframes=3;s.shooting.statevar1=0;s.shooting.stateflags=0x20;
    s.legacy_unk24=6;s.legacy_unk25=8;s.legacy_unk26=3;s.legacy_unk27=2;s.legacy_recovery=4;
    s.legacy_recoildist=23;s.legacy_recoilangle=30;s.definition_posz=100;
    s.rotxstart=0.2f;s.rotxend=0.3f;s.rotxoffset=0.4f;s.matmot2=0.7f;
    s.sin_result=0.25f;s.cos_result=-0.5f;s.tween_result=0.125f;s.aim_x_result=14;
    s.animation_ready_result=1;
    for(int i=0;i<3;++i){s.posstart[i]=float(i+1);s.posend[i]=float(i+6);s.posoffset[i]=float(i+10);s.aimpos[i]=float(i-3);}
    for(int i=0;i<16;++i){s.posrotmtx[i]=float(i+4);s.matrix_x_result[i]=float(i+21);s.matrix_translation_result[i]=float(i+41);s.matrix_identity_result[i]=float(i+61);}
    for(int i=0;i<12;++i){s.vibration_start[i]=float(i+1);s.vibration_max[i]=float(i+10);s.upgrademult[i]=float(i+20);s.finalmult[i]=float(i+30);s.vibration_mix_result[i]=float(i+40);s.vibration_multiply_result[i]=float(i+50);}
    return s;
}
void sameAttack(const native_shooting_attack_case &a,const native_shooting_attack_case &b) {
    sameFull(a.shooting,b.shooting);
    REQUIRE(a.stateminor==b.stateminor);REQUIRE(a.statecycles==b.statecycles);REQUIRE(a.triggerreleased==b.triggerreleased);
    REQUIRE(a.animation_latch==b.animation_latch);REQUIRE(a.useposrot==b.useposrot);REQUIRE(a.animmode==b.animmode);
    REQUIRE(std::bit_cast<uint32_t>(a.matmot2)==std::bit_cast<uint32_t>(b.matmot2));
    REQUIRE(std::bit_cast<uint32_t>(a.shooting.matmot)==std::bit_cast<uint32_t>(b.shooting.matmot));
    REQUIRE(std::bit_cast<uint32_t>(a.rotxstart)==std::bit_cast<uint32_t>(b.rotxstart));
    REQUIRE(std::bit_cast<uint32_t>(a.rotxend)==std::bit_cast<uint32_t>(b.rotxend));
    REQUIRE(std::bit_cast<uint32_t>(a.rotxoffset)==std::bit_cast<uint32_t>(b.rotxoffset));
    REQUIRE(std::memcmp(a.posstart,b.posstart,sizeof(a.posstart))==0);REQUIRE(std::memcmp(a.posend,b.posend,sizeof(a.posend))==0);
    REQUIRE(std::memcmp(a.posoffset,b.posoffset,sizeof(a.posoffset))==0);REQUIRE(std::memcmp(a.posrotmtx,b.posrotmtx,sizeof(a.posrotmtx))==0);
    REQUIRE(std::memcmp(a.upgrademult,b.upgrademult,sizeof(a.upgrademult))==0);REQUIRE(std::memcmp(a.finalmult,b.finalmult,sizeof(a.finalmult))==0);
    REQUIRE(a.gkef_initialized==b.gkef_initialized);REQUIRE(a.gkef_state==b.gkef_state);
    REQUIRE(a.program_refused==0);REQUIRE(b.program_refused==0);REQUIRE(a.trace_overflow==0);REQUIRE(b.trace_overflow==0);
    REQUIRE(a.trace_count==b.trace_count);
    for(int i=0;i<a.trace_count;++i){INFO("ordered recoil/attack effect "<<i);REQUIRE(std::memcmp(&a.trace[i],&b.trace[i],sizeof(a.trace[i]))==0);}
}
void attackParity(const native_shooting_attack_case &initial,ggv_stock_program program) {
    auto nativeOwner=std::make_unique<native_shooting_attack_case>(initial),connectedOwner=std::make_unique<native_shooting_attack_case>(initial);
    auto preparedOwner=std::make_unique<AttackHost>(initial);
    auto &native=*nativeOwner,&connected=*connectedOwner;auto &prepared=*preparedOwner;
    const auto expected=program==GGV_RECOIL_RECOVERY?nativeShootingRecovery(&native):nativeShootingAttack(&native);
    REQUIRE((program==GGV_RECOIL_RECOVERY?nativeGraphShootingRecovery(&connected):nativeGraphShootingAttack(&connected))==expected);
    REQUIRE(prepared.run(program)==expected);sameAttack(native,connected);sameAttack(native,prepared.state);
}
}
TEST_CASE("complete recoil preserves lazy graph legacy timing and version thresholds","[gameplay][graph][parity][recoil]") {
    for(int graph:{0,1})for(unsigned mask=0;mask<128;++mask)for(int frame:{-1,0,3,6,14,19}) {
        auto s=attackBaseline();s.shooting.graph_present=graph;s.shooting.stateframes=frame;
        s.graph_has_unk24=mask&1;s.graph_has_unk25=mask&2;s.graph_has_unk26=mask&4;s.graph_has_unk27=mask&8;
        s.graph_has_recovery=mask&16;s.graph_has_recoildist=mask&32;s.graph_has_recoilangle=mask&64;
        s.graph_unk24=3;s.graph_unk25=4;s.graph_unk26=5;s.graph_unk27=3;s.graph_recovery=5;
        s.graph_recoildist=71;s.graph_recoilangle=-45;
        attackParity(s,GGV_RECOIL_RECOVERY);
    }
    for(int first:{-129,-1,0,1,3,4,127,128})for(int second:{-1,0,1,4}) {
        auto s=attackBaseline();s.shooting.graph_present=0;s.legacy_unk24=first;s.legacy_unk25=second;
        s.shooting.stateframes=30;attackParity(s,GGV_RECOIL_RECOVERY);
    }
}
TEST_CASE("recoil interruption snapshots pose returns matrix identity and persists across calls","[gameplay][graph][parity][recoil]") {
    for(unsigned flags:{0u,0x40u,0x80u,0xc0u})for(int trigger:{0,1,-1})for(int release:{0,1,-1})for(int frame:{1,3,4,6,14,18}) {
        auto s=attackBaseline();s.shooting.stateflags=flags;s.shooting.trigger=trigger;s.triggerreleased=release;
        s.shooting.stateframes=frame;s.legacy_unk27=4;attackParity(s,GGV_RECOIL_RECOVERY);
    }
    auto initial=attackBaseline();initial.shooting.trigger=1;initial.triggerreleased=1;initial.legacy_unk27=4;
    auto nativeOwner=std::make_unique<native_shooting_attack_case>(initial),connectedOwner=std::make_unique<native_shooting_attack_case>(initial);
    auto preparedOwner=std::make_unique<AttackHost>(initial);
    auto &native=*nativeOwner,&connected=*connectedOwner;auto &prepared=*preparedOwner;
    for(int frame:{3,4,5,8,17}) {
        native.shooting.stateframes=frame;connected.shooting.stateframes=frame;prepared.state.shooting.stateframes=frame;
        const auto expected=nativeShootingRecovery(&native);REQUIRE(nativeGraphShootingRecovery(&connected)==expected);
        REQUIRE(prepared.run(GGV_RECOIL_RECOVERY)==expected);sameAttack(native,connected);sameAttack(native,prepared.state);
        nativeShootingAttackResetTrace(&native);nativeShootingAttackResetTrace(&connected);nativeShootingAttackResetTrace(&prepared.state);
    }
}
TEST_CASE("attack driver composes null initial active recovery and symbolic weapon branches","[gameplay][graph][parity][attack]") {
    for(int minor:{-1,0,1,2,3})for(int absent:{0,1})for(int cycle:{0,1})for(int ready:{0,1,-1})for(int animation:{0,1}) {
        auto s=attackBaseline();s.stateminor=minor;s.function_absent=absent;s.statecycles=cycle;
        s.animation_ready_result=ready;s.fire_animation_present=animation;s.gkef_override_enabled=1;s.gkef_override_value=0xffffffffu;
        s.shooting.trigger=0;attackParity(s,GGV_ATTACK_SHOOT);
    }
    for(int weapon:{2,6,0x13})for(int busy:{0,2})for(int trigger:{0,1})for(int recoil:{0,1}) {
        auto s=attackBaseline();s.weaponnum=weapon;s.animmode=busy;s.shooting.trigger=trigger;
        s.shooting.stateflags=recoil?0x20u:0u;s.shooting.stateframes=40;s.shooting.matmot=7;
        attackParity(s,GGV_ATTACK_SHOOT);
    }
}
TEST_CASE("attack driver preserves vibration guards complete control commit effects and repeated static state","[gameplay][graph][parity][attack]") {
    for(int start:{0,1})for(int maximum:{0,1})for(int release:{0,1})for(int type:{1,0x101,0x201})for(int trigger:{0,1}) {
        auto s=attackBaseline();s.stateminor=1;s.shooting.legacy_functype=type;s.vibration_start_present=start;s.vibration_max_present=maximum;
        s.triggerreleased=release;s.shooting.trigger=trigger;s.shooting.flags=0;s.shooting.legacy_flags=0;
        s.shooting.spool=0.2f;s.animation_latch=1;attackParity(s,GGV_ATTACK_SHOOT);
    }
    auto input=attackBaseline();input.stateminor=0;input.fire_animation_present=1;input.shooting.flags=0;
    input.shooting.trigger=1;input.triggerreleased=0;input.gkef_override_enabled=1;input.gkef_override_value=17;
    auto nativeOwner=std::make_unique<native_shooting_attack_case>(input),connectedOwner=std::make_unique<native_shooting_attack_case>(input);
    auto preparedOwner=std::make_unique<AttackHost>(input);
    auto &native=*nativeOwner,&connected=*connectedOwner;auto &prepared=*preparedOwner;
    for(int step=0;step<3;++step) {
        const auto expected=nativeShootingAttack(&native);REQUIRE(nativeGraphShootingAttack(&connected)==expected);
        REQUIRE(prepared.run(GGV_ATTACK_SHOOT)==expected);sameAttack(native,connected);sameAttack(native,prepared.state);
        nativeShootingAttackResetTrace(&native);nativeShootingAttackResetTrace(&connected);nativeShootingAttackResetTrace(&prepared.state);
        native.shooting.stateframes+=3;connected.shooting.stateframes+=3;prepared.state.shooting.stateframes+=3;
    }
}
TEST_CASE("attack and recovery malformed services and registers refuse before host mutation","[gameplay][graph][attack]") {
    auto untouchedOwner=std::make_unique<AttackHost>(attackBaseline());auto &untouched=*untouchedOwner;ggv_host noFourth{&untouched,AttackHost::read,AttackHost::write,AttackHost::call};int32_t result=123;
    REQUIRE_FALSE(ggvExecute(ggvStockProgram(GGV_ATTACK_SHOOT),&noFourth,&result));
    ggv_host noThree{&untouched,AttackHost::read,AttackHost::write,nullptr,AttackHost::call4};
    REQUIRE_FALSE(ggvExecute(ggvStockProgram(GGV_RECOIL_RECOVERY),&noThree,&result));
    REQUIRE(result==123);REQUIRE(untouched.state.trace_count==0);REQUIRE_FALSE(untouched.graphResolved);REQUIRE_FALSE(untouched.functionResolved);
    auto source=ggvStockProgram(GGV_ATTACK_SHOOT)->source;ggv_program destination{};char error[128]{};
    unsigned first=0;while(first<source.count&&source.nodes[first].op!=GGV_CALL4)++first;REQUIRE(first<source.count);
    source.nodes[first].d=31;REQUIRE_FALSE(ggvPrepare(&source,&destination,error,sizeof(error)));
    source=ggvStockProgram(GGV_ATTACK_SHOOT)->source;source.nodes[first].c=21;
    REQUIRE_FALSE(ggvPrepare(&source,&destination,error,sizeof(error)));
    source=ggvStockProgram(GGV_RECOIL_RECOVERY)->source;
    first=0;while(first<source.count&&source.nodes[first].op!=GGV_F_BLEND)++first;REQUIRE(first<source.count);
    source.nodes[first].c=31;REQUIRE_FALSE(ggvPrepare(&source,&destination,error,sizeof(error)));
    REQUIRE(result==123);REQUIRE(untouched.state.trace_count==0);
}

TEST_CASE("attack driver exposes each real complete control result without a scripted policy result","[gameplay][graph][parity][attack]") {
    for(int expected:{-1,0,1,2}) {
        auto input=attackBaseline();input.stateminor=1;input.shooting.graph_present=0;
        input.shooting.legacy_functype=1;input.shooting.legacy_flags=0;input.shooting.stateflags=0;
        input.shooting.trigger=1;input.shooting.burstbullets=0;input.shooting.stateframes=1;
        if(expected==-1){input.shooting.trigger=0;input.shooting.stateflags=0x10;} // Held shot: first-shot bypass disabled.
        if(expected==0){input.shooting.legacy_flags=2;input.shooting.burstbullets=1;}
        if(expected==1){input.shooting.legacy_functype=0x101;input.shooting.spool=1;}
        attackParity(input,GGV_ATTACK_SHOOT);
        auto host=std::make_unique<AttackHost>(input);REQUIRE(host->run(GGV_ATTACK_SHOOT)==0);
        unsigned controls=0,commits=0;
        for(int i=0;i<host->state.trace_count;++i) {
            const auto &event=host->state.trace[i];
            if(event.kind==NTA_EVENT_SERVICE&&event.operation==NTA_CONTROL){++controls;REQUIRE(event.scalar[3]==expected);}
            if(event.kind==NTA_EVENT_SERVICE&&event.operation==NTA_COMMIT)++commits;
        }
        REQUIRE(controls==1);REQUIRE(commits==unsigned(expected>0));
        REQUIRE(host->state.stateminor==((expected<0||expected==2)?2:1));
    }
}
TEST_CASE("recoil preserves signed frame wrap and input witnesses across exact engine leaf calls","[gameplay][graph][parity][recoil]") {
    auto s=attackBaseline();s.shooting.stateframes=std::numeric_limits<int32_t>::min();
    s.shooting.statevar1=std::numeric_limits<int32_t>::max();
    attackParity(s,GGV_RECOIL_RECOVERY); // The native -fwrapv expression yields1.
    auto input=attackBaseline();input.shooting.stateframes=3;input.legacy_unk24=6;input.legacy_unk25=8;
    input.sin_result=-0.0f;input.tween_result=std::bit_cast<float>(uint32_t(0x3e000001));
    attackParity(input,GGV_RECOIL_RECOVERY);
    auto host=std::make_unique<AttackHost>(input);REQUIRE(host->run(GGV_RECOIL_RECOVERY)==0);
    bool sine=false,tween=false,translation=false;
    for(int i=0;i<host->state.trace_count;++i) {
        const auto &event=host->state.trace[i];
        if(event.kind!=NTA_EVENT_SERVICE)continue;
        if(event.operation==NTA_SIN) {
            sine=true;
#if PAL
            const int first=6*50/60;
#else
            const int first=6;
#endif
            const float argument=3*1.5707963705063f/float(first);
            REQUIRE(event.scalar[0]==std::bit_cast<int32_t>(argument));
            REQUIRE(event.scalar[3]==std::bit_cast<int32_t>(-0.0f));
        }
        if(event.operation==NTA_TWEEN){tween=true;REQUIRE(event.scalar[2]==std::bit_cast<int32_t>(-0.0f));}
        if(event.operation==NTA_MATRIX_TRANSLATE){translation=true;REQUIRE(event.input0_count==3);REQUIRE(event.input1_count==16);REQUIRE(event.output_count==16);}
    }
    REQUIRE(sine);REQUIRE(tween);REQUIRE(translation);
}

TEST_CASE("attack vibration consumes two coefficients and preserves every destination tail sentinel","[gameplay][graph][parity][attack]") {
    auto input=attackBaseline();input.stateminor=1;input.shooting.trigger=1;
    input.shooting.legacy_functype=0x101;input.shooting.flags=0;input.shooting.legacy_flags=0;
    input.shooting.spool=1;input.vibration_start_present=1;input.vibration_max_present=1;
    // Supplied outputs model only the two real leaf stores. Deliberately distinct
    // unused fixture-result tails must never overwrite the actual final tail.
    for(int i=2;i<12;++i) {
        input.finalmult[i]=std::bit_cast<float>(uint32_t(0x4a123450u+unsigned(i)));
        input.vibration_mix_result[i]=float(3000+i);
        input.vibration_multiply_result[i]=float(4000+i);
    }
    auto nativeOwner=std::make_unique<native_shooting_attack_case>(input),connectedOwner=std::make_unique<native_shooting_attack_case>(input);
    auto preparedOwner=std::make_unique<AttackHost>(input);
    auto &native=*nativeOwner,&connected=*connectedOwner;auto &prepared=*preparedOwner;
    REQUIRE(nativeShootingAttack(&native)==0);REQUIRE(nativeGraphShootingAttack(&connected)==0);
    REQUIRE(prepared.run(GGV_ATTACK_SHOOT)==0);
    sameAttack(native,connected);sameAttack(native,prepared.state);
    for(const auto *state:{&native,&connected,&prepared.state}) {
        for(int i=0;i<2;++i)REQUIRE(std::bit_cast<uint32_t>(state->finalmult[i])==std::bit_cast<uint32_t>(input.vibration_multiply_result[i]));
        for(int i=2;i<12;++i)REQUIRE(std::bit_cast<uint32_t>(state->finalmult[i])==std::bit_cast<uint32_t>(input.finalmult[i]));
        unsigned mixes=0,multiplies=0;int mixIndex=-1,multiplyIndex=-1;
        for(int i=0;i<state->trace_count;++i) {
            const auto &event=state->trace[i];if(event.kind!=NTA_EVENT_SERVICE)continue;
            if(event.operation==NTA_VIBRATION_MIX) {
                ++mixes;mixIndex=i;
                REQUIRE(event.scalar[0]==std::bit_cast<int32_t>(1.0f));
                REQUIRE(event.input0_count==2);REQUIRE(event.input1_count==2);REQUIRE(event.output_count==2);
                for(int coefficient=0;coefficient<2;++coefficient) {
                    REQUIRE(event.input0_bits[coefficient]==std::bit_cast<uint32_t>(input.vibration_start[coefficient]));
                    REQUIRE(event.input1_bits[coefficient]==std::bit_cast<uint32_t>(input.vibration_max[coefficient]));
                    REQUIRE(event.output_bits[coefficient]==std::bit_cast<uint32_t>(input.vibration_mix_result[coefficient]));
                }
            }
            if(event.operation==NTA_VIBRATION_MULTIPLY) {
                ++multiplies;multiplyIndex=i;
                REQUIRE(event.input0_count==2);REQUIRE(event.input1_count==2);REQUIRE(event.output_count==2);
                for(int coefficient=0;coefficient<2;++coefficient) {
                    REQUIRE(event.input0_bits[coefficient]==std::bit_cast<uint32_t>(input.upgrademult[coefficient]));
                    REQUIRE(event.input1_bits[coefficient]==std::bit_cast<uint32_t>(input.vibration_mix_result[coefficient]));
                    REQUIRE(event.output_bits[coefficient]==std::bit_cast<uint32_t>(input.vibration_multiply_result[coefficient]));
                }
            }
        }
        REQUIRE(mixes==1);REQUIRE(multiplies==1);REQUIRE(mixIndex<multiplyIndex);
    }
}

TEST_CASE("recoil translation preserves thirteen matrix cells and witnesses the full initialized state","[gameplay][graph][parity][recoil]") {
    auto input=attackBaseline();input.shooting.stateframes=3;
    for(int i=0;i<16;++i) {
        input.matrix_x_result[i]=std::bit_cast<float>(uint32_t(0x49123450u+unsigned(i)));
        input.matrix_translation_result[i]=float(5000+i);
    }
    auto nativeOwner=std::make_unique<native_shooting_attack_case>(input),connectedOwner=std::make_unique<native_shooting_attack_case>(input);
    auto preparedOwner=std::make_unique<AttackHost>(input);
    auto &native=*nativeOwner,&connected=*connectedOwner;auto &prepared=*preparedOwner;
    REQUIRE(nativeShootingRecovery(&native)==0);REQUIRE(nativeGraphShootingRecovery(&connected)==0);
    REQUIRE(prepared.run(GGV_RECOIL_RECOVERY)==0);
    sameAttack(native,connected);sameAttack(native,prepared.state);
    for(const auto *state:{&native,&connected,&prepared.state}) {
        for(int i=0;i<16;++i) {
            const auto expected=(i>=12&&i<=14)?input.matrix_translation_result[i]:input.matrix_x_result[i];
            REQUIRE(std::bit_cast<uint32_t>(state->posrotmtx[i])==std::bit_cast<uint32_t>(expected));
        }
        unsigned rotations=0,translations=0;int rotationIndex=-1,translationIndex=-1;
        for(int i=0;i<state->trace_count;++i) {
            const auto &event=state->trace[i];if(event.kind!=NTA_EVENT_SERVICE)continue;
            if(event.operation==NTA_MATRIX_X){++rotations;rotationIndex=i;}
            if(event.operation==NTA_MATRIX_TRANSLATE) {
                ++translations;translationIndex=i;
                REQUIRE(event.input0_count==3);REQUIRE(event.input1_count==16);REQUIRE(event.output_count==16);
                for(int cell=0;cell<16;++cell) {
                    REQUIRE(event.input1_bits[cell]==std::bit_cast<uint32_t>(input.matrix_x_result[cell]));
                    const auto expected=(cell>=12&&cell<=14)?input.matrix_translation_result[cell]:input.matrix_x_result[cell];
                    REQUIRE(event.output_bits[cell]==std::bit_cast<uint32_t>(expected));
                }
                for(int axis=0;axis<3;++axis)REQUIRE(event.input0_bits[axis]==std::bit_cast<uint32_t>(state->posoffset[axis]));
            }
        }
        REQUIRE(rotations==1);REQUIRE(translations==1);REQUIRE(rotationIndex<translationIndex);
    }
}

namespace {
/* Source-contract host for the complete throw IR. Native engine leaves are
 * explicit inert fixture responses; no complete original throw oracle is
 * claimed by these cases. Independent native-body fixture work is a later gate. */
struct ThrowHost {
    bool functionPresent{true},graphPresent{false},animationPresent{false};
    bool hasActivation{},hasRecovery{};
    uint32_t graphFlags{},legacyFlags{};
    int32_t graphAmmo{-1},legacyAmmo{0},ammoIndex{-99};
    int32_t graphActivation{120},legacyActivation{60},graphRecovery{120},legacyRecovery{60};
    int32_t minor{},cycles{},frames{3},handnum{},weapon{2},weaponFunction{},prime{99};
    int32_t released{},trigger{},latch{},firing{},attackType{99};
    int32_t loaded[2]{5,7},secondary{};int animationToken{1};
    int currentPlayer{};uint32_t throwing[2]{};
    // No invented current-player or frame mutation in native leaf fixtures.
    struct Event {int kind,operation,a,b,c;int32_t minor,prime,loaded0,loaded1;int player;};
    std::vector<Event> events;
    std::vector<ggv_binding> reads;
    void event(int kind,int operation,int a=0,int b=0,int c=0) {
        events.push_back({kind,operation,a,b,c,minor,prime,loaded[0],loaded[1],currentPlayer});
    }
    static int32_t narrow16(int32_t value){return std::bit_cast<int16_t>(uint16_t(value));}
    static int32_t ticks(int32_t value) {
#if PAL
        return std::bit_cast<int32_t>(uint32_t(value)*50u)/60;
#else
        return value;
#endif
    }
    static ggv_value read(void *opaque,ggv_binding binding) {
        auto &h=*static_cast<ThrowHost *>(opaque);h.reads.push_back(binding);ggv_value v{};
        switch(binding) {
        case GGV_GRAPH_FLAGS:REQUIRE(h.graphPresent);v.u=h.graphFlags;break;
        case GGV_LEGACY_FLAGS:v.u=h.legacyFlags;break;
        case GGV_GRAPH_AMMO_SLOT:REQUIRE(h.graphPresent);v.i=h.graphAmmo;break;
        case GGV_LEGACY_AMMO_INDEX:v.i=HandHost::narrow8(h.legacyAmmo);break;
        case GGV_AMMO_INDEX:v.i=h.ammoIndex;break;
        case GGV_THROW_GRAPH_HAS_ACTIVATION:REQUIRE(h.graphPresent);v.b=h.hasActivation;break;
        case GGV_THROW_GRAPH_ACTIVATION:REQUIRE(h.graphPresent);REQUIRE(h.hasActivation);v.i=h.graphActivation;break;
        case GGV_THROW_LEGACY_ACTIVATION:v.i=narrow16(h.legacyActivation);break;
        case GGV_THROW_GRAPH_HAS_RECOVERY:REQUIRE(h.graphPresent);v.b=h.hasRecovery;break;
        case GGV_THROW_GRAPH_RECOVERY:REQUIRE(h.graphPresent);REQUIRE(h.hasRecovery);v.i=h.graphRecovery;break;
        case GGV_THROW_LEGACY_RECOVERY:v.i=h.legacyRecovery;break;
        case GGV_STATE_MINOR:v.i=h.minor;break;
        case GGV_STATE_CYCLES:v.i=h.cycles;break;
        case GGV_STATE_FRAMES:v.i=h.frames;break;
        case GGV_FIRE_ANIMATION:v.ref=h.animationPresent?&h.animationToken:nullptr;break;
        case GGV_PASSED_HAND:v.ref=&h;break;
        case GGV_HAND_NUMBER:v.i=h.handnum;break;
        case GGV_TRIGGER_RELEASED:v.b=h.released;break;
        case GGV_TRIGGER:v.b=h.trigger;break;
        case GGV_LOADED_AMMO:REQUIRE(h.ammoIndex>=0);REQUIRE(h.ammoIndex<2);v.i=h.loaded[h.ammoIndex];break;
        case GGV_PRIME_TIMER:v.i=h.prime;break;
        case GGV_CURRENT_PLAYER_THROWING:v.b=h.throwing[h.currentPlayer];break;
        case GGV_WEAPON_NUMBER:v.i=h.weapon;break;
        case GGV_WEAPON_FUNCTION:v.i=h.weaponFunction;break;
        case GGV_VERSION_1_OR_LATER:v.b=VERSION>=VERSION_NTSC_1_0;break;
        default:FAIL("Throw read outside native qualified binding domain");break;
        }return v;
    }
    static void write(void *opaque,ggv_binding binding,ggv_value v) {
        auto &h=*static_cast<ThrowHost *>(opaque);
        switch(binding) {
        case GGV_AMMO_INDEX:h.ammoIndex=v.i;return; // Native local, no engine store event.
        case GGV_STATE_MINOR:h.minor=v.i;break;
        case GGV_ANIMATION_LATCH:h.latch=uint32_t(v.b)&1u;break;
        case GGV_FIRING:h.firing=std::bit_cast<int8_t>(uint8_t(v.b));break;
        case GGV_ATTACK_TYPE:h.attackType=v.i;break;
        case GGV_LOADED_AMMO:REQUIRE(h.ammoIndex>=0);REQUIRE(h.ammoIndex<2);h.loaded[h.ammoIndex]=v.i;break;
        case GGV_PRIME_TIMER:h.prime=narrow16(v.i);break;
        case GGV_CURRENT_PLAYER_THROWING:h.throwing[h.currentPlayer]=uint32_t(v.b)&1u;break;
        default:FAIL("Throw write outside native qualified binding domain");break;
        }h.event(1,binding);
    }
    static ggv_value call(void *opaque,ggv_service service,ggv_value a,ggv_value b,ggv_value c) {
        auto &h=*static_cast<ThrowHost *>(opaque);ggv_value v{};
        switch(service) {
        case GGV_RESOLVE_FUNCTION:v.b=h.functionPresent;h.event(2,service);break;
        case GGV_RESOLVE_GRAPH:v.b=h.graphPresent;h.event(2,service);break;
        case GGV_THROW_REMOVE_ITEM:
            REQUIRE(a.i==h.weapon);
            h.event(2,service,a.i);break;
        case GGV_THROW_SWITCH_PREVIOUS:case GGV_THROW_AUTO_SWITCH:h.event(2,service);break;
        case GGV_THROW_RESET_ANIMATION:
            REQUIRE(a.ref==&h);h.latch=0; // Actual reset clears the modeled latch; other animation fields unqualified.
            h.event(2,service);break;
        case GGV_THROW_SECONDARY_VALUE:v.i=h.secondary;h.event(2,service,v.i);break;
        case GGV_RECOIL_TICKS:v.i=ticks(a.i);break; // Pure macro, not an engine effect.
        case GGV_FIRE_ANIMATION_START:
            REQUIRE(a.ref==&h.animationToken);REQUIRE(b.i==h.handnum);REQUIRE(c.ref==&h);
            h.event(2,service,h.animationToken,b.i);break;
        case GGV_ATTACK_READY:
            REQUIRE(a.ref==&h);REQUIRE(b.i==2);v.b=h.ready;
            h.event(2,service,b.i,v.b);break;
        default:FAIL("Throw call outside explicit leaf domain");break;
        }return v;
    }
    int ready{};
    int32_t run(){ggv_host host{this,read,write,call};int32_t result=987;REQUIRE(ggvExecute(ggvStockProgram(GGV_THROW_CONTROL),&host,&result));return result;}
    int count(int kind,int operation)const {return int(std::count_if(events.begin(),events.end(),[&](const auto &e){return e.kind==kind&&e.operation==operation;}));}
    int index(int kind,int operation)const {for(unsigned i=0;i<events.size();++i)if(events[i].kind==kind&&events[i].operation==operation)return int(i);return -1;}
};
}
TEST_CASE("complete throw resolves both witnesses before null refusal and selects raw configuration lazily","[gameplay][graph][throw]") {
    ThrowHost absent;absent.functionPresent=false;absent.graphPresent=true;
    REQUIRE(absent.run()==1);REQUIRE(absent.events.size()==2);REQUIRE(absent.reads.empty());
    REQUIRE(absent.events[0].operation==GGV_RESOLVE_FUNCTION);REQUIRE(absent.events[1].operation==GGV_RESOLVE_GRAPH);
    REQUIRE(absent.prime==99);REQUIRE(absent.loaded[0]==5);REQUIRE(absent.loaded[1]==7);
    for(bool graph:{false,true})for(int slot:{-2,-1,0,1})for(int legacy:{-1,0,1,128}) {
        ThrowHost h;h.minor=1;h.graphPresent=graph;h.graphAmmo=slot;h.legacyAmmo=legacy;
        const int expected=(graph&&slot>=0)?slot:HandHost::narrow8(legacy);
        REQUIRE(h.run()==0);REQUIRE(h.ammoIndex==expected);REQUIRE(h.minor==2);REQUIRE(h.attackType==3);REQUIRE(h.firing==1);
        REQUIRE(h.loaded[0]==(expected==0?4:5));REQUIRE(h.loaded[1]==(expected==1?6:7));
        REQUIRE(h.count(1,GGV_LOADED_AMMO)==int(expected>=0));REQUIRE(h.prime==99);
    }
    for(bool graph:{false,true})for(bool recovery:{false,true}) {
        ThrowHost h;h.minor=2;h.graphPresent=graph;h.hasRecovery=recovery;
        h.frames=ThrowHost::ticks(60)+1;
        REQUIRE(h.run()==int(!(graph&&recovery))); // graph recovery120 remains pending.
        REQUIRE(h.count(1,GGV_PRIME_TIMER)==0);REQUIRE(h.count(2,GGV_THROW_RESET_ANIMATION)==0);
    }
}
TEST_CASE("throw discard preserves inventory current-owner switch and prime store order","[gameplay][graph][throw]") {
    for(bool graph:{false,true})for(bool selectedDiscard:{false,true})for(int cycle:{0,1}) {
        ThrowHost h;h.graphPresent=graph;h.cycles=cycle;h.animationPresent=true;h.ready=0;
        h.graphFlags=graph?(selectedDiscard?0x40000u:0u):0x40000u;
        h.legacyFlags=graph?0x40000u:(selectedDiscard?0x40000u:0u);h.currentPlayer=1;
        const bool discard=selectedDiscard&&cycle==0;
        REQUIRE(h.run()==int(discard));REQUIRE(h.count(2,GGV_THROW_REMOVE_ITEM)==int(discard));
        if(discard) {
            REQUIRE(h.events.size()==6); // two resolves, remove, throwing store, switch, prime store.
            REQUIRE(h.events[2].operation==GGV_THROW_REMOVE_ITEM);REQUIRE(h.events[3].operation==GGV_CURRENT_PLAYER_THROWING);
#if VERSION >= VERSION_NTSC_1_0
            REQUIRE(h.events[4].operation==GGV_THROW_SWITCH_PREVIOUS);
#else
            REQUIRE(h.events[4].operation==GGV_THROW_AUTO_SWITCH);
#endif
            REQUIRE(h.events[5].operation==GGV_PRIME_TIMER);REQUIRE(h.throwing[0]==0);REQUIRE(h.throwing[1]==1);
            REQUIRE(h.prime==0);REQUIRE(h.minor==0);REQUIRE(h.loaded[0]==5);REQUIRE(h.count(2,GGV_FIRE_ANIMATION_START)==0);
        }else {REQUIRE(h.prime==3);REQUIRE(h.minor==0);REQUIRE(h.throwing[0]==0);REQUIRE(h.throwing[1]==0);}
    }
}
TEST_CASE("throw initial animation branches fall through to exactly one firing transition","[gameplay][graph][throw]") {
    for(int cycle:{0,1})for(bool animation:{false,true})for(int ready:{0,1,-1})for(int release:{0,1,-1})for(int hand:{0,1}) {
        ThrowHost h;h.cycles=cycle;h.animationPresent=animation;h.ready=ready;h.released=release;h.handnum=hand;h.latch=1;
        const bool fires=!animation||ready!=0;
        REQUIRE(h.run()==0);REQUIRE(h.minor==(fires?2:0));REQUIRE(h.firing==int(fires));REQUIRE(h.loaded[0]==(fires?4:5));
        REQUIRE(h.count(2,GGV_FIRE_ANIMATION_START)==int(animation&&cycle==0));
        REQUIRE(h.count(2,GGV_ATTACK_READY)==int(animation));REQUIRE(h.count(1,GGV_FIRING)==int(fires));
        REQUIRE(h.prime==(fires?99:3));
        if(animation&&(fires||release))REQUIRE(h.latch==0);
        if(fires) {
            REQUIRE(h.index(1,GGV_FIRING)<h.index(1,GGV_ATTACK_TYPE));
            REQUIRE(h.index(1,GGV_ATTACK_TYPE)<h.index(1,GGV_LOADED_AMMO));
            REQUIRE(h.events.back().kind==1);REQUIRE(h.events.back().operation==GGV_STATE_MINOR);
            REQUIRE(h.events.back().minor==2); // Final store, after any initial phase1 transition.
            REQUIRE(h.index(1,GGV_LOADED_AMMO)<int(h.events.size()-1));
        }
    }
    ThrowHost empty;empty.minor=1;empty.loaded[0]=0;REQUIRE(empty.run()==0);REQUIRE(empty.loaded[0]==-1);
    ThrowHost wrap;wrap.minor=1;wrap.loaded[0]=std::numeric_limits<int32_t>::min();
    REQUIRE(wrap.run()==0);REQUIRE(wrap.loaded[0]==std::numeric_limits<int32_t>::max());
}
TEST_CASE("throw recovery respects strict time boundary and exact native secondary true comparison","[gameplay][graph][throw]") {
    for(int weapon:{2,34})for(int secondary:{-1,0,1,2})for(int release:{0,1,-1})for(int trigger:{0,1,-1})for(int delta:{-1,0,1}) {
        ThrowHost h;h.minor=2;h.weapon=weapon;h.secondary=secondary;h.released=release;h.trigger=trigger;
        h.frames=ThrowHost::ticks(60)+delta;
        const bool timed=delta>0;const bool remote=weapon==34&&secondary==1&&release&&trigger;
        REQUIRE(h.run()==int(timed||remote));REQUIRE(h.count(2,GGV_THROW_SECONDARY_VALUE)==int(!timed&&weapon==34));
        REQUIRE(h.count(1,GGV_PRIME_TIMER)==0);REQUIRE(h.firing==0);REQUIRE(h.loaded[0]==5);
    }
}
TEST_CASE("throw grenade wait resets first and expiry rereads narrowed signed16 prime state","[gameplay][graph][throw]") {
#if PAL
    const int wait=250;
#else
    const int wait=300;
#endif
    for(int delta:{-1,0,1}) {
        ThrowHost h;h.minor=55;h.frames=wait+delta;REQUIRE(h.run()==int(delta>0));
        REQUIRE(h.count(2,GGV_THROW_RESET_ANIMATION)==1);REQUIRE(h.count(1,GGV_PRIME_TIMER)==0);REQUIRE(h.loaded[0]==5);
    }
    ThrowHost reset;reset.minor=55;reset.frames=0;reset.latch=1;
    REQUIRE(reset.run()==0);REQUIRE(reset.count(2,GGV_THROW_RESET_ANIMATION)==1);REQUIRE(reset.latch==0);
    REQUIRE(reset.frames==0); // Actual reset never invents a frame-counter write.
    struct PrimeCase {int32_t frame,prime;bool expires;};
    for(const auto c:{PrimeCase{32767,32767,true},{32768,-32768,false},{65535,-1,false},{65536,0,false},{-32769,32767,true}}) {
        ThrowHost h;h.minor=7;h.weapon=30;h.weaponFunction=0;h.legacyActivation=0;h.frames=c.frame;
        REQUIRE(h.run()==0);REQUIRE(h.prime==c.prime);REQUIRE(h.minor==(c.expires?55:7));REQUIRE(h.loaded[0]==(c.expires?4:5));
        REQUIRE(h.count(1,GGV_FIRING)==int(c.expires));
        if(c.expires)REQUIRE(h.index(1,GGV_PRIME_TIMER)<h.index(1,GGV_FIRING));
    }
    for(int weapon:{2,30})for(int function:{0,1})for(bool graph:{false,true})for(bool activation:{false,true}) {
        ThrowHost h;h.minor=7;h.weapon=weapon;h.weaponFunction=function;h.graphPresent=graph;h.hasActivation=activation;
        h.frames=ThrowHost::ticks(60)+1;const bool expires=weapon==30&&function==0&&!(graph&&activation);
        REQUIRE(h.run()==0);REQUIRE(h.minor==(expires?55:7));REQUIRE(h.count(1,GGV_FIRING)==int(expires));
    }
}
TEST_CASE("throw service and input type refusal precedes every resolver and state mutation","[gameplay][graph][throw]") {
    ThrowHost h;ggv_host missing{&h,ThrowHost::read,ThrowHost::write};int32_t result=123;
    REQUIRE_FALSE(ggvExecute(ggvStockProgram(GGV_THROW_CONTROL),&missing,&result));
    REQUIRE(result==123);REQUIRE(h.events.empty());REQUIRE(h.reads.empty());REQUIRE(h.prime==99);
    auto source=ggvStockProgram(GGV_THROW_CONTROL)->source;ggv_program destination{};char error[128]{};
    unsigned call=0;while(call<source.count&&!(source.nodes[call].op==GGV_CALL&&source.nodes[call].service==GGV_THROW_REMOVE_ITEM))++call;
    REQUIRE(call<source.count);source.nodes[call].a=21; // REF cannot be inventory weapon I32.
    REQUIRE_FALSE(ggvPrepare(&source,&destination,error,sizeof(error)));
    source=ggvStockProgram(GGV_THROW_CONTROL)->source;source.nodes[call].service=static_cast<ggv_service>(999);
    REQUIRE_FALSE(ggvPrepare(&source,&destination,error,sizeof(error)));REQUIRE(h.events.empty());REQUIRE(result==123);
}

namespace {
/* Authored source-contract fixtures. Animation/cheat/clock leaves are inert
 * responses, not complete engine or independent original melee-body proof. */
struct MinClipHost {
    int32_t weapon{},function{};std::vector<ggv_binding> reads;
    static ggv_value read(void *p,ggv_binding b) {
        auto &h=*static_cast<MinClipHost *>(p);h.reads.push_back(b);ggv_value v{};
        if(b==GGV_WEAPON_NUMBER)v.i=h.weapon;
        else if(b==GGV_WEAPON_FUNCTION)v.i=h.function;
        else FAIL("Min-clip read outside pure scalar domain");
        return v;
    }
    static void write(void *,ggv_binding,ggv_value){FAIL("Pure min-clip utility emitted a store");}
    int32_t run(){ggv_host h{this,read,write,nullptr};int32_t r=-99;REQUIRE(ggvExecute(ggvStockProgram(GGV_MIN_CLIP_QTY),&h,&r));return r;}
};
struct MeleeHost {
    bool functionPresent{true},graphPresent{},animationPresent{},busy{true},cheat{};
    int32_t graphAmmo{-1},legacyAmmo{0},ammoIndex{-99},weapon{2},handnum{},minor{},cycles{},frames{};
    int32_t trigger{1},released{},latch{},firing{},attackType{99},burst{},loaded[2]{8,11};
    float motor{},clock{1.0f};int animationToken{1};
    bool ready2{},ready3{};std::vector<bool> readyScript;size_t readyIndex{};
    struct Event {int kind,operation,a,b;int32_t minor,loaded0,loaded1;uint32_t motorBits;};
    std::vector<Event> events;std::vector<ggv_binding> reads;
    void event(int kind,int op,int a=0,int b=0){events.push_back({kind,op,a,b,minor,loaded[0],loaded[1],std::bit_cast<uint32_t>(motor)});}
    static ggv_value read(void *opaque,ggv_binding binding) {
        auto &h=*static_cast<MeleeHost *>(opaque);h.reads.push_back(binding);ggv_value v{};
        switch(binding) {
        case GGV_GRAPH_AMMO_SLOT:REQUIRE(h.graphPresent);v.i=h.graphAmmo;break;
        case GGV_LEGACY_AMMO_INDEX:v.i=std::bit_cast<int8_t>(uint8_t(h.legacyAmmo));break;
        case GGV_AMMO_INDEX:v.i=h.ammoIndex;break;
        case GGV_WEAPON_NUMBER:v.i=h.weapon;break;
        case GGV_STATE_MINOR:v.i=h.minor;break;
        case GGV_STATE_CYCLES:v.i=h.cycles;break;
        case GGV_STATE_FRAMES:v.i=h.frames;break;
        case GGV_MATMOT2:v.f=h.motor;break;
        case GGV_BURST_BULLETS:v.i=h.burst;break;
        case GGV_TRIGGER:v.b=h.trigger;break;
        case GGV_TRIGGER_RELEASED:v.b=h.released;break;
        case GGV_FIRE_ANIMATION:v.ref=h.animationPresent?&h.animationToken:nullptr;break;
        case GGV_PASSED_HAND:v.ref=&h;break;
        case GGV_HAND_NUMBER:v.i=h.handnum;break;
        case GGV_LOADED_AMMO:REQUIRE(h.ammoIndex>=0);REQUIRE(h.ammoIndex<2);v.i=h.loaded[h.ammoIndex];break;
        default:FAIL("Melee read outside qualified native domain");break;
        }return v;
    }
    static void write(void *opaque,ggv_binding binding,ggv_value v) {
        auto &h=*static_cast<MeleeHost *>(opaque);
        switch(binding) {
        case GGV_AMMO_INDEX:h.ammoIndex=v.i;return; // Pure native local, no hand store.
        case GGV_MATMOT2:h.motor=v.f;break;
        case GGV_BURST_BULLETS:h.burst=v.i;break;
        case GGV_FIRING:h.firing=std::bit_cast<int8_t>(uint8_t(v.b));break;
        case GGV_ATTACK_TYPE:h.attackType=v.i;break;
        case GGV_STATE_MINOR:h.minor=v.i;break;
        case GGV_ANIMATION_LATCH:h.latch=uint32_t(v.b)&1u;break;
        case GGV_LOADED_AMMO:REQUIRE(h.ammoIndex>=0);REQUIRE(h.ammoIndex<2);h.loaded[h.ammoIndex]=v.i;break;
        default:FAIL("Melee write outside qualified native domain");break;
        }h.event(1,binding);
    }
    static ggv_value call(void *opaque,ggv_service service,ggv_value a,ggv_value b,ggv_value c) {
        auto &h=*static_cast<MeleeHost *>(opaque);ggv_value v{};
        switch(service) {
        case GGV_RESOLVE_FUNCTION:v.b=h.functionPresent;break;
        case GGV_RESOLVE_GRAPH:v.b=h.graphPresent;break;
        case GGV_MELEE_MIN_CLIP:{
            MinClipHost nested;nested.weapon=a.i;nested.function=b.i;v.i=nested.run();
            REQUIRE(nested.reads==std::vector<ggv_binding>{GGV_WEAPON_NUMBER,GGV_WEAPON_FUNCTION});break;
        }
        case GGV_MELEE_CLOCK:v.f=h.clock;break;
        case GGV_MELEE_CHEAT_ACTIVE:REQUIRE(a.i==0);v.b=h.cheat;break;
        case GGV_MELEE_ANIM_BUSY:REQUIRE(a.ref==&h);v.b=h.busy;break;
        case GGV_FIRE_ANIMATION_START:REQUIRE(a.ref==&h.animationToken);REQUIRE(b.i==h.handnum);REQUIRE(c.ref==&h);break;
        case GGV_ATTACK_READY:
            REQUIRE(a.ref==&h);REQUIRE((b.i==2||b.i==3));
            v.b=h.readyIndex<h.readyScript.size()?h.readyScript[h.readyIndex]:(b.i==2?h.ready2:h.ready3);++h.readyIndex;break;
        case GGV_RECOIL_TICKS:v.i=ThrowHost::ticks(a.i);return v; // Actual macro boundary, no effect.
        default:FAIL("Melee service outside qualified native domain");break;
        }
        // Record scalar arguments only for signatures that actually carry them.
        if(service==GGV_MELEE_MIN_CLIP)h.event(2,service,a.i,b.i);
        else if(service==GGV_ATTACK_READY)h.event(2,service,b.i,v.b);
        else if(service==GGV_FIRE_ANIMATION_START)h.event(2,service,b.i);
        else if(service==GGV_MELEE_CHEAT_ACTIVE)h.event(2,service,a.i,v.b);
        else h.event(2,service);
        return v;
    }
    int32_t run(){ggv_host h{this,read,write,call};int32_t r=-99;REQUIRE(ggvExecute(ggvStockProgram(GGV_MELEE_CONTROL),&h,&r));return r;}
    size_t count(int kind,int op)const{return std::count_if(events.begin(),events.end(),[&](const Event &e){return e.kind==kind&&e.operation==op;});}
    std::vector<int> readyArguments()const{std::vector<int> out;for(const auto &e:events)if(e.kind==2&&e.operation==GGV_ATTACK_READY)out.push_back(e.a);return out;}
};
}
TEST_CASE("Pure min-clip composition preserves both scalar branches and lazy function read","[ggv][melee]") {
    for(int32_t weapon:std::vector<int32_t>{-1,1,20,27,28,29,std::numeric_limits<int32_t>::max()})
        for(int32_t function:std::vector<int32_t>{-1,0,1,2,std::numeric_limits<int32_t>::min()}) {
            MinClipHost h;h.weapon=weapon;h.function=function;
            REQUIRE(h.run()==(weapon==28&&function==1?4:1));
            REQUIRE(h.reads.size()==(weapon==28?2:1));REQUIRE(h.reads[0]==GGV_WEAPON_NUMBER);
        }
}
TEST_CASE("Melee resolves both witnesses before null refusal and selects actual ammo precedence","[ggv][melee]") {
    MeleeHost absent;absent.functionPresent=false;absent.weapon=20;
    REQUIRE(absent.run()==1);REQUIRE(absent.reads.empty());REQUIRE(absent.events.size()==2);
    REQUIRE(absent.events[0].operation==GGV_RESOLVE_FUNCTION);REQUIRE(absent.events[1].operation==GGV_RESOLVE_GRAPH);
    for(bool present:{false,true})for(int slot:{-1,0,1}) {
        MeleeHost h;h.graphPresent=present;h.graphAmmo=slot;h.legacyAmmo=257;h.minor=99;
        REQUIRE(h.run()==0);REQUIRE(h.ammoIndex==(present&&slot>=0?slot:1));
        REQUIRE(h.count(1,GGV_STATE_MINOR)==0);REQUIRE(h.count(2,GGV_MELEE_MIN_CLIP)==0);
        const auto fallbacks=std::count(h.reads.begin(),h.reads.end(),GGV_LEGACY_AMMO_INDEX);
        REQUIRE(fallbacks==(present&&slot>=0?0:1));
    }
}
TEST_CASE("Reaper composition retains ordered motor burst trigger and strict cap stores","[ggv][melee]") {
    MeleeHost first;first.weapon=20;first.motor=99;first.burst=90;
    REQUIRE(first.run()==0);REQUIRE(first.motor==0.11f);REQUIRE(first.burst==1);REQUIRE(first.firing==1);REQUIRE(first.attackType==4);
    const std::vector<int> stores{GGV_MATMOT2,GGV_BURST_BULLETS,GGV_FIRING,GGV_ATTACK_TYPE,GGV_BURST_BULLETS,GGV_MATMOT2};
    std::vector<int> actual;for(const auto &e:first.events)if(e.kind==1)actual.push_back(e.operation);REQUIRE(actual==stores);
    MeleeHost capped;capped.weapon=20;capped.cycles=1;capped.motor=0.99f;capped.clock=4;
    REQUIRE(capped.run()==0);REQUIRE(capped.motor==1.0f);REQUIRE(capped.count(1,GGV_MATMOT2)==2);
    MeleeHost exact;exact.weapon=20;exact.cycles=1;exact.motor=1.0f;exact.clock=0;
    REQUIRE(exact.run()==0);REQUIRE(exact.count(1,GGV_MATMOT2)==1);
    MeleeHost released;released.weapon=20;released.trigger=0;released.burst=std::numeric_limits<int32_t>::max();released.cycles=1;
    REQUIRE(released.run()==1);REQUIRE(released.motor==0);REQUIRE(released.burst==std::numeric_limits<int32_t>::min());
    REQUIRE(released.count(2,GGV_MELEE_CLOCK)==0);REQUIRE(released.count(2,GGV_MELEE_CHEAT_ACTIVE)==0);
}
TEST_CASE("Melee initial animation falls through only when ready and respects repeated queries","[ggv][melee]") {
    MeleeHost noAnimation;REQUIRE(noAnimation.run()==0);REQUIRE(noAnimation.minor==1);
    REQUIRE(noAnimation.count(1,GGV_FIRING)==2);REQUIRE(noAnimation.attackType==4);
    MeleeHost pending;pending.animationPresent=true;pending.released=1;
    REQUIRE(pending.run()==0);REQUIRE(pending.minor==0);REQUIRE(pending.latch==0);REQUIRE(pending.attackType==10);
    REQUIRE(pending.readyArguments()==std::vector<int>{2});REQUIRE(pending.count(2,GGV_MELEE_CHEAT_ACTIVE)==0);
    MeleeHost ready;ready.animationPresent=true;ready.readyScript={true,false};
    REQUIRE(ready.run()==0);REQUIRE(ready.minor==3);REQUIRE(ready.latch==0);
    REQUIRE(ready.readyArguments()==std::vector<int>{2,3});
    REQUIRE(std::count(ready.reads.begin(),ready.reads.end(),GGV_FIRE_ANIMATION)==5);
    MeleeHost continuation;continuation.cycles=1;continuation.animationPresent=true;
    REQUIRE(continuation.run()==0);REQUIRE(continuation.count(2,GGV_FIRE_ANIMATION_START)==0);
}
TEST_CASE("Melee phase-three recovery may immediately repeat readiness then cheat wins","[ggv][melee]") {
    MeleeHost pending;pending.minor=3;pending.animationPresent=true;
    REQUIRE(pending.run()==0);REQUIRE(pending.readyArguments()==std::vector<int>{3});REQUIRE(pending.count(1,GGV_FIRING)==0);
    MeleeHost repeat;repeat.minor=3;repeat.animationPresent=true;repeat.readyScript={true,false};repeat.cheat=true;
    REQUIRE(repeat.run()==0);REQUIRE(repeat.minor==2);REQUIRE(repeat.readyArguments()==std::vector<int>{3,3});
    REQUIRE(repeat.count(1,GGV_STATE_MINOR)==3);REQUIRE(repeat.count(2,GGV_MELEE_CHEAT_ACTIVE)==1);
}
TEST_CASE("Tranquilizer melee preserves strict threshold and two composed utility invocations","[ggv][melee]") {
    struct Row{int32_t before,after,calls;};
    for(Row row:std::vector<Row>{{-2,0,1},{0,0,1},{3,0,1},{4,0,1},{5,1,2},{8,4,2},{2147483647,2147483643,2}}) {
        MeleeHost h;h.minor=1;h.weapon=28;h.loaded[0]=row.before;
        REQUIRE(h.run()==0);REQUIRE(h.loaded[0]==row.after);REQUIRE(h.loaded[1]==11);
        REQUIRE(h.count(2,GGV_MELEE_MIN_CLIP)==size_t(row.calls));REQUIRE(h.count(1,GGV_LOADED_AMMO)==1);
        REQUIRE(h.events[h.events.size()-2].operation==GGV_LOADED_AMMO);
        REQUIRE(h.events.back().operation==GGV_MELEE_CHEAT_ACTIVE);
    }
    MeleeHost noAmmo;noAmmo.minor=1;noAmmo.weapon=28;noAmmo.legacyAmmo=255;
    REQUIRE(noAmmo.run()==0);REQUIRE(noAmmo.ammoIndex==-1);REQUIRE(noAmmo.count(2,GGV_MELEE_MIN_CLIP)==0);REQUIRE(noAmmo.loaded[0]==8);
    MeleeHost other;other.minor=1;REQUIRE(other.run()==0);REQUIRE(other.count(2,GGV_MELEE_MIN_CLIP)==0);
}
TEST_CASE("Melee final animation idle cheat and tick boundaries retain lazy order","[ggv][melee]") {
    MeleeHost idle;idle.minor=2;idle.busy=false;idle.cheat=true;REQUIRE(idle.run()==1);REQUIRE(idle.count(2,GGV_MELEE_CHEAT_ACTIVE)==0);
    MeleeHost unarmed;unarmed.minor=2;unarmed.weapon=1;unarmed.cheat=true;REQUIRE(unarmed.run()==1);
    REQUIRE(std::count(unarmed.reads.begin(),unarmed.reads.end(),GGV_STATE_FRAMES)==0);
    const int boundary=ThrowHost::ticks(60);
    for(int frames:{boundary-1,boundary,boundary+1}) {
        MeleeHost h;h.minor=2;h.frames=frames;REQUIRE(h.run()==(frames>boundary?1:0));
        REQUIRE(h.count(2,GGV_MELEE_ANIM_BUSY)==1);REQUIRE(h.count(2,GGV_MELEE_CHEAT_ACTIVE)==1);
        REQUIRE(std::count(h.reads.begin(),h.reads.end(),GGV_WEAPON_NUMBER)==1); // Reaper selection only; false cheat skips extra read.
    }
    MeleeHost armed;armed.minor=2;armed.cheat=true;armed.frames=boundary;REQUIRE(armed.run()==0);
    REQUIRE(std::count(armed.reads.begin(),armed.reads.end(),GGV_WEAPON_NUMBER)==2);
}
TEST_CASE("Melee new primitive and callbacks refuse before effects and preserve output canaries","[ggv][melee]") {
    MeleeHost h;ggv_host missing{&h,MeleeHost::read,MeleeHost::write,nullptr};int32_t out=0x13579;
    REQUIRE_FALSE(ggvExecute(ggvStockProgram(GGV_MELEE_CONTROL),&missing,&out));REQUIRE(out==0x13579);REQUIRE(h.events.empty());REQUIRE(h.reads.empty());
    ggv_source invalid{};invalid.count=2;
    invalid.nodes[0].op=GGV_F_ADD_PRODUCT;invalid.nodes[0].dst=0;invalid.nodes[0].a=1;invalid.nodes[0].b=2;invalid.nodes[0].c=3;
    invalid.nodes[1].op=GGV_RETURN;ggv_program p{};char error[128]{};
    REQUIRE_FALSE(ggvPrepare(&invalid,&p,error,sizeof(error)));REQUIRE(p.validated==0);
    REQUIRE(h.events.empty());REQUIRE(out==0x13579);
}
TEST_CASE("Melee motor scalar retains additive product grouping and zero representation","[ggv][melee]") {
    struct Row{float old,factor,clock;uint32_t expected;};
    for(Row row:std::vector<Row>{{-0.25f,0.5f,2.0f,0x3f400000u},{-0.0f,0.0f,1.0f,0x00000000u},{0.0f,-0.0f,1.0f,0x00000000u}}) {
        ggv_source s{};s.count=6;
        const float values[]{row.old,row.factor,row.clock};
        for(unsigned i=0;i<3;++i){s.nodes[i].op=GGV_CONST;s.nodes[i].dst=i;s.nodes[i].type=GGV_F32;s.nodes[i].literal.f=values[i];}
        s.nodes[3].op=GGV_F_ADD_PRODUCT;s.nodes[3].dst=0;s.nodes[3].a=0;s.nodes[3].b=1;s.nodes[3].c=2;
        s.nodes[4].op=GGV_WRITE;s.nodes[4].a=0;s.nodes[4].binding=GGV_MATMOT2;s.nodes[5].op=GGV_RETURN;
        ggv_program p{};char error[128]{};REQUIRE(ggvPrepare(&s,&p,error,sizeof(error)));
        MeleeHost h;ggv_host host{&h,MeleeHost::read,MeleeHost::write,nullptr};int32_t out=99;
        REQUIRE(ggvExecute(&p,&host,&out));REQUIRE(std::bit_cast<uint32_t>(h.motor)==row.expected);REQUIRE(h.count(1,GGV_MATMOT2)==1);
    }
}

namespace {
/* Source-contract hosts, not complete independent original-body or native
 * animation/audio/state qualification. Leaf behavior is explicitly modeled. */
struct SpecialHost {
    bool functionPresent{true},graphPresent{},hasSpecial{},hasRecovery{};
    int32_t graphSpecial{99},legacySpecial{42},graphRecovery{120},legacyRecovery{60};
    int32_t graphAmmo{-1},legacyAmmo{},ammoIndex{-99},minor{},frames{},firing{},attackType{-1},loaded[2]{3,7};
    std::vector<ggv_binding> reads,writes;std::vector<ggv_service> calls;
    static ggv_value read(void *p,ggv_binding b){auto &h=*static_cast<SpecialHost *>(p);h.reads.push_back(b);ggv_value v{};
        switch(b){
        case GGV_SPECIAL_GRAPH_HAS_VALUE:REQUIRE(h.graphPresent);v.b=h.hasSpecial;break;
        case GGV_SPECIAL_GRAPH_VALUE:REQUIRE(h.graphPresent);REQUIRE(h.hasSpecial);v.i=h.graphSpecial;break;
        case GGV_SPECIAL_LEGACY_VALUE:v.i=h.legacySpecial;break;
        case GGV_SPECIAL_GRAPH_HAS_RECOVERY:REQUIRE(h.graphPresent);v.b=h.hasRecovery;break;
        case GGV_SPECIAL_GRAPH_RECOVERY:REQUIRE(h.graphPresent);REQUIRE(h.hasRecovery);v.i=h.graphRecovery;break;
        case GGV_SPECIAL_LEGACY_RECOVERY:v.i=h.legacyRecovery;break;
        case GGV_GRAPH_AMMO_SLOT:REQUIRE(h.graphPresent);v.i=h.graphAmmo;break;
        case GGV_LEGACY_AMMO_INDEX:v.i=std::bit_cast<int8_t>(uint8_t(h.legacyAmmo));break;
        case GGV_STATE_MINOR:v.i=h.minor;break;
        case GGV_STATE_FRAMES:v.i=h.frames;break;
        case GGV_LOADED_AMMO:REQUIRE(h.ammoIndex>=0);REQUIRE(h.ammoIndex<2);v.i=h.loaded[h.ammoIndex];break;
        default:FAIL("Special read outside qualified native domain");break;
        }return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v){auto &h=*static_cast<SpecialHost *>(p);
        switch(b){
        case GGV_AMMO_INDEX:h.ammoIndex=v.i;return;
        case GGV_STATE_MINOR:h.minor=v.i;break;
        case GGV_FIRING:h.firing=std::bit_cast<int8_t>(uint8_t(v.b));break;
        case GGV_ATTACK_TYPE:h.attackType=v.i;break;
        case GGV_LOADED_AMMO:REQUIRE(h.ammoIndex>=0);REQUIRE(h.ammoIndex<2);h.loaded[h.ammoIndex]=v.i;break;
        default:FAIL("Special write outside qualified native domain");break;
        }h.writes.push_back(b);
    }
    static ggv_value call(void *p,ggv_service s,ggv_value a,ggv_value,ggv_value){auto &h=*static_cast<SpecialHost *>(p);h.calls.push_back(s);ggv_value v{};
        if(s==GGV_RESOLVE_FUNCTION)v.b=h.functionPresent;
        else if(s==GGV_RESOLVE_GRAPH)v.b=h.graphPresent;
        else if(s==GGV_RECOIL_TICKS)v.i=ThrowHost::ticks(a.i);
        else FAIL("Special service outside qualified native domain");return v;
    }
    int32_t run(){ggv_host host{this,read,write,call};int32_t result=-99;REQUIRE(ggvExecute(ggvStockProgram(GGV_SPECIAL_CONTROL),&host,&result));return result;}
};
struct EmptyHost {
    int32_t weapon{2},handnum{},frames{},animmode{},count60{7},count{9},lvupdate{17};
    uint32_t flags{},mode{99};int32_t trigger{1};bool definitionPresent{},functionPresent{},animationPresent{};
    bool ready{},stateAccepted{true}; // false is a structural leaf-fault response, not current native IDLE rejection.
    bool priorityEnabled{VERSION>=VERSION_NTSC_1_0};int32_t priority{10},audioPriority{20};
    int32_t water{101},click{102};bool soundHandles[2]{true,true};unsigned starts{};
    struct Handle{int token;};Handle handles[2]{{1},{2}};int definitionToken{},animationToken{};
    int32_t animload{8},latch{1},incremental{1},loopcount{3},bit02{1},bit07{1};void *command{&animationToken};
    int32_t selectedCurrentFrames[2]{51,52};int currentPlayerGeneration{7};bool passedIsSelected{true};
    struct Event{int kind,operation,a,b;int32_t frames;uint32_t flags,mode;int32_t priority;};
    std::vector<Event> events;std::vector<ggv_binding> reads;
    void event(int kind,int op,int a=0,int b=0){events.push_back({kind,op,a,b,frames,flags,mode,priority});}
    void resetPassedAnimation(){ // Eight actual reset stores are modeled; engine animation lifetime is unqualified.
        animload=-1;animmode=0;latch=0;incremental=0;command=nullptr;loopcount=0;bit02=0;bit07=0;
    }
    static ggv_value read(void *p,ggv_binding b){auto &h=*static_cast<EmptyHost *>(p);h.reads.push_back(b);ggv_value v{};
        switch(b){
        case GGV_WEAPON_NUMBER:v.i=h.weapon;break;
        case GGV_STATE_FRAMES:v.i=h.frames;break;
        case GGV_STATE_FLAGS:v.u=h.flags;break;
        case GGV_EMPTY_ANIM_MODE:v.i=h.animmode;break;
        case GGV_EMPTY_DEFINITION:v.ref=h.definitionPresent?&h.definitionToken:nullptr;break;
        case GGV_FIRE_ANIMATION:REQUIRE(h.functionPresent);v.ref=h.animationPresent?&h.animationToken:nullptr;break;
        case GGV_PASSED_HAND:v.ref=&h;break;
        case GGV_HAND_NUMBER:v.i=h.handnum;break;
        case GGV_EMPTY_LVUPDATE:v.i=h.lvupdate;break;
        case GGV_TRIGGER:v.b=h.trigger;break;
        case GGV_PRIORITY_ENABLED:v.b=h.priorityEnabled;break;
        case GGV_EMPTY_WATER_SOUND:v.i=h.water;break;
        case GGV_EMPTY_CLICK_SOUND:v.i=h.click;break;
        default:FAIL("Empty read outside qualified native domain");break;
        }return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v){auto &h=*static_cast<EmptyHost *>(p);
        switch(b){
        case GGV_STATE_FRAMES:h.frames=v.i;break;
        case GGV_STATE_FLAGS:h.flags=v.u;break;
        case GGV_EMPTY_MODE:h.mode=v.u;break;
        case GGV_EMPTY_COUNT60:h.count60=v.i;break;
        case GGV_EMPTY_COUNT:h.count=v.i;break;
        default:FAIL("Empty write outside qualified native domain");break;
        }h.event(1,b);
    }
    static ggv_value call(void *p,ggv_service s,ggv_value a,ggv_value b,ggv_value c){auto &h=*static_cast<EmptyHost *>(p);ggv_value v{};
        switch(s){
        case GGV_RECOIL_TICKS:v.i=ThrowHost::ticks(a.i);h.event(2,s,a.i,v.i);return v;
        case GGV_EMPTY_RESET_ANIM:REQUIRE(a.ref==&h);h.resetPassedAnimation();h.event(2,s);break;
        case GGV_RESOLVE_FUNCTION:v.b=h.functionPresent;h.event(2,s,0,v.b);break;
        case GGV_FIRE_ANIMATION_START:REQUIRE(a.ref==&h.animationToken);REQUIRE(b.i==h.handnum);REQUIRE(c.ref==&h);h.event(2,s,b.i);break;
        case GGV_ATTACK_READY:REQUIRE(a.ref==&h);REQUIRE(b.i==5);v.b=h.ready;h.event(2,s,b.i,v.b);break;
        case GGV_CURRENT_PRIORITY:v.i=h.priority;h.event(2,s,0,v.i);break;
        case GGV_AUDIO_PRIORITY:v.i=h.audioPriority;h.event(2,s,0,v.i);break;
        case GGV_SET_PRIORITY:h.priority=a.i;h.event(2,s,a.i);break;
        case GGV_EMPTY_SOUND_START:
            REQUIRE(h.starts<2);v.ref=h.soundHandles[h.starts]?&h.handles[h.starts]:nullptr;++h.starts;h.event(2,s,a.i,int(h.starts));break;
        case GGV_SOUND_EVENT:{
            REQUIRE(a.ref!=nullptr);auto *handle=static_cast<Handle *>(a.ref);h.event(2,s,handle->token,b.i);break;
        }
        case GGV_EMPTY_SET_STATE:
            REQUIRE(a.i==h.handnum);REQUIRE(b.i==0);
            if(h.stateAccepted){h.selectedCurrentFrames[a.i]=0;if(h.passedIsSelected){h.frames=0;h.flags=0;}}
            v.b=h.stateAccepted;h.event(2,s,a.i,b.i);break;
        default:FAIL("Empty service outside qualified native domain");break;
        }return v;
    }
    int32_t run(){ggv_host host{this,read,write,call};int32_t result=-99;REQUIRE(ggvExecute(ggvStockProgram(GGV_EMPTY_ATTACK),&host,&result));return result;}
    size_t calls(ggv_service s)const{return std::count_if(events.begin(),events.end(),[&](const Event&e){return e.kind==2&&e.operation==s;});}
    std::vector<int> soundSequence()const{std::vector<int> out;for(const auto&e:events)if(e.kind==2&&e.operation==GGV_EMPTY_SOUND_START)out.push_back(e.a);return out;}
};
}
TEST_CASE("Special full composition selects lazy graph value ammo and recovery in native order","[ggv][special]") {
    SpecialHost absent;absent.functionPresent=false;REQUIRE(absent.run()==1);REQUIRE(absent.reads.empty());REQUIRE(absent.writes.empty());
    REQUIRE(absent.calls==std::vector<ggv_service>{GGV_RESOLVE_FUNCTION,GGV_RESOLVE_GRAPH});
    for(bool graph:{false,true})for(bool value:{false,true})for(bool recovery:{false,true}) {
        SpecialHost h;h.graphPresent=graph;h.hasSpecial=value;h.hasRecovery=recovery;h.graphAmmo=1;h.legacyAmmo=256;
        REQUIRE(h.run()==0);REQUIRE(h.attackType==(graph&&value?99:42));REQUIRE(h.ammoIndex==(graph?1:0));
        REQUIRE(h.loaded[graph?1:0]==(graph?6:2));REQUIRE(h.loaded[graph?0:1]==(graph?3:7));
        REQUIRE(h.writes==std::vector<ggv_binding>{GGV_STATE_MINOR,GGV_FIRING,GGV_ATTACK_TYPE,GGV_LOADED_AMMO,GGV_STATE_MINOR});
        const auto legacyValue=std::count(h.reads.begin(),h.reads.end(),GGV_SPECIAL_LEGACY_VALUE);
        const auto legacyRecovery=std::count(h.reads.begin(),h.reads.end(),GGV_SPECIAL_LEGACY_RECOVERY);
        REQUIRE(legacyValue==(graph&&value?0:1));REQUIRE(legacyRecovery==(graph&&recovery?0:1));
    }
}
TEST_CASE("Special whole driver preserves negative slots wrapping debit and strict recovery","[ggv][special]") {
    SpecialHost fallback;fallback.graphPresent=true;fallback.graphAmmo=-1;fallback.legacyAmmo=257;
    REQUIRE(fallback.run()==0);REQUIRE(fallback.ammoIndex==1);REQUIRE(fallback.loaded[0]==3);REQUIRE(fallback.loaded[1]==6);
    SpecialHost none;none.minor=1;none.legacyAmmo=255;REQUIRE(none.run()==0);REQUIRE(none.ammoIndex==-1);REQUIRE(none.loaded[0]==3);
    SpecialHost wrapping;wrapping.minor=1;wrapping.loaded[0]=std::numeric_limits<int32_t>::min();
    REQUIRE(wrapping.run()==0);REQUIRE(wrapping.loaded[0]==std::numeric_limits<int32_t>::max());
    for(bool graph:{false,true})for(int offset:{-1,0,1}) {
        SpecialHost h;h.minor=2;h.graphPresent=graph;h.hasRecovery=true;h.frames=ThrowHost::ticks(graph?120:60)+offset;
        REQUIRE(h.run()==(offset>0?1:0));REQUIRE(h.writes.empty());
    }
    SpecialHost unknown;unknown.minor=99;REQUIRE(unknown.run()==0);REQUIRE(unknown.writes.empty());
}
TEST_CASE("Empty exact fifteen visible trigger classes keep repeated ticks and animation laziness","[ggv][empty]") {
    const int period=ThrowHost::ticks(25);
    for(int weapon:std::vector<int>{2,3,4,5,6,7,8,9,10,11,12,13,14,20,28}) {
        EmptyHost h;h.weapon=weapon;h.frames=period+1;h.definitionPresent=true;h.functionPresent=true;h.animationPresent=true;h.animmode=2;
        REQUIRE(h.run()==0);REQUIRE(h.frames==1);REQUIRE(h.mode==13);REQUIRE(h.count60==0);REQUIRE(h.count==0);
        REQUIRE(h.calls(GGV_RECOIL_TICKS)==2);REQUIRE(h.calls(GGV_EMPTY_RESET_ANIM)==1);
        REQUIRE(h.calls(GGV_RESOLVE_FUNCTION)==1);REQUIRE(h.calls(GGV_FIRE_ANIMATION_START)==1);REQUIRE(h.calls(GGV_ATTACK_READY)==0);
        REQUIRE(h.soundSequence().empty());REQUIRE(h.flags==0);
        REQUIRE(h.animload==-1);REQUIRE(h.incremental==0);REQUIRE(h.command==nullptr);REQUIRE(h.loopcount==0);REQUIRE(h.bit02==0);REQUIRE(h.bit07==0);
    }
    EmptyHost noDefinition;noDefinition.frames=period+1;REQUIRE(noDefinition.run()==0);REQUIRE(noDefinition.calls(GGV_RESOLVE_FUNCTION)==0);
    REQUIRE(noDefinition.calls(GGV_RECOIL_TICKS)==3);REQUIRE(noDefinition.soundSequence().empty());
    EmptyHost nullFunction;nullFunction.frames=period;nullFunction.definitionPresent=true;
    REQUIRE(nullFunction.run()==0);REQUIRE(nullFunction.calls(GGV_RESOLVE_FUNCTION)==1);
    REQUIRE(std::count(nullFunction.reads.begin(),nullFunction.reads.end(),GGV_FIRE_ANIMATION)==0);
    EmptyHost held;held.flags=0x10;held.frames=period;held.definitionPresent=true;
    REQUIRE(held.run()==0);REQUIRE(held.calls(GGV_RESOLVE_FUNCTION)==0);
}
TEST_CASE("Empty busy readiness sets latch before clicks and strict default boundary stays visible","[ggv][empty]") {
    EmptyHost busy;busy.animmode=2;busy.ready=true;busy.frames=0;
    REQUIRE(busy.run()==0);REQUIRE(busy.calls(GGV_ATTACK_READY)==1);REQUIRE(busy.calls(GGV_RESOLVE_FUNCTION)==0);
    REQUIRE(busy.flags==0x10);REQUIRE(busy.soundSequence()==std::vector<int>{busy.click});
    size_t flagIndex=0,soundIndex=0;for(size_t i=0;i<busy.events.size();++i){
        if(busy.events[i].kind==1&&busy.events[i].operation==GGV_STATE_FLAGS)flagIndex=i;
        if(busy.events[i].kind==2&&busy.events[i].operation==GGV_EMPTY_SOUND_START)soundIndex=i;
    }REQUIRE(flagIndex<soundIndex);
    const auto previousStarts=busy.starts;REQUIRE(busy.run()==0);REQUIRE(busy.starts==previousStarts);REQUIRE(busy.calls(GGV_ATTACK_READY)==2);
    const int period=ThrowHost::ticks(25);
    for(int frames:{period-1,period,period+1}) {
        EmptyHost h;h.weapon=15;h.frames=frames;REQUIRE(h.run()==0);
        REQUIRE(h.starts==(frames>period?1u:0u));REQUIRE(h.frames==(frames>period?1:frames));
        REQUIRE(h.calls(GGV_RECOIL_TICKS)==(frames>period?2u:1u));REQUIRE(h.calls(GGV_CURRENT_PRIORITY)==0);
    }
}
TEST_CASE("Empty Maian wet click falls through with exact two pitch bits and priority restoration","[ggv][empty]") {
    for(int weapon:{7,12,22})for(bool enabled:{false,true})for(bool waterHandle:{false,true})for(bool clickHandle:{false,true}) {
        EmptyHost h;h.weapon=weapon;h.frames=ThrowHost::ticks(25)*3;h.animmode=2;h.ready=true;
        h.priorityEnabled=enabled;h.soundHandles[0]=waterHandle;h.soundHandles[1]=clickHandle;
        REQUIRE(h.run()==0);REQUIRE(h.soundSequence()==std::vector<int>{h.water,h.click});REQUIRE(h.priority==10);
        REQUIRE(h.calls(GGV_CURRENT_PRIORITY)==(enabled?2u:0u));REQUIRE(h.calls(GGV_AUDIO_PRIORITY)==(enabled?2u:0u));
        REQUIRE(h.calls(GGV_SET_PRIORITY)==(enabled?4u:0u));
        std::vector<uint32_t> pitches;std::vector<int> priorityStores,handleTokens,audioOperations;
        for(const auto&e:h.events){if(e.kind==2&&e.operation==GGV_SOUND_EVENT){pitches.push_back(std::bit_cast<uint32_t>(int32_t(e.b)));handleTokens.push_back(e.a);}
            if(e.kind==2&&e.operation==GGV_SET_PRIORITY)priorityStores.push_back(e.a);
            if(e.kind==2&&(e.operation==GGV_CURRENT_PRIORITY||e.operation==GGV_AUDIO_PRIORITY||e.operation==GGV_SET_PRIORITY||e.operation==GGV_EMPTY_SOUND_START||e.operation==GGV_SOUND_EVENT))audioOperations.push_back(e.operation);}
        std::vector<uint32_t> expected;if(waterHandle)expected.push_back(0x40047ae1u);if(clickHandle)expected.push_back(0x3fc00000u);
        REQUIRE(pitches==expected);REQUIRE(priorityStores==(enabled?std::vector<int>{21,10,21,10}:std::vector<int>{}));
        std::vector<int> expectedTokens;if(waterHandle)expectedTokens.push_back(1);if(clickHandle)expectedTokens.push_back(2);REQUIRE(handleTokens==expectedTokens);
        if(enabled&&waterHandle&&clickHandle)REQUIRE(audioOperations==std::vector<int>{GGV_CURRENT_PRIORITY,GGV_AUDIO_PRIORITY,GGV_SET_PRIORITY,GGV_EMPTY_SOUND_START,GGV_SOUND_EVENT,GGV_SET_PRIORITY,GGV_CURRENT_PRIORITY,GGV_AUDIO_PRIORITY,GGV_SET_PRIORITY,GGV_EMPTY_SOUND_START,GGV_SOUND_EVENT,GGV_SET_PRIORITY});
    }
}
TEST_CASE("Empty faster no-sound and default classes keep separate audio effects","[ggv][empty]") {
    for(int weapon:{28,36}){
        EmptyHost h;h.weapon=weapon;h.frames=ThrowHost::ticks(25)*3;h.priorityEnabled=true;REQUIRE(h.run()==0);
        REQUIRE(h.soundSequence()==std::vector<int>{h.click});REQUIRE(h.calls(GGV_SOUND_EVENT)==1);REQUIRE(h.calls(GGV_CURRENT_PRIORITY)==1);
        for(const auto&e:h.events)if(e.kind==2&&e.operation==GGV_SOUND_EVENT)REQUIRE(std::bit_cast<uint32_t>(int32_t(e.b))==0x3fc00000u);
    }
    for(int weapon:{1,26,30,31,32,33,34,35}){
        EmptyHost h;h.weapon=weapon;h.frames=ThrowHost::ticks(25)+1;REQUIRE(h.run()==0);
        REQUIRE(h.starts==0);REQUIRE(h.flags==0x10);REQUIRE(h.calls(GGV_CURRENT_PRIORITY)==0);
    }
    EmptyHost ordinary;ordinary.weapon=15;ordinary.frames=ThrowHost::ticks(25)+1;ordinary.priorityEnabled=true;
    REQUIRE(ordinary.run()==0);REQUIRE(ordinary.soundSequence()==std::vector<int>{ordinary.click});
    REQUIRE(ordinary.calls(GGV_SOUND_EVENT)==0);REQUIRE(ordinary.calls(GGV_CURRENT_PRIORITY)==0);REQUIRE(ordinary.calls(GGV_SET_PRIORITY)==0);
}
TEST_CASE("Empty release resets mode counts before current-hand state transition and returns raw lvupdate","[ggv][empty]") {
    for(int32_t update:std::vector<int32_t>{-123,0,17,std::numeric_limits<int32_t>::max()}){
        EmptyHost h;h.trigger=0;h.handnum=1;h.passedIsSelected=false;h.lvupdate=update;h.frames=7;
        REQUIRE(h.run()==update);REQUIRE(h.mode==0);REQUIRE(h.count60==0);REQUIRE(h.count==0);
        REQUIRE(h.selectedCurrentFrames[1]==0);REQUIRE(h.selectedCurrentFrames[0]==51);REQUIRE(h.frames==7);
        REQUIRE(h.calls(GGV_EMPTY_RESET_ANIM)==0);REQUIRE(h.calls(GGV_EMPTY_SET_STATE)==1);REQUIRE(h.currentPlayerGeneration==7);
        const size_t n=h.events.size();REQUIRE(n>=4);REQUIRE(h.events[n-4].operation==GGV_EMPTY_MODE);
        REQUIRE(h.events[n-3].operation==GGV_EMPTY_COUNT60);REQUIRE(h.events[n-2].operation==GGV_EMPTY_COUNT);REQUIRE(h.events[n-1].operation==GGV_EMPTY_SET_STATE);
    }
    EmptyHost alias;alias.trigger=0;alias.frames=7;REQUIRE(alias.run()==17);REQUIRE(alias.frames==0);REQUIRE(alias.selectedCurrentFrames[0]==0);
    EmptyHost refusal;refusal.trigger=0;refusal.stateAccepted=false; // Synthetic non-total engine leaf fault; current native IDLE normally succeeds.
    REQUIRE(refusal.run()==0);REQUIRE(refusal.calls(GGV_EMPTY_RESET_ANIM)==1);
    REQUIRE(std::count(refusal.reads.begin(),refusal.reads.end(),GGV_EMPTY_LVUPDATE)==0);REQUIRE(refusal.events.back().operation==GGV_EMPTY_RESET_ANIM);
}
TEST_CASE("Special and empty missing callbacks refuse before effects with output unchanged","[ggv][special][empty]") {
    SpecialHost special;ggv_host sh{&special,SpecialHost::read,SpecialHost::write,nullptr};int32_t out=0x123456;
    REQUIRE_FALSE(ggvExecute(ggvStockProgram(GGV_SPECIAL_CONTROL),&sh,&out));REQUIRE(out==0x123456);REQUIRE(special.calls.empty());REQUIRE(special.reads.empty());REQUIRE(special.writes.empty());
    EmptyHost empty;ggv_host eh{&empty,EmptyHost::read,EmptyHost::write,nullptr};
    REQUIRE_FALSE(ggvExecute(ggvStockProgram(GGV_EMPTY_ATTACK),&eh,&out));REQUIRE(out==0x123456);REQUIRE(empty.events.empty());REQUIRE(empty.reads.empty());
    ggv_source invalid{};invalid.count=1;invalid.nodes[0].op=GGV_RETURN_I32;invalid.nodes[0].a=31;
    ggv_program p{};char error[128]{};REQUIRE_FALSE(ggvPrepare(&invalid,&p,error,sizeof(error)));REQUIRE(p.validated==0);
}

namespace {
/* Source-contract fixtures. Native lookup/logging are bounded inert leaves;
 * composed callees below actually execute their prepared stock programs when
 * tests are eventually admitted. No complete native differential is claimed. */
struct StateHost {
    struct Hand {int32_t state{9},frames{13},cycles{4},minor{3},last{8};uint32_t flags{0xabcdef};uint8_t weapon{2},function{0};int8_t inuse{1};};
    Hand hands[2]{};Hand *selected{};int32_t handnum{},request{},playernum{};
    bool alternatePresent{true},mutateOnAlternate{},switchPlayerOnAlternate{};int alternateToken{};
    int32_t logWeapon{},logInuse{};
    struct Event{int kind,operation,a,b,c;int32_t state,frames,minor;uint32_t flags;};
    std::vector<Event> events;std::vector<ggv_binding> reads;
    void event(int kind,int op,int a=0,int b=0,int c=0){REQUIRE(selected!=nullptr);events.push_back({kind,op,a,b,c,selected->state,selected->frames,selected->minor,selected->flags});}
    static ggv_value read(void *p,ggv_binding b){auto &h=*static_cast<StateHost *>(p);REQUIRE(h.selected!=nullptr);h.reads.push_back(b);ggv_value v{};
        switch(b){
        case GGV_STATE_REQUEST:v.i=h.request;break;
        case GGV_STATE_VALUE:v.i=h.selected->state;break;
        case GGV_WEAPON_FUNCTION:v.i=h.selected->function;break;
        case GGV_STATE_PLAYER_NUMBER:v.i=h.playernum;break;
        case GGV_HAND_NUMBER:v.i=h.handnum;break;
        case GGV_WEAPON_NUMBER:v.i=h.selected->weapon;break;
        case GGV_STATE_INUSE:v.i=h.selected->inuse;break;
        default:FAIL("State read outside native qualified context");break;
        }return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v){auto &h=*static_cast<StateHost *>(p);REQUIRE(h.selected!=nullptr);
        switch(b){
        case GGV_STATE_VALUE:h.selected->state=v.i;break;
        case GGV_STATE_FRAMES:h.selected->frames=v.i;break;
        case GGV_STATE_FLAGS:h.selected->flags=v.u;break;
        case GGV_STATE_CYCLES:h.selected->cycles=v.i;break;
        case GGV_STATE_MINOR:h.selected->minor=v.i;break;
        case GGV_STATE_LAST_FRAME:h.selected->last=v.i;break;
        case GGV_STATE_LOG_WEAPON:h.logWeapon=v.i;return;
        case GGV_STATE_LOG_INUSE:h.logInuse=v.i;return;
        default:FAIL("State store outside qualified context");break;
        }h.event(1,b);
    }
    static ggv_value call(void *p,ggv_service s,ggv_value a,ggv_value b,ggv_value c){auto &h=*static_cast<StateHost *>(p);ggv_value v{};
        switch(s){
        case GGV_STATE_SELECT_HAND:REQUIRE((h.handnum==0||h.handnum==1));h.selected=&h.hands[h.handnum];h.event(2,s,h.handnum);break;
        case GGV_STATE_ALTERNATE_FUNCTION:
            REQUIRE(a.i==1-int32_t(h.selected->function));
            if(h.mutateOnAlternate){h.selected->state=88;h.selected->weapon=42;h.selected->inuse=-1;}
            if(h.switchPlayerOnAlternate)h.playernum=1;
            v.ref=h.alternatePresent?&h.alternateToken:nullptr;h.event(2,s,a.i);break;
        case GGV_STATE_LOG:
            REQUIRE(a.i==h.handnum);REQUIRE(h.logWeapon==h.selected->weapon);REQUIRE(h.logInuse==h.selected->inuse);
            h.event(2,s,a.i,b.i,c.b);break;
        default:FAIL("State service outside narrow native domain");break;
        }return v;
    }
    int32_t run(){ggv_host host{this,read,write,call};int32_t r=-99;REQUIRE(ggvExecute(ggvStockProgram(GGV_SET_STATE),&host,&r));return r;}
    size_t calls(ggv_service s)const{return std::count_if(events.begin(),events.end(),[&](const Event&e){return e.kind==2&&e.operation==s;});}
};
struct DispatchHost {
    bool graphPresent{},graphHasType{},definitionPresent{true},functionPresent{true};
    int32_t graphType{1},legacyType{1},weapon{2},trigger{1},function{},handnum{},lvupdate{17};
    bool useComposedDrivers{},nestedAbsent{},stateFault{};int32_t scriptedResult{};
    int definitionToken{};StateHost state;
    struct Event{int kind,operation,result;};std::vector<Event> events;std::vector<ggv_binding> reads;
    static ggv_value read(void *p,ggv_binding b){auto &h=*static_cast<DispatchHost *>(p);h.reads.push_back(b);ggv_value v{};
        switch(b){
        case GGV_EMPTY_DEFINITION:v.ref=h.definitionPresent?&h.definitionToken:nullptr;break;
        case GGV_GRAPH_HAS_TYPE:REQUIRE(h.graphPresent);v.b=h.graphHasType;break;
        case GGV_GRAPH_FUNCTION_TYPE:REQUIRE(h.graphPresent);REQUIRE(h.graphHasType);v.i=h.graphType;break;
        case GGV_LEGACY_FUNCTION_TYPE:REQUIRE(h.functionPresent);v.i=h.legacyType;break;
        case GGV_WEAPON_NUMBER:v.i=uint8_t(h.weapon);break;
        case GGV_TRIGGER:v.b=h.trigger;break;
        case GGV_HAND_NUMBER:v.i=h.handnum;break;
        case GGV_EMPTY_LVUPDATE:v.i=h.lvupdate;break;
        default:FAIL("Dispatcher read outside qualified domain");break;
        }return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v){auto &h=*static_cast<DispatchHost *>(p);REQUIRE(b==GGV_WEAPON_FUNCTION);h.function=uint8_t(v.i);h.events.push_back({1,b,h.function});}
    int32_t composedDriver(ggv_service s){
        if(s==GGV_DISPATCH_THROW){ThrowHost h;h.minor=99;h.functionPresent=!nestedAbsent;return h.run();}
        if(s==GGV_DISPATCH_MELEE){MeleeHost h;h.minor=99;h.functionPresent=!nestedAbsent;return h.run();}
        if(s==GGV_DISPATCH_SPECIAL){SpecialHost h;h.minor=99;h.functionPresent=!nestedAbsent;return h.run();}
        REQUIRE(s==GGV_DISPATCH_SHOOT);
        auto fixture=std::make_unique<native_shooting_attack_case>(attackBaseline());fixture->stateminor=99;fixture->function_absent=nestedAbsent;
        auto h=std::make_unique<AttackHost>(*fixture);return h->run(GGV_ATTACK_SHOOT);
    }
    static ggv_value call(void *p,ggv_service s,ggv_value a,ggv_value b,ggv_value){auto &h=*static_cast<DispatchHost *>(p);ggv_value v{};
        switch(s){
        case GGV_RESOLVE_GRAPH:v.b=h.graphPresent;break;
        case GGV_RESOLVE_FUNCTION:v.b=h.functionPresent;break;
        case GGV_DISPATCH_SHOOT:case GGV_DISPATCH_THROW:case GGV_DISPATCH_MELEE:case GGV_DISPATCH_SPECIAL:
            v.b=h.useComposedDrivers?h.composedDriver(s):h.scriptedResult;break;
        case GGV_EMPTY_SET_STATE:
            REQUIRE(a.i==h.handnum);REQUIRE(b.i==0);
            if(h.stateFault)v.b=0; // Explicit structural fault response, not a native IDLE rejection.
            else {h.state.handnum=a.i;h.state.request=b.i;v.b=h.state.run();}break;
        default:FAIL("Dispatcher service outside qualified domain");break;
        }h.events.push_back({2,s,v.b});return v;
    }
    int32_t run(){ggv_host host{this,read,write,call};int32_t r=-99;REQUIRE(ggvExecute(ggvStockProgram(GGV_ATTACK_DISPATCH),&host,&r));return r;}
    size_t calls(ggv_service s)const{return std::count_if(events.begin(),events.end(),[&](const Event&e){return e.kind==2&&e.operation==s;});}
};
}
TEST_CASE("State utility targets current selected hand and preserves six reset stores","[ggv][state]") {
    for(int handnum:{0,1})for(int32_t request:std::vector<int32_t>{-1,0,2,6,8,std::numeric_limits<int32_t>::min(),std::numeric_limits<int32_t>::max()}){
        StateHost h;h.handnum=handnum;h.request=request;const auto peer=h.hands[1-handnum];REQUIRE(h.run()==1);
        const auto &selected=h.hands[handnum];REQUIRE(selected.state==request);REQUIRE(selected.frames==0);REQUIRE(selected.flags==0);REQUIRE(selected.cycles==0);REQUIRE(selected.minor==0);REQUIRE(selected.last==0);
        REQUIRE(h.hands[1-handnum].state==peer.state);REQUIRE(h.hands[1-handnum].flags==peer.flags);REQUIRE(h.hands[1-handnum].frames==peer.frames);REQUIRE(h.hands[1-handnum].last==peer.last);
        std::vector<int> writes;for(const auto&e:h.events)if(e.kind==1)writes.push_back(e.operation);
        REQUIRE(writes==std::vector<int>{GGV_STATE_VALUE,GGV_STATE_FRAMES,GGV_STATE_FLAGS,GGV_STATE_CYCLES,GGV_STATE_MINOR,GGV_STATE_LAST_FRAME});
        REQUIRE(h.calls(GGV_STATE_ALTERNATE_FUNCTION)==0);REQUIRE(h.calls(GGV_STATE_LOG)==1);
        REQUIRE(h.events.back().b==9);REQUIRE(h.events.back().c==1);
    }
}
TEST_CASE("State CHANGEFUNC alone queries exact alternate and refuses without resets","[ggv][state]") {
    for(int function:{0,1,255})for(bool present:{false,true})for(int player:{0,1}){
        StateHost h;h.request=7;h.hands[0].function=uint8_t(function);h.alternatePresent=present;h.playernum=player;const auto before=h.hands[0];
        REQUIRE(h.run()==int(present));REQUIRE(h.calls(GGV_STATE_ALTERNATE_FUNCTION)==1);
        REQUIRE(h.events[1].operation==GGV_STATE_ALTERNATE_FUNCTION);REQUIRE(h.events[1].a==1-function);
        REQUIRE(h.calls(GGV_STATE_LOG)==(player==0?1u:0u));
        if(!present){REQUIRE(h.hands[0].state==before.state);REQUIRE(h.hands[0].frames==before.frames);REQUIRE(h.hands[0].flags==before.flags);REQUIRE(h.hands[0].cycles==before.cycles);REQUIRE(h.hands[0].minor==before.minor);REQUIRE(h.hands[0].last==before.last);
            REQUIRE(std::none_of(h.events.begin(),h.events.end(),[](const StateHost::Event&e){return e.kind==1;}));}
    }
}
TEST_CASE("State captures prior value before lookup and reads logging fields after resets","[ggv][state]") {
    StateHost h;h.request=7;h.mutateOnAlternate=true;REQUIRE(h.run()==1);
    REQUIRE(h.events.back().operation==GGV_STATE_LOG);REQUIRE(h.events.back().b==9);REQUIRE(h.events.back().c==1);
    REQUIRE(h.logWeapon==42);REQUIRE(h.logInuse==-1);REQUIRE(h.hands[0].state==7);
    StateHost switched;switched.request=7;switched.switchPlayerOnAlternate=true;REQUIRE(switched.run()==1);
    REQUIRE(switched.calls(GGV_STATE_LOG)==0);REQUIRE(std::count(switched.reads.begin(),switched.reads.end(),GGV_WEAPON_NUMBER)==0);
    StateHost other;other.playernum=2;other.request=0;REQUIRE(other.run()==1);REQUIRE(other.calls(GGV_STATE_LOG)==0);
}
TEST_CASE("Attack outer dispatcher resolves graph before lazy function and unknown types finish","[ggv][dispatch]") {
    for(bool definition:{false,true})for(bool function:{false,true}){
        DispatchHost h;h.definitionPresent=definition;h.functionPresent=function;h.legacyType=0;h.lvupdate=-123;
        REQUIRE(h.run()==-123);REQUIRE(h.events.front().operation==GGV_RESOLVE_GRAPH);REQUIRE(h.calls(GGV_RESOLVE_FUNCTION)==(definition?1u:0u));
        REQUIRE(h.calls(GGV_EMPTY_SET_STATE)==1);REQUIRE(h.state.hands[0].state==0);
        REQUIRE(std::count(h.reads.begin(),h.reads.end(),GGV_LEGACY_FUNCTION_TYPE)==(definition&&function?1:0));
    }
    for(int32_t type:std::vector<int32_t>{0,5,0x100,std::numeric_limits<int32_t>::min()}){
        DispatchHost h;h.legacyType=type;REQUIRE(h.run()==17);REQUIRE(h.events.size()==3);
    }
}
TEST_CASE("Attack outer low-byte graph selection dispatches one exact service lazily","[ggv][dispatch]") {
    struct Row{int32_t type;ggv_service service;};
    for(Row row:std::vector<Row>{{1,GGV_DISPATCH_SHOOT},{0x101,GGV_DISPATCH_SHOOT},{-255,GGV_DISPATCH_SHOOT},{2,GGV_DISPATCH_THROW},{0x202,GGV_DISPATCH_THROW},{3,GGV_DISPATCH_MELEE},{4,GGV_DISPATCH_SPECIAL}})
        for(bool graph:{false,true})for(bool hasType:{false,true}){
            DispatchHost h;h.graphPresent=graph;h.graphHasType=hasType;h.graphType=row.type;h.legacyType=graph&&hasType?99:row.type;
            REQUIRE(h.run()==0);REQUIRE(h.calls(row.service)==1);REQUIRE(h.calls(GGV_EMPTY_SET_STATE)==0);
            REQUIRE(std::count(h.reads.begin(),h.reads.end(),GGV_WEAPON_NUMBER)==0);
            REQUIRE(std::count(h.reads.begin(),h.reads.end(),GGV_LEGACY_FUNCTION_TYPE)==(graph&&hasType?0:1));
        }
}
TEST_CASE("Attack composes all four actual stock drivers and actual state utility","[ggv][dispatch][composition]") {
    // Inner null/unknown phases exercise real programs under disclosed scripted
    // lookup fixtures, not complete engine or native whole-body oracle proof.
    for(int type:{1,2,3,4})for(bool innerAbsent:{false,true}){
        DispatchHost h;h.legacyType=type;h.useComposedDrivers=true;h.nestedAbsent=innerAbsent;h.lvupdate=1234;
        REQUIRE(h.run()==(innerAbsent?1234:0));REQUIRE(h.calls(GGV_EMPTY_SET_STATE)==(innerAbsent?1u:0u));
        REQUIRE(h.state.calls(GGV_STATE_SELECT_HAND)==(innerAbsent?1u:0u));
    }
}
TEST_CASE("Attack Reaper completion selects secondary with narrowing and skips state change","[ggv][dispatch]") {
    for(bool finished:{false,true})for(int trigger:{0,1,7}){
        DispatchHost h;h.scriptedResult=finished;h.weapon=276;h.trigger=trigger;h.function=255;
        REQUIRE(h.run()==(finished&&!trigger?17:0));REQUIRE(h.function==(finished&&trigger?1:255));
        REQUIRE(h.calls(GGV_EMPTY_SET_STATE)==(finished&&!trigger?1u:0u));
        REQUIRE(std::count(h.reads.begin(),h.reads.end(),GGV_WEAPON_NUMBER)==(finished?1:0));
    }
    DispatchHost absent;absent.definitionPresent=false;absent.weapon=20;absent.trigger=1;REQUIRE(absent.run()==0);REQUIRE(absent.function==1);
    DispatchHost fault;fault.scriptedResult=1;fault.stateFault=true;REQUIRE(fault.run()==0);
    REQUIRE(std::count(fault.reads.begin(),fault.reads.end(),GGV_EMPTY_LVUPDATE)==0);
}
TEST_CASE("State and attack dispatcher callback refusal preserves canaries before effects","[ggv][state][dispatch]") {
    StateHost state;ggv_host sh{&state,StateHost::read,StateHost::write,nullptr};int32_t out=0x123456;
    REQUIRE_FALSE(ggvExecute(ggvStockProgram(GGV_SET_STATE),&sh,&out));REQUIRE(out==0x123456);REQUIRE(state.selected==nullptr);REQUIRE(state.events.empty());REQUIRE(state.reads.empty());
    DispatchHost dispatch;ggv_host dh{&dispatch,DispatchHost::read,DispatchHost::write,nullptr};
    REQUIRE_FALSE(ggvExecute(ggvStockProgram(GGV_ATTACK_DISPATCH),&dh,&out));REQUIRE(out==0x123456);REQUIRE(dispatch.events.empty());REQUIRE(dispatch.reads.empty());
}

namespace {
/* Declared physical field widths, not a claim of compiler-specific bit packing
 * or complete native animation/input execution. Borrowed pointer is not owned. */
struct AnimationUtilityHost {
    struct Bits {uint8_t latch:1;uint8_t bit02:1;uint8_t incremental:1;uint8_t peer3:3;uint8_t peer7:1;uint8_t peer8:1;};
    struct Eject {uint8_t state:4;uint8_t type:3;uint8_t bit07:1;};
    Bits bits{1,1,1,5,1,1};Eject eject{9,5,1};uint8_t loop{255};
    int32_t load{23},mode{2};int commandToken{42};void *command{&commandToken};
    std::vector<ggv_binding> reads,writes;
    struct Snapshot{int32_t load,mode;int latch,incremental,bit02,bit07,loop;bool commandNull;};
    std::vector<Snapshot> snapshots;
    static ggv_value read(void *p,ggv_binding b){auto &h=*static_cast<AnimationUtilityHost *>(p);h.reads.push_back(b);ggv_value v{};
        REQUIRE(b==GGV_EMPTY_ANIM_MODE);v.i=h.mode;return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v){auto &h=*static_cast<AnimationUtilityHost *>(p);
        switch(b){
        case GGV_ANIM_LOAD:h.load=v.i;break;
        case GGV_EMPTY_ANIM_MODE:h.mode=v.i;break;
        case GGV_ANIMATION_LATCH:h.bits.latch=v.b;break;
        case GGV_ANIM_INCREMENTAL:h.bits.incremental=v.b;break;
        case GGV_ANIM_COMMAND:h.command=v.ref;break;
        case GGV_ANIM_LOOP_COUNT:h.loop=v.i;break;
        case GGV_ANIM_BIT02:h.bits.bit02=v.b;break;
        case GGV_ANIM_BIT07:h.eject.bit07=v.b;break;
        default:FAIL("Animation utility store outside exact eight-field domain");break;
        }h.writes.push_back(b);h.snapshots.push_back({h.load,h.mode,h.bits.latch,h.bits.incremental,h.bits.bit02,h.eject.bit07,h.loop,h.command==nullptr});
    }
    int32_t run(ggv_stock_program program){ggv_host host{this,read,write,nullptr};int32_t result=-99;REQUIRE(ggvExecute(ggvStockProgram(program),&host,&result));return result;}
};
struct TriggerUtilityHost {
    struct Hand{int32_t on{17},previous{0x1357},released{-3},peer{0x7531};};Hand hands[2]{};Hand *selected{};
    int32_t handnum{},request{};int playerToken{1};void *currentPlayer{&playerToken};void *observedPlayer{};
    bool mutateReleasedInInput{};int inputCalls{};int32_t pressed{-1},released{-1};
    struct Event{int kind,operation;int32_t on,previous,released;};std::vector<Event> events;std::vector<ggv_binding> reads;
    void event(int kind,int op){REQUIRE(selected!=nullptr);events.push_back({kind,op,selected->on,selected->previous,selected->released});}
    static ggv_value read(void *p,ggv_binding b){auto &h=*static_cast<TriggerUtilityHost *>(p);REQUIRE(h.selected!=nullptr);h.reads.push_back(b);ggv_value v{};
        switch(b){
        case GGV_TRIGGER:v.b=h.selected->on;break;
        case GGV_TRIGGER_PREVIOUS:v.b=h.selected->previous;break;
        case GGV_TRIGGER_REQUEST:v.b=h.request;break;
        case GGV_TRIGGER_PLAYER:REQUIRE(h.events.size()>=3);REQUIRE(h.events[1].operation==GGV_TRIGGER_PREVIOUS);REQUIRE(h.events[2].operation==GGV_TRIGGER);v.ref=h.currentPlayer;break;
        case GGV_HAND_NUMBER:v.i=h.handnum;break;
        default:FAIL("Trigger read outside qualified native domain");break;
        }return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v){auto &h=*static_cast<TriggerUtilityHost *>(p);
        switch(b){
        case GGV_TRIGGER_PREVIOUS:h.selected->previous=v.b;break;
        case GGV_TRIGGER:h.selected->on=v.b;break;
        case GGV_TRIGGER_RELEASED:h.selected->released=v.b;break;
        default:FAIL("Trigger store outside three BOOL32 fields");break;
        }h.event(1,b);
    }
    static ggv_value call(void *p,ggv_service s,ggv_value,ggv_value,ggv_value){auto &h=*static_cast<TriggerUtilityHost *>(p);
        REQUIRE(s==GGV_STATE_SELECT_HAND);REQUIRE((h.handnum==0||h.handnum==1));h.selected=&h.hands[h.handnum];h.event(2,s);return ggv_value{};
    }
    static ggv_value call4(void *p,ggv_service4 s,ggv_value a,ggv_value b,ggv_value c,ggv_value d){auto &h=*static_cast<TriggerUtilityHost *>(p);
        REQUIRE(s==GGV_TRIGGER_V2_INPUT);REQUIRE(a.ref==h.currentPlayer);REQUIRE(b.i==h.handnum);
        REQUIRE((c.b==0||c.b==1));REQUIRE((d.b==0||d.b==1));
        h.observedPlayer=a.ref;h.pressed=c.b;h.released=d.b;++h.inputCalls;h.event(3,s);
        // Explicit adversarial engine-leaf response, not an asserted behavior of GraphV2.
        if(h.mutateReleasedInInput)h.selected->released=77;
        return ggv_value{};
    }
    int32_t run(){ggv_host host{this,read,write,call,call4};int32_t result=-99;REQUIRE(ggvExecute(ggvStockProgram(GGV_TRIGGER_INPUT),&host,&result));return result;}
};
}
TEST_CASE("Animation reset emits all eight ordered physical stores and preserves adjacent bit fields","[ggv][animation]") {
    AnimationUtilityHost h;void *borrowed=h.command;REQUIRE(borrowed==&h.commandToken);REQUIRE(h.run(GGV_ANIMATION_RESET)==0);
    REQUIRE(h.writes==std::vector<ggv_binding>{GGV_ANIM_LOAD,GGV_EMPTY_ANIM_MODE,GGV_ANIMATION_LATCH,GGV_ANIM_INCREMENTAL,GGV_ANIM_COMMAND,GGV_ANIM_LOOP_COUNT,GGV_ANIM_BIT02,GGV_ANIM_BIT07});
    REQUIRE(h.reads.empty());REQUIRE(h.load==-1);REQUIRE(h.mode==0);REQUIRE(h.bits.latch==0);REQUIRE(h.bits.incremental==0);
    REQUIRE(h.command==nullptr);REQUIRE(h.loop==0);REQUIRE(h.bits.bit02==0);REQUIRE(h.eject.bit07==0);
    REQUIRE(h.bits.peer3==5);REQUIRE(h.bits.peer7==1);REQUIRE(h.bits.peer8==1);REQUIRE(h.eject.state==9);REQUIRE(h.eject.type==5);REQUIRE(h.commandToken==42);
    REQUIRE(h.snapshots[0].load==-1);REQUIRE(h.snapshots[0].mode==2);REQUIRE(h.snapshots[1].mode==0);REQUIRE(h.snapshots[1].latch==1);
    REQUIRE(h.snapshots[2].latch==0);REQUIRE(h.snapshots[2].incremental==1);REQUIRE(h.snapshots[3].incremental==0);REQUIRE_FALSE(h.snapshots[3].commandNull);
    REQUIRE(h.snapshots[4].commandNull);REQUIRE(h.snapshots[4].loop==255);REQUIRE(h.snapshots[5].loop==0);REQUIRE(h.snapshots[5].bit02==1);
    REQUIRE(h.snapshots[6].bit02==0);REQUIRE(h.snapshots[6].bit07==1);REQUIRE(h.snapshots[7].bit07==0);
    REQUIRE(h.run(GGV_ANIMATION_RESET)==0);REQUIRE(h.writes.size()==16);REQUIRE(h.commandToken==42);
}
TEST_CASE("Animation busy is the complete not-idle predicate with no stores or normalization of mode","[ggv][animation]") {
    for(int32_t mode:std::vector<int32_t>{0,1,2,-1,std::numeric_limits<int32_t>::min(),std::numeric_limits<int32_t>::max()}){
        AnimationUtilityHost h;h.mode=mode;REQUIRE(h.run(GGV_ANIMATION_BUSY)==(mode==0?0:1));
        REQUIRE(h.reads==std::vector<ggv_binding>{GGV_EMPTY_ANIM_MODE});REQUIRE(h.writes.empty());REQUIRE(h.mode==mode);REQUIRE(h.command==&h.commandToken);
    }
}
TEST_CASE("Trigger preserves raw BOOL32 previous and on values while emitting one logical input event","[ggv][input]") {
    for(int handnum:{0,1})for(int32_t old:std::vector<int32_t>{0,1,2,-1,std::numeric_limits<int32_t>::min()})
        for(int32_t on:std::vector<int32_t>{0,1,2,-7,std::numeric_limits<int32_t>::max()}){
            TriggerUtilityHost h;h.handnum=handnum;h.hands[handnum].on=old;h.request=on;const auto peer=h.hands[1-handnum];
            REQUIRE(h.run()==0);REQUIRE(h.hands[handnum].previous==old);REQUIRE(h.hands[handnum].on==on);
            REQUIRE(h.hands[handnum].released==(on==0?1:-3));REQUIRE(h.inputCalls==1);REQUIRE(h.observedPlayer==&h.playerToken);
            REQUIRE(h.pressed==(on!=0&&old==0?1:0));REQUIRE(h.released==(on==0&&old!=0?1:0));
            REQUIRE(h.hands[1-handnum].on==peer.on);REQUIRE(h.hands[1-handnum].previous==peer.previous);REQUIRE(h.hands[1-handnum].released==peer.released);REQUIRE(h.hands[handnum].peer==0x7531);
            REQUIRE(std::count(h.reads.begin(),h.reads.end(),GGV_TRIGGER_PREVIOUS)==1);
            REQUIRE(h.events[1].operation==GGV_TRIGGER_PREVIOUS);REQUIRE(h.events[1].previous==old);REQUIRE(h.events[1].on==old);
            REQUIRE(h.events[2].operation==GGV_TRIGGER);REQUIRE(h.events[2].on==on);
            REQUIRE(h.events[3].kind==3);REQUIRE(h.events[3].previous==old);REQUIRE(h.events[3].on==on);REQUIRE(h.events[3].released==-3);
            REQUIRE(h.events.size()==(on==0?5u:4u));if(on==0)REQUIRE(h.events[4].operation==GGV_TRIGGER_RELEASED);
        }
}
TEST_CASE("Trigger event precedes final release store and held input never clears a returned release value","[ggv][input]") {
    TriggerUtilityHost off;off.request=0;off.mutateReleasedInInput=true;REQUIRE(off.run()==0);REQUIRE(off.hands[0].released==1);
    TriggerUtilityHost on;on.request=2;on.mutateReleasedInInput=true;REQUIRE(on.run()==0);REQUIRE(on.hands[0].released==77);
    REQUIRE(off.events[3].released==-3);REQUIRE(off.events[4].released==1);REQUIRE(on.events.size()==4);
    TriggerUtilityHost repeated;repeated.hands[0].on=0;repeated.request=2;REQUIRE(repeated.run()==0);REQUIRE(repeated.pressed==1);
    REQUIRE(repeated.run()==0);REQUIRE(repeated.hands[0].previous==2);REQUIRE(repeated.pressed==0);REQUIRE(repeated.released==0);REQUIRE(repeated.inputCalls==2);
}
TEST_CASE("Trigger four-input signature refuses wrong operand type during preparation","[ggv][input]") {
    auto source=ggvStockProgram(GGV_TRIGGER_INPUT)->source;
    auto it=std::find_if(source.nodes,source.nodes+source.count,[](const ggv_node &n){return n.op==GGV_CALL4;});REQUIRE(it!=source.nodes+source.count);
    REQUIRE(it->service4==GGV_TRIGGER_V2_INPUT);it->c=6; // Hand I32 cannot replace pressed BOOL32.
    ggv_program p{};char error[128]{};REQUIRE_FALSE(ggvPrepare(&source,&p,error,sizeof(error)));REQUIRE(p.validated==0);
}
TEST_CASE("Animation and trigger malformed host refusal precedes field mutation and selection","[ggv][animation][input]") {
    AnimationUtilityHost animation;ggv_host missing{&animation,AnimationUtilityHost::read,nullptr,nullptr};int32_t out=0x12345;
    REQUIRE_FALSE(ggvExecute(ggvStockProgram(GGV_ANIMATION_RESET),&missing,&out));REQUIRE(out==0x12345);REQUIRE(animation.load==23);REQUIRE(animation.writes.empty());REQUIRE(animation.command==&animation.commandToken);
    TriggerUtilityHost trigger;ggv_host noFour{&trigger,TriggerUtilityHost::read,TriggerUtilityHost::write,TriggerUtilityHost::call,nullptr};
    REQUIRE_FALSE(ggvExecute(ggvStockProgram(GGV_TRIGGER_INPUT),&noFour,&out));REQUIRE(out==0x12345);REQUIRE(trigger.selected==nullptr);REQUIRE(trigger.events.empty());REQUIRE(trigger.reads.empty());
}

namespace {
/* Inert source-contract metadata/player hosts. No complete native/provider
 * differential or invalid native array-index safety is claimed. */
struct AmmoCountHost {
    struct Player{int32_t reserve{1};int8_t inuse[2]{1,1},ammo[2]{1,1};uint8_t weapon[2]{2,28};int32_t loaded[2][2]{{2,4},{8,16}};};
    Player players[2]{};Player *cached{};int global{},hand{-1},slot{-1},request{1};
    bool switchAfterReserve{},mutateInFlag{};bool flags[2][2]{{true,false},{false,true}};
    struct Event{int kind,operation,hand,slot,value;};std::vector<Event> events;
    static ggv_value read(void *p,ggv_binding b){auto &h=*static_cast<AmmoCountHost *>(p);ggv_value v{};
        switch(b){
        case GGV_AMMO_RESERVE:REQUIRE(h.cached==nullptr);v.i=h.players[h.global].reserve;h.events.push_back({1,b,-1,-1,v.i});if(h.switchAfterReserve)h.global=1;return v;
        case GGV_AMMO_REQUEST:v.i=h.request;break;
        case GGV_AMMO_HAND_INUSE:REQUIRE(h.cached!=nullptr);v.b=h.cached->inuse[h.hand];break;
        case GGV_AMMO_EQUIPPED_TYPE:REQUIRE(h.cached!=nullptr);v.i=h.cached->ammo[h.slot];break;
        case GGV_AMMO_HAND_WEAPON:REQUIRE(h.cached!=nullptr);v.i=h.cached->weapon[h.hand];break;
        case GGV_AMMO_LOADED:REQUIRE(h.cached!=nullptr);v.i=h.cached->loaded[h.hand][h.slot];break;
        default:FAIL("Ammo count read outside fixed native traversal domain");break;
        }h.events.push_back({1,b,h.hand,h.slot,b==GGV_AMMO_HAND_INUSE?v.b:v.i});return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v){auto &h=*static_cast<AmmoCountHost *>(p);
        REQUIRE((v.i==0||v.i==1));if(b==GGV_AMMO_HAND_INDEX)h.hand=v.i;else if(b==GGV_AMMO_SLOT_INDEX)h.slot=v.i;else FAIL("Count must not mutate native player state");
    }
    static ggv_value call(void *p,ggv_service s,ggv_value a,ggv_value b,ggv_value c){auto &h=*static_cast<AmmoCountHost *>(p);ggv_value v{};
        if(s==GGV_AMMO_SELECT_PLAYER){h.cached=&h.players[h.global];h.events.push_back({2,s,-1,-1,h.global});}
        else if(s==GGV_AMMO_HAS_FLAG){REQUIRE(a.i==h.cached->weapon[h.hand]);REQUIRE(b.i==h.slot);REQUIRE(c.u==1);
            v.b=h.flags[h.hand][h.slot];h.events.push_back({2,s,h.hand,h.slot,v.b});
            // Explicit adversarial leaf response; native property getter mutation is unqualified.
            if(h.mutateInFlag){h.cached->loaded[h.hand][h.slot]=90;h.global=0;}
        }else FAIL("Count service outside native property boundary");return v;
    }
    int32_t run(ggv_stock_program program){cached=nullptr;ggv_host host{this,read,write,call};int32_t result=-99;REQUIRE(ggvExecute(ggvStockProgram(program),&host,&result));return result;}
    size_t count(int kind,int op)const{return std::count_if(events.begin(),events.end(),[&](const Event&e){return e.kind==kind&&e.operation==op;});}
};
struct AmmoEligibilityHost {
    bool functionPresent{true},weaponAvailable{true},ammoPresent{true},composedCount{};
    int32_t type{1},special{5},ammoIndex{};uint32_t ammoType{1};int32_t countResult{1};int token{};
    std::vector<ggv_binding> reads;std::vector<int32_t> countArguments;AmmoCountHost count;
    static ggv_value read(void *p,ggv_binding b){auto &h=*static_cast<AmmoEligibilityHost *>(p);h.reads.push_back(b);ggv_value v{};
        switch(b){
        case GGV_AMMO_FUNCTION_REF:v.ref=h.functionPresent?&h.token:nullptr;break;
        case GGV_LEGACY_FUNCTION_TYPE:REQUIRE(h.functionPresent);v.i=h.type;break;
        case GGV_SPECIAL_LEGACY_VALUE:REQUIRE((uint32_t(h.type)&0xff)==4);v.i=h.special;break;
        case GGV_LEGACY_AMMO_INDEX:v.i=std::bit_cast<int8_t>(uint8_t(h.ammoIndex));break;
        case GGV_AMMO_WEAPON_REF:REQUIRE(h.weaponAvailable);REQUIRE((h.ammoIndex==0||h.ammoIndex==1));v.ref=h.ammoPresent?&h.token:nullptr;break;
        case GGV_AMMO_DEFINITION_TYPE:REQUIRE(h.weaponAvailable);REQUIRE(h.ammoPresent);v.u=h.ammoType;break;
        default:FAIL("Eligibility read outside lazy native metadata domain");break;
        }return v;
    }
    static void write(void *,ggv_binding,ggv_value){FAIL("Complete ammo exclusion predicate has no stores");}
    static ggv_value call(void *p,ggv_service s,ggv_value a,ggv_value,ggv_value){auto &h=*static_cast<AmmoEligibilityHost *>(p);REQUIRE(s==GGV_AMMO_TOTAL_COUNT);h.countArguments.push_back(a.i);ggv_value v{};
        if(h.composedCount){REQUIRE(a.i>=0);REQUIRE(a.i<33);h.count.request=a.i;v.i=h.count.run(GGV_AMMO_COUNT);}else v.i=h.countResult;return v;
    }
    int32_t run(){ggv_host host{this,read,write,call};int32_t result=-99;REQUIRE(ggvExecute(ggvStockProgram(GGV_AMMO_ELIGIBILITY),&host,&result));return result;}
};
struct UnlimitedAmmoHost {
    uint32_t type{};int32_t stage{};std::vector<ggv_binding> reads;
    static ggv_value read(void *p,ggv_binding b){auto &h=*static_cast<UnlimitedAmmoHost *>(p);h.reads.push_back(b);ggv_value v{};
        if(b==GGV_AMMO_REQUEST_U32)v.u=h.type;else if(b==GGV_AMMO_STAGE)v.i=h.stage;else FAIL("Unlimited policy read outside scalar/stage domain");return v;
    }
    static void write(void *,ggv_binding,ggv_value){FAIL("Unlimited policy emitted a store");}
    int32_t run(){ggv_host host{this,read,write,nullptr};int32_t result=-99;REQUIRE(ggvExecute(ggvStockProgram(GGV_UNLIMITED_AMMO),&host,&result));return result;}
};
}
TEST_CASE("Ammo count unrolls native hand-slot order and reserved-only metadata filter","[ggv][ammo]") {
    AmmoCountHost total;REQUIRE(total.run(GGV_AMMO_COUNT)==31);REQUIRE(total.count(2,GGV_AMMO_HAS_FLAG)==0);
    AmmoCountHost reserved;REQUIRE(reserved.run(GGV_RESERVED_AMMO_COUNT)==19);REQUIRE(reserved.count(2,GGV_AMMO_HAS_FLAG)==4);
    std::vector<int> loadedOrder;for(const auto&e:total.events)if(e.kind==1&&e.operation==GGV_AMMO_LOADED)loadedOrder.push_back(e.hand*2+e.slot);
    REQUIRE(loadedOrder==std::vector<int>{0,1,2,3});loadedOrder.clear();for(const auto&e:reserved.events)if(e.kind==1&&e.operation==GGV_AMMO_LOADED)loadedOrder.push_back(e.hand*2+e.slot);
    REQUIRE(loadedOrder==std::vector<int>{0,3});REQUIRE(total.count(1,GGV_AMMO_EQUIPPED_TYPE)==4);
    struct Row{int8_t first,second;int32_t expected;};
    for(Row row:std::vector<Row>{{0,0,1},{1,0,7},{0,1,25},{-1,-128,31}}){
        AmmoCountHost h;h.players[0].inuse[0]=row.first;h.players[0].inuse[1]=row.second;REQUIRE(h.run(GGV_AMMO_COUNT)==row.expected);
        REQUIRE(h.count(1,GGV_AMMO_EQUIPPED_TYPE)==size_t((row.first?2:0)+(row.second?2:0)));
    }
    AmmoCountHost mismatch;mismatch.players[0].ammo[1]=int8_t(-1);REQUIRE(mismatch.run(GGV_RESERVED_AMMO_COUNT)==3);
    REQUIRE(mismatch.count(2,GGV_AMMO_HAS_FLAG)==2);REQUIRE(mismatch.count(1,GGV_AMMO_LOADED)==1);
}
TEST_CASE("Ammo counts preserve signed wrapping and reserve-before-cached-player boundary","[ggv][ammo]") {
    AmmoCountHost wrap;wrap.players[0].reserve=std::numeric_limits<int32_t>::max();wrap.players[0].loaded[0][0]=2;wrap.players[0].inuse[1]=0;wrap.players[0].ammo[1]=2;
    REQUIRE(wrap.run(GGV_AMMO_COUNT)==-2147483647);
    AmmoCountHost negative;negative.players[0].reserve=std::numeric_limits<int32_t>::min();negative.players[0].loaded[0][0]=-1;negative.players[0].inuse[1]=0;negative.players[0].ammo[1]=2;
    REQUIRE(negative.run(GGV_RESERVED_AMMO_COUNT)==2147483647);
    AmmoCountHost switched;switched.switchAfterReserve=true;switched.players[0].reserve=100;switched.players[1].reserve=900;
    switched.players[1].loaded[0][0]=1;switched.players[1].loaded[0][1]=2;switched.players[1].loaded[1][0]=3;switched.players[1].loaded[1][1]=4;
    REQUIRE(switched.run(GGV_AMMO_COUNT)==110);REQUIRE(switched.events[0].operation==GGV_AMMO_RESERVE);REQUIRE(switched.events[1].operation==GGV_AMMO_SELECT_PLAYER);REQUIRE(switched.events[1].value==1);
    AmmoCountHost leaf;leaf.mutateInFlag=true;leaf.players[0].inuse[1]=0;leaf.players[0].ammo[1]=2;
    REQUIRE(leaf.run(GGV_RESERVED_AMMO_COUNT)==91);REQUIRE(leaf.count(1,GGV_AMMO_LOADED)==1);
}
TEST_CASE("Ammo exclusion keeps null full NONE and low-byte melee special exceptions lazy","[ggv][ammo]") {
    AmmoEligibilityHost absent;absent.functionPresent=false;absent.weaponAvailable=false;REQUIRE(absent.run()==1);REQUIRE(absent.reads.size()==1);
    for(int32_t type:{0,3,0x103}){AmmoEligibilityHost h;h.type=type;h.weaponAvailable=false;REQUIRE(h.run()==1);REQUIRE(h.countArguments.empty());}
    for(int special:{5,6,7,8}){
        AmmoEligibilityHost h;h.type=0x104;h.special=special;h.ammoIndex=-1;h.weaponAvailable=false;
        REQUIRE(h.run()==(special==8?1:0));REQUIRE(h.countArguments.empty());
        REQUIRE(std::count(h.reads.begin(),h.reads.end(),GGV_SPECIAL_LEGACY_VALUE)==(special==5?1:special==6?2:3));
    }
    AmmoEligibilityHost rawNone;rawNone.type=0x100;REQUIRE(rawNone.run()==0);REQUIRE(rawNone.countArguments==std::vector<int32_t>{1});
}
TEST_CASE("Ammo exclusion preserves signed throw slots and lazy weapon ammo reference","[ggv][ammo]") {
    for(int slot:{-1,-2,-128}){AmmoEligibilityHost h;h.type=2;h.ammoIndex=slot;h.weaponAvailable=false;REQUIRE(h.run()==1);REQUIRE(h.countArguments.empty());}
    AmmoEligibilityHost noAmmo;noAmmo.ammoIndex=-1;noAmmo.weaponAvailable=false;REQUIRE(noAmmo.run()==0);REQUIRE(noAmmo.countArguments.empty());
    AmmoEligibilityHost nullAmmo;nullAmmo.ammoPresent=false;REQUIRE(nullAmmo.run()==0);REQUIRE(nullAmmo.countArguments.empty());
    REQUIRE(std::count(nullAmmo.reads.begin(),nullAmmo.reads.end(),GGV_AMMO_DEFINITION_TYPE)==0);
    for(int32_t count:std::vector<int32_t>{std::numeric_limits<int32_t>::min(),-1,0,1,std::numeric_limits<int32_t>::max()}){
        AmmoEligibilityHost h;h.countResult=count;REQUIRE(h.run()==(count<=0?1:0));REQUIRE(h.countArguments==std::vector<int32_t>{1});
    }
}
TEST_CASE("Ammo exclusion invokes complete new ammo-count program not supplied whole-count result","[ggv][ammo][composition]") {
    for(int32_t reserve:{-1,0,1}){
        AmmoEligibilityHost h;h.composedCount=true;h.count.players[0].reserve=reserve;h.count.players[0].inuse[0]=0;h.count.players[0].inuse[1]=0;
        REQUIRE(h.run()==(reserve<=0?1:0));REQUIRE(h.countArguments==std::vector<int32_t>{1});REQUIRE(h.count.count(2,GGV_AMMO_SELECT_PLAYER)==1);
    }
    AmmoEligibilityHost clips;clips.composedCount=true;clips.count.players[0].reserve=0;REQUIRE(clips.run()==0);
    REQUIRE(clips.count.count(1,GGV_AMMO_LOADED)==4);REQUIRE(clips.count.count(2,GGV_AMMO_HAS_FLAG)==0);
}
TEST_CASE("Unlimited ammo preserves eleven forbidden classes u32 unknowns and lazy mission exceptions","[ggv][ammo]") {
    for(uint32_t type:std::vector<uint32_t>{22,23,24,25,26,27,28,29,30,31,32}){
        UnlimitedAmmoHost h;h.type=type;h.stage=29;REQUIRE(h.run()==0);REQUIRE(h.reads==std::vector<ggv_binding>{GGV_AMMO_REQUEST_U32});
    }
    for(uint32_t type:std::vector<uint32_t>{0,1,13,33,0x7fffffffu,0x80000000u,0xffffffffu}){
        UnlimitedAmmoHost h;h.type=type;h.stage=49;REQUIRE(h.run()==1);REQUIRE(h.reads.size()==1);
    }
    for(int stage:{-1,29,49,66}){
        UnlimitedAmmoHost remote;remote.type=12;remote.stage=stage;REQUIRE(remote.run()==(stage==29?0:1));REQUIRE(remote.reads.size()==2);
        UnlimitedAmmoHost timed;timed.type=14;timed.stage=stage;REQUIRE(timed.run()==(stage==49?0:1));REQUIRE(timed.reads.size()==2);
    }
}
TEST_CASE("Ammo policy missing callbacks refuse before player access with output canary intact","[ggv][ammo]") {
    AmmoCountHost count;ggv_host noCalls{&count,AmmoCountHost::read,AmmoCountHost::write,nullptr};int32_t out=0x12345;
    REQUIRE_FALSE(ggvExecute(ggvStockProgram(GGV_AMMO_COUNT),&noCalls,&out));REQUIRE(out==0x12345);REQUIRE(count.events.empty());REQUIRE(count.cached==nullptr);
    REQUIRE_FALSE(ggvExecute(ggvStockProgram(GGV_RESERVED_AMMO_COUNT),&noCalls,&out));REQUIRE(out==0x12345);REQUIRE(count.events.empty());
    AmmoEligibilityHost exclusion;ggv_host noCount{&exclusion,AmmoEligibilityHost::read,AmmoEligibilityHost::write,nullptr};
    REQUIRE_FALSE(ggvExecute(ggvStockProgram(GGV_AMMO_ELIGIBILITY),&noCount,&out));REQUIRE(out==0x12345);REQUIRE(exclusion.reads.empty());
}

namespace {
/* Inert source-contract metadata/array hosts; no native index/provider safety
 * or complete original/current query-body differential is claimed. */
struct AmmoStatusHost {
    bool present{true};int32_t function{},weapon{2},legacyIndex{},index{-99},equipped{1},loaded{},clip{10},reserve{};
    std::vector<ggv_binding> reads;std::vector<ggv_service> calls;int32_t resolvedFunction{-99};
    static ggv_value read(void *p,ggv_binding b){auto &h=*static_cast<AmmoStatusHost *>(p);h.reads.push_back(b);ggv_value v{};
        switch(b){
        case GGV_QUERY_FUNCTION_I32:v.i=h.function;break;
        case GGV_LEGACY_AMMO_INDEX:REQUIRE(h.present);v.i=std::bit_cast<int8_t>(uint8_t(h.legacyIndex));break;
        case GGV_QUERY_EQUIPPED_TYPE:REQUIRE((h.index==0||h.index==1));v.i=std::bit_cast<int8_t>(uint8_t(h.equipped));break;
        case GGV_LOADED_AMMO:REQUIRE((h.index==0||h.index==1));v.i=h.loaded;break;
        case GGV_QUERY_CLIP_CAPACITY:v.i=h.clip;break;
        case GGV_WEAPON_NUMBER:v.i=h.weapon;break;
        case GGV_QUERY_RESERVE:v.i=h.reserve;break;
        default:FAIL("Ammo status read outside native domain");break;
        }return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v){auto &h=*static_cast<AmmoStatusHost *>(p);REQUIRE(b==GGV_AMMO_INDEX);h.index=v.i;}
    static ggv_value call(void *p,ggv_service s,ggv_value a,ggv_value b,ggv_value){auto &h=*static_cast<AmmoStatusHost *>(p);h.calls.push_back(s);ggv_value v{};
        if(s==GGV_QUERY_RESOLVE_FUNCTION){h.resolvedFunction=a.i;v.b=h.present;}
        else if(s==GGV_MELEE_MIN_CLIP){MinClipHost nested;nested.weapon=a.i;nested.function=b.i;v.i=nested.run();}
        else FAIL("Ammo status service outside metadata/composed utility boundary");return v;
    }
    int32_t run(){ggv_host host{this,read,write,call};int32_t result=-99;REQUIRE(ggvExecute(ggvStockProgram(GGV_AMMO_STATUS),&host,&result));return result;}
};
struct AmmoQueryHost {
    struct Ammo{uint32_t type;};Ammo ammo[2]{{1},{2}};Ammo *cached{};int tableIndex{},weaponToken{};
    uint32_t weaponU{2};int32_t weaponI{2},slotI{},ammotype{1},capacity{200},reservedResult{77};uint32_t slotU{};
    bool weaponPresent{true},ammoPresent{true},swapAfterRef{},composedReserved{};
    std::vector<ggv_binding> reads;std::vector<ggv_service> calls;std::vector<int32_t> lookupArgs,reservedArgs;AmmoCountHost count;
    static ggv_value read(void *p,ggv_binding b){auto &h=*static_cast<AmmoQueryHost *>(p);h.reads.push_back(b);ggv_value v{};
        switch(b){
        case GGV_QUERY_CAPACITY:v.i=h.capacity;break;
        case GGV_QUERY_WEAPON_U32:v.u=h.weaponU;break;
        case GGV_QUERY_WEAPON_I32:v.i=h.weaponI;break;
        case GGV_QUERY_AMMO_REF_U32:case GGV_QUERY_AMMO_REF_I32:
            REQUIRE(h.weaponPresent);if(b==GGV_QUERY_AMMO_REF_U32)REQUIRE(h.slotU<2);else REQUIRE((h.slotI==0||h.slotI==1));
            v.ref=h.ammoPresent?&h.ammo[h.tableIndex]:nullptr;if(h.swapAfterRef)h.tableIndex=1;break;
        case GGV_QUERY_AMMO_TYPE:REQUIRE(h.weaponPresent);REQUIRE(h.ammoPresent);v.u=h.ammo[h.tableIndex].type;break;
        case GGV_QUERY_CACHED_TYPE:REQUIRE(h.cached!=nullptr);v.u=h.cached->type;break;
        case GGV_QUERY_CACHED_CAPACITY:REQUIRE(h.cached!=nullptr);v.i=h.capacity;break;
        default:FAIL("Ammo query read outside borrowed metadata domain");break;
        }return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v){auto &h=*static_cast<AmmoQueryHost *>(p);REQUIRE(b==GGV_QUERY_CACHED_AMMO);h.cached=static_cast<Ammo *>(v.ref);}
    static ggv_value call(void *p,ggv_service s,ggv_value a,ggv_value,ggv_value){auto &h=*static_cast<AmmoQueryHost *>(p);h.calls.push_back(s);ggv_value v{};
        if(s==GGV_QUERY_FIND_WEAPON){h.lookupArgs.push_back(a.i);v.ref=h.weaponPresent?&h.weaponToken:nullptr;}
        else if(s==GGV_QUERY_RESERVED_COUNT){h.reservedArgs.push_back(a.i);if(h.composedReserved){REQUIRE(a.i>=0);REQUIRE(a.i<33);h.count.request=a.i;v.i=h.count.run(GGV_RESERVED_AMMO_COUNT);}else v.i=h.reservedResult;}
        else FAIL("Ammo query service outside native metadata/composed count boundary");return v;
    }
    int32_t run(ggv_stock_program program){ggv_host host{this,read,write,call};int32_t result=-99;REQUIRE(ggvExecute(ggvStockProgram(program),&host,&result));return result;}
};
}
TEST_CASE("Ammo status preserves all five returns exact zero reserve and early lazy exits","[ggv][ammo][query]") {
    AmmoStatusHost absent;absent.present=false;absent.function=-17;REQUIRE(absent.run()==-1);REQUIRE(absent.resolvedFunction==-17);REQUIRE(absent.reads==std::vector<ggv_binding>{GGV_QUERY_FUNCTION_I32});
    AmmoStatusHost noAmmo;noAmmo.legacyIndex=255;REQUIRE(noAmmo.run()==3);REQUIRE(noAmmo.index==-99);REQUIRE(noAmmo.reads.size()==2);
    AmmoStatusHost unequipped;unequipped.equipped=255;REQUIRE(unequipped.run()==3);REQUIRE(std::count(unequipped.reads.begin(),unequipped.reads.end(),GGV_LOADED_AMMO)==0);
    AmmoStatusHost full;full.loaded=10;full.weapon=28;full.function=1;REQUIRE(full.run()==3);REQUIRE(full.calls.size()==1);REQUIRE(std::count(full.reads.begin(),full.reads.end(),GGV_QUERY_RESERVE)==0);
    for(int loaded:{0,1})for(int reserve:{-1,0,5}){
        AmmoStatusHost h;h.loaded=loaded;h.reserve=reserve;REQUIRE(h.run()==(loaded==0?(reserve==0?-1:0):(reserve==0?2:1)));
        REQUIRE(std::count(h.reads.begin(),h.reads.end(),GGV_QUERY_RESERVE)==1);REQUIRE(std::count(h.reads.begin(),h.reads.end(),GGV_LOADED_AMMO)==2);
    }
}
TEST_CASE("Ammo status keeps exact shotgun and composed tranquilizer secondary thresholds","[ggv][ammo][query]") {
    struct Row{int weapon,function,loaded,result,minCalls;};
    for(Row row:std::vector<Row>{{19,0,1,2,0},{19,1,1,-1,0},{19,1,2,2,0},{28,0,1,2,0},{28,1,3,-1,1},{28,1,4,2,1},{2,1,1,2,0}}){
        AmmoStatusHost h;h.weapon=row.weapon;h.function=row.function;h.loaded=row.loaded;h.legacyIndex=257;
        REQUIRE(h.run()==row.result);REQUIRE(h.index==1);REQUIRE(std::count(h.calls.begin(),h.calls.end(),GGV_MELEE_MIN_CLIP)==row.minCalls);
    }
}
TEST_CASE("Ammo metadata type query preserves u32 lookup and result representations and null laziness","[ggv][ammo][query]") {
    for(uint32_t weapon:std::vector<uint32_t>{2,0x80000000u,0xffffffffu})for(uint32_t type:std::vector<uint32_t>{0,1,0x80000000u,0xffffffffu}){
        AmmoQueryHost h;h.weaponU=weapon;h.ammo[0].type=type;REQUIRE(uint32_t(h.run(GGV_AMMO_TYPE_QUERY))==type);
        REQUIRE(h.lookupArgs==std::vector<int32_t>{std::bit_cast<int32_t>(weapon)});REQUIRE(h.calls.size()==1);
    }
    AmmoQueryHost noWeapon;noWeapon.weaponPresent=false;REQUIRE(noWeapon.run(GGV_AMMO_TYPE_QUERY)==0);REQUIRE(noWeapon.reads.size()==1);
    AmmoQueryHost noAmmo;noAmmo.ammoPresent=false;REQUIRE(noAmmo.run(GGV_AMMO_TYPE_QUERY)==0);REQUIRE(std::count(noAmmo.reads.begin(),noAmmo.reads.end(),GGV_QUERY_AMMO_TYPE)==0);
}
TEST_CASE("Ammo quantity keeps cached reference and composes complete reserved count","[ggv][ammo][query]") {
    AmmoQueryHost noWeapon;noWeapon.weaponPresent=false;REQUIRE(noWeapon.run(GGV_AMMO_QTY_QUERY)==0);REQUIRE(noWeapon.reservedArgs.empty());
    AmmoQueryHost noAmmo;noAmmo.ammoPresent=false;REQUIRE(noAmmo.run(GGV_AMMO_QTY_QUERY)==0);REQUIRE(noAmmo.cached==nullptr);REQUIRE(noAmmo.reservedArgs.empty());
    AmmoQueryHost cached;cached.swapAfterRef=true;REQUIRE(cached.run(GGV_AMMO_QTY_QUERY)==77);REQUIRE(cached.cached==&cached.ammo[0]);REQUIRE(cached.reservedArgs==std::vector<int32_t>{1});
    AmmoQueryHost reread;reread.swapAfterRef=true;REQUIRE(reread.run(GGV_AMMO_TYPE_QUERY)==2); // Explicit synthetic metadata-table response, not native mutation proof.
    AmmoQueryHost composed;composed.composedReserved=true;REQUIRE(composed.run(GGV_AMMO_QTY_QUERY)==19);REQUIRE(composed.count.count(2,GGV_AMMO_HAS_FLAG)==4);
}
TEST_CASE("Capacity queries preserve signed field return cached ammo and unchecked native weapon precondition","[ggv][ammo][query]") {
    for(int32_t capacity:std::vector<int32_t>{-1,0,200,std::numeric_limits<int32_t>::max()}){
        AmmoQueryHost h;h.capacity=capacity;REQUIRE(h.run(GGV_AMMO_CAPACITY)==capacity);REQUIRE(h.calls.empty());REQUIRE(h.reads==std::vector<ggv_binding>{GGV_QUERY_CAPACITY});
        AmmoQueryHost weapon;weapon.capacity=capacity;weapon.weaponI=-19;weapon.swapAfterRef=true;
        REQUIRE(weapon.run(GGV_AMMO_WEAPON_CAPACITY)==capacity);REQUIRE(weapon.lookupArgs==std::vector<int32_t>{-19});REQUIRE(weapon.cached==&weapon.ammo[0]);
    }
    AmmoQueryHost noAmmo;noAmmo.ammoPresent=false;REQUIRE(noAmmo.run(GGV_AMMO_WEAPON_CAPACITY)==0);
    // Do not execute the original invalid NULL-weapon dereference. Inspect the
    // exact structural precondition instead of inventing a safe runtime case.
    const auto &source=ggvStockProgram(GGV_AMMO_WEAPON_CAPACITY)->source;
    REQUIRE(source.nodes[1].op==GGV_CALL);REQUIRE(source.nodes[1].service==GGV_QUERY_FIND_WEAPON);
    REQUIRE(source.nodes[2].op==GGV_READ);REQUIRE(source.nodes[2].binding==GGV_QUERY_AMMO_REF_I32);
}
TEST_CASE("Ammo query callback refusal precedes metadata reads and preserves output canaries","[ggv][ammo][query]") {
    AmmoStatusHost status;ggv_host sh{&status,AmmoStatusHost::read,AmmoStatusHost::write,nullptr};int32_t out=0x12345;
    REQUIRE_FALSE(ggvExecute(ggvStockProgram(GGV_AMMO_STATUS),&sh,&out));REQUIRE(out==0x12345);REQUIRE(status.reads.empty());REQUIRE(status.calls.empty());
    for(auto program:{GGV_AMMO_TYPE_QUERY,GGV_AMMO_QTY_QUERY,GGV_AMMO_WEAPON_CAPACITY}){
        AmmoQueryHost h;ggv_host qh{&h,AmmoQueryHost::read,AmmoQueryHost::write,nullptr};
        REQUIRE_FALSE(ggvExecute(ggvStockProgram(program),&qh,&out));REQUIRE(out==0x12345);REQUIRE(h.reads.empty());REQUIRE(h.calls.empty());
    }
}

namespace {
/* Inert typed scalar/metadata/field hosts. No engine/provider qualification or
 * complete frozen-original differential is claimed by these authored cases. */
struct ReloadHost {
    int32_t weapon{2},function{},activeFunction{},ammoindex{-99},reloadindex{-99};
    uint8_t one{},check{},validWeapon{};bool pal{},jpn{};int32_t ticks{213};
    bool present[2]{true,true};int8_t indices[2]{0,1},equipped[2]{1,2};
    int32_t loaded[2]{2,0},clip[2]{10,10},reserves[2][33]{};uint16_t rounds[4]{};uint32_t flags[2]{};
    int player{};int32_t *reserveTarget{};bool switchAfterClip{},switchAfterReserve{};
    std::vector<int> resolverScript;size_t resolverPosition{};
    struct Event {int kind,id;int32_t value;bool operator==(const Event&)const=default;};
    std::vector<Event> events;std::vector<int32_t> resolutions,transfers,indexArgs;
    ReloadHost(){reserves[0][1]=20;reserves[0][2]=20;reserves[1][1]=100;reserves[1][2]=100;}
    static ggv_value read(void *p,ggv_binding b){auto &h=*static_cast<ReloadHost*>(p);ggv_value v{};
        switch(b){
        case GGV_WEAPON_NUMBER:v.i=h.weapon;break;
        case GGV_RELOAD_FUNCTION:v.i=h.function;break;
        case GGV_LEGACY_AMMO_INDEX:REQUIRE((h.activeFunction==0||h.activeFunction==1));v.i=h.indices[h.activeFunction];break;
        case GGV_QUERY_EQUIPPED_TYPE:REQUIRE((h.ammoindex==0||h.ammoindex==1));v.i=h.equipped[h.ammoindex];break;
        case GGV_QUERY_CLIP_CAPACITY:v.i=h.clip[h.ammoindex];break;
        case GGV_LOADED_AMMO:v.i=h.loaded[h.ammoindex];break;
        case GGV_RELOAD_VALID_WEAPON:v.b=h.validWeapon;break;
        case GGV_RELOAD_CHECK:v.b=h.check;break;
        case GGV_RELOAD_ONE:v.b=h.one;break;
        case GGV_RELOAD_ROUNDS:REQUIRE(h.reloadindex>=0);REQUIRE(h.reloadindex<4);v.u=h.rounds[h.reloadindex];break;
        case GGV_RELOAD_TICKS256:v.i=h.ticks;break;
        case GGV_RELOAD_PAL:v.b=h.pal;break;
        case GGV_RELOAD_JPN:v.b=h.jpn;break;
        case GGV_RELOAD_RESERVE:REQUIRE(h.equipped[h.ammoindex]>=0);REQUIRE(h.equipped[h.ammoindex]<33);h.reserveTarget=&h.reserves[h.player][h.equipped[h.ammoindex]];v.i=*h.reserveTarget;break;
        case GGV_RELOAD_AMMO_FLAGS:v.u=h.flags[h.ammoindex];break;
        default:FAIL("Reload read outside qualified field domain");break;
        }int32_t observed=ggvBindingType(b)==GGV_U32?std::bit_cast<int32_t>(v.u):ggvBindingType(b)==GGV_BOOL?v.b:v.i;
        h.events.push_back({0,int(b),observed});return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v){auto &h=*static_cast<ReloadHost*>(p);h.events.push_back({1,int(b),v.i});
        switch(b){
        case GGV_AMMO_INDEX:h.ammoindex=v.i;break;
        case GGV_RELOAD_INDEX:h.reloadindex=v.i;break;
        case GGV_LOADED_AMMO:h.loaded[h.ammoindex]=v.i;if(h.switchAfterClip)h.player=1;break;
        case GGV_RELOAD_RESERVE:REQUIRE(h.reserveTarget!=nullptr);*h.reserveTarget=v.i;if(h.switchAfterReserve)h.player=0;break;
        case GGV_RELOAD_RESERVE_CLEAR:h.reserves[h.player][h.equipped[h.ammoindex]]=v.i;break;
        default:FAIL("Reload write outside borrowed/local field domain");break;
        }
    }
    static ggv_value call(void *p,ggv_service s,ggv_value a,ggv_value,ggv_value){auto &h=*static_cast<ReloadHost*>(p);h.events.push_back({2,int(s),a.i});ggv_value v{};
        if(s==GGV_QUERY_RESOLVE_FUNCTION){REQUIRE((a.i==0||a.i==1));h.activeFunction=a.i;h.resolutions.push_back(a.i);v.b=h.resolverPosition<h.resolverScript.size()?h.resolverScript[h.resolverPosition]:h.present[a.i];++h.resolverPosition;}
        else if(s==GGV_RELOAD_INDEX_FOR_WEAPON){h.indexArgs.push_back(a.i);auto previous=h.weapon;h.weapon=a.i;v.i=h.run(GGV_UNEQUIPPED_RELOAD_INDEX);h.weapon=previous;}
        else if(s==GGV_SVC_RELOAD_TRANSFER){h.transfers.push_back(a.i);
            auto previousFunction=h.function;auto previousActive=h.activeFunction;auto previousAmmo=h.ammoindex;auto previousReload=h.reloadindex;auto previousTarget=h.reserveTarget;auto previousOne=h.one;auto previousCheck=h.check;
            h.function=a.i;h.one=0;h.check=1;h.run(GGV_RELOAD_TRANSFER); // Actual complete stock child; no supplied result.
            h.function=previousFunction;h.activeFunction=previousActive;h.ammoindex=previousAmmo;h.reloadindex=previousReload;h.reserveTarget=previousTarget;h.one=previousOne;h.check=previousCheck;
        }else FAIL("Reload service outside metadata/composed child boundary");return v;
    }
    int32_t run(ggv_stock_program p){ggv_host host{this,read,write,call};int32_t result=-99;REQUIRE(ggvExecute(ggvStockProgram(p),&host,&result));return result;}
    int count(int kind,int id)const{return int(std::count_if(events.begin(),events.end(),[&](auto e){return e.kind==kind&&e.id==id;}));}
    std::vector<ggv_binding> stores()const{std::vector<ggv_binding> out;for(auto e:events)if(e.kind==1)out.push_back(ggv_binding(e.id));return out;}
};
ggv_source guardedScalarSource(ggv_op op,int32_t a,int32_t b,unsigned dst=2,bool afterWrite=false){
    ggv_source s{};s.count=afterWrite?5:4;
    for(unsigned k=0;k<2;++k){auto &n=s.nodes[k];n.op=GGV_CONST;n.dst=k;n.type=op==GGV_I_DIV_TRUNC?GGV_I32:GGV_U32;if(n.type==GGV_I32)n.literal.i=k?b:a;else n.literal.u=uint32_t(k?b:a);}
    s.nodes[2].op=op;s.nodes[2].dst=dst;s.nodes[2].a=0;s.nodes[2].b=1;
    if(op==GGV_U_SHR){s.count+=1;for(unsigned k=s.count-1;k>3;--k)s.nodes[k]=s.nodes[k-1];s.nodes[3].op=GGV_U_TO_I;s.nodes[3].dst=3;s.nodes[3].a=dst;}
    const unsigned out=op==GGV_U_SHR?3:dst;const unsigned last=s.count-1;
    if(afterWrite){s.nodes[last-1].op=GGV_WRITE;s.nodes[last-1].binding=GGV_SHOTS;s.nodes[last-1].a=out;}
    s.nodes[last].op=GGV_RETURN_I32;s.nodes[last].a=out;return s;
}
}
TEST_CASE("Prepared truncating I32 divide preserves signed literals and aliased registers","[ggv][reload][divshift]") {
    struct Row{int32_t a,b,out;};
    for(Row row:std::vector<Row>{{7,3,2},{-7,3,-2},{7,-3,-2},{-7,-3,2},{0,-1,0},{std::numeric_limits<int32_t>::min(),1,std::numeric_limits<int32_t>::min()},{std::numeric_limits<int32_t>::max(),2,1073741823}})for(unsigned dst:{0u,1u,2u}){
        auto source=guardedScalarSource(GGV_I_DIV_TRUNC,row.a,row.b,dst);ggv_program p{};REQUIRE(ggvPrepare(&source,&p,nullptr,0));ReloadHost h;ggv_host host{&h,ReloadHost::read,ReloadHost::write,nullptr};int32_t result=123;
        REQUIRE(ggvExecute(&p,&host,&result));REQUIRE(result==row.out);REQUIRE(h.events.empty());
    }
}
TEST_CASE("Prepared U32 logical shift preserves high bits count endpoints and aliases","[ggv][reload][divshift]") {
    struct Row{uint32_t a,b,out;};
    for(Row row:std::vector<Row>{{0,31,0},{0xffffffffu,0,0xffffffffu},{0xffffffffu,1,0x7fffffffu},{0x80000000u,31,1},{0x80000000u,8,0x00800000u},{65535,8,255}})for(unsigned dst:{0u,1u,2u}){
        auto source=guardedScalarSource(GGV_U_SHR,std::bit_cast<int32_t>(row.a),int32_t(row.b),dst);ggv_program p{};REQUIRE(ggvPrepare(&source,&p,nullptr,0));ReloadHost h;ggv_host host{&h,ReloadHost::read,ReloadHost::write,nullptr};int32_t result=123;
        REQUIRE(ggvExecute(&p,&host,&result));REQUIRE(uint32_t(result)==row.out);REQUIRE(h.events.empty());
    }
}
TEST_CASE("Guarded scalar refusals preserve result canary and forbid later host effects","[ggv][reload][divshift]") {
    struct Row{ggv_op op;int32_t a,b;};
    for(Row row:std::vector<Row>{{GGV_I_DIV_TRUNC,5,0},{GGV_I_DIV_TRUNC,std::numeric_limits<int32_t>::min(),-1},{GGV_U_SHR,-1,32},{GGV_U_SHR,-1,-1}}){
        auto source=guardedScalarSource(row.op,row.a,row.b,2,true);ggv_program p{};REQUIRE(ggvPrepare(&source,&p,nullptr,0));ReloadHost h;ggv_host host{&h,ReloadHost::read,ReloadHost::write,nullptr};int32_t result=0x12345678;
        REQUIRE_FALSE(ggvExecute(&p,&host,&result));REQUIRE(result==0x12345678);REQUIRE(h.events.empty());
    }
    for(ggv_op op:{GGV_I_DIV_TRUNC,GGV_U_SHR})for(int defect:{0,1,2}){
        auto source=guardedScalarSource(op,8,1);if(defect==0)source.nodes[0].type=GGV_F32;if(defect==1)source.nodes[2].a=31;if(defect==2)source.nodes[2].dst=32;
        ggv_program p{};const auto before=p;REQUIRE_FALSE(ggvPrepare(&source,&p,nullptr,0));REQUIRE(std::memcmp(&p,&before,sizeof p)==0);
    }
}
TEST_CASE("Unequipped reload index preserves four exact weapon matches and default","[ggv][reload]") {
    struct Row{int32_t weapon,index;};for(Row row:std::vector<Row>{{27,0},{19,1},{8,2},{9,3},{0,-1},{-1,-1},{2,-1},{std::numeric_limits<int32_t>::min(),-1},{std::numeric_limits<int32_t>::max(),-1}}){
        ReloadHost h;h.weapon=row.weapon;REQUIRE(h.run(GGV_UNEQUIPPED_RELOAD_INDEX)==row.index);REQUIRE(h.events==std::vector<ReloadHost::Event>{{0,GGV_WEAPON_NUMBER,row.weapon}});
    }
}
TEST_CASE("Reload transfer early metadata exits retain signed8 and lazy field gates","[ggv][reload]") {
    ReloadHost absent;absent.present[0]=false;absent.run(GGV_RELOAD_TRANSFER);REQUIRE(absent.resolutions==std::vector<int32_t>{0});REQUIRE(absent.stores().empty());REQUIRE(absent.events.size()==2);
    ReloadHost noAmmo;noAmmo.indices[0]=-1;noAmmo.run(GGV_RELOAD_TRANSFER);REQUIRE(noAmmo.stores().empty());REQUIRE(noAmmo.count(0,GGV_QUERY_EQUIPPED_TYPE)==0);
    ReloadHost unequipped;unequipped.equipped[0]=-1;unequipped.run(GGV_RELOAD_TRANSFER);REQUIRE(unequipped.stores()==std::vector<ggv_binding>{GGV_AMMO_INDEX});REQUIRE(unequipped.count(0,GGV_QUERY_CLIP_CAPACITY)==0);REQUIRE(unequipped.indexArgs.empty());
    ReloadHost secondary;secondary.function=1;secondary.indices[1]=0;secondary.run(GGV_RELOAD_TRANSFER);REQUIRE(secondary.loaded[0]==10);REQUIRE(secondary.loaded[1]==0);REQUIRE(secondary.resolutions==std::vector<int32_t>{1});
}
TEST_CASE("Reload countdown uses promoted u16 logical shift or PAL integer quotient without clamping","[ggv][reload]") {
    struct Row{bool pal;uint16_t rounds;int32_t loaded,reserve;};
    for(Row row:std::vector<Row>{{false,0,10,992},{false,255,10,992},{false,256,9,993},{false,65535,-245,1247},{true,212,10,992},{true,213,9,993},{true,255,9,993},{true,65535,-297,1299}}){
        ReloadHost h;h.weapon=27;h.check=255;h.pal=row.pal;h.rounds[0]=row.rounds;h.reserves[0][1]=1000;h.run(GGV_RELOAD_TRANSFER);
        REQUIRE(h.loaded[0]==row.loaded);REQUIRE(h.reserves[0][1]==row.reserve);REQUIRE(h.reloadindex==0);REQUIRE(h.count(0,GGV_RELOAD_ROUNDS)==1);REQUIRE(h.count(0,GGV_RELOAD_TICKS256)==int(row.pal));
    }
    for(bool valid:{false,true}){ReloadHost h;h.weapon=27;h.validWeapon=valid;h.check=255;h.rounds[0]=65535;h.run(GGV_RELOAD_TRANSFER);REQUIRE(h.count(0,GGV_RELOAD_ROUNDS)==(valid?0:1));}
    ReloadHost noCheck;noCheck.weapon=27;noCheck.rounds[0]=65535;noCheck.run(GGV_RELOAD_TRANSFER);REQUIRE(noCheck.loaded[0]==10);REQUIRE(noCheck.count(0,GGV_RELOAD_PAL)==0);
}
TEST_CASE("Reload one byte reserve and JPN capacity clamps retain ordering and wrapping stores","[ggv][reload]") {
    for(uint8_t one:{uint8_t(0),uint8_t(1),uint8_t(255)}){ReloadHost h;h.one=one;h.run(GGV_RELOAD_TRANSFER);REQUIRE(h.loaded[0]==(one?3:10));REQUIRE(h.reserves[0][1]==(one?19:12));}
    ReloadHost scarce;scarce.reserves[0][1]=3;scarce.run(GGV_RELOAD_TRANSFER);REQUIRE(scarce.loaded[0]==5);REQUIRE(scarce.reserves[0][1]==0);REQUIRE(scarce.count(0,GGV_RELOAD_RESERVE)==3);
    ReloadHost negative;negative.reserves[0][1]=-3;negative.run(GGV_RELOAD_TRANSFER);REQUIRE(negative.loaded[0]==-1);REQUIRE(negative.reserves[0][1]==0);
    for(bool jpn:{false,true}){ReloadHost h;h.jpn=jpn;h.one=255;h.loaded[0]=10;h.run(GGV_RELOAD_TRANSFER);REQUIRE(h.loaded[0]==(jpn?10:11));REQUIRE(h.reserves[0][1]==(jpn?20:19));REQUIRE(h.count(0,GGV_QUERY_CLIP_CAPACITY)==(jpn?3:1));}
    ReloadHost wrapped;wrapped.loaded[0]=std::numeric_limits<int32_t>::max();wrapped.clip[0]=std::numeric_limits<int32_t>::max();wrapped.one=1;wrapped.run(GGV_RELOAD_TRANSFER);REQUIRE(wrapped.loaded[0]==std::numeric_limits<int32_t>::min());REQUIRE(wrapped.reserves[0][1]==19);
    for(uint32_t flags:{0u,1u,2u,0x80000001u}){ReloadHost h;h.flags[0]=flags;h.run(GGV_RELOAD_TRANSFER);REQUIRE(h.reserves[0][1]==((flags&1)?0:12));auto stores=h.stores();REQUIRE(stores[1]==GGV_LOADED_AMMO);REQUIRE(stores[2]==GGV_RELOAD_RESERVE);REQUIRE(stores.size()==((flags&1)?4:3));if(flags&1)REQUIRE(stores[3]==GGV_RELOAD_RESERVE_CLEAR);}
}
TEST_CASE("Reload reserve compound lvalue and later statements reobserve synthetic current player","[ggv][reload]") {
    ReloadHost h;h.switchAfterClip=true;h.switchAfterReserve=true;h.flags[0]=1;h.run(GGV_RELOAD_TRANSFER);
    // This deliberate host response demonstrates statement boundaries only;
    // no real concurrent/global mutation or engine authority proof is claimed.
    REQUIRE(h.loaded[0]==10);REQUIRE(h.reserves[1][1]==92);REQUIRE(h.reserves[0][1]==0);
    REQUIRE(h.stores()==std::vector<ggv_binding>{GGV_AMMO_INDEX,GGV_LOADED_AMMO,GGV_RELOAD_RESERVE,GGV_RELOAD_RESERVE_CLEAR});
}
TEST_CASE("Two fixed reload slots execute complete children and repeated resolver leaves in order","[ggv][reload]") {
    ReloadHost both;both.run(GGV_RELOAD_BOTH);REQUIRE(both.resolutions==std::vector<int32_t>{0,0,1,1});REQUIRE(both.transfers==std::vector<int32_t>{0,1});REQUIRE(both.loaded[0]==10);REQUIRE(both.loaded[1]==10);REQUIRE(both.reserves[0][1]==12);REQUIRE(both.reserves[0][2]==10);
    ReloadHost shared;shared.indices[1]=0;shared.run(GGV_RELOAD_BOTH);REQUIRE(shared.loaded[0]==10);REQUIRE(shared.reserves[0][1]==12);REQUIRE(shared.count(1,GGV_LOADED_AMMO)==2);
    ReloadHost none;none.present[0]=false;none.present[1]=false;none.run(GGV_RELOAD_BOTH);REQUIRE(none.resolutions==std::vector<int32_t>{0,1});REQUIRE(none.transfers.empty());REQUIRE(none.stores().empty());
    ReloadHost changed;changed.resolverScript={1,0,1,1};changed.run(GGV_RELOAD_BOTH);REQUIRE(changed.transfers==std::vector<int32_t>{0,1});REQUIRE(changed.resolutions==std::vector<int32_t>{0,0,1,1});REQUIRE(changed.loaded[0]==2);REQUIRE(changed.loaded[1]==10);
}

namespace {
/* Narrow typed field/address fixtures, not live actors, concurrency or native
 * provider/ABI proof. Complete original/current oracle is separately assigned. */
struct CountdownHost {
    uint16_t timers[2][2][4]{};int32_t update{1};int hand{-1},slot{-1},player{};
    int updateReads{};std::vector<int32_t> updateScript;bool switchAfterLoad{};
    struct Access{int hand,slot,player;uint16_t value;bool operator==(const Access&)const=default;};
    std::vector<Access> loads,stores;std::vector<ggv_binding> reads,localWrites;
    static ggv_value read(void *p,ggv_binding b){auto &h=*static_cast<CountdownHost*>(p);h.reads.push_back(b);ggv_value v{};
        if(b==GGV_COUNTDOWN_SPENT){REQUIRE(h.hand>=0);REQUIRE(h.hand<2);REQUIRE(h.slot>=0);REQUIRE(h.slot<4);v.u=h.timers[h.player][h.hand][h.slot];h.loads.push_back({h.hand,h.slot,h.player,uint16_t(v.u)});if(h.switchAfterLoad)h.player=1-h.player;}
        else if(b==GGV_COUNTDOWN_UPDATE60){v.i=size_t(h.updateReads)<h.updateScript.size()?h.updateScript[size_t(h.updateReads)]:h.update;++h.updateReads;}
        else FAIL("Countdown read outside literal2x4 fields");return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v){auto &h=*static_cast<CountdownHost*>(p);
        if(b==GGV_COUNTDOWN_HAND_INDEX){h.hand=v.i;h.localWrites.push_back(b);}
        else if(b==GGV_COUNTDOWN_SLOT_INDEX){h.slot=v.i;h.localWrites.push_back(b);}
        else if(b==GGV_COUNTDOWN_SPENT){h.timers[h.player][h.hand][h.slot]=uint16_t(v.u);h.stores.push_back({h.hand,h.slot,h.player,uint16_t(v.u)});}
        else FAIL("Countdown write outside native fields/local indices");
    }
    void run(){ggv_host host{this,read,write,nullptr};int32_t result=-99;REQUIRE(ggvExecute(ggvStockProgram(GGV_UNEQUIPPED_COUNTDOWN),&host,&result));REQUIRE(result==0);}
};
struct GunVisibilityHost {
    uint8_t command{4},upgrades{},op{0xa5};uint16_t parameter{},part{0xcafe},neighbor{0xbeef};
    int currentPlayer{},hands[2][2]{},foreignHand{};void *hand{&hands[0][0]};bool switchAfterCommand{};std::vector<ggv_binding> reads;
    static ggv_value read(void *p,ggv_binding b){auto &h=*static_cast<GunVisibilityHost*>(p);h.reads.push_back(b);ggv_value v{};
        switch(b){
        case GGV_VIS_COMMAND_TYPE:v.i=h.command;if(h.switchAfterCommand)h.currentPlayer=1;break;
        case GGV_VIS_UPGRADES:REQUIRE(h.hand!=nullptr);v.u=h.upgrades;break;
        case GGV_VIS_PARAMETER:v.u=h.parameter;break;
        case GGV_VIS_IS_LEFT_HAND:v.b=h.hand==&h.hands[h.currentPlayer][1];break;
        case GGV_VIS_IS_RIGHT_HAND:v.b=h.hand==&h.hands[h.currentPlayer][0];break;
        default:FAIL("Visibility read outside raw command/native identity leaves");break;
        }return v;
    }
    static void write(void*,ggv_binding,ggv_value){FAIL("Pure visibility must not write");}
    int32_t run(){ggv_host host{this,read,write,nullptr};int32_t result=-99;REQUIRE(ggvExecute(ggvStockProgram(GGV_GUN_VISIBILITY),&host,&result));REQUIRE(op==0xa5);REQUIRE(part==0xcafe);REQUIRE(neighbor==0xbeef);return result;}
};
}
TEST_CASE("Unequipped countdown covers all2x4 U16 fields in native hand slot order","[ggv][countdown]") {
    CountdownHost h;const uint16_t before[8]={0,1,2,65535,256,213,10,42};const uint16_t after[8]={0,0,1,65534,255,212,9,41};
    for(int i=0;i<2;++i)for(int j=0;j<4;++j){h.timers[0][i][j]=before[i*4+j];h.timers[1][i][j]=0x5a5a;}
    h.run();REQUIRE(h.loads.size()==8);REQUIRE(h.stores.size()==8);REQUIRE(h.updateReads==14);REQUIRE(h.localWrites.size()==16);
    for(int i=0;i<2;++i)for(int j=0;j<4;++j){const size_t k=size_t(i*4+j);REQUIRE(h.loads[k]==CountdownHost::Access{i,j,0,before[k]});REQUIRE(h.stores[k]==CountdownHost::Access{i,j,0,after[k]});REQUIRE(h.timers[0][i][j]==after[k]);REQUIRE(h.timers[1][i][j]==0x5a5a);REQUIRE(h.localWrites[k*2]==GGV_COUNTDOWN_HAND_INDEX);REQUIRE(h.localWrites[k*2+1]==GGV_COUNTDOWN_SLOT_INDEX);}
}
TEST_CASE("Countdown preserves raw negative update subtraction and U16 narrowing","[ggv][countdown]") {
    struct Row{int32_t update;uint16_t input,output;int reads;};
    for(Row row:std::vector<Row>{{-1,0,1,16},{-1,65535,0,16},{-65536,42,42,16},{std::numeric_limits<int32_t>::min(),42,42,16},{0,0,0,8},{0,65535,65535,16},{65535,65535,0,8},{std::numeric_limits<int32_t>::max(),65535,0,8}}){
        CountdownHost h;h.update=row.update;for(auto &hand:h.timers[0])for(auto &value:hand)value=row.input;h.run();REQUIRE(h.updateReads==row.reads);for(auto &hand:h.timers[0])for(auto value:hand)REQUIRE(value==row.output);
    }
}
TEST_CASE("Countdown reobserves update at subtraction and player at final store","[ggv][countdown]") {
    CountdownHost repeated;repeated.update=0;repeated.updateScript={0,2};for(auto &hand:repeated.timers[0])for(auto &value:hand)value=1;repeated.run();REQUIRE(repeated.timers[0][0][0]==65535);REQUIRE(repeated.timers[0][0][1]==1);REQUIRE(repeated.updateReads==16);
    CountdownHost switched;switched.switchAfterLoad=true;for(auto &hand:switched.timers[0])for(auto &value:hand)value=10;for(auto &hand:switched.timers[1])for(auto &value:hand)value=20;switched.run();
    // Deliberate fixture response proves source observation boundaries only;
    // no real engine/concurrent current-player mutation has been exercised.
    for(int i=0;i<2;++i)for(int j=0;j<4;++j){const size_t k=size_t(i*4+j);REQUIRE(switched.loads[k].player==int(k%2));REQUIRE(switched.stores[k].player==int(1-k%2));REQUIRE(switched.stores[k].value==(k%2?19:9));REQUIRE(switched.timers[k%2][i][j]==(k%2?20:10));}
}
TEST_CASE("Visibility upgrade command uses exact U8 bits U16 parameter and all valid counts","[ggv][visibility]") {
    struct Row{uint8_t value;int expected[6];};const uint16_t counts[6]={0,1,7,8,15,31};
    for(Row row:std::vector<Row>{{0,{0,0,0,0,0,0}},{1,{1,0,0,0,0,0}},{128,{0,0,1,0,0,0}},{255,{1,1,1,0,0,0}},{0xaa,{0,1,1,0,0,0}}})for(size_t k=0;k<6;++k){
        GunVisibilityHost h;h.upgrades=row.value;h.parameter=counts[k];REQUIRE(h.run()==row.expected[k]);REQUIRE(h.reads==std::vector<ggv_binding>{GGV_VIS_COMMAND_TYPE,GGV_VIS_UPGRADES,GGV_VIS_PARAMETER});
    }
    for(uint16_t count=0;count<32;++count){GunVisibilityHost h;h.upgrades=0xff;h.parameter=count;REQUIRE(h.run()==(count<8?1:0));}
}
TEST_CASE("Visibility left right commands use exact live hand address and no shift fields","[ggv][visibility]") {
    for(uint8_t command:{uint8_t(5),uint8_t(6)})for(int target:{0,1,2,3,4,5}){
        GunVisibilityHost h;h.command=command;h.parameter=65535;
        h.hand=target<4?static_cast<void*>(&h.hands[target/2][target%2]):target==4?static_cast<void*>(&h.foreignHand):nullptr;
        REQUIRE(h.run()==(target==(command==5?1:0)?1:0));REQUIRE(h.reads==std::vector<ggv_binding>{GGV_VIS_COMMAND_TYPE,command==5?GGV_VIS_IS_LEFT_HAND:GGV_VIS_IS_RIGHT_HAND});
    }
    GunVisibilityHost current;current.command=5;current.hand=&current.hands[1][1];current.switchAfterCommand=true;REQUIRE(current.run()==1);
}
TEST_CASE("Visibility unknown raw commands retain default true and untouched neighbor fields","[ggv][visibility]") {
    for(uint8_t command:{uint8_t(0),uint8_t(1),uint8_t(2),uint8_t(3),uint8_t(7),uint8_t(255)}){
        GunVisibilityHost h;h.command=command;h.parameter=65535;h.hand=nullptr;REQUIRE(h.run()==1);REQUIRE(h.reads==std::vector<ggv_binding>{GGV_VIS_COMMAND_TYPE});
    }
}
TEST_CASE("Visibility invalid native shift domain refuses without publishing output or writes","[ggv][visibility]") {
    for(uint16_t count:{uint16_t(32),uint16_t(255),uint16_t(65535)}){
        GunVisibilityHost h;h.parameter=count;h.upgrades=255;ggv_host host{&h,GunVisibilityHost::read,GunVisibilityHost::write,nullptr};int32_t result=0x12345678;
        REQUIRE_FALSE(ggvExecute(ggvStockProgram(GGV_GUN_VISIBILITY),&host,&result));REQUIRE(result==0x12345678);REQUIRE(h.reads.size()==3);REQUIRE(h.op==0xa5);REQUIRE(h.part==0xcafe);REQUIRE(h.neighbor==0xbeef);
    }
    // This is a guarded interpreter refusal, not a test of the original C
    // undefined count>=32 expression or a newly approved gameplay policy.
}

namespace {
/* Captured/fresh field hosts and narrow metadata leaves. Complete child stock
 * programs execute; none supplies an opaque getter/query/setter policy result.
 * Native provider/ABI/lifetime/concurrency and original/current parity remain
 * separate unrun qualifications. */
struct AmmoQuantityHost {
    struct Player{int8_t inuse[2]{1,1},weapon{2};int32_t loaded[2][2]{{2,3},{4,5}},clip[2][2]{{10,10},{10,10}},reserve[33]{};};
    Player players[2];int player{},captured{-1};int32_t hand{},ammotype{1},quantity{7},function{-99},capacity{200};
    uint32_t types[2]{1,2},flags[2]{},weaponU{2},funcU{},rawQuantity{7};
    struct Ammo{uint32_t type;};Ammo metadata[2]{{1},{2}};Ammo *cached{};int weaponToken{},tableIndex{};
    bool weaponPresent{true},ammoPresent{true},swapAfterRef{},switchAfterCapture{},switchDuringGetter{};
    struct Call{ggv_service service;uint32_t a,b,c;bool operator==(const Call&)const=default;};
    struct Store{ggv_binding binding;int32_t value;bool operator==(const Store&)const=default;};
    std::vector<Call> calls;std::vector<Store> stores;std::vector<ggv_binding> reads;std::vector<int> childQueryReads;
    static ggv_value read(void *p,ggv_binding b){auto &h=*static_cast<AmmoQuantityHost*>(p);h.reads.push_back(b);ggv_value v{};
        switch(b){
        case GGV_CURRENT_HAND_INUSE:REQUIRE((h.hand==0||h.hand==1));v.i=h.players[h.player].inuse[h.hand];if(h.switchDuringGetter)h.player=1;break;
        case GGV_CURRENT_WEAPON_NUMBER:v.i=h.players[h.player].weapon;break;
        case GGV_QUANTITY_AMMO_REQUEST:v.i=h.ammotype;break;
        case GGV_QUANTITY_REQUEST:v.i=h.quantity;break;
        case GGV_QUANTITY_REQUEST_U32:v.u=h.rawQuantity;break;
        case GGV_QUANTITY_LOADED_RIGHT:REQUIRE(h.captured>=0);REQUIRE((h.function==0||h.function==1));v.i=h.players[h.captured].loaded[0][h.function];break;
        case GGV_QUANTITY_LOADED_LEFT:REQUIRE(h.captured>=0);REQUIRE((h.function==0||h.function==1));v.i=h.players[h.captured].loaded[1][h.function];break;
        case GGV_QUANTITY_CLIP_RIGHT:REQUIRE(h.captured>=0);REQUIRE((h.function==0||h.function==1));v.i=h.players[h.captured].clip[0][h.function];break;
        case GGV_QUANTITY_CAPACITY:v.i=h.capacity;break;
        case GGV_QUERY_WEAPON_U32:v.u=h.weaponU;break;
        case GGV_QUERY_AMMO_REF_U32:REQUIRE(h.weaponPresent);REQUIRE(h.funcU<2);v.ref=h.ammoPresent?&h.metadata[h.tableIndex]:nullptr;if(h.swapAfterRef)h.tableIndex=1;break;
        case GGV_QUERY_CACHED_TYPE:REQUIRE(h.cached!=nullptr);v.u=h.cached->type;break;
        default:FAIL("Quantity/getter read outside native field metadata domain");break;
        }return v;
    }
    static void write(void *p,ggv_binding b,ggv_value v){auto &h=*static_cast<AmmoQuantityHost*>(p);
        if(b==GGV_QUERY_CACHED_AMMO){h.cached=static_cast<Ammo*>(v.ref);return;}
        h.stores.push_back({b,v.i});
        if(b==GGV_QUANTITY_FUNCTION)h.function=v.i;
        else if(b==GGV_QUANTITY_LOADED_RIGHT){REQUIRE(h.captured>=0);REQUIRE((h.function==0||h.function==1));h.players[h.captured].loaded[0][h.function]=v.i;}
        else if(b==GGV_QUANTITY_RESERVE){REQUIRE(h.captured>=0);REQUIRE(h.ammotype>=0);REQUIRE(h.ammotype<33);h.players[h.captured].reserve[h.ammotype]=v.i;}
        else FAIL("Quantity write outside captured player/local bindings");
    }
    static ggv_value call(void *p,ggv_service s,ggv_value a,ggv_value b,ggv_value c){auto &h=*static_cast<AmmoQuantityHost*>(p);ggv_value v{};
        if(s==GGV_AMMO_SELECT_PLAYER){h.calls.push_back({s,0,0,0});h.captured=h.player;if(h.switchAfterCapture)h.player=1;}
        else if(s==GGV_QUANTITY_CURRENT_WEAPON){h.calls.push_back({s,uint32_t(a.i),0,0});auto previous=h.hand;h.hand=a.i;v.i=h.run(GGV_CURRENT_WEAPON);h.hand=previous;}
        else if(s==GGV_QUANTITY_AMMO_TYPE){h.calls.push_back({s,a.u,b.u,0});REQUIRE(b.u<2);AmmoQueryHost nested;nested.weaponU=a.u;nested.slotU=b.u;nested.ammo[0].type=h.types[b.u];v.u=uint32_t(nested.run(GGV_AMMO_TYPE_QUERY));for(auto r:nested.reads)h.childQueryReads.push_back(int(r));}
        else if(s==GGV_AMMO_HAS_FLAG){h.calls.push_back({s,uint32_t(a.i),uint32_t(b.i),c.u});REQUIRE((b.i==0||b.i==1));v.b=(h.flags[b.i]&c.u)!=0;}
        else if(s==GGV_QUERY_FIND_WEAPON){h.calls.push_back({s,uint32_t(a.i),0,0});v.ref=h.weaponPresent?&h.weaponToken:nullptr;}
        else if(s==GGV_QUANTITY_APPLY){h.calls.push_back({s,uint32_t(a.i),uint32_t(b.i),0});auto previousAmmo=h.ammotype;auto previousQuantity=h.quantity;auto previousCaptured=h.captured;auto previousFunction=h.function;h.ammotype=a.i;h.quantity=b.i;h.run(GGV_SET_AMMO_QUANTITY);h.ammotype=previousAmmo;h.quantity=previousQuantity;h.captured=previousCaptured;h.function=previousFunction;}
        else FAIL("Quantity service outside metadata/complete child boundary");return v;
    }
    int32_t run(ggv_stock_program p){ggv_host host{this,read,write,call};int32_t result=-99;REQUIRE(ggvExecute(ggvStockProgram(p),&host,&result));return result;}
    int count(ggv_binding b)const{return int(std::count(reads.begin(),reads.end(),b));}
    std::vector<Call> flagCalls()const{std::vector<Call> out;for(auto c:calls)if(c.service==GGV_AMMO_HAS_FLAG)out.push_back(c);return out;}
};
}
TEST_CASE("Current weapon getter preserves signed8 zero gating sign extension and fresh owner","[ggv][quantity]") {
    for(int8_t inuse:{int8_t(0),int8_t(1),int8_t(-1),int8_t(-128)})for(int8_t weapon:{int8_t(0),int8_t(2),int8_t(127),int8_t(-128)})for(int hand:{0,1}){
        AmmoQuantityHost h;h.hand=hand;h.players[0].inuse[hand]=inuse;h.players[0].weapon=weapon;REQUIRE(h.run(GGV_CURRENT_WEAPON)==(inuse?int32_t(weapon):0));REQUIRE(h.count(GGV_CURRENT_WEAPON_NUMBER)==(inuse?1:0));REQUIRE(h.stores.empty());REQUIRE(h.calls.empty());
    }
    AmmoQuantityHost fresh;fresh.players[1].weapon=9;fresh.switchDuringGetter=true;REQUIRE(fresh.run(GGV_CURRENT_WEAPON)==9);
}
TEST_CASE("Ammo quantity matches both functions independently with secondary precedence and lazy flags","[ggv][quantity]") {
    struct Row{uint32_t primary,secondary;int selected;};
    for(Row row:std::vector<Row>{{1,2,0},{2,1,1},{1,1,1},{2,3,-1},{0,0,1}}){
        AmmoQuantityHost h;h.types[0]=row.primary;h.types[1]=row.secondary;if(row.primary==0)h.ammotype=0;h.run(GGV_SET_AMMO_QUANTITY);REQUIRE(h.function==row.selected);
        REQUIRE(h.calls[0].service==GGV_AMMO_SELECT_PLAYER);REQUIRE(h.calls[1]==AmmoQuantityHost::Call{GGV_QUANTITY_CURRENT_WEAPON,0,0,0});REQUIRE(h.calls[2]==AmmoQuantityHost::Call{GGV_QUANTITY_AMMO_TYPE,2,0,0});REQUIRE(h.calls[3]==AmmoQuantityHost::Call{GGV_QUANTITY_AMMO_TYPE,2,1,0});
        REQUIRE(h.childQueryReads.size()==6);const auto flags=h.flagCalls();REQUIRE(flags.size()==(row.selected<0?0:2));if(row.selected>=0){REQUIRE(flags[0].c==1);REQUIRE(flags[1].c==2);REQUIRE(flags[0].b==uint32_t(row.selected));}
        REQUIRE(h.count(GGV_QUANTITY_LOADED_RIGHT)==0);REQUIRE(h.count(GGV_QUANTITY_LOADED_LEFT)==0);REQUIRE(h.players[0].reserve[h.ammotype]==7);
    }
}
TEST_CASE("NORESERVE quantity updates only selected right clip with native upper clamp then reserve zero","[ggv][quantity]") {
    struct Row{int32_t request,result;int clipReads;};
    for(Row row:std::vector<Row>{{-9,-7,1},{0,2,1},{7,9,1},{8,10,1},{9,10,2}}){
        AmmoQuantityHost h;h.quantity=row.request;h.flags[0]=1;h.players[0].reserve[1]=77;h.run(GGV_SET_AMMO_QUANTITY);
        REQUIRE(h.players[0].loaded[0][0]==row.result);REQUIRE(h.players[0].loaded[1][0]==4);REQUIRE(h.players[0].reserve[1]==0);REQUIRE(h.count(GGV_QUANTITY_CLIP_RIGHT)==row.clipReads);REQUIRE(h.count(GGV_QUANTITY_CAPACITY)==0);REQUIRE(h.flagCalls().size()==1);
        REQUIRE(h.stores.front()==AmmoQuantityHost::Store{GGV_QUANTITY_FUNCTION,0});REQUIRE(h.stores[1]==AmmoQuantityHost::Store{GGV_QUANTITY_LOADED_RIGHT,int32_t(2+row.request)});REQUIRE(h.stores.back()==AmmoQuantityHost::Store{GGV_QUANTITY_RESERVE,0});
    }
    AmmoQuantityHost secondary;secondary.types[0]=1;secondary.types[1]=1;secondary.flags[1]=3;secondary.quantity=20;secondary.run(GGV_SET_AMMO_QUANTITY);REQUIRE(secondary.players[0].loaded[0][0]==2);REQUIRE(secondary.players[0].loaded[0][1]==10);REQUIRE(secondary.flagCalls().size()==1);
    AmmoQuantityHost wrapped;wrapped.flags[0]=1;wrapped.quantity=1;wrapped.players[0].loaded[0][0]=std::numeric_limits<int32_t>::max();wrapped.run(GGV_SET_AMMO_QUANTITY);REQUIRE(wrapped.players[0].loaded[0][0]==std::numeric_limits<int32_t>::min());
}
TEST_CASE("Quantity reserve capacity accounts both equipped clips and preserves negatives equality wrapping","[ggv][quantity]") {
    struct Row{uint32_t flags;int32_t request,result;int capacityReads;};
    for(Row row:std::vector<Row>{{0,-1,-1,1},{0,200,200,1},{0,201,200,2},{2,193,193,1},{2,194,194,1},{2,195,194,2}}){
        AmmoQuantityHost h;h.flags[0]=row.flags;h.quantity=row.request;h.run(GGV_SET_AMMO_QUANTITY);REQUIRE(h.players[0].reserve[1]==row.result);REQUIRE(h.players[0].loaded[0][0]==2);REQUIRE(h.players[0].loaded[1][0]==4);REQUIRE(h.count(GGV_QUANTITY_CAPACITY)==row.capacityReads);REQUIRE(h.count(GGV_QUANTITY_LOADED_RIGHT)==((row.flags&2)?1:0));REQUIRE(h.count(GGV_QUANTITY_LOADED_LEFT)==((row.flags&2)?1:0));
    }
    AmmoQuantityHost negative;negative.flags[0]=2;negative.capacity=3;negative.quantity=0;negative.run(GGV_SET_AMMO_QUANTITY);REQUIRE(negative.players[0].reserve[1]==-3);
    AmmoQuantityHost wrapped;wrapped.flags[0]=2;wrapped.players[0].loaded[0][0]=std::numeric_limits<int32_t>::max();wrapped.players[0].loaded[1][0]=1;wrapped.capacity=0;wrapped.quantity=0;wrapped.run(GGV_SET_AMMO_QUANTITY);REQUIRE(wrapped.players[0].reserve[1]==std::numeric_limits<int32_t>::min());
}
TEST_CASE("Quantity captured player precedes fresh getter and remains destination after metadata calls","[ggv][quantity]") {
    AmmoQuantityHost h;h.switchAfterCapture=true;h.players[0].weapon=2;h.players[1].weapon=9;h.players[1].reserve[1]=123;h.flags[0]=1;h.run(GGV_SET_AMMO_QUANTITY);
    // Deliberate fixture response proves the source observation contract,
    // not real concurrent owner mutation or native player lifetime safety.
    REQUIRE(h.captured==0);REQUIRE(h.player==1);REQUIRE(h.calls[2].a==9);REQUIRE(h.calls[3].a==9);REQUIRE(h.players[0].loaded[0][0]==9);REQUIRE(h.players[0].reserve[1]==0);REQUIRE(h.players[1].loaded[0][0]==2);REQUIRE(h.players[1].reserve[1]==123);
}
TEST_CASE("Weapon quantity wrapper retains null guards cached ammo and full signed quantity conversion","[ggv][quantity]") {
    AmmoQuantityHost noWeapon;noWeapon.weaponPresent=false;noWeapon.weaponU=0x80000000u;noWeapon.run(GGV_SET_WEAPON_AMMO_QUANTITY);REQUIRE(noWeapon.calls==std::vector<AmmoQuantityHost::Call>{{GGV_QUERY_FIND_WEAPON,0x80000000u,0,0}});REQUIRE(noWeapon.stores.empty());REQUIRE(noWeapon.cached==nullptr);
    AmmoQuantityHost noAmmo;noAmmo.ammoPresent=false;noAmmo.run(GGV_SET_WEAPON_AMMO_QUANTITY);REQUIRE(noAmmo.calls.size()==1);REQUIRE(noAmmo.stores.empty());REQUIRE(noAmmo.cached==nullptr);
    for(uint32_t quantity:std::vector<uint32_t>{0,7,0xffffffffu,0x80000000u}){
        AmmoQuantityHost h;h.swapAfterRef=true;h.rawQuantity=quantity;h.run(GGV_SET_WEAPON_AMMO_QUANTITY);REQUIRE(h.cached==&h.metadata[0]);REQUIRE(h.calls[1]==AmmoQuantityHost::Call{GGV_QUANTITY_APPLY,1,quantity,0});const int32_t signedQuantity=std::bit_cast<int32_t>(quantity);REQUIRE(h.players[0].reserve[1]==(signedQuantity>200?200:signedQuantity));REQUIRE(h.calls[2].service==GGV_AMMO_SELECT_PLAYER);
    }
    // High-bit ammo types/invalid slots are not executed as safe native cases;
    // the original table/index lifetime preconditions remain unqualified.
}
TEST_CASE("Quantity programs refuse missing service before callbacks and preserve output canary","[ggv][quantity]") {
    for(ggv_stock_program p:{GGV_SET_AMMO_QUANTITY,GGV_SET_WEAPON_AMMO_QUANTITY}){AmmoQuantityHost h;ggv_host host{&h,AmmoQuantityHost::read,AmmoQuantityHost::write,nullptr};int32_t result=0x12345678;REQUIRE_FALSE(ggvExecute(ggvStockProgram(p),&host,&result));REQUIRE(result==0x12345678);REQUIRE(h.reads.empty());REQUIRE(h.calls.empty());REQUIRE(h.stores.empty());}
}
