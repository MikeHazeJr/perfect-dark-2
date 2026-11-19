#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "grid_archive_policy.h"
#include "grid_match_package_io.h"
#include "net/grid_mod_proposal.h"
#include "net/net.h"

#define GRID_MOD_PROPOSAL_WIRE_VERSION 1
#define GRID_MOD_PROPOSAL_PREFIX_SIZE (sizeof(u64) + 2u)
#define GRID_MOD_PROPOSAL_STATUS_SIZE (1u + sizeof(u64) + SHA256_HEX_SIZE - 1u + 1u)

struct grid_mod_proposal_host {
	grid_mod_proposal_peer_t peer;
	grid_mod_proposal_control_t proposal;
	grid_mod_proposal_control_t request;
	grid_match_cache_t *cache;
	grid_match_transfer_t *transfer;
	u16 ordinal;
};

struct grid_mod_proposal_donor {
	grid_mod_proposal_peer_t peer;
	grid_match_offer_t offer;
	grid_match_offer_t requested;
	grid_match_shared_packages_t allowed;
	grid_match_shared_packages_t missing;
	grid_match_cache_t *cache;
};

static s32 proposalError(char *err, size_t cap, const char *message)
{
	if (err && cap) snprintf(err, cap, "%s", message);
	return 0;
}

static s32 identityValid(const grid_match_offer_t *offer)
{
	return offer->room_id != 0xff && offer->generation
		&& offer->manifest_digest[SHA256_HEX_SIZE - 1] == '\0'
		&& strspn(offer->manifest_digest, "0123456789abcdef") == SHA256_HEX_SIZE - 1
		&& strspn(offer->manifest_digest, "0") != SHA256_HEX_SIZE - 1;
}

static s32 sameSources(const grid_match_offer_t *first, const grid_match_offer_t *second)
{
	return first->room_id == second->room_id
		&& memcmp(first->manifest_digest, second->manifest_digest, SHA256_HEX_SIZE) == 0;
}

static s32 sameOffer(const grid_match_offer_t *first, const grid_match_offer_t *second)
{
	return first->generation == second->generation && sameSources(first, second);
}

static s32 peerValid(const grid_mod_proposal_peer_t *peer, const grid_match_offer_t *offer)
{
	return peer && peer->room_id == offer->room_id && peer->room_lifetime && peer->connection;
}

static s32 samePeer(const grid_mod_proposal_peer_t *first, const grid_mod_proposal_peer_t *second)
{
	return second && first->room_id == second->room_id && first->client_id == second->client_id
		&& first->room_lifetime == second->room_lifetime && first->connection == second->connection;
}

static s32 packagesValid(const grid_match_shared_packages_t *packages, size_t bound)
{
	if (!packages || !packages->count || packages->count > GRID_MATCH_MAX_SHARED_PACKAGES) return 0;
	for (size_t i = 0; i < packages->count; i++) {
		if (packages->indexes[i] >= bound
				|| (i && packages->indexes[i - 1] >= packages->indexes[i])) return 0;
	}
	return 1;
}

static s32 packagesAllowed(const grid_match_shared_packages_t *requested,
	const grid_match_shared_packages_t *allowed)
{
	if (!packagesValid(requested, MANIFEST_MAX_ENTRIES)) return 0;
	size_t permitted = 0;
	for (size_t i = 0; i < requested->count; i++) {
		while (permitted < allowed->count && allowed->indexes[permitted] < requested->indexes[i]) {
			permitted++;
		}
		if (permitted == allowed->count || allowed->indexes[permitted] != requested->indexes[i]) return 0;
		permitted++;
	}
	return 1;
}

static s32 sourcesEmpty(const grid_match_sources_t *sources)
{
	return !sources->entries && !sources->packages && !sources->entry_count && !sources->package_count
		&& !sources->total_bytes && !sources->digest[0];
}

static s32 controlValid(const grid_mod_proposal_control_t *control, char *err, size_t err_cap)
{
	if (!control || !control->proposal_serial || !identityValid(&control->offer) || control->accepted > 1
			|| (control->kind != GRID_MOD_PROPOSAL_RESULT && control->accepted)) {
		return proposalError(err, err_cap, "invalid Grid proposal control identity");
	}
	if (control->kind == GRID_MOD_PROPOSAL_OFFER) {
		grid_match_sources_t sealed = control->sources;
		if (!gridMatchSourcesSeal(&sealed, err, err_cap)) return 0;
		if (control->offer.generation != control->proposal_serial
				|| sealed.total_bytes != control->sources.total_bytes
				|| !packagesValid(&control->packages, sealed.package_count)
				|| memcmp(sealed.digest, control->sources.digest, SHA256_HEX_SIZE) != 0
				|| memcmp(sealed.digest, control->offer.manifest_digest, SHA256_HEX_SIZE) != 0
				|| gridMatchSourcesFiestaCount(&sealed)) {
			return proposalError(err, err_cap, "Grid proposal requires an exact source-only offer and consented packages");
		}
		for (size_t i = 0; i < control->packages.count; i++) {
			const grid_match_source_package_t *package = &sealed.packages[control->packages.indexes[i]];
			if (package->bundled || strncmp(package->owner.id, "base:", 5) == 0
					|| !gridMatchPackageTypeShareable(package->owner.type, package->suffix)) {
				return proposalError(err, err_cap, "Grid proposal grants a package that cannot be shared");
			}
		}
		return 1;
	}
	if (!sourcesEmpty(&control->sources)) {
		return proposalError(err, err_cap, "only a Grid proposal offer carries sources");
	}
	if (control->kind == GRID_MOD_PROPOSAL_REQUEST) {
		if (control->offer.generation != control->proposal_serial
				&& packagesValid(&control->packages, MANIFEST_MAX_ENTRIES)) return 1;
	} else if (!control->packages.count
			&& (control->kind == GRID_MOD_PROPOSAL_CANCEL || control->kind == GRID_MOD_PROPOSAL_RESULT)
			&& (!control->accepted || control->offer.generation != control->proposal_serial)) {
		return 1;
	}
	return proposalError(err, err_cap, "invalid Grid proposal control payload");
}

void gridModProposalControlClear(grid_mod_proposal_control_t *control)
{
	if (!control) return;
	gridMatchSourcesFree(&control->sources);
	memset(control, 0, sizeof(*control));
}

s32 gridModProposalControlRead(struct netbuf *src, grid_mod_proposal_control_t *out,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!src || !src->data || src->error || src->rp > src->wp || src->wp > src->size
			|| src->wp - src->rp > NET_BUFSIZE - 1u || !out) {
		return proposalError(err, err_cap, "invalid Grid proposal packet");
	}
	struct netbuf wire = *src;
	grid_mod_proposal_control_t candidate = {0};
	candidate.proposal_serial = netbufReadU64(&wire);
	const u8 version = netbufReadU8(&wire);
	candidate.kind = (grid_mod_proposal_kind_e)netbufReadU8(&wire);
	if (wire.error || version != GRID_MOD_PROPOSAL_WIRE_VERSION || !candidate.proposal_serial
			|| candidate.kind < GRID_MOD_PROPOSAL_OFFER || candidate.kind > GRID_MOD_PROPOSAL_RESULT) goto malformed;
	if (candidate.kind == GRID_MOD_PROPOSAL_OFFER) {
		if (!gridMatchSharedPackagesRead(&wire, &candidate.packages)
				|| !gridMatchOfferRead(&wire, &candidate.offer, &candidate.sources, err, err_cap)) goto malformed;
	} else {
		if (!gridMatchStatusRead(&wire, &candidate.offer, &candidate.accepted)) goto malformed;
		if (candidate.kind == GRID_MOD_PROPOSAL_REQUEST
				&& !gridMatchSharedPackagesRead(&wire, &candidate.packages)) goto malformed;
	}
	if (wire.rp != wire.wp || !controlValid(&candidate, err, err_cap)) goto malformed;
	gridModProposalControlClear(out);
	*out = candidate;
	*src = wire;
	return 1;

malformed:
	gridModProposalControlClear(&candidate);
	if (err && err_cap && !err[0]) proposalError(err, err_cap, "invalid Grid proposal payload");
	return 0;
}

s32 gridModProposalControlWrite(struct netbuf *dst, const grid_mod_proposal_control_t *control,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!dst || !dst->data || dst->error || dst->wp > dst->size) {
		return proposalError(err, err_cap, "invalid Grid proposal packet destination");
	}
	if (!controlValid(control, err, err_cap)) return 0;
	size_t cap = dst->size - dst->wp;
	size_t limit = NET_BUFSIZE - 1u;
	if (control->kind != GRID_MOD_PROPOSAL_OFFER) {
		limit = GRID_MOD_PROPOSAL_PREFIX_SIZE + GRID_MOD_PROPOSAL_STATUS_SIZE;
		if (control->kind == GRID_MOD_PROPOSAL_REQUEST) {
			limit += sizeof(u16) + sizeof(u16) * control->packages.count;
		}
	}
	if (cap > limit) cap = limit;
	if (cap < GRID_MOD_PROPOSAL_PREFIX_SIZE) {
		return proposalError(err, err_cap, "Grid proposal exceeds the packet capacity");
	}
	u8 *body = malloc(cap);
	if (!body) return proposalError(err, err_cap, "out of memory writing Grid proposal");
	struct netbuf wire = { .data = body, .size = cap };
	netbufWriteU64(&wire, control->proposal_serial);
	netbufWriteU8(&wire, GRID_MOD_PROPOSAL_WIRE_VERSION);
	netbufWriteU8(&wire, (u8)control->kind);
	s32 ok;
	if (control->kind == GRID_MOD_PROPOSAL_OFFER) {
		ok = gridMatchSharedPackagesWrite(&wire, &control->packages)
			&& gridMatchOfferWrite(&wire, &control->offer, &control->sources, err, err_cap);
	} else {
		ok = gridMatchStatusWrite(&wire, &control->offer, control->accepted)
			&& (control->kind != GRID_MOD_PROPOSAL_REQUEST || gridMatchSharedPackagesWrite(&wire, &control->packages));
	}
	if (ok) {
		memcpy(dst->data + dst->wp, body, wire.wp);
		dst->wp += wire.wp;
	} else if (err && err_cap && !err[0]) {
		proposalError(err, err_cap, "Grid proposal exceeds the packet capacity");
	}
	free(body);
	return ok;
}

s32 gridModProposalHostBegin(grid_mod_proposal_host_t **host, const grid_mod_proposal_peer_t *peer,
	grid_mod_proposal_control_t *proposal, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!host || *host || !proposal || proposal->kind != GRID_MOD_PROPOSAL_OFFER
			|| !peerValid(peer, &proposal->offer)) {
		return proposalError(err, err_cap, "invalid Room proposal or another proposal is pending");
	}
	if (!controlValid(proposal, err, err_cap)) return 0;
	grid_mod_proposal_host_t *candidate = calloc(1, sizeof(*candidate));
	if (!candidate) return proposalError(err, err_cap, "out of memory holding Grid proposal");
	candidate->peer = *peer;
	candidate->proposal = *proposal;
	memset(proposal, 0, sizeof(*proposal));
	*host = candidate;
	return 1;
}

const grid_mod_proposal_control_t *gridModProposalHostGet(const grid_mod_proposal_host_t *host,
	const grid_mod_proposal_peer_t *peer)
{
	return host && samePeer(&host->peer, peer) ? &host->proposal : NULL;
}

const grid_mod_proposal_control_t *gridModProposalHostRequest(const grid_mod_proposal_host_t *host,
	const grid_mod_proposal_peer_t *peer)
{
	return host && samePeer(&host->peer, peer) && host->request.kind ? &host->request : NULL;
}

s32 gridModProposalHostApprove(grid_mod_proposal_host_t *host, const grid_mod_proposal_peer_t *peer,
	u64 host_nonce, const grid_match_shared_packages_t *missing,
	grid_mod_proposal_load_package_fn load_installed, void *userdata,
	grid_mod_proposal_control_t *request, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	const grid_mod_proposal_control_t *proposal = gridModProposalHostGet(host, peer);
	if (!proposal || host->request.kind || !host_nonce || host_nonce == proposal->proposal_serial
			|| !request || !sourcesEmpty(&request->sources) || !packagesAllowed(missing, &proposal->packages)) {
		return proposalError(err, err_cap, "Grid proposal is stale or asks for packages outside the relay permission");
	}
	grid_mod_proposal_control_t approved = {
		.kind = GRID_MOD_PROPOSAL_REQUEST,
		.proposal_serial = proposal->proposal_serial,
		.offer = proposal->offer,
		.packages = *missing,
	};
	approved.offer.generation = host_nonce;
	grid_match_cache_t *cache = NULL;
	if (!gridMatchCacheBegin(&cache, &approved.offer, &proposal->sources, err, err_cap)) return 0;

	size_t missing_ordinal = 0;
	for (size_t i = 0; i < proposal->sources.package_count; i++) {
		if (missing_ordinal < missing->count && i == missing->indexes[missing_ordinal]) {
			missing_ordinal++;
			continue;
		}
		u32 size = 0;
		void *bytes = load_installed ? load_installed(&proposal->sources.packages[i], &size, userdata) : NULL;
		const s32 ok = bytes && gridMatchCacheAccept(cache, &approved.offer, i, bytes, size, err, err_cap);
		free(bytes);
		if (!ok) {
			gridMatchCacheReset(&cache);
			if (err && err_cap && !err[0]) proposalError(err, err_cap, "an unrequested exact Grid dependency is unavailable");
			return 0;
		}
	}
	const u16 first = missing->indexes[0];
	if (!gridMatchTransferBegin(&host->transfer, &approved.offer, first, proposal->sources.packages[first].size)) {
		gridMatchCacheReset(&cache);
		return proposalError(err, err_cap, "could not prepare the approved Grid upload");
	}
	host->request = approved;
	host->cache = cache;
	*request = approved;
	return 1;
}

static s32 terminalMatches(const grid_mod_proposal_control_t *terminal,
	const grid_match_offer_t *offered, const grid_match_offer_t *requested)
{
	return terminal && (terminal->kind == GRID_MOD_PROPOSAL_CANCEL || terminal->kind == GRID_MOD_PROPOSAL_RESULT)
		&& terminal->proposal_serial == offered->generation && controlValid(terminal, NULL, 0)
		&& ((requested && sameOffer(&terminal->offer, requested))
			|| (!terminal->accepted && sameOffer(&terminal->offer, offered)));
}

s32 gridModProposalHostCancelMatches(const grid_mod_proposal_host_t *host,
	const grid_mod_proposal_peer_t *peer, const grid_mod_proposal_control_t *cancel)
{
	const grid_mod_proposal_control_t *proposal = gridModProposalHostGet(host, peer);
	return proposal && cancel && cancel->kind == GRID_MOD_PROPOSAL_CANCEL
		&& terminalMatches(cancel, &proposal->offer, host->request.kind ? &host->request.offer : NULL);
}

static grid_mod_proposal_append_e hostAppendFailed(grid_mod_proposal_host_t *host)
{
	gridMatchTransferReset(&host->transfer);
	gridMatchCacheReset(&host->cache);
	return GRID_MOD_PROPOSAL_APPEND_FAILED;
}

grid_mod_proposal_append_e gridModProposalHostAppend(grid_mod_proposal_host_t *host,
	const grid_mod_proposal_peer_t *peer, const grid_match_chunk_t *chunk, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	const grid_mod_proposal_control_t *request = gridModProposalHostRequest(host, peer);
	if (!request || !chunk || !host->transfer || !sameOffer(&request->offer, &chunk->offer)
			|| host->ordinal >= request->packages.count) return GRID_MOD_PROPOSAL_APPEND_IGNORED;
	const u16 index = request->packages.indexes[host->ordinal];
	if (index != chunk->package_index) return GRID_MOD_PROPOSAL_APPEND_IGNORED;
	if (!gridMatchTransferAppend(host->transfer, chunk)) {
		proposalError(err, err_cap, "Grid proposal upload has an invalid chunk");
		return hostAppendFailed(host);
	}
	u32 size = 0;
	const u8 *bytes = gridMatchTransferComplete(host->transfer, &request->offer, index, &size);
	if (!bytes) return GRID_MOD_PROPOSAL_APPEND_IN_PROGRESS;
	if (!gridMatchCacheAccept(host->cache, &request->offer, index, bytes, size, err, err_cap)) {
		return hostAppendFailed(host);
	}
	const grid_match_sources_t *sources = &host->proposal.sources;
	if (gridMatchPackageOrigin(sources->packages[index].suffix, bytes, size) != GRID_ARCHIVE_ORIGINAL) {
		proposalError(err, err_cap, "Grid proposal upload is not an original redistributable source");
		return hostAppendFailed(host);
	}
	gridMatchTransferReset(&host->transfer);
	if (++host->ordinal == request->packages.count) {
		if (gridMatchCacheReady(host->cache, &request->offer)) return GRID_MOD_PROPOSAL_APPEND_READY;
	} else {
		const u16 next = request->packages.indexes[host->ordinal];
		if (gridMatchTransferBegin(&host->transfer, &request->offer, next, sources->packages[next].size)) {
			return GRID_MOD_PROPOSAL_APPEND_IN_PROGRESS;
		}
	}
	proposalError(err, err_cap, "could not finish the approved Grid package set");
	return hostAppendFailed(host);
}

s32 gridModProposalHostTakeReady(grid_mod_proposal_host_t **host, const grid_mod_proposal_peer_t *peer,
	grid_mod_proposal_control_t *request, grid_match_sources_t *plan, grid_match_cache_t **cache)
{
	const grid_mod_proposal_control_t *approved = host ? gridModProposalHostRequest(*host, peer) : NULL;
	if (!approved || !request || !sourcesEmpty(&request->sources) || !plan || !sourcesEmpty(plan)
			|| !cache || *cache || !gridMatchCacheReady((*host)->cache, &approved->offer)) return 0;
	*request = *approved;
	*plan = (*host)->proposal.sources;
	memset(&(*host)->proposal.sources, 0, sizeof((*host)->proposal.sources));
	*cache = (*host)->cache;
	(*host)->cache = NULL;
	gridModProposalHostReset(host);
	return 1;
}

void gridModProposalHostReset(grid_mod_proposal_host_t **host)
{
	if (!host || !*host) return;
	gridMatchTransferReset(&(*host)->transfer);
	gridMatchCacheReset(&(*host)->cache);
	gridModProposalControlClear(&(*host)->proposal);
	free(*host);
	*host = NULL;
}

s32 gridModProposalDonorBegin(grid_mod_proposal_donor_t **donor, const grid_mod_proposal_peer_t *peer,
	const grid_mod_proposal_control_t *proposal, grid_match_cache_t *ready,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!donor || *donor || !proposal || proposal->kind != GRID_MOD_PROPOSAL_OFFER
			|| !peerValid(peer, &proposal->offer) || !gridMatchCacheReady(ready, &proposal->offer)) {
		return proposalError(err, err_cap, "Grid proposal needs a ready local source set and a Room connection");
	}
	if (!controlValid(proposal, err, err_cap)) return 0;
	for (size_t i = 0; i < proposal->packages.count; i++) {
		const u16 index = proposal->packages.indexes[i];
		u32 size = 0;
		const void *bytes = gridMatchCacheRead(ready, &proposal->offer, index, &size);
		if (!bytes || gridMatchPackageOrigin(proposal->sources.packages[index].suffix, bytes, size) != GRID_ARCHIVE_ORIGINAL) {
			return proposalError(err, err_cap, "only exact original Grid sources can be offered for relay");
		}
	}
	grid_mod_proposal_donor_t *candidate = calloc(1, sizeof(*candidate));
	if (!candidate) return proposalError(err, err_cap, "out of memory holding the approved local Grid sources");
	if (!gridMatchCacheRetainReady(&candidate->cache, ready, &proposal->offer, err, err_cap)) {
		free(candidate);
		return 0;
	}
	candidate->peer = *peer;
	candidate->offer = proposal->offer;
	candidate->allowed = proposal->packages;
	*donor = candidate;
	return 1;
}

s32 gridModProposalDonorAcceptRequest(grid_mod_proposal_donor_t *donor,
	const grid_mod_proposal_peer_t *peer, const grid_mod_proposal_control_t *request,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!donor || !samePeer(&donor->peer, peer) || donor->requested.generation || !request
			|| request->kind != GRID_MOD_PROPOSAL_REQUEST || request->proposal_serial != donor->offer.generation
			|| !sameSources(&request->offer, &donor->offer) || !packagesAllowed(&request->packages, &donor->allowed)) {
		return proposalError(err, err_cap, "stale Grid request or request outside the offered relay permission");
	}
	if (!controlValid(request, err, err_cap)) return 0;
	donor->requested = request->offer;
	donor->missing = request->packages;
	return 1;
}

s32 gridModProposalDonorTerminalMatches(const grid_mod_proposal_donor_t *donor,
	const grid_mod_proposal_peer_t *peer, const grid_mod_proposal_control_t *terminal)
{
	return donor && samePeer(&donor->peer, peer) && terminalMatches(terminal, &donor->offer,
		donor->requested.generation ? &donor->requested : NULL);
}

s32 gridModProposalDonorChunk(const grid_mod_proposal_donor_t *donor,
	const grid_mod_proposal_peer_t *peer, u16 ordinal, u32 offset,
	grid_match_chunk_t *chunk, u32 *package_size)
{
	if (!donor || !samePeer(&donor->peer, peer) || !donor->requested.generation
			|| ordinal >= donor->missing.count || !chunk || !package_size) return 0;
	const u16 index = donor->missing.indexes[ordinal];
	u32 size = 0;
	const u8 *bytes = gridMatchCacheRead(donor->cache, &donor->offer, index, &size);
	if (!bytes || offset >= size) return 0;
	const u32 remaining = size - offset;
	const grid_match_chunk_t next = {
		.offer = donor->requested,
		.package_index = index,
		.offset = offset,
		.size = remaining < NET_DISTRIB_CHUNK_SIZE ? (u16)remaining : NET_DISTRIB_CHUNK_SIZE,
		.data = bytes + offset,
	};
	*chunk = next;
	*package_size = size;
	return 1;
}

void gridModProposalDonorReset(grid_mod_proposal_donor_t **donor)
{
	if (!donor || !*donor) return;
	gridMatchCacheReset(&(*donor)->cache);
	free(*donor);
	*donor = NULL;
}
