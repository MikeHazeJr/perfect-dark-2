/**
 * netstun.c -- transport-neutral STUN helpers and fail-closed route facade.
 *
 * Candidate STUN is sent and demultiplexed by the actual bound ICE socket in
 * p2p_ice.c. A second socket cannot safely bind ENet's numeric listen port:
 * Address sharing makes response ownership ambiguous, while an unrelated
 * ephemeral socket cannot prove match-route provenance. The legacy async
 * route entry point is therefore retained for ABI compatibility but fails
 * closed until the ENet transport exposes an owned transaction/demux path.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <PR/ultratypes.h>
#include "system.h"
#include "net/netstun.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#endif

static const char *const s_StunServers[] = {
	"stun.l.google.com:19302",
	"stun1.l.google.com:19302",
	"stun.cloudflare.com:3478",
	NULL
};

static s32 s_StunStatus = STUN_STATUS_IDLE;
static s32 s_StunNatType = STUN_NAT_UNKNOWN;
static u16 s_StunPort;
static u16 s_BindPort;
static char s_StunExternalIP[64];

#define STUN_MAGIC_COOKIE 0x2112A442u
#define STUN_HEADER_SIZE 20
#define STUN_ATTRIBUTE_HEADER_SIZE 4
#define STUN_MESSAGE_BIND_RESPONSE 0x0101u
#define STUN_ATTRIBUTE_MAPPED_ADDRESS 0x0001u
#define STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS 0x0020u
#define STUN_FAMILY_IPV4 0x01u

static void stunResetState(void)
{
	s_StunStatus = STUN_STATUS_IDLE;
	s_StunNatType = STUN_NAT_UNKNOWN;
	s_StunPort = 0;
	s_BindPort = 0;
	s_StunExternalIP[0] = '\0';
}

void stunBindingRequestBuild(const u8 transaction_id[STUN_TRANSACTION_ID_LEN],
	u8 out_request[STUN_BINDING_REQUEST_LEN])
{
	if (!transaction_id || !out_request) return;
	out_request[0] = 0x00;
	out_request[1] = 0x01;
	out_request[2] = 0x00;
	out_request[3] = 0x00;
	out_request[4] = 0x21;
	out_request[5] = 0x12;
	out_request[6] = 0xa4;
	out_request[7] = 0x42;
	memcpy(out_request + 8, transaction_id, STUN_TRANSACTION_ID_LEN);
}

s32 stunBindingResponseParse(const u8 *packet, s32 packet_len,
	const u8 transaction_id[STUN_TRANSACTION_ID_LEN],
	u32 *out_ipv4, u16 *out_port)
{
	if (!packet || !transaction_id || !out_ipv4 || !out_port ||
		packet_len < STUN_HEADER_SIZE) return 0;
	const u16 message_type = ((u16)packet[0] << 8) | packet[1];
	const u16 message_len = ((u16)packet[2] << 8) | packet[3];
	const u32 cookie = ((u32)packet[4] << 24) |
		((u32)packet[5] << 16) | ((u32)packet[6] << 8) | packet[7];
	if (message_type != STUN_MESSAGE_BIND_RESPONSE ||
		cookie != STUN_MAGIC_COOKIE ||
		memcmp(packet + 8, transaction_id, STUN_TRANSACTION_ID_LEN) != 0 ||
		STUN_HEADER_SIZE + (s32)message_len > packet_len) return 0;

	u32 mapped_ipv4 = 0;
	u16 mapped_port = 0;
	s32 have_xor = 0;
	const s32 end = STUN_HEADER_SIZE + (s32)message_len;
	for (s32 offset = STUN_HEADER_SIZE;
		offset + STUN_ATTRIBUTE_HEADER_SIZE <= end;) {
		const u16 type = ((u16)packet[offset] << 8) | packet[offset + 1];
		const u16 length = ((u16)packet[offset + 2] << 8) |
			packet[offset + 3];
		offset += STUN_ATTRIBUTE_HEADER_SIZE;
		if (offset + (s32)length > end) return 0;
		if (length >= 8 && packet[offset + 1] == STUN_FAMILY_IPV4 &&
			type == STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS) {
			mapped_port = (u16)((((u16)packet[offset + 2] << 8) |
				packet[offset + 3]) ^ 0x2112u);
			const u32 encoded_ipv4 = ((u32)packet[offset + 4] << 24) |
				((u32)packet[offset + 5] << 16) |
				((u32)packet[offset + 6] << 8) | packet[offset + 7];
			mapped_ipv4 = encoded_ipv4 ^ STUN_MAGIC_COOKIE;
			have_xor = 1;
		} else if (!have_xor && length >= 8 &&
			packet[offset + 1] == STUN_FAMILY_IPV4 &&
			type == STUN_ATTRIBUTE_MAPPED_ADDRESS) {
			mapped_port = ((u16)packet[offset + 2] << 8) |
				packet[offset + 3];
			mapped_ipv4 = ((u32)packet[offset + 4] << 24) |
				((u32)packet[offset + 5] << 16) |
				((u32)packet[offset + 6] << 8) | packet[offset + 7];
		}
		offset += (s32)((length + 3u) & ~3u);
	}
	if (mapped_ipv4 == 0 || mapped_port == 0) return 0;
	*out_ipv4 = mapped_ipv4;
	*out_port = mapped_port;
	return 1;
}

static s32 stunResolveServer(const char *host_port, struct sockaddr_in *out)
{
	if (!host_port || !out) return 0;
	char host[256];
	char port_text[8];
	strncpy(host, host_port, sizeof(host) - 1);
	host[sizeof(host) - 1] = '\0';
	u16 port = 3478;
	char *colon = strrchr(host, ':');
	if (colon) {
		*colon = '\0';
		port = (u16)atoi(colon + 1);
	}
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	snprintf(port_text, sizeof(port_text), "%u", (unsigned)port);
	struct addrinfo *resolved = NULL;
	if (getaddrinfo(host, port_text, &hints, &resolved) != 0 || !resolved) {
		return 0;
	}
	memcpy(out, resolved->ai_addr, sizeof(*out));
	freeaddrinfo(resolved);
	return 1;
}

u32 stunServerCount(void)
{
	u32 count = 0;
	while (s_StunServers[count]) count++;
	return count;
}

s32 stunResolveServerAt(u32 server_index, u32 *out_ipv4, u16 *out_port)
{
	struct sockaddr_in address;
	if (!out_ipv4 || !out_port || server_index >= stunServerCount() ||
		!stunResolveServer(s_StunServers[server_index], &address)) return 0;
	*out_ipv4 = ntohl(address.sin_addr.s_addr);
	*out_port = ntohs(address.sin_port);
	return *out_ipv4 != 0 && *out_port != 0;
}

void stunInit(void)
{
	stunResetState();
}

void stunShutdown(void)
{
	stunResetState();
}

s32 stunDiscoverAsync(u16 localport)
{
	stunResetState();
	s_StunStatus = STUN_STATUS_FAILED;
	sysLogPrintf(LOG_WARNING,
		"STUN: route discovery for ENet port %u rejected; transport-owned demux required",
		(unsigned)localport);
	return -1;
}

s32 stunGetStatus(void)
{
	return s_StunStatus;
}

s32 stunGetNatType(void)
{
	return s_StunNatType;
}

const char *stunGetExternalIP(void)
{
	return s_StunExternalIP;
}

u16 stunGetExternalPort(void)
{
	return s_StunPort;
}

u16 stunGetDiscoveryPort(void)
{
	return s_BindPort;
}

void stunCancel(void)
{
	/* No route worker or socket exists. Preserve the fail-closed result. */
}
