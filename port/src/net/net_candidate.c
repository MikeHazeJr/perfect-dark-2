#include "net/net_candidate.h"

#include "net/net.h"
#include "sha256.h"

#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <stdio.h>
#endif

static void wU16(u8 *dst, u16 value)
{
	dst[0] = (u8)value;
	dst[1] = (u8)(value >> 8);
}

static void wU32(u8 *dst, u32 value)
{
	dst[0] = (u8)value;
	dst[1] = (u8)(value >> 8);
	dst[2] = (u8)(value >> 16);
	dst[3] = (u8)(value >> 24);
}

static u16 rU16(const u8 *src)
{
	return (u16)src[0] | ((u16)src[1] << 8);
}

static u32 rU32(const u8 *src)
{
	return (u32)src[0] | ((u32)src[1] << 8) |
		((u32)src[2] << 16) | ((u32)src[3] << 24);
}

static s32 bytesAreNonzero(const u8 *bytes, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		if (bytes[i] != 0) return 1;
	}
	return 0;
}

static s32 bytesEqual(const u8 *a, const u8 *b, size_t count)
{
	u8 different = 0;
	for (size_t i = 0; i < count; i++) different |= (u8)(a[i] ^ b[i]);
	return different == 0;
}

static s32 fixedAgentIsCanonical(const char *agent)
{
	s32 nul = -1;
	for (u32 i = 0; i < NET_CANDIDATE_AGENT_LEN; i++) {
		const u8 c = (u8)agent[i];
		if (c == 0) {
			nul = (s32)i;
			break;
		}
		if (c < 0x20u || c > 0x7eu) return 0;
	}
	if (nul <= 0) return 0;
	for (u32 i = (u32)nul + 1u; i < NET_CANDIDATE_AGENT_LEN; i++) {
		if (agent[i] != '\0') return 0;
	}
	return 1;
}

static s32 candidateEqual(const net_candidate_t *a,
	const net_candidate_t *b)
{
	return a && b && a->ipv4 == b->ipv4 && a->port == b->port &&
		a->type == b->type && a->provenance == b->provenance &&
		a->priority == b->priority;
}

static s32 candidateEndpointEqual(const net_candidate_t *a,
	const net_candidate_t *b)
{
	return a && b && a->ipv4 == b->ipv4 && a->port == b->port;
}

static s32 isRfc1918(u32 ipv4)
{
	const u8 first = (u8)(ipv4 >> 24);
	const u8 second = (u8)(ipv4 >> 16);
	return first == 10u ||
		(first == 172u && second >= 16u && second <= 31u) ||
		(first == 192u && second == 168u);
}

static s32 isSharedCgn(u32 ipv4)
{
	return (ipv4 & 0xffc00000u) == 0x64400000u; /* 100.64.0.0/10 */
}

static s32 isDocumentation(u32 ipv4)
{
	return (ipv4 >= 0xc0000200u && ipv4 <= 0xc00002ffu) ||
		(ipv4 >= 0xc6336400u && ipv4 <= 0xc63364ffu) ||
		(ipv4 >= 0xcb007100u && ipv4 <= 0xcb0071ffu);
}

static s32 isNonRouteable(u32 ipv4)
{
	const u8 first = (u8)(ipv4 >> 24);
	const u8 second = (u8)(ipv4 >> 16);
	const u8 third = (u8)(ipv4 >> 8);

	return ipv4 == 0 || ipv4 == 0xffffffffu || first == 0u ||
		first == 127u || first >= 224u ||
		(first == 169u && second == 254u) ||
		(first == 100u && second >= 64u && second <= 127u) ||
		(first == 192u && second == 0u) ||
		(first == 192u && second == 88u && third == 99u) ||
		(first == 198u && second >= 18u && second <= 19u);
}

s32 netCandidateIpv4IsHostRoute(u32 ipv4)
{
	/* Documentation space is never a candidate, regardless of the label a
	 * sender applies to it. This closes the HOST relabelling path for all
	 * three TEST-NET ranges as well as the public-route path below. */
	return netCandidateIpv4IsUnicast(ipv4) && !isNonRouteable(ipv4) &&
		!isDocumentation(ipv4);
}

s32 netCandidateIpv4IsPublicRoute(u32 ipv4)
{
	return netCandidateIpv4IsHostRoute(ipv4) && !isRfc1918(ipv4) &&
		!isSharedCgn(ipv4) && !isDocumentation(ipv4);
}

static u32 expectedPriority(u8 type)
{
	switch (type) {
	case NET_CANDIDATE_TYPE_HOST: return NET_CANDIDATE_PRIORITY_HOST;
	case NET_CANDIDATE_TYPE_UPNP: return NET_CANDIDATE_PRIORITY_UPNP;
	case NET_CANDIDATE_TYPE_STUN: return NET_CANDIDATE_PRIORITY_STUN;
	default: return 0;
	}
}

/* Canonical order is strongest transport first, then stable endpoint order. */
static s32 candidatePrecedes(const net_candidate_t *a,
	const net_candidate_t *b)
{
	if (a->priority != b->priority) return a->priority > b->priority;
	if (a->ipv4 != b->ipv4) return a->ipv4 < b->ipv4;
	if (a->port != b->port) return a->port < b->port;
	if (a->type != b->type) return a->type < b->type;
	return a->provenance < b->provenance;
}

s32 netCandidateIpv4IsUnicast(u32 ipv4)
{
	const u8 first = (u8)(ipv4 >> 24);
	return ipv4 != 0 && ipv4 != 0xffffffffu && first < 224u &&
		first != 127u;
}

s32 netCandidateRandomBytes(u8 *out, size_t len)
{
	if (!out && len != 0) return 0;
	if (len == 0) return 1;
#ifdef _WIN32
	if (len > (size_t)0xffffffffu) return 0;
	/* Resolve BCryptGenRandom dynamically so the self-contained pd-tests
	 * target does not acquire a new link-time library dependency. */
	HMODULE bcrypt = LoadLibraryA("bcrypt.dll");
	if (!bcrypt) return 0;
	typedef LONG (WINAPI *bcrypt_random_fn)(void *, unsigned char *,
		unsigned long, unsigned long);
	bcrypt_random_fn random_fn = (bcrypt_random_fn)GetProcAddress(bcrypt,
		"BCryptGenRandom");
	const s32 ok = random_fn && random_fn(NULL, out, (ULONG)len, 2u) == 0;
	FreeLibrary(bcrypt);
	return ok;
#else
	FILE *random_file = fopen("/dev/urandom", "rb");
	if (!random_file) return 0;
	const size_t read_count = fread(out, 1, len, random_file);
	fclose(random_file);
	return read_count == len ? 1 : 0;
#endif
}

s32 netCandidateTimestampIsFresh(u32 issued_unix_seconds,
	u32 expires_unix_seconds, u32 now_unix_seconds)
{
	if (issued_unix_seconds == 0 || expires_unix_seconds <= issued_unix_seconds) {
		return 0;
	}
	if (expires_unix_seconds - issued_unix_seconds >
		NET_CANDIDATE_MAX_LEASE_SECONDS) return 0;
	if (issued_unix_seconds > now_unix_seconds &&
		issued_unix_seconds - now_unix_seconds >
		NET_CANDIDATE_CLOCK_SKEW_SECONDS) return 0;
	if (now_unix_seconds > issued_unix_seconds &&
		now_unix_seconds - issued_unix_seconds >
		NET_CANDIDATE_FRESH_SECONDS) return 0;
	if (expires_unix_seconds < now_unix_seconds) return 0;
	return 1;
}

static s32 timeReached(u32 now_ms, u32 target_ms)
{
	return (s32)(now_ms - target_ms) >= 0;
}

void netCandidateProbeRetryInit(net_candidate_probe_retry_t *state,
	u32 now_ms)
{
	if (!state) return;
	memset(state, 0, sizeof(*state));
	state->next_send_ms = now_ms;
}

net_candidate_probe_retry_action_t netCandidateProbeRetryPoll(
	net_candidate_probe_retry_t *state, u32 now_ms, u32 deadline_ms)
{
	if (!state || timeReached(now_ms, deadline_ms) ||
		state->send_count >= NET_CANDIDATE_PROBE_MAX_SENDS) {
		return NET_CANDIDATE_PROBE_RETRY_EXHAUSTED;
	}
	if (!timeReached(now_ms, state->next_send_ms)) {
		return NET_CANDIDATE_PROBE_RETRY_WAIT;
	}
	state->send_count++;
	state->next_send_ms = now_ms + NET_CANDIDATE_PROBE_RETRY_INTERVAL_MS;
	return NET_CANDIDATE_PROBE_RETRY_SEND;
}

void netCandidateSignalRetryInit(net_candidate_signal_retry_t *state,
	u32 now_ms)
{
	if (!state) return;
	memset(state, 0, sizeof(*state));
	state->next_send_ms = now_ms;
}

net_candidate_signal_retry_action_t netCandidateSignalRetryPoll(
	net_candidate_signal_retry_t *state, u32 now_ms, u32 deadline_ms)
{
	if (!state || timeReached(now_ms, deadline_ms) ||
		state->send_count >= NET_CANDIDATE_SIGNAL_MAX_SENDS) {
		return NET_CANDIDATE_SIGNAL_RETRY_EXHAUSTED;
	}
	if (!timeReached(now_ms, state->next_send_ms)) {
		return NET_CANDIDATE_SIGNAL_RETRY_WAIT;
	}
	state->send_count++;
	state->next_send_ms = now_ms + NET_CANDIDATE_SIGNAL_RETRY_INTERVAL_MS;
	return NET_CANDIDATE_SIGNAL_RETRY_SEND;
}

s32 netCandidateExpiryHasHeadroom(u32 expires_unix_seconds,
	u32 now_unix_seconds, u32 required_seconds)
{
	return expires_unix_seconds >= now_unix_seconds &&
		expires_unix_seconds - now_unix_seconds >= required_seconds;
}

net_candidate_probe_replay_result_t netCandidateProbeReplayRecord(
	net_candidate_probe_replay_t *entries, u32 entry_count,
	u32 nonce, u32 source_ipv4, u16 source_port)
{
	if (!entries || entry_count == 0 || nonce == 0 || source_ipv4 == 0 ||
		source_port == 0) return NET_CANDIDATE_PROBE_REPLAY_REJECT;

	net_candidate_probe_replay_t *free_entry = NULL;
	for (u32 i = 0; i < entry_count; i++) {
		net_candidate_probe_replay_t *entry = &entries[i];
		if (!entry->in_use) {
			if (!free_entry) free_entry = entry;
			continue;
		}
		if (entry->nonce != nonce) continue;
		return entry->source_ipv4 == source_ipv4 &&
			entry->source_port == source_port
			? NET_CANDIDATE_PROBE_REPLAY_DUPLICATE
			: NET_CANDIDATE_PROBE_REPLAY_REJECT;
	}
	if (!free_entry) return NET_CANDIDATE_PROBE_REPLAY_REJECT;
	memset(free_entry, 0, sizeof(*free_entry));
	free_entry->nonce = nonce;
	free_entry->source_ipv4 = source_ipv4;
	free_entry->source_port = source_port;
	free_entry->in_use = 1;
	return NET_CANDIDATE_PROBE_REPLAY_NEW;
}

s32 netCandidateSetNormalize(const net_candidate_set_t *wire,
	u32 expected_sender_handle, u32 expected_target_handle,
	u32 now_unix_seconds, net_candidate_set_t *out)
{
	if (!wire || !out || wire->sender_handle == 0 ||
		wire->target_handle == 0 ||
		wire->sender_handle == wire->target_handle ||
		(wire->kind != NET_CANDIDATE_FRAME_PUBLISH &&
			wire->kind != NET_CANDIDATE_FRAME_RETIRE) || wire->flags != 0 ||
		wire->generation == 0 || wire->proto_version != NET_PROTOCOL_VER ||
		!netCandidateTimestampIsFresh(wire->issued_unix_seconds,
			wire->expires_unix_seconds, now_unix_seconds) ||
		!bytesAreNonzero(wire->credential, sizeof(wire->credential)) ||
		!fixedAgentIsCanonical(wire->agent_name)) {
		return 0;
	}
	if (expected_sender_handle != 0 &&
		wire->sender_handle != expected_sender_handle) return 0;
	if (expected_target_handle != 0 &&
		wire->target_handle != expected_target_handle) return 0;
	if (wire->candidate_count > NET_CANDIDATE_MAX) return 0;
	if (wire->kind == NET_CANDIDATE_FRAME_RETIRE) {
		if (wire->candidate_count != 0) return 0;
	} else if (wire->candidate_count == 0) {
		return 0;
	}

	for (u32 i = 0; i < wire->candidate_count; i++) {
		const net_candidate_t *c = &wire->candidates[i];
		const u32 priority = expectedPriority(c->type);
		const s32 address_ok = c->type == NET_CANDIDATE_TYPE_HOST
			? netCandidateIpv4IsHostRoute(c->ipv4)
			: netCandidateIpv4IsPublicRoute(c->ipv4);
		if (!address_ok || c->port == 0 || c->provenance != c->type ||
			priority == 0 || c->priority != priority) return 0;
		for (u32 j = 0; j < i; j++) {
			/* An endpoint may not be relabelled or silently collapsed by ICE. */
			if (candidateEndpointEqual(c, &wire->candidates[j])) return 0;
		}
	}

	*out = *wire;
	memset(out->_pad, 0, sizeof(out->_pad));
	for (u32 i = 1; i < out->candidate_count; i++) {
		const net_candidate_t key = out->candidates[i];
		u32 j = i;
		while (j > 0 && candidatePrecedes(&key, &out->candidates[j - 1])) {
			out->candidates[j] = out->candidates[j - 1];
			j--;
		}
		out->candidates[j] = key;
	}
	if (out->candidate_count < NET_CANDIDATE_MAX) {
		memset(&out->candidates[out->candidate_count], 0,
			(NET_CANDIDATE_MAX - out->candidate_count) *
			sizeof(out->candidates[0]));
	}
	return 1;
}

s32 netCandidateSetEqual(const net_candidate_set_t *a,
	const net_candidate_set_t *b)
{
	if (!a || !b || a->candidate_count > NET_CANDIDATE_MAX ||
		b->candidate_count > NET_CANDIDATE_MAX ||
		a->sender_handle != b->sender_handle ||
		a->target_handle != b->target_handle ||
		a->proto_version != b->proto_version || a->kind != b->kind ||
		a->flags != b->flags || a->generation != b->generation ||
		a->issued_unix_seconds != b->issued_unix_seconds ||
		a->expires_unix_seconds != b->expires_unix_seconds ||
		a->candidate_count != b->candidate_count ||
		memcmp(a->credential, b->credential, sizeof(a->credential)) != 0 ||
		memcmp(a->agent_name, b->agent_name, sizeof(a->agent_name)) != 0) {
		return 0;
	}
	for (u32 i = 0; i < a->candidate_count; i++) {
		if (!candidateEqual(&a->candidates[i], &b->candidates[i])) return 0;
	}
	return 1;
}

static s32 peerStateContentEqual(const net_candidate_peer_state_t *prior,
	const net_candidate_set_t *incoming)
{
	if (prior->candidate_count != incoming->candidate_count ||
		prior->expires_unix_seconds != incoming->expires_unix_seconds ||
		memcmp(prior->credential, incoming->credential,
			sizeof(prior->credential)) != 0) return 0;
	for (u32 i = 0; i < incoming->candidate_count; i++) {
		if (!candidateEqual(&prior->candidates[i], &incoming->candidates[i])) {
			return 0;
		}
	}
	return 1;
}

net_candidate_update_t netCandidatePlanUpdate(
	const net_candidate_peer_state_t *prior,
	const net_candidate_set_t *incoming)
{
	if (!incoming || incoming->generation == 0 ||
		incoming->candidate_count > NET_CANDIDATE_MAX ||
		(prior && (prior->generation == 0 ||
			prior->candidate_count > NET_CANDIDATE_MAX))) {
		return NET_CANDIDATE_UPDATE_REJECT;
	}
	if (!prior) {
		return incoming->kind == NET_CANDIDATE_FRAME_RETIRE
			? NET_CANDIDATE_UPDATE_RETIRE : NET_CANDIDATE_UPDATE_ACCEPT;
	}

	if (incoming->generation > prior->generation) {
		/* Credential rotation is mandatory for every generation. */
		if (bytesEqual(prior->credential, incoming->credential,
			sizeof(prior->credential))) return NET_CANDIDATE_UPDATE_REJECT;
		return incoming->kind == NET_CANDIDATE_FRAME_RETIRE
			? NET_CANDIDATE_UPDATE_RETIRE : NET_CANDIDATE_UPDATE_ACCEPT;
	}
	if (incoming->generation < prior->generation) {
		/* Generation is a live-state monotonic counter. A process restart starts
		 * at generation 1 with a fresh random credential, but it must not replace
		 * a still-live higher generation: otherwise an old generation-1 frame can
		 * be replayed after a wrap. Callers may discard an expired prior state
		 * before planning, which gives a restarted sender a clean epoch. */
		return NET_CANDIDATE_UPDATE_REJECT;
	}
	if (incoming->issued_unix_seconds < prior->issued_unix_seconds ||
		!peerStateContentEqual(prior, incoming)) return NET_CANDIDATE_UPDATE_REJECT;
	if (incoming->issued_unix_seconds == prior->issued_unix_seconds) {
		return NET_CANDIDATE_UPDATE_DUPLICATE;
	}
	return incoming->kind == NET_CANDIDATE_FRAME_RETIRE
		? NET_CANDIDATE_UPDATE_RETIRE : NET_CANDIDATE_UPDATE_ACCEPT;
}

void netCandidatePeerStateCommit(net_candidate_peer_state_t *state,
	const net_candidate_set_t *set)
{
	if (!state || !set || set->candidate_count > NET_CANDIDATE_MAX) return;
	memset(state, 0, sizeof(*state));
	state->generation = set->generation;
	state->issued_unix_seconds = set->issued_unix_seconds;
	state->expires_unix_seconds = set->expires_unix_seconds;
	memcpy(state->credential, set->credential, sizeof(state->credential));
	state->candidate_count = set->candidate_count;
	state->active = set->kind == NET_CANDIDATE_FRAME_PUBLISH ? 1 : 0;
	for (u32 i = 0; i < set->candidate_count; i++) {
		state->candidates[i] = set->candidates[i];
	}
}

s32 netCandidateFrameEncodeBody(const net_candidate_set_t *set,
	const u8 pubkey[32], u8 out_body[NET_CANDIDATE_BODY_LEN])
{
	if (!set || !pubkey || !out_body || set->candidate_count > NET_CANDIDATE_MAX) {
		return 0;
	}
	memset(out_body, 0, NET_CANDIDATE_BODY_LEN);
	memcpy(out_body, NET_CANDIDATE_MAGIC, NET_CANDIDATE_MAGIC_LEN);
	out_body[5] = NET_CANDIDATE_VERSION;
	out_body[6] = set->kind;
	out_body[7] = set->flags;
	wU32(out_body + 8, set->sender_handle);
	wU32(out_body + 12, set->target_handle);
	wU16(out_body + 16, set->proto_version);
	wU32(out_body + 18, set->generation);
	wU32(out_body + 22, set->issued_unix_seconds);
	wU32(out_body + 26, set->expires_unix_seconds);
	out_body[30] = set->candidate_count;
	memcpy(out_body + 32, set->credential, NET_CANDIDATE_CREDENTIAL_LEN);
	/* Caller-owned unused slots are intentionally ignored. The initial memset
	 * above makes every unused wire record canonical zero. */
	for (u32 i = 0; i < set->candidate_count; i++) {
		const size_t off = 48u + i * NET_CANDIDATE_RECORD_LEN;
		const net_candidate_t *c = &set->candidates[i];
		wU32(out_body + off, c->ipv4);
		wU16(out_body + off + 4, c->port);
		out_body[off + 6] = c->type;
		out_body[off + 7] = c->provenance;
		wU32(out_body + off + 8, c->priority);
	}
	memcpy(out_body + 144, set->agent_name, NET_CANDIDATE_AGENT_LEN);
	memcpy(out_body + NET_CANDIDATE_PUBKEY_OFFSET, pubkey, 32);
	return 1;
}

s32 netCandidateFrameDecode(const u8 *frame, size_t frame_len,
	net_candidate_set_t *out, const u8 **out_pubkey,
	const u8 **out_signature)
{
	if (!frame || frame_len != NET_CANDIDATE_FRAME_LEN || !out ||
		!out_pubkey || !out_signature ||
		memcmp(frame, NET_CANDIDATE_MAGIC, NET_CANDIDATE_MAGIC_LEN) != 0 ||
		frame[5] != NET_CANDIDATE_VERSION || frame[7] != 0 ||
		(frame[6] != NET_CANDIDATE_FRAME_PUBLISH &&
			frame[6] != NET_CANDIDATE_FRAME_RETIRE) ||
		frame[30] > NET_CANDIDATE_MAX || frame[31] != 0) {
		return 0;
	}
	memset(out, 0, sizeof(*out));
	out->kind = frame[6];
	out->flags = frame[7];
	out->sender_handle = rU32(frame + 8);
	out->target_handle = rU32(frame + 12);
	out->proto_version = rU16(frame + 16);
	out->generation = rU32(frame + 18);
	out->issued_unix_seconds = rU32(frame + 22);
	out->expires_unix_seconds = rU32(frame + 26);
	out->candidate_count = frame[30];
	memcpy(out->credential, frame + 32, NET_CANDIDATE_CREDENTIAL_LEN);
	for (u32 i = 0; i < NET_CANDIDATE_MAX; i++) {
		const size_t off = 48u + i * NET_CANDIDATE_RECORD_LEN;
		if (i >= out->candidate_count) {
			for (u32 j = 0; j < NET_CANDIDATE_RECORD_LEN; j++) {
				if (frame[off + j] != 0) return 0;
			}
			continue;
		}
		net_candidate_t *c = &out->candidates[i];
		c->ipv4 = rU32(frame + off);
		c->port = rU16(frame + off + 4);
		c->type = frame[off + 6];
		c->provenance = frame[off + 7];
		c->priority = rU32(frame + off + 8);
	}
	memcpy(out->agent_name, frame + 144, NET_CANDIDATE_AGENT_LEN);
	*out_pubkey = frame + NET_CANDIDATE_PUBKEY_OFFSET;
	*out_signature = frame + NET_CANDIDATE_SIG_OFFSET;
	return 1;
}

static void hmacSha256(const u8 *key, size_t key_len, const u8 *data,
	size_t data_len, u8 out[SHA256_DIGEST_SIZE])
{
	u8 key_block[SHA256_BLOCK_SIZE];
	u8 inner[SHA256_DIGEST_SIZE];
	memset(key_block, 0, sizeof(key_block));
	if (key_len > sizeof(key_block)) {
		sha256Hash(key, key_len, key_block);
	} else {
		memcpy(key_block, key, key_len);
	}
	for (u32 i = 0; i < sizeof(key_block); i++) key_block[i] ^= 0x36u;
	sha256_ctx ctx;
	sha256Init(&ctx);
	sha256Update(&ctx, key_block, sizeof(key_block));
	sha256Update(&ctx, data, data_len);
	sha256Final(&ctx, inner);
	for (u32 i = 0; i < sizeof(key_block); i++) key_block[i] ^= 0x36u ^ 0x5cu;
	sha256Init(&ctx);
	sha256Update(&ctx, key_block, sizeof(key_block));
	sha256Update(&ctx, inner, sizeof(inner));
	sha256Final(&ctx, out);
	memset(key_block, 0, sizeof(key_block));
	memset(inner, 0, sizeof(inner));
}

s32 netCandidateProbeEncode(const net_candidate_probe_t *probe,
	const u8 key[NET_CANDIDATE_CREDENTIAL_LEN],
	u8 out_frame[NET_CANDIDATE_PROBE_FRAME_LEN])
{
	if (!probe || !key || !out_frame ||
		(probe->kind != NET_CANDIDATE_PROBE_KIND_PROBE &&
			probe->kind != NET_CANDIDATE_PROBE_KIND_ACK) ||
		probe->sender_handle == 0 || probe->target_handle == 0 ||
		probe->sender_handle == probe->target_handle ||
		probe->candidate_owner_handle == 0 || probe->generation == 0 ||
		probe->nonce == 0 || !bytesAreNonzero(probe->credential,
			sizeof(probe->credential)) ||
		!bytesAreNonzero(key, NET_CANDIDATE_CREDENTIAL_LEN) ||
		!netCandidateIpv4IsHostRoute(probe->candidate_ipv4) ||
		probe->candidate_port == 0 || probe->candidate_index >= NET_CANDIDATE_MAX) {
		return 0;
	}
	memset(out_frame, 0, NET_CANDIDATE_PROBE_FRAME_LEN);
	memcpy(out_frame, NET_CANDIDATE_PROBE_MAGIC,
		NET_CANDIDATE_PROBE_MAGIC_LEN);
	out_frame[5] = NET_CANDIDATE_PROBE_VERSION;
	out_frame[6] = probe->kind;
	wU32(out_frame + 8, probe->sender_handle);
	wU32(out_frame + 12, probe->target_handle);
	wU32(out_frame + 16, probe->candidate_owner_handle);
	wU32(out_frame + 20, probe->generation);
	wU32(out_frame + 24, probe->nonce);
	memcpy(out_frame + 28, probe->credential, NET_CANDIDATE_CREDENTIAL_LEN);
	wU32(out_frame + 44, probe->candidate_ipv4);
	wU16(out_frame + 48, probe->candidate_port);
	out_frame[50] = probe->candidate_index;
	/* out_frame[7] and [51] remain reserved zero. */
	hmacSha256(key, NET_CANDIDATE_CREDENTIAL_LEN, out_frame,
		NET_CANDIDATE_PROBE_TAG_OFFSET,
		out_frame + NET_CANDIDATE_PROBE_TAG_OFFSET);
	return 1;
}

s32 netCandidateProbeDecode(const u8 *frame, size_t frame_len,
	net_candidate_probe_t *out)
{
	if (!frame || frame_len != NET_CANDIDATE_PROBE_FRAME_LEN || !out ||
		memcmp(frame, NET_CANDIDATE_PROBE_MAGIC,
			NET_CANDIDATE_PROBE_MAGIC_LEN) != 0 ||
		frame[5] != NET_CANDIDATE_PROBE_VERSION || frame[7] != 0 ||
		frame[51] != 0 ||
		(frame[6] != NET_CANDIDATE_PROBE_KIND_PROBE &&
			frame[6] != NET_CANDIDATE_PROBE_KIND_ACK)) return 0;
	memset(out, 0, sizeof(*out));
	out->kind = frame[6];
	out->sender_handle = rU32(frame + 8);
	out->target_handle = rU32(frame + 12);
	out->candidate_owner_handle = rU32(frame + 16);
	out->generation = rU32(frame + 20);
	out->nonce = rU32(frame + 24);
	memcpy(out->credential, frame + 28, NET_CANDIDATE_CREDENTIAL_LEN);
	out->candidate_ipv4 = rU32(frame + 44);
	out->candidate_port = rU16(frame + 48);
	out->candidate_index = frame[50];
	if (out->sender_handle == 0 || out->target_handle == 0 ||
		out->sender_handle == out->target_handle ||
		out->candidate_owner_handle == 0 || out->generation == 0 ||
		out->nonce == 0 || !bytesAreNonzero(out->credential,
		sizeof(out->credential)) ||
		!netCandidateIpv4IsHostRoute(out->candidate_ipv4) ||
		out->candidate_port == 0 || out->candidate_index >= NET_CANDIDATE_MAX) {
		return 0;
	}
	return 1;
}

s32 netCandidateProbeAuthenticate(const u8 *frame, size_t frame_len,
	const u8 key[NET_CANDIDATE_CREDENTIAL_LEN])
{
	if (!frame || !key || frame_len != NET_CANDIDATE_PROBE_FRAME_LEN ||
		!bytesAreNonzero(key, NET_CANDIDATE_CREDENTIAL_LEN)) return 0;
	net_candidate_probe_t probe;
	if (!netCandidateProbeDecode(frame, frame_len, &probe)) return 0;
	u8 expected[NET_CANDIDATE_PROBE_TAG_LEN];
	hmacSha256(key, NET_CANDIDATE_CREDENTIAL_LEN, frame,
		NET_CANDIDATE_PROBE_TAG_OFFSET, expected);
	return bytesEqual(expected, frame + NET_CANDIDATE_PROBE_TAG_OFFSET,
		NET_CANDIDATE_PROBE_TAG_LEN);
}

s32 netCandidateProbeSourceMatches(const net_candidate_probe_t *probe,
	u32 source_ipv4, u16 source_port)
{
	return probe && source_ipv4 != 0 && source_port != 0 &&
		probe->candidate_ipv4 == source_ipv4 &&
		probe->candidate_port == source_port;
}

s32 netCandidateProbeValidate(const net_candidate_probe_t *probe,
	const u8 *frame, size_t frame_len,
	const u8 key[NET_CANDIDATE_CREDENTIAL_LEN],
	u32 expected_sender_handle, u32 expected_target_handle,
	u32 expected_owner_handle, u32 expected_generation,
	const u8 expected_credential[NET_CANDIDATE_CREDENTIAL_LEN],
	u32 expires_unix_seconds, u32 now_unix_seconds, u32 prior_nonce)
{
	if (!probe || !frame || !key || !expected_credential ||
		expected_sender_handle == 0 || expected_target_handle == 0 ||
		expected_owner_handle == 0 || expected_generation == 0 ||
		!netCandidateProbeAuthenticate(frame, frame_len, key)) return 0;
	net_candidate_probe_t decoded;
	if (!netCandidateProbeDecode(frame, frame_len, &decoded) ||
		decoded.kind != probe->kind ||
		decoded.sender_handle != probe->sender_handle ||
		decoded.target_handle != probe->target_handle ||
		decoded.candidate_owner_handle != probe->candidate_owner_handle ||
		decoded.generation != probe->generation || decoded.nonce != probe->nonce ||
		decoded.candidate_ipv4 != probe->candidate_ipv4 ||
		decoded.candidate_port != probe->candidate_port ||
		decoded.candidate_index != probe->candidate_index ||
		!bytesEqual(decoded.credential, probe->credential,
			sizeof(decoded.credential)) ||
		decoded.sender_handle != expected_sender_handle ||
		decoded.target_handle != expected_target_handle ||
		decoded.candidate_owner_handle != expected_owner_handle ||
		decoded.generation != expected_generation ||
		!bytesEqual(decoded.credential, expected_credential,
			sizeof(decoded.credential)) || expires_unix_seconds == 0 ||
		now_unix_seconds > expires_unix_seconds ||
		(prior_nonce != 0 && decoded.nonce == prior_nonce)) {
		return 0;
	}
	return 1;
}
