"""Cross-component contract for the Wine compatibility marker and its launcher."""

from pathlib import Path
import re
import sys
from types import SimpleNamespace
import unittest


class _Logger:
    def __getattr__(self, _name):
        return lambda *_args, **_kwargs: None


sys.modules.setdefault("decky", SimpleNamespace(logger=_Logger()))

from py_modules.mako_plugin.constants import (  # noqa: E402
    MAKO_PROFILE_ENV,
    WINE_CHILD_WINDOW_INEFFECTIVE_SUFFIX,
    WINE_CHILD_WINDOW_MARKER_SUFFIX,
    WINE_CHILD_WINDOW_SWITCH_ENV,
    WINE_COMPATIBILITY_DIRNAME,
    WINE_COMPATIBILITY_FIX_ENV,
)
from py_modules.mako_plugin.types import RuntimeWineCompatibilityFix  # noqa: E402


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RENDERER_SOURCE = REPOSITORY_ROOT / "engine/mako-render/src"


def _renderer_string(header: str, name: str) -> str:
    match = re.search(
        rf"{name}\s*=\s*\"([^\"]+)\"",
        header,
        flags=re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"{name} is missing from wine_compatibility.hpp")
    return match.group(1)


class WineCompatibilityContractTests(unittest.TestCase):
    def setUp(self):
        self.header = (RENDERER_SOURCE / "wine_compatibility.hpp").read_text(
            encoding="utf-8"
        )
        self.source = (RENDERER_SOURCE / "wine_compatibility.cpp").read_text(
            encoding="utf-8"
        )

    def test_marker_names_and_environment_match_the_renderer(self):
        self.assertEqual(
            _renderer_string(self.header, "wineChildWindowSwitchEnvironment"),
            WINE_CHILD_WINDOW_SWITCH_ENV,
        )
        self.assertEqual(
            _renderer_string(self.header, "wineCompatibilitySettingEnvironment"),
            WINE_COMPATIBILITY_FIX_ENV,
        )
        self.assertEqual(
            _renderer_string(self.header, "wineCompatibilityDirectory"),
            WINE_COMPATIBILITY_DIRNAME,
        )
        self.assertEqual(
            _renderer_string(self.header, "wineChildWindowMarkerSuffix"),
            WINE_CHILD_WINDOW_MARKER_SUFFIX,
        )
        self.assertEqual(
            _renderer_string(self.header, "wineChildWindowIneffectiveSuffix"),
            WINE_CHILD_WINDOW_INEFFECTIVE_SUFFIX,
        )
        # The Renderer promises a restart only when the launcher identified the
        # same profile through the variable the wrapper exports.
        self.assertIn(f'environmentValue("{MAKO_PROFILE_ENV}")', self.source)

    def test_marker_stem_matches_the_renderer_test_vector(self):
        tests = (REPOSITORY_ROOT / "engine/mako-render/tests/wine_compatibility_tests.cpp")
        self.assertIn('"Half-Life-2") == "48616c662d4c6966652d32"', tests.read_text(
            encoding="utf-8"
        ))
        self.assertEqual("Half-Life-2".encode().hex(), "48616c662d4c6966652d32")

    def test_runtime_states_match_the_renderer(self):
        renderer_states = set(re.findall(
            r'case WineChildWindowFix::\w+: return "([a-z-]+)";',
            self.header,
        ))
        self.assertEqual(renderer_states, set(RuntimeWineCompatibilityFix.__args__))
        api = (REPOSITORY_ROOT / "plugin/src/api/makoApi.ts").read_text(encoding="utf-8")
        union = re.search(
            r"export type RuntimeWineCompatibilityFix =(?P<body>[^;]+);",
            api,
        )
        self.assertIsNotNone(union)
        self.assertEqual(
            set(re.findall(r'"([a-z-]+)"', union.group("body"))),
            renderer_states,
        )


if __name__ == "__main__":
    unittest.main()
