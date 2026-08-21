"""Tests for local and published release identity parsing."""

import json
from pathlib import Path
import subprocess
import tempfile
import unittest


PLUGIN_DIR = Path(__file__).resolve().parents[1]
READER = PLUGIN_DIR / "scripts/read-release-info.mjs"


class ReleaseInfoTests(unittest.TestCase):
    def _read(self, contents, *arguments):
        with tempfile.TemporaryDirectory() as temporary_directory:
            notes_path = Path(temporary_directory) / "RELEASE_NOTES.md"
            notes_path.write_text(contents, encoding="utf-8")
            return subprocess.run(
                [
                    "node",
                    str(READER),
                    str(notes_path),
                    "MAKO Decky",
                    *arguments,
                ],
                check=False,
                capture_output=True,
                text=True,
            )

    def test_reads_version_and_codename(self):
        result = self._read(
            "## What's new in MAKO Decky v2.1.0\n\n"
            "### Release codename: Leviathan Rising\n"
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            json.loads(result.stdout),
            {"version": "2.1.0", "codename": "Leviathan Rising"},
        )

    def test_reads_version_for_local_packaging(self):
        result = self._read(
            "## What's new in MAKO Decky v2.1.0\n\n"
            "### Release codename: Leviathan Rising\n",
            "--version",
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "2.1.0")

    def test_rejects_an_unversioned_heading(self):
        result = self._read(
            "## What's new in MAKO Decky\n\n"
            "### Release codename: Leviathan Rising\n"
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("v<X.Y.Z>", result.stderr)

    def test_rejects_a_missing_codename(self):
        result = self._read("## What's new in MAKO Decky v2.1.0\n")

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Release codename", result.stderr)


if __name__ == "__main__":
    unittest.main()
