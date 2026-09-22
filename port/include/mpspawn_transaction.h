#ifndef PD_MPSPAWN_TRANSACTION_H
#define PD_MPSPAWN_TRANSACTION_H

#include <PR/ultratypes.h>
#include "constants.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MPSPAWN_TRANSACTION_MAX_PLAYERS MAX_PLAYERS

typedef enum mpspawn_transaction_status {
	MPSPAWN_TRANSACTION_OK = 0,
	MPSPAWN_TRANSACTION_INVALID_ARGUMENT,
	MPSPAWN_TRANSACTION_DUPLICATE_PLAYER,
	MPSPAWN_TRANSACTION_PREPARE_REJECTED,
	MPSPAWN_TRANSACTION_BATCH_REJECTED,
	MPSPAWN_TRANSACTION_INCOMPLETE,
	MPSPAWN_TRANSACTION_ALREADY_COMMITTED,
} mpspawn_transaction_status_t;

typedef struct mpspawn_transaction {
	s32 expected_count;
	s32 prepared_count;
	s32 rejected_playernum;
	s32 batch_rejected;
	s32 committed;
	u8 prepared_players[MPSPAWN_TRANSACTION_MAX_PLAYERS];
} mpspawn_transaction_t;

typedef struct mpspawn_capsule {
	f32 x;
	f32 z;
	f32 bottom_y;
	f32 radius;
	f32 height;
} mpspawn_capsule_t;

typedef struct mpspawn_pool_scan {
	s32 count;
	s32 next_index;
	s32 last_index;
	s32 accepted_index;
} mpspawn_pool_scan_t;

mpspawn_transaction_status_t mpspawnTransactionBegin(
	mpspawn_transaction_t *transaction, s32 expected_count);
mpspawn_transaction_status_t mpspawnTransactionRecordPrepare(
	mpspawn_transaction_t *transaction, s32 playernum, s32 prepared);
mpspawn_transaction_status_t mpspawnTransactionRejectPreparedBatch(
	mpspawn_transaction_t *transaction);
mpspawn_transaction_status_t mpspawnTransactionCommit(
	mpspawn_transaction_t *transaction);
s32 mpspawnTransactionCanCommit(const mpspawn_transaction_t *transaction);
s32 mpspawnCapsulesOverlap(const mpspawn_capsule_t *a,
	const mpspawn_capsule_t *b);
s32 mpspawnPoolScanBegin(mpspawn_pool_scan_t *scan, s32 count);
s32 mpspawnPoolScanNext(mpspawn_pool_scan_t *scan);
s32 mpspawnPoolScanAccept(mpspawn_pool_scan_t *scan, s32 index);
s32 mpspawnPoolScanResult(const mpspawn_pool_scan_t *scan);

#ifdef __cplusplus
}
#endif

#endif /* PD_MPSPAWN_TRANSACTION_H */
