#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net/grid_match_wire.h"
#include "net/netbuf.h"
#include "platform.h"

#define GRID_OFFER_WIRE_VERSION 1
#define GRID_OFFER_WIRE_VERSION_FIESTA 2

static s32 sharedPackagesValid(const grid_match_shared_packages_t *packages)
{
	if (!packages || packages->count > GRID_MATCH_MAX_SHARED_PACKAGES) return 0;
	for (size_t i = 0; i < packages->count; i++) {
		if (packages->indexes[i] >= MANIFEST_MAX_ENTRIES
				|| (i && packages->indexes[i - 1] >= packages->indexes[i])) return 0;
	}
	return 1;
}

s32 gridMatchSharedPackagesWrite(struct netbuf *dst, const grid_match_shared_packages_t *packages)
{
	if (!dst || !dst->data || dst->error || dst->rp > dst->wp || dst->wp > dst->size
			|| !sharedPackagesValid(packages)) return 0;
	const u32 size = sizeof(u16) + packages->count * sizeof(u16);
	if (size > dst->size - dst->wp) return 0;

	u8 payload[GRID_MATCH_SHARED_PACKAGES_WIRE_MAX];
	const u16 count = PD_LE16(packages->count);
	memcpy(payload, &count, sizeof(count));
	for (size_t i = 0; i < packages->count; i++) {
		const u16 index = PD_LE16(packages->indexes[i]);
		memcpy(payload + sizeof(count) + i * sizeof(index), &index, sizeof(index));
	}
	return netbufWriteData(dst, payload, size) == size;
}

s32 gridMatchSharedPackagesRead(struct netbuf *src, grid_match_shared_packages_t *packages)
{
	if (!src || !src->data || src->error || src->rp > src->wp || src->wp > src->size
			|| src->wp - src->rp < sizeof(u16) || !packages) return 0;
	struct netbuf wire = *src;
	grid_match_shared_packages_t candidate = {0};
	candidate.count = netbufReadU16(&wire);
	if (candidate.count > GRID_MATCH_MAX_SHARED_PACKAGES
			|| candidate.count * sizeof(u16) > wire.wp - wire.rp) return 0;
	for (size_t i = 0; i < candidate.count; i++) candidate.indexes[i] = netbufReadU16(&wire);
	if (wire.error || !sharedPackagesValid(&candidate)) return 0;
	*packages = candidate;
	src->rp = wire.rp;
	return 1;
}

static s32 offerError(char *err, size_t cap, const char *message)
{
	if (err && cap) snprintf(err, cap, "%s", message);
	return 0;
}

static s32 offerReadString(struct netbuf *src, char *out, size_t cap)
{
	const u16 size = netbufReadU16(src);
	return size && size <= cap && netbufReadData(src, out, size) == size
		&& out[size - 1] == '\0' && !memchr(out, '\0', size - 1);
}

s32 gridMatchOfferWrite(struct netbuf *dst, const grid_match_offer_t *offer,
	const grid_match_sources_t *plan, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!dst || !dst->data || dst->error || dst->wp > dst->size
			|| !offer || offer->room_id == 0xff || !offer->generation || !plan) {
		return offerError(err, err_cap, "invalid Grid match offer");
	}
	grid_match_sources_t sealed = *plan;
	if (!gridMatchSourcesSeal(&sealed, err, err_cap)) return 0;
	if (memcmp(offer->manifest_digest, sealed.digest, SHA256_HEX_SIZE) != 0) {
		return offerError(err, err_cap, "Grid offer digest differs from its exact sources");
	}

	struct netbuf wire = *dst;
	const u8 version = gridMatchSourcesFiestaCount(&sealed)
		? GRID_OFFER_WIRE_VERSION_FIESTA : GRID_OFFER_WIRE_VERSION;
	netbufWriteU8(&wire, version);
	netbufWriteU8(&wire, offer->room_id);
	netbufWriteU64(&wire, offer->generation);
	netbufWriteData(&wire, sealed.digest, SHA256_HEX_SIZE - 1);
	netbufWriteU16(&wire, (u16)sealed.package_count);
	netbufWriteU16(&wire, (u16)sealed.entry_count);
	for (size_t i = 0; i < sealed.package_count && !wire.error; i++) {
		const grid_match_source_package_t *package = &sealed.packages[i];
		netbufWriteU8(&wire, (u8)package->owner.type);
		netbufWriteU8(&wire, (u8)package->bundled);
		netbufWriteU32(&wire, package->size);
		netbufWriteData(&wire, package->digest, SHA256_DIGEST_SIZE);
		netbufWriteStr(&wire, package->owner.id);
		netbufWriteStr(&wire, package->suffix);
	}
	for (size_t i = 0; i < sealed.entry_count && !wire.error; i++) {
		const grid_match_source_entry_t *entry = &sealed.entries[i];
		const u8 type = (u8)(entry->asset.type
			| (entry->fiesta_pool ? GRID_MATCH_SOURCE_FIESTA_WIRE_BIT : 0));
		netbufWriteU8(&wire, type);
		netbufWriteU16(&wire, (u16)entry->package_index);
		netbufWriteStr(&wire, entry->asset.id);
		netbufWriteStr(&wire, entry->member_chain);
	}
	if (wire.error) return offerError(err, err_cap, "Grid match offer exceeds the lobby payload capacity");
	*dst = wire;
	return 1;
}

s32 gridMatchOfferRead(struct netbuf *src, grid_match_offer_t *offer,
	grid_match_sources_t *plan, char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!src || !src->data || src->error || src->rp > src->wp || src->wp > src->size
			|| !offer || !plan) {
		return offerError(err, err_cap, "invalid Grid offer input");
	}
	struct netbuf wire = *src;
	grid_match_offer_t received = {0};
	grid_match_sources_t candidate = {0};
	const u8 version = netbufReadU8(&wire);
	received.room_id = netbufReadU8(&wire);
	received.generation = netbufReadU64(&wire);
	netbufReadData(&wire, received.manifest_digest, SHA256_HEX_SIZE - 1);
	candidate.package_count = netbufReadU16(&wire);
	candidate.entry_count = netbufReadU16(&wire);
	if (wire.error || (version != GRID_OFFER_WIRE_VERSION && version != GRID_OFFER_WIRE_VERSION_FIESTA)
			|| received.room_id == 0xff || !received.generation
			|| !candidate.entry_count || candidate.entry_count > MANIFEST_MAX_ENTRIES
			|| !candidate.package_count || candidate.package_count > candidate.entry_count) {
		return offerError(err, err_cap, "invalid or unsupported Grid offer header");
	}
	const size_t minimum_rows = candidate.package_count * (1 + 1 + 4 + SHA256_DIGEST_SIZE + 3 + 3)
		+ candidate.entry_count * (1 + 2 + 3 + 3);
	if (minimum_rows > (size_t)netbufReadLeft(&wire)) {
		return offerError(err, err_cap, "Grid offer does not contain its declared rows");
	}
	candidate.packages = calloc(candidate.package_count, sizeof(*candidate.packages));
	candidate.entries = calloc(candidate.entry_count, sizeof(*candidate.entries));
	if (!candidate.packages || !candidate.entries) {
		gridMatchSourcesFree(&candidate);
		return offerError(err, err_cap, "out of memory reading Grid offer");
	}
	for (size_t i = 0; i < candidate.package_count; i++) {
		grid_match_source_package_t *package = &candidate.packages[i];
		package->owner.type = (asset_type_e)netbufReadU8(&wire);
		package->bundled = netbufReadU8(&wire);
		package->size = netbufReadU32(&wire);
		netbufReadData(&wire, package->digest, SHA256_DIGEST_SIZE);
		if (!offerReadString(&wire, package->owner.id, sizeof(package->owner.id))
				|| !offerReadString(&wire, package->suffix, sizeof(package->suffix))) goto malformed;
	}
	for (size_t i = 0; i < candidate.entry_count; i++) {
		grid_match_source_entry_t *entry = &candidate.entries[i];
		const u8 encoded_type = netbufReadU8(&wire);
		const u8 type = (u8)(encoded_type & ~GRID_MATCH_SOURCE_FIESTA_WIRE_BIT);
		entry->fiesta_pool = (encoded_type & GRID_MATCH_SOURCE_FIESTA_WIRE_BIT) != 0;
		if (type <= ASSET_NONE || type >= ASSET_TYPE_COUNT
				|| (entry->fiesta_pool && (version != GRID_OFFER_WIRE_VERSION_FIESTA || type != ASSET_WEAPON))) {
			goto malformed;
		}
		entry->asset.type = (asset_type_e)type;
		entry->package_index = netbufReadU16(&wire);
		if (!offerReadString(&wire, entry->asset.id, sizeof(entry->asset.id))
				|| !offerReadString(&wire, entry->member_chain, sizeof(entry->member_chain))) goto malformed;
	}
	if (wire.error || (version == GRID_OFFER_WIRE_VERSION_FIESTA && !gridMatchSourcesFiestaCount(&candidate))) {
		goto malformed;
	}
	if (!gridMatchSourcesSeal(&candidate, err, err_cap)) {
		gridMatchSourcesFree(&candidate);
		return 0;
	}
	if (memcmp(received.manifest_digest, candidate.digest, SHA256_HEX_SIZE) != 0) {
		gridMatchSourcesFree(&candidate);
		return offerError(err, err_cap, "Grid offer digest differs from its exact sources");
	}
	gridMatchSourcesFree(plan);
	*plan = candidate;
	*offer = received;
	*src = wire;
	return 1;

malformed:
	gridMatchSourcesFree(&candidate);
	return offerError(err, err_cap, "truncated or malformed Grid offer rows");
}

static s32 offerIdentityValid(const grid_match_offer_t *offer)
{
	return offer && offer->room_id != 0xff && offer->generation
		&& offer->manifest_digest[SHA256_HEX_SIZE - 1] == '\0'
		&& strspn(offer->manifest_digest, "0123456789abcdef") == SHA256_HEX_SIZE - 1;
}

s32 gridMatchStatusWrite(struct netbuf *dst, const grid_match_offer_t *offer, u8 ready)
{
	if (!dst || !dst->data || dst->error || dst->wp > dst->size
			|| !offerIdentityValid(offer) || ready > 1) return 0;
	struct netbuf wire = *dst;
	netbufWriteU8(&wire, offer->room_id);
	netbufWriteU64(&wire, offer->generation);
	netbufWriteData(&wire, offer->manifest_digest, SHA256_HEX_SIZE - 1);
	netbufWriteU8(&wire, ready);
	if (wire.error) return 0;
	*dst = wire;
	return 1;
}

s32 gridMatchStatusRead(struct netbuf *src, grid_match_offer_t *offer, u8 *ready)
{
	if (!src || !src->data || src->error || src->rp > src->wp || src->wp > src->size
			|| !offer || !ready) return 0;
	struct netbuf wire = *src;
	grid_match_offer_t verified = {0};
	verified.room_id = netbufReadU8(&wire);
	verified.generation = netbufReadU64(&wire);
	netbufReadData(&wire, verified.manifest_digest, SHA256_HEX_SIZE - 1);
	const u8 flag = netbufReadU8(&wire);
	if (wire.error || !offerIdentityValid(&verified) || flag > 1) return 0;
	*src = wire;
	*offer = verified;
	*ready = flag;
	return 1;
}

s32 gridMatchStageCancelWrite(struct netbuf *dst, const grid_match_offer_t *offer, u32 epoch)
{
	const u32 bytes = 1 + sizeof(u64) + SHA256_HEX_SIZE - 1 + 1 + sizeof(u32);
	if (!epoch || !dst || dst->wp > dst->size || bytes > dst->size - dst->wp) return 0;
	struct netbuf wire = *dst;
	if (!gridMatchStatusWrite(&wire, offer, 0)) return 0;
	netbufWriteU32(&wire, epoch);
	*dst = wire;
	return 1;
}

s32 gridMatchStageCancelRead(struct netbuf *src, const grid_match_offer_t *expected, u32 epoch)
{
	if (!src || !epoch || !offerIdentityValid(expected)) return 0;
	struct netbuf wire = *src;
	grid_match_offer_t offer;
	u8 ready;
	if (!gridMatchStatusRead(&wire, &offer, &ready) || ready
			|| offer.room_id != expected->room_id || offer.generation != expected->generation
			|| memcmp(offer.manifest_digest, expected->manifest_digest, SHA256_HEX_SIZE) != 0
			|| netbufReadU32(&wire) != epoch || wire.error || netbufReadLeft(&wire) != 0) return 0;
	*src = wire;
	return 1;
}
