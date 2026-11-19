/* Offline Ed25519 signing with the same Monocypher as firmware.
 * Seeds stay outside source/packages. keygen never overwrites a seed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <aclapi.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
#include "monocypher.h"
#include "monocypher-ed25519.h"
#define MAX_IMAGE_BYTES (512u * 1024u)

static int read_file(const char *path, size_t limit, uint8_t **out, size_t *size)
{
    FILE *f;
    long length;
    uint8_t *buf;
    *out = NULL; *size = 0;
    f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "meowsign: cannot read %s\n", path); return -1; }
    if (fseek(f, 0, SEEK_END) || (length = ftell(f)) < 0 ||
        (unsigned long)length > limit || fseek(f, 0, SEEK_SET)) {
        fclose(f); fprintf(stderr, "meowsign: invalid file size\n"); return -1;
    }
    buf = (uint8_t *)malloc(length ? (size_t)length : 1);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)length, f) != (size_t)length) {
        crypto_wipe(buf, (size_t)length); free(buf); fclose(f); return -1;
    }
    fclose(f); *out = buf; *size = (size_t)length; return 0;
}

static int write_file(const char *path, const uint8_t *buf, size_t size)
{
#ifdef _WIN32
    HANDLE f;
    DWORD written = 0;
    int ok;
    if (size > MAXDWORD) return -1;
    f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return -1;
    ok = WriteFile(f, buf, (DWORD)size, &written, NULL) && written == size && FlushFileBuffers(f);
    if (!CloseHandle(f)) ok = 0;
    if (!ok) DeleteFileA(path);
#else
    int f = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    int ok;
    if (f < 0) return -1;
    ok = write(f, buf, size) == (ssize_t)size && fsync(f) == 0;
    if (close(f)) ok = 0;
    if (!ok) unlink(path);
#endif
    return ok ? 0 : -1;
}

/* Atomic create-only seed write; protected current-user/SYSTEM ACL on Windows,
 * owner-only mode on POSIX. A failed write removes only the just-created file. */
static int write_new_seed(const char *path, const uint8_t seed[32])
{
#ifdef _WIN32
    HANDLE token = NULL, file = INVALID_HANDLE_VALUE;
    TOKEN_USER *user = NULL;
    DWORD needed = 0, written = 0, system_size = SECURITY_MAX_SID_SIZE;
    BYTE system_sid[SECURITY_MAX_SID_SIZE];
    EXPLICIT_ACCESSA entries[2];
    PACL acl = NULL;
    SECURITY_DESCRIPTOR descriptor;
    SECURITY_ATTRIBUTES attributes;
    int result = -1;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) goto done;
    GetTokenInformation(token, TokenUser, NULL, 0, &needed);
    if (!needed || !(user = (TOKEN_USER *)malloc(needed))) goto done;
    if (!GetTokenInformation(token, TokenUser, user, needed, &needed) ||
        !CreateWellKnownSid(WinLocalSystemSid, NULL, system_sid, &system_size)) goto done;
    memset(entries, 0, sizeof entries);
    entries[0].grfAccessPermissions = GENERIC_ALL;
    entries[0].grfAccessMode = SET_ACCESS;
    entries[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entries[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
    entries[0].Trustee.ptstrName = (LPSTR)user->User.Sid;
    entries[1] = entries[0];
    entries[1].Trustee.ptstrName = (LPSTR)system_sid;
    if (SetEntriesInAclA(2, entries, NULL, &acl) != ERROR_SUCCESS ||
        !InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) ||
        !SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE) ||
        !SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED)) goto done;
    attributes.nLength = sizeof attributes;
    attributes.lpSecurityDescriptor = &descriptor;
    attributes.bInheritHandle = FALSE;
    file = CreateFileA(path, GENERIC_WRITE, 0, &attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) goto done;
    if (WriteFile(file, seed, 32, &written, NULL) && written == 32 && FlushFileBuffers(file)) result = 0;
    CloseHandle(file); file = INVALID_HANDLE_VALUE;
    if (result) DeleteFileA(path);
done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (acl) LocalFree(acl);
    free(user);
    if (token) CloseHandle(token);
    return result;
#else
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    int ok;
    if (fd < 0) return -1;
    ok = write(fd, seed, 32) == 32 && fsync(fd) == 0;
    if (close(fd)) ok = 0;
    if (!ok) unlink(path);
    return ok ? 0 : -1;
#endif
}

static int os_random(uint8_t seed[32])
{
#ifdef _WIN32
    return BCryptGenRandom(NULL, seed, 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 ? 0 : -1;
#else
    FILE *f = fopen("/dev/urandom", "rb");
    int ok;
    if (!f) return -1;
    ok = fread(seed, 1, 32, f) == 32;
    fclose(f); return ok ? 0 : -1;
#endif
}

static int output_public(const uint8_t pub[32], const char *path)
{
    static const char digits[] = "0123456789abcdef";
    char hex[66];
    size_t i;
    for (i = 0; i < 32; ++i) { hex[2*i] = digits[pub[i] >> 4]; hex[2*i+1] = digits[pub[i] & 15]; }
    hex[64] = '\n'; hex[65] = '\0';
    if (path && write_file(path, (const uint8_t *)hex, 65)) return -1;
    fputs(hex, stdout); /* Public data only. */
    return 0;
}

static int hex_value(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int read_public(const char *argument, uint8_t pub[32])
{
    uint8_t *file = NULL;
    const uint8_t *hex = (const uint8_t *)argument;
    size_t length = strlen(argument), i;
    int is_hex = length == 64;
    for (i = 0; is_hex && i < length; ++i) if (hex_value(hex[i]) < 0) is_hex = 0;
    if (!is_hex) {
        if (read_file(argument, 66, &file, &length)) return -1;
        hex = file;
        while (length && (hex[length-1] == '\r' || hex[length-1] == '\n')) --length;
    }
    if (length != 64) { free(file); return -1; }
    for (i = 0; i < 32; ++i) {
        int high = hex_value(hex[i*2]), low = hex_value(hex[i*2+1]);
        if (high < 0 || low < 0) { free(file); return -1; }
        pub[i] = (uint8_t)((high << 4) | low);
    }
    free(file); return 0;
}

int main(int argc, char **argv)
{
    uint8_t *seed_file = NULL, *message = NULL, *signature = NULL;
    uint8_t seed[32] = {0}, secret[64] = {0}, pub[32], sig[64];
    size_t seed_size = 0, message_size = 0, signature_size = 0;
    int result = 1;
    if (argc >= 2 && !strcmp(argv[1], "keygen") && (argc == 3 || argc == 4)) {
        if (os_random(seed)) { fprintf(stderr, "meowsign: OS random generator failed\n"); goto done; }
        if (write_new_seed(argv[2], seed)) {
            fprintf(stderr, "meowsign: seed not created (path unavailable or already exists)\n"); goto done;
        }
        crypto_ed25519_key_pair(secret, pub, seed);
        result = output_public(pub, argc == 4 ? argv[3] : NULL) != 0;
        if (result) fprintf(stderr, "meowsign: seed exists, but public output failed; recover with pub\n");
        goto done;
    }
    if (argc >= 2 && ((!strcmp(argv[1], "pub") && (argc == 3 || argc == 4)) ||
                     (!strcmp(argv[1], "sign") && argc == 5))) {
        if (read_file(argv[2], 32, &seed_file, &seed_size) || seed_size != 32) {
            fprintf(stderr, "meowsign: seed must be exactly 32 bytes\n"); goto done;
        }
        crypto_ed25519_key_pair(secret, pub, seed_file);
        if (!strcmp(argv[1], "pub")) {
            result = output_public(pub, argc == 4 ? argv[3] : NULL) != 0;
            goto done;
        }
        if (read_file(argv[3], MAX_IMAGE_BYTES, &message, &message_size) || !message_size) goto done;
        crypto_ed25519_sign(sig, secret, message, message_size);
        result = write_file(argv[4], sig, sizeof sig) != 0;
        if (!result) puts("Signed");
        goto done;
    }
    if (argc == 5 && !strcmp(argv[1], "verify")) {
        if (read_public(argv[2], pub) ||
            read_file(argv[3], MAX_IMAGE_BYTES, &message, &message_size) || !message_size ||
            read_file(argv[4], 64, &signature, &signature_size) || signature_size != 64) goto done;
        result = crypto_ed25519_check(signature, pub, message, message_size) != 0;
        puts(result ? "BAD" : "OK");
        goto done;
    }
    fprintf(stderr, "usage: meowsign keygen <seed> [pub-file] | pub <seed> [pub-file] | sign <seed> <input> <sig> | verify <pub-hex-or-file> <input> <sig>\n");
    result = 2;
done:
    crypto_wipe(seed, sizeof seed); crypto_wipe(secret, sizeof secret);
    if (seed_file) { crypto_wipe(seed_file, seed_size); free(seed_file); }
    free(message); free(signature);
    if (result == 1) fprintf(stderr, "meowsign: operation failed\n");
    return result;
}
