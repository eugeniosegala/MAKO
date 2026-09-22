"""Cross-component contracts for MAKO's UI languages and shared terminology."""

import json
from pathlib import Path
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
DECKY_I18N_ROOT = REPOSITORY_ROOT / "plugin/defaults/i18n"
RENDERER_TRANSLATIONS = (
    REPOSITORY_ROOT / "engine/mako-ui/rsc/i18n/translations.json"
)


class LocalizationLanguageContractTests(unittest.TestCase):
    def test_decky_and_renderer_advertise_the_same_ordered_languages(self):
        decky_metadata = json.loads(
            (DECKY_I18N_ROOT / "language_metadata.json").read_text(
                encoding="utf-8"
            )
        )
        renderer = json.loads(
            RENDERER_TRANSLATIONS.read_text(encoding="utf-8")
        )
        renderer_metadata = {
            language["code"]: {"name": language["name"]}
            for language in renderer["languages"]
        }

        self.assertEqual(decky_metadata, renderer_metadata)
        self.assertEqual(
            list(renderer["catalogs"]),
            list(renderer_metadata),
        )

    def test_decky_sources_cover_every_non_english_language(self):
        decky_metadata = json.loads(
            (DECKY_I18N_ROOT / "language_metadata.json").read_text(
                encoding="utf-8"
            )
        )
        source_languages = {
            path.stem
            for path in DECKY_I18N_ROOT.glob("*.json")
            if path.name not in {
                "template.json",
                "language_metadata.json",
                "steam_language_map.json",
            }
        }

        self.assertEqual(
            source_languages,
            set(decky_metadata) - {"en"},
        )

    def test_renderer_catalogs_are_complete_and_string_only(self):
        renderer = json.loads(
            RENDERER_TRANSLATIONS.read_text(encoding="utf-8")
        )
        english_keys = list(renderer["catalogs"]["en"])

        for language, catalog in renderer["catalogs"].items():
            with self.subTest(language=language):
                self.assertEqual(list(catalog), english_keys)
                self.assertTrue(
                    all(
                        isinstance(value, str) and value
                        for value in catalog.values()
                    )
                )

    def test_steam_aliases_resolve_only_to_advertised_languages(self):
        decky_metadata = json.loads(
            (DECKY_I18N_ROOT / "language_metadata.json").read_text(
                encoding="utf-8"
            )
        )
        steam_aliases = json.loads(
            (DECKY_I18N_ROOT / "steam_language_map.json").read_text(
                encoding="utf-8"
            )
        )

        self.assertLessEqual(set(steam_aliases.values()), set(decky_metadata))
        self.assertEqual(steam_aliases["brazilian"], "pt-BR")
        self.assertEqual(steam_aliases["portuguese"], "pt-PT")

    def test_japanese_shared_control_names_agree_across_independent_catalogs(self):
        decky = json.loads(
            (DECKY_I18N_ROOT / "ja.json").read_text(encoding="utf-8")
        )
        renderer = json.loads(
            RENDERER_TRANSLATIONS.read_text(encoding="utf-8")
        )["catalogs"]["ja"]
        shared_controls = {
            "ADAPTIVE_TITLE": "adaptiveFrameGen",
            "FRACTIONAL_ADAPTIVE_PRESET": "fractionalAdaptive",
            "ADAPTIVE_MAX_MULTIPLIER": "maxAdaptiveMultiplier",
            "FIXED_MULTIPLIER": "multiplier",
            "CONFIG_BASE_FPS_CAP": "baseFpsCap",
            "SCALING_FACTOR": "scalingFactor",
            "SCALING_SUPERSAMPLING": "scalingSupersampling",
        }

        for decky_key, renderer_key in shared_controls.items():
            with self.subTest(control=decky_key):
                self.assertEqual(decky[decky_key], renderer[renderer_key])


if __name__ == "__main__":
    unittest.main()
