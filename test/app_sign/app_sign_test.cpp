#include "app_sign.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

static unsigned checks = 0, failures = 0;
static void check(bool ok, const char* label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
}
static std::vector<uint8_t> read(const char* path) {
    std::ifstream stream(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), {});
}
static void verify_pair(const char* message_path, const char* signature_path) {
    auto message = read(message_path), signature = read(signature_path);
    check(!message.empty() && signature.size() == 64, "fixture readable");
    if (message.empty() || signature.size() != 64) return;
    check(meow_app_verify(message.data(), message.size(), signature.data(), signature.size()), "trusted signature");
    message[message.size()/2] ^= 0x80;
    check(!meow_app_verify(message.data(), message.size(), signature.data(), signature.size()), "changed image rejected");
    message[message.size()/2] ^= 0x80;
    signature[17] ^= 0x20;
    check(!meow_app_verify(message.data(), message.size(), signature.data(), signature.size()), "changed signature rejected");
    signature[17] ^= 0x20;
    const uint8_t tiny = 0;
    check(!meow_app_verify(message.data(), message.size(), &tiny, 0), "empty signature rejected before read");
    check(!meow_app_verify(message.data(), message.size(), &tiny, 63), "short signature rejected before read");
    check(!meow_app_verify(message.data(), message.size(), &tiny, 65), "long signature rejected before read");
    check(!meow_app_verify(nullptr, message.size(), signature.data(), 64), "null image rejected");
    check(!meow_app_verify(message.data(), 0, signature.data(), 64), "empty image rejected");
    check(!meow_app_verify(message.data(), message.size(), nullptr, 64), "null signature rejected");
    check(!meow_app_verify(message.data(), message.size()-1, signature.data(), 64), "truncated image rejected");
    message.push_back(0);
    check(!meow_app_verify(message.data(), message.size(), signature.data(), 64), "appended image rejected");
    // This deliberately public test-only key must never be trusted by firmware.
    uint8_t seed[32] = {7}, secret[64], pub[32], unknown_sig[64];
    crypto_ed25519_key_pair(secret, pub, seed);
    crypto_ed25519_sign(unknown_sig, secret, message.data(), message.size());
    check(!meow_app_verify(message.data(), message.size(), unknown_sig, 64), "untrusted valid signature rejected");
    crypto_wipe(secret, sizeof secret);
}
int main(int argc, char** argv) {
    if (argc != 5) return 2;
    verify_pair(argv[1], argv[2]);
    verify_pair(argv[3], argv[4]);
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
