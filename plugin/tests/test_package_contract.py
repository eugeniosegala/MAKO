"""Tests for Decky Loader-compatible packaged metadata."""

import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


PLUGIN_DIR = Path(__file__).resolve().parents[1]
VALIDATOR = PLUGIN_DIR / "scripts/validate-package-contract.mjs"


class PackageContractTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.package_dir = Path(self.temp_dir.name) / "Mako"
        self.bin_dir = self.package_dir / "bin"
        self.bin_dir.mkdir(parents=True)
        legal_files = {
            "ASSET_PROVENANCE.md": "MAKO asset provenance\n",
            "LICENSE.md": "GPL-3.0-or-later\n",
            "THIRD_PARTY_NOTICES.md": "MAKO third-party notices\n",
            "third_party_licenses/@decky-api-LGPL-2.1.txt": "LGPL-2.1\n",
            "third_party_licenses/react-icons-LICENSE.txt": "React Icons MIT\n",
            "third_party_licenses/tslib-0BSD.txt": "tslib 0BSD\n",
        }
        for relative_path, contents in legal_files.items():
            destination = self.package_dir / relative_path
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(contents, encoding="utf-8")
        source_map = {
            "version": 3,
            "sources": [
                "node_modules/@decky/api/dist/index.js",
                "node_modules/react-icons/fi/index.mjs",
            ],
            "sourcesContent": ["export const api = {};", "export const Icon = {};"],
        }
        source_map_path = self.package_dir / "dist" / "index.js.map"
        source_map_path.parent.mkdir(parents=True)
        source_map_path.write_text(json.dumps(source_map), encoding="utf-8")
        self.archive_name = "MAKO-Renderer-v2.0.0-test-linux.tar.xz"
        self.archive = self.bin_dir / self.archive_name
        self.archive.write_bytes(b"verified-renderer-archive")
        self.checksum = hashlib.sha256(self.archive.read_bytes()).hexdigest()

    def tearDown(self):
        self.temp_dir.cleanup()

    def _renderer(self):
        return {
            "name": self.archive_name,
            "version": "2.0.0",
            "sha256hash": self.checksum,
            "host_architectures": ["x86_64"],
        }

    def _validate(self, manifest, mode):
        (self.package_dir / "package.json").write_text(
            json.dumps(manifest), encoding="utf-8"
        )
        return subprocess.run(
            ["node", str(VALIDATOR), str(self.package_dir), mode],
            check=False,
            capture_output=True,
            text=True,
        )

    def test_accepts_self_contained_local_renderer(self):
        result = self._validate(
            {"version": "2.1.0.local.test", "bundled_renderer": self._renderer()},
            "local",
        )

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_remote_binary_in_self_contained_local_package(self):
        renderer = self._renderer()
        renderer["url"] = "local-worktree://renderer.tar.xz"

        result = self._validate(
            {
                "version": "2.1.0.local.test",
                "remote_binary_bundling": True,
                "remote_binary": [renderer],
            },
            "local",
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Decky Loader always downloads it", result.stderr)

    def test_accepts_release_remote_binary_for_2_0_upgrade(self):
        renderer = self._renderer()
        renderer["url"] = (
            "https://github.com/eugeniosegala/MAKO/releases/download/"
            "render-v2.0.0/MAKO-Renderer-v2.0.0-linux.tar.xz"
        )

        result = self._validate(
            {
                "version": "2.1.0",
                "remote_binary_bundling": True,
                "remote_binary": [renderer],
            },
            "release",
        )

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_non_https_release_renderer(self):
        renderer = self._renderer()
        renderer["url"] = "local-worktree://renderer.tar.xz"

        result = self._validate(
            {
                "version": "2.1.0",
                "remote_binary_bundling": True,
                "remote_binary": [renderer],
            },
            "release",
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("valid HTTPS URL", result.stderr)

    def test_rejects_embedded_archive_checksum_mismatch(self):
        renderer = self._renderer()
        renderer["sha256hash"] = "0" * 64

        result = self._validate(
            {"version": "2.1.0.local.test", "bundled_renderer": renderer},
            "local",
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("checksum does not match", result.stderr)

    def test_rejects_package_without_third_party_notices(self):
        (self.package_dir / "THIRD_PARTY_NOTICES.md").unlink()

        result = self._validate(
            {"version": "2.1.0.local.test", "bundled_renderer": self._renderer()},
            "local",
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Required legal file is missing", result.stderr)

    def test_rejects_package_without_bundled_dependency_source(self):
        (self.package_dir / "dist" / "index.js.map").unlink()

        result = self._validate(
            {"version": "2.1.0.local.test", "bundled_renderer": self._renderer()},
            "local",
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("frontend source map is missing", result.stderr)


if __name__ == "__main__":
    unittest.main()
