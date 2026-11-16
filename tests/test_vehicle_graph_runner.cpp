/**
 * test_vehicle_graph_runner.cpp -- pd.vehicle_graph.v1 (vehicle graph runner).
 *
 * The schema is context/designs/modding/vehicle-graph-v1.md (accepted by CORE
 * 2026-11-16). The base hoverbike's graph must drive exactly as the original
 * full-throttle bonus did, the Maian graph must turn its crystals black and
 * lock the turret at the boost and heat phase changes, and every broken rule
 * must refuse the program when it compiles.
 */
#include "catch.hpp"

#include <cstring>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "vehicle_boost_heat.h"
#include "vehicle_graph_runner.h"
#include "vehicle_graph_runtime.h"
}

namespace {

const char *const kBaseGraph = R"({
  "schema": "pd.vehicle_graph.v1",
  "catalog_id": "base:vehicle_hoverbike",
  "nodes": [
    { "id": "mount", "kind": "event.vehicle.mount_requested" },
    { "id": "dismount", "kind": "event.vehicle.dismount_requested" },
    { "id": "tick", "kind": "event.vehicle.tick" },
    { "id": "driver_free", "kind": "gate.vehicle.seat_free", "params": { "seat": "driver" } },
    { "id": "mount_driver", "kind": "og.vehicle.mount_seat", "params": { "seat": "driver" } },
    { "id": "dismount_driver", "kind": "og.vehicle.dismount_seat", "params": { "seat": "driver" } },
    { "id": "drive", "kind": "og.vehicle.hover_drive" },
    { "id": "throttle_boost", "kind": "og.vehicle.throttle_boost",
      "params": { "boost_speed": 5.0, "ramp_time": { "value": 2400, "unit": "ticks240" } } }
  ],
  "edges": [
    { "from": "mount", "output": "out", "to": "driver_free", "input": "exec" },
    { "from": "driver_free", "output": "pass", "to": "mount_driver", "input": "exec" },
    { "from": "dismount", "output": "out", "to": "dismount_driver", "input": "exec" },
    { "from": "tick", "output": "out", "to": "drive", "input": "exec" },
    { "from": "tick", "output": "out", "to": "throttle_boost", "input": "exec" }
  ]
})";

const char *const kMaianGraph = R"({
  "schema": "pd.vehicle_graph.v1",
  "catalog_id": "mod:vehicle_maian_hover",
  "nodes": [
    { "id": "mount", "kind": "event.vehicle.mount_requested" },
    { "id": "dismount", "kind": "event.vehicle.dismount_requested" },
    { "id": "tick", "kind": "event.vehicle.tick" },
    { "id": "heat", "kind": "event.vehicle.heat_changed" },
    { "id": "driver_free", "kind": "gate.vehicle.seat_free", "params": { "seat": "driver" } },
    { "id": "mount_driver", "kind": "og.vehicle.mount_seat", "params": { "seat": "driver" } },
    { "id": "dismount_driver", "kind": "og.vehicle.dismount_seat", "params": { "seat": "driver" } },
    { "id": "drive", "kind": "og.vehicle.hover_drive" },
    { "id": "boost", "kind": "og.vehicle.boost_heat", "params": {
        "boost_speed": "$boost_speed", "ramp_time": "$boost_ramp",
        "overheat_after": "$overheat_after", "overheated_boost": "$overheated_boost",
        "steer_limit": "$steer_limit", "steer_limit_time": "$steer_limit_time",
        "recover_time_stopped": "$recover_time_stopped",
        "recover_speed_scale": "$recover_speed_scale" } },
    { "id": "crystals_black", "kind": "gate.vehicle.heat_phase",
      "params": { "phases": ["overheated", "recovering"] } },
    { "id": "hide_seafoam", "kind": "og.vehicle.part_visible",
      "params": { "part": "$cool_crystal_part", "visible": false } },
    { "id": "show_seafoam", "kind": "og.vehicle.part_visible",
      "params": { "part": "$cool_crystal_part", "visible": true } },
    { "id": "turret_blocked", "kind": "gate.vehicle.heat_phase",
      "params": { "phases": ["full", "overheated", "recovering"] } },
    { "id": "lock_turret", "kind": "og.vehicle.turret_lockout", "params": { "reason": "heat", "locked": true } },
    { "id": "unlock_turret", "kind": "og.vehicle.turret_lockout", "params": { "reason": "heat", "locked": false } }
  ],
  "edges": [
    { "from": "mount", "output": "out", "to": "driver_free", "input": "exec" },
    { "from": "driver_free", "output": "pass", "to": "mount_driver", "input": "exec" },
    { "from": "dismount", "output": "out", "to": "dismount_driver", "input": "exec" },
    { "from": "tick", "output": "out", "to": "drive", "input": "exec" },
    { "from": "tick", "output": "out", "to": "boost", "input": "exec" },
    { "from": "heat", "output": "out", "to": "crystals_black", "input": "exec" },
    { "from": "crystals_black", "output": "pass", "to": "hide_seafoam", "input": "exec" },
    { "from": "crystals_black", "output": "fail", "to": "show_seafoam", "input": "exec" },
    { "from": "heat", "output": "out", "to": "turret_blocked", "input": "exec" },
    { "from": "turret_blocked", "output": "pass", "to": "lock_turret", "input": "exec" },
    { "from": "turret_blocked", "output": "fail", "to": "unlock_turret", "input": "exec" }
  ]
})";

const char *const kMaianVariables = R"({
  "schema": "pd.vehicle_variables.v1",
  "boost_speed": 7.0,
  "boost_ramp": { "value": 60, "unit": "ticks60" },
  "overheat_after": { "value": 240, "unit": "ticks60" },
  "overheated_boost": 0.5,
  "steer_limit": 0.5,
  "steer_limit_time": { "value": 45, "unit": "ticks60" },
  "recover_time_stopped": { "value": 120, "unit": "ticks60" },
  "recover_speed_scale": 2.0,
  "cool_crystal_part": 140
})";

struct Program {
	vehicle_graph_program_t *program = nullptr;
	std::string error;

	Program(const char *graph, const char *variables = nullptr, const char *catalog_id = nullptr,
		vehicle_graph_has_part_fn has_part = nullptr, void *context = nullptr)
	{
		char err[512] = { 0 };
		program = vehicleGraphProgramCompile(graph, std::strlen(graph), variables,
			variables ? std::strlen(variables) : 0, catalog_id, has_part, context, err, sizeof(err));
		error = err;
	}
	~Program() { vehicleGraphProgramFree(program); }
	Program(const Program &) = delete;
	Program &operator=(const Program &) = delete;
};

struct Instance {
	vehicle_graph_instance_t *instance;
	explicit Instance(const vehicle_graph_program_t *program)
		: instance(vehicleGraphInstanceCreate(program)) {}
	~Instance() { vehicleGraphInstanceFree(instance); }
	Instance(const Instance &) = delete;
	Instance &operator=(const Instance &) = delete;
};

struct Host {
	bool free_seat[2] = { true, true };
	bool refuse_mount = false;
	std::vector<std::string> calls;

	static Host &of(void *context) { return *static_cast<Host *>(context); }
	static s32 seatFree(void *context, s32 seat) { return of(context).free_seat[seat] ? 1 : 0; }
	static s32 mount(void *context, s32 seat, const vehicle_graph_event_t *event)
	{
		of(context).calls.push_back("mount " + std::to_string(seat) + " player " +
			std::to_string(event->player));
		return of(context).refuse_mount ? 0 : 1;
	}
	static s32 dismount(void *context, s32 seat, const vehicle_graph_event_t *)
	{
		of(context).calls.push_back("dismount " + std::to_string(seat));
		return 1;
	}
	static s32 switchSeat(void *context, s32 seat, const vehicle_graph_event_t *event)
	{
		of(context).calls.push_back("switch to " + std::to_string(seat) + " player " +
			std::to_string(event->player));
		return 1;
	}
	static s32 part(void *context, s32 part, s32 visible)
	{
		of(context).calls.push_back("part " + std::to_string(part) + (visible ? " shown" : " hidden"));
		return 1;
	}
	vehicle_graph_host_t table()
	{
		vehicle_graph_host_t host{};
		host.context = this;
		host.seat_free = seatFree;
		host.mount_seat = mount;
		host.dismount_seat = dismount;
		host.switch_seat = switchSeat;
		host.set_part_visible = part;
		return host;
	}
};

/* A two-seat vehicle: each seat mounts when it is free, either seat
 * dismounts, and a rider may switch to the other seat when it is free. */
const char *const kTwoSeatGraph = R"({
  "schema": "pd.vehicle_graph.v1",
  "catalog_id": "mod:vehicle_two_seat",
  "nodes": [
    { "id": "mount", "kind": "event.vehicle.mount_requested" },
    { "id": "dismount", "kind": "event.vehicle.dismount_requested" },
    { "id": "switch", "kind": "event.vehicle.switch_seat_requested" },
    { "id": "tick", "kind": "event.vehicle.tick" },
    { "id": "mount_wants_driver", "kind": "gate.vehicle.seat", "params": { "seat": "driver" } },
    { "id": "driver_free", "kind": "gate.vehicle.seat_free", "params": { "seat": "driver" } },
    { "id": "gunner_free", "kind": "gate.vehicle.seat_free", "params": { "seat": "gunner" } },
    { "id": "mount_driver", "kind": "og.vehicle.mount_seat", "params": { "seat": "driver" } },
    { "id": "mount_gunner", "kind": "og.vehicle.mount_seat", "params": { "seat": "gunner" } },
    { "id": "leaves_driver", "kind": "gate.vehicle.seat", "params": { "seat": "driver" } },
    { "id": "dismount_driver", "kind": "og.vehicle.dismount_seat", "params": { "seat": "driver" } },
    { "id": "dismount_gunner", "kind": "og.vehicle.dismount_seat", "params": { "seat": "gunner" } },
    { "id": "switch_wants_gunner", "kind": "gate.vehicle.seat", "params": { "seat": "gunner" } },
    { "id": "gunner_free_for_switch", "kind": "gate.vehicle.seat_free", "params": { "seat": "gunner" } },
    { "id": "driver_free_for_switch", "kind": "gate.vehicle.seat_free", "params": { "seat": "driver" } },
    { "id": "switch_to_gunner", "kind": "og.vehicle.switch_seat", "params": { "seat": "gunner" } },
    { "id": "switch_to_driver", "kind": "og.vehicle.switch_seat", "params": { "seat": "driver" } },
    { "id": "drive", "kind": "og.vehicle.hover_drive" }
  ],
  "edges": [
    { "from": "mount", "output": "out", "to": "mount_wants_driver", "input": "exec" },
    { "from": "mount_wants_driver", "output": "pass", "to": "driver_free", "input": "exec" },
    { "from": "mount_wants_driver", "output": "fail", "to": "gunner_free", "input": "exec" },
    { "from": "driver_free", "output": "pass", "to": "mount_driver", "input": "exec" },
    { "from": "gunner_free", "output": "pass", "to": "mount_gunner", "input": "exec" },
    { "from": "dismount", "output": "out", "to": "leaves_driver", "input": "exec" },
    { "from": "leaves_driver", "output": "pass", "to": "dismount_driver", "input": "exec" },
    { "from": "leaves_driver", "output": "fail", "to": "dismount_gunner", "input": "exec" },
    { "from": "switch", "output": "out", "to": "switch_wants_gunner", "input": "exec" },
    { "from": "switch_wants_gunner", "output": "pass", "to": "gunner_free_for_switch", "input": "exec" },
    { "from": "switch_wants_gunner", "output": "fail", "to": "driver_free_for_switch", "input": "exec" },
    { "from": "gunner_free_for_switch", "output": "pass", "to": "switch_to_gunner", "input": "exec" },
    { "from": "driver_free_for_switch", "output": "pass", "to": "switch_to_driver", "input": "exec" },
    { "from": "tick", "output": "out", "to": "drive", "input": "exec" }
  ]
})";

vehicle_graph_event_t tick(float throttle, bool boost = false, float speed_frac = 0.0f,
	int ticks240 = 4)
{
	vehicle_graph_event_t event;
	std::memset(&event, 0, sizeof(event));
	event.kind = VEHICLE_GRAPH_EVENT_TICK;
	event.seat = VEHICLE_GRAPH_SEAT_NONE;
	event.player = -1;
	event.ticks240 = ticks240;
	event.ticks60 = ticks240 / 4.0f;
	event.throttle = throttle;
	event.boost_held = boost ? 1 : 0;
	event.speed_frac = speed_frac;
	return event;
}

vehicle_graph_event_t seatEvent(int kind, int seat, int player = 0)
{
	vehicle_graph_event_t event;
	std::memset(&event, 0, sizeof(event));
	event.kind = kind;
	event.seat = seat;
	event.player = player;
	return event;
}

/* Fire by value, so a test can pass a freshly made event. */
s32 fire(vehicle_graph_instance_t *instance, vehicle_graph_event_t event,
	const vehicle_graph_host_t *host)
{
	return vehicleGraphInstanceFire(instance, &event, host);
}

/* The original full-throttle bonus from hoverbikeUpdateMovement, NTSC. */
struct OriginalBonus {
	s32 maxspeedtime240 = 0;
	f32 step(f32 speedforwards, s32 lvupdate240, s32 boost_time, f32 boost_speed)
	{
		if (speedforwards >= 0.99f) {
			maxspeedtime240 += lvupdate240;
			if (maxspeedtime240 > boost_time) maxspeedtime240 = boost_time;
		} else if (maxspeedtime240 > 0) {
			if (speedforwards >= 0.8f) {
				// empty
			} else if (speedforwards >= -0.1f) {
				maxspeedtime240 -= lvupdate240;
				if (maxspeedtime240 < 0) maxspeedtime240 = 0;
			} else {
				maxspeedtime240 = 0;
			}
		}
		return maxspeedtime240 * boost_speed / boost_time;
	}
};

std::string withEdits(const char *graph, const std::string &from, const std::string &to)
{
	std::string text(graph);
	const size_t at = text.find(from);
	REQUIRE(at != std::string::npos);
	text.replace(at, from.size(), to);
	return text;
}

std::string refusal(const std::string &graph, const char *variables = nullptr,
	const char *catalog_id = nullptr)
{
	Program p(graph.c_str(), variables, catalog_id);
	CHECK(p.program == nullptr);
	return p.error;
}

} // namespace

TEST_CASE("The base hoverbike graph boosts exactly like the original full-throttle bonus",
	"[vehicle-graph]")
{
	Program p(kBaseGraph, nullptr, "base:vehicle_hoverbike");
	REQUIRE(p.program != nullptr);
	CHECK(p.error.empty());
	CHECK(vehicleGraphProgramNodeCount(p.program) == 8);
	CHECK(vehicleGraphProgramExecEdgeCount(p.program) == 5);
	CHECK(vehicleGraphProgramPendingBound(p.program) == 0);
	CHECK(vehicleGraphProgramBoostHeatParams(p.program) == nullptr);
	Instance v(p.program);
	REQUIRE(v.instance != nullptr);

	vehicle_graph_drive_t drive;
	vehicleGraphInstanceDrive(v.instance, &drive);
	CHECK(drive.hover == 0); // nothing before the first tick

	// A throttle sequence through every branch: full, hold, decay, reset,
	// with uneven updates (lvupdate240 3 to 6).
	OriginalBonus original;
	Host host;
	vehicle_graph_host_t table = host.table();
	int checked = 0;
	for (int i = 0; i < 2000; i++) {
		f32 throttle;
		if (i < 700) throttle = 1.0f;          // ramps past the 10 s cap
		else if (i < 800) throttle = 0.85f;    // holds
		else if (i < 900) throttle = 0.3f;     // decays
		else if (i < 1500) throttle = 0.995f;  // ramps again
		else if (i < 1510) throttle = -0.5f;   // resets at once
		else throttle = (i % 7 < 4) ? 1.0f : 0.0f;
		const s32 ticks = 3 + i % 4;
		const f32 expected = original.step(throttle, ticks, 2400, 5.0f);
		REQUIRE(fire(v.instance, tick(throttle, false, 0.0f, ticks), &table) == 2);
		vehicleGraphInstanceDrive(v.instance, &drive);
		REQUIRE(drive.hover == 1);
		REQUIRE(drive.drive == 1);
		REQUIRE(drive.steer_scale == 1.0f);
		REQUIRE(drive.extra_speed == expected); // bit for bit
		checked++;
	}
	CHECK(checked == 2000);
	CHECK(host.calls.empty());

	// The full bonus is 5.0 after 2400 ticks (10 s).
	OriginalBonus full;
	Instance w(p.program);
	for (int i = 0; i < 600; i++) {
		REQUIRE(fire(w.instance, tick(1.0f), &table) == 2);
		full.step(1.0f, 4, 2400, 5.0f);
	}
	vehicleGraphInstanceDrive(w.instance, &drive);
	CHECK(drive.extra_speed == 5.0f);

	// The original rest reset zeroes the timer.
	vehicleGraphInstanceRest(w.instance);
	REQUIRE(fire(w.instance, tick(0.85f), &table) == 2);
	vehicleGraphInstanceDrive(w.instance, &drive);
	CHECK(drive.extra_speed == 0.0f);
}

TEST_CASE("A drive that ignores the driver's inputs gives no throttle bonus", "[vehicle-graph]")
{
	// CORE's review, 2026-11-16: with hover_drive {drive: false} the game
	// zeroes the inputs, so the bonus must not creep forward on the throttle.
	Program normal(kBaseGraph);
	Program parked(withEdits(kBaseGraph, "{ \"id\": \"drive\", \"kind\": \"og.vehicle.hover_drive\" }",
		"{ \"id\": \"drive\", \"kind\": \"og.vehicle.hover_drive\", \"params\": { \"drive\": false } }").c_str());
	REQUIRE(normal.program != nullptr);
	INFO(parked.error);
	REQUIRE(parked.program != nullptr);
	Instance a(normal.program);
	Instance b(parked.program);
	for (int i = 0; i < 100; i++) {
		REQUIRE(fire(a.instance, tick(1.0f), nullptr) >= 0);
		REQUIRE(fire(b.instance, tick(1.0f), nullptr) >= 0);
	}
	vehicle_graph_drive_t da{}, db{};
	vehicleGraphInstanceDrive(a.instance, &da);
	vehicleGraphInstanceDrive(b.instance, &db);
	CHECK(da.drive == 1);
	CHECK(da.extra_speed > 0.0f);
	CHECK(db.hover == 1);
	CHECK(db.drive == 0);
	CHECK(db.extra_speed == 0.0f);
}

TEST_CASE("Mounting and dismounting go through the seat gate and the host", "[vehicle-graph]")
{
	Program p(kBaseGraph);
	REQUIRE(p.program != nullptr);
	CHECK(vehicleGraphProgramEventReachesModule(p.program, VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED,
		"og.vehicle.mount_seat") == 1);
	CHECK(vehicleGraphProgramEventReachesModule(p.program, VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED,
		"og.vehicle.hover_drive") == 0);
	CHECK(vehicleGraphProgramEventReachesModule(p.program, VEHICLE_GRAPH_EVENT_DISMOUNT_REQUESTED,
		"og.vehicle.dismount_seat") == 1);
	Instance v(p.program);
	Host host;
	vehicle_graph_host_t table = host.table();

	CHECK(fire(v.instance,
		seatEvent(VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED, VEHICLE_GRAPH_SEAT_DRIVER, 2), &table) == 1);
	REQUIRE(host.calls.size() == 1);
	CHECK(host.calls[0] == "mount 0 player 2");

	host.free_seat[VEHICLE_GRAPH_SEAT_DRIVER] = false;
	CHECK(fire(v.instance,
		seatEvent(VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED, VEHICLE_GRAPH_SEAT_DRIVER), &table) == 0);
	CHECK(host.calls.size() == 1); // the gate failed, nothing mounted

	host.free_seat[VEHICLE_GRAPH_SEAT_DRIVER] = true;
	host.refuse_mount = true;
	CHECK(fire(v.instance,
		seatEvent(VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED, VEHICLE_GRAPH_SEAT_DRIVER), &table) == -1);

	CHECK(fire(v.instance,
		seatEvent(VEHICLE_GRAPH_EVENT_DISMOUNT_REQUESTED, VEHICLE_GRAPH_SEAT_DRIVER), &table) == 1);
	CHECK(host.calls.back() == "dismount 0");

	// No host: the gate fails and nothing runs.
	CHECK(fire(v.instance,
		seatEvent(VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED, VEHICLE_GRAPH_SEAT_DRIVER), nullptr) == 0);

	// Malformed and raised-only events are refused.
	CHECK(fire(v.instance,
		seatEvent(VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED, 5), &table) == -1);
	vehicle_graph_event_t heat = seatEvent(VEHICLE_GRAPH_EVENT_HEAT_CHANGED, VEHICLE_GRAPH_SEAT_NONE);
	CHECK(fire(v.instance, heat, &table) == -1);
	vehicle_graph_event_t bad = tick(1.0f);
	bad.ticks240 = -1;
	CHECK(fire(v.instance, bad, &table) == -1);
}

TEST_CASE("A two-seat graph mounts, dismounts and switches seats through its gates", "[vehicle-graph]")
{
	Program p(kTwoSeatGraph);
	INFO(p.error);
	REQUIRE(p.program != nullptr);
	const u32 driver = 1u << VEHICLE_GRAPH_SEAT_DRIVER;
	const u32 gunner = 1u << VEHICLE_GRAPH_SEAT_GUNNER;

	// What the game asks before it fires: which seats each request can act on.
	CHECK(vehicleGraphProgramEventSeats(p.program, VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED) == (driver | gunner));
	CHECK(vehicleGraphProgramEventSeats(p.program, VEHICLE_GRAPH_EVENT_DISMOUNT_REQUESTED) == (driver | gunner));
	CHECK(vehicleGraphProgramEventSeats(p.program, VEHICLE_GRAPH_EVENT_SWITCH_SEAT_REQUESTED) == (driver | gunner));
	CHECK(vehicleGraphProgramEventSeats(p.program, VEHICLE_GRAPH_EVENT_TICK) == 0);
	CHECK(vehicleGraphProgramEventSeats(p.program, VEHICLE_GRAPH_EVENT_HEAT_CHANGED) == 0);
	CHECK(vehicleGraphProgramEventSeats(p.program, 0) == 0);
	CHECK(vehicleGraphProgramEventSeats(p.program, VEHICLE_GRAPH_EVENT_KIND_END) == 0);
	CHECK(vehicleGraphProgramEventSeats(nullptr, VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED) == 0);
	CHECK(vehicleGraphProgramEventReachesModule(p.program, VEHICLE_GRAPH_EVENT_SWITCH_SEAT_REQUESTED,
		"og.vehicle.switch_seat") == 1);
	CHECK(vehicleGraphProgramEventReachesModule(p.program, VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED,
		"og.vehicle.switch_seat") == 0);
	// A one-seat vehicle has no gunner seat and nothing to switch to.
	{
		Program base(kBaseGraph);
		REQUIRE(base.program != nullptr);
		CHECK(vehicleGraphProgramEventSeats(base.program, VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED) == driver);
		CHECK(vehicleGraphProgramEventSeats(base.program, VEHICLE_GRAPH_EVENT_DISMOUNT_REQUESTED) == driver);
		CHECK(vehicleGraphProgramEventSeats(base.program, VEHICLE_GRAPH_EVENT_SWITCH_SEAT_REQUESTED) == 0);
		Instance one(base.program);
		Host host;
		vehicle_graph_host_t table = host.table();
		CHECK(fire(one.instance, seatEvent(VEHICLE_GRAPH_EVENT_SWITCH_SEAT_REQUESTED,
			VEHICLE_GRAPH_SEAT_GUNNER), &table) == 0);
		CHECK(host.calls.empty());
	}

	Instance v(p.program);
	Host host;
	vehicle_graph_host_t table = host.table();

	// Each mount takes the seat it asks for.
	CHECK(fire(v.instance,
		seatEvent(VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED, VEHICLE_GRAPH_SEAT_GUNNER, 1), &table) == 1);
	CHECK(fire(v.instance,
		seatEvent(VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED, VEHICLE_GRAPH_SEAT_DRIVER, 0), &table) == 1);
	REQUIRE(host.calls.size() == 2);
	CHECK(host.calls[0] == "mount 1 player 1");
	CHECK(host.calls[1] == "mount 0 player 0");

	// A taken seat refuses its mount; the other seat is not offered instead.
	host.free_seat[VEHICLE_GRAPH_SEAT_GUNNER] = false;
	CHECK(fire(v.instance,
		seatEvent(VEHICLE_GRAPH_EVENT_MOUNT_REQUESTED, VEHICLE_GRAPH_SEAT_GUNNER, 2), &table) == 0);
	CHECK(host.calls.size() == 2);

	// A dismount leaves the seat it names.
	CHECK(fire(v.instance,
		seatEvent(VEHICLE_GRAPH_EVENT_DISMOUNT_REQUESTED, VEHICLE_GRAPH_SEAT_GUNNER, 1), &table) == 1);
	CHECK(host.calls.back() == "dismount 1");
	CHECK(fire(v.instance,
		seatEvent(VEHICLE_GRAPH_EVENT_DISMOUNT_REQUESTED, VEHICLE_GRAPH_SEAT_DRIVER, 0), &table) == 1);
	CHECK(host.calls.back() == "dismount 0");
	REQUIRE(host.calls.size() == 4);

	// A switch moves the rider to the seat asked for, once, when it is free.
	host.free_seat[VEHICLE_GRAPH_SEAT_GUNNER] = true;
	CHECK(fire(v.instance,
		seatEvent(VEHICLE_GRAPH_EVENT_SWITCH_SEAT_REQUESTED, VEHICLE_GRAPH_SEAT_GUNNER, 3), &table) == 1);
	REQUIRE(host.calls.size() == 5);
	CHECK(host.calls.back() == "switch to 1 player 3");
	CHECK(fire(v.instance,
		seatEvent(VEHICLE_GRAPH_EVENT_SWITCH_SEAT_REQUESTED, VEHICLE_GRAPH_SEAT_DRIVER, 3), &table) == 1);
	REQUIRE(host.calls.size() == 6);
	CHECK(host.calls.back() == "switch to 0 player 3");

	// With someone in the other seat the switch does nothing.
	host.free_seat[VEHICLE_GRAPH_SEAT_GUNNER] = false;
	CHECK(fire(v.instance,
		seatEvent(VEHICLE_GRAPH_EVENT_SWITCH_SEAT_REQUESTED, VEHICLE_GRAPH_SEAT_GUNNER, 3), &table) == 0);
	CHECK(host.calls.size() == 6);

	// A host that cannot switch refuses the module; a switch names a seat.
	host.free_seat[VEHICLE_GRAPH_SEAT_GUNNER] = true;
	vehicle_graph_host_t noSwitch = host.table();
	noSwitch.switch_seat = nullptr;
	CHECK(fire(v.instance,
		seatEvent(VEHICLE_GRAPH_EVENT_SWITCH_SEAT_REQUESTED, VEHICLE_GRAPH_SEAT_GUNNER, 3), &noSwitch) == -1);
	CHECK(fire(v.instance,
		seatEvent(VEHICLE_GRAPH_EVENT_SWITCH_SEAT_REQUESTED, VEHICLE_GRAPH_SEAT_NONE, 3), &table) == -1);
	CHECK(fire(v.instance, seatEvent(VEHICLE_GRAPH_EVENT_SWITCH_SEAT_REQUESTED, 2, 3), &table) == -1);
	CHECK(host.calls.size() == 6);
}

TEST_CASE("Seat modules run only on their own request, once per seat", "[vehicle-graph]")
{
	// A switch on the mount request.
	CHECK(refusal(withEdits(kTwoSeatGraph,
		"{ \"from\": \"gunner_free\", \"output\": \"pass\", \"to\": \"mount_gunner\", \"input\": \"exec\" }",
		"{ \"from\": \"gunner_free\", \"output\": \"pass\", \"to\": \"switch_to_gunner\", \"input\": \"exec\" }"))
		.find("og.vehicle.switch_seat 'switch_to_gunner' cannot run on event.vehicle.mount_requested") !=
		std::string::npos);
	// A mount on the switch request.
	CHECK(refusal(withEdits(kTwoSeatGraph,
		"{ \"from\": \"gunner_free_for_switch\", \"output\": \"pass\", \"to\": \"switch_to_gunner\", \"input\": \"exec\" }",
		"{ \"from\": \"gunner_free_for_switch\", \"output\": \"pass\", \"to\": \"mount_gunner\", \"input\": \"exec\" }"))
		.find("og.vehicle.mount_seat 'mount_gunner' cannot run on event.vehicle.switch_seat_requested") !=
		std::string::npos);
	// A tick module on the switch request.
	CHECK(refusal(withEdits(kTwoSeatGraph,
		"{ \"from\": \"driver_free_for_switch\", \"output\": \"pass\", \"to\": \"switch_to_driver\", \"input\": \"exec\" }",
		"{ \"from\": \"driver_free_for_switch\", \"output\": \"pass\", \"to\": \"drive\", \"input\": \"exec\" }"))
		.find("og.vehicle.hover_drive 'drive' cannot run on event.vehicle.switch_seat_requested") !=
		std::string::npos);
	// Two switches to one seat on one request, even on a gate's two branches.
	const std::string switchEdge =
		"{ \"from\": \"driver_free_for_switch\", \"output\": \"pass\", \"to\": \"switch_to_driver\", \"input\": \"exec\" }";
	const std::string secondSwitch = withEdits(withEdits(kTwoSeatGraph,
		"{ \"id\": \"drive\", \"kind\": \"og.vehicle.hover_drive\" }",
		"{ \"id\": \"drive\", \"kind\": \"og.vehicle.hover_drive\" },\n"
		"    { \"id\": \"switch_again\", \"kind\": \"og.vehicle.switch_seat\", \"params\": { \"seat\": \"driver\" } }").c_str(),
		switchEdge, switchEdge + ",\n"
		"    { \"from\": \"driver_free_for_switch\", \"output\": \"fail\", \"to\": \"switch_again\", \"input\": \"exec\" }");
	CHECK(refusal(secondSwitch)
		.find("event.vehicle.switch_seat_requested reaches a second og.vehicle.switch_seat for the driver seat") !=
		std::string::npos);
	// A switch names a seat.
	CHECK(refusal(withEdits(kTwoSeatGraph,
		"\"kind\": \"og.vehicle.switch_seat\", \"params\": { \"seat\": \"gunner\" }",
		"\"kind\": \"og.vehicle.switch_seat\", \"params\": { \"seat\": \"turret\" }"))
		.find("must be \"driver\" or \"gunner\"") != std::string::npos);
	CHECK(refusal(withEdits(kTwoSeatGraph,
		"\"kind\": \"og.vehicle.switch_seat\", \"params\": { \"seat\": \"gunner\" }",
		"\"kind\": \"og.vehicle.switch_seat\""))
		.find("param 'seat' is missing") != std::string::npos);
	// A part or a lockout may run on a switch, as on any event.
	{
		const std::string withPart = withEdits(withEdits(kTwoSeatGraph,
			"{ \"id\": \"drive\", \"kind\": \"og.vehicle.hover_drive\" }",
			"{ \"id\": \"drive\", \"kind\": \"og.vehicle.hover_drive\" },\n"
			"    { \"id\": \"lock\", \"kind\": \"og.vehicle.turret_lockout\", \"params\": { \"reason\": \"switching\", \"locked\": true } }").c_str(),
			switchEdge, switchEdge + ",\n"
			"    { \"from\": \"switch_to_driver\", \"output\": \"out\", \"to\": \"lock\", \"input\": \"exec\" }");
		Program p(withPart.c_str());
		INFO(p.error);
		REQUIRE(p.program != nullptr);
		Instance v(p.program);
		Host host;
		vehicle_graph_host_t table = host.table();
		CHECK(vehicleGraphInstanceTurretLockMask(v.instance) == 0);
		CHECK(fire(v.instance,
			seatEvent(VEHICLE_GRAPH_EVENT_SWITCH_SEAT_REQUESTED, VEHICLE_GRAPH_SEAT_DRIVER, 0), &table) == 2);
		CHECK(vehicleGraphInstanceTurretLockMask(v.instance) == 1u);
	}
}

TEST_CASE("The Maian graph turns the crystals black and locks the turret at the heat phases",
	"[vehicle-graph]")
{
	Program p(kMaianGraph, kMaianVariables, "mod:vehicle_maian_hover");
	REQUIRE(p.program != nullptr);
	CHECK(p.error.empty());
	CHECK(vehicleGraphProgramPendingBound(p.program) == 1);
	CHECK(vehicleGraphProgramLockoutReason(p.program, "heat") == 0);
	CHECK(vehicleGraphProgramLockoutReason(p.program, "aim") == -1);
	CHECK(vehicleGraphProgramEventReachesModule(p.program, VEHICLE_GRAPH_EVENT_TICK,
		"og.vehicle.part_visible") == 1); // through heat_changed
	CHECK(vehicleGraphProgramEventReachesModule(p.program, VEHICLE_GRAPH_EVENT_TICK,
		"og.vehicle.throttle_boost") == 0);

	// The variables resolve to the native params.
	const vehicle_boost_heat_params_t *params = vehicleGraphProgramBoostHeatParams(p.program);
	REQUIRE(params != nullptr);
	CHECK(params->boost_speed == 7.0f);
	CHECK(params->ramp_ticks240 == 240.0f);
	CHECK(params->overheat_ticks240 == 960.0f);
	CHECK(params->overheated_boost == 0.5f);
	CHECK(params->steer_limit == 0.5f);
	CHECK(params->steer_limit_ticks240 == 180.0f);
	CHECK(params->recover_ticks240 == 480.0f);
	CHECK(params->recover_speed_scale == 2.0f);

	Instance v(p.program);
	Host host;
	vehicle_graph_host_t table = host.table();
	CHECK(vehicleGraphInstanceHeatPhase(v.instance) == VEHICLE_BOOST_HEAT_COOL);

	// The same steps run straight through the state machine for comparison.
	vehicle_boost_heat_state_t reference;
	vehicleBoostHeatReset(&reference);
	std::vector<std::string> phases;
	std::vector<u32> locks;
	auto frame = [&](bool held, f32 speed_frac) {
		s32 prev = 0;
		vehicleBoostHeatStep(&reference, params, held, speed_frac, 4.0f, &prev);
		const s32 before = vehicleGraphInstanceHeatPhase(v.instance);
		const s32 ran = fire(v.instance, tick(0.0f, held, speed_frac), &table);
		REQUIRE(ran >= 2);
		vehicle_graph_drive_t drive;
		vehicleGraphInstanceDrive(v.instance, &drive);
		REQUIRE(drive.hover == 1);
		REQUIRE(drive.extra_speed == vehicleBoostHeatExtraSpeed(&reference, params));
		REQUIRE(drive.steer_scale == vehicleBoostHeatSteerScale(&reference, params));
		const s32 after = vehicleGraphInstanceHeatPhase(v.instance);
		REQUIRE(after == reference.phase);
		if (after != before) {
			phases.push_back(vehicleBoostHeatPhaseName(after));
			locks.push_back(vehicleGraphInstanceTurretLockMask(v.instance));
		}
	};
	for (int i = 0; i < 300; i++) frame(true, 1.0f);   // boost until black
	for (int i = 0; i < 400; i++) frame(false, 0.0f);  // let go, standing still

	const std::vector<std::string> expected_phases = {
		"ramping", "full", "overheated", "recovering", "cool" };
	CHECK(phases == expected_phases);
	const std::vector<u32> expected_locks = { 0u, 1u, 1u, 1u, 0u };
	CHECK(locks == expected_locks);
	const std::vector<std::string> expected_calls = {
		"part 140 shown",   // ramping: not black
		"part 140 shown",   // full
		"part 140 hidden",  // overheated: black
		"part 140 hidden",  // recovering: still black
		"part 140 shown" }; // cool again
	CHECK(host.calls == expected_calls);

	// A second vehicle keeps its own state.
	Instance other(p.program);
	CHECK(vehicleGraphInstanceHeatPhase(other.instance) == VEHICLE_BOOST_HEAT_COOL);
	CHECK(vehicleGraphInstanceTurretLockMask(other.instance) == 0u);
}

TEST_CASE("Graph params take variables whole and refuse unknown names and wrong types",
	"[vehicle-graph]")
{
	// A literal part and a variable part compile the same.
	Program literal(withEdits(kMaianGraph, "\"part\": \"$cool_crystal_part\", \"visible\": false",
		"\"part\": 140, \"visible\": false").c_str(), kMaianVariables);
	CHECK(literal.program != nullptr);

	CHECK(refusal(kMaianGraph).find("unknown variable '$boost_speed'") != std::string::npos);
	CHECK(refusal(kMaianGraph, R"({ "schema": "pd.vehicle_variables.v1", "boost_speed": 7.0 })")
		.find("uses unknown variable '$boost_ramp'") != std::string::npos);

	std::string vars(kMaianVariables);
	CHECK(refusal(kMaianGraph, withEdits(vars.c_str(), "\"boost_speed\": 7.0", "\"boost_speed\": \"fast\"").c_str())
		.find("param 'boost_speed' must be a number") != std::string::npos);
	CHECK(refusal(kMaianGraph, withEdits(vars.c_str(), "{ \"value\": 60, \"unit\": \"ticks60\" }", "60").c_str())
		.find("param 'ramp_time' must be {") != std::string::npos);
	CHECK(refusal(kMaianGraph, withEdits(vars.c_str(), "\"unit\": \"ticks60\" }", "\"unit\": \"frames\" }").c_str())
		.find("unknown unit") != std::string::npos);
	CHECK(refusal(kMaianGraph, withEdits(vars.c_str(), "\"overheated_boost\": 0.5", "\"overheated_boost\": 1.5").c_str())
		.find("param 'overheated_boost' must be from 0 to 1") != std::string::npos);
	CHECK(refusal(kMaianGraph, withEdits(vars.c_str(), "\"cool_crystal_part\": 140", "\"cool_crystal_part\": 140.5").c_str())
		.find("param 'part' must be a whole number") != std::string::npos);
	CHECK(refusal(kMaianGraph, withEdits(vars.c_str(), "\"pd.vehicle_variables.v1\"", "\"pd.vehicle_variables.v2\"").c_str())
		.find("needs \"schema\": \"pd.vehicle_variables.v1\"") != std::string::npos);
	CHECK(refusal(kMaianGraph, withEdits(vars.c_str(), "\"steer_limit\": 0.5", "\"Steer\": 0.5").c_str())
		.find("variable name 'Steer'") != std::string::npos);
	CHECK(refusal(kMaianGraph, withEdits(vars.c_str(), "\"schema\"", "\"catalog_id\": \"mod:other\", \"schema\"").c_str(),
		"mod:vehicle_maian_hover").find("is for 'mod:other'") != std::string::npos);
}

TEST_CASE("Broken vehicle graphs are refused when they compile", "[vehicle-graph]")
{
	CHECK(refusal(withEdits(kBaseGraph, "pd.vehicle_graph.v1", "pd2.vehicle.behavior.v2"))
		.find("needs \"schema\": \"pd.vehicle_graph.v1\"") != std::string::npos);
	CHECK(refusal(kBaseGraph, nullptr, "mod:vehicle_other").find("is for 'base:vehicle_hoverbike'") !=
		std::string::npos);
	CHECK(refusal(withEdits(kBaseGraph, "\"catalog_id\": \"base:vehicle_hoverbike\",", ""))
		.find("needs its vehicle's catalog_id") != std::string::npos);
	CHECK(refusal(withEdits(kBaseGraph, "og.vehicle.hover_drive", "og.vehicle.fly"))
		.find("native module 'og.vehicle.fly' does not exist") != std::string::npos);
	CHECK(refusal(withEdits(kBaseGraph, "event.vehicle.tick", "event.vehicle.honk"))
		.find("is not a vehicle event") != std::string::npos);
	CHECK(refusal(withEdits(kBaseGraph, "gate.vehicle.seat_free", "gate.vehicle.seat_empty"))
		.find("is not executable") != std::string::npos);
	CHECK(refusal(withEdits(kBaseGraph, "\"params\": { \"seat\": \"driver\" } },\n    { \"id\": \"mount_driver\"",
		"\"params\": { \"seat\": \"pilot\" } },\n    { \"id\": \"mount_driver\""))
		.find("must be \"driver\" or \"gunner\"") != std::string::npos);
	CHECK(refusal(withEdits(kBaseGraph, "\"boost_speed\": 5.0,", "\"boost_speed\": 5.0, \"turbo\": true,"))
		.find("has no param 'turbo'") != std::string::npos);
	CHECK(refusal(withEdits(kBaseGraph, "\"boost_speed\": 5.0, ", ""))
		.find("param 'boost_speed' is missing") != std::string::npos);
	CHECK(refusal(withEdits(kBaseGraph, "{ \"value\": 2400, \"unit\": \"ticks240\" }", "2400"))
		.find("param 'ramp_time' must be {") != std::string::npos);
	CHECK(refusal(withEdits(kBaseGraph, "{ \"value\": 2400, \"unit\": \"ticks240\" }",
		"{ \"value\": 0, \"unit\": \"ticks240\" }")).find("must be longer than 0") != std::string::npos);
	CHECK(refusal(withEdits(kBaseGraph, "{ \"id\": \"drive\",", "{ \"id\": \"tick\","))
		.find("node id 'tick' is used twice") != std::string::npos);
	CHECK(refusal(withEdits(kBaseGraph, "\"to\": \"driver_free\"", "\"to\": \"tick\""))
		.find("which is not a gate or a native module") != std::string::npos);
	CHECK(refusal(withEdits(kBaseGraph, "\"output\": \"pass\"", "\"output\": \"out\""))
		.find("needs output \"pass\" or \"fail\"") != std::string::npos);
	CHECK(refusal(withEdits(kBaseGraph, "{ \"from\": \"tick\", \"output\": \"out\", \"to\": \"drive\"",
		"{ \"from\": \"tick\", \"output\": \"pass\", \"to\": \"drive\""))
		.find("output must be \"out\"") != std::string::npos);
	CHECK(refusal(withEdits(kBaseGraph, "\"to\": \"drive\", \"input\": \"exec\"", "\"to\": \"drive\""))
		.find("has an output but is not an execution edge") != std::string::npos);
	CHECK(refusal(withEdits(kBaseGraph, "\"to\": \"dismount_driver\"", "\"to\": \"ghost\""))
		.find("names a missing node ('ghost')") != std::string::npos);
	// A tick-only module on the mount event.
	CHECK(refusal(withEdits(kBaseGraph, "\"from\": \"driver_free\", \"output\": \"pass\", \"to\": \"mount_driver\"",
		"\"from\": \"driver_free\", \"output\": \"pass\", \"to\": \"drive\""))
		.find("og.vehicle.hover_drive 'drive' cannot run on event.vehicle.mount_requested") != std::string::npos);
	// A mount on the tick.
	CHECK(refusal(withEdits(kBaseGraph, "\"from\": \"tick\", \"output\": \"out\", \"to\": \"drive\"",
		"\"from\": \"tick\", \"output\": \"out\", \"to\": \"mount_driver\""))
		.find("cannot run on event.vehicle.tick") != std::string::npos);
	// One drive and one heat per vehicle.
	CHECK(refusal(withEdits(kBaseGraph, "{ \"id\": \"drive\", \"kind\": \"og.vehicle.hover_drive\" }",
		"{ \"id\": \"drive\", \"kind\": \"og.vehicle.hover_drive\" },\n    { \"id\": \"drive2\", \"kind\": \"og.vehicle.hover_drive\" }"))
		.find("has one og.vehicle.hover_drive") != std::string::npos);
	CHECK(refusal(withEdits(kMaianGraph, "{ \"id\": \"crystals_black\"",
		"{ \"id\": \"boost2\", \"kind\": \"og.vehicle.boost_heat\", \"params\": { \"boost_speed\": 1, "
		"\"ramp_time\": { \"value\": 1, \"unit\": \"ticks60\" }, \"overheat_after\": { \"value\": 1, \"unit\": \"ticks60\" }, "
		"\"overheated_boost\": 0, \"steer_limit\": 1, \"steer_limit_time\": { \"value\": 0, \"unit\": \"ticks60\" }, "
		"\"recover_time_stopped\": { \"value\": 1, \"unit\": \"ticks60\" }, \"recover_speed_scale\": 1 } },\n    { \"id\": \"crystals_black\""),
		kMaianVariables).find("has one og.vehicle.boost_heat") != std::string::npos);
	CHECK(refusal(withEdits(kMaianGraph, "[\"overheated\", \"recovering\"]", "[\"overheated\", \"black\"]"),
		kMaianVariables).find("may name only cool") != std::string::npos);
	CHECK(refusal(withEdits(kMaianGraph, "[\"overheated\", \"recovering\"]", "[\"overheated\", \"overheated\"]"),
		kMaianVariables).find("names a phase twice") != std::string::npos);
	CHECK(refusal(withEdits(kMaianGraph, "\"reason\": \"heat\", \"locked\": true", "\"reason\": \"Heat!\", \"locked\": true"),
		kMaianVariables).find("param 'reason' must be 1 to 31") != std::string::npos);
	// One event mounts or dismounts a seat once (CORE's review, 2026-11-16: a
	// second dismount on the host would act on a rider who has already gone).
	const std::string secondDismount = withEdits(kBaseGraph,
		"{ \"id\": \"drive\", \"kind\": \"og.vehicle.hover_drive\" }",
		"{ \"id\": \"drive\", \"kind\": \"og.vehicle.hover_drive\" },\n"
		"    { \"id\": \"dismount_again\", \"kind\": \"og.vehicle.dismount_seat\", \"params\": { \"seat\": \"driver\" } }");
	const std::string dismountEdge =
		"{ \"from\": \"dismount\", \"output\": \"out\", \"to\": \"dismount_driver\", \"input\": \"exec\" }";
	CHECK(refusal(withEdits(secondDismount.c_str(), dismountEdge, dismountEdge + ",\n"
		"    { \"from\": \"dismount\", \"output\": \"out\", \"to\": \"dismount_again\", \"input\": \"exec\" }"))
		.find("event.vehicle.dismount_requested reaches a second og.vehicle.dismount_seat for the driver seat") !=
		std::string::npos);
	// Even on the two branches of a gate.
	const std::string mountEdge =
		"{ \"from\": \"driver_free\", \"output\": \"pass\", \"to\": \"mount_driver\", \"input\": \"exec\" }";
	const std::string secondMount = withEdits(withEdits(kBaseGraph,
		"{ \"id\": \"drive\", \"kind\": \"og.vehicle.hover_drive\" }",
		"{ \"id\": \"drive\", \"kind\": \"og.vehicle.hover_drive\" },\n"
		"    { \"id\": \"mount_again\", \"kind\": \"og.vehicle.mount_seat\", \"params\": { \"seat\": \"driver\" } }").c_str(),
		mountEdge, mountEdge + ",\n"
		"    { \"from\": \"driver_free\", \"output\": \"fail\", \"to\": \"mount_again\", \"input\": \"exec\" }");
	CHECK(refusal(secondMount)
		.find("event.vehicle.mount_requested reaches a second og.vehicle.mount_seat for the driver seat") !=
		std::string::npos);
	// One for each seat is fine.
	{
		const std::string bothSeats = withEdits(withEdits(kBaseGraph,
			"{ \"id\": \"drive\", \"kind\": \"og.vehicle.hover_drive\" }",
			"{ \"id\": \"drive\", \"kind\": \"og.vehicle.hover_drive\" },\n"
			"    { \"id\": \"dismount_gunner\", \"kind\": \"og.vehicle.dismount_seat\", \"params\": { \"seat\": \"gunner\" } }").c_str(),
			dismountEdge, dismountEdge + ",\n"
			"    { \"from\": \"dismount\", \"output\": \"out\", \"to\": \"dismount_gunner\", \"input\": \"exec\" }");
		Program both(bothSeats.c_str());
		INFO(both.error);
		CHECK(both.program != nullptr);
	}
	CHECK(refusal("[1, 2]").find("is not one JSON object") != std::string::npos);
	CHECK(refusal("{ \"schema\": \"pd.vehicle_graph.v1\", \"catalog_id\": \"x:y\" }")
		.find("has no nodes array") != std::string::npos);
}

TEST_CASE("Comments on a graph, its nodes, its edges and its variables change nothing",
          "[vehicle-graph]")
{
	// The same Maian graph with a comment everywhere one may go.
	std::string commented = withEdits(kMaianGraph, "\"schema\": \"pd.vehicle_graph.v1\",",
		"\"schema\": \"pd.vehicle_graph.v1\", \"comment\": \"what this vehicle does\",");
	commented = withEdits(commented.c_str(), "{ \"id\": \"tick\", \"kind\": \"event.vehicle.tick\" }",
		"{ \"id\": \"tick\", \"kind\": \"event.vehicle.tick\", \"comment\": \"once per update\" }");
	commented = withEdits(commented.c_str(), "{ \"from\": \"tick\", \"output\": \"out\", \"to\": \"drive\", \"input\": \"exec\" }",
		"{ \"from\": \"tick\", \"output\": \"out\", \"to\": \"drive\", \"input\": \"exec\", \"comment\": \"first\" }");
	const std::string variables = withEdits(kMaianVariables, "\"schema\": \"pd.vehicle_variables.v1\",",
		"\"schema\": \"pd.vehicle_variables.v1\", \"comment\": \"the numbers a modder tunes\",");
	Program plain(kMaianGraph, kMaianVariables);
	Program with(commented.c_str(), variables.c_str());
	REQUIRE(plain.program != nullptr);
	INFO(with.error);
	REQUIRE(with.program != nullptr);
	CHECK(vehicleGraphProgramNodeCount(with.program) == vehicleGraphProgramNodeCount(plain.program));
	CHECK(vehicleGraphProgramExecEdgeCount(with.program) ==
		vehicleGraphProgramExecEdgeCount(plain.program));
	CHECK(vehicleGraphProgramPendingBound(with.program) ==
		vehicleGraphProgramPendingBound(plain.program));
	CHECK(vehicleGraphProgramPartCount(with.program) == vehicleGraphProgramPartCount(plain.program));

	// A comment is a string, and in a variables file it is not a variable.
	CHECK(refusal(withEdits(kMaianGraph, "\"schema\": \"pd.vehicle_graph.v1\",",
		"\"schema\": \"pd.vehicle_graph.v1\", \"comment\": 7,"), kMaianVariables)
		.find("behavior.graph.json comment must be a string") != std::string::npos);
	CHECK(refusal(withEdits(kMaianGraph, "{ \"id\": \"tick\", \"kind\": \"event.vehicle.tick\" }",
		"{ \"id\": \"tick\", \"kind\": \"event.vehicle.tick\", \"comment\": [1] }"), kMaianVariables)
		.find("node 'tick' comment must be a string") != std::string::npos);
	CHECK(refusal(withEdits(kMaianGraph, "{ \"from\": \"tick\", \"output\": \"out\", \"to\": \"drive\", \"input\": \"exec\" }",
		"{ \"from\": \"tick\", \"output\": \"out\", \"to\": \"drive\", \"input\": \"exec\", \"comment\": true }"), kMaianVariables)
		.find("comment must be a string") != std::string::npos);
	const std::string badVariables = withEdits(kMaianVariables, "\"schema\": \"pd.vehicle_variables.v1\",",
		"\"schema\": \"pd.vehicle_variables.v1\", \"comment\": 3,");
	CHECK(refusal(kMaianGraph, badVariables.c_str())
		.find("behavior/variables.json comment must be a string") != std::string::npos);
	CHECK(refusal(withEdits(kMaianGraph, "\"boost_speed\": \"$boost_speed\"",
		"\"boost_speed\": \"$comment\""), variables.c_str())
		.find("uses unknown variable '$comment'") != std::string::npos);
}

TEST_CASE("A part the vehicle's model does not have is refused", "[vehicle-graph]")
{
	struct Parts {
		static s32 has(void *, s32 part) { return part == 140 || part == 141; }
		static s32 none(void *, s32) { return 0; }
	};
	Program fine(kMaianGraph, kMaianVariables, nullptr, Parts::has, nullptr);
	CHECK(fine.program != nullptr);
	Program missing(kMaianGraph, kMaianVariables, nullptr, Parts::none, nullptr);
	CHECK(missing.program == nullptr);
	CHECK(missing.error.find("names part 140, which the vehicle's model does not have") != std::string::npos);
}

TEST_CASE("Data nodes and structural edges are kept but never walked", "[vehicle-graph]")
{
	const std::string graph = withEdits(kBaseGraph, "\"nodes\": [",
		"\"nodes\": [\n    { \"id\": \"note\", \"kind\": \"comment\", \"text\": \"mine\" },");
	const std::string with_edge = withEdits(graph.c_str(), "\"edges\": [",
		"\"edges\": [\n    { \"from\": \"note\", \"to\": \"drive\" },");
	Program p(with_edge.c_str());
	REQUIRE(p.program != nullptr);
	CHECK(vehicleGraphProgramNodeCount(p.program) == 9);
	CHECK(vehicleGraphProgramExecEdgeCount(p.program) == 5);
	// An execution edge from data is refused.
	const std::string exec_from_data = withEdits(graph.c_str(), "\"edges\": [",
		"\"edges\": [\n    { \"from\": \"note\", \"output\": \"out\", \"to\": \"drive\", \"input\": \"exec\" },");
	CHECK(refusal(exec_from_data).find("which cannot execute") != std::string::npos);
}

TEST_CASE("Graphs over the runner's limits are refused, not truncated", "[vehicle-graph]")
{
	std::string graph = "{ \"schema\": \"pd.vehicle_graph.v1\", \"catalog_id\": \"x:y\", \"nodes\": [";
	for (int i = 0; i <= VEHICLE_GRAPH_MAX_NODES; i++) {
		graph += (i ? ", " : "") + std::string("{ \"id\": \"n") + std::to_string(i) +
			"\", \"kind\": \"comment\" }";
	}
	graph += "] }";
	CHECK(refusal(graph).find("more than 4096 nodes") != std::string::npos);
}

TEST_CASE("vehicleGraphIsV1 tells a v1 graph from a v2 behavior file", "[vehicle-graph]")
{
	CHECK(vehicleGraphIsV1(kBaseGraph, std::strlen(kBaseGraph)) == 1);
	const char *v2 = R"({ "schema": "pd2.vehicle.behavior.v2", "catalog_id": "x:y", "allow_mount": true })";
	CHECK(vehicleGraphIsV1(v2, std::strlen(v2)) == 0);
	CHECK(vehicleGraphIsV1("not json", 8) == 0);
	CHECK(vehicleGraphIsV1(nullptr, 0) == 0);
}

TEST_CASE("Conformance knows exactly the runner's vehicle graph kinds", "[vehicle-graph]")
{
	std::ifstream input("tools/asset_archive_conformance.py", std::ios::binary);
	REQUIRE(input.good());
	std::ostringstream text;
	text << input.rdbuf();
	const std::string conformance = text.str();
	auto set_of = [&](const char *name) {
		std::smatch m;
		const std::regex re(std::string("\n") + name + R"( = \{([^}]*)\})");
		REQUIRE(std::regex_search(conformance, m, re));
		std::set<std::string> out;
		const std::string body = m[1].str();
		const std::regex item("\"([^\"]+)\"");
		for (std::sregex_iterator it(body.begin(), body.end(), item), end; it != end; ++it) {
			out.insert((*it)[1].str());
		}
		return out;
	};
	std::set<std::string> events, gates, modules;
	for (s32 i = 0; const char *kind = vehicleGraphKnownKind(i); i++) {
		const std::string k(kind);
		if (k.rfind("event.", 0) == 0) events.insert(k);
		else if (k.rfind("gate.", 0) == 0) gates.insert(k);
		else modules.insert(k);
	}
	CHECK(events.size() == 5);
	CHECK(gates.size() == 3);
	CHECK(modules.size() == 8);
	CHECK(set_of("VEHICLE_GRAPH_EVENTS") == events);
	CHECK(set_of("VEHICLE_GRAPH_GATES") == gates);
	CHECK(set_of("VEHICLE_GRAPH_MODULES") == modules);
	CHECK(events.count("event.vehicle.switch_seat_requested") == 1);
	CHECK(modules.count("og.vehicle.switch_seat") == 1);
	CHECK(vehicleGraphKnownKind(-1) == nullptr);
	CHECK(vehicleGraphKnownKind(15) != nullptr);
	CHECK(vehicleGraphKnownKind(16) == nullptr);
	REQUIRE(conformance.find("errors.extend(validate_vehicle_graph_execution(") != std::string::npos);
}

TEST_CASE("The graph's parts are listed for the model check", "[vehicle-graph]")
{
	Program maian(kMaianGraph, kMaianVariables);
	REQUIRE(maian.program != nullptr);
	CHECK(vehicleGraphProgramPartCount(maian.program) == 1); // 140, shown and hidden
	CHECK(vehicleGraphProgramPart(maian.program, 0) == 140);
	CHECK(vehicleGraphProgramPart(maian.program, 1) == -1);
	Program base(kBaseGraph);
	REQUIRE(base.program != nullptr);
	CHECK(vehicleGraphProgramPartCount(base.program) == 0);
}

TEST_CASE("Each vehicle keeps its own graph instance until the stage resets", "[vehicle-graph]")
{
	Program maian(kMaianGraph, kMaianVariables);
	Program base(kBaseGraph);
	REQUIRE(maian.program != nullptr);
	REQUIRE(base.program != nullptr);
	vehicleGraphRuntimeReset();
	int a = 0, b = 0, idle = 0;
	s32 created = 0;

	vehicle_graph_instance_t *ia = vehicleGraphRuntimeInstance(&a, 7, maian.program, &created);
	REQUIRE(ia != nullptr);
	CHECK(created == 1);
	CHECK(vehicleGraphRuntimeInstance(&a, 7, maian.program, &created) == ia);
	CHECK(created == 0);
	vehicle_graph_instance_t *ib = vehicleGraphRuntimeInstance(&b, 9, maian.program, &created);
	REQUIRE(ib != nullptr);
	CHECK(ib != ia);
	CHECK(vehicleGraphInstanceProgram(ib) == maian.program);

	// Heat one vehicle; the other stays cool.
	Host host;
	vehicle_graph_host_t table = host.table();
	for (int i = 0; i < 70; i++) REQUIRE(fire(ia, tick(0.0f, true, 1.0f), &table) >= 0);
	CHECK(vehicleGraphInstanceHeatPhase(ia) == VEHICLE_BOOST_HEAT_FULL);
	CHECK(vehicleGraphInstanceHeatPhase(ib) == VEHICLE_BOOST_HEAT_COOL);

	// A new vehicle in the same prop slot (another generation) starts fresh.
	vehicle_graph_instance_t *ia2 = vehicleGraphRuntimeInstance(&a, 8, maian.program, &created);
	REQUIRE(ia2 != nullptr);
	CHECK(created == 1);
	CHECK(vehicleGraphInstanceHeatPhase(ia2) == VEHICLE_BOOST_HEAT_COOL);

	// A changed program (the catalog reloaded) makes the instance again.
	vehicle_graph_instance_t *ib2 = vehicleGraphRuntimeInstance(&b, 9, base.program, &created);
	REQUIRE(ib2 != nullptr);
	CHECK(created == 1);
	CHECK(vehicleGraphInstanceProgram(ib2) == base.program);

	// The driver's boost input is taken once per update.
	vehicleGraphRuntimeSetBoostHeld(&b, 9, 1);
	CHECK(vehicleGraphRuntimeTakeBoostHeld(&b, 9) == 1);
	CHECK(vehicleGraphRuntimeTakeBoostHeld(&b, 9) == 0);
	CHECK(vehicleGraphRuntimeTakeBoostHeld(&a, 8) == 0);
	// Not pressing boost on a vehicle takes no record.
	const s32 records = vehicleGraphRuntimeCount();
	vehicleGraphRuntimeSetBoostHeld(&idle, 1, 0);
	CHECK(vehicleGraphRuntimeCount() == records);
	CHECK(vehicleGraphRuntimeTakeBoostHeld(&idle, 1) == 0);

	// A freed vehicle's record goes with it (propFree); another generation's
	// record in that slot, or a release for a prop with none, changes nothing.
	{
		int freed = 0;
		REQUIRE(vehicleGraphRuntimeInstance(&freed, 4, maian.program, &created) != nullptr);
		const s32 withFreed = vehicleGraphRuntimeCount();
		vehicleGraphRuntimeRelease(&freed, 3);
		CHECK(vehicleGraphRuntimeCount() == withFreed);
		vehicleGraphRuntimeRelease(&freed, 4);
		CHECK(vehicleGraphRuntimeCount() == withFreed - 1);
		vehicleGraphRuntimeRelease(&freed, 4);
		vehicleGraphRuntimeRelease(nullptr, 4);
		CHECK(vehicleGraphRuntimeCount() == withFreed - 1);
		// A vehicle respawned in that slot starts fresh.
		CHECK(vehicleGraphRuntimeInstance(&freed, 5, maian.program, &created) != nullptr);
		CHECK(created == 1);
		vehicleGraphRuntimeRelease(&freed, 5);
	}

	// Freeing a program forgets only the vehicles made from it.
	{
		int x = 0, y = 0;
		REQUIRE(vehicleGraphRuntimeInstance(&x, 1, maian.program, &created) != nullptr);
		REQUIRE(vehicleGraphRuntimeInstance(&y, 1, base.program, &created) != nullptr);
		vehicleGraphRuntimeForgetProgram(maian.program);
		CHECK(vehicleGraphRuntimeInstance(&y, 1, base.program, &created) != nullptr);
		CHECK(created == 0);
		CHECK(vehicleGraphRuntimeInstance(&x, 1, maian.program, &created) != nullptr);
		CHECK(created == 1);
		vehicleGraphRuntimeForgetProgram(nullptr);
	}

	// A refused vehicle gets no instance until a new vehicle takes its slot.
	vehicleGraphRuntimeRefuse(&b, 9);
	CHECK(vehicleGraphRuntimeInstance(&b, 9, maian.program, &created) == nullptr);
	CHECK(vehicleGraphRuntimeInstance(&b, 10, maian.program, &created) != nullptr);
	CHECK(vehicleGraphRuntimeInstance(&a, 8, nullptr, &created) == nullptr);

	// A full table refuses more vehicles instead of sharing an instance.
	vehicleGraphRuntimeReset();
	CHECK(vehicleGraphRuntimeCount() == 0);
	std::vector<int> many(VEHICLE_GRAPH_RUNTIME_MAX + 1);
	int made = 0;
	for (int &vehicle : many) {
		if (vehicleGraphRuntimeInstance(&vehicle, 1, maian.program, nullptr)) made++;
	}
	CHECK(made == VEHICLE_GRAPH_RUNTIME_MAX);
	// Freeing one vehicle makes room for the next, so destroying and
	// respawning vehicles never runs the table out.
	CHECK(vehicleGraphRuntimeInstance(&many.back(), 1, maian.program, nullptr) == nullptr);
	vehicleGraphRuntimeRelease(&many[0], 1);
	CHECK(vehicleGraphRuntimeInstance(&many.back(), 1, maian.program, nullptr) != nullptr);
	vehicleGraphRuntimeReset();
	CHECK(vehicleGraphRuntimeCount() == 0);
}

TEST_CASE("A vehicle's record says who sits in which seat", "[vehicle-graph]")
{
	Program maian(kMaianGraph, kMaianVariables);
	Program base(kBaseGraph);
	REQUIRE(maian.program != nullptr);
	REQUIRE(base.program != nullptr);
	vehicleGraphRuntimeReset();
	int a = 0, b = 0;
	const s32 driver = VEHICLE_GRAPH_SEAT_DRIVER;
	const s32 gunner = VEHICLE_GRAPH_SEAT_GUNNER;

	// A vehicle nobody has touched: empty seats, and asking makes no record.
	CHECK(vehicleGraphRuntimeSeatOccupant(&a, 1, driver) == -1);
	CHECK(vehicleGraphRuntimeSeatOccupant(&a, 1, gunner) == -1);
	CHECK(vehicleGraphRuntimeSeatOfPlayer(&a, 1, 0) == VEHICLE_GRAPH_SEAT_NONE);
	CHECK(vehicleGraphRuntimeSetSeatOccupant(&a, 1, gunner, -1) == 1); // already empty
	CHECK(vehicleGraphRuntimeCount() == 0);

	// Player 0 drives, player 2 is the gunner.
	CHECK(vehicleGraphRuntimeSetSeatOccupant(&a, 1, driver, 0) == 1);
	CHECK(vehicleGraphRuntimeSetSeatOccupant(&a, 1, gunner, 2) == 1);
	CHECK(vehicleGraphRuntimeCount() == 1);
	CHECK(vehicleGraphRuntimeSeatOccupant(&a, 1, driver) == 0);
	CHECK(vehicleGraphRuntimeSeatOccupant(&a, 1, gunner) == 2);
	CHECK(vehicleGraphRuntimeSeatOfPlayer(&a, 1, 0) == driver);
	CHECK(vehicleGraphRuntimeSeatOfPlayer(&a, 1, 2) == gunner);
	CHECK(vehicleGraphRuntimeSeatOfPlayer(&a, 1, 1) == VEHICLE_GRAPH_SEAT_NONE);
	// Seating the same player again changes nothing.
	CHECK(vehicleGraphRuntimeSetSeatOccupant(&a, 1, gunner, 2) == 1);
	CHECK(vehicleGraphRuntimeSeatOccupant(&a, 1, gunner) == 2);
	// A taken seat refuses another player, and a rider cannot switch into it.
	CHECK(vehicleGraphRuntimeSetSeatOccupant(&a, 1, gunner, 3) == 0);
	CHECK(vehicleGraphRuntimeSetSeatOccupant(&a, 1, gunner, 0) == 0);
	CHECK(vehicleGraphRuntimeSeatOccupant(&a, 1, gunner) == 2);
	CHECK(vehicleGraphRuntimeSeatOfPlayer(&a, 1, 0) == driver);

	// The gunner leaves; the driver then switches to that seat in one call.
	CHECK(vehicleGraphRuntimeSetSeatOccupant(&a, 1, gunner, -1) == 1);
	CHECK(vehicleGraphRuntimeSeatOccupant(&a, 1, gunner) == -1);
	CHECK(vehicleGraphRuntimeSetSeatOccupant(&a, 1, gunner, 0) == 1);
	CHECK(vehicleGraphRuntimeSeatOccupant(&a, 1, gunner) == 0);
	CHECK(vehicleGraphRuntimeSeatOccupant(&a, 1, driver) == -1);
	CHECK(vehicleGraphRuntimeSeatOfPlayer(&a, 1, 0) == gunner);

	// Each vehicle has its own seats.
	CHECK(vehicleGraphRuntimeSeatOccupant(&b, 1, gunner) == -1);
	CHECK(vehicleGraphRuntimeSetSeatOccupant(&b, 1, driver, 0) == 1);
	CHECK(vehicleGraphRuntimeSeatOfPlayer(&b, 1, 0) == driver);
	CHECK(vehicleGraphRuntimeSeatOfPlayer(&a, 1, 0) == gunner);

	// Out of range: nothing is recorded.
	CHECK(vehicleGraphRuntimeSetSeatOccupant(&a, 1, 2, 1) == 0);
	CHECK(vehicleGraphRuntimeSetSeatOccupant(&a, 1, -1, 1) == 0);
	CHECK(vehicleGraphRuntimeSetSeatOccupant(&a, 1, driver, -2) == 0);
	CHECK(vehicleGraphRuntimeSetSeatOccupant(nullptr, 1, driver, 1) == 0);
	CHECK(vehicleGraphRuntimeSeatOccupant(&a, 1, 2) == -1);
	CHECK(vehicleGraphRuntimeSeatOccupant(nullptr, 1, driver) == -1);
	CHECK(vehicleGraphRuntimeSeatOfPlayer(&a, 1, -1) == VEHICLE_GRAPH_SEAT_NONE);
	CHECK(vehicleGraphRuntimeSeatOccupant(&a, 1, driver) == -1);

	// The seats are the vehicle's, not the program's: a freed program takes
	// its instance and leaves the riders, and a vehicle nobody rides is
	// forgotten whole, as before.
	s32 created = 0;
	int unridden = 0;
	REQUIRE(vehicleGraphRuntimeInstance(&a, 1, maian.program, &created) != nullptr);
	REQUIRE(vehicleGraphRuntimeInstance(&unridden, 1, maian.program, &created) != nullptr);
	CHECK(vehicleGraphRuntimeCount() == 3);
	vehicleGraphRuntimeForgetProgram(maian.program);
	CHECK(vehicleGraphRuntimeCount() == 2);
	CHECK(vehicleGraphRuntimeSeatOfPlayer(&a, 1, 0) == gunner);
	CHECK(vehicleGraphRuntimeSeatOfPlayer(&b, 1, 0) == driver);
	// The next program finds the rider where they sat.
	REQUIRE(vehicleGraphRuntimeInstance(&a, 1, base.program, &created) != nullptr);
	CHECK(created == 1);
	CHECK(vehicleGraphRuntimeSeatOfPlayer(&a, 1, 0) == gunner);

	// A new vehicle in the prop slot (another generation) has empty seats.
	REQUIRE(vehicleGraphRuntimeInstance(&a, 2, base.program, &created) != nullptr);
	CHECK(created == 1);
	CHECK(vehicleGraphRuntimeSeatOccupant(&a, 2, gunner) == -1);
	CHECK(vehicleGraphRuntimeSeatOfPlayer(&a, 2, 0) == VEHICLE_GRAPH_SEAT_NONE);
	// A freed vehicle takes its seats with it.
	vehicleGraphRuntimeRelease(&b, 1);
	CHECK(vehicleGraphRuntimeSeatOfPlayer(&b, 1, 0) == VEHICLE_GRAPH_SEAT_NONE);
	// So does a stage reset.
	CHECK(vehicleGraphRuntimeSetSeatOccupant(&a, 2, driver, 1) == 1);
	vehicleGraphRuntimeReset();
	CHECK(vehicleGraphRuntimeSeatOccupant(&a, 2, driver) == -1);
	CHECK(vehicleGraphRuntimeCount() == 0);

	// A full table refuses a new vehicle's rider; emptying a seat needs no room.
	std::vector<int> many(VEHICLE_GRAPH_RUNTIME_MAX);
	for (int &vehicle : many) {
		REQUIRE(vehicleGraphRuntimeSetSeatOccupant(&vehicle, 1, driver, 0) == 1);
	}
	int late = 0;
	CHECK(vehicleGraphRuntimeSetSeatOccupant(&late, 1, driver, 0) == 0);
	CHECK(vehicleGraphRuntimeSetSeatOccupant(&late, 1, driver, -1) == 1);
	vehicleGraphRuntimeReset();
	CHECK(vehicleGraphRuntimeCount() == 0);
}

TEST_CASE("A vehicle's speed over the ground comes from its moves and their ticks", "[vehicle-graph]")
{
	vehicleGraphRuntimeReset();
	int a = 0, b = 0;

	// The first call has nothing to compare with.
	CHECK(vehicleGraphRuntimeGroundSpeed(&a, 3, 100.0f, 200.0f, 2.0f) == 0.0f);
	// 30 units in x and 40 in z (50 in all) over the previous update's 2 ticks.
	CHECK(vehicleGraphRuntimeGroundSpeed(&a, 3, 130.0f, 240.0f, 1.0f) == Approx(25.0f));
	// 21 units over this update's 1 tick, slower: the peak keeps the 25.
	CHECK(vehicleGraphRuntimeGroundSpeed(&a, 3, 130.0f, 261.0f, 1.0f) == Approx(21.0f));
	CHECK(vehicleGraphRuntimeTakePeakGround(&a, 3) == Approx(25.0f));
	CHECK(vehicleGraphRuntimeTakePeakGround(&a, 3) == 0.0f);
	// Against a wall: no move, no speed.
	CHECK(vehicleGraphRuntimeGroundSpeed(&a, 3, 130.0f, 261.0f, 1.0f) == 0.0f);
	CHECK(vehicleGraphRuntimeTakePeakGround(&a, 3) == 0.0f);

	// Each vehicle has its own.
	CHECK(vehicleGraphRuntimeGroundSpeed(&b, 5, 0.0f, 0.0f, 1.0f) == 0.0f);
	CHECK(vehicleGraphRuntimeGroundSpeed(&b, 5, 0.0f, 7.0f, 1.0f) == Approx(7.0f));
	CHECK(vehicleGraphRuntimeTakePeakGround(&b, 5) == Approx(7.0f));

	// A new vehicle in the same slot starts again; a reset forgets it all.
	CHECK(vehicleGraphRuntimeGroundSpeed(&a, 4, 500.0f, 500.0f, 1.0f) == 0.0f);
	vehicleGraphRuntimeReset();
	CHECK(vehicleGraphRuntimeTakePeakGround(&b, 5) == 0.0f);
	CHECK(vehicleGraphRuntimeCount() == 0);
}
