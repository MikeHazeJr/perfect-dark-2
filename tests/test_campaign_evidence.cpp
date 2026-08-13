#include "catch.hpp"

#include <filesystem>
#include <fstream>
#include <string>

extern "C" {
#include "campaign_evidence.h"
}

namespace fs = std::filesystem;

static std::string readCampaignEvidence(const fs::path &path)
{
	std::ifstream input(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>());
}

TEST_CASE("campaign evidence atomically records live and persisted proof",
	"[campaign-run][campaign-evidence][t-tests-002]")
{
	const char *const plan[] = { "base:defection" };
	campaign_run_t run{};
	REQUIRE(campaignRunInit(&run, 0, 0, "smoke", plan, 1) == 1);
	REQUIRE(campaignRunRecordLoaded(&run, 0, plan[0], 0x30,
		0, 3, 2, 1, 1) == 1);
	REQUIRE(campaignRunRecordCompleted(&run, 0, plan[0],
		2, 7, 0, 12, 1) == 1);
	REQUIRE(campaignRunRecordRoute(&run, 0, plan[0],
		"system:credits", 1, 0) == 1);
	REQUIRE(campaignRunRecordCredits(&run, 1, 1) == 1);

	const fs::path dir = fs::temp_directory_path() / "pd2-campaign-evidence";
	fs::remove_all(dir);
	fs::create_directories(dir);
	const fs::path report = dir / "campaign_release_evidence.json";
	const u16 persisted[] = { 12 };
	const std::string reportString = report.string();
	campaign_evidence_options_t options{
		reportString.c_str(), "complete", 0, persisted, 1
	};
	char error[160]{};
	REQUIRE(campaignEvidenceWrite(&run, &options, error,
		static_cast<s32>(sizeof(error))) == 0);

	const std::string json = readCampaignEvidence(report);
	REQUIRE(json.find("\"schema\": \"pd2.campaign.release-evidence\"") !=
		std::string::npos);
	REQUIRE(json.find("\"status\": \"complete\"") != std::string::npos);
	REQUIRE(json.find("\"phase\": \"complete\"") != std::string::npos);
	REQUIRE(json.find("\"expected_stage_id\": \"base:defection\"") !=
		std::string::npos);
	REQUIRE(json.find("\"persisted_besttime\": 12") != std::string::npos);

	fs::remove_all(dir);
}
