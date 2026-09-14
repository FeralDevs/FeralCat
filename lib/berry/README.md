# berry (vendored)

The [Berry](https://github.com/berry-lang/berry) scripting VM, embedded so the
firmware can run `.be` scripts from the SD card (see the Script Runner app and
`docs/SCRIPTING.md`).

- `src/` — Berry core (`be_*.c/.h`) + `be_port.c`, `be_modtab.c`, `berry_conf.h`.
  Trimmed config: string + math only (json/os/sys/time modules off); the debug
  **hook** is on (`BE_USE_DEBUG_HOOK 1`) so a runaway script can be interrupted.
  `be_writebuffer` was removed from `be_port.c` — the firmware provides it
  (in `src/system/berry_engine.cpp`) to route `print()` to the on-screen console.
- `generate/` — const-object headers, pre-generated once with Berry's `coc` tool
  so there's no build-time codegen:
  `python3 tools/coc/coc -o generate src default -c default/berry_conf.h`
  (re-run only if the module config in `berry_conf.h` changes).
