/* Minimal native ELF app for the MeowKit elf_loader spike.
 * Symbols like printf are resolved at load time against the firmware's table.
 * Build (see build.sh): xtensa-esp32s3-elf-gcc -mlongcalls -nostartfiles
 *   -nostdlib -fPIC -shared -e app_main ... -o app.elf app_main.c
 */
int printf(const char *fmt, ...);

int app_main(int argc, char **argv)
{
    printf("Hello from a native ELF app on MeowKit!\n");
    int sum = 0;
    for (int i = 1; i <= 10; i++) sum += i;
    printf("  argc=%d, sum(1..10)=%d\n", argc, sum);
    return 42;
}
