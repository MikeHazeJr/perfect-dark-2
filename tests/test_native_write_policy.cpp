#include "catch.hpp"
#include "native_write_policy.h"
#include <windows.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
namespace fs = std::filesystem;
std::string utf8(const fs::path &path) {
    auto text = path.generic_u8string();
    return std::string(reinterpret_cast<const char *>(text.data()), text.size());
}
std::string bytes(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
struct Fixture {
    fs::path root, base, profile, source;
    FILETIME writeTime;
    Fixture() {
        nativeWritePolicyResetForTests();
        FILETIME time;
        GetSystemTimePreciseAsFileTime(&time);
        static unsigned serial = 0;
        char name[100];
        snprintf(name, sizeof(name), "%lu-%08lx%08lx-%u", GetCurrentProcessId(),
            time.dwHighDateTime, time.dwLowDateTime, ++serial);
        root = fs::current_path() / ".claude" / "native-write-policy-tests" / name;
        base = root / "base";
        profile = root / "profile";
        fs::create_directories(base);
        fs::create_directories(profile);
        source = base / "source.pdmesh";
        std::ofstream(source, std::ios::binary) << "editable public source sentinel";
        std::ofstream(profile / ".pd-reuse-owner.json") << "protected owner sentinel";
        WIN32_FILE_ATTRIBUTE_DATA info;
        REQUIRE(GetFileAttributesExW(base.c_str(), GetFileExInfoStandard, &info));
        writeTime = info.ftLastWriteTime;
    }
    ~Fixture() { nativeWritePolicyResetForTests(); } // Tiny evidence is retained.
    void activate() { REQUIRE(nativeWritePolicyInit(utf8(base).c_str(), utf8(profile).c_str())); }
    void unchanged() {
        REQUIRE(bytes(source) == "editable public source sentinel");
        REQUIRE(bytes(profile / ".pd-reuse-owner.json") == "protected owner sentinel");
        WIN32_FILE_ATTRIBUTE_DATA info;
        REQUIRE(GetFileAttributesExW(base.c_str(), GetFileExInfoStandard, &info));
        REQUIRE(CompareFileTime(&writeTime, &info.ftLastWriteTime) == 0);
        REQUIRE(!fs::exists(base / "new-cache"));
    }
};
}

TEST_CASE("native write policy rejects invalid and overlapping roots before activation", "[native-reuse][write-policy]") {
    Fixture f;
    REQUIRE(nativeWritePolicyLinkContract());
    CHECK_FALSE(nativeWritePolicyInit(utf8(f.base).c_str(), utf8(f.base).c_str()));
    CHECK_FALSE(nativeWritePolicyInit("C:\\", utf8(f.profile).c_str()));
    CHECK_FALSE(nativeWritePolicyInit("base", utf8(f.profile).c_str()));
    fs::create_directories(f.base / "nested");
    CHECK_FALSE(nativeWritePolicyInit(utf8(f.base).c_str(), utf8(f.base / "nested").c_str()));
    CHECK_FALSE(nativeWritePolicyActive());
}

TEST_CASE("recognized invalid device opens latch refusal instead of hiding devices", "[native-reuse][write-policy]") {
    Fixture f; f.activate();
    CHECK_FALSE(nativeWritePolicyRefused());
    HANDLE device = CreateFileW(L"\\\\?\\HID#pd2-synthetic-invalid-device", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_FLAG_OVERLAPPED, nullptr);
    CHECK(device == INVALID_HANDLE_VALUE);
    CHECK(nativeWritePolicyRefused());
    CHECK(nativeWritePolicyRefused());
    f.unchanged();
}

TEST_CASE("inherited Win32 and CRT outputs cannot mutate authored source on activation refusal", "[native-reuse][write-policy]") {
    SECTION("Win32 standard handle") {
        Fixture f;
        HANDLE inherited = CreateFileW(f.source.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        REQUIRE(inherited != INVALID_HANDLE_VALUE);
        const HANDLE original = GetStdHandle(STD_ERROR_HANDLE);
        REQUIRE(SetStdHandle(STD_ERROR_HANDLE, inherited));
        const bool admitted = nativeWritePolicyInit(utf8(f.base).c_str(), utf8(f.profile).c_str());
        const bool guarded = nativeWritePolicyActive();
        DWORD written = 0;
        const bool wrote = WriteFile(inherited, "forbidden diagnostic", 20, &written, nullptr);
        const bool restored = SetStdHandle(STD_ERROR_HANDLE, original);
        CloseHandle(inherited);
        REQUIRE(restored);
        CHECK_FALSE(admitted);
        CHECK(guarded);
        CHECK_FALSE(wrote);
        CHECK(written == 0);
        f.unchanged();
    }
    SECTION("CRT descriptor independently of Win32 standard handle") {
        Fixture f;
        const int original = _dup(_fileno(stderr));
        REQUIRE(original >= 0);
        const int inherited = _open(utf8(f.source).c_str(), _O_WRONLY | _O_APPEND | _O_BINARY);
        REQUIRE(inherited >= 0);
        REQUIRE(_dup2(inherited, _fileno(stderr)) == 0);
        _close(inherited);
        const bool admitted = nativeWritePolicyInit(utf8(f.base).c_str(), utf8(f.profile).c_str());
        const bool guarded = nativeWritePolicyActive();
        const int written = _write(_fileno(stderr), "forbidden diagnostic", 20);
        const int restored = _dup2(original, _fileno(stderr));
        _close(original);
        REQUIRE(restored == 0);
        CHECK_FALSE(admitted);
        CHECK(guarded);
        CHECK(written < 0);
        f.unchanged();
    }
}

TEST_CASE("ordinary absent IPC endpoints preserve OS failure without policy refusal", "[native-reuse][write-policy]") {
    Fixture f; f.activate();
    wchar_t name[160];
    swprintf(name, 160, L"\\\\.\\pipe\\pd2-policy-absent-%lu-%llu", GetCurrentProcessId(), GetTickCount64());
    CHECK(CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, 0, nullptr) == INVALID_HANDLE_VALUE);
    CHECK(GetLastError() != ERROR_ACCESS_DENIED);
    CHECK_FALSE(nativeWritePolicyRefused());
    f.unchanged();
}

TEST_CASE("owned synthetic named pipe remains IPC under active policy", "[native-reuse][write-policy]") {
    Fixture f;
    wchar_t name[160];
    swprintf(name, 160, L"\\\\.\\pipe\\pd2-policy-%lu-%llu", GetCurrentProcessId(), GetTickCount64());
    HANDLE server = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE,
        1, 1024, 1024, 1000, nullptr);
    REQUIRE(server != INVALID_HANDLE_VALUE);
    f.activate();
    HANDLE client = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(client != INVALID_HANDLE_VALUE);
    if (client != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        CHECK(WriteFile(client, "ipc", 3, &written, nullptr));
        CHECK(written == 3);
        CloseHandle(client);
    }
    CHECK_FALSE(nativeWritePolicyRefused());
    CloseHandle(server);
    f.unchanged();
}

TEST_CASE("native C file opens allow profile caches and refuse base mutation before truncate", "[native-reuse][write-policy]") {
    Fixture f; f.activate();
    for (const char *mode : {"wb", "ab", "rb+", "wb+"}) {
        CHECK(fopen(utf8(f.source).c_str(), mode) == nullptr);
        CHECK(_wfopen(f.source.c_str(), std::wstring(mode, mode + strlen(mode)).c_str()) == nullptr);
    }
    CHECK(fopen(utf8(f.base / "new-cache").c_str(), "wb") == nullptr);
    CHECK(_open(utf8(f.source).c_str(), _O_RDWR | _O_TRUNC) < 0);
    FILE *out = fopen(utf8(f.profile / "generated").c_str(), "wb");
    REQUIRE(out);
    CHECK(fwrite("cache", 1, 5, out) == 5);
    CHECK(fclose(out) == 0);
    CHECK(bytes(f.profile / "generated") == "cache");
    f.unchanged();
}

TEST_CASE("native C++ and CRT sharing opens use the same write policy", "[native-reuse][write-policy]") {
    Fixture f; f.activate();
    std::ofstream forbidden(f.source, std::ios::binary | std::ios::trunc);
    CHECK_FALSE(forbidden.is_open());
    std::ofstream allowed(f.profile / "cplusplus", std::ios::binary);
    REQUIRE(allowed.is_open()); allowed << "derived"; allowed.close();
    CHECK(bytes(f.profile / "cplusplus") == "derived");
    CHECK(_fsopen(utf8(f.source).c_str(), "wb", _SH_DENYNO) == nullptr);
    CHECK(_sopen(utf8(f.source).c_str(), _O_WRONLY | _O_TRUNC, _SH_DENYNO) < 0);
    f.unchanged();
}

TEST_CASE("native Win32 imported writers refuse base replacement creation and deletion", "[native-reuse][write-policy]") {
    Fixture f;
    auto emptySource = f.root / "empty-source";
    REQUIRE(fs::create_directory(emptySource));
    f.activate();
    CHECK(rmdir(utf8(emptySource).c_str()) < 0);
    CHECK(fs::is_directory(emptySource));
    for (DWORD disposition : {CREATE_ALWAYS, TRUNCATE_EXISTING, OPEN_ALWAYS})
        CHECK(CreateFileW(f.source.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            disposition, FILE_ATTRIBUTE_NORMAL, nullptr) == INVALID_HANDLE_VALUE);
    CHECK(CreateFileA(utf8(f.base / "new-cache").c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr) == INVALID_HANDLE_VALUE);
    CHECK_FALSE(CreateDirectoryW((f.base / "new-cache").c_str(), nullptr));
    CHECK_FALSE(DeleteFileA(utf8(f.source).c_str()));
    CHECK_FALSE(CopyFileW(f.source.c_str(), (f.base / "new-cache").c_str(), FALSE));
    CHECK_FALSE(MoveFileExA(utf8(f.source).c_str(), utf8(f.profile / "moved").c_str(), MOVEFILE_REPLACE_EXISTING));
    CHECK_FALSE(MoveFileExW((f.profile / "missing").c_str(), f.source.c_str(), MOVEFILE_REPLACE_EXISTING));
    CHECK(remove(utf8(f.source).c_str()) < 0);
    CHECK(rename(utf8(f.source).c_str(), utf8(f.profile / "moved").c_str()) < 0);
    CHECK(_mkdir(utf8(f.base / "new-cache").c_str()) < 0);
    f.unchanged();
}

TEST_CASE("native write policy checks descriptors acquired before activation", "[native-reuse][write-policy]") {
    Fixture f;
    int fd = _open(utf8(f.source).c_str(), _O_RDWR | _O_BINARY);
    REQUIRE(fd >= 0);
    FILE *file = _fdopen(fd, "rb+");
    REQUIRE(file);
    HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    f.activate();
    CHECK(_chsize_s(fd, 0) != 0);
    CHECK(_chsize(fd, 0) < 0);
    CHECK(_write(fd, "bad", 3) < 0);
    CHECK(fwrite("bad", 1, 3, file) == 0);
    DWORD written = 9;
    CHECK_FALSE(WriteFile(handle, "bad", 3, &written, nullptr));
    CHECK(written == 0);
    CHECK_FALSE(SetEndOfFile(handle));
    CHECK(fclose(file) == 0);
    f.unchanged();
}

TEST_CASE("native write policy rejects lexical and device aliases and protects ownership", "[native-reuse][write-policy]") {
    Fixture f; f.activate();
    for (const char *suffix : {"../base/source.pdmesh", "NUL", "con.txt", "LPT1.bin",
        "COM1", "stream:other", "trailing.", "trailing ", ".pd-reuse-owner.json", "$B/file"}) {
        auto path = utf8(f.profile) + "/" + suffix;
        CHECK_FALSE(nativeWritePolicyAllowsPath(path.c_str()));
        CHECK(fopen(path.c_str(), "wb") == nullptr);
    }
    CHECK_FALSE(nativeWritePolicyAllowsPath((utf8(f.profile) + "-sibling/out").c_str()));
    CHECK_FALSE(nativeWritePolicyAllowsPath("\\\\?\\C:\\outside"));
    f.unchanged();
}

TEST_CASE("native write policy refuses a hardlink from profile to authored base", "[native-reuse][write-policy]") {
    Fixture f;
    auto alias = f.profile / "linked";
    REQUIRE(CreateHardLinkW(alias.c_str(), f.source.c_str(), nullptr));
    f.activate();
    CHECK_FALSE(nativeWritePolicyAllowsPath(utf8(alias).c_str()));
    CHECK(_wfopen(alias.c_str(), L"wb") == nullptr);
    CHECK(CreateFileW(alias.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) == INVALID_HANDLE_VALUE);
    f.unchanged();
}

TEST_CASE("native generated cache paths are deterministic profile products", "[native-reuse][write-policy][c3842]") {
    Fixture f; f.activate();
    char a[2048], b[2048], other[2048];
    REQUIRE(nativeWritePolicyCachePath("extraction-sha256", utf8(f.source).c_str(), a, sizeof(a)));
    REQUIRE(nativeWritePolicyCachePath("extraction-sha256", utf8(f.source).c_str(), b, sizeof(b)));
    REQUIRE(nativeWritePolicyCachePath("pdextract-stamp", utf8(f.base).c_str(), other, sizeof(other)));
    CHECK(std::string(a) == b); CHECK(std::string(a) != other);
    CHECK(fs::path(a).parent_path().parent_path().parent_path() == f.profile);
    CHECK_FALSE(nativeWritePolicyCachePath("../base", "id", b, sizeof(b)));
    CHECK_FALSE(nativeWritePolicyCachePath("cache", "id", b, 4));
    FILE *cache = fopen(a, "wb"); REQUIRE(cache);
    REQUIRE(fwrite("source-hashed derived cache", 1, 26, cache) == 26);
    REQUIRE(fclose(cache) == 0);
    f.unchanged();
}

TEST_CASE("native profile outputs support Unicode without changing base", "[native-reuse][write-policy]") {
    Fixture f;
    auto wideProfile = f.root / L"profile \u03a9";
    fs::create_directory(wideProfile);
    REQUIRE(nativeWritePolicyInit(utf8(f.base).c_str(), utf8(wideProfile).c_str()));
    auto output = wideProfile / L"derived \u03a9";
    FILE *file = _wfopen(output.c_str(), L"wb"); REQUIRE(file);
    CHECK(fputs("unicode-cache", file) >= 0); CHECK(fclose(file) == 0);
    CHECK(bytes(output) == "unicode-cache");
    f.unchanged();
}

TEST_CASE("native directory locks prevent active profile root replacement", "[native-reuse][write-policy]") {
    Fixture f; f.activate();
    CHECK_FALSE(MoveFileExW(f.profile.c_str(), (f.root / "renamed").c_str(), 0));
    CHECK_FALSE(RemoveDirectoryW(f.profile.c_str()));
    CHECK_FALSE(nativeWritePolicyInit(utf8(f.base).c_str(), utf8(f.profile).c_str()));
    f.unchanged();
}

TEST_CASE("native file mappings cannot grow or mutate retained source", "[native-reuse][write-policy]") {
    Fixture f;
    HANDLE file = CreateFileW(f.source.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(file != INVALID_HANDLE_VALUE);
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READWRITE, 0, 0, nullptr);
    REQUIRE(mapping);
    f.activate();
    CHECK(CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 65536, nullptr) == nullptr);
    CHECK(CreateFileMappingW(file, nullptr, PAGE_READWRITE, 0, 65536, nullptr) == nullptr);
    CHECK(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 0) == nullptr);
    CHECK(MapViewOfFileEx(mapping, FILE_MAP_WRITE, 0, 0, 0, nullptr) == nullptr);
    CloseHandle(mapping); CloseHandle(file);
    f.unchanged();
}

TEST_CASE("native sharing and shell escape paths refuse without launching or removing files", "[native-reuse][write-policy]") {
    Fixture f; f.activate();
    CHECK(_fsopen(utf8(f.profile / "share").c_str(), "wb", _SH_DENYRW) == nullptr);
    CHECK(_sopen(utf8(f.profile / "share").c_str(), _O_WRONLY | _O_CREAT, _SH_DENYWR, 0666) < 0);
    CHECK(system("echo must-not-launch") < 0);
    CHECK_FALSE(SetFileAttributesW(f.source.c_str(), FILE_ATTRIBUTE_HIDDEN));
    CHECK(_chmod(utf8(f.source).c_str(), 0400) < 0);
    FILE *stream = nullptr;
    CHECK(freopen_s(&stream, utf8(f.source).c_str(), "wb", stdout) != 0);
    CHECK(stream == nullptr);
    f.unchanged();
}
