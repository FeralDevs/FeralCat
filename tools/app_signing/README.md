# Native app signing

Native apps carry a detached **64-byte RFC 8032 Ed25519 signature** in
`app.elf.sig`. It authenticates the complete final ELF file. The manifest and
other files are not included in that signature. Firmware verifies before
relocation using the same vendored Monocypher implementation as this tool.

This NES fork accepts two public keys in `src/system/app_sign.cpp`:

- The unchanged upstream key, reference `app_signing.pub`, preserves all
  existing official app signatures.
- The local NES development key, reference `nes-development.pub`, permits
  signed local apps. Its trust is not restricted by directory or app name;
  protect the private seed as a firmware trust credential.

The existing unsigned-app setting remains unchanged and **is not needed** for
the signed NES package. The private seed is never included in firmware, source,
SD packages or logs. Public keys and signatures are safe to distribute.

## Build on Windows

Use CMake and the installed Visual Studio C build tools:

```powershell
cmake -S tools/app_signing -B .build/app-signing -G "Visual Studio 18 2026" -A x64
cmake --build .build/app-signing --config Release
$signer = '.\.build\app-signing\Release\meowsign.exe'
```

On POSIX, `tools/app_signing/build.sh` is also available. No external crypto
package is required. Windows links the system `bcrypt` and `advapi32` libraries.

## Keys and signing

The local seed was generated once outside this checkout at
`C:\Projekte\Meowkit\.work\nes-signing-20260922\nes.seed.bin`.
Do not regenerate it during normal builds. Keep a secure external backup.
The public key can always be derived again with `pub`; that command never
prints the seed.

```powershell
# Generate a NEW key only when intentionally introducing/rotating trust.
# Its parent directory must already exist. Never place a seed in the checkout.
& $signer keygen C:\outside-the-repo\new.seed.bin C:\outside-the-repo\new.pub

# Recover only the public key (stdout contains public hex).
& $signer pub C:\Projekte\Meowkit\.work\nes-signing-20260922\nes.seed.bin

# Sign the FINAL ELF, after compiler, linker and strip have finished.
& $signer sign C:\Projekte\Meowkit\.work\nes-signing-20260922\nes.seed.bin `
    'sd files/apps/nes/app.elf' 'sd files/apps/nes/app.elf.sig'
& $signer verify tools/app_signing/nes-development.pub `
    'sd files/apps/nes/app.elf' 'sd files/apps/nes/app.elf.sig'
```

`verify` accepts a public-key filename or exactly 64 hexadecimal characters;
success returns exit code 0. Input images must contain 1..524288 bytes, matching
the firmware loader limit. Signature lengths other than 64 are rejected.

**Every output is create-only.** Existing seeds, public files and signature
files are never overwritten, even through a file alias. For an updated app,
sign into a fresh staging directory and verify before replacing the installed
package. Never modify the ELF after signing it. Generating a different seed
does not make it trusted: firmware must explicitly contain its public key.

Windows key generation uses `BCryptGenRandom` and an exclusive `CREATE_NEW`
write with a protected ACL granting access only to the current user and
SYSTEM. POSIX uses `/dev/urandom`, `O_EXCL` and mode `0600`. Sensitive buffers
are wiped before release. The tool reports public data and status only.

## Host verification

```powershell
cmake -S test/app_sign -B .build/app-sign-tests -G "Visual Studio 18 2026" -A x64
cmake --build .build/app-sign-tests --config Release
ctest --test-dir .build/app-sign-tests -C Release --output-on-failure
```

The tests use the actual firmware `meow_app_verify` function with an existing
upstream MeowPlayer ELF and a synthetic message signed by the new key. They
reject modified/truncated/appended images, modified or incorrectly sized
signatures, and correctly signed messages from an untrusted test key. CLI
tests also exercise overwrite protection, missing files and input bounds.
No private development seed is needed to repeat these tests.

To verify a final NES artifact with the firmware function as well as the CLI:

```powershell
& '.\.build\app-sign-tests\Release\app_sign_test.exe' `
    'sd files/apps/meowplayer/app.elf' 'sd files/apps/meowplayer/app.elf.sig' `
    'sd files/apps/nes/app.elf' 'sd files/apps/nes/app.elf.sig'
```

Package only the verified app, detached signature, manifest and public
provenance/hash files. Signing and host verification do not replace a device
test of ELF loading, execution and return to the launcher.
