/*
 * meowsign — offline Ed25519 signing tool for MeowKit native ELF apps.
 *
 * Uses the SAME Monocypher source that the firmware verifies with
 * (lib/monocypher), so a signature this tool produces is byte-for-byte
 * verifiable on-device. RFC 8032 Ed25519 (curve25519 + SHA-512).
 *
 * Build:   tools/app_signing/build.sh   (produces ./meowsign)
 *
 * Commands:
 *   meowsign keygen  <seed.bin>              generate a private seed (32 B),
 *                                            print the public key as a C array
 *   meowsign pub     <seed.bin>              print public key (C array + hex)
 *   meowsign sign    <seed.bin> <in> <out>   write a 64-byte detached signature
 *   meowsign verify  <pub.hex>  <in> <sig>   verify (exit 0 = OK)
 *
 * The seed is the secret. Keep it OUT of the repo (e.g. ~/Projects/MeowKit/keys).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "monocypher.h"
#include "monocypher-ed25519.h"

static long read_file(const char *path, uint8_t **out)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "meowsign: cannot open %s\n", path); return -1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return -1; }
    uint8_t *buf = malloc(n ? n : 1);
    if (fread(buf, 1, n, f) != (size_t)n) { fclose(f); free(buf); return -1; }
    fclose(f); *out = buf; return n;
}

static int write_file(const char *path, const uint8_t *buf, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "meowsign: cannot write %s\n", path); return -1; }
    int ok = fwrite(buf, 1, n, f) == n;
    fclose(f); return ok ? 0 : -1;
}

static void print_pub(const uint8_t pub[32])
{
    printf("// MeowKit app-signing public key (Ed25519). Paste into app_sign.cpp:\n");
    printf("static const uint8_t APP_SIGN_PUBKEY[32] = {\n    ");
    for (int i = 0; i < 32; i++) {
        printf("0x%02x%s", pub[i], i == 31 ? "" : ", ");
        if (i % 8 == 7 && i != 31) printf("\n    ");
    }
    printf("\n};\nhex: ");
    for (int i = 0; i < 32; i++) printf("%02x", pub[i]);
    printf("\n");
}

/* Fill buf with cryptographically-strong random bytes from the OS. */
static int os_random(uint8_t *buf, size_t n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return -1;
    int ok = fread(buf, 1, n, f) == n;
    fclose(f); return ok ? 0 : -1;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: meowsign keygen|pub|sign|verify ...\n"); return 2; }

    if (!strcmp(argv[1], "keygen") && argc == 3) {
        uint8_t seed[32], seed_copy[32], sk[64], pub[32];
        if (os_random(seed, 32)) { fprintf(stderr, "meowsign: no entropy\n"); return 1; }
        memcpy(seed_copy, seed, 32);              /* key_pair() wipes its seed arg */
        crypto_ed25519_key_pair(sk, pub, seed);
        int wr = write_file(argv[2], seed_copy, 32);
        crypto_wipe(sk, sizeof sk);
        crypto_wipe(seed_copy, sizeof seed_copy);
        if (wr) return 1;
        print_pub(pub);
        fprintf(stderr, "meowsign: seed written to %s (KEEP SECRET, do not commit)\n", argv[2]);
        return 0;
    }

    if (!strcmp(argv[1], "pub") && argc == 3) {
        uint8_t *seed; long n = read_file(argv[2], &seed);
        if (n != 32) { fprintf(stderr, "meowsign: seed must be 32 bytes\n"); return 1; }
        uint8_t sk[64], pub[32];
        crypto_ed25519_key_pair(sk, pub, seed);
        crypto_wipe(sk, sizeof sk); free(seed);
        print_pub(pub);
        return 0;
    }

    if (!strcmp(argv[1], "sign") && argc == 5) {
        uint8_t *seed; long sn = read_file(argv[2], &seed);
        if (sn != 32) { fprintf(stderr, "meowsign: seed must be 32 bytes\n"); return 1; }
        uint8_t *msg; long mn = read_file(argv[3], &msg);
        if (mn < 0) { free(seed); return 1; }
        uint8_t sk[64], pub[32], sig[64];
        crypto_ed25519_key_pair(sk, pub, seed);
        crypto_ed25519_sign(sig, sk, msg, mn);
        crypto_wipe(sk, sizeof sk); free(seed); free(msg);
        if (write_file(argv[4], sig, 64)) return 1;
        fprintf(stderr, "meowsign: signed %s (%ld bytes) -> %s\n", argv[3], mn, argv[4]);
        return 0;
    }

    if (!strcmp(argv[1], "verify") && argc == 5) {
        /* argv[2] = 64-hex-char public key */
        if (strlen(argv[2]) != 64) { fprintf(stderr, "meowsign: pub hex must be 64 chars\n"); return 1; }
        uint8_t pub[32];
        for (int i = 0; i < 32; i++) sscanf(argv[2] + 2*i, "%2hhx", &pub[i]);
        uint8_t *msg; long mn = read_file(argv[3], &msg);
        uint8_t *sig; long sgn = read_file(argv[4], &sig);
        if (mn < 0 || sgn != 64) { free(msg); free(sig); return 1; }
        int rc = crypto_ed25519_check(sig, pub, msg, mn);
        free(msg); free(sig);
        printf("%s\n", rc == 0 ? "OK" : "BAD");
        return rc == 0 ? 0 : 1;
    }

    fprintf(stderr, "meowsign: bad args\n");
    return 2;
}
