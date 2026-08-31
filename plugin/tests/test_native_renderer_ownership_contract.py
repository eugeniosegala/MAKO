"""Cross-component contracts for the shared native Renderer lifecycle."""

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
    ACTIVE_RENDERER_OWNER_STANDALONE,
    ACTIVE_RENDERER_STATE_SCHEMA_VERSION,
    DECKY_NATIVE_RENDERER_RELATIVE_PATHS,
)


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
STANDALONE_INSTALLER = REPOSITORY_ROOT / "engine/scripts/mako-installer"


class NativeRendererOwnershipContractTests(unittest.TestCase):
    def test_standalone_uninstaller_covers_every_decky_renderer_file(self):
        installer = STANDALONE_INSTALLER.read_text(encoding="utf-8")
        match = re.search(
            r"^decky_renderer_relative_paths=\(\n(?P<body>.*?)^\)\n",
            installer,
            flags=re.MULTILINE | re.DOTALL,
        )
        self.assertIsNotNone(match, "standalone Decky cleanup list is missing")
        shell_paths = tuple(re.findall(
            r'^\s+"([^"]+)"$',
            match.group("body"),
            flags=re.MULTILINE,
        ))

        self.assertEqual(shell_paths, DECKY_NATIVE_RENDERER_RELATIVE_PATHS)

    def test_standalone_active_state_matches_decky_reader(self):
        installer = STANDALONE_INSTALLER.read_text(encoding="utf-8")

        self.assertIn(
            f'"schema_version": {ACTIVE_RENDERER_STATE_SCHEMA_VERSION}',
            installer,
        )
        self.assertIn(
            f'"owner": "{ACTIVE_RENDERER_OWNER_STANDALONE}"',
            installer,
        )


if __name__ == "__main__":
    unittest.main()
