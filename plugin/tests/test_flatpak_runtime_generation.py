"""Exercise catalogue changes and read-only freshness across both components."""

import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "flatpak_runtime_generator", REPOSITORY_ROOT / "scripts/generate-flatpak-runtimes.py"
)
generator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generator)


class FlatpakRuntimeGenerationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for relative in (generator.CATALOGUE, generator.TEMPLATE):
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((REPOSITORY_ROOT / relative).read_bytes())
        self.entries = generator.load_runtimes(self.root)

    def catalogue(self, entries):
        (self.root / generator.CATALOGUE).write_text(json.dumps(entries))

    def snapshot(self):
        return {
            path.relative_to(self.root): (path.read_bytes(), path.stat().st_mtime_ns)
            for path in self.root.rglob("*") if path.is_file()
        }

    def test_adding_and_retiring_a_runtime_updates_both_components(self):
        generator.synchronize(self.root, check=False)
        added = {
            "version": f"{int(self.entries[-1]['version'][:2]) + 1:02d}.08",
            "llvm": self.entries[-1]["llvm"] + 1,
        }
        self.catalogue([*self.entries, added])
        before = self.snapshot()
        stale = generator.synchronize(self.root, check=True)
        self.assertEqual(self.snapshot(), before)
        self.assertIn(Path("plugin/shared_flatpak_runtimes.py"), stale)
        generator.synchronize(self.root, check=False)
        matrix = self.root / generator.MATRIX_DIRECTORY
        self.assertIn(f"{added['version']}\n", (matrix / "runtime-versions.txt").read_text())
        manifest = matrix / f"{generator.MANIFEST_PREFIX}{added['version']}.yml"
        self.assertIn(f"org.freedesktop.Sdk.Extension.llvm{added['llvm']}", manifest.read_text())
        self.assertIn(f"runtime-version: '{added['version']}'", manifest.read_text())
        self.assertIn(f'"{added["version"]}",', (self.root / "plugin/shared_flatpak_runtimes.py").read_text())
        # Remove the oldest entry too: no retired manifest or Decky row survives.
        retired = self.entries[0]["version"]
        self.catalogue([*self.entries[1:], added])
        before = self.snapshot()
        self.assertTrue(generator.synchronize(self.root, check=True))
        self.assertEqual(self.snapshot(), before)
        generator.synchronize(self.root, check=False)
        self.assertFalse((matrix / f"{generator.MANIFEST_PREFIX}{retired}.yml").exists())
        self.assertNotIn(retired, (matrix / "runtime-versions.txt").read_text())
        self.assertNotIn(retired, (self.root / "plugin/shared_flatpak_runtimes.py").read_text())
        self.assertEqual(generator.synchronize(self.root, check=True), [])

    def test_missing_and_modified_outputs_are_rejected_without_writes(self):
        generator.synchronize(self.root, check=False)
        outputs = generator.generated_files(self.root)
        for relative in outputs:
            with self.subTest(output=relative):
                path = self.root / relative
                path.write_text("stale\n")
                before = self.snapshot()
                self.assertIn(relative, generator.synchronize(self.root, check=True))
                self.assertEqual(self.snapshot(), before)
                path.unlink()
                before = self.snapshot()
                self.assertIn(relative, generator.synchronize(self.root, check=True))
                self.assertEqual(self.snapshot(), before)
                generator.synchronize(self.root, check=False)

    def test_generation_preserves_foreign_manifests(self):
        generator.synchronize(self.root, check=False)
        foreign = self.root / generator.MATRIX_DIRECTORY / f"{generator.MANIFEST_PREFIX}99.08.yml"
        foreign.write_text("foreign manifest\n")
        before = self.snapshot()
        self.assertIn(foreign.relative_to(self.root), generator.synchronize(self.root, check=True))
        with self.assertRaisesRegex(ValueError, "unmanaged manifest"):
            generator.synchronize(self.root, check=False)
        self.assertEqual(self.snapshot(), before)

    def test_invalid_catalogues_fail_before_writing(self):
        invalid = (
            [], {}, [self.entries[0], self.entries[0]], list(reversed(self.entries)),
            [{"version": "26.08", "llvm": True}],
            [{"version": "26.08", "llvm": 0}],
            [{"version": "26.08", "llvm": "22"}],
            [{"version": "../26.08", "llvm": 22}],
            [{"version": "26.08", "llvm": 22, "extra": 1}],
        )
        for entries in invalid:
            with self.subTest(entries=entries):
                self.catalogue(entries)
                before = self.snapshot()
                with self.assertRaises(ValueError):
                    generator.synchronize(self.root, check=False)
                self.assertEqual(self.snapshot(), before)

    def test_decky_binding_imports_without_renderer_checkout(self):
        # Retiring the oldest branch must not break constants or the package helper.
        retained = self.entries[1:] if len(self.entries) > 1 else self.entries
        self.catalogue(retained)
        generator.synchronize(self.root, check=False)
        for relative in (
            "shared_config.py", "scripts/read_flatpak_runtime_contract.py",
            "py_modules/mako_plugin/__init__.py", "py_modules/mako_plugin/constants.py",
            "py_modules/mako_plugin/package_paths.py",
        ):
            target = self.root / "plugin" / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((REPOSITORY_ROOT / "plugin" / relative).read_bytes())
        shutil.rmtree(self.root / "engine")
        result = subprocess.run(
            [sys.executable, "-I", str(self.root / "plugin/scripts/read_flatpak_runtime_contract.py"),
             "versions"], text=True, capture_output=True, check=True,
        )
        self.assertEqual(result.stdout.splitlines(),
                         [entry["version"] for entry in retained])
