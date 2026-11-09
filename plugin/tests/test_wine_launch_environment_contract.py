"""Keep independently installed launchers on the same Wine switch."""

from pathlib import Path
import unittest

from py_modules.mako_plugin.constants import WINE_CHILD_WINDOW_SWITCH_ENV


class WineLaunchEnvironmentContractTests(unittest.TestCase):
    def test_native_launcher_preserves_the_decky_switch_identity(self):
        launcher = (Path(__file__).resolve().parents[2] / "engine/scripts/mako-launch").read_text()
        self.assertIn(f"export {WINE_CHILD_WINDOW_SWITCH_ENV}=1", launcher)
        self.assertIn("${" + WINE_CHILD_WINDOW_SWITCH_ENV + "+set}", launcher)


if __name__ == "__main__":
    unittest.main()
