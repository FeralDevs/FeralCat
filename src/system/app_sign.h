/**
 * @file app_sign.h
 * @brief Ed25519 signature verification for native ELF apps.
 *
 * Apps are signed offline with tools/app_signing/meowsign (same Monocypher
 * source, so signatures are byte-compatible). The signing seed is kept off the
 * device; only the 32-byte public key is baked into the firmware.
 *
 * A signed app carries a detached 64-byte signature file "<app>.sig" next to
 * it. The loader (elf_runner) verifies it before running; unsigned or
 * bad-signature apps run only when the user has enabled "Allow unsigned apps"
 * in Settings ▸ Features.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Verify a detached Ed25519 signature (64 bytes) over `data` (`len` bytes)
 * against the firmware's built-in public key. Returns true iff valid. */
bool meow_app_verify(const uint8_t* data, size_t len, const uint8_t* sig64);

#ifdef __cplusplus
}
#endif
