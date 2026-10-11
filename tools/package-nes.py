"""Package matched NES firmware and a verified native SD app, without user data."""
import argparse
import csv
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent
PUBLIC_KEY = Path('tools/app_signing/nes-development.pub')
FORBIDDEN_SUFFIXES = {'.nes', '.sav', '.bak', '.tmp', '.seed', '.key', '.pem'}
REQUIRED_FILES = {
    'firmware.bin', 'ANLEITUNG.md', 'BUILD.json', 'SHA256SUMS.txt',
    'apps/nes/app.elf', 'apps/nes/app.elf.sig', 'apps/nes/manifest.ini',
    'roms/nes/README.txt', 'saves/nes/README.txt',
    'licenses/nofrendo-COPYING', 'licenses/nofrendo-CREDITS',
    'licenses/NES-CORE-PROVENANCE.md', 'licenses/NES-APP-SIGNING.md',
    'licenses/nes-development.pub',
}

def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()

def sha256(data):
    return hashlib.sha256(data).hexdigest()


def verify_signature(signer, app, signature, public):
    """Verify exactly the bytes later packaged, immune to source-file replacement."""
    with tempfile.TemporaryDirectory(prefix='nes-package-verify-') as temporary:
        directory = Path(temporary)
        assert directory.resolve().parent == Path(tempfile.gettempdir()).resolve()
        assert directory.name.startswith('nes-package-verify-')
        (directory / 'app.elf').write_bytes(app)
        (directory / 'app.elf.sig').write_bytes(signature)
        (directory / 'public.hex').write_bytes(public)
        try:
            result = subprocess.run([str(signer.resolve()), 'verify', str(directory / 'public.hex'),
                                     str(directory / 'app.elf'), str(directory / 'app.elf.sig')],
                                    capture_output=True, timeout=30)
        except (OSError, subprocess.TimeoutExpired) as error:
            raise ValueError('Native app signature verifier unavailable or timed out') from error
        if result.returncode:
            raise ValueError('Native app signature is not valid for the NES development key')


def verify_package(archive):
    """Check exact ZIP membership and every payload hash; returns the build record."""
    with zipfile.ZipFile(archive) as package:
        names = package.namelist()
        if len(names) != len(set(names)) or set(names) != REQUIRED_FILES:
            raise ValueError('Unexpected or missing package entries')
        for name in names:
            if (PurePosixPath(name).is_absolute() or '..' in PurePosixPath(name).parts or
                    '\\' in name or Path(name).suffix.lower() in FORBIDDEN_SUFFIXES):
                raise ValueError('Forbidden package path or user data')
        sums = {}
        for line in package.read('SHA256SUMS.txt').decode('ascii').splitlines():
            digest, name = line.split('  ', 1)
            if name in sums or not re.fullmatch('[0-9a-f]{64}', digest):
                raise ValueError('Invalid or duplicate checksum entry')
            sums[name] = digest
        if set(sums) != REQUIRED_FILES - {'SHA256SUMS.txt'}:
            raise ValueError('Incomplete checksum coverage')
        for name, digest in sums.items():
            if sha256(package.read(name)) != digest:
                raise ValueError('Package checksum mismatch: ' + name)
        build = json.loads(package.read('BUILD.json'))
        if (build['firmware_sha256'] != sha256(package.read('firmware.bin')) or
                build['app_sha256'] != sha256(package.read('apps/nes/app.elf')) or
                build['signature_sha256'] != sha256(package.read('apps/nes/app.elf.sig')) or
                build['public_key_sha256'] != sha256(package.read('licenses/nes-development.pub')) or
                build['firmware_bytes'] != len(package.read('firmware.bin')) or
                build['app_bytes'] != len(package.read('apps/nes/app.elf'))):
            raise ValueError('Build record does not match package payloads')
        return build


def build_package(firmware, app_path, signature_path, signer, output):
    version = re.search(r'#define MEOWGOTCHI_FW_VERSION\s+"([^"]+)"',
                        (ROOT / 'src/bsp/config.h').read_text(encoding='utf-8'))[1]
    if not re.fullmatch(r'[A-Za-z0-9._-]+', version):
        raise ValueError('Invalid version for package filename')
    image = firmware.read_bytes()
    slots = []
    for row in csv.reader((ROOT / 'partitions_ota_16mb.csv').read_text().splitlines()):
        if len(row) >= 5 and row[1].strip() == 'app':
            slots.append(int(row[4].strip(), 0))
    if not slots or len(image) > min(slots) or len(image) < 65536 or image[0] != 0xe9:
        raise ValueError('Invalid image or OTA partition overflow')
    if version.encode() not in image:
        raise ValueError('Build version does not match source config')
    app = app_path.read_bytes()
    if (not 52 <= len(app) <= 512 * 1024 or app[:7] != b'\x7fELF\x01\x01\x01' or
            struct.unpack_from('<HHI', app, 16) != (3, 94, 1)):
        raise ValueError('Invalid or oversized Xtensa native app')
    signature = signature_path.read_bytes()
    if len(signature) != 64:
        raise ValueError('Native app signature must be exactly 64 bytes')
    public = (ROOT / PUBLIC_KEY).read_bytes()
    if not re.fullmatch(rb'[0-9a-fA-F]{64}\s*', public):
        raise ValueError('Invalid NES development public key')
    verify_signature(signer, app, signature, public)
    app_manifest = (ROOT / 'sd files/apps/nes/manifest.ini').read_bytes()
    fields = dict(line.split('=', 1) for line in app_manifest.decode('utf-8').splitlines()
                  if '=' in line and not line.startswith(('#', ';')))
    if any(fields.get(key) != value for key, value in
           {'id': 'nes', 'type': 'native', 'entry': 'app.elf', 'nes_api': '1'}.items()):
        raise ValueError('Invalid NES native manifest')
    destination = output / ('MeowKit-NES-' + version)
    archive = destination.with_suffix(destination.suffix + '-SD.zip')
    if destination.exists() or archive.exists():
        raise ValueError('Output already exists; use a new output directory to preserve it')
    # Materialize a fixed whitelist in memory before creating package output.
    # User ROM/save directories are never traversed or copied.
    payloads = {
        'firmware.bin': image,
        'ANLEITUNG.md': (ROOT / 'docs/NES.md').read_bytes(),
        'apps/nes/app.elf': app, 'apps/nes/app.elf.sig': signature,
        'apps/nes/manifest.ini': app_manifest,
        'roms/nes/README.txt': b'Copy your .nes cartridge files here. Subfolders are supported. ROMs are not included.\n',
        'saves/nes/README.txt': b'The emulator writes checked cartridge SRAM files here when supported by the game.\n',
        'licenses/nofrendo-COPYING': (ROOT / 'lib/nes_core/vendor/COPYING').read_bytes(),
        'licenses/nofrendo-CREDITS': (ROOT / 'lib/nes_core/vendor/CREDITS').read_bytes(),
        'licenses/NES-CORE-PROVENANCE.md': (ROOT / 'lib/nes_core/PROVENANCE.md').read_bytes(),
        'licenses/NES-APP-SIGNING.md': (ROOT / 'tools/app_signing/README.md').read_bytes(),
        'licenses/nes-development.pub': public,
    }
    manifest = {
        'version': version, 'source_checkout': str(ROOT), 'source_commit': git('rev-parse', 'HEAD'),
        'source_dirty': bool(git('status', '--porcelain')), 'submodules': git('submodule', 'status'),
        'firmware_bytes': len(image), 'ota_slot_bytes': min(slots),
        'firmware_sha256': sha256(image),
        'architecture': 'native SD ELF frontend with firmware NES core and hardware services',
        'app_path': 'apps/nes/app.elf', 'app_version': fields.get('version', ''),
        'app_bytes': len(app), 'app_sha256': sha256(app),
        'signature_sha256': sha256(signature), 'signature_verified': True,
        'public_key_sha256': sha256(public), 'signing_key': 'local NES development Ed25519',
        'native_nes_api': 1,
        'core': 'ducalex/retro-go Nofrendo 4ced120669750ca7228fd0414211430c1d923166 + local patches',
        'hardware_tested': False, 'contains_roms': False,
        'app_hardware_tested': False,
        'installation': 'SD firmware update on compatible FeralCat dual-OTA, plus apps/nes native package',
    }
    payloads['BUILD.json'] = (json.dumps(manifest, indent=2) + '\n').encode('utf-8')
    payloads['SHA256SUMS.txt'] = ('\n'.join(sha256(data) + '  ' + name
        for name, data in sorted(payloads.items())) + '\n').encode('ascii')
    assert set(payloads) == REQUIRED_FILES
    destination.mkdir(parents=True)
    for name, data in payloads.items():
        path = destination / name
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open('xb') as file:
            file.write(data)
    with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED, compresslevel=9) as out:
        for name, data in sorted(payloads.items()):
            # Stable metadata: identical source/input bytes produce an identical ZIP.
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            out.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    verify_package(archive)
    return {'directory': str(destination), 'zip': str(archive),
            'zip_sha256': sha256(archive.read_bytes()), 'signature_verified': True,
            'app_sha256': sha256(app), 'app_bytes': len(app)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', type=Path, default=ROOT / '.build/firmware/esp32s3box/firmware.bin')
    parser.add_argument('--app', type=Path, default=ROOT / '.build/native-nes/app.elf')
    parser.add_argument('--signature', type=Path, help='Defaults to <app>.sig')
    parser.add_argument('--signer', type=Path, default=ROOT / '.build/app-signing/Release/meowsign.exe')
    parser.add_argument('--output', type=Path, default=ROOT / 'dist')
    args = parser.parse_args()
    try:
        result = build_package(args.firmware, args.app, args.signature or Path(str(args.app) + '.sig'),
                               args.signer, args.output)
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from error
    print(json.dumps(result, indent=2))

if __name__ == '__main__':
    main()
