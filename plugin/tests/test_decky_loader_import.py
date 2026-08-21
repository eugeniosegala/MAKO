"""Import MAKO Decky through the same path boundary as Decky Loader."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


PLUGIN_ROOT = Path(__file__).resolve().parents[1]


class DeckyLoaderImportTests(unittest.TestCase):
    def test_main_loads_when_only_py_modules_is_on_python_path(self):
        script = """
import importlib.util
from pathlib import Path
import sys
from types import SimpleNamespace

plugin_root = Path(sys.argv[1]).resolve()
assert plugin_root not in [Path(entry).resolve() for entry in sys.path if entry]
sys.modules["decky"] = SimpleNamespace(logger=SimpleNamespace())
spec = importlib.util.spec_from_file_location("mako_decky_main", plugin_root / "main.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
assert module.Plugin.__name__ == "Plugin"
assert plugin_root in [Path(entry).resolve() for entry in sys.path if entry]
"""
        environment = os.environ.copy()
        environment["PYTHONPATH"] = str(PLUGIN_ROOT / "py_modules")
        environment["PYTHONNOUSERSITE"] = "1"
        with tempfile.TemporaryDirectory() as service_directory:
            result = subprocess.run(
                [sys.executable, "-c", script, str(PLUGIN_ROOT)],
                cwd=service_directory,
                env=environment,
                capture_output=True,
                text=True,
                check=False,
            )

        self.assertEqual(
            result.returncode,
            0,
            msg=f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}",
        )


if __name__ == "__main__":
    unittest.main()
