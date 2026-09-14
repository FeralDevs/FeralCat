/** Native tests for hostile and boundary-case SD application manifests. */
#include "app_manifest.h"

#include <cstdio>
#include <cstring>
#include <string>

using meow::luaapps::Manifest;
using meow::luaapps::parseManifest;
using meow::luaapps::validateAppId;
using meow::luaapps::CapUi;
using meow::luaapps::CapSystem;
using meow::luaapps::CapAudio;

namespace {

unsigned int checks = 0;
unsigned int failures = 0;

void check(bool condition, const char* description) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
}

const char* const baseManifest =
    "id=player\nname=Player\nversion=0.1.0\napi=1\nentry=main.lua\n"
    "capabilities=ui,system\n";

std::string replaceLine(const char* key, const std::string& value) {
    std::string text(baseManifest);
    const std::string prefix = std::string(key) + "=";
    const std::size_t start = text.find(prefix);
    const std::size_t end = text.find('\n', start);
    text.replace(start, end - start, prefix + value);
    return text;
}

Manifest accepts(const std::string& text, const char* description) {
    Manifest manifest = {};
    char error[96] = "previous error";
    const bool ok = parseManifest(text.data(), text.size(), manifest, error, sizeof(error));
    check(ok, description);
    if (!ok) std::fprintf(stderr, "  parser: %s\n", error);
    check(!ok || error[0] == '\0', "success clears previous error");
    return manifest;
}

void rejects(const std::string& text, const char* description) {
    Manifest manifest;
    std::memset(&manifest, 0x5a, sizeof(manifest));
    unsigned char original[sizeof(Manifest)];
    std::memcpy(original, &manifest, sizeof(manifest));
    char error[96] = {};
    check(!parseManifest(text.data(), text.size(), manifest, error, sizeof(error)), description);
    check(error[0] != '\0', "failure describes the error");
    check(std::memcmp(original, &manifest, sizeof(manifest)) == 0,
          "failure does not publish a partial manifest");
}

void testValidDocuments() {
    Manifest manifest = {};
    char error[96];
    check(parseManifest(baseManifest, std::strlen(baseManifest), manifest, error, sizeof(error)),
          "minimal complete manifest parses");
    check(std::strcmp(manifest.id, "player") == 0 &&
          std::strcmp(manifest.name, "Player") == 0 &&
          std::strcmp(manifest.version, "0.1.0") == 0 &&
          std::strcmp(manifest.entry, "main.lua") == 0 &&
          manifest.api == 1 && manifest.memoryBytes == 256 * 1024,
          "parsed fields and default memory agree with the manifest");
    accepts("# UTF-8 comment: \xe2\x99\xab\r\n\r\n"
            " capabilities = system, ui \r\nentry=main.lua\r\napi=1\r\n"
            "version=1.0 beta\r\n name = Caf\xc3\xa9 \r\nid=a_b-9\r\n",
            "reordered keys, CRLF, comments and surrounding spaces parse");
    std::string noNewline(baseManifest);
    noNewline.pop_back();
    accepts(noNewline, "final line does not require a newline or NUL terminator");
    accepts(replaceLine("name", "Music #1 = personal"), "values may contain literal # and =");
    accepts(replaceLine("name", "\xf0\x9f\x90\xb1"), "valid four-byte UTF-8 name parses");
    for (unsigned int kb : {64u, 128u, 256u}) {
        const std::string text = std::string(baseManifest) + "memory_kb=" + std::to_string(kb);
        check(parseManifest(text.data(), text.size(), manifest, nullptr, 0) &&
              manifest.memoryBytes == kb * 1024, "declared memory budget is converted to bytes");
    }
}

void testRequiredAndUnknownKeys() {
    const char* const keys[] = {"id", "name", "version", "api", "entry", "capabilities"};
    for (const char* key : keys) {
        std::string text(baseManifest);
        const std::size_t start = text.find(std::string(key) + "=");
        text.erase(start, text.find('\n', start) - start + 1);
        rejects(text, "every required key must be present");
        rejects(std::string(baseManifest) + key + "=ignored\n", "duplicate keys are rejected");
    }
    rejects(std::string(baseManifest) + "memory_kb=64\nmemory_kb=128", "duplicate optional key rejected");
    rejects(std::string(baseManifest) + " name = Another", "spaces cannot conceal a duplicate");
    rejects(std::string(baseManifest) + "network=enabled", "unknown capability-like key rejected");
    rejects(std::string(baseManifest) + "Name=Another", "keys are case sensitive");
    rejects(std::string("[app]\n") + baseManifest, "INI sections are not part of the flat schema");
    rejects(std::string(baseManifest) + "broken", "missing equals rejected");
    rejects(std::string(baseManifest) + "=value", "empty key rejected");
    rejects("# only a comment\n", "comments do not satisfy required fields");
    for (const char* key : keys) rejects(replaceLine(key, ""), "required values cannot be empty");
    for (const char* value : {"0", "2", "01", "+1", "1 # current"}) {
        rejects(replaceLine("api", value), "only exact API version 1 accepted");
    }
    const std::size_t length = std::strlen(baseManifest);
    for (std::size_t i = 0; i + 1 < length; ++i) {
        rejects(std::string(baseManifest, i), "truncated mandatory tail fails closed");
    }
}

void testPathsAndCapabilities() {
    for (const char* id : {"../player", "a/b", "a\\b", "/a", "a..b", "A", "1app", "-app", "a%2fb", "a:b"}) {
        check(!validateAppId(id, std::strlen(id)), "unsafe folder id rejected");
        rejects(replaceLine("id", id), "unsafe manifest id rejected");
    }
    check(!validateAppId(nullptr, 1) && !validateAppId("", 0), "null and empty ids rejected");
    const char idWithNul[] = {'a', '\0', 'b'};
    check(!validateAppId(idWithNul, sizeof(idWithNul)), "ID validation uses explicit length");
    const char exactId[] = {'a', '-', '9'};
    check(validateAppId(exactId, sizeof(exactId)), "non-terminated bounded ID accepted");
    for (const char* entry : {"../main.lua", "/main.lua", "sub/main.lua", "sub\\main.lua", "Main.lua", "main.lua.bak"}) {
        rejects(replaceLine("entry", entry), "entry cannot navigate or select another script");
    }
    for (const char* caps : {"ui", "system", "ui,network", "ui,system,network", "ui,ui,system", "ui,system,system", "ui,,system", ",ui,system", "ui,system,", "UI,system", "ui system"}) {
        rejects(replaceLine("capabilities", caps), "missing unknown duplicate or malformed capabilities rejected");
    }
    accepts(replaceLine("capabilities", "system, ui"), "supported capabilities may be reordered");
    auto audio = accepts(replaceLine("capabilities", "system,audio,ui") + "icon=music\n",
                         "audio app and built-in music icon accepted");
    check(audio.capabilities == (CapUi | CapSystem | CapAudio) && audio.musicIcon,
          "capabilities and icon carried into native host policy");
    rejects(replaceLine("capabilities", "ui,system,audio,audio"), "duplicate audio rejected");
    rejects(std::string(baseManifest) + "icon=/mp3/cover.png\n", "icon cannot read arbitrary paths");
}

void testLimitsAndEncoding() {
    accepts(replaceLine("id", std::string(31, 'a')), "31-byte ID accepted");
    rejects(replaceLine("id", std::string(32, 'a')), "32-byte ID rejected");
    accepts(replaceLine("name", std::string(63, 'n')), "63-byte name accepted");
    rejects(replaceLine("name", std::string(64, 'n')), "64-byte name rejected");
    std::string unicodeName;
    for (unsigned int i = 0; i < 21; ++i) unicodeName += "\xe7\x8c\xab";
    accepts(replaceLine("name", unicodeName), "UTF-8 name limit is bytes, not codepoints");
    rejects(replaceLine("name", unicodeName + "x"), "UTF-8 name exceeding byte limit rejected");
    accepts(replaceLine("version", std::string(31, 'v')), "31-byte version accepted");
    rejects(replaceLine("version", std::string(32, 'v')), "32-byte version rejected");
    rejects(replaceLine("version", "v\xc3\xa4"), "version is printable ASCII");
    for (const char* memory : {"", "0", "63", "257", "-64", "+64", "64.0", "64k", "4294967296", "999999999999999999999"}) {
        rejects(std::string(baseManifest) + "memory_kb=" + memory, "invalid and overflowing budgets rejected");
    }
    std::string maximum(baseManifest);
    maximum += '#';
    maximum.append(meow::luaapps::kManifestMaxBytes - maximum.size(), 'x');
    accepts(maximum, "exactly 2048 bytes accepted");
    rejects(maximum + "x", "2049 bytes rejected");

    for (unsigned int control = 0; control <= 0x1f; ++control) {
        if (control == '\n') continue;
        rejects(replaceLine("name", std::string("a") + static_cast<char>(control) + "b"),
                "embedded NUL and ASCII controls rejected");
    }
    rejects(replaceLine("name", std::string("a\x7f", 2)), "DEL rejected");
    rejects(std::string(baseManifest) + "# bad\tcomment", "control checks also cover comments");
    rejects(std::string(baseManifest) + "# broken\r", "bare CR line ending rejected");
    const char* const invalidUtf8[] = {
        "\x80", "\xc0\xaf", "\xc1\xbf", "\xc2", "\xc2\x41",
        "\xe0\x80\xaf", "\xed\xa0\x80", "\xf0\x80\x80\xaf",
        "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xff", "\xc2\x85"
    };
    for (const char* invalid : invalidUtf8) {
        rejects(replaceLine("name", invalid), "malformed UTF-8 or Unicode control rejected");
        rejects(std::string(baseManifest) + "# " + invalid, "malformed comment UTF-8 rejected");
    }
    rejects(std::string("\xef\xbb\xbf") + baseManifest, "UTF-8 BOM is not a manifest key prefix");
}

void testErrorBoundaries() {
    Manifest manifest = {};
    char guarded[] = {'L', 'x', 'x', 'x', 'R'};
    check(!parseManifest(nullptr, 1, manifest, guarded + 1, 3), "null input fails");
    check(guarded[0] == 'L' && guarded[4] == 'R' && guarded[3] == '\0',
          "bounded error leaves guard bytes intact and terminates");
    char single = 'x';
    check(!parseManifest(nullptr, 0, manifest, &single, 1) && single == '\0',
          "one-byte error buffer is terminated");
    single = 'x';
    check(!parseManifest(nullptr, 0, manifest, &single, 0) && single == 'x',
          "zero-size error buffer is untouched");
    check(!parseManifest(nullptr, 0, manifest, nullptr, 5), "null error destination accepted");
}

}  // namespace

int main() {
    testValidDocuments();
    testRequiredAndUnknownKeys();
    testPathsAndCapabilities();
    testLimitsAndEncoding();
    testErrorBoundaries();
    std::printf("manifest_test: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
