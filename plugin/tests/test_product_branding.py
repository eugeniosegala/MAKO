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
            "MAKO brings Vulkan-powered spatial scaling and Lossless Scaling "
            "frame generation to Steam Deck, Steam Machine, and SteamOS gaming.",
        )

    def test_decky_component_identity_is_mako_decky(self):
        frontend = (PLUGIN_DIR / "src/index.tsx").read_text(encoding="utf-8")
        self.assertIn('name: "MAKO Decky"', frontend)
        self.assertIn(">MAKO Decky</div>", frontend)

        content = (
            PLUGIN_DIR / "src/components/ContentNotices.tsx"
        ).read_text(encoding="utf-8")
        self.assertRegex(content, r'MAKO Decky(?:\{" "\})?\s*<code>')
        self.assertIn('{" · MAKO Renderer "}', content)

        configuration = (
            PLUGIN_DIR / "src/components/ConfigurationSectionGroups.tsx"
        ).read_text(encoding="utf-8")
        self.assertIn("Advanced Rendering Settings", configuration)

        lifecycle = (
            PLUGIN_DIR / "py_modules/mako_plugin/plugin.py"
        ).read_text(encoding="utf-8")
        self.assertNotIn("mako plugin", lifecycle.lower())
        self.assertIn('decky.logger.info("MAKO Decky loaded")', lifecycle)

        decky_client = (
            PLUGIN_DIR / "scripts/decky-loader-client.mjs"
        ).read_text(encoding="utf-8")
        self.assertIn(
            'DEFAULT_PLUGIN_NAME = "MAKO - Frame Generation"', decky_client
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

    def test_release_version_and_codename_are_shared_by_component_notes(self):
        decky_notes = (
            PLUGIN_DIR / "RELEASE_NOTES.md"
        ).read_text(encoding="utf-8")
        engine_notes = (
            REPOSITORY_ROOT / "engine/RELEASE_NOTES.md"
        ).read_text(encoding="utf-8")
        codename_match = re.search(
            r"^### Release codename:\s*(.+?)\s*$",
            decky_notes,
            re.MULTILINE,
        )
        self.assertIsNotNone(codename_match)
        codename = codename_match.group(1)
        self.assertTrue(codename)
        decky_version = re.match(
            r"^## What's new in MAKO Decky v(.+?)$",
            decky_notes,
            re.MULTILINE,
        )
        engine_version = re.match(
            r"^## What's new in MAKO Renderer v(.+?)$",
            engine_notes,
            re.MULTILINE,
        )
        self.assertIsNotNone(decky_version)
        self.assertIsNotNone(engine_version)
        self.assertEqual(decky_version.group(1), engine_version.group(1))
        expected_heading = f"### Release codename: {codename}"
        self.assertIn(expected_heading, engine_notes)

    def test_component_publishers_share_the_release_note_structure(self):
        decky_publisher = (
            PLUGIN_DIR / "scripts/publish-package.sh"
        ).read_text(encoding="utf-8")
        renderer_publisher = (
            REPOSITORY_ROOT / "engine/scripts/publish-package.sh"
        ).read_text(encoding="utf-8")

        shared_headings = (
            "## 🎮 In-game considerations",
            "## Installation",
            "## Known limitation",
            "## Before you play",
        )
        for heading in shared_headings:
            self.assertIn(heading, decky_publisher)
            self.assertIn(heading, renderer_publisher)

        self.assertIn(
            "First-time Heroic or EmuDeck setup",
            decky_publisher,
        )
        self.assertIn(
            "## Updating an existing MAKO Renderer installation",
            renderer_publisher,
        )
        self.assertIn(
            "## MAKO Renderer release assets \\`$version\\`",
            renderer_publisher,
        )
        self.assertNotIn("## MAKO Renderer Linux build", renderer_publisher)
        self.assertNotIn("## Included files", renderer_publisher)

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
