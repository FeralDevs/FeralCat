# MeowKit app signing (Ed25519)

Native ELF apps (`/apps/<name>/app.elf`) are signed offline and verified
on-device before they run. Verification uses the vendored Monocypher
(`lib/monocypher`) — the firmware ships only the **public** key; the private
seed never touches the device.

An app runs if **either**:
- it has a valid detached signature `app.elf.sig` next to it, **or**
- the user has enabled **Settings ▸ Features ▸ Allow unsigned apps** (off by
  default, like Lua).

## The keys

- Private seed (32 bytes): `~/Projects/MeowKit/keys/app_signing_seed.bin` —
  **secret, outside the repo, never commit.** Back it up somewhere safe; losing
  it means you must rotate to a new key (and reflash firmware).
- Public key (baked into firmware): `src/system/app_sign.cpp`
  `APP_SIGN_PUBKEY[32]`. Reference copy: `app_signing.pub` (hex).

## Build the tool

```bash
tools/app_signing/build.sh      # -> tools/app_signing/meowsign
```

## Usage

```bash
# one-time: generate a keypair (prints the C array for app_sign.cpp)
meowsign keygen ~/Projects/MeowKit/keys/app_signing_seed.bin

# print the public key again (C array + hex)
meowsign pub    ~/Projects/MeowKit/keys/app_signing_seed.bin

# sign an app (produces app.elf.sig)
meowsign sign   ~/Projects/MeowKit/keys/app_signing_seed.bin \
                "sd files/apps/hello/app.elf" \
                "sd files/apps/hello/app.elf.sig"

# verify locally (exit 0 = OK) — same code path as the device
meowsign verify <pubkey-hex> app.elf app.elf.sig
```

## Rotating the key

`meowsign keygen` a new seed, paste the printed `APP_SIGN_PUBKEY` into
`src/system/app_sign.cpp`, rebuild + reflash firmware, and re-sign every app.
Old signatures stop verifying (that's the point).
