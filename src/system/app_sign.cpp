/**
 * @file app_sign.cpp
 * @brief See app_sign.h. Ed25519 verify via vendored Monocypher.
 */
#include "app_sign.h"
#include "monocypher-ed25519.h"

/* Unchanged upstream MeowKit Ed25519 key: existing official SD apps remain
 * trusted. Never replace it just to install this fork's development app. */
static const uint8_t APP_SIGN_PUBKEY[32] = {
    0x0b, 0x13, 0x11, 0x66, 0x74, 0x71, 0x02, 0x52,
    0x68, 0xda, 0x14, 0x77, 0x0a, 0x39, 0xd1, 0xb9,
    0x3c, 0xc9, 0x30, 0xbe, 0x50, 0xb0, 0x33, 0x3d,
    0xae, 0xc5, 0x0a, 0x05, 0x01, 0x1f, 0xe9, 0x7e
};

/* Local NES-development key, generated with the Windows OS CSPRNG.
 * Public reference: tools/app_signing/nes-development.pub.
 * The private seed is outside the repository and never installed on-device. */
static const uint8_t NES_DEVELOPMENT_PUBKEY[32] = {
    0x60, 0x0c, 0xad, 0xab, 0x83, 0xd6, 0x34, 0x60,
    0x2e, 0xf8, 0xba, 0x3f, 0x1a, 0x91, 0xe8, 0x00,
    0xf7, 0xe0, 0xef, 0x67, 0xa6, 0x1b, 0x0d, 0x7a,
    0x31, 0x97, 0x55, 0x07, 0x4c, 0x6b, 0x6a, 0x51
};

bool meow_app_verify(const uint8_t* data, size_t len,
                     const uint8_t* signature, size_t signature_len)
{
    if (!data || !len || !signature || signature_len != 64) return false;
    /* crypto_ed25519_check returns 0 on a valid signature, -1 otherwise. */
    return crypto_ed25519_check(signature, APP_SIGN_PUBKEY, data, len) == 0 ||
           crypto_ed25519_check(signature, NES_DEVELOPMENT_PUBKEY, data, len) == 0;
}
