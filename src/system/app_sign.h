/**
 * @file app_sign.h
 * @brief Ed25519 signature verification for native ELF apps.
 *
 * Apps are signed offline with tools/app_signing/meowsign (same Monocypher
 * source, so signatures are byte-compatible). The signing seed is kept off the
 * device; only trusted public keys are baked into the firmware.
 *
 * A signed app carries a detached 64-byte signature file "<app>.sig" next to
 * it. The loader (elf_runner) verifies it before running; unsigned or
 * bad-signature apps run only when the user has enabled "Allow unsigned apps"
 * in Settings ▸ Features.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Verify a detached Ed25519 signature over the complete nonempty ELF image.
 * Trusts the unchanged upstream key and this fork's NES development key.
 * Rejects any signature length other than 64 before reading signature bytes. */
bool meow_app_verify(const uint8_t* data, size_t len,
                     const uint8_t* signature, size_t signature_len);

#ifdef __cplusplus
}
#endif
