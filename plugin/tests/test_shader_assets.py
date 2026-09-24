"""Licensing and dependency contracts for MAKO's bundled shader catalog."""

from pathlib import Path
import re
import unittest

import shared_config
from py_modules.mako_plugin import profile_storage
from py_modules.mako_plugin.constants import VKBASALT_SHADER_ASSET_FILENAMES


PLUGIN_ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = PLUGIN_ROOT.parent
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
    def test_installer_catalog_matches_bundled_shader_assets(self):
        self.assertEqual(
            tuple(sorted(VKBASALT_SHADER_ASSET_FILENAMES)),
            tuple(sorted(path.name for path in SHADER_ROOT.iterdir()
                         if path.is_file())),
        )

    def test_qt_shader_adapter_matches_decky_contract(self):
        source = (
            REPOSITORY_ROOT
            / "engine/mako-common/src/configuration/vkbasalt.cpp"
        ).read_text(encoding="utf-8")
        header = (
            REPOSITORY_ROOT
            / "engine/mako-common/include/mako-common/configuration/vkbasalt.hpp"
        ).read_text(encoding="utf-8")

        def array_values(name: str) -> tuple[str, ...]:
            match = re.search(
                rf"constexpr std::array<std::string_view, \d+> {name}\{{(.*?)\n\}};",
                source,
                re.DOTALL,
            )
            self.assertIsNotNone(match, f"missing Qt {name} catalog")
            return tuple(re.findall(r'"([^"]+)"', match.group(1)))

        self.assertEqual(
            array_values("SHARPENING"),
            shared_config.VKBASALT_SHARPENING_VALUES,
        )
        self.assertEqual(
            array_values("ANTIALIASING"),
            shared_config.VKBASALT_ANTIALIASING_VALUES,
        )
        self.assertEqual(
            array_values("SHADERS"),
            shared_config.VKBASALT_SHADER_VALUES,
        )

        effect_block = re.search(
            r"SHADER_EFFECTS\{(.*?)\n\};", source, re.DOTALL
        )
        self.assertIsNotNone(effect_block)
        qt_effects = dict(re.findall(
            r'\{"([^"]+)", "([^"]+)"\}', effect_block.group(1)
        ))
        self.assertEqual(qt_effects, profile_storage._VKBASALT_SHADER_EFFECTS)
        for field in (
            'std::string sharpening{"cas"}',
            'float sharpness{0.5F}',
            'float dls_denoise{0.2F}',
            'std::string antialiasing{"none"}',
            'std::string shader{"none"}',
            "vkBasaltStrengthMinimum = 0.0F",
            "vkBasaltStrengthMaximum = 1.0F",
        ):
            self.assertIn(field, header)

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
