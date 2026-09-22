#include "mpspawn_transaction.h"

#include <string.h>

mpspawn_transaction_status_t mpspawnTransactionBegin(
		mpspawn_transaction_t *transaction, s32 expected_count)
{
	if (transaction == NULL) {
		return MPSPAWN_TRANSACTION_INVALID_ARGUMENT;
	}

	memset(transaction, 0, sizeof(*transaction));
	transaction->rejected_playernum = -1;

	if (expected_count < 0
			|| expected_count > MPSPAWN_TRANSACTION_MAX_PLAYERS) {
		return MPSPAWN_TRANSACTION_INVALID_ARGUMENT;
	}

	transaction->expected_count = expected_count;
	return MPSPAWN_TRANSACTION_OK;
}

mpspawn_transaction_status_t mpspawnTransactionRecordPrepare(
		mpspawn_transaction_t *transaction, s32 playernum, s32 prepared)
{
	if (transaction == NULL || transaction->committed
			|| transaction->batch_rejected
			|| playernum < 0
			|| playernum >= MPSPAWN_TRANSACTION_MAX_PLAYERS) {
		return MPSPAWN_TRANSACTION_INVALID_ARGUMENT;
	}
	if (transaction->prepared_players[playernum]) {
		return MPSPAWN_TRANSACTION_DUPLICATE_PLAYER;
	}
	if (!prepared) {
		transaction->rejected_playernum = playernum;
		return MPSPAWN_TRANSACTION_PREPARE_REJECTED;
	}

	transaction->prepared_players[playernum] = 1;
	transaction->prepared_count++;
	return MPSPAWN_TRANSACTION_OK;
}

mpspawn_transaction_status_t mpspawnTransactionRejectPreparedBatch(
		mpspawn_transaction_t *transaction)
{
	if (transaction == NULL || transaction->committed) {
		return MPSPAWN_TRANSACTION_INVALID_ARGUMENT;
	}

	transaction->batch_rejected = 1;
	return MPSPAWN_TRANSACTION_BATCH_REJECTED;
}

s32 mpspawnTransactionCanCommit(const mpspawn_transaction_t *transaction)
{
	return transaction != NULL
		&& !transaction->committed
		&& !transaction->batch_rejected
		&& transaction->rejected_playernum < 0
		&& transaction->prepared_count == transaction->expected_count;
}

mpspawn_transaction_status_t mpspawnTransactionCommit(
		mpspawn_transaction_t *transaction)
{
	if (transaction == NULL) {
		return MPSPAWN_TRANSACTION_INVALID_ARGUMENT;
	}
	if (transaction->committed) {
		return MPSPAWN_TRANSACTION_ALREADY_COMMITTED;
	}
	if (transaction->batch_rejected) {
		return MPSPAWN_TRANSACTION_BATCH_REJECTED;
	}
	if (transaction->rejected_playernum >= 0) {
		return MPSPAWN_TRANSACTION_PREPARE_REJECTED;
	}
	if (!mpspawnTransactionCanCommit(transaction)) {
		return MPSPAWN_TRANSACTION_INCOMPLETE;
	}

	transaction->committed = 1;
	return MPSPAWN_TRANSACTION_OK;
}

s32 mpspawnCapsulesOverlap(const mpspawn_capsule_t *a,
		const mpspawn_capsule_t *b)
{
	f32 dx;
	f32 dz;
	f32 radius_sum;
	f32 a_top;
	f32 b_top;

	if (a == NULL || b == NULL || a->radius <= 0.0f || b->radius <= 0.0f
			|| a->height <= 0.0f || b->height <= 0.0f) {
		return 0;
	}

	a_top = a->bottom_y + a->height;
	b_top = b->bottom_y + b->height;
	if (a->bottom_y >= b_top || b->bottom_y >= a_top) {
		return 0;
	}

	dx = a->x - b->x;
	dz = a->z - b->z;
	radius_sum = a->radius + b->radius;
	return dx * dx + dz * dz < radius_sum * radius_sum;
}

s32 mpspawnPoolScanBegin(mpspawn_pool_scan_t *scan, s32 count)
{
	if (scan == NULL || count < 0) {
		return 0;
	}

	scan->count = count;
	scan->next_index = 0;
	scan->last_index = -1;
	scan->accepted_index = -1;
	return 1;
}

s32 mpspawnPoolScanNext(mpspawn_pool_scan_t *scan)
{
	if (scan == NULL || scan->accepted_index >= 0
			|| scan->next_index < 0 || scan->next_index >= scan->count) {
		return -1;
	}

	scan->last_index = scan->next_index;
	scan->next_index++;
	return scan->last_index;
}

s32 mpspawnPoolScanAccept(mpspawn_pool_scan_t *scan, s32 index)
{
	if (scan == NULL || scan->accepted_index >= 0
			|| index < 0 || index != scan->last_index) {
		return 0;
	}

	scan->accepted_index = index;
	return 1;
}

s32 mpspawnPoolScanResult(const mpspawn_pool_scan_t *scan)
{
	return scan != NULL ? scan->accepted_index : -1;
}
