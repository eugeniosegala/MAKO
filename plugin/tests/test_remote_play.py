"""Portable native Remote Play install, restoration, launch and session tests."""
import asyncio
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest
from unittest.mock import AsyncMock, Mock, patch

from tests.test_game_profiles import _Logger
from py_modules.mako_plugin.config_schema import ConfigurationManager
from py_modules.mako_plugin.configuration import ConfigurationService
from py_modules.mako_plugin.plugin import Plugin
from py_modules.mako_plugin.remote_play import RemotePlayService, REMOTE_PLAY_PROFILE
from py_modules.mako_plugin.remote_play_launch import launch, LaunchPaths

ELF = b"\x7fELF\x02\x01" + b"\0" * 12 + b"\x3e\0" + b"synthetic client"


class RemotePlayTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.home = Path(temporary.name)
        with patch("py_modules.mako_plugin.base_service.resolve_user_home", return_value=self.home):
            self.service = RemotePlayService(_Logger())
            self.configuration = ConfigurationService(_Logger())
        self.service.proc_root = self.home / "proc"
        self.service.proc_root.mkdir()
        self.service.client.parent.mkdir(parents=True)
        self.service.client.write_bytes(ELF)
        self.service.client.chmod(0o750)
        self.configuration._save_profile_data(ConfigurationManager.parse_toml_content_multi_profile(
            'version=2\n[[profile]]\nname="mako"\n',
        ))
        self.service.mako_script_path.parent.mkdir(parents=True)
        self.service.mako_script_path.write_text("#!/bin/sh\n")
        self.service.mako_script_path.chmod(0o755)

    def test_idempotent_install_and_restore_preserve_original_permissions(self):
        self.service.install()
        self.assertTrue(self.service.get_status()["installed"])
        self.assertEqual(ELF, self.service.backup.read_bytes())
        entry = self.service.client.read_bytes()
        self.service.install()
        self.assertEqual(entry, self.service.client.read_bytes())
        self.service.remove()
        self.assertEqual(ELF, self.service.client.read_bytes())
        self.assertEqual(0o750, self.service.client.stat().st_mode & 0o777)
        self.assertFalse(self.service.state.exists())
        self.assertFalse(self.service.backup.exists())

    def test_steam_update_is_never_overwritten_and_override_can_be_reinstalled(self):
        self.service.install()
        updated = ELF + b"Steam update"
        self.service.client.write_bytes(updated)
        status = self.service.get_status()
        self.assertTrue(status["managed"])
        self.assertTrue(status["conflict"])
        self.service.remove()
        self.assertEqual(updated, self.service.client.read_bytes())
        self.service.install()
        self.assertEqual(updated, self.service.backup.read_bytes())

    def test_corrupt_backup_state_and_wrapper_block_restoration(self):
        for target in ("backup", "client", "state"):
            with self.subTest(target=target):
                self.service.install()
                path = getattr(self.service, target)
                path.write_bytes(path.read_bytes() + b"tampered")
                entry = self.service.client.read_bytes()
                with self.assertRaises((ValueError, OSError)):
                    self.service.remove()
                self.assertEqual(entry, self.service.client.read_bytes())
                for path in (self.service.backup, self.service.checksum, self.service.state):
                    path.unlink(missing_ok=True)
                self.service.client.write_bytes(ELF)

    def test_existing_symlink_files_and_lock_are_rejected(self):
        for name in ("backup", "checksum", "state"):
            path = getattr(self.service, name)
            path.symlink_to(self.home / "missing")
            with self.assertRaises(ValueError):
                self.service.install()
            self.assertEqual(ELF, self.service.client.read_bytes())
            path.unlink()
        self.service.lock_path.unlink()
        self.service.lock_path.symlink_to(self.home / "missing")
        with self.assertRaises(OSError):
            self.service.install()

    def test_experimental_manifest_and_unmanaged_original_are_not_adopted(self):
        experiment = self.home / ".local/share/vulkan/implicit_layer.d/VkLayer_MAKO_native_remote_play.json"
        experiment.parent.mkdir(parents=True)
        experiment.write_text("{}")
        with self.assertRaisesRegex(ValueError, "experimental"):
            self.service.install()
        experiment.unlink()
        for entry in (b"#!/bin/sh\n", ELF[:18] + b"\xb7\0"):
            self.service.client.write_bytes(entry)
            with self.assertRaisesRegex(ValueError, "Native x86_64"):
                self.service.install()
            self.assertEqual(entry, self.service.client.read_bytes())
        self.service.client.write_bytes(ELF)
        self.service.backup.write_bytes(ELF)
        with self.assertRaisesRegex(ValueError, "Existing"):
            self.service.install()
        self.assertEqual(ELF, self.service.client.read_bytes())

    def test_running_original_deleted_inode_and_python_wrapper_block_mutation(self):
        process = self.service.proc_root / "123"
        process.mkdir()
        for executable, arguments in (
            (str(self.service.client), b""),
            (str(self.service.client) + " (deleted)", b""),
            ("/usr/bin/python3", os.fsencode("python3\0" + str(self.service.client) + "\0")),
        ):
            with self.subTest(executable=executable):
                (process / "exe").unlink(missing_ok=True)
                (process / "exe").symlink_to(executable)
                (process / "cmdline").write_bytes(arguments)
                self.assertEqual([123], self.service.running_pids())
                with self.assertRaisesRegex(ValueError, "Close native"):
                    self.service.install()

    def test_shared_launch_lease_blocks_mutation_promptly(self):
        with self.service.lock_path.open("w") as lock:
            fcntl.flock(lock, fcntl.LOCK_SH)
            with self.assertRaisesRegex(ValueError, "in progress"):
                self.service.install()
        self.assertEqual(ELF, self.service.client.read_bytes())

    def test_launch_lease_survives_runner_exec_until_child_exit(self):
        with self.configuration._configuration_write_lock:
            self.configuration.ensure_remote_play_profile(REMOTE_PLAY_PROFILE)
        ready = self.home / "ready"
        self.service.mako_script_path.write_text(
            "#!/bin/sh\n" + f"touch '{ready}'\nexec /usr/bin/sleep 30\n"
        )
        self.service.install()
        process = subprocess.Popen([str(self.service.client)], stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 3
            while not ready.exists() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(ready.exists(), process.poll())
            self.assertEqual([], self.service.running_pids())
            with self.assertRaisesRegex(ValueError, "in progress"):
                self.service.remove()
        finally:
            process.terminate()
            process.communicate(timeout=3)
        self.service.remove()
        self.assertEqual(ELF, self.service.client.read_bytes())

    def test_late_restore_failure_rolls_back_original_and_sidecars(self):
        self.service.install()
        paths = [self.service.client, self.service.backup, self.service.checksum, self.service.state]
        before = {path: path.read_bytes() for path in paths}
        unlink = Path.unlink
        def fail_state(path, *arguments, **kwargs):
            if path == self.service.state:
                raise OSError("late state removal failure")
            return unlink(path, *arguments, **kwargs)
        with patch.object(Path, "unlink", fail_state):
            with self.assertRaises(OSError):
                self.service.remove()
        self.assertEqual(before, {path: path.read_bytes() for path in paths})

    def test_late_install_failure_rolls_back_all_managed_files(self):
        from py_modules.mako_plugin import remote_play
        write = remote_play.write_managed_text_atomically
        def fail_entry(path, *arguments):
            if path == self.service.client:
                raise OSError("late replacement failure")
            return write(path, *arguments)
        with patch.object(remote_play, "write_managed_text_atomically", side_effect=fail_entry):
            with self.assertRaises(OSError):
                self.service.install()
        self.assertEqual(ELF, self.service.client.read_bytes())
        self.assertFalse(self.service.backup.exists())
        self.assertFalse(self.service.state.exists())

    def test_digest_rejects_symlinks_and_changed_owner_and_copy_stays_private(self):
        link = self.home / "client-link"
        link.symlink_to(self.service.client)
        with self.assertRaises(OSError):
            self.service._digest(link)
        with self.assertRaisesRegex(ValueError, "identity changed"):
            self.service._digest(self.service.client, self.home.stat().st_uid + 1)
        from py_modules.mako_plugin import remote_play
        copy = remote_play.copy_managed_file_atomically
        with patch.object(remote_play, "copy_managed_file_atomically", wraps=copy) as copier:
            self.service.install()
        self.assertEqual(0o600, copier.call_args.args[2])
        self.assertEqual(0o750, self.service.backup.stat().st_mode & 0o777)

    def test_active_indicator_requires_exact_client_pid_and_active_fg(self):
        self.service.install()
        process = self.service.proc_root / "123"
        process.mkdir()
        (process / "exe").symlink_to(self.service.backup)
        context = {"pid": 123, "phase": "active", "frame_generation_active": True}
        self.assertTrue(self.service.get_status([context])["frame_generation_active"])
        for change in ({"pid": 124}, {"phase": "preparing"}, {"frame_generation_active": False}):
            self.assertFalse(self.service.get_status([{**context, **change}])["frame_generation_active"])

    def test_plugin_transaction_rolls_back_profile_and_preserves_existing_power_sets(self):
        plugin = Plugin.__new__(Plugin)
        plugin.remote_play_service = self.service
        plugin.configuration_service = self.configuration
        before = self.configuration.config_file_path.read_bytes()
        with patch.object(self.service, "install_locked", side_effect=OSError("fail")):
            with self.assertRaises(OSError):
                plugin._install_remote_play_override()
        self.assertEqual(before, self.configuration.config_file_path.read_bytes())
        self.assertFalse(self.configuration.wrapper_profile_settings_path.exists())
        plugin._install_remote_play_override()
        remote = self.configuration.get_profile_config(REMOTE_PLAY_PROFILE, "shared")
        self.assertTrue(remote["success"], remote)
        self.assertEqual(2, remote["config"]["multiplier"])
        self.assertEqual(30, remote["config"]["base_fps_cap"])
        self.assertTrue(remote["config"]["frame_generation_provisioned"])
        self.assertFalse(remote["separate_power_modes"])
        self.configuration.set_profile_power_modes(REMOTE_PLAY_PROFILE, True)
        self.configuration.update_profile_config_fields(REMOTE_PLAY_PROFILE, {"target_fps": 144}, "docked")
        self.service.remove()
        plugin._install_remote_play_override()
        self.assertEqual(144, self.configuration.get_profile_config(REMOTE_PLAY_PROFILE, "docked")["config"]["target_fps"])

    def test_profile_preparation_does_not_rewrite_unrelated_shader_files(self):
        shader = self.configuration.vkbasalt_global_config_path
        shader.parent.mkdir(parents=True, exist_ok=True)
        content = "# advanced user settings\neffects=Custom\nCustom=/tmp/user.fx\n"
        shader.write_text(content)
        with patch.object(self.configuration, "_write_vkbasalt_profile_configs") as merge:
            with self.configuration._configuration_write_lock:
                self.configuration.ensure_remote_play_profile(REMOTE_PLAY_PROFILE)
            merge.assert_not_called()
        self.assertEqual(content, shader.read_text())

    def test_renderer_uninstall_is_blocked_until_safe_steam_restoration(self):
        plugin = Plugin.__new__(Plugin)
        plugin.remote_play_service = self.service
        plugin.installation_service = Mock()
        plugin.installation_service._error_response.side_effect = lambda _type, error: {"success": False, "error": error}
        plugin._stop_flatpak_vrr_monitor = AsyncMock()
        self.service.install()
        self.service.backup.write_bytes(ELF + b"corrupt")
        async def inline(function, *arguments):
            return function(*arguments)
        with patch("py_modules.mako_plugin.plugin.asyncio.to_thread", inline):
            result = asyncio.run(plugin.uninstall_mako())
            self.assertFalse(result["success"])
            plugin.installation_service.uninstall.assert_not_called()
            plugin._stop_flatpak_vrr_monitor.assert_not_called()
            asyncio.run(plugin._uninstall())
            plugin.installation_service.cleanup_on_uninstall.assert_not_called()

    def test_remote_session_selection_keeps_power_source_and_resets_on_exit(self):
        with self.configuration._configuration_write_lock:
            self.configuration.ensure_remote_play_profile(REMOTE_PLAY_PROFILE)
        with patch("py_modules.mako_plugin.configuration.detect_power_source", return_value="docked"):
            result = self.configuration.sync_current_profile("", REMOTE_PLAY_PROFILE)
            self.assertTrue(result["success"], result)
            self.assertTrue(result["remote_play_running"])
            self.assertTrue(result["game_running"])
            self.assertEqual("docked", result["power_source"])
            self.assertEqual(REMOTE_PLAY_PROFILE, result["profile_name"])
            self.assertFalse(self.configuration.sync_current_profile("", REMOTE_PLAY_PROFILE)["changed"])
            exited = self.configuration.sync_current_profile("")
            self.assertFalse(exited["remote_play_running"])
            self.assertEqual("mako", exited["profile_name"])


class RemoteLaunchTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        self.paths = LaunchPaths(**{name: root / name for name in LaunchPaths.__annotations__})
        self.paths["original"].write_bytes(ELF)
        self.paths["original"].chmod(0o755)
        self.paths["checksum"].write_text(hashlib.sha256(ELF).hexdigest())
        self.paths["state"].write_text(json.dumps({"version": 1, "enabled": True, "profile": REMOTE_PLAY_PROFILE}))
        self.paths["config"].write_text('version=2\n[[profile]]\nname="Remote-Play"\n')
        self.paths["runner"].write_text("#!/bin/sh\n")
        self.paths["runner"].chmod(0o755)
        self.calls = []
        self.args = ["--appid", "123", "session value", "$(literal)"]

    def run_launch(self, execute=None):
        return launch(self.paths, self.args, execute or (lambda *args: self.calls.append(args)), {
            "GALLIUM_DRIVER": "zink", "ENABLE_MAKO": "1", "MAKO_PROFILE": "other",
            "ENABLE_VKBASALT": "1", "SteamAppId": "123",
            "ENABLE_MAKO_SPATIAL_SCALING": "1",
            "VK_INSTANCE_LAYERS": "VK_LAYER_MAKO_spatial_scaling:VK_LAYER_MAKO_render:other",
        })

    def test_delegates_power_and_model_free_profiles_to_canonical_runner(self):
        for extra in ("", '[profile.handheld]\nframe_generation_enabled=false\n[profile.docked]\nframe_generation_enabled=true\n', 'scaling_enabled=true\nframe_generation_provisioned=false\n'):
            with self.subTest(extra=extra):
                self.paths["config"].write_text('version=2\n[[profile]]\nname="Remote-Play"\n' + extra)
                self.calls.clear()
                self.assertEqual(0, self.run_launch())
                executable, arguments, environment = self.calls[0]
                self.assertEqual(str(self.paths["runner"]), executable)
                self.assertEqual(self.args, arguments[-len(self.args):])
                self.assertEqual(REMOTE_PLAY_PROFILE, environment["MAKO_PROFILE"])
                self.assertEqual("123", environment["SteamAppId"])
                self.assertNotIn("GALLIUM_DRIVER", environment)
                self.assertNotIn("ENABLE_VKBASALT", environment)
                self.assertNotIn("ENABLE_MAKO_SPATIAL_SCALING", environment)
                self.assertNotIn("DISABLE_MAKO_SPATIAL_SCALING", environment)
                self.assertEqual("other", environment["VK_INSTANCE_LAYERS"])

    def test_missing_invalid_state_config_or_runner_falls_back(self):
        for key, content in (("state", "{}"), ("state", "{"), ("config", "version=999"), ("config", "version=2"), ("config", "{")):
            with self.subTest(key=key, content=content):
                before = self.paths[key].read_text()
                self.paths[key].write_text(content)
                self.calls.clear()
                self.assertEqual(0, self.run_launch())
                self.assertEqual(str(self.paths["original"]), self.calls[0][0])
                self.assertEqual("1", self.calls[0][2]["DISABLE_MAKO"])
                self.assertEqual("1", self.calls[0][2]["DISABLE_MAKO_SPATIAL_SCALING"])
                self.assertNotIn("ENABLE_MAKO_SPATIAL_SCALING", self.calls[0][2])
                self.paths[key].write_text(before)
        self.paths["runner"].unlink()
        self.calls.clear()
        self.assertEqual(0, self.run_launch())
        self.assertEqual(str(self.paths["original"]), self.calls[0][0])

    def test_missing_launch_lease_uses_only_verified_native_passthrough(self):
        self.assertEqual(0, launch(self.paths, self.args, lambda *args: self.calls.append(args),
                                  {"ENABLE_MAKO_SPATIAL_SCALING": "1"}, allow_override=False))
        self.assertEqual(str(self.paths["original"]), self.calls[0][0])
        self.assertEqual("1", self.calls[0][2]["DISABLE_MAKO"])
        self.assertEqual("1", self.calls[0][2]["DISABLE_MAKO_SPATIAL_SCALING"])

    def test_corrupt_original_never_executes_and_exec_failure_retries_once(self):
        self.paths["original"].write_bytes(ELF + b"bad")
        self.assertEqual(126, self.run_launch())
        self.assertEqual([], self.calls)
        self.paths["original"].write_bytes(ELF)
        def fail_runner(*args):
            self.calls.append(args)
            if len(self.calls) == 1:
                raise OSError("runner cannot exec")
        self.assertEqual(0, self.run_launch(fail_runner))
        self.assertEqual(2, len(self.calls))
        self.assertEqual(str(self.paths["original"]), self.calls[-1][0])
