#ifndef PD_GRID_MOD_PROPOSAL_H
#define PD_GRID_MOD_PROPOSAL_H

#include "net/grid_match_transfer.h"
#include "net/grid_match_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum grid_mod_proposal_kind {
	GRID_MOD_PROPOSAL_OFFER = 1,
	GRID_MOD_PROPOSAL_REQUEST = 2,
	GRID_MOD_PROPOSAL_CANCEL = 3,
	GRID_MOD_PROPOSAL_RESULT = 4,
} grid_mod_proposal_kind_e;

typedef struct grid_mod_proposal_control {
	grid_mod_proposal_kind_e kind;
	u64 proposal_serial;
	grid_match_offer_t offer;
	grid_match_shared_packages_t packages;
	grid_match_sources_t sources;
	u8 accepted;
} grid_mod_proposal_control_t;

/* OFFER identifies the donor's sealed sources (generation == proposal_serial)
 * and lists the original packages the donor permits the host to relay. REQUEST
 * echoes the donor serial and digest, uses a fresh host generation, and lists
 * only the missing packages approved for upload. Only OFFER owns sources;
 * only RESULT has an accepted flag. Chunks use the REQUEST offer identity.
 * Callers supply and authenticate the enclosing direction and opcode. Read
 * consumes exactly one complete body and replaces a zero-initialized or
 * previously decoded control. Refusal preserves the cursor and destination. */
s32 gridModProposalControlRead(struct netbuf *src, grid_mod_proposal_control_t *out,
	char *err, size_t err_cap);
s32 gridModProposalControlWrite(struct netbuf *dst, const grid_mod_proposal_control_t *control,
	char *err, size_t err_cap);
void gridModProposalControlClear(grid_mod_proposal_control_t *control);

/* Locally authenticated endpoint, supplied on every operation. The room
 * lifetime and connection are nonzero local tokens and never appear on wire.
 * The caller enforces the Room's authority, membership, phase and expiry, and
 * resets a state on detach, Room reuse or policy change. */
typedef struct grid_mod_proposal_peer {
	u8 room_id;
	u32 client_id;
	u64 room_lifetime;
	u64 connection;
} grid_mod_proposal_peer_t;

typedef struct grid_mod_proposal_host grid_mod_proposal_host_t;
typedef struct grid_mod_proposal_donor grid_mod_proposal_donor_t;

/* The callback returns a malloc-owned local archive for an unrequested row.
 * Do not resolve through a donor-supplied path. The controller verifies bytes. */
typedef void *(*grid_mod_proposal_load_package_fn)(const grid_match_source_package_t *package,
	u32 *size, void *userdata);

typedef enum grid_mod_proposal_append {
	GRID_MOD_PROPOSAL_APPEND_FAILED = -1,
	GRID_MOD_PROPOSAL_APPEND_IGNORED = 0,
	GRID_MOD_PROPOSAL_APPEND_IN_PROGRESS = 1,
	GRID_MOD_PROPOSAL_APPEND_READY = 2,
} grid_mod_proposal_append_e;

/* Begin moves the successfully decoded OFFER into an empty host controller. */
s32 gridModProposalHostBegin(grid_mod_proposal_host_t **host, const grid_mod_proposal_peer_t *peer,
	grid_mod_proposal_control_t *proposal, char *err, size_t err_cap);
const grid_mod_proposal_control_t *gridModProposalHostGet(const grid_mod_proposal_host_t *host,
	const grid_mod_proposal_peer_t *peer);
const grid_mod_proposal_control_t *gridModProposalHostRequest(const grid_mod_proposal_host_t *host,
	const grid_mod_proposal_peer_t *peer);
/* Call only after host approval. Can approve once per proposal. The caller
 * supplies a fresh host generation distinct from the donor serial. Refusal
 * leaves the proposal and output intact. The output must own no sources. */
s32 gridModProposalHostApprove(grid_mod_proposal_host_t *host, const grid_mod_proposal_peer_t *peer,
	u64 host_nonce, const grid_match_shared_packages_t *missing,
	grid_mod_proposal_load_package_fn load_installed, void *userdata,
	grid_mod_proposal_control_t *request, char *err, size_t err_cap);
s32 gridModProposalHostCancelMatches(const grid_mod_proposal_host_t *host,
	const grid_mod_proposal_peer_t *peer, const grid_mod_proposal_control_t *cancel);
/* Call after gridMatchChunkRead consumes exactly one complete chunk body.
 * An authenticated invalid current chunk fails the approval permanently;
 * a stale or unrelated chunk leaves it intact. A ready cache is still detached
 * and has no catalog, distribution or gameplay authority. */
grid_mod_proposal_append_e gridModProposalHostAppend(grid_mod_proposal_host_t *host,
	const grid_mod_proposal_peer_t *peer, const grid_match_chunk_t *chunk, char *err, size_t err_cap);
/* Moves the ready sources/cache and destroys the controller on success.
 * Outputs must own no sources or cache. REQUEST identifies the donor-authorized
 * uploaded subset and the key used for the cache. */
s32 gridModProposalHostTakeReady(grid_mod_proposal_host_t **host, const grid_mod_proposal_peer_t *peer,
	grid_mod_proposal_control_t *request, grid_match_sources_t *plan, grid_match_cache_t **cache);
void gridModProposalHostReset(grid_mod_proposal_host_t **host);

/* Begin retains the donor's already validated local cache after checking
 * exact original provenance for every package affirmatively offered. */
s32 gridModProposalDonorBegin(grid_mod_proposal_donor_t **donor, const grid_mod_proposal_peer_t *peer,
	const grid_mod_proposal_control_t *proposal, grid_match_cache_t *ready,
	char *err, size_t err_cap);
s32 gridModProposalDonorAcceptRequest(grid_mod_proposal_donor_t *donor,
	const grid_mod_proposal_peer_t *peer, const grid_mod_proposal_control_t *request,
	char *err, size_t err_cap);
s32 gridModProposalDonorTerminalMatches(const grid_mod_proposal_donor_t *donor,
	const grid_mod_proposal_peer_t *peer, const grid_mod_proposal_control_t *terminal);
/* Borrow the next chunk at the caller-owned send position. Advance only after
 * successful enqueue. Set package_size on success for advancing the ordinal. */
s32 gridModProposalDonorChunk(const grid_mod_proposal_donor_t *donor,
	const grid_mod_proposal_peer_t *peer, u16 ordinal, u32 offset,
	grid_match_chunk_t *chunk, u32 *package_size);
void gridModProposalDonorReset(grid_mod_proposal_donor_t **donor);

#ifdef __cplusplus
}
#endif

#endif
