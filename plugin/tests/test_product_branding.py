"""Regression tests for the public MAKO component names."""

import json
from pathlib import Path
import re
import unittest


PLUGIN_DIR = Path(__file__).resolve().parent.parent
REPOSITORY_ROOT = PLUGIN_DIR.parent


class ProductBrandingTests(unittest.TestCase):
    def test_decky_manifest_preserves_established_listing_identity(self):
        manifest = json.loads(
            (PLUGIN_DIR / "plugin.json").read_text(encoding="utf-8")
        )
        self.assertEqual(manifest["name"], "MAKO - Frame Generation")
        self.assertEqual(
            manifest["publish"]["description"],
            "MAKO brings Vulkan-powered Lossless Scaling frame generation to "
            "Steam Deck, Steam Machine, and SteamOS gaming.",
        )

    def test_decky_component_identity_is_mako_decky(self):

        frontend = (PLUGIN_DIR / "src/index.tsx").read_text(encoding="utf-8")
        self.assertIn('name: "MAKO Decky"', frontend)
        self.assertIn(">MAKO Decky</div>", frontend)

        content = (
            PLUGIN_DIR / "src/components/Content.tsx"
        ).read_text(encoding="utf-8")
        self.assertIn("MAKO Decky <code>", content)
        self.assertIn('{" · MAKO Renderer "}', content)

        configuration = (
            PLUGIN_DIR / "src/components/ConfigurationSection.tsx"
        ).read_text(encoding="utf-8")
        self.assertIn("Advanced MAKO Renderer Settings", configuration)

        lifecycle = (
            PLUGIN_DIR / "py_modules/mako_plugin/plugin.py"
        ).read_text(encoding="utf-8")
        self.assertNotIn("mako plugin", lifecycle.lower())
        self.assertIn('decky.logger.info("MAKO Decky loaded")', lifecycle)

        reloader = (
            PLUGIN_DIR / "scripts/reload-decky-plugin.mjs"
        ).read_text(encoding="utf-8")
        self.assertIn(
            'DEFAULT_PLUGIN_NAME = "MAKO - Frame Generation"', reloader
        )

        failure_guide = (
            PLUGIN_DIR / "docs/DECKY_INSTALLATION_FAILURES.md"
        ).read_text(encoding="utf-8")
        self.assertIn("=== MAKO Decky plugin log ===", failure_guide)
        self.assertNotIn("=== Mako plugin log ===", failure_guide)

    def test_default_decky_archive_uses_the_product_name(self):
        packager = (
            PLUGIN_DIR / "scripts/package-local.sh"
        ).read_text(encoding="utf-8")
        self.assertIn("MAKO-Decky.zip", packager)
        self.assertIn("MAKO-Decky-local.", packager)

    def test_release_codename_is_shared_by_package_and_component_notes(self):
        manifest = json.loads(
            (PLUGIN_DIR / "package.json").read_text(encoding="utf-8")
        )
        decky_notes = (
            PLUGIN_DIR / "RELEASE_NOTES.md"
        ).read_text(encoding="utf-8")
        match = re.search(r"^### Codename:\s*(.+?)\s*$", decky_notes, re.MULTILINE)
        self.assertIsNotNone(match)
        codename = match.group(1)
        self.assertTrue(codename)
        self.assertTrue(
            decky_notes.startswith(
                f"## What's new in MAKO Decky v{manifest['version']}\n"
            )
        )
        expected_heading = f"### Codename: {codename}"
        self.assertIn(
            expected_heading,
            (
                REPOSITORY_ROOT / "engine/RELEASE_NOTES.md"
            ).read_text(encoding="utf-8"),
        )

    def test_renderer_logs_use_the_mako_renderer_prefix(self):
        source_files = [
            *(
                REPOSITORY_ROOT / "engine/mako-render/src"
            ).glob("*.cpp"),
            *(
                REPOSITORY_ROOT / "engine/mako-backend/src"
            ).rglob("*.cpp"),
        ]
        sources = "\n".join(
            path.read_text(encoding="utf-8") for path in source_files
        )
        self.assertIsNone(
            re.search(r'["\'][Mm]ako(?:-render| Renderer)?:\s', sources)
        )
        self.assertIsNone(re.search(r'["\']MAKO:\s', sources))
        self.assertIn(
            '"MAKO Renderer: render layer active; identity="', sources
        )


if __name__ == "__main__":
    unittest.main()
