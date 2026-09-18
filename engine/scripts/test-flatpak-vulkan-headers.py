#!/usr/bin/env python3
"""Portable mutation coverage for the shared Flatpak header dependency."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ENGINE_ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ENGINE_ROOT / "scripts/generate-flatpak-vulkan-headers.py"


class FlatpakVulkanHeadersTests(unittest.TestCase):
    def test_checked_in_module_is_current(self):
        subprocess.run([sys.executable, str(GENERATOR), "--check"], check=True)

    def test_pin_changes_require_regeneration_without_check_rewriting(self):
        with tempfile.TemporaryDirectory() as directory:
            pin = Path(directory) / "revision.txt"
            output = Path(directory) / "module.json"
            command = [sys.executable, str(GENERATOR), "--revision-file", str(pin), "--output", str(output)]
            pin.write_text("v1.4.362\n")
            subprocess.run(command, check=True, capture_output=True)
            original = output.read_bytes()
            pin.write_text("v1.4.363\n")
            check = subprocess.run(command + ["--check"], capture_output=True)
            self.assertNotEqual(check.returncode, 0)
            self.assertEqual(output.read_bytes(), original)
            subprocess.run(command, check=True, capture_output=True)
            source = json.loads(output.read_text())["sources"][0]
            self.assertEqual(source["tag"], "v1.4.363")
            self.assertNotIn("branch", source)
            subprocess.run(command + ["--check"], check=True, capture_output=True)

    def test_invalid_ref_does_not_replace_generated_module(self):
        with tempfile.TemporaryDirectory() as directory:
            pin = Path(directory) / "revision.txt"
            output = Path(directory) / "module.json"
            pin.write_text("v1.4.362; unexpected-command\n")
            output.write_text("preserved\n")
            result = subprocess.run([sys.executable, str(GENERATOR), "--revision-file", str(pin), "--output", str(output)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_text(), "preserved\n")

    def test_all_runtime_architectures_use_and_require_shared_headers(self):
        directory = ENGINE_ROOT / "dist/flatpak/mako-render"
        module = json.loads((directory / "vulkan-headers.json").read_text())
        self.assertEqual(module["cleanup"], ["/include/vulkan", "/include/vk_video"])
        for runtime in (directory / "runtime-versions.txt").read_text().splitlines():
            if not runtime or runtime.startswith("#"):
                continue
            with self.subTest(runtime=runtime):
                manifest = (directory / f"org.freedesktop.Platform.VulkanLayer.makorender_{runtime}.yml").read_text()
                self.assertEqual(manifest.count("  - vulkan-headers.json\n"), 1)
                self.assertLess(manifest.index("  - vulkan-headers.json\n"), manifest.index("  - name: mako\n"))
                self.assertIn("  cxxflags: -I/usr/lib/extensions/vulkan/makorender/include\n", manifest)
                self.assertEqual(manifest.count("- -DMAKO_REQUIRE_NATIVE_PACKAGE_HEADERS=ON\n"), 2)


if __name__ == "__main__":
    unittest.main()
