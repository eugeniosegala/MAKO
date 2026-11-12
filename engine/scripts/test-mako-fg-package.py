#!/usr/bin/env python3
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('package', Path(__file__).with_name('mako-fg-package.py'))
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class MakoFgPackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def release_catalogue(self, active='1.10'):
        """Open-weight releases as MAKO FG's release command mirrors them."""
        releases = self.root / 'models/mako-fg'
        entries = []
        for version, weights in (('1.9', b'MFGTV8\0\0 weights of 1.9'), ('1.10', b'MFGTV9\0\0 weights of 1.10')):
            folder = releases / f'V{version}'
            files = {'model/weights.bin': weights, 'model/ops.spv': b'\x03\x02\x23\x07shared ops',
                     'model/transport.spv': b'\x03\x02\x23\x07shared transport',
                     'runtime/x64/bin/mako-fg-host': b'\x7fELF host ' + version.encode(),
                     'MODEL_CARD.md': b'Public model card'}
            for name, data in files.items():
                (folder / name).parent.mkdir(parents=True, exist_ok=True)
                (folder / name).write_bytes(data)
            (folder / 'MANIFEST.json').write_text(json.dumps({'format': 2, 'files': [
                {'name': name, 'sha256': hashlib.sha256(data).hexdigest()} for name, data in files.items()]}))
            entries.append({'version': version, 'folder': folder.name})
        (releases / 'versions.json').write_text(json.dumps({'active': active, 'versions': entries}))
        return releases

    def library(self, name, version=None, embedded=None, directory='lib'):
        """A Renderer library in the given directory, with a release's files where a package installs them."""
        releases = self.root / 'models/mako-fg'
        library = self.root / name / directory / 'libmako-render.so'
        library.parent.mkdir(parents=True, exist_ok=True)
        extra = (releases / f'V{embedded}/model/weights.bin').read_bytes() if embedded else b''
        library.write_bytes(b'\x7fELF prefix ' + extra + b' suffix')
        if version:
            folder = releases / f'V{version}'
            program = self.root / name / {'lib': 'lib', 'lib32': 'lib', 'lib/i386-linux-gnu': 'lib64'}[directory] / 'mako-fg'
            (program / 'model').mkdir(parents=True, exist_ok=True)
            for file in ('weights.bin', 'ops.spv', 'transport.spv'):
                (program / 'model' / file).write_bytes((folder / 'model' / file).read_bytes())
            (program / 'mako-fg-host').write_bytes((folder / 'runtime/x64/bin/mako-fg-host').read_bytes())
        return library

    def test_each_library_sits_beside_the_active_release(self):
        releases = self.release_catalogue(active='1.9')
        # The active field chooses the release, even when a higher version exists; releases
        # sharing shaders are told apart by their weights.
        self.assertEqual(package.verify(releases, [self.library('active', '1.9')]), '1.9')
        # 32-bit native and Flatpak layers share the 64-bit program.
        self.assertEqual(package.verify(releases, [self.library('native', '1.9', directory='lib32'),
                                                   self.library('flatpak', '1.9', directory='lib/i386-linux-gnu')]), '1.9')
        with self.assertRaisesRegex(package.PackageError, r'is not beside MAKO FG release 1.9 \(model/weights.bin, mako-fg-host\)'):
            package.verify(releases, [self.library('newest', '1.10')])
        with self.assertRaisesRegex(package.PackageError, 'is not beside MAKO FG release 1.9'):
            package.verify(releases, [self.library('missing')])

    def test_no_library_may_contain_mako_fg(self):
        releases = self.release_catalogue()
        for version in ('1.9', '1.10'):
            library = self.library(f'embedded-{version}', '1.10', embedded=version)
            with self.assertRaisesRegex(package.PackageError, 'must stay a separate program'):
                package.verify(releases, [library])
            with self.assertRaisesRegex(package.PackageError, 'must stay a separate program'):
                package.verify(releases, [library], installed=False)
        self.assertEqual(package.verify(releases, [self.library('build-tree')], installed=False), '1.10')

    def test_releases_must_be_open_and_intact(self):
        releases = self.release_catalogue()
        library = self.library('renderer', '1.10')
        (releases / 'V1.10/model/ops.spv').write_bytes(b'edited')
        with self.assertRaisesRegex(package.PackageError, 'differs from its MANIFEST.json'):
            package.verify(releases, [library])
        manifest = json.loads((releases / 'V1.9/MANIFEST.json').read_text())
        (releases / 'V1.9/MANIFEST.json').write_text(json.dumps({**manifest, 'format': 1}))
        with self.assertRaisesRegex(package.PackageError, 'not an open-weight release'):
            package.verify(releases, [library])
        (releases / 'versions.json').write_text(json.dumps({'active': '1.10', 'versions': [{'version': '1.10', 'folder': '../elsewhere'}]}))
        with self.assertRaisesRegex(package.PackageError, 'Unsafe'):
            package.verify(releases, [library])

    def test_artifact_reuse_requires_the_verified_archive_and_current_active_release(self):
        releases = self.release_catalogue()
        artifact = self.root / 'renderer.tar.xz'
        artifact.write_bytes(b'archive fixture')
        library = self.library('renderer', '1.10')
        with self.assertRaises(OSError):
            package.check_artifact(releases, artifact)
        package.record_artifact(releases, artifact, [library])
        self.assertEqual(package.check_artifact(releases, artifact), '1.10')
        artifact.write_bytes(b'modified archive')
        with self.assertRaisesRegex(package.PackageError, 'rebuild it'):
            package.check_artifact(releases, artifact)
        artifact.write_bytes(b'archive fixture')
        index = json.loads((releases / 'versions.json').read_text())
        (releases / 'versions.json').write_text(json.dumps({**index, 'active': '1.9'}))
        with self.assertRaisesRegex(package.PackageError, 'rebuild it'):
            package.check_artifact(releases, artifact)
        with self.assertRaisesRegex(package.PackageError, 'is not beside MAKO FG release 1.9'):
            package.record_artifact(releases, artifact, [library])
        package.record_artifact(releases, artifact, [self.library('older', '1.9')])
        self.assertEqual(package.check_artifact(releases, artifact), '1.9')

    def test_artifact_receipts_require_every_requested_architecture_and_intact_model(self):
        releases = self.release_catalogue()
        artifact = self.root / 'flatpaks.tar.xz'
        artifact.write_bytes(b'flatpak archive fixture')
        library = self.library('64-bit', '1.10')
        other = self.library('32-bit')
        with self.assertRaisesRegex(package.PackageError, 'requires verified'):
            package.record_artifact(releases, artifact, [])
        with self.assertRaisesRegex(package.PackageError, 'is not beside'):
            package.record_artifact(releases, artifact, [library, other])
        self.assertFalse(artifact.with_name(artifact.name + '.mako-fg.json').exists())
        package.record_artifact(releases, artifact, [library, self.library('32-bit', '1.10', directory='lib32')])
        (releases / 'V1.10/model/weights.bin').write_bytes(b'changed model')
        with self.assertRaisesRegex(package.PackageError, 'differs from its MANIFEST.json'):
            package.check_artifact(releases, artifact)


if __name__ == '__main__':
    unittest.main()
