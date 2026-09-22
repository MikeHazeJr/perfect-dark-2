/* Pure signed ICE/STUN/UPnP candidate transport contracts. */

#include "catch.hpp"

extern "C" {
#include "net/net_candidate.h"
#include "net/netupnp.h"

extern const u32 g_TestLiveNetProtocolVer;
}

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifndef PD_SOURCE_DIR
#error "PD_SOURCE_DIR must identify the canonical Perfect Dark source checkout"
#endif

namespace {

net_candidate_set_t makeSet(u32 now)
{
	net_candidate_set_t set{};
	set.sender_handle = 0x11111111u;
	set.target_handle = 0x22222222u;
	set.proto_version = g_TestLiveNetProtocolVer;
	set.kind = NET_CANDIDATE_FRAME_PUBLISH;
	set.generation = 7;
	set.issued_unix_seconds = now - 1;
	set.expires_unix_seconds = now + 60;
	std::memset(set.credential, 0x5a, sizeof(set.credential));
	std::strcpy(set.agent_name, "Agent");
	set.candidate_count = 3;
	set.candidates[0] = {0xc0a80102u, 27103, NET_CANDIDATE_TYPE_HOST,
		NET_CANDIDATE_TYPE_HOST, 100};
	set.candidates[1] = {0x08080808u, 40123, NET_CANDIDATE_TYPE_STUN,
		NET_CANDIDATE_TYPE_STUN, 200};
	set.candidates[2] = {0x01010101u, 27103, NET_CANDIDATE_TYPE_UPNP,
		NET_CANDIDATE_TYPE_UPNP, 150};
	return set;
}

std::string readSource(const char *relative_path)
{
	const std::filesystem::path path =
		std::filesystem::path(PD_SOURCE_DIR) / relative_path;
	std::ifstream input(path, std::ios::in | std::ios::binary);
	REQUIRE(input.good());
	std::ostringstream output;
	output << input.rdbuf();
	return output.str();
}

} // namespace

TEST_CASE("candidate validator normalizes bounded typed endpoints",
	"[networking][candidate][security]")
{
	const u32 now = 1700000000u;
	const net_candidate_set_t wire = makeSet(now);
	net_candidate_set_t normalized{};
	REQUIRE(netCandidateSetNormalize(&wire, wire.sender_handle,
		wire.target_handle, now, &normalized) == 1);
	REQUIRE(normalized._pad[0] == 0);
	REQUIRE(normalized.candidate_count == 3);
	REQUIRE(normalized.candidates[0].type == NET_CANDIDATE_TYPE_STUN);
	REQUIRE(normalized.candidates[1].type == NET_CANDIDATE_TYPE_UPNP);
	REQUIRE(normalized.candidates[2].type == NET_CANDIDATE_TYPE_HOST);
	for (u32 i = normalized.candidate_count; i < NET_CANDIDATE_MAX; i++) {
		REQUIRE(normalized.candidates[i].ipv4 == 0);
		REQUIRE(normalized.candidates[i].port == 0);
	}
	net_candidate_set_t poisoned = wire;
	std::memset(poisoned.candidates + poisoned.candidate_count, 0xa5,
		(NET_CANDIDATE_MAX - poisoned.candidate_count) *
		sizeof(poisoned.candidates[0]));
	REQUIRE(netCandidateSetNormalize(&poisoned, wire.sender_handle,
		wire.target_handle, now, &normalized) == 1);
	for (u32 i = normalized.candidate_count; i < NET_CANDIDATE_MAX; i++) {
		REQUIRE(normalized.candidates[i].ipv4 == 0);
		REQUIRE(normalized.candidates[i].port == 0);
		REQUIRE(normalized.candidates[i].priority == 0);
	}
}

TEST_CASE("candidate validator fails closed for stale, malformed, and relabelled data",
	"[networking][candidate][security]")
{
	const u32 now = 1700000000u;
	net_candidate_set_t wire = makeSet(now);
	net_candidate_set_t normalized{};

	wire.candidates[0].ipv4 = 0;
	REQUIRE(netCandidateSetNormalize(&wire, wire.sender_handle,
		wire.target_handle, now, &normalized) == 0);
	wire = makeSet(now);
	wire.candidates[0].ipv4 = 0xe0000001u;
	REQUIRE(netCandidateSetNormalize(&wire, wire.sender_handle,
		wire.target_handle, now, &normalized) == 0);
	wire = makeSet(now);
	wire.candidates[0].provenance = NET_CANDIDATE_TYPE_STUN;
	REQUIRE(netCandidateSetNormalize(&wire, wire.sender_handle,
		wire.target_handle, now, &normalized) == 0);
	wire = makeSet(now);
	wire.candidates[1].ipv4 = wire.candidates[0].ipv4;
	wire.candidates[1].port = wire.candidates[0].port;
	wire.candidates[1].type = NET_CANDIDATE_TYPE_HOST;
	wire.candidates[1].provenance = NET_CANDIDATE_TYPE_HOST;
	wire.candidates[1].priority = NET_CANDIDATE_PRIORITY_HOST;
	REQUIRE(netCandidateSetNormalize(&wire, wire.sender_handle,
		wire.target_handle, now, &normalized) == 0);
	wire = makeSet(now);
	wire.candidates[0].ipv4 = 0x7f000001u;
	REQUIRE(netCandidateSetNormalize(&wire, wire.sender_handle,
		wire.target_handle, now, &normalized) == 0);
	wire = makeSet(now);
	wire.candidates[0].ipv4 = 0xa9fe0101u;
	REQUIRE(netCandidateSetNormalize(&wire, wire.sender_handle,
		wire.target_handle, now, &normalized) == 0);
	wire = makeSet(now);
	wire.candidates[0].ipv4 = 0xc0000001u;
	REQUIRE(netCandidateSetNormalize(&wire, wire.sender_handle,
		wire.target_handle, now, &normalized) == 0);
	wire = makeSet(now);
	wire.candidates[0].ipv4 = 0x64400001u;
	REQUIRE(netCandidateSetNormalize(&wire, wire.sender_handle,
		wire.target_handle, now, &normalized) == 0);
	const u32 reserved_host_routes[] = {
		0xc0000201u, /* TEST-NET-1 */
		0xc6336401u, /* TEST-NET-2 */
		0xcb007101u, /* TEST-NET-3 */
		0xc6120001u, /* benchmarking 198.18/15 */
		0xc613ffffu,
	};
	for (u32 reserved : reserved_host_routes) {
		wire = makeSet(now);
		wire.candidates[0].ipv4 = reserved;
		REQUIRE(netCandidateSetNormalize(&wire, wire.sender_handle,
			wire.target_handle, now, &normalized) == 0);
	}
	wire = makeSet(now);
	wire.candidates[1].ipv4 = 0x0a000001u;
	REQUIRE(netCandidateSetNormalize(&wire, wire.sender_handle,
		wire.target_handle, now, &normalized) == 0);
	wire = makeSet(now);
	wire.expires_unix_seconds = now + NET_CANDIDATE_MAX_LEASE_SECONDS + 1;
	REQUIRE(netCandidateSetNormalize(&wire, wire.sender_handle,
		wire.target_handle, now, &normalized) == 0);
	wire = makeSet(now);
	wire.issued_unix_seconds = now - NET_CANDIDATE_FRESH_SECONDS - 1;
	wire.expires_unix_seconds = now + 60;
	REQUIRE(netCandidateSetNormalize(&wire, wire.sender_handle,
		wire.target_handle, now, &normalized) == 0);
	wire = makeSet(now);
	wire.issued_unix_seconds = now - NET_CANDIDATE_FRESH_SECONDS;
	REQUIRE(netCandidateSetNormalize(&wire, wire.sender_handle,
		wire.target_handle, now, &normalized) == 1);
	wire = makeSet(now);
	wire.issued_unix_seconds = now + NET_CANDIDATE_CLOCK_SKEW_SECONDS + 1;
	wire.expires_unix_seconds = wire.issued_unix_seconds + 60;
	REQUIRE(netCandidateSetNormalize(&wire, wire.sender_handle,
		wire.target_handle, now, &normalized) == 0);
	wire = makeSet(now);
	wire.proto_version++;
	REQUIRE(netCandidateSetNormalize(&wire, wire.sender_handle,
		wire.target_handle, now, &normalized) == 0);
	wire = makeSet(now);
	wire.kind = NET_CANDIDATE_FRAME_RETIRE;
	wire.candidate_count = 1;
	REQUIRE(netCandidateSetNormalize(&wire, wire.sender_handle,
		wire.target_handle, now, &normalized) == 0);
	wire = makeSet(now);
	wire.agent_name[0] = '\0';
	REQUIRE(netCandidateSetNormalize(&wire, wire.sender_handle,
		wire.target_handle, now, &normalized) == 0);
}

TEST_CASE("ICE retry and replay state tolerates skew but remains bounded",
	"[networking][candidate][ice][state]")
{
	net_candidate_probe_retry_t early{};
	net_candidate_probe_retry_t late{};
	netCandidateProbeRetryInit(&early, 0);
	netCandidateProbeRetryInit(&late, 900);
	net_candidate_probe_replay_t early_responder[
		NET_CANDIDATE_PROBE_REPLAY_SLOTS]{};
	net_candidate_probe_replay_t late_responder[
		NET_CANDIDATE_PROBE_REPLAY_SLOTS]{};
	bool early_open = false;
	bool late_open = false;
	for (u32 now_ms = 0; now_ms < 2500; now_ms += 100) {
		if (!early_open && netCandidateProbeRetryPoll(&early, now_ms, 2500) ==
			NET_CANDIDATE_PROBE_RETRY_SEND && now_ms >= 900) {
			early_open = netCandidateProbeReplayRecord(late_responder,
				NET_CANDIDATE_PROBE_REPLAY_SLOTS, 0x1001u,
				0x0a000001u, 27103) == NET_CANDIDATE_PROBE_REPLAY_NEW;
		}
		if (now_ms >= 900 && !late_open &&
			netCandidateProbeRetryPoll(&late, now_ms, 2500) ==
				NET_CANDIDATE_PROBE_RETRY_SEND) {
			late_open = netCandidateProbeReplayRecord(early_responder,
				NET_CANDIDATE_PROBE_REPLAY_SLOTS, 0x2001u,
				0x0a000002u, 27103) == NET_CANDIDATE_PROBE_REPLAY_NEW;
		}
	}
	REQUIRE(early_open);
	REQUIRE(late_open);
	REQUIRE(early.send_count > 1);
	REQUIRE(early.send_count <= NET_CANDIDATE_PROBE_MAX_SENDS);
	REQUIRE(late.send_count <= NET_CANDIDATE_PROBE_MAX_SENDS);

	net_candidate_probe_replay_t replay[2]{};
	REQUIRE(netCandidateProbeReplayRecord(replay, 2, 0xabcdef01u,
		0x0a000001u, 27103) == NET_CANDIDATE_PROBE_REPLAY_NEW);
	REQUIRE(netCandidateProbeReplayRecord(replay, 2, 0xabcdef01u,
		0x0a000001u, 27103) == NET_CANDIDATE_PROBE_REPLAY_DUPLICATE);
	REQUIRE(netCandidateProbeReplayRecord(replay, 2, 0xabcdef01u,
		0x0a000002u, 27103) == NET_CANDIDATE_PROBE_REPLAY_REJECT);

	net_candidate_probe_retry_t bounded{};
	netCandidateProbeRetryInit(&bounded, 0);
	for (u32 i = 0; i < NET_CANDIDATE_PROBE_MAX_SENDS; i++) {
		REQUIRE(netCandidateProbeRetryPoll(&bounded,
			i * NET_CANDIDATE_PROBE_RETRY_INTERVAL_MS, 10000) ==
			NET_CANDIDATE_PROBE_RETRY_SEND);
	}
	REQUIRE(netCandidateProbeRetryPoll(&bounded, 9000, 10000) ==
		NET_CANDIDATE_PROBE_RETRY_EXHAUSTED);
}

TEST_CASE("candidate signaling retries are bounded and retain full ICE expiry headroom",
	"[networking][candidate][signal][state]")
{
	net_candidate_signal_retry_t signal{};
	netCandidateSignalRetryInit(&signal, 100);
	REQUIRE(netCandidateSignalRetryPoll(&signal, 100, 2600) ==
		NET_CANDIDATE_SIGNAL_RETRY_SEND);
	REQUIRE(netCandidateSignalRetryPoll(&signal, 599, 2600) ==
		NET_CANDIDATE_SIGNAL_RETRY_WAIT);
	for (u32 send = 1; send < NET_CANDIDATE_SIGNAL_MAX_SENDS; send++) {
		REQUIRE(netCandidateSignalRetryPoll(&signal,
			100 + send * NET_CANDIDATE_SIGNAL_RETRY_INTERVAL_MS, 2600) ==
			NET_CANDIDATE_SIGNAL_RETRY_SEND);
	}
	REQUIRE(signal.send_count == NET_CANDIDATE_SIGNAL_MAX_SENDS);
	REQUIRE(netCandidateSignalRetryPoll(&signal, 2599, 2600) ==
		NET_CANDIDATE_SIGNAL_RETRY_EXHAUSTED);

	const u32 now = 1700000000u;
	REQUIRE(netCandidateExpiryHasHeadroom(now +
		NET_CANDIDATE_SIGNAL_EXPIRY_HEADROOM_SECONDS - 1, now,
		NET_CANDIDATE_SIGNAL_EXPIRY_HEADROOM_SECONDS) == 0);
	REQUIRE(netCandidateExpiryHasHeadroom(now +
		NET_CANDIDATE_SIGNAL_EXPIRY_HEADROOM_SECONDS, now,
		NET_CANDIDATE_SIGNAL_EXPIRY_HEADROOM_SECONDS) == 1);
	REQUIRE(netCandidateExpiryHasHeadroom(now - 1, now,
		NET_CANDIDATE_SIGNAL_EXPIRY_HEADROOM_SECONDS) == 0);
}

TEST_CASE("candidate planner distinguishes new generations, replays, conflicts, and retirement",
	"[networking][candidate][replay]")
{
	const u32 now = 1700000000u;
	net_candidate_set_t first = makeSet(now);
	net_candidate_peer_state_t prior{};
	REQUIRE(netCandidatePlanUpdate(nullptr, &first) ==
		NET_CANDIDATE_UPDATE_ACCEPT);
	netCandidatePeerStateCommit(&prior, &first);
	REQUIRE(netCandidatePlanUpdate(&prior, &first) ==
		NET_CANDIDATE_UPDATE_DUPLICATE);

	net_candidate_set_t older = first;
	older.issued_unix_seconds--;
	REQUIRE(netCandidatePlanUpdate(&prior, &older) ==
		NET_CANDIDATE_UPDATE_REJECT);
	net_candidate_set_t conflict = first;
	conflict.candidates[0].port++;
	REQUIRE(netCandidatePlanUpdate(&prior, &conflict) ==
		NET_CANDIDATE_UPDATE_REJECT);
	net_candidate_set_t refresh = first;
	refresh.issued_unix_seconds++;
	REQUIRE(netCandidatePlanUpdate(&prior, &refresh) ==
		NET_CANDIDATE_UPDATE_ACCEPT);
	net_candidate_probe_replay_t retained_replay[2]{};
	REQUIRE(netCandidateProbeReplayRecord(retained_replay, 2, 0x7001u,
		0x0a000001u, 27103) == NET_CANDIDATE_PROBE_REPLAY_NEW);
	netCandidatePeerStateCommit(&prior, &refresh);
	REQUIRE(netCandidateProbeReplayRecord(retained_replay, 2, 0x7001u,
		0x0a000001u, 27103) == NET_CANDIDATE_PROBE_REPLAY_DUPLICATE);
	REQUIRE(netCandidateProbeReplayRecord(retained_replay, 2, 0x7001u,
		0x0a000002u, 27103) == NET_CANDIDATE_PROBE_REPLAY_REJECT);
	net_candidate_set_t expiry_change = first;
	expiry_change.expires_unix_seconds++;
	REQUIRE(netCandidatePlanUpdate(&prior, &expiry_change) ==
		NET_CANDIDATE_UPDATE_REJECT);
	net_candidate_set_t newer = first;
	newer.generation++;
	std::memset(newer.credential, 0x5b, sizeof(newer.credential));
	REQUIRE(netCandidatePlanUpdate(&prior, &newer) ==
		NET_CANDIDATE_UPDATE_ACCEPT);
	net_candidate_set_t retire = newer;
	retire.kind = NET_CANDIDATE_FRAME_RETIRE;
	retire.candidate_count = 0;
	retire.issued_unix_seconds++;
	REQUIRE(netCandidatePlanUpdate(&prior, &retire) ==
		NET_CANDIDATE_UPDATE_RETIRE);
	net_candidate_set_t stale_generation = first;
	stale_generation.generation--;
	REQUIRE(netCandidatePlanUpdate(&prior, &stale_generation) ==
		NET_CANDIDATE_UPDATE_REJECT);
	net_candidate_peer_state_t high_generation{};
	net_candidate_set_t high = first;
	high.generation = 0xffffffffu;
	high.issued_unix_seconds = now;
	high.expires_unix_seconds = now + 60;
	std::memset(high.credential, 0x6b, sizeof(high.credential));
	netCandidatePeerStateCommit(&high_generation, &high);
	net_candidate_set_t stale_wrap = first;
	stale_wrap.generation = 1;
	stale_wrap.issued_unix_seconds = now + 1;
	stale_wrap.expires_unix_seconds = now + 61;
	REQUIRE(netCandidatePlanUpdate(&high_generation, &stale_wrap) ==
		NET_CANDIDATE_UPDATE_REJECT);
	std::memset(stale_wrap.credential, 0x6c, sizeof(stale_wrap.credential));
	REQUIRE(netCandidatePlanUpdate(&high_generation, &stale_wrap) ==
		NET_CANDIDATE_UPDATE_REJECT);
}

TEST_CASE("candidate frame codec is fixed-size and rejects wire mutations",
	"[networking][candidate][protocol]")
{
	const u32 now = 1700000000u;
	net_candidate_set_t set = makeSet(now);
	std::memset(set.candidates + set.candidate_count, 0xa5,
		(NET_CANDIDATE_MAX - set.candidate_count) * sizeof(set.candidates[0]));
	u8 pubkey[32];
	std::memset(pubkey, 0xa5, sizeof(pubkey));
	u8 frame[NET_CANDIDATE_FRAME_LEN]{};
	REQUIRE(netCandidateFrameEncodeBody(&set, pubkey, frame) == 1);
	for (u32 i = set.candidate_count; i < NET_CANDIDATE_MAX; i++) {
		for (u32 j = 0; j < NET_CANDIDATE_RECORD_LEN; j++) {
			REQUIRE(frame[48 + i * NET_CANDIDATE_RECORD_LEN + j] == 0);
		}
	}
	std::memset(frame + NET_CANDIDATE_SIG_OFFSET, 0xa5,
		NET_CANDIDATE_SIG_LEN);

	net_candidate_set_t decoded{};
	const u8 *decoded_pubkey = nullptr;
	const u8 *decoded_signature = nullptr;
	REQUIRE(netCandidateFrameDecode(frame, sizeof(frame), &decoded,
		&decoded_pubkey, &decoded_signature) == 1);
	REQUIRE(decoded.sender_handle == set.sender_handle);
	REQUIRE(std::memcmp(decoded_pubkey, pubkey, sizeof(pubkey)) == 0);
	REQUIRE(decoded_signature == frame + NET_CANDIDATE_SIG_OFFSET);
	REQUIRE(netCandidateFrameDecode(frame, sizeof(frame) - 1, &decoded,
		&decoded_pubkey, &decoded_signature) == 0);
	frame[0] = 'X';
	REQUIRE(netCandidateFrameDecode(frame, sizeof(frame), &decoded,
		&decoded_pubkey, &decoded_signature) == 0);
	frame[0] = NET_CANDIDATE_MAGIC[0];
	frame[5]++;
	REQUIRE(netCandidateFrameDecode(frame, sizeof(frame), &decoded,
		&decoded_pubkey, &decoded_signature) == 0);
	frame[5] = NET_CANDIDATE_VERSION;
	frame[48 + 3 * NET_CANDIDATE_RECORD_LEN] = 1;
	REQUIRE(netCandidateFrameDecode(frame, sizeof(frame), &decoded,
		&decoded_pubkey, &decoded_signature) == 0);
	frame[48 + 3 * NET_CANDIDATE_RECORD_LEN] = 0;
	frame[30] = NET_CANDIDATE_MAX + 1;
	REQUIRE(netCandidateFrameDecode(frame, sizeof(frame), &decoded,
		&decoded_pubkey, &decoded_signature) == 0);
}

TEST_CASE("authenticated ICE probe and ack reject identity, freshness, and replay mutations",
	"[networking][candidate][ice][security]")
{
	const u32 now = 1700000000u;
	u8 key[NET_CANDIDATE_CREDENTIAL_LEN];
	std::memset(key, 0x5a, sizeof(key));
	net_candidate_probe_t probe{};
	probe.kind = NET_CANDIDATE_PROBE_KIND_PROBE;
	probe.sender_handle = 0x11111111u;
	probe.target_handle = 0x22222222u;
	probe.candidate_owner_handle = probe.target_handle;
	probe.generation = 7;
	probe.nonce = 0x12345678u;
	std::memset(probe.credential, 0x5a, sizeof(probe.credential));
	probe.candidate_ipv4 = 0x08080808u;
	probe.candidate_port = 40123;
	probe.candidate_index = 0;
	u8 frame[NET_CANDIDATE_PROBE_FRAME_LEN]{};
	REQUIRE(netCandidateProbeEncode(&probe, key, frame) == 1);
	net_candidate_probe_t decoded{};
	REQUIRE(netCandidateProbeDecode(frame, sizeof(frame), &decoded) == 1);
	REQUIRE(netCandidateProbeAuthenticate(frame, sizeof(frame), key) == 1);
	REQUIRE(netCandidateProbeValidate(&decoded, frame, sizeof(frame), key,
		probe.sender_handle, probe.target_handle, probe.candidate_owner_handle,
		probe.generation, probe.credential, now + 60, now, 0) == 1);

	net_candidate_probe_t wrong_target = probe;
	wrong_target.target_handle = 0x33333333u;
	REQUIRE(netCandidateProbeEncode(&wrong_target, key, frame) == 1);
	REQUIRE(netCandidateProbeValidate(&wrong_target, frame, sizeof(frame), key,
		probe.sender_handle, probe.target_handle, probe.candidate_owner_handle,
		probe.generation, probe.credential, now + 60, now, 0) == 0);
	net_candidate_probe_t wrong_credential = probe;
	wrong_credential.credential[0] ^= 1;
	REQUIRE(netCandidateProbeEncode(&wrong_credential, key, frame) == 1);
	REQUIRE(netCandidateProbeValidate(&wrong_credential, frame, sizeof(frame), key,
		probe.sender_handle, probe.target_handle, probe.candidate_owner_handle,
		probe.generation, probe.credential, now + 60, now, 0) == 0);
	REQUIRE(netCandidateProbeEncode(&probe, key, frame) == 1);
	REQUIRE(netCandidateProbeValidate(&probe, frame, sizeof(frame), key,
		probe.sender_handle, probe.target_handle, probe.candidate_owner_handle,
		probe.generation, probe.credential, now - 1, now, 0) == 0);
	REQUIRE(netCandidateProbeValidate(&probe, frame, sizeof(frame), key,
		probe.sender_handle, probe.target_handle, probe.candidate_owner_handle,
		probe.generation + 1, probe.credential, now + 60, now, 0) == 0);
	REQUIRE(netCandidateProbeValidate(&probe, frame, sizeof(frame), key,
		probe.sender_handle, probe.target_handle, probe.candidate_owner_handle,
		probe.generation, probe.credential, now + 60, now,
		probe.nonce) == 0);
	frame[7] = 1;
	REQUIRE(netCandidateProbeDecode(frame, sizeof(frame), &decoded) == 0);

	net_candidate_probe_t ack = probe;
	ack.kind = NET_CANDIDATE_PROBE_KIND_ACK;
	ack.sender_handle = probe.target_handle;
	ack.target_handle = probe.sender_handle;
	ack.candidate_owner_handle = probe.candidate_owner_handle;
	REQUIRE(netCandidateProbeEncode(&ack, key, frame) == 1);
	REQUIRE(netCandidateProbeValidate(&ack, frame, sizeof(frame), key,
		probe.target_handle, probe.sender_handle, probe.candidate_owner_handle,
		probe.generation, probe.credential, now + 60, now, 0) == 1);
	REQUIRE(netCandidateProbeSourceMatches(&ack, probe.candidate_ipv4,
		probe.candidate_port) == 1);
	REQUIRE(netCandidateProbeSourceMatches(&ack, probe.candidate_ipv4,
		(u16)(probe.candidate_port + 1)) == 0);
}

TEST_CASE("UPnP lifecycle preserves a live lease across failed renewal",
	"[networking][upnp][state]")
{
	s32 next = -1;
	s32 preserve = -1;
	REQUIRE(netUpnpLifecycleTransition(UPNP_STATUS_IDLE,
		NET_UPNP_LIFECYCLE_BEGIN_SETUP, 0, &next, &preserve) == 1);
	REQUIRE(next == UPNP_STATUS_WORKING);
	REQUIRE(preserve == 0);
	REQUIRE(netUpnpLifecycleTransition(UPNP_STATUS_SUCCESS,
		NET_UPNP_LIFECYCLE_BEGIN_RENEW, 1, &next, &preserve) == 1);
	REQUIRE(next == UPNP_STATUS_WORKING);
	REQUIRE(preserve == 1);
	REQUIRE(netUpnpLifecycleTransition(UPNP_STATUS_WORKING,
		NET_UPNP_LIFECYCLE_WORKER_FAILURE, 1, &next, &preserve) == 1);
	REQUIRE(next == UPNP_STATUS_SUCCESS);
	REQUIRE(preserve == 1);
	REQUIRE(netUpnpLifecycleTransition(UPNP_STATUS_WORKING,
		NET_UPNP_LIFECYCLE_WORKER_FAILURE, 0, &next, &preserve) == 1);
	REQUIRE(next == UPNP_STATUS_FAILED);
	REQUIRE(preserve == 0);
	REQUIRE(netUpnpLifecycleTransition(UPNP_STATUS_SUCCESS,
		NET_UPNP_LIFECYCLE_TEARDOWN, 1, &next, &preserve) == 1);
	REQUIRE(next == UPNP_STATUS_IDLE);
}

TEST_CASE("production flow keeps candidates separate from D-003 route frames",
	"[networking][candidate][d-003][static]")
{
	const std::string presence = readSource("port/src/presence.c");
	const std::string ice = readSource("port/src/net/p2p_ice.c");
	const std::string stun = readSource("port/src/net/p2p_stun.c");
	const std::string netstun = readSource("port/src/net/netstun.c");
	const std::string p2p_upnp = readSource("port/src/net/p2p_upnp.c");
	const std::string upnp = readSource("port/src/net/netupnp.c");
	const std::string p2p = readSource("port/src/net/p2p.c");
	const std::string direct = readSource("port/src/net/p2p_direct.c");
	const std::string turn = readSource("port/src/net/p2p_turn.c");
	const std::string lan = readSource("port/src/net/p2p_lan.c");

	REQUIRE(presence.find("NET_CANDIDATE_FRAME_LEN") != std::string::npos);
	REQUIRE(presence.find("signCandidateFrame") != std::string::npos);
	REQUIRE(presence.find("netCandidateRandomBytes") != std::string::npos);
	REQUIRE(presence.find("set.expires_unix_seconds = s_LocalCandidateExpires") != std::string::npos);
	REQUIRE(presence.find("p2pPeerCandidateSetReceived") != std::string::npos);
	REQUIRE(presence.find("NET_UPNP_OWNER_SOCIAL") != std::string::npos);
	REQUIRE(presence.find("netUpnpGetOwnedMappedPort") != std::string::npos);
	REQUIRE(presence.find("presenceSendCandidateRefresh") != std::string::npos);
	REQUIRE(presence.find("PRESENCE_KIND_MATCH_ROUTE") != std::string::npos);
	REQUIRE(ice.find("p2pIceApplyPeerCandidateSet") != std::string::npos);
	REQUIRE(ice.find("ICE_MAX_LOCAL_CANDIDATE_TARGETS") != std::string::npos);
	REQUIRE(ice.find("findLocalCandidateTarget(probe.sender_handle)") !=
		std::string::npos);
	const size_t responder_path =
		ice.find("findLocalCandidateTarget(probe.sender_handle)");
	const size_t ack_pair_gate =
		ice.find("if (!peer_pair || !peer_pair->started) continue");
	REQUIRE(responder_path < ack_pair_gate);
	REQUIRE(ice.find("NET_CANDIDATE_PROBE_REPLAY_DUPLICATE") ==
		std::string::npos); /* pure helper classifies; responder re-ACKs both */
	REQUIRE(ice.find("netCandidateProbeRetryPoll") != std::string::npos);
	const size_t local_set = ice.find("void p2pIceSetLocalCandidateSet");
	const size_t local_plan = ice.find("netCandidatePlanUpdate(", local_set);
	const size_t local_replay_epoch = ice.find("memset(target->replay", local_set);
	const size_t local_commit = ice.find("netCandidatePeerStateCommit", local_set);
	const size_t ice_start = ice.find("s32 p2pIceStart", local_set);
	REQUIRE(local_set < local_plan);
	REQUIRE(local_plan < local_replay_epoch);
	REQUIRE(local_replay_epoch < local_commit);
	REQUIRE(local_commit < ice_start);
	REQUIRE(ice.find("if (same_generation)") != std::string::npos);
	REQUIRE(ice.find("SO_EXCLUSIVEADDRUSE") != std::string::npos);
	REQUIRE(ice.find("SO_REUSEADDR") == std::string::npos);
	REQUIRE(ice.find("socketBindConflict()") != std::string::npos);
	REQUIRE(ice.find("stunBindingResponseParse") != std::string::npos);
	REQUIRE(ice.find("sendto(s_Sock") != std::string::npos);
	REQUIRE(ice.find("probeSenderAllowed") != std::string::npos);
	REQUIRE(ice.find("netCandidateProbeValidate") != std::string::npos);
	const std::string actual_probe_length =
		"netCandidateProbeValidate(&probe, packet, (size_t)received,";
	const size_t receive_length_guard = ice.find(
		"received != (int)NET_CANDIDATE_PROBE_FRAME_LEN");
	const size_t first_actual_length = ice.find(actual_probe_length);
	const size_t second_actual_length = ice.find(actual_probe_length,
		first_actual_length + 1);
	REQUIRE(receive_length_guard != std::string::npos);
	REQUIRE(receive_length_guard < first_actual_length);
	REQUIRE(first_actual_length != std::string::npos);
	REQUIRE(second_actual_length != std::string::npos);
	REQUIRE(ice.find(actual_probe_length, second_actual_length + 1) ==
		std::string::npos);
	REQUIRE(ice.find(
		"netCandidateProbeValidate(&probe, packet, sizeof(packet),") ==
		std::string::npos);
	REQUIRE(ice.find("netCandidateProbeSourceMatches") != std::string::npos);
	REQUIRE(ice.find("P2P_ICE_PKT_LEN") == std::string::npos);
	REQUIRE(ice.find("p2pIceAddPeerCandidate") == std::string::npos);
	REQUIRE(stun.find("stunDiscoverAsync(ice_port)") == std::string::npos);
	REQUIRE(stun.find("p2pIceStunStart") != std::string::npos);
	REQUIRE(stun.find("s32 have_ice_port = getIcePort") != std::string::npos);
	REQUIRE(stun.find("p2pPublishMyReflexive") == std::string::npos);
	REQUIRE(netstun.find("SO_REUSEADDR") == std::string::npos);
	REQUIRE(netstun.find("CreateThread") == std::string::npos);
	REQUIRE(netstun.find("pthread_create") == std::string::npos);
	REQUIRE(netstun.find("recvfrom") == std::string::npos);
	REQUIRE(netstun.find("bind(") == std::string::npos);
	REQUIRE(netstun.find("transport-owned demux required") !=
		std::string::npos);
	REQUIRE(p2p_upnp.find("p2pInternalReportSuccess") == std::string::npos);
	REQUIRE(p2p_upnp.find("candidate mappings ready; continue to ICE") !=
		std::string::npos);
	REQUIRE(p2p_upnp.find("NET_UPNP_OWNER_SOCIAL") != std::string::npos);
	REQUIRE(p2p_upnp.find("P2P_DIRECT_PORT") == std::string::npos);
	REQUIRE(p2p_upnp.find("NET_DEFAULT_PORT") == std::string::npos);
	REQUIRE(upnp.find("UPNP_GetSpecificPortMappingEntry") != std::string::npos);
	REQUIRE(upnp.find("s_OwnerPorts[NET_UPNP_OWNER_COUNT]") !=
		std::string::npos);
	REQUIRE(upnp.find("rollbackNewMappings") != std::string::npos);
	REQUIRE(upnp.find("UPNP_DeletePortMapping") != std::string::npos);
	REQUIRE(upnp.find("pthread_detach") == std::string::npos);
	REQUIRE(upnp.find("CreateThread") != std::string::npos);
	REQUIRE(upnp.find("oldContainsPort") != std::string::npos);
	const size_t worker_begin = upnp.find("static s32 workerPerform");
	const size_t worker_end = upnp.find("static DWORD WINAPI upnpWorkerThread",
		worker_begin);
	const std::string worker_body = upnp.substr(worker_begin,
		worker_end - worker_begin);
	REQUIRE(worker_body.find("requestedContainsPort") == std::string::npos);
	REQUIRE(worker_body.find("deleteStaleMappingsAfterAcceptance") ==
		std::string::npos);
	const size_t commit = upnp.find("static void commitWorkerIfDone");
	const size_t owner_match = upnp.find("requestMatchesCurrentOwnerUnion(request)",
		commit);
	const size_t accepted_delete = upnp.find(
		"deleteStaleMappingsAfterAcceptance(request, &old_urls", commit);
	const size_t live_status_commit = upnp.find("s_UpnpStatus = next_status",
		commit);
	const size_t old_urls_free = upnp.find("FreeUPNPUrls(&old_urls)", commit);
	REQUIRE(commit < owner_match);
	REQUIRE(owner_match < live_status_commit);
	REQUIRE(live_status_commit < accepted_delete);
	REQUIRE(accepted_delete < old_urls_free);
	REQUIRE(p2p.find("socialMyHandle() == 0") != std::string::npos);
	REQUIRE(p2p.find("if (tier != P2P_TIER_ICE)") !=
		std::string::npos);
	REQUIRE(p2p.find("p2pDirectStart(p->pair_id") == std::string::npos);
	REQUIRE(direct.find("socket(") == std::string::npos);
	REQUIRE(direct.find("p2pInternalReportSuccess") == std::string::npos);
	REQUIRE(lan.find("p2pInternalReportSuccess") == std::string::npos);
	REQUIRE(turn.find("socket(") == std::string::npos);
	REQUIRE(turn.find("p2pInternalReportSuccess") == std::string::npos);
	REQUIRE(turn.find("relay allocation proof is not authenticated") !=
		std::string::npos);
	REQUIRE(p2p.find("p2pTurnStart(p->pair_id)") == std::string::npos);
	REQUIRE(p2p.find("p2pIceCancel(pair_id, peer_handle)") !=
		std::string::npos);
	REQUIRE(p2p.find("netCandidateSignalRetryPoll") != std::string::npos);
	REQUIRE(p2p.find("presenceSendCandidateRefresh(p->peer_handle)") !=
		std::string::npos);
	REQUIRE(presence.find("netCandidateExpiryHasHeadroom") !=
		std::string::npos);
	REQUIRE(presence.find("SO_EXCLUSIVEADDRUSE") != std::string::npos);
	REQUIRE(presence.find("SO_REUSEADDR") == std::string::npos);
	REQUIRE(p2p.find("p2pIceShutdown()") != std::string::npos);
	REQUIRE(presence.find("netStartClient") == std::string::npos);
	REQUIRE(ice.find("netStartClient") == std::string::npos);
	REQUIRE(stun.find("netStartClient") == std::string::npos);
	REQUIRE(p2p_upnp.find("netStartClient") == std::string::npos);
}
