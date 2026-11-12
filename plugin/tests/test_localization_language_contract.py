"""Cross-component contracts for MAKO's UI languages and shared terminology."""

import json
from pathlib import Path
import re
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
                for key, value in catalog.items():
                    self.assertEqual(
                        sorted(re.findall(r"\{[a-z][a-z0-9_]*\}", value)),
                        sorted(re.findall(r"\{[a-z][a-z0-9_]*\}", renderer["catalogs"]["en"][key])),
                        f"{language}.{key} changed named placeholders",
                    )

    @staticmethod
    def localized_catalogs():
        renderer = json.loads(RENDERER_TRANSLATIONS.read_text(encoding="utf-8"))["catalogs"]
        decky_english = json.loads((DECKY_I18N_ROOT / "template.json").read_text(encoding="utf-8"))
        for language, catalog in renderer.items():
            if language == "en":
                continue
            yield "Qt", language, catalog, renderer["en"]
            decky = json.loads((DECKY_I18N_ROOT / f"{language}.json").read_text(encoding="utf-8"))
            yield "Decky", language, decky, decky_english

    def test_localized_catalogs_do_not_fall_back_to_english_copy(self):
        # Brands, algorithm identifiers and units remain literal in every locale.
        invariant_names = {
            "CAS", "DLS", "FXAA", "SMAA", "DPX / Cineon",
            "Technicolor", "Technicolor 2", "MAKO Scaler", "MAKO Renderer",
            "MAKO + DeckyFG", "Lossless Scaling", "Gamescope VRR", "FPS", "FPS)",
        }
        # These words have the same spelling in English and the target language.
        same_spelling = {
            "pt-BR": {"Experimental", "Nostalgia", "LSFG Q"},
            "pt-PT": {"Experimental", "Nostalgia", "LSFG Q"},
            "es": {"Error", "Error:", "Experimental", "Factor", "Nostalgia", "Sepia"},
            "de": {"Details", "Installation", "Name", "Sepia", "Vignette", "Alpha", "Lossless Scaling (optional)", "LSFG Q"},
        }
        for component, language, catalog, english in self.localized_catalogs():
            allowed = invariant_names | same_spelling.get(language, set())
            for key, value in catalog.items():
                with self.subTest(component=component, language=language, key=key):
                    if value == english[key] and re.search(r"[A-Za-z]{3}", value):
                        self.assertIn(value.strip(), allowed, "untranslated English UI copy")

    def test_localized_catalogs_do_not_mix_scripts_or_english_feature_names(self):
        cyrillic = r"[\u0400-\u04ff]"
        east_asian = r"[\u3040-\u30ff\u4e00-\u9fff\uac00-\ud7af]"
        latin_locales = {"pt-BR", "pt-PT", "es", "de"}
        untranslated_features = (
            "LS1 Quality", "LS1 Performance", "HDR Look", "Ultra Performance",
            "Frame-gen", "Frame-Gen",
        )
        for component, language, catalog, _ in self.localized_catalogs():
            for key, value in catalog.items():
                with self.subTest(component=component, language=language, key=key):
                    if language in latin_locales:
                        self.assertNotRegex(value, cyrillic + "|" + east_asian)
                    elif language in {"ru", "uk"}:
                        self.assertNotRegex(value, east_asian)
                        # Distinguish the two Cyrillic alphabets as well.
                        foreign_letters = r"[ыъэЫЪЭ]" if language == "uk" else r"[іїєґІЇЄҐ]"
                        self.assertNotRegex(value, foreign_letters)
                    else:
                        self.assertNotRegex(value, cyrillic)
                        if language == "zh":
                            self.assertNotRegex(value, r"[\u3040-\u30ff\uac00-\ud7af]")
                        elif language == "ja":
                            self.assertNotRegex(value, r"[\uac00-\ud7af]")
                        elif language == "ko":
                            self.assertNotRegex(value, r"[\u3040-\u30ff]")
                    for feature in untranslated_features:
                        self.assertNotIn(feature, value)

    def test_new_shared_control_names_agree_in_every_language(self):
        renderer = json.loads(RENDERER_TRANSLATIONS.read_text(encoding="utf-8"))["catalogs"]
        controls = {
            "ADAPTIVE_TARGET_REFRESH_RATE": "adaptiveTargetRefreshRate",
            "ADAPTIVE_FALLBACK_TARGET_FPS": "fallbackTargetFps",
            "POWER_HANDHELD": "powerHandheld",
            "POWER_DOCKED": "powerDocked",
            "CONFIG_VKBASALT_CUSTOM_ADD": "addCustomShader",
            "CONFIG_VKBASALT_CUSTOM_REFRESH": "refreshCustomShaders",
            "CONFIG_VKBASALT_CUSTOM_DELETE": "deleteSelectedCustomShaders",
            "SHADER_EDITOR_RED": "shaderEditorRed",
            "SHADER_EDITOR_GREEN": "shaderEditorGreen",
            "SHADER_EDITOR_BLUE": "shaderEditorBlue",
            "SHADER_EDITOR_ALPHA": "shaderEditorAlpha",
            "SHADER_EDITOR_COLOR_PREVIEW": "shaderEditorColorPreview",
            "SHADER_EDITOR_COLOR_RANGE": "shaderEditorColorRange",
            "REMOTE_PLAY_TITLE": "remotePlay",
            "REMOTE_PLAY_INSTALL": "overrideRemotePlay",
            "REMOTE_PLAY_STATUS": "remotePlayStatus",
            "REMOTE_PLAY_ATTENTION": "remotePlayAttention",
            "SCALING_METHOD_LS1": "scalingMethodLs1",
            "SCALING_METHOD_LS1_PERFORMANCE": "scalingMethodLs1Performance",
        }
        for component, language, decky, _ in self.localized_catalogs():
            if component != "Decky":
                continue
            for decky_key, qt_key in controls.items():
                with self.subTest(language=language, control=decky_key):
                    self.assertEqual(decky[decky_key], renderer[language][qt_key])


    def test_qt_bundled_effects_use_localized_labels_matching_decky(self):
        qml = (REPOSITORY_ROOT / "engine/mako-ui/rsc/UI.qml").read_text(encoding="utf-8")
        effect_ids = json.loads(re.search(r"property var bundledEffectIds: (\[[^\n]+\])", qml)[1])
        label_binding = re.search(r"property var bundledEffectLabels: \[([^\n]+)\]", qml)[1]
        labels = [value.strip() for value in label_binding.split(",")]
        self.assertEqual(len(effect_ids), len(labels))
        self.assertEqual(labels[0], "t.off")
        for label in labels:
            self.assertRegex(label, r"^t\.[a-zA-Z]+[0-9]*$", "effect label bypasses localization")
        renderer = json.loads(RENDERER_TRANSLATIONS.read_text(encoding="utf-8"))["catalogs"]
        for language, catalog in renderer.items():
            source_name = "template" if language == "en" else language
            decky = json.loads((DECKY_I18N_ROOT / f"{source_name}.json").read_text(encoding="utf-8"))
            for effect_id, label in zip(effect_ids[1:], labels[1:]):
                with self.subTest(language=language, effect=effect_id):
                    self.assertEqual(catalog[label[2:]], decky["CONFIG_VKBASALT_SHADER_" + effect_id.upper()])

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
        self.assertEqual(steam_aliases["german"], "de")
        self.assertEqual(steam_aliases["russian"], "ru")

    def test_shared_mangohud_guidance_matches_in_every_language(self):
        renderer = json.loads(RENDERER_TRANSLATIONS.read_text(encoding="utf-8"))["catalogs"]
        shared = {
            "CONFIG_ENABLE_MANGOHUD": "enableMangoHud",
            "CONFIG_ENABLE_MANGOHUD_DESC": "enableMangoHudDesc",
            "CONFIG_EXTERNAL_TOOLS_TITLE": "externalTools",
        }
        for language, catalog in renderer.items():
            source = "template" if language == "en" else language
            decky = json.loads((DECKY_I18N_ROOT / f"{source}.json").read_text(encoding="utf-8"))
            for decky_key, qt_key in shared.items():
                with self.subTest(language=language, key=qt_key):
                    self.assertEqual(decky[decky_key], catalog[qt_key])

    def test_shared_control_names_agree_across_independent_catalogs(self):
        renderer = json.loads(
            RENDERER_TRANSLATIONS.read_text(encoding="utf-8")
        )["catalogs"]
        shared_controls = {
            "ADAPTIVE_TITLE": "adaptiveFrameGen",
            "FRACTIONAL_ADAPTIVE_PRESET": "fractionalAdaptive",
            "ADAPTIVE_MAX_MULTIPLIER": "maxAdaptiveMultiplier",
            "FIXED_MULTIPLIER": "multiplier",
            "CONFIG_BASE_FPS_CAP": "baseFpsCap",
            "SCALING_FACTOR": "scalingFactor",
            "SCALING_SUPERSAMPLING": "scalingSupersampling",
        }

        for language in ("ja", "de", "ru"):
            decky = json.loads(
                (DECKY_I18N_ROOT / f"{language}.json").read_text(encoding="utf-8")
            )
            for decky_key, renderer_key in shared_controls.items():
                with self.subTest(language=language, control=decky_key):
                    self.assertEqual(decky[decky_key], renderer[language][renderer_key])


if __name__ == "__main__":
    unittest.main()
