"""Deterministic tests for the installed diagnostics preset helper."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest


PROJECT_DIR = Path(__file__).resolve().parent.parent
REPOSITORY_ROOT = PROJECT_DIR.parent
HELPER = REPOSITORY_ROOT / "scripts" / "mako-diagnostics"


class _Logger:
    def __getattr__(self, _name):
        return lambda *_args, **_kwargs: None


sys.modules.setdefault("decky", SimpleNamespace(logger=_Logger()))

from py_modules.mako_plugin.installation import InstallationService  # noqa: E402

FIXTURE = """\
[Vulkan Loader] Loading VK_LAYER_MAKO_frame_generation
[Vulkan Loader] Loading VK_LAYER_MAKO_render
[Gamescope WSI] HDR output available
MAKO Renderer: render layer active; identity=VK_LAYER_MAKO_render; build=1.0.0; fingerprint=abc123.dirty.12345678
MAKO Renderer: present diagnostics: operation=process-identity pid=4242 executable=game.exe wine_executable=game.exe process_name=GameThread profile=mako identification=fallback build=1.0.0 fingerprint=abc123.dirty.12345678
MAKO Renderer: swapchain colour pipeline: format=64; color-space=1000104008; mode=hdr10-pq; source=gamescope-normalized; transport=packed-hdr10-32-bit; frame-generation=supported
MAKO Renderer: HDR10 transport: mode=packed-10-bit; nominal_bytes=16384000; nominal_bytes_saved=16384000; application_device_supported=1; backend_device_supported=1
MAKO Renderer: Gamescope application HDR feedback stabilized: active=1; contexts_pending_recreation=1
MAKO Renderer: present diagnostics: operation=swapchain-context-create context=1 pid=4242 swapchain=1234 width=1280 height=800 images=3 format=64 color_space=1000104008 present_mode=2 ordered_transport=1 active_contexts=1 inserted=1 layer_forced_recreation=live-profile-resources-one-shot
MAKO Renderer: present diagnostics: operation=runtime-transition-pending context=1 state_revision=2 reason=profile-resources spatial_scaling_pending=1 frame_generation_backend_pending=1 flow_scale_pending=1 lighter_model_pending=1 generated_capacity_pending=1 available_generated_capacity=1 requested_generated_capacity=3 process_restart_required=0 action=signal-out-of-date-after-successful-present
MAKO Renderer: process-static configuration changes remain pending until game restart; contexts=1
MAKO Renderer: present diagnostics: operation=runtime-transition-pending context=1 state_revision=3 reason=process-static-profile gpu_selection_pending=1 pacing_pending=0 frame_generation_interop_pending=0 ultra_performance_pending=0 action=wait-for-process-restart
MAKO Renderer: live profile resource change requested a game-owned swapchain recreation after one successful lower present
MAKO Renderer: present diagnostics: operation=runtime-transition-recreation-requested context=1 state_revision=2 reason=profile-resources lower_present_result=0 signal=VK_ERROR_OUT_OF_DATE_KHR delivery=one-shot-after-semaphore-consumption
MAKO Renderer: present diagnostics: operation=runtime-state-applied context=2 state_revision=2 adaptive=1 target_fps=110 effective_flow_scale=0.75 lighter_model=1 generated_frame_capacity=3 hdr=1
MAKO Renderer: spatial scaling surface virtualized: source=854x532; presentation=1280x800
MAKO Renderer: spatial scaling swapchain policy: requested=854x532; surface_current=1280x800; selected_source=854x532; selected_presentation=1280x800; format=44; format_supported=1; shape_supported=1; queue_presentation_support=supported; queue_commands_supported=1; variable_feedback_suppressed=0; active=1
MAKO Renderer: spatial scaling active: source=854x532; presentation=1280x800; factor=1.5; requested_method=ls1; active_method=ls1; sharpness=0.5; ls1_model_variant=2; ls1_translator=/runtime/libvkd3d-shader.so.1; working_format=37; pipeline=pre-frame-generation
MAKO Renderer: LS1 scaling unavailable; using MAKO fallback: test translator unavailable
MAKO Renderer: spatial scaling variable-surface feedback guard: surface=5678; previous_source=500x500; previous_presentation=750x750; requested=750x750; action=native-feedback-guard
MAKO Renderer: standalone spatial scaling: frame-generation backend and interop resources were not created
MAKO Renderer: multi-swapchain spatial-scaling present rejected before semaphore consumption; batch scaling is not supported
MAKO Renderer: present diagnostics: operation=adaptive-ramp context=1 old_limit=0 new_limit=1
MAKO Renderer: present diagnostics: operation=adaptive-plan context=1 base_fps=60 target_fps=90 generated=1 max_generated=1 stable_cadence=0 target_clock=1 target_budget_credit_outputs=0.25 target_deferred_budget_output=1 target_phase_error_ms=-2.1 source_interval_samples=60 source_interval_mean_ms=16.6 source_interval_stddev_ms=1.2 source_interval_p95_ms=18.5 source_interval_p99_ms=20.5 generated_count_changes=29 requested_interval_samples=90 requested_interval_mean_ms=11.1 requested_interval_stddev_ms=1.3 requested_interval_p95_ms=13.5 requested_interval_p99_ms=13.5 target_phase_error_samples=60 target_phase_error_rms_ms=2.2 target_phase_error_max_ms=5.5
MAKO Renderer: present diagnostics: operation=fixed-plan context=2 base_fps=61.2 multiplier=2 generated_per_real=1 observed_output_fps=122.4 generated_presented=61 generated_skipped=0 configured_adaptive_target_fps=110 target_applies=0
MAKO Renderer: present diagnostics: operation=acquire-generated-image context=1 duration_ms=50 result=VK_TIMEOUT
MAKO Renderer: present diagnostics: operation=skip-generated-frames context=1 reason=initial-timeout
MAKO Renderer: present diagnostics: operation=ordered-acquire-policy context=1 configured_timeout_ms=50 slow_threshold_ms=25 severe_threshold_ms=50 budget_scope=application-present first_slow_action=zero-wait-protection guard_miss_action=native-relief-history-warmup recovery_probe_timeout_ms=8.33 recovery_probe_timeout_max_ms=25 recovery_probe_failure=backoff post_probe_policy=native-only stabilization_ms=2000
MAKO Renderer: present diagnostics: operation=ordered-acquire-budget-exhausted context=1 phase=acquire acquire_total_ms=50 acquire_max_ms=30 budget_ms=50 requested_generated=2 admitted_generated=2 presented_generated=1 action=stop-acquiring
MAKO Renderer: present diagnostics: operation=ordered-acquire-guard context=1 phase=zero-wait-guard acquire_total_ms=31 acquire_max_ms=31 slow_threshold_ms=25 consecutive_slow_frames=1 action=zero-wait-protection
MAKO Renderer: present diagnostics: operation=ordered-acquire-guard-bypass context=1 phase=native-relief acquire_timeout_ns=0 history_warmup_frames=3 action=native-present-warm-history-then-normal-retry
MAKO Renderer: present diagnostics: operation=ordered-acquire-quarantine context=1 phase=native-drain reason=timeout retry_ms=250 action=native-drain
MAKO Renderer: present diagnostics: operation=ordered-acquire-retry context=1 phase=history-warmup bypassed_frames=12 action=warm-history-before-probe
MAKO Renderer: present diagnostics: operation=ordered-acquire-recovered context=1 phase=native-stabilization recovery_ms=400 action=native-only
MAKO Renderer: present diagnostics: operation=ordered-acquire-stabilized context=1 phase=history-warmup recovery_ms=2400 terminal_reason=stabilization-deadline-elapsed action=warm-history-before-normal-policy
MAKO Renderer: present diagnostics: operation=pipeline-busy-bypass context=1 consecutive_frames=1 total_bypassed_frames=16 duration_ms=0 planned=1 history_action=preserved action=native-present
MAKO Renderer: present diagnostics: operation=pipeline-busy-recovered context=1 bypassed_frames=1 total_recoveries=16 duration_ms=8 history_warmup_requested=0
MAKO Renderer: present diagnostics: operation=render-fence-budget-missed context=1 planned=1 action=native-present
MAKO Renderer: present diagnostics: operation=present-breakdown context=1 total_ms=44.4 render_fence_ms=0.1 schedule_ms=1.0 source_copy_ms=1.2 acquire_ms=40 generated_submit_ms=0.4 generated_present_ms=0.5 original_present_ms=0.6 unattributed_ms=0.6 frame=42 sequence=84
mako: present diagnostics: operation=resume-generated-frames context=1
mako: present diagnostics: operation=generated-image-recovered context=1
mako: present diagnostics: operation=swapchain-recreation-suppressed context=1
MAKO Renderer: frame-generation initialization failed; native presentation retained: test failure
unrelated application output
"""


class DiagnosticsHelperTests(unittest.TestCase):
    def _run(self, *arguments, environment=None):
        return subprocess.run(
            ["bash", str(HELPER), *arguments],
            check=False,
            capture_output=True,
            text=True,
            env=environment,
        )

    def _fixture_path(self, directory: Path) -> Path:
        path = directory / "diagnostics.log"
        path.write_text(FIXTURE, encoding="utf-8")
        return path

    def test_hdr_preset_excludes_adaptive_policy(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = self._fixture_path(Path(temporary_directory))
            result = self._run("--log", str(path), "hdr")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("mode=hdr10-pq", result.stdout)
        self.assertIn("HDR10 transport: mode=packed-10-bit", result.stdout)
        self.assertIn("nominal_bytes_saved=16384000", result.stdout)
        self.assertIn("initialization failed", result.stdout)
        self.assertIn("render layer active", result.stdout)
        self.assertNotIn("adaptive-ramp", result.stdout)

    def test_multiple_presets_combine_recovery_and_adaptive(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = self._fixture_path(Path(temporary_directory))
            result = self._run("--log", str(path), "adaptive", "recovery")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("adaptive-ramp", result.stdout)
        self.assertIn("runtime-state-applied", result.stdout)
        self.assertIn("skip-generated-frames", result.stdout)
        self.assertIn("ordered-acquire-quarantine", result.stdout)
        self.assertIn("ordered-acquire-policy", result.stdout)
        self.assertIn("ordered-acquire-budget-exhausted", result.stdout)
        self.assertIn("ordered-acquire-guard", result.stdout)
        self.assertIn("ordered-acquire-guard-bypass", result.stdout)
        self.assertIn("ordered-acquire-retry", result.stdout)
        self.assertIn("ordered-acquire-recovered", result.stdout)
        self.assertIn("ordered-acquire-stabilized", result.stdout)
        self.assertIn("pipeline-busy-bypass", result.stdout)
        self.assertIn("render-fence-budget-missed", result.stdout)
        self.assertIn("resume-generated-frames", result.stdout)
        self.assertIn("generated-image-recovered", result.stdout)
        self.assertIn("swapchain-recreation-suppressed", result.stdout)
        self.assertIn("render layer active", result.stdout)
        self.assertNotIn("mode=hdr10-pq", result.stdout)
        self.assertNotIn("HDR10 transport", result.stdout)

    def test_scaling_preset_keeps_extent_activation_and_lifecycle_policy(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = self._fixture_path(Path(temporary_directory))
            result = self._run("--log", str(path), "scaling")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("spatial scaling surface virtualized", result.stdout)
        self.assertIn("selected_source=854x532", result.stdout)
        self.assertIn("selected_presentation=1280x800", result.stdout)
        self.assertIn("queue_presentation_support=supported", result.stdout)
        self.assertIn("queue_commands_supported=1", result.stdout)
        self.assertIn("spatial scaling active", result.stdout)
        self.assertIn("requested_method=ls1", result.stdout)
        self.assertIn("active_method=ls1", result.stdout)
        self.assertIn("ls1_model_variant=2", result.stdout)
        self.assertIn("pipeline=pre-frame-generation", result.stdout)
        self.assertIn("LS1 scaling unavailable; using MAKO fallback", result.stdout)
        self.assertIn("action=native-feedback-guard", result.stdout)
        self.assertIn("standalone spatial scaling", result.stdout)
        self.assertIn("multi-swapchain spatial-scaling present rejected", result.stdout)
        self.assertIn("runtime-transition-pending", result.stdout)
        self.assertIn("swapchain colour pipeline", result.stdout)
        self.assertNotIn("adaptive-ramp", result.stdout)

    def test_adaptive_preset_keeps_fractional_pacing_contract(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = self._fixture_path(Path(temporary_directory))
            result = self._run("--log", str(path), "adaptive")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("operation=adaptive-plan", result.stdout)

        producer = (
            REPOSITORY_ROOT / "engine/mako-render/src/present_diagnostics.cpp"
        ).read_text(encoding="utf-8")
        for field in (
            "stable_cadence=",
            "target_clock=",
            "target_budget_credit_outputs=",
            "target_deferred_budget_output=",
            "target_phase_error_ms=",
            "source_interval_samples=",
            "source_interval_mean_ms=",
            "source_interval_stddev_ms=",
            "source_interval_p95_ms=",
            "source_interval_p99_ms=",
            "generated_count_changes=",
            "requested_interval_samples=",
            "requested_interval_mean_ms=",
            "requested_interval_stddev_ms=",
            "requested_interval_p95_ms=",
            "requested_interval_p99_ms=",
            "target_phase_error_samples=",
            "target_phase_error_rms_ms=",
            "target_phase_error_max_ms=",
        ):
            with self.subTest(field=field):
                self.assertIn(field, result.stdout)
                self.assertIn(field, producer)

    def test_current_recovery_operations_have_renderer_producers(self):
        renderer_source = "\n".join(
            path.read_text(encoding="utf-8")
            for path in (
                REPOSITORY_ROOT / "engine/mako-render/src"
            ).glob("*.cpp")
        )
        current_operations = {
            "skip-generated-frames",
            "generated-delivery-miss",
            "generated-admission-pressure",
            "generated-admission-recovered",
            "ordered-acquire-quarantine",
            "ordered-acquire-policy",
            "ordered-acquire-budget-exhausted",
            "ordered-acquire-guard",
            "ordered-acquire-guard-bypass",
            "ordered-acquire-retry",
            "ordered-acquire-recovered",
            "ordered-acquire-stabilized",
            "pipeline-busy-bypass",
            "pipeline-busy-recovered",
            "render-fence-budget-missed",
            "history-warmup",
            "runtime-transition-pending",
            "runtime-transition-applied",
        }
        for operation in current_operations:
            with self.subTest(operation=operation):
                self.assertIn(f"operation={operation}", renderer_source)

    def test_every_preset_keeps_the_authoritative_build_marker(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = self._fixture_path(Path(temporary_directory))
            for preset in (
                "hdr", "config", "scaling", "adaptive", "recovery", "performance",
                "lifecycle", "startup", "layers", "errors", "all",
            ):
                with self.subTest(preset=preset):
                    result = self._run("--log", str(path), preset)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn("render layer active", result.stdout)
                    self.assertIn("fingerprint=abc123.dirty.12345678", result.stdout)
                    self.assertIn("operation=process-identity", result.stdout)
                    self.assertIn("operation=swapchain-context-create", result.stdout)
                    self.assertIn("spatial scaling active", result.stdout)
                    self.assertIn("action=native-feedback-guard", result.stdout)

    def test_config_preset_correlates_requested_and_applied_state(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = self._fixture_path(Path(temporary_directory))
            result = self._run("--log", str(path), "config")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("runtime-transition-pending", result.stdout)
        self.assertIn(
            "signal-out-of-date-after-successful-present", result.stdout
        )
        self.assertIn("runtime-state-applied", result.stdout)
        self.assertIn("generated_capacity_pending=1", result.stdout)
        self.assertIn("available_generated_capacity=1", result.stdout)
        self.assertIn("requested_generated_capacity=3", result.stdout)
        self.assertIn("generated_frame_capacity=3", result.stdout)
        self.assertIn("process-static configuration changes", result.stdout)
        self.assertIn("gpu_selection_pending=1", result.stdout)
        self.assertIn("frame_generation_interop_pending=0", result.stdout)
        self.assertIn("action=wait-for-process-restart", result.stdout)
        self.assertIn("runtime-transition-recreation-requested", result.stdout)
        self.assertIn("one-shot-after-semaphore-consumption", result.stdout)
        self.assertNotIn("adaptive-ramp", result.stdout)

    def test_performance_preset_includes_fixed_multiplier_telemetry(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = self._fixture_path(Path(temporary_directory))
            result = self._run("--log", str(path), "performance")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("operation=fixed-plan", result.stdout)
        self.assertIn("generated_skipped=0", result.stdout)
        self.assertIn("ordered-acquire-quarantine", result.stdout)
        self.assertIn("ordered-acquire-recovered", result.stdout)
        self.assertIn("pipeline-busy-bypass", result.stdout)
        self.assertIn("pipeline-busy-recovered", result.stdout)
        self.assertIn("operation=present-breakdown", result.stdout)
        self.assertNotIn("adaptive-ramp", result.stdout)

    def test_startup_includes_loader_gamescope_context_and_hdr(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = self._fixture_path(Path(temporary_directory))
            result = self._run("--log", str(path), "startup")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Vulkan Loader", result.stdout)
        self.assertIn("Gamescope WSI", result.stdout)
        self.assertIn("swapchain-context-create", result.stdout)
        self.assertIn("mode=hdr10-pq", result.stdout)
        self.assertIn("HDR10 transport: mode=packed-10-bit", result.stdout)

    def test_private_log_is_selected_from_home(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            home = Path(temporary_directory)
            path = home / ".config/mako-render/present-diagnostics.log"
            path.parent.mkdir(parents=True)
            path.write_text(FIXTURE, encoding="utf-8")
            environment = {**os.environ, "HOME": str(home)}
            environment.pop("MAKO_PRESENT_DIAGNOSTICS_LOG", None)
            result = self._run("errors", environment=environment)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("VK_TIMEOUT", result.stdout)
        self.assertIn("initialization failed", result.stdout)
        self.assertIn(str(path), result.stderr)
        self.assertIn("session: latest", result.stderr)

    def test_retained_sessions_can_be_selected_or_combined(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            base_path = Path(temporary_directory) / "present-diagnostics.log"
            previous_path = Path(f"{base_path}.1")
            oldest_path = Path(f"{base_path}.2")
            for path, label in (
                (base_path, "latest"),
                (previous_path, "previous"),
                (oldest_path, "oldest"),
            ):
                path.write_text(
                    f"MAKO Renderer: session-marker={label}\n",
                    encoding="utf-8",
                )

            latest = self._run("--log", str(base_path), "all")
            previous = self._run(
                "--log", str(base_path), "--session", "previous", "all"
            )
            oldest = self._run(
                "--log", str(base_path), "--session", "oldest", "all"
            )
            previous_two = self._run(
                "--log",
                str(base_path),
                "--session",
                "previous-two",
                "--lines",
                "1",
                "all",
            )
            combined = self._run(
                "--log",
                str(base_path),
                "--session",
                "all",
                "--lines",
                "1",
                "all",
            )

        self.assertEqual(latest.returncode, 0, latest.stderr)
        self.assertIn("session-marker=latest", latest.stdout)
        self.assertNotIn("session-marker=previous", latest.stdout)
        self.assertEqual(previous.returncode, 0, previous.stderr)
        self.assertIn("session-marker=previous", previous.stdout)
        self.assertEqual(oldest.returncode, 0, oldest.stderr)
        self.assertIn("session-marker=oldest", oldest.stdout)
        self.assertEqual(previous_two.returncode, 0, previous_two.stderr)
        self.assertLess(
            previous_two.stdout.index("session-marker=oldest"),
            previous_two.stdout.index("session-marker=previous"),
        )
        self.assertNotIn("session-marker=latest", previous_two.stdout)
        self.assertEqual(combined.returncode, 0, combined.stderr)
        self.assertLess(
            combined.stdout.index("session-marker=oldest"),
            combined.stdout.index("session-marker=previous"),
        )
        self.assertLess(
            combined.stdout.index("session-marker=previous"),
            combined.stdout.index("session-marker=latest"),
        )
        self.assertEqual(combined.stderr.count("retained sessions: 3"), 3)

    def test_missing_retained_session_fails_clearly(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = self._fixture_path(Path(temporary_directory))
            result = self._run(
                "--log", str(path), "--session", "previous", "all"
            )

        self.assertEqual(result.returncode, 1)
        self.assertIn("not found for session 'previous'", result.stderr)
        self.assertIn(f"{path}.1", result.stderr)

    def test_newest_native_or_flatpak_steam_log_is_selected(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            home = Path(temporary_directory)
            native_log = home / ".steam/steam/logs/console-linux.txt"
            flatpak_log = (
                home
                / ".var/app/com.valvesoftware.Steam/.steam/steam/logs/console-linux.txt"
            )
            native_log.parent.mkdir(parents=True)
            flatpak_log.parent.mkdir(parents=True)
            native_log.write_text(FIXTURE, encoding="utf-8")
            flatpak_log.write_text(FIXTURE, encoding="utf-8")
            os.utime(native_log, (1, 1))
            os.utime(flatpak_log, (2, 2))
            environment = {**os.environ, "HOME": str(home)}
            environment.pop("MAKO_PRESENT_DIAGNOSTICS_LOG", None)
            result = self._run("errors", environment=environment)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(str(flatpak_log), result.stderr)

    def test_invalid_preset_and_line_count_fail_clearly(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = self._fixture_path(Path(temporary_directory))
            invalid_preset = self._run("--log", str(path), "not-a-preset")
            invalid_lines = self._run(
                "--log", str(path), "--lines", "0", "all"
            )
            invalid_session = self._run(
                "--log", str(path), "--session", "fourth", "all"
            )
        self.assertEqual(invalid_preset.returncode, 2)
        self.assertIn("Unknown preset", invalid_preset.stderr)
        self.assertEqual(invalid_lines.returncode, 2)
        self.assertIn("positive integer", invalid_lines.stderr)
        self.assertEqual(invalid_session.returncode, 2)
        self.assertIn(
            "latest, previous, oldest, previous-two, or all",
            invalid_session.stderr,
        )

    def test_plugin_migration_installs_and_refreshes_executable_helper(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            destination = Path(temporary_directory) / "mako-diagnostics"
            service = InstallationService(logger=_Logger())
            service.diagnostics_script_path = destination

            self.assertTrue(service.migrate_diagnostics_helper_if_needed())
            self.assertEqual(destination.read_bytes(), HELPER.read_bytes())
            self.assertTrue(destination.stat().st_mode & 0o111)
            self.assertFalse(service.migrate_diagnostics_helper_if_needed())

            destination.write_text("outdated\n", encoding="utf-8")
            self.assertTrue(service.migrate_diagnostics_helper_if_needed())
            self.assertEqual(destination.read_bytes(), HELPER.read_bytes())

    def test_backend_development_deployment_refreshes_helper(self):
        deployment_script = (
            PROJECT_DIR / "scripts" / "deploy-dev.sh"
        ).read_text(encoding="utf-8")

        self.assertIn(
            'copy_file "$repository_root/scripts/mako-diagnostics"',
            deployment_script,
        )
        self.assertIn(
            '"$plugin_dir/bin/mako-diagnostics"',
            deployment_script,
        )


if __name__ == "__main__":
    unittest.main()
