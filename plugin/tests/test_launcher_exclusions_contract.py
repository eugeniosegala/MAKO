"""Validate the shared launcher registry and its read-only generation gate."""

import copy
import json
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
GENERATOR_PATH = Path("scripts/generate-launcher-exclusions.py")
GENERATOR = runpy.run_path(str(REPOSITORY_ROOT / GENERATOR_PATH))


class LauncherExclusionsContractTests(unittest.TestCase):
    def test_new_launcher_is_documented_and_normalized(self):
        entry = {
            "launcher": "Example Launcher",
            "reason": "Its UI inherits the child game's profile.",
            "executables": ["ExampleLauncher.EXE", "ExampleWeb.exe"],
        }
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "launchers.json"
            source.write_text(json.dumps([entry]), encoding="utf-8")
            normalized = GENERATOR["load_exclusions"](source)
        self.assertEqual(normalized, [{
            **entry, "executables": ["examplelauncher.exe", "exampleweb.exe"],
        }])

    def test_invalid_or_ambiguous_entries_are_rejected(self):
        valid = {
            "launcher": "Example Launcher",
            "reason": "Keep the launcher UI inactive.",
            "executables": ["Example.exe"],
        }
        invalid = [
            {},
            [None],
            [{**valid, "reason": " "}],
            [{**valid, "launcher": ""}],
            [{**valid, "unexpected": True}],
            [{**valid, "executables": []}],
            [valid, {**valid, "launcher": "EXAMPLE LAUNCHER"}],
            [valid, {**valid, "launcher": "Another launcher"}],
        ]
        for name in ("*.exe", "foo?.exe", "dir/Example.exe", "C:\\Example.exe",
                     "Example", "Example.exe.backup", "Example Launcher.exe",
                     'bad".exe', "ゲーム.exe", 7):
            invalid.append([{**valid, "executables": [name]}])
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "launchers.json"
            for entries in invalid:
                with self.subTest(entries=entries):
                    source.write_text(json.dumps(entries), encoding="utf-8")
                    with self.assertRaises(ValueError):
                        GENERATOR["load_exclusions"](source)

    def test_check_detects_missing_and_stale_outputs_without_writing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = [GENERATOR_PATH, GENERATOR["SOURCE"].relative_to(REPOSITORY_ROOT)]
            outputs = [GENERATOR[name].relative_to(REPOSITORY_ROOT)
                       for name in ("RENDERER_OUTPUT", "DECKY_OUTPUT")]
            for relative in paths + outputs:
                (root / relative).parent.mkdir(parents=True, exist_ok=True)
            for relative in paths:
                shutil.copyfile(REPOSITORY_ROOT / relative, root / relative)

            def run(*args):
                return subprocess.run(
                    [sys.executable, str(root / GENERATOR_PATH), *args],
                    capture_output=True, text=True, check=False,
                )

            self.assertEqual(run("--check").returncode, 1)
            self.assertTrue(all(not (root / path).exists() for path in outputs))
            generated = run()
            self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertEqual(run("--check").returncode, 0)
            snapshot = {path: (root / path).read_bytes() for path in outputs}

            source = root / paths[1]
            entries = json.loads(source.read_text(encoding="utf-8"))
            new_entry = copy.deepcopy(entries[0])
            new_entry.update(launcher="Example Launcher", executables=["Example.exe"])
            entries.append(new_entry)
            source.write_text(json.dumps(entries), encoding="utf-8")
            checked = run("--check")
            self.assertEqual(checked.returncode, 1, checked.stderr)
            for relative, content in snapshot.items():
                self.assertIn(str(relative), checked.stderr)
                self.assertEqual((root / relative).read_bytes(), content)
            self.assertEqual(run().returncode, 0)
            self.assertEqual(run("--check").returncode, 0)
            # The packaged Python binding needs no registry or Renderer checkout.
            source.unlink()
            binding = runpy.run_path(str(root / outputs[1]))
            self.assertIn("example.exe", binding["EXCLUDED_WINDOWS_LAUNCHERS"])
