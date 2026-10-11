"""Host-only packaging tests with synthetic images and an ephemeral test key."""
import argparse
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('package_nes', ROOT / 'tools/package-nes.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)
parser = argparse.ArgumentParser()
parser.add_argument('--signer', type=Path, default=ROOT / '.build/app-signing/Release/meowsign.exe')
args, remaining = parser.parse_known_args()
SIGNER = args.signer.resolve()


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='nes-package-test-')
        self.root = Path(self.temporary.name)
        assert self.root.resolve().parent == Path(tempfile.gettempdir()).resolve()
        assert self.root.name.startswith('nes-package-test-')
        self.addCleanup(self.temporary.cleanup)
        self.version = 'v0.11.2-nes.package-test'
        self.write('src/bsp/config.h', f'#define MEOWGOTCHI_FW_VERSION "{self.version}"\n'.encode())
        self.write('partitions_ota_16mb.csv', b'app0,app,ota_0,0x10000,0x20000\napp1,app,ota_1,0x30000,0x20000\n')
        self.write('docs/NES.md', b'Synthetic packaging test, no hardware claim.\n')
        self.write('sd files/apps/nes/manifest.ini', b'id=nes\nname=NES\nversion=1.1.0\napi=1\nentry=app.elf\ntype=native\nnes_api=1\n')
        for name in ('lib/nes_core/vendor/COPYING', 'lib/nes_core/vendor/CREDITS',
                     'lib/nes_core/PROVENANCE.md', 'tools/app_signing/README.md'):
            self.write(name, b'Synthetic fixture notice\n')
        self.seed = self.root / 'test.seed.bin'
        self.public = self.root / package.PUBLIC_KEY
        self.public.parent.mkdir(parents=True, exist_ok=True)
        self.signer('keygen', self.seed, self.public)
        self.firmware = self.root / 'firmware.bin'
        self.firmware.write_bytes(b'\xe9' + self.version.encode() + bytes(65536 - 1 - len(self.version)))
        self.app = self.root / 'app.elf'
        # Synthetic ELF identification/header, never executed on host or device.
        header = struct.pack('<16sHHIIIIIHHHHHH', b'\x7fELF\x01\x01\x01' + bytes(9),
                             3, 94, 1, 0x100, 0, 0, 0, 52, 0, 0, 0, 0, 0)
        self.app.write_bytes(header + b'synthetic test payload\0')
        self.signature = self.root / 'app.elf.sig'
        self.signer('sign', self.seed, self.app, self.signature)
        self.output = self.root / 'release'
        self.root_patch = patch.object(package, 'ROOT', self.root)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)
        self.git_patch = patch.object(package, 'git', side_effect=lambda *a: '' if a[0] in ('status', 'submodule') else 'fixture-commit')
        self.git_patch.start()
        self.addCleanup(self.git_patch.stop)

    def write(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def signer(self, *arguments):
        result = subprocess.run([str(SIGNER), *map(str, arguments)], capture_output=True)
        self.assertEqual(result.returncode, 0, 'test signing setup failed')

    def build(self, **overrides):
        values = dict(firmware=self.firmware, app_path=self.app, signature_path=self.signature,
                      signer=SIGNER, output=self.output)
        values.update(overrides)
        return package.build_package(**values)

    def rejected_before_output(self, **overrides):
        with self.assertRaises((ValueError, OSError)):
            self.build(**overrides)
        self.assertFalse(self.output.exists())

    def test_package_membership_hashes_and_no_user_data(self):
        self.write('roms/nes/example.nes', b'fake test ROM name, not a ROM')
        self.write('saves/nes/example.sav', b'fake test save')
        self.write('sd files/apps/nes/do-not-copy.seed.bin', b'fake secret sentinel')
        result = self.build()
        build = package.verify_package(Path(result['zip']))
        self.assertEqual(build['app_sha256'], package.sha256(self.app.read_bytes()))
        self.assertTrue(build['signature_verified'])
        self.assertFalse(build['hardware_tested'])
        self.assertFalse(build['app_hardware_tested'])
        self.assertFalse(build['contains_roms'])
        self.assertIn('SD ELF frontend', build['architecture'])
        with zipfile.ZipFile(result['zip']) as archive:
            self.assertEqual(set(archive.namelist()), package.REQUIRED_FILES)
            self.assertEqual(archive.read('apps/nes/app.elf.sig'), self.signature.read_bytes())
            self.assertEqual(archive.read('apps/nes/app.elf'), self.app.read_bytes())

    def test_modified_signed_app_is_rejected(self):
        self.app.write_bytes(self.app.read_bytes() + b'x')
        self.rejected_before_output()

    def test_only_the_verified_snapshot_is_packaged(self):
        original = self.app.read_bytes()
        real_run = subprocess.run

        def replace_source_during_verification(*args, **kwargs):
            self.app.write_bytes(original + b'concurrent source replacement')
            return real_run(*args, **kwargs)

        with patch.object(package.subprocess, 'run', side_effect=replace_source_during_verification):
            result = self.build()
        with zipfile.ZipFile(result['zip']) as archive:
            self.assertEqual(archive.read('apps/nes/app.elf'), original)

    def test_identical_inputs_produce_identical_archives(self):
        first = self.build()
        second = self.build(output=self.root / 'second-release')
        self.assertEqual(Path(first['zip']).read_bytes(), Path(second['zip']).read_bytes())

    def test_modified_signature_is_rejected(self):
        signature = bytearray(self.signature.read_bytes())
        signature[0] ^= 1
        self.signature.write_bytes(signature)
        self.rejected_before_output()

    def test_signature_lengths_are_rejected(self):
        for length in (0, 63, 65):
            with self.subTest(length=length):
                self.signature.write_bytes(bytes(length))
                self.rejected_before_output()

    def test_foreign_signer_key_is_rejected(self):
        other_seed, other_signature = self.root / 'other.seed.bin', self.root / 'other.sig'
        self.signer('keygen', other_seed)
        self.signer('sign', other_seed, self.app, other_signature)
        self.rejected_before_output(signature_path=other_signature)

    def test_non_elf_and_oversized_app_are_rejected(self):
        self.app.write_bytes(bytes(80))
        self.rejected_before_output()
        self.app.write_bytes(bytes(512*1024+1))
        self.rejected_before_output()

    def test_firmware_bounds_and_version_are_rejected(self):
        for data in (b'', bytes(65536), b'\xe9' + bytes(65535), b'\xe9' + bytes(131072)):
            with self.subTest(size=len(data)):
                self.firmware.write_bytes(data)
                self.rejected_before_output()

    def test_missing_verifier_is_rejected(self):
        self.rejected_before_output(signer=self.root / 'missing-signer.exe')

    def test_invalid_manifest_is_rejected(self):
        self.write('sd files/apps/nes/manifest.ini', b'id=nes\ntype=lua\nentry=main.lua\n')
        self.rejected_before_output()

    def test_existing_output_is_preserved(self):
        result = self.build()
        archive = Path(result['zip'])
        original = archive.read_bytes()
        with self.assertRaises(ValueError):
            self.build()
        self.assertEqual(archive.read_bytes(), original)

    def test_zip_payload_corruption_is_detected(self):
        result = self.build()
        corrupt = self.root / 'corrupt.zip'
        with zipfile.ZipFile(result['zip']) as source, zipfile.ZipFile(corrupt, 'x') as target:
            for name in source.namelist():
                data = source.read(name)
                target.writestr(name, data + b'changed' if name == 'apps/nes/app.elf' else data)
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            package.verify_package(corrupt)

    def test_extra_zip_entries_are_rejected(self):
        result = self.build()
        with zipfile.ZipFile(result['zip'], 'a') as archive:
            archive.writestr('roms/nes/not-allowed.nes', b'not a real ROM')
        with self.assertRaisesRegex(ValueError, 'entries'):
            package.verify_package(Path(result['zip']))


if __name__ == '__main__':
    if not SIGNER.is_file():
        raise SystemExit('Build meowsign first or pass --signer.')
    unittest.main(argv=[sys.argv[0], *remaining])
