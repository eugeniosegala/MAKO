"""Validation and aggregation tests for Renderer runtime status records."""

import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest


class _Logger:
    def __getattr__(self, _name):
        return lambda *_args, **_kwargs: None


sys.modules.setdefault("decky", SimpleNamespace(logger=_Logger()))

from py_modules.mako_plugin.runtime_state import RuntimeStateService  # noqa: E402


def _profile(name: str, multiplier: int) -> dict[str, object]:
    return {
        "name": name,
        "gpu": None,
        "multiplier": multiplier,
        "frame_generation_enabled": True,
        "scaling_enabled": True,
        "scaling_method": "ls1",
        "scaling_factor": 1.5,
        "scaling_sharpness": 0.5,
        "frame_generation_refresh_threshold": 0,
        "base_fps_cap": 45,
        "adaptive": False,
        "adaptive_auto_base_fps_cap": False,
        "target_fps": 90,
        "adaptive_max_multiplier": multiplier,
        "adaptive_stable_cadence": True,
        "dynamic_cadence_recovery": False,
        "dynamic_cadence_probe_interval_seconds": 2.0,
        "ultra_performance": False,
        "flow_scale": 0.85,
        "effective_flow_scale": 0.85,
        "performance_mode": False,
        "effective_performance_mode": False,
        "pacing": "none",
        "required_generated_capacity": max(1, multiplier - 1),
    }


class RuntimeStateTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.runtime_directory = Path(self.temporary_directory.name)
        self.service = RuntimeStateService(_Logger())
        self.service.runtime_state_dir = self.runtime_directory
        process_start_ticks = self.service._process_start_ticks(os.getpid())
        assert process_start_ticks is not None
        self.process_start_ticks = process_start_ticks

    def tearDown(self):
        self.temporary_directory.cleanup()

    def _record(
            self, *, role: str = "frame-generation",
            phase: str = "active", context: int = 1,
            updated: int = 100, profile: str = "game-profile",
            process_start_ticks: int | None = None,
            error: str | None = None) -> dict[str, object]:
        return {
            "schema_version": 2,
            "pid": os.getpid(),
            "process_start_ticks": (
                self.process_start_ticks
                if process_start_ticks is None
                else process_start_ticks
            ),
            "context": context,
            "role": role,
            "updated_unix_ms": updated,
            "state_revision": 9,
            "phase": phase,
            "reason": "configuration-update",
            "pending": {
                "frame_generation_private": phase != "active",
                "spatial_private": False,
                "swapchain_recreation": False,
                "process_restart": False,
            },
            "applied_generated_capacity": 1,
            "spatial_scaling": {
                "active": role == "spatial-scaling",
                "activation_supported": True,
                "inactive_reason": None,
            },
            "requested": _profile(profile, 5),
            "applied": _profile(profile, 2),
            "error": error,
        }

    def _write(self, name: str, record: dict[str, object]) -> Path:
        path = self.runtime_directory / name
        path.write_text(json.dumps(record), encoding="utf-8")
        return path

    def test_valid_contexts_are_filtered_sorted_and_aggregated(self):
        self._write(
            "fg.json",
            self._record(phase="draining", context=4, updated=200),
        )
        self._write(
            "scaling.json",
            self._record(
                role="spatial-scaling", context=5, updated=100,
            ),
        )

        status = self.service.get_status("game-profile")

        self.assertTrue(status["success"])
        self.assertEqual(status["phase"], "draining")
        self.assertEqual(
            [context["context"] for context in status["contexts"]],
            [4, 5],
        )
        self.assertEqual(
            status["contexts"][0]["requested"]["multiplier"], 5
        )
        self.assertFalse(
            status["contexts"][0]["spatial_scaling"]["active"]
        )
        self.assertEqual(
            self.service.get_status("another-profile")["phase"], "inactive"
        )

    def test_failed_context_has_aggregate_priority(self):
        self._write(
            "restart.json",
            self._record(phase="process-restart", context=6),
        )
        self._write(
            "failed.json",
            self._record(
                phase="failed", context=7, error="replacement failed"
            ),
        )

        status = self.service.get_status()

        self.assertEqual(status["phase"], "failed")
        self.assertEqual(status["contexts"][0]["error"], "replacement failed")

    def test_stale_pid_identity_is_removed(self):
        path = self._write(
            "stale.json",
            self._record(process_start_ticks=self.process_start_ticks + 1),
        )

        status = self.service.get_status()

        self.assertEqual(status["contexts"], [])
        self.assertFalse(path.exists())

    def test_malformed_oversized_and_linked_records_are_ignored(self):
        (self.runtime_directory / "malformed.json").write_text(
            "{not-json", encoding="utf-8"
        )
        (self.runtime_directory / "oversized.json").write_bytes(
            b"x" * (64 * 1024 + 1)
        )
        target = self.runtime_directory / "target.txt"
        target.write_text(json.dumps(self._record()), encoding="utf-8")
        (self.runtime_directory / "linked.json").symlink_to(target)

        status = self.service.get_status()

        self.assertTrue(status["success"])
        self.assertEqual(status["contexts"], [])


if __name__ == "__main__":
    unittest.main()
