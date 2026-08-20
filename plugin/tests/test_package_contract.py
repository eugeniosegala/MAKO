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


if __name__ == "__main__":
    unittest.main()
