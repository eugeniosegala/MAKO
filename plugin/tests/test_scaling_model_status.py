"""Selected LS1 preflight contracts; fixtures contain no licensed resources."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.modules.setdefault("decky", SimpleNamespace(logger=Mock()))

from py_modules.mako_plugin.constants import CLI_DIR, CLI_FILENAME
from py_modules.mako_plugin.dll_detection import DllDetectionService


class ScalingModelStatusTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.home = Path(self.temporary.name)
        self.service = DllDetectionService(Mock())
        self.service.user_home = self.home
        self.dll = self.home / "library with spaces" / "Lossless.dll"
        self.dll.parent.mkdir()
        self.dll.write_text("synthetic input")
        self.cli = self.home / CLI_DIR / CLI_FILENAME
        self.cli.parent.mkdir(parents=True)
        self.cli.write_text("synthetic inspector identity")
        runner_patch = patch("py_modules.mako_plugin.dll_detection.subprocess.run")
        self.runner = runner_patch.start()
        self.addCleanup(runner_patch.stop)
        self.runner.return_value = self.output(True)

    @staticmethod
    def output(compatible):
        return SimpleNamespace(
            returncode=0 if compatible else 1,
            stdout=json.dumps({"schema_version": 1, "compatible": compatible}),
        )

    def check(self, method="ls1", sharpness=0.8):
        return self.service.check_scaling_model(str(self.dll), method, sharpness)

    def test_selected_model_uses_runtime_inspector_and_does_not_modify_input(self):
        original = self.dll.read_bytes()
        for method in ("ls1", "ls1-performance"):
            for sharpness in (0, 0.25, 0.5, 0.75, 1):
                self.assertEqual(self.check(method, sharpness), {"compatible": True, "reason": None})
                args, kwargs = self.runner.call_args
                self.assertEqual(args[0], [str(self.cli), "inspect-dll", "--dll", str(self.dll),
                                          "--ls1", method, "--sharpness", str(sharpness)])
                self.assertEqual(kwargs["timeout"], 15)
        self.assertEqual(self.dll.read_bytes(), original)
        self.assertFalse((self.home / ".config").exists())

    def test_missing_explicit_dll_does_not_silently_inspect_a_different_installation(self):
        self.dll.unlink()
        with patch.object(self.service, "check_lossless_scaling_dll") as discovery:
            self.assertFalse(self.check()["compatible"])
            discovery.assert_not_called()
        self.runner.assert_not_called()

    def test_empty_path_uses_canonical_discovery(self):
        with patch.object(self.service, "check_lossless_scaling_dll", return_value={
            "path": str(self.dll), "error": None,
        }):
            self.assertTrue(self.service.check_scaling_model("", "ls1", 0.8)["compatible"])

    def test_unavailable_selected_model_is_distinct_from_unknown_inspection(self):
        self.runner.return_value = self.output(False)
        self.assertEqual(self.check(), {"compatible": False, "reason": "ls1-unavailable"})
        self.assertEqual(self.check(), {"compatible": False, "reason": "ls1-unavailable"})
        self.runner.assert_called_once()

    def test_old_cli_crash_bad_response_and_timeout_are_unknown(self):
        for output in (
            SimpleNamespace(returncode=1, stdout="old CLI usage"),
            SimpleNamespace(returncode=-11, stdout=self.output(False).stdout),
            SimpleNamespace(returncode=0, stdout='{"schema_version":1,"compatible":"yes"}'),
            SimpleNamespace(returncode=0, stdout='{"schema_version":2,"compatible":true}'),
            SimpleNamespace(returncode=0, stdout='{"schema_version":true,"compatible":true}'),
            SimpleNamespace(returncode=0, stdout='[]'),
        ):
            self.runner.return_value = output
            self.assertIsNone(self.check()["compatible"])
        self.runner.side_effect = subprocess.TimeoutExpired("inspector", 15)
        self.assertIsNone(self.check()["compatible"])
        self.runner.side_effect = None
        self.cli.unlink()
        self.assertIsNone(self.check()["compatible"])

    def test_replacement_invalidates_cache_even_with_same_size_and_mtime(self):
        self.assertTrue(self.check()["compatible"])
        response = self.check()
        response["compatible"] = False
        self.assertTrue(self.check()["compatible"])
        self.runner.assert_called_once()
        previous = self.dll.stat()
        replacement = self.dll.with_suffix(".replacement")
        replacement.write_text("changed input!!")
        replacement.replace(self.dll)
        os.utime(self.dll, ns=(previous.st_atime_ns, previous.st_mtime_ns))
        self.assertEqual(self.dll.stat().st_size, previous.st_size)
        self.runner.return_value = self.output(False)
        self.assertFalse(self.check()["compatible"])
        self.assertEqual(self.runner.call_count, 2)
        self.dll.unlink()
        self.assertFalse(self.check()["compatible"])
        self.dll.write_text("restored input!")
        self.runner.return_value = self.output(True)
        self.assertTrue(self.check()["compatible"])

    def test_inspector_replacement_and_expiry_retry_model_availability(self):
        self.runner.return_value = self.output(False)
        self.assertFalse(self.check()["compatible"])
        self.cli.write_text("new inspector identity")
        self.runner.return_value = self.output(True)
        self.assertTrue(self.check()["compatible"])
        self.runner.return_value = self.output(False)
        self.service._model_cache["ls1"].checked_at -= 301
        self.assertFalse(self.check()["compatible"])

    def test_dll_changes_during_inspection_do_not_publish_stale_result(self):
        def replace_during_probe(*args, **kwargs):
            self.dll.write_text("changed while probing")
            return self.output(True)
        self.runner.side_effect = replace_during_probe
        self.assertIsNone(self.check()["compatible"])

    def test_inflight_probe_does_not_queue_more_translations(self):
        with self.service._model_lock:
            self.assertIsNone(self.check()["compatible"])
        self.runner.assert_not_called()

    def test_invalid_inputs_cannot_become_cli_options(self):
        for method, sharpness in (("--help", 0.8), ("ls1", float("nan")),
                                  ("ls1", -1), ("ls1", 2), ("ls1", True)):
            self.assertIsNone(self.check(method, sharpness)["compatible"])
        self.runner.assert_not_called()

    def test_discovery_does_not_accept_directories_named_lossless_dll(self):
        self.dll.unlink()
        self.dll.mkdir()
        with patch.dict(os.environ, {"MAKO_DLL_PATH": str(self.dll)}):
            self.assertIsNone(self.service._check_env_dll_path())

    def test_lsfg_checks_saved_precision_and_does_not_evict_ls1_cache(self):
        self.assertTrue(self.check()["compatible"])
        self.runner.return_value = self.output(False)
        check_fg = self.service.check_frame_generation_model
        self.assertEqual(check_fg(str(self.dll), True),
                         {"compatible": False, "reason": "lsfg-unavailable"})
        self.assertEqual(self.runner.call_args.args[0][-1:], ["--lsfg"])
        self.assertTrue(self.check()["compatible"])
        self.assertFalse(check_fg(str(self.dll), True)["compatible"])
        self.assertEqual(self.runner.call_count, 2)
        check_fg(str(self.dll), False)
        self.assertEqual(self.runner.call_args.args[0][-2:], ["--lsfg", "--no-fp16"])
        self.assertEqual(len(self.service._model_cache), 2)
        self.runner.return_value = self.output(True)
        self.dll.write_text("changed model input")
        self.assertTrue(check_fg(str(self.dll), False)["compatible"])
        self.assertTrue(self.check()["compatible"])
        self.assertEqual(self.runner.call_count, 5)

    def test_lsfg_missing_dll_is_distinct_from_unknown_inspector(self):
        check_fg = self.service.check_frame_generation_model
        self.runner.return_value = SimpleNamespace(returncode=1, stdout="old CLI")
        self.assertIsNone(check_fg(str(self.dll), True)["compatible"])
        self.runner.side_effect = subprocess.TimeoutExpired("inspect-dll", 15)
        self.assertIsNone(check_fg(str(self.dll), True)["compatible"])
        self.dll.unlink()
        self.assertEqual(check_fg(str(self.dll), True),
                         {"compatible": False, "reason": "dll-unavailable"})
        for value in ("false", 1, None):
            self.assertIsNone(check_fg(str(self.dll), value)["compatible"])


if __name__ == "__main__":
    unittest.main()
