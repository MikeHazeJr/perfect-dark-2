#include "catch.hpp"

extern "C" {
#include "net/net_bandwidth.h"
}

#include <fstream>
#include <sstream>
#include <string>

static std::string readBandwidthSource(const char *path)
{
	std::ifstream stream(path, std::ios::binary);
	std::ostringstream out;
	out << stream.rdbuf();
	return out.str();
}

TEST_CASE("passive upload meter samples real cumulative ENet bytes",
		"[networking][t-networking-004][b1057]")
{
	net_upload_meter_t meter{};
	REQUIRE(netUploadMeterSample(&meter, 1000, 500) == 0);
	REQUIRE(netUploadMeterSample(&meter, 2999, 125499) == 0);
	REQUIRE(netUploadMeterSample(&meter, 3000, 125500) == 500);

	/* A complete quiet interval resets the window but does not fabricate a
	 * capability measurement from a configured rate. */
	REQUIRE(netUploadMeterSample(&meter, 5000, 126000) == 0);
	REQUIRE(netUploadMeterSample(&meter, 7000, 251000) == 500);
}

TEST_CASE("passive upload meter handles counter wrap and caps hostile values",
		"[networking][t-networking-004][b1057]")
{
	net_upload_meter_t meter{};
	netUploadMeterReset(&meter, 0, 0xfffffff0u);
	REQUIRE(netUploadMeterSample(&meter, 2000, 0x00001000u) == 16);

	netUploadMeterReset(&meter, 0, 0);
	REQUIRE(netUploadMeterSample(&meter, 2000, 0xffffffffu) ==
		NET_UPLOAD_KBPS_MAX);
}

TEST_CASE("persisted upload evidence expires and rejects future or oversized data",
		"[networking][t-networking-004][b1057]")
{
	const u32 measured = 1000000;
	REQUIRE(netUploadKbpsIfFresh(900, measured, measured) == 900);
	REQUIRE(netUploadKbpsIfFresh(900, measured,
		measured + NET_UPLOAD_REPORT_MAX_AGE_S) == 900);
	REQUIRE(netUploadKbpsIfFresh(900, measured,
		measured + NET_UPLOAD_REPORT_MAX_AGE_S + 1) == 0);
	REQUIRE(netUploadKbpsIfFresh(900, measured, measured - 1) == 0);
	REQUIRE(netUploadKbpsIfFresh(NET_UPLOAD_KBPS_MAX + 1, measured,
		measured) == 0);
}

TEST_CASE("authority uses speed then initiator then stable public handle",
		"[networking][t-networking-004][b1057]")
{
	net_authority_candidate_t candidates[] = {
		{ 30, 700, 1, 1 },
		{ 20, 900, 1, 0 },
		{ 10, 900, 1, 0 },
		{  5, 5000, 0, 0 },
	};
	net_authority_choice_t choice{};

	REQUIRE(netBandwidthChooseAuthority(candidates, 4, 20, &choice) == 1);
	REQUIRE(choice.handle == 20);
	REQUIRE(choice.kbps == 900);
	REQUIRE(choice.is_local == 0);

	REQUIRE(netBandwidthChooseAuthority(candidates, 4, 0, &choice) == 1);
	REQUIRE(choice.handle == 10);

	for (auto &candidate : candidates) candidate.kbps = 0;
	REQUIRE(netBandwidthChooseAuthority(candidates, 4, 30, &choice) == 1);
	REQUIRE(choice.handle == 30);
	REQUIRE(choice.is_local == 1);

	candidates[0].eligible = 0;
	REQUIRE(netBandwidthChooseAuthority(candidates, 4, 30, &choice) == 1);
	REQUIRE(choice.handle == 10);
}

TEST_CASE("bandwidth evidence is wired through persistence presence aging and TURN",
		"[networking][t-networking-004][b1057][static]")
{
	const std::string net = readBandwidthSource("port/src/net/net.c");
	const std::string presence = readBandwidthSource("port/src/presence.c");
	const std::string group = readBandwidthSource("port/src/net/group_session.c");
	const std::string turn = readBandwidthSource("port/src/net/p2p_turn.c");

	REQUIRE(net.find("g_NetHost->totalSentData") != std::string::npos);
	REQUIRE(net.find("Net.UploadKbpsEstimate") != std::string::npos);
	REQUIRE(net.find("Net.UploadKbpsMeasuredAt") != std::string::npos);
	REQUIRE(net.find("netUploadMeasurementTick();") != std::string::npos);

	REQUIRE(presence.find("PRESENCE_VERSION          4") != std::string::npos);
	REQUIRE(presence.find("pd-presence-v4") != std::string::npos);
	REQUIRE(presence.find("PRESENCE_UPLOAD_KBPS_OFFSET") != std::string::npos);
	REQUIRE(presence.find("wU32(&upload_w, netUploadKbpsEstimate())") !=
		std::string::npos);
	REQUIRE(presence.find("groupSessionUpdateKbps(handle, p->upload_kbps)") !=
		std::string::npos);

	REQUIRE(group.find("placeholder for local kbps") == std::string::npos);
	REQUIRE(group.find("GROUP_KBPS_FRESH_MS") != std::string::npos);
	REQUIRE(group.find("netBandwidthChooseAuthority") != std::string::npos);
	REQUIRE(group.find("p2pTurnRegisterRelayCandidate(handle, 0, 0, 0)") !=
		std::string::npos);
	REQUIRE(turn.find("ipv4 == 0 || port == 0") != std::string::npos);
}
