"""Licensing and dependency contracts for MAKO's bundled shader catalog."""

from pathlib import Path
import unittest


PLUGIN_ROOT = Path(__file__).resolve().parents[1]
SHADER_ROOT = PLUGIN_ROOT / "py_modules" / "mako_plugin" / "vkbasalt_shaders"

SWEETFX_SHADERS = (
    "Vibrance.fx",
    "Curves.fx",
    "Technicolor.fx",
    "Sepia.fx",
    "Monochrome.fx",
    "Vignette.fx",
    "FakeHDR.fx",
    "Technicolor2.fx",
    "DPX.fx",
    "FilmGrain.fx",
    "Cartoon.fx",
    "Nostalgia.fx",
    "ChromaticAberration.fx",
)


class ShaderAssetTests(unittest.TestCase):
    def test_third_party_sources_have_pinned_provenance_and_licenses(self):
        provenance = (SHADER_ROOT / "SOURCE.md").read_text(encoding="utf-8")
        self.assertIn("407c11562950195c1b45461fbb59f4bd6bbe7ba4", provenance)
        self.assertIn("4fee10cdac28f0a6d4fa5ddd778faf0016ab7b91", provenance)

        sweetfx_license = (SHADER_ROOT / "LICENSE-SweetFX").read_text(
            encoding="utf-8"
        )
        self.assertIn("The MIT License (MIT)", sweetfx_license)
        self.assertIn("Copyright (c) 2014 CeeJayDK", sweetfx_license)

        colourfulness = (SHADER_ROOT / "Colourfulness.fx").read_text(
            encoding="utf-8"
        )
        colourfulness_license = (
            SHADER_ROOT / "LICENSE-Colourfulness"
        ).read_text(encoding="utf-8")
        for notice in (
            "Copyright (c) 2016-2018, bacondither",
            "Redistribution and use in source and binary forms",
            "THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS''",
        ):
            self.assertIn(notice, colourfulness)
            self.assertIn(notice, colourfulness_license)

        reshade_header = (SHADER_ROOT / "ReShade.fxh").read_text(
            encoding="utf-8"
        )
        self.assertIn("SPDX-License-Identifier: CC0-1.0", reshade_header)

    def test_catalog_has_no_unbundled_ui_or_texture_dependency(self):
        shader_paths = sorted(SHADER_ROOT.glob("*.fx"))
        self.assertTrue(shader_paths)
        for shader_path in shader_paths:
            source = shader_path.read_text(encoding="utf-8")
            with self.subTest(shader=shader_path.name):
                self.assertNotIn('include "ReShadeUI.fxh"', source)
                self.assertNotIn("__UNIFORM_", source)
                self.assertNotIn("reshadeTexturePath", source)

    def test_expected_sources_and_mako_owned_presets_are_present(self):
        for filename in SWEETFX_SHADERS:
            with self.subTest(shader=filename):
                self.assertTrue((SHADER_ROOT / filename).is_file())

        for filename in ("BleachBypass.fx", "Noir.fx"):
            source = (SHADER_ROOT / filename).read_text(encoding="utf-8")
            self.assertIn("SPDX-License-Identifier: GPL-3.0-or-later", source)


if __name__ == "__main__":
    unittest.main()
