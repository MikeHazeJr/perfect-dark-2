// Every smoke schedule must fit inside its own timeout (B-1135).
//
// smokeParseJson in port/src/smoke_harness.c refuses a schedule whose latest
// event plus every wait_until timeout (plus one pad transition per controller
// attach or detach) is not strictly below the file's timeout. The refusal
// happens inside the game after the runner has built the install, so a bad
// fixture costs a whole runtime turn and proves nothing. This file checks
// every fixture the runner can load with the same rule, and pins the rule in
// the harness so the two cannot drift apart.
#include "catch.hpp"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

// Mirrors of the harness constants; the first test case pins them.
constexpr uint64_t kDefaultTimeoutMs = 90000;
constexpr uint64_t kPadTransitionMs = 1000;
constexpr uint64_t kTimeoutSecondsCap = 3600;

std::string readFile(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	REQUIRE(in.good());
	std::ostringstream out;
	out << in.rdbuf();
	return out.str();
}

struct JsonValue {
	enum Kind { Null, Bool, Number, String, Array, Object };
	Kind kind = Null;
	double number = 0;
	std::string text;
	std::vector<JsonValue> items;
	std::vector<std::pair<std::string, JsonValue>> members;

	const JsonValue *member(const char *name) const
	{
		if (kind != Object) {
			return nullptr;
		}
		for (const auto &m : members) {
			if (m.first == name) {
				return &m.second;
			}
		}
		return nullptr;
	}
};

// A strict reader for the fixture files: enough JSON to walk their trees.
class JsonReader {
public:
	explicit JsonReader(const std::string &src) : m_Src(src) {}

	bool parseDocument(JsonValue &out)
	{
		if (!parseValue(out, 0)) {
			return false;
		}
		skipSpace();
		return m_Pos == m_Src.size();
	}

private:
	const std::string &m_Src;
	size_t m_Pos = 0;

	bool atEnd() const { return m_Pos >= m_Src.size(); }

	void skipSpace()
	{
		while (!atEnd() && (m_Src[m_Pos] == ' ' || m_Src[m_Pos] == '\t'
				|| m_Src[m_Pos] == '\n' || m_Src[m_Pos] == '\r')) {
			m_Pos++;
		}
	}

	bool literal(const char *word)
	{
		const size_t len = std::strlen(word);
		if (m_Src.compare(m_Pos, len, word) != 0) {
			return false;
		}
		m_Pos += len;
		return true;
	}

	bool parseString(std::string &out)
	{
		if (atEnd() || m_Src[m_Pos] != '"') {
			return false;
		}
		m_Pos++;
		while (!atEnd()) {
			const char c = m_Src[m_Pos++];
			if (c == '"') {
				return true;
			}
			if (c != '\\') {
				out += c;
				continue;
			}
			if (atEnd()) {
				return false;
			}
			const char e = m_Src[m_Pos++];
			switch (e) {
			case '"': out += '"'; break;
			case '\\': out += '\\'; break;
			case '/': out += '/'; break;
			case 'b': out += '\b'; break;
			case 'f': out += '\f'; break;
			case 'n': out += '\n'; break;
			case 'r': out += '\r'; break;
			case 't': out += '\t'; break;
			case 'u':
				if (m_Pos + 4 > m_Src.size()) {
					return false;
				}
				for (size_t i = 0; i < 4; i++) {
					if (!std::isxdigit(static_cast<unsigned char>(m_Src[m_Pos + i]))) {
						return false;
					}
				}
				m_Pos += 4;
				out += '?';
				break;
			default:
				return false;
			}
		}
		return false;
	}

	bool parseNumber(double &out)
	{
		const size_t start = m_Pos;
		if (!atEnd() && m_Src[m_Pos] == '-') {
			m_Pos++;
		}
		while (!atEnd() && (std::isdigit(static_cast<unsigned char>(m_Src[m_Pos]))
				|| m_Src[m_Pos] == '.' || m_Src[m_Pos] == 'e' || m_Src[m_Pos] == 'E'
				|| m_Src[m_Pos] == '+' || m_Src[m_Pos] == '-')) {
			m_Pos++;
		}
		if (m_Pos == start) {
			return false;
		}
		try {
			size_t used = 0;
			const std::string token = m_Src.substr(start, m_Pos - start);
			out = std::stod(token, &used);
			return used == token.size();
		} catch (...) {
			return false;
		}
	}

	bool parseValue(JsonValue &out, int depth)
	{
		if (depth > 64) {
			return false;
		}
		skipSpace();
		if (atEnd()) {
			return false;
		}
		const char c = m_Src[m_Pos];
		if (c == '{' || c == '[') {
			const bool object = c == '{';
			const char close = object ? '}' : ']';
			out.kind = object ? JsonValue::Object : JsonValue::Array;
			m_Pos++;
			skipSpace();
			if (!atEnd() && m_Src[m_Pos] == close) {
				m_Pos++;
				return true;
			}
			for (;;) {
				std::string key;
				if (object) {
					skipSpace();
					if (!parseString(key)) {
						return false;
					}
					skipSpace();
					if (atEnd() || m_Src[m_Pos] != ':') {
						return false;
					}
					m_Pos++;
				}
				JsonValue value;
				if (!parseValue(value, depth + 1)) {
					return false;
				}
				if (object) {
					out.members.emplace_back(std::move(key), std::move(value));
				} else {
					out.items.push_back(std::move(value));
				}
				skipSpace();
				if (atEnd()) {
					return false;
				}
				if (m_Src[m_Pos] == ',') {
					m_Pos++;
					continue;
				}
				if (m_Src[m_Pos] == close) {
					m_Pos++;
					return true;
				}
				return false;
			}
		}
		if (c == '"') {
			out.kind = JsonValue::String;
			return parseString(out.text);
		}
		if (literal("true") || literal("false")) {
			out.kind = JsonValue::Bool;
			return true;
		}
		if (literal("null")) {
			out.kind = JsonValue::Null;
			return true;
		}
		out.kind = JsonValue::Number;
		return parseNumber(out.number);
	}
};

uint64_t memberMs(const JsonValue &event, const char *name)
{
	const JsonValue *v = event.member(name);
	if (v == nullptr || v->kind != JsonValue::Number || v->number <= 0) {
		return 0;
	}
	return static_cast<uint64_t>(v->number);
}

std::string memberText(const JsonValue &event, const char *name)
{
	const JsonValue *v = event.member(name);
	return v != nullptr && v->kind == JsonValue::String ? v->text : std::string();
}

struct ScheduleBound {
	uint64_t timeout_ms = kDefaultTimeoutMs;
	uint64_t latest_event_ms = 0;
	uint64_t wait_budget_ms = 0;
};

// The same arithmetic as smokeParseJson. Screenshot entries count toward the
// latest event whether or not the runner gives them an output folder, which
// can only make this check stricter than the harness.
ScheduleBound scheduleBound(const JsonValue &doc)
{
	ScheduleBound bound;
	if (const JsonValue *secs = doc.member("timeout_seconds");
			secs != nullptr && secs->kind == JsonValue::Number && secs->number > 0) {
		uint64_t s = static_cast<uint64_t>(secs->number);
		if (s > kTimeoutSecondsCap) {
			s = kTimeoutSecondsCap;
		}
		bound.timeout_ms = s * 1000;
	}
	if (const JsonValue *seq = doc.member("input_sequence");
			seq != nullptr && seq->kind == JsonValue::Array) {
		for (const JsonValue &event : seq->items) {
			const uint64_t at = memberMs(event, "at_ms");
			if (at > bound.latest_event_ms) {
				bound.latest_event_ms = at;
			}
			const std::string type = memberText(event, "type");
			if (type == "wait_until") {
				bound.wait_budget_ms += memberMs(event, "timeout_ms");
			} else if (type == "controller_attach" || type == "controller_detach") {
				bound.wait_budget_ms += kPadTransitionMs;
			}
		}
	}
	if (const JsonValue *shots = doc.member("screenshots");
			shots != nullptr && shots->kind == JsonValue::Array) {
		for (const JsonValue &shot : shots->items) {
			const uint64_t at = memberMs(shot, "at_ms");
			if (at > bound.latest_event_ms) {
				bound.latest_event_ms = at;
			}
		}
	}
	return bound;
}

// What the harness actually executes: a test file, or a per-process fixture
// named by a test file's smoke_path.
bool isSchedule(const JsonValue &doc)
{
	return doc.kind == JsonValue::Object
		&& (doc.member("input_sequence") != nullptr || doc.member("screenshots") != nullptr);
}

} // namespace

TEST_CASE("the smoke harness bounds a schedule by its latest event plus every wait",
	"[smoke][tooling][fixtures][smoke-schedule][b1135][static]")
{
	const std::string source = readFile(fs::path(PD_SOURCE_DIR) / "port" / "src" / "smoke_harness.c");
	CHECK(source.find("#define SMOKE_DEFAULT_TIMEOUT_MS 90000") != std::string::npos);
	CHECK(source.find("#define SMOKE_PAD_TRANSITION_MS 1000u") != std::string::npos);
	CHECK(source.find("if (secs > 3600) secs = 3600;") != std::string::npos);
	CHECK(source.find("wait_budget_ms += (u64)ev->wait_timeout_ms;") != std::string::npos);
	CHECK(source.find("wait_budget_ms += SMOKE_PAD_TRANSITION_MS;") != std::string::npos);
	CHECK(source.find("latest_event_ms + wait_budget_ms >= (u64)s_State.timeout_ms") != std::string::npos);
	CHECK(source.find("!strcmp(field, \"timeout_seconds\")") != std::string::npos);
	CHECK(source.find("!strcmp(field, \"input_sequence\")") != std::string::npos);
	CHECK(source.find("!strcmp(field, \"screenshots\")") != std::string::npos);
}

TEST_CASE("every smoke schedule fits inside its own timeout",
	"[smoke][tooling][fixtures][smoke-schedule][b1135][static]")
{
	const fs::path base = fs::path(PD_SOURCE_DIR) / "tools" / "smoke-verify";
	size_t schedules = 0;
	for (const char *dir : {"tests", "fixtures"}) {
		const fs::path root = base / dir;
		REQUIRE(fs::is_directory(root));
		for (const fs::directory_entry &entry : fs::recursive_directory_iterator(root)) {
			if (!entry.is_regular_file() || entry.path().extension() != ".json") {
				continue;
			}
			const std::string text = readFile(entry.path());
			JsonValue doc;
			JsonReader reader(text);
			const std::string name = fs::relative(entry.path(), base).generic_string();
			CAPTURE(name);
			REQUIRE(reader.parseDocument(doc));
			if (!isSchedule(doc)) {
				continue;
			}
			schedules++;
			const ScheduleBound bound = scheduleBound(doc);
			if (bound.wait_budget_ms == 0) {
				continue; // the harness applies the rule only to schedules that wait
			}
			const uint64_t total = bound.latest_event_ms + bound.wait_budget_ms;
			CAPTURE(bound.timeout_ms, bound.latest_event_ms, bound.wait_budget_ms, total);
			CHECK(total < bound.timeout_ms);
		}
	}
	// Hundreds of schedules exist; a near-empty count means the walk broke.
	CHECK(schedules > 100);
}
