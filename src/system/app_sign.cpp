/**
 * @file app_sign.cpp
 * @brief See app_sign.h. Ed25519 verify via vendored Monocypher.
 */
#include "app_sign.h"
#include "monocypher-ed25519.h"

/* MeowKit app-signing public key (Ed25519).
 * Private seed lives off-device (see tools/app_signing/README). To rotate,
 * run `meowsign keygen` and paste the printed array here. */
static const uint8_t APP_SIGN_PUBKEY[32] = {
    0x0b, 0x13, 0x11, 0x66, 0x74, 0x71, 0x02, 0x52,
    0x68, 0xda, 0x14, 0x77, 0x0a, 0x39, 0xd1, 0xb9,
    0x3c, 0xc9, 0x30, 0xbe, 0x50, 0xb0, 0x33, 0x3d,
    0xae, 0xc5, 0x0a, 0x05, 0x01, 0x1f, 0xe9, 0x7e
};

bool meow_app_verify(const uint8_t* data, size_t len, const uint8_t* sig64)
{
    if (!data || !sig64) return false;
    /* crypto_ed25519_check returns 0 on a valid signature, -1 otherwise. */
    return crypto_ed25519_check(sig64, APP_SIGN_PUBKEY, data, len) == 0;
}
