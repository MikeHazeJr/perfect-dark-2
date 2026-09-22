/**
 * netupnp.c -- bounded async UPnP port forwarding.
 *
 * UPnP I/O is owned by a joinable worker. It receives an immutable request
 * snapshot and produces a worker-local result; only the game thread commits
 * that result to the live mapping/URL state. A worker may add and verify the
 * desired set, but it never deletes the committed set. Stale-old removal is a
 * post-acceptance game-thread action, so cancellation, timeout, or an owner
 * union change cannot leave logical leases whose router mappings were lost.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <PR/ultratypes.h>
#include "types.h"
#include "system.h"
#include "net/netupnp.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "miniupnpc.h"
#include "upnpcommands.h"
#include "upnperrors.h"
#include "connectcode.h"

#define NET_UPNP_MAX_MAPPINGS NET_UPNP_OWNER_COUNT
#define NET_UPNP_LEASE_SECONDS 3600u
#define NET_UPNP_RENEW_AFTER_SECONDS 1800u
#define NET_UPNP_RETRY_SECONDS 60u
#define NET_UPNP_WORK_TIMEOUT_SECONDS 10u
#define NET_UPNP_WORK_SETUP 1
#define NET_UPNP_WORK_RENEW 2

typedef struct {
	u16 requested_port;
	u16 mapped_port;
	u8 active;
} upnp_mapping_t;

typedef struct {
	u16 requested_ports[NET_UPNP_MAX_MAPPINGS];
	u8 requested_count;
	u8 mode;
	u32 generation;
	u32 deadline_unix;
	upnp_mapping_t old_mappings[NET_UPNP_MAX_MAPPINGS];
} upnp_worker_request_t;

typedef struct {
	struct UPNPUrls urls;
	struct IGDdatas data;
	char external_ip[64];
	char lan_addr[64];
	upnp_mapping_t mappings[NET_UPNP_MAX_MAPPINGS];
	u8 mapping_count;
	u8 urls_ready;
	s32 success;
} upnp_worker_result_t;

typedef struct {
	upnp_worker_request_t request;
	upnp_worker_result_t result;
} upnp_worker_job_t;

/* These are game-thread-owned live state. The worker never writes them. */
static struct UPNPUrls s_UpnpUrls;
static struct IGDdatas s_UpnpData;
static char s_ExternalIP[64];
static char s_LanAddr[64];
static upnp_mapping_t s_Mappings[NET_UPNP_MAX_MAPPINGS];
static u16 s_RequestedPorts[NET_UPNP_MAX_MAPPINGS];
static u8 s_RequestedCount;
static u8 s_UrlsReady;
static s32 s_UpnpStatus = UPNP_STATUS_IDLE;
static s32 s_UpnpActive;
static u32 s_RenewAtUnix;
static u32 s_RequestGeneration = 1;
static u16 s_OwnerPorts[NET_UPNP_OWNER_COUNT];

static upnp_worker_job_t *s_WorkerJob;
static s32 s_WorkerRunning;

#ifdef _WIN32
static HANDLE s_WorkerHandle;
static HANDLE s_WorkerDoneEvent;
static LONG s_WorkerCancel;
#else
static pthread_t s_WorkerThread;
static pthread_mutex_t s_WorkerMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_WorkerCondition = PTHREAD_COND_INITIALIZER;
static s32 s_WorkerDone;
static s32 s_WorkerCancel;
#endif

static u8 buildRequestedPorts(u16 out_ports[NET_UPNP_MAX_MAPPINGS])
{
	u8 count = 0;
	for (u8 i = 0; i < NET_UPNP_OWNER_COUNT; i++) {
		if (s_OwnerPorts[i] == 0) continue;
		s32 duplicate = 0;
		for (u8 j = 0; j < count; j++) {
			if (out_ports[j] == s_OwnerPorts[i]) duplicate = 1;
		}
		if (!duplicate && count < NET_UPNP_MAX_MAPPINGS) {
			out_ports[count++] = s_OwnerPorts[i];
		}
	}
	return count;
}

static s32 requestedPortsMatch(const u16 *ports, u8 count)
{
	if (count != s_RequestedCount) return 0;
	for (u8 i = 0; i < count; i++) {
		if (ports[i] != s_RequestedPorts[i]) return 0;
	}
	return 1;
}

static s32 oldContainsPort(const upnp_worker_request_t *request, u16 port)
{
	if (!request || port == 0) return 0;
	for (u8 i = 0; i < NET_UPNP_MAX_MAPPINGS; i++) {
		if (request->old_mappings[i].active &&
			request->old_mappings[i].mapped_port == port) return 1;
	}
	return 0;
}

static s32 requestedContainsPort(const upnp_worker_request_t *request, u16 port)
{
	if (!request || port == 0) return 0;
	for (u8 i = 0; i < request->requested_count; i++) {
		if (request->requested_ports[i] == port) return 1;
	}
	return 0;
}

static s32 deleteMappings(const struct UPNPUrls *urls,
	const struct IGDdatas *data, const upnp_mapping_t *mappings, u8 count)
{
	s32 all_removed = 1;
	if (!urls || !data || !urls->controlURL) return 0;
	for (u8 i = 0; i < count; i++) {
		if (!mappings[i].active || mappings[i].mapped_port == 0) continue;
		char port[8];
		snprintf(port, sizeof(port), "%u", (unsigned)mappings[i].mapped_port);
		int result = UPNP_DeletePortMapping(urls->controlURL,
			data->first.servicetype, port, "UDP", NULL);
		if (result != 0) {
			all_removed = 0;
			sysLogPrintf(LOG_WARNING,
				"UPNP: mapping %s/UDP removal failed: %s (%d)",
				port, strupnperror(result), result);
		}
	}
	return all_removed;
}

/* Roll back only mappings that were not part of the old live set. If a
 * renewal fails, the old ports are deliberately left untouched. */
static void rollbackNewMappings(const struct UPNPUrls *urls,
	const struct IGDdatas *data, const upnp_mapping_t *mapped, u8 count,
	const upnp_worker_request_t *request)
{
	if (!urls || !data || !mapped || !request) return;
	for (u8 i = 0; i < count; i++) {
		if (!mapped[i].active || mapped[i].mapped_port == 0 ||
			oldContainsPort(request, mapped[i].mapped_port)) continue;
		upnp_mapping_t one = mapped[i];
		(void)deleteMappings(urls, data, &one, 1);
	}
}

static s32 workerShouldStop(const upnp_worker_request_t *request)
{
#ifdef _WIN32
	if (InterlockedCompareExchange(&s_WorkerCancel, 0, 0) != 0) return 1;
#else
	pthread_mutex_lock(&s_WorkerMutex);
	s32 canceled = s_WorkerCancel;
	pthread_mutex_unlock(&s_WorkerMutex);
	if (canceled) return 1;
#endif
	return request && request->deadline_unix != 0 &&
		(u32)time(NULL) > request->deadline_unix;
}

static s32 workerPerform(const upnp_worker_request_t *request,
	upnp_worker_result_t *result)
{
	struct UPNPUrls urls;
	struct IGDdatas data;
	upnp_mapping_t mapped[NET_UPNP_MAX_MAPPINGS];
	char external_ip[64] = {0};
	char lan_addr[64] = {0};
	memset(&urls, 0, sizeof(urls));
	memset(&data, 0, sizeof(data));
	memset(mapped, 0, sizeof(mapped));
	if (result) memset(result, 0, sizeof(*result));

	if (!request || !result || request->requested_count == 0 ||
		workerShouldStop(request)) {
		return 0;
	}
	sysLogPrintf(LOG_NOTE,
		"UPNP: [thread] discovering devices for %u mappings...",
		(unsigned)request->requested_count);
	struct UPNPDev *devlist = NULL;
	int error = 0;
	devlist = upnpDiscover(2000, NULL, NULL, 0, 0, 2, &error);
	if (!devlist || workerShouldStop(request)) {
		if (devlist) freeUPNPDevlist(devlist);
		sysLogPrintf(LOG_WARNING, "UPNP: [thread] no devices found (error %d)", error);
		return 0;
	}
	char wan_addr[64] = {0};
	int igd_result = UPNP_GetValidIGD(devlist, &urls, &data,
		lan_addr, sizeof(lan_addr), wan_addr, sizeof(wan_addr));
	freeUPNPDevlist(devlist);
	if (igd_result == 0 || workerShouldStop(request)) {
		if (igd_result != 0) FreeUPNPUrls(&urls);
		sysLogPrintf(LOG_WARNING, "UPNP: [thread] no valid IGD found");
		return 0;
	}
	int ip_result = UPNP_GetExternalIPAddress(urls.controlURL,
		data.first.servicetype, external_ip);
	if (ip_result != 0 || external_ip[0] == '\0') {
		strncpy(external_ip, "unknown", sizeof(external_ip) - 1);
		external_ip[sizeof(external_ip) - 1] = '\0';
	}

	for (u8 i = 0; i < request->requested_count; i++) {
		if (workerShouldStop(request)) {
			rollbackNewMappings(&urls, &data, mapped,
				NET_UPNP_MAX_MAPPINGS, request);
			FreeUPNPUrls(&urls);
			return 0;
		}
		char port[8];
		char lease[12];
		snprintf(port, sizeof(port), "%u",
			(unsigned)request->requested_ports[i]);
		snprintf(lease, sizeof(lease), "%u", NET_UPNP_LEASE_SECONDS);
		int map_result = UPNP_AddPortMapping(urls.controlURL,
			data.first.servicetype, port, port, lan_addr, "Perfect Dark 2",
			"UDP", NULL, lease);
		if (map_result != 0) {
			sysLogPrintf(LOG_WARNING,
				"UPNP: [thread] port mapping %s/UDP failed: %s (%d)",
				port, strupnperror(map_result), map_result);
			rollbackNewMappings(&urls, &data, mapped,
				NET_UPNP_MAX_MAPPINGS, request);
			FreeUPNPUrls(&urls);
			return 0;
		}
		char verified_client[64] = {0};
		char verified_port[8] = {0};
		char verified_description[80] = {0};
		char verified_enabled[8] = {0};
		char verified_lease[16] = {0};
		int verify_result = UPNP_GetSpecificPortMappingEntry(
			urls.controlURL, data.first.servicetype, port, "UDP", NULL,
			verified_client, verified_port, verified_description,
			verified_enabled, verified_lease);
		if (verify_result != 0 || strcmp(verified_client, lan_addr) != 0 ||
			(u16)atoi(verified_port) != request->requested_ports[i] ||
			verified_enabled[0] == '0' || verified_enabled[0] == '\0') {
			sysLogPrintf(LOG_WARNING,
				"UPNP: [thread] router did not verify %s/UDP ownership",
				port);
			upnp_mapping_t unverified;
			memset(&unverified, 0, sizeof(unverified));
			unverified.mapped_port = request->requested_ports[i];
			unverified.active = 1;
			if (!oldContainsPort(request, unverified.mapped_port)) {
				(void)deleteMappings(&urls, &data, &unverified, 1);
			}
			rollbackNewMappings(&urls, &data, mapped,
				NET_UPNP_MAX_MAPPINGS, request);
			FreeUPNPUrls(&urls);
			return 0;
		}
		mapped[i].requested_port = request->requested_ports[i];
		mapped[i].mapped_port = request->requested_ports[i];
		mapped[i].active = 1;
	}

	if (workerShouldStop(request)) {
		rollbackNewMappings(&urls, &data, mapped,
			NET_UPNP_MAX_MAPPINGS, request);
		FreeUPNPUrls(&urls);
		return 0;
	}

	result->urls = urls;
	result->data = data;
	strncpy(result->external_ip, external_ip,
		sizeof(result->external_ip) - 1);
	strncpy(result->lan_addr, lan_addr,
		sizeof(result->lan_addr) - 1);
	memcpy(result->mappings, mapped, sizeof(mapped));
	result->mapping_count = request->requested_count;
	result->urls_ready = 1;
	result->success = 1;
	memset(&urls, 0, sizeof(urls));
	return 1;
}

#ifdef _WIN32
static DWORD WINAPI upnpWorkerThread(LPVOID param)
{
	upnp_worker_job_t *job = (upnp_worker_job_t *)param;
	upnp_worker_request_t request = job->request;
	(void)workerPerform(&request, &job->result);
	SetEvent(s_WorkerDoneEvent);
	return 0;
}
#else
static void *upnpWorkerThread(void *param)
{
	upnp_worker_job_t *job = (upnp_worker_job_t *)param;
	upnp_worker_request_t request = job->request;
	(void)workerPerform(&request, &job->result);
	pthread_mutex_lock(&s_WorkerMutex);
	s_WorkerDone = 1;
	pthread_cond_broadcast(&s_WorkerCondition);
	pthread_mutex_unlock(&s_WorkerMutex);
	return NULL;
}
#endif

static s32 workerIsDone(void)
{
	if (!s_WorkerRunning) return 0;
#ifdef _WIN32
	return WaitForSingleObject(s_WorkerDoneEvent, 0) == WAIT_OBJECT_0;
#else
	pthread_mutex_lock(&s_WorkerMutex);
	s32 done = s_WorkerDone;
	pthread_mutex_unlock(&s_WorkerMutex);
	return done;
#endif
}

static void workerJoin(void)
{
	if (!s_WorkerRunning) return;
#ifdef _WIN32
	WaitForSingleObject(s_WorkerHandle, INFINITE);
	CloseHandle(s_WorkerHandle);
	CloseHandle(s_WorkerDoneEvent);
	s_WorkerHandle = NULL;
	s_WorkerDoneEvent = NULL;
#else
	pthread_mutex_lock(&s_WorkerMutex);
	while (!s_WorkerDone) pthread_cond_wait(&s_WorkerCondition, &s_WorkerMutex);
	pthread_mutex_unlock(&s_WorkerMutex);
	pthread_join(s_WorkerThread, NULL);
#endif
	s_WorkerRunning = 0;
}

static s32 startWorker(u8 mode, const u16 *requested, u8 requested_count)
{
	if (s_WorkerRunning || !requested || requested_count == 0) return -1;
	upnp_worker_job_t *job = (upnp_worker_job_t *)calloc(1, sizeof(*job));
	if (!job) return -1;
	memcpy(job->request.requested_ports, requested,
		requested_count * sizeof(requested[0]));
	job->request.requested_count = requested_count;
	job->request.mode = mode;
	job->request.generation = ++s_RequestGeneration;
	job->request.deadline_unix = (u32)time(NULL) +
		NET_UPNP_WORK_TIMEOUT_SECONDS;
	memcpy(job->request.old_mappings, s_Mappings, sizeof(s_Mappings));
	s_WorkerJob = job;
#ifdef _WIN32
	s_WorkerCancel = 0;
	s_WorkerDoneEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!s_WorkerDoneEvent) {
		free(s_WorkerJob);
		s_WorkerJob = NULL;
		return -1;
	}
	s_WorkerRunning = 1;
	s_WorkerHandle = CreateThread(NULL, 0, upnpWorkerThread,
		s_WorkerJob, 0, NULL);
	if (!s_WorkerHandle) {
		s_WorkerRunning = 0;
		CloseHandle(s_WorkerDoneEvent);
		s_WorkerDoneEvent = NULL;
		free(s_WorkerJob);
		s_WorkerJob = NULL;
		return -1;
	}
#else
	pthread_mutex_lock(&s_WorkerMutex);
	s_WorkerCancel = 0;
	s_WorkerDone = 0;
	s_WorkerRunning = 1;
	int result = pthread_create(&s_WorkerThread, NULL, upnpWorkerThread,
		s_WorkerJob);
	if (result != 0) s_WorkerRunning = 0;
	pthread_mutex_unlock(&s_WorkerMutex);
	if (result != 0) {
		free(s_WorkerJob);
		s_WorkerJob = NULL;
		return -1;
	}
#endif
	return 0;
}

static void discardWorkerResult(void)
{
	if (!s_WorkerJob) return;
	if (s_WorkerJob->result.success && s_WorkerJob->result.urls_ready) {
		/* A result can finish between the done probe and cancellation. Keep
		 * every mapping that belonged to the old live request in that case. */
		rollbackNewMappings(&s_WorkerJob->result.urls,
			&s_WorkerJob->result.data, s_WorkerJob->result.mappings,
			s_WorkerJob->result.mapping_count, &s_WorkerJob->request);
		FreeUPNPUrls(&s_WorkerJob->result.urls);
	}
	free(s_WorkerJob);
	s_WorkerJob = NULL;
}

static s32 requestMatchesCurrentOwnerUnion(
	const upnp_worker_request_t *request)
{
	if (!request) return 0;
	u16 current[NET_UPNP_MAX_MAPPINGS] = {0};
	const u8 current_count = buildRequestedPorts(current);
	if (request->requested_count != current_count) return 0;
	for (u8 i = 0; i < current_count; i++) {
		if (request->requested_ports[i] != current[i]) return 0;
	}
	return 1;
}

static void deleteStaleMappingsAfterAcceptance(
	const upnp_worker_request_t *request, const struct UPNPUrls *old_urls,
	const struct IGDdatas *old_data, s32 old_urls_ready)
{
	if (!request || !old_urls_ready) return;
	for (u8 i = 0; i < NET_UPNP_MAX_MAPPINGS; i++) {
		const upnp_mapping_t *old = &request->old_mappings[i];
		if (!old->active || old->mapped_port == 0 ||
			requestedContainsPort(request, old->mapped_port)) continue;
		/* Failure conservatively leaves the old verified lease until a retry or
		 * router expiry; it cannot invalidate the accepted replacement. */
		(void)deleteMappings(old_urls, old_data, old, 1);
	}
}

static void commitWorkerIfDone(void)
{
	if (!workerIsDone()) return;
	const s32 was_active = s_UpnpActive;
	workerJoin();
	if (!s_WorkerJob) return;
	upnp_worker_result_t *result = &s_WorkerJob->result;
	upnp_worker_request_t *request = &s_WorkerJob->request;
	if (!requestMatchesCurrentOwnerUnion(request)) {
		/* The immutable result belongs to an older owner union. Discarding it
		 * removes only newly-added ports and preserves every committed mapping. */
		discardWorkerResult();
		s_UpnpStatus = s_UpnpActive ? UPNP_STATUS_SUCCESS : UPNP_STATUS_IDLE;
		return;
	}
	if (result->success && result->urls_ready) {
		s32 next_status = UPNP_STATUS_IDLE;
		s32 preserve_old = 0;
		if (!netUpnpLifecycleTransition(s_UpnpStatus,
			NET_UPNP_LIFECYCLE_WORKER_SUCCESS, was_active,
			&next_status, &preserve_old)) {
			discardWorkerResult();
			return;
		}
		(void)preserve_old;
		struct UPNPUrls old_urls = s_UpnpUrls;
		struct IGDdatas old_data = s_UpnpData;
		const s32 old_urls_ready = s_UrlsReady;
		s_UpnpUrls = result->urls;
		s_UpnpData = result->data;
		strncpy(s_ExternalIP, result->external_ip,
			sizeof(s_ExternalIP) - 1);
		s_ExternalIP[sizeof(s_ExternalIP) - 1] = '\0';
		strncpy(s_LanAddr, result->lan_addr, sizeof(s_LanAddr) - 1);
		s_LanAddr[sizeof(s_LanAddr) - 1] = '\0';
		memcpy(s_Mappings, result->mappings, sizeof(s_Mappings));
		memcpy(s_RequestedPorts, request->requested_ports,
			sizeof(s_RequestedPorts));
		s_RequestedCount = request->requested_count;
		s_UrlsReady = 1;
		s_UpnpActive = 1;
		s_RenewAtUnix = (u32)time(NULL) + NET_UPNP_RENEW_AFTER_SECONDS;
		s_UpnpStatus = next_status;
		memset(&result->urls, 0, sizeof(result->urls));
		/* The verified replacement is now the logical live set. Only after that
		 * atomic game-thread acceptance may stale entries from the old router
		 * handle be removed. Failure leaves a conservative extra lease. */
		deleteStaleMappingsAfterAcceptance(request, &old_urls, &old_data,
			old_urls_ready);
		if (old_urls_ready) FreeUPNPUrls(&old_urls);
		free(s_WorkerJob);
		s_WorkerJob = NULL;
		sysLogPrintf(LOG_NOTE,
			"UPNP: [thread] %u UDP mappings active; renewal scheduled",
			(unsigned)s_RequestedCount);
	} else {
		s32 next_status = UPNP_STATUS_IDLE;
		s32 preserve_old = 0;
		if (!netUpnpLifecycleTransition(s_UpnpStatus,
			NET_UPNP_LIFECYCLE_WORKER_FAILURE, was_active,
			&next_status, &preserve_old)) {
			s_UpnpStatus = was_active ? UPNP_STATUS_SUCCESS : UPNP_STATUS_FAILED;
		} else {
			s_UpnpStatus = next_status;
		}
		/* A failed renewal is not permission to throw away the old lease. */
		if (preserve_old || was_active) {
			s_UpnpActive = 1;
			s_RenewAtUnix = (u32)time(NULL) + NET_UPNP_RETRY_SECONDS;
		} else {
			s_UpnpActive = 0;
		}
		free(s_WorkerJob);
		s_WorkerJob = NULL;
	}
}

static s32 workerRequestMatches(const u16 *ports, u8 count)
{
	if (!s_WorkerJob || s_WorkerJob->request.requested_count != count) return 0;
	for (u8 i = 0; i < count; i++) {
		if (s_WorkerJob->request.requested_ports[i] != ports[i]) return 0;
	}
	return 1;
}

static void cancelWorkerAndDiscard(void)
{
	if (!s_WorkerRunning) return;
#ifdef _WIN32
	InterlockedExchange(&s_WorkerCancel, 1);
#else
	pthread_mutex_lock(&s_WorkerMutex);
	s_WorkerCancel = 1;
	pthread_mutex_unlock(&s_WorkerMutex);
#endif
	workerJoin();
	discardWorkerResult();
}

static void clearLiveMappings(void)
{
	if (s_UpnpActive && g_AppQuitting) {
		sysLogPrintf(LOG_NOTE,
			"UPNP: skipping mapping removal (app quitting; leases expire)");
	} else if (s_UpnpActive && s_UrlsReady) {
		(void)deleteMappings(&s_UpnpUrls, &s_UpnpData, s_Mappings,
			NET_UPNP_MAX_MAPPINGS);
	}
	if (s_UrlsReady) FreeUPNPUrls(&s_UpnpUrls);
	memset(&s_UpnpUrls, 0, sizeof(s_UpnpUrls));
	memset(&s_UpnpData, 0, sizeof(s_UpnpData));
	memset(s_Mappings, 0, sizeof(s_Mappings));
	memset(s_RequestedPorts, 0, sizeof(s_RequestedPorts));
	s_UrlsReady = 0;
	s_UpnpActive = 0;
	s_ExternalIP[0] = '\0';
	s_LanAddr[0] = '\0';
	s_RequestedCount = 0;
	s_RenewAtUnix = 0;
	s_UpnpStatus = UPNP_STATUS_IDLE;
}

static s32 reconcileOwnerUnion(void)
{
	u16 requested[NET_UPNP_MAX_MAPPINGS] = {0};
	u8 requested_count = buildRequestedPorts(requested);
	commitWorkerIfDone();
	if (s_WorkerRunning) {
		if (workerRequestMatches(requested, requested_count)) return 0;
		cancelWorkerAndDiscard();
		s_UpnpStatus = s_UpnpActive ? UPNP_STATUS_SUCCESS : UPNP_STATUS_IDLE;
	}
	if (requested_count == 0) {
		clearLiveMappings();
		return 0;
	}
	if (s_UpnpActive && requestedPortsMatch(requested, requested_count)) return 0;
	if (s_UpnpStatus == UPNP_STATUS_FAILED) s_UpnpStatus = UPNP_STATUS_IDLE;
	s32 next_status = UPNP_STATUS_IDLE;
	s32 preserve_old = 0;
	if (!netUpnpLifecycleTransition(s_UpnpStatus,
		NET_UPNP_LIFECYCLE_BEGIN_SETUP, s_UpnpActive,
		&next_status, &preserve_old)) return -1;
	(void)preserve_old;
	s_UpnpStatus = next_status;
	if (startWorker(NET_UPNP_WORK_SETUP, requested, requested_count) != 0) {
		s_UpnpStatus = s_UpnpActive ? UPNP_STATUS_SUCCESS : UPNP_STATUS_FAILED;
		return -1;
	}
	sysLogPrintf(LOG_NOTE,
		"UPNP: starting async discovery for %u required UDP mappings...",
		(unsigned)requested_count);
	return 0;
}

s32 netUpnpAcquire(net_upnp_owner_t owner, u16 port)
{
	if ((u32)owner >= NET_UPNP_OWNER_COUNT || port == 0) return -1;
	if (s_OwnerPorts[owner] == port) return reconcileOwnerUnion();
	s_OwnerPorts[owner] = port;
	return reconcileOwnerUnion();
}

void netUpnpRelease(net_upnp_owner_t owner)
{
	if ((u32)owner >= NET_UPNP_OWNER_COUNT) return;
	if (s_OwnerPorts[owner] == 0) return;
	s_OwnerPorts[owner] = 0;
	(void)reconcileOwnerUnion();
}

s32 netUpnpSetup(u16 port)
{
	return netUpnpAcquire(NET_UPNP_OWNER_MATCH_AUTHORITY, port);
}

void netUpnpTick(void)
{
	commitWorkerIfDone();
	if (s_WorkerRunning || !s_UpnpActive ||
		s_UpnpStatus != UPNP_STATUS_SUCCESS || s_RenewAtUnix == 0 ||
		(u32)time(NULL) < s_RenewAtUnix) return;
	s32 next_status = UPNP_STATUS_IDLE;
	s32 preserve_old = 0;
	if (!netUpnpLifecycleTransition(s_UpnpStatus,
		NET_UPNP_LIFECYCLE_BEGIN_RENEW, 1,
		&next_status, &preserve_old)) return;
	(void)preserve_old;
	/* Publish the transactional state before starting a worker that may finish
	 * immediately; the old lease remains live while replacement is pending. */
	s_UpnpStatus = next_status;
	u16 desired[NET_UPNP_MAX_MAPPINGS] = {0};
	u8 desired_count = buildRequestedPorts(desired);
	if (desired_count != 0 && startWorker(NET_UPNP_WORK_RENEW, desired,
		desired_count) == 0) {
		/* Keep s_UpnpActive and the old mappings live while replacement runs. */
	} else {
		s_UpnpStatus = UPNP_STATUS_SUCCESS;
	}
}

void netUpnpTeardown(void)
{
	netUpnpRelease(NET_UPNP_OWNER_MATCH_AUTHORITY);
}

const char *netUpnpGetExternalIP(void)
{
	commitWorkerIfDone();
	return s_ExternalIP;
}

s32 netUpnpIsActive(void)
{
	commitWorkerIfDone();
	return s_UpnpActive;
}

s32 netUpnpGetStatus(void)
{
	commitWorkerIfDone();
	return s_UpnpStatus;
}

u16 netUpnpGetMappedPort(u16 requested_port)
{
	commitWorkerIfDone();
	if (!s_UpnpActive || requested_port == 0) return 0;
	for (u8 i = 0; i < NET_UPNP_MAX_MAPPINGS; i++) {
		if (s_Mappings[i].active &&
			s_Mappings[i].requested_port == requested_port) {
			return s_Mappings[i].mapped_port;
		}
	}
	return 0;
}

u16 netUpnpGetOwnedMappedPort(net_upnp_owner_t owner, u16 requested_port)
{
	if ((u32)owner >= NET_UPNP_OWNER_COUNT || requested_port == 0 ||
		s_OwnerPorts[owner] != requested_port) return 0;
	return netUpnpGetMappedPort(requested_port);
}

/* -------------------------------------------------------------------------
 * HTTP public IP fallback (when UPnP fails).
 * ------------------------------------------------------------------------- */

#include <curl/curl.h>

static size_t ipWriteCallback(void *data, size_t size, size_t nmemb, void *userp)
{
	size_t total = size * nmemb;
	char *buf = (char *)userp;
	size_t curlen = strlen(buf);
	if (curlen + total >= 63) total = 63 - curlen;
	memcpy(buf + curlen, data, total);
	buf[curlen + total] = '\0';
	return size * nmemb;
}

s32 netHttpGetPublicIP(char *buf, s32 bufsize)
{
	if (!buf || bufsize < 16) return -1;
	buf[0] = '\0';
	CURL *curl = curl_easy_init();
	if (!curl) return -1;
	char tmp[64] = "";
	curl_easy_setopt(curl, CURLOPT_URL, "https://api.ipify.org");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ipWriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, tmp);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "PerfectDark-Server/1.0");
	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	if (res != CURLE_OK || tmp[0] == '\0') {
		sysLogPrintf(LOG_WARNING, "NET: HTTP IP lookup failed (curl error %d)",
			(int)res);
		return -1;
	}
	u32 a, b, c, d;
	if (sscanf(tmp, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
		sysLogPrintf(LOG_WARNING,
			"NET: HTTP IP lookup returned invalid response: '%s'", tmp);
		return -1;
	}
	strncpy(buf, tmp, (size_t)bufsize - 1);
	buf[bufsize - 1] = '\0';
	sysLogPrintf(LOG_NOTE, "NET: public IP resolved via HTTP: %s", buf);
	return 0;
}
