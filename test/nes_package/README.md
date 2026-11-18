# Native NES package verification

Run from the repository root after building `meowsign`:

```powershell
python test/nes_package/nes_package_test.py -v
```

Use `--signer <executable>` to choose another host build. Tests use synthetic
firmware/ELF-shaped bytes and temporary test-only keys. No ROM is loaded,
executed or copied; the private development seed is not needed.

Coverage includes the real Ed25519 verifier, changed app/signature, foreign
key, signature length, firmware version/partition limits, native ELF limits,
missing verifier, invalid manifest, existing-output preservation, fixed ZIP
membership, complete checksums and byte-identical repeat packaging. A source
replacement during signature verification cannot change the verified snapshot
that is packaged.

The package generator also calls its `verify_package(archive)` helper after
creating each ZIP. This checks membership and all declared payload hashes;
signature verification is separately mandatory before package creation. The
helper alone is an integrity check, not independent authentication of a ZIP.

These checks do not execute the native ELF or validate device hardware.
