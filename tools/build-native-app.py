"""Build a MeowKit native SD app with the firmware's existing Xtensa ELF recipe.

Requires the ESP32-S3 GCC package from PlatformIO; does not flash or sign apps.
Output defaults to .build/native-apps/<app-directory>/app.elf, separate from
signed SD packages so rebuilding cannot silently leave a stale signature.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
PREFIX = "xtensa-esp32s3-elf-"


def toolchain(value):
    suffix = ".exe" if os.name == "nt" else ""
    candidates = []
    if value:
        path = Path(value).expanduser().resolve()
        candidates.extend([path.parent if path.is_file() else path, path / "bin"])
    else:
        core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
        candidates.append(core / "packages/toolchain-xtensa-esp32s3/bin")
        found = shutil.which(PREFIX + "gcc")
        if found:
            candidates.append(Path(found).parent)
    for directory in candidates:
        tools = {name: directory / (PREFIX + name + suffix) for name in ["gcc", "strip"]}
        if all(path.is_file() for path in tools.values()):
            return tools
    raise SystemExit("ESP32-S3 GCC/strip not found; pass --toolchain <package/bin>.")


def inspect_elf(data):
    """Reject incompatible compiler output before it reaches the SD loader."""
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("Expected 32-bit little-endian ELF")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    if header[1] != 3 or header[2] != 94:
        raise ValueError("Expected Xtensa shared ELF")
    offset, entry_size, count, names_index = header[6], header[11], header[12], header[13]
    if entry_size != 40 or not count or names_index >= count or offset + count * 40 > len(data):
        raise ValueError("Invalid section table")
    sections = [struct.unpack_from("<10I", data, offset + i * 40) for i in range(count)]

    def payload(section):
        start, size = section[4], section[5]
        if start + size > len(data):
            raise ValueError("Section exceeds file")
        return data[start:start + size]

    def string(table, index):
        end = table.find(b"\0", index)
        if index >= len(table) or end < 0:
            raise ValueError("Invalid ELF string")
        return table[index:end].decode("ascii")

    names = payload(sections[names_index])
    imports = set()
    section_names = []
    for section in sections:
        name = string(names, section[0])
        section_names.append(name)
        # The firmware loader identifies a plain .rodata section by name.
        if name.startswith(".rodata") and (name != ".rodata" or section[2] & 0x30 or section[9]):
            raise ValueError("Merged/split rodata is unsupported by the ELF loader")
        if section[1] != 11:  # SHT_DYNSYM
            continue
        if section[9] != 16 or section[6] >= count:
            raise ValueError("Invalid dynamic symbol table")
        strings = payload(sections[section[6]])
        symbols = payload(section)
        for i in range(16, len(symbols), 16):
            symbol = struct.unpack_from("<IIIBBH", symbols, i)
            if symbol[5] == 0 and symbol[0]:
                imports.add(string(strings, symbol[0]))
    if ".text" not in section_names or not header[4]:
        raise ValueError("Missing app entry/text")
    # GCC may lower a C struct initialization/copy to these two loader exports.
    unknown = {name for name in imports if not name.startswith("mk_") and name not in {"memcpy", "memset"}}
    if unknown:
        raise ValueError("App imports symbols outside mk_app_abi/compiler memory helpers: " + ", ".join(sorted(unknown)))
    return sorted(imports)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="Directory containing app_main.c")
    parser.add_argument("--toolchain", help="ESP32-S3 package, bin directory, or GCC executable")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    app = args.app.resolve()
    source = app / "app_main.c"
    if not source.is_file():
        raise SystemExit("Missing app_main.c in " + str(app))
    output = (args.output or ROOT / ".build/native-apps" / app.name / "app.elf").resolve()
    if output.with_suffix(output.suffix + ".sig").exists():
        raise SystemExit("Refusing to overwrite an ELF with an adjacent signature; use a separate build output.")
    tools = toolchain(args.toolchain)
    flags = ["-std=c99", "-mlongcalls", "-nostartfiles", "-nostdlib", "-fPIC", "-shared",
             "-e", "app_main", "-fdata-sections", "-ffunction-sections", "-Wl,--gc-sections",
             "-fvisibility=hidden", "-O0", "-fno-merge-constants", "-fno-jump-tables",
             "-Wall", "-Wextra", "-Werror", "-I" + str(ROOT / "src/system")]
    removed = [".comment", ".got.loc", ".dynamic", ".xt.lit", ".xt.prop", ".xtensa.info"]
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="native-app-", dir=output.parent) as work:
        image = Path(work) / "app.elf"
        subprocess.run([str(tools["gcc"]), *flags, "-o", str(image), str(source)], check=True)
        subprocess.run([str(tools["strip"]), "--strip-unneeded",
                        *["--remove-section=" + name for name in removed], str(image)], check=True)
        data = image.read_bytes()
        imports = inspect_elf(data)
        image.replace(output)
    compiler = subprocess.check_output([str(tools["gcc"]), "--version"], text=True).splitlines()[0]
    receipt = {"app": app.name, "elf": str(output), "bytes": len(data),
               "sha256": hashlib.sha256(data).hexdigest(), "compiler": compiler,
               "flags": flags, "imports": imports, "signed": False}
    output.with_suffix(".build.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
