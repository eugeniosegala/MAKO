"""Portable native Remote Play install, restoration, launch and session tests."""
import asyncio
import fcntl
import hashlib
import json
import os
import shutil
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
from py_modules.mako_plugin.remote_play_launch import launch, LaunchPaths, MANAGED_LAYERS

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
        self.service.mako_script_path.write_text("#!/bin/sh\n# MAKO_LAUNCH_RENDERER_REQUIRED\n")
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

    def test_enabled_payload_upgrade_preserves_original_and_defers_during_stream(self):
        self.assertFalse(self.service.refresh_installed_payload())
        self.assertEqual(ELF, self.service.client.read_bytes())
        self.service.install()
        original = self.service.backup.read_bytes()
        payload = self.service._payload() + '\n# next payload revision\n'
        with patch.object(self.service, '_payload', return_value=payload):
            with patch.object(self.service, 'running_pids', return_value=[123]):
                with self.assertRaisesRegex(ValueError, 'Close native'):
                    self.service.refresh_installed_payload()
            self.assertTrue(self.service.refresh_installed_payload())
            self.assertFalse(self.service.refresh_installed_payload())
        self.assertEqual(original, self.service.backup.read_bytes())
        self.assertEqual(payload, self.service.client.read_text())
        self.service.remove()
        self.assertEqual(ELF, self.service.client.read_bytes())

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
        for target in ("backup", "client"):
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

    def test_lost_or_corrupt_state_can_restore_only_the_known_wrapper(self):
        for content in (None, '{broken', '{}'):
            with self.subTest(content=content):
                self.service.install()
                if content is None:
                    self.service.state.unlink()
                else:
                    self.service.state.write_text(content)
                self.assertTrue(self.service.get_status()['conflict'])
                self.service.remove()
                self.assertEqual(ELF, self.service.client.read_bytes())
        self.service.install()
        self.service.state.unlink()
        self.service.client.write_bytes(self.service.client.read_bytes() + b'unknown edits')
        with self.assertRaises((OSError, ValueError)):
            self.service.remove()
        self.assertTrue(self.service.backup.exists())

    def test_interrupted_restore_can_retire_bookkeeping_without_replacing_steam(self):
        self.service.install()
        os.replace(self.service.backup, self.service.client)
        inode = self.service.client.stat().st_ino
        self.service.remove()
        self.assertEqual(inode, self.service.client.stat().st_ino)
        self.assertEqual(ELF, self.service.client.read_bytes())
        self.assertFalse(self.service.checksum.exists())
        self.assertFalse(self.service.state.exists())

    def test_interrupted_checksum_cleanup_preserves_a_later_steam_update(self):
        self.service.install()
        os.replace(self.service.backup, self.service.client)
        self.service.checksum.unlink()
        updated = ELF + b'updated after native restoration'
        self.service.client.write_bytes(updated)
        inode = self.service.client.stat().st_ino
        self.service.remove()
        self.assertEqual(inode, self.service.client.stat().st_ino)
        self.assertEqual(updated, self.service.client.read_bytes())
        self.assertFalse(self.service.state.exists())

    def test_privileged_backup_is_not_restored_or_used_for_payload_upgrade(self):
        self.service.install()
        self.service.backup.chmod(0o4750)
        entry = self.service.client.read_bytes()
        with self.assertRaisesRegex(ValueError, 'ownership or privileges'):
            self.service.remove()
        with patch.object(self.service, '_payload', return_value=self.service._payload() + '\n# update\n'):
            with self.assertRaisesRegex(ValueError, 'ownership or privileges'):
                self.service.refresh_installed_payload()
        self.assertEqual(entry, self.service.client.read_bytes())

    def test_payload_upgrade_uses_original_permissions(self):
        self.service.install()
        self.service.client.chmod(0o4777)
        with patch.object(self.service, '_payload', return_value=self.service._payload() + '\n# update\n'):
            self.service.refresh_installed_payload()
        self.assertEqual(0o750, self.service.client.stat().st_mode & 0o7777)

    def test_special_steam_entries_do_not_block_status_reads(self):
        self.service.client.unlink()
        os.mkfifo(self.service.client)
        self.assertFalse(self.service.get_status()['available'])
        with self.assertRaises((OSError, ValueError)):
            self.service.install()

    def test_special_or_oversized_records_do_not_block_restore(self):
        self.service.install()
        self.service.state.unlink()
        os.mkfifo(self.service.state)
        with self.assertRaisesRegex(OSError, 'not a file'):
            self.service.remove()
        self.service.state.unlink()
        self.service.checksum.unlink()
        os.mkfifo(self.service.checksum)
        with self.assertRaisesRegex(ValueError, 'not a regular'):
            self.service.remove()
        self.service.checksum.unlink()
        self.service.checksum.write_text('x' * 4097)
        with self.assertRaisesRegex(ValueError, 'too large'):
            self.service.remove()
        self.assertTrue(self.service._installed())

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

    def test_python_entry_alias_and_relative_path_still_block_mutations(self):
        alias = self.home / '.steam/root'
        alias.parent.mkdir()
        alias.symlink_to(self.home / '.local/share/Steam')
        process = self.service.proc_root / '123'
        process.mkdir()
        (process / 'exe').symlink_to('/usr/bin/python3')
        (process / 'cwd').symlink_to(self.service.client.parent)
        for script in (str(alias / 'ubuntu12_64/streaming_client'), './streaming_client'):
            (process / 'cmdline').write_bytes(os.fsencode('python3\0' + script + '\0'))
            self.assertEqual([123], self.service.running_pids())
            with self.assertRaisesRegex(ValueError, 'Close native'):
                self.service.install()

    def test_hardlinked_lock_never_changes_an_unrelated_file(self):
        target = self.home / 'unrelated'
        target.write_text('keep')
        self.service.lock_path.parent.mkdir(parents=True, exist_ok=True)
        os.link(target, self.service.lock_path)
        with patch('os.fchown') as chown:
            with self.assertRaisesRegex(ValueError, 'private regular'):
                self.service.install()
            chown.assert_not_called()
        self.assertEqual('keep', target.read_text())

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
            "#!/bin/sh\n# MAKO_LAUNCH_RENDERER_REQUIRED\n" + f"touch '{ready}'\nexec /usr/bin/sleep 30\n"
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

    def test_steam_update_during_install_failure_is_not_rolled_back(self):
        plugin = Plugin.__new__(Plugin)
        plugin.remote_play_service = self.service
        plugin.configuration_service = self.configuration
        before = self.configuration.config_file_path.read_bytes()
        from py_modules.mako_plugin import remote_play_core as remote_play
        write = remote_play.write_managed_text_atomically
        updated = ELF + b"updated by Steam during install"
        def fail_after_update(path, *arguments, **keywords):
            if path == self.service.client:
                replacement = path.with_suffix(".steam-update")
                replacement.write_bytes(updated)
                replacement.chmod(0o750)
                replacement.replace(path)
                raise OSError("Steam updated during preparation")
            return write(path, *arguments, **keywords)
        with patch.object(remote_play, "write_managed_text_atomically", side_effect=fail_after_update):
            with self.assertRaisesRegex(OSError, "changed externally"):
                plugin._install_remote_play_override()
        self.assertEqual(updated, self.service.client.read_bytes())
        self.assertEqual(before, self.configuration.config_file_path.read_bytes())
        self.assertTrue(list(self.service.client.parent.glob(".mako-rollback-*")))

    def test_steam_update_during_removal_failure_is_not_rolled_back(self):
        self.service.install()
        updated = ELF + b"updated by Steam during removal"
        unlink = Path.unlink
        def fail_after_update(path, *arguments, **keywords):
            if path == self.service.state:
                replacement = self.service.client.with_suffix(".steam-update")
                replacement.write_bytes(updated)
                replacement.chmod(0o750)
                replacement.replace(self.service.client)
                raise OSError("late failure after Steam update")
            return unlink(path, *arguments, **keywords)
        with patch.object(Path, "unlink", fail_after_update):
            with self.assertRaisesRegex(OSError, "changed externally"):
                self.service.remove()
        self.assertEqual(updated, self.service.client.read_bytes())
        self.service.remove()
        self.assertEqual(updated, self.service.client.read_bytes())

    def test_original_permissions_and_dependencies_are_ready_before_wrapper_commit(self):
        from py_modules.mako_plugin import remote_play_core as remote_play
        write = remote_play.write_managed_text_atomically
        def check_entry(path, *arguments, **keywords):
            if path == self.service.client:
                for dependency in (self.service.backup, self.service.checksum, self.service.state):
                    self.assertEqual(self.home.stat().st_uid, dependency.stat().st_uid)
                    self.assertTrue(os.access(dependency, os.R_OK))
                self.assertEqual((self.home.stat().st_uid, self.home.stat().st_gid), keywords['owner'])
                self.assertTrue(keywords['replace_guard']())
            return write(path, *arguments, **keywords)
        with patch.object(remote_play, "write_managed_text_atomically", side_effect=check_entry):
            self.service.install()

    def test_incompatible_system_python_never_replaces_steam(self):
        with patch.object(self.service, "_check_interpreter", side_effect=ValueError("unsupported Python")):
            with self.assertRaises(ValueError):
                self.service.install()
        self.assertEqual(ELF, self.service.client.read_bytes())
        self.assertFalse(self.service.backup.exists())

    def test_destination_guard_catches_new_stream_and_updater_before_commit(self):
        from py_modules.mako_plugin import remote_play_core as remote_play
        write = remote_play.write_managed_text_atomically
        def new_stream(path, *arguments, **keywords):
            if path == self.service.client:
                with patch.object(self.service, "running_pids", return_value=[123]):
                    return write(path, *arguments, **keywords)
            return write(path, *arguments, **keywords)
        with patch.object(remote_play, "write_managed_text_atomically", side_effect=new_stream):
            with self.assertRaisesRegex(OSError, "changed before replacement"):
                self.service.install()
        self.assertEqual(ELF, self.service.client.read_bytes())

    def test_late_install_failure_rolls_back_all_managed_files(self):
        from py_modules.mako_plugin import remote_play_core as remote_play
        write = remote_play.write_managed_text_atomically
        def fail_entry(path, *arguments, **keywords):
            if path == self.service.client:
                raise OSError("late replacement failure")
            return write(path, *arguments, **keywords)
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
        from py_modules.mako_plugin import remote_play_core as remote_play
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

    def test_qt_alternate_config_is_not_edited_as_the_decky_profile(self):
        from py_modules.mako_plugin.remote_play_core import RemotePlayOverride
        launcher = self.home / '.local/bin/mako-launch'
        launcher.write_text('#!/bin/sh\n')
        launcher.chmod(0o755)
        config = self.home / 'alternate/conf.toml'
        config.parent.mkdir()
        config.write_text('version=2\n[[profile]]\nname="Remote-Play"\n')
        owner = RemotePlayOverride(self.home, launcher, config_file=config)
        owner.proc_root = self.service.proc_root
        owner.install()
        before = self.configuration.config_file_path.read_bytes()
        with patch.object(self.service, 'running_pids', return_value=[123]):
            status = self.service.get_status()
            self.assertTrue(status['running'])
            self.assertTrue(status['conflict'])
            plugin = Plugin.__new__(Plugin)
            plugin.remote_play_service = self.service
            plugin.configuration_service = self.configuration
            async def inline(function, *arguments):
                return function(*arguments)
            with patch.object(self.configuration, 'sync_current_profile') as sync, \
                    patch('py_modules.mako_plugin.plugin.asyncio.to_thread', inline):
                asyncio.run(plugin.sync_current_profile(''))
                sync.assert_called_once_with('', '')
        self.assertEqual(self.configuration.config_file_path, self.service.config_file_path)
        with self.assertRaisesRegex(ValueError, 'different configuration'):
            plugin._install_remote_play_override()
        self.assertEqual(before, self.configuration.config_file_path.read_bytes())
        self.service.remove()
        self.service.install()
        saved = json.loads(self.service.state.read_text())
        self.assertEqual(str(self.home / '.local/bin/mako-run'), saved['launcher_path'])
        self.assertEqual(str(self.configuration.config_file_path), saved['configuration_path'])

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


class RemoteFeatureLaunchTests(unittest.TestCase):
    """Run the installed entry point and the real generated launcher without Vulkan."""
    def setUp(self):
        RemotePlayTests.setUp(self)
        self.service.client.write_bytes(Path('/usr/bin/env').read_bytes())
        if not self.service._native_client(self.service.client):
            self.skipTest('native x86_64 env executable unavailable')
        with self.configuration._configuration_write_lock:
            self.configuration.ensure_remote_play_profile(REMOTE_PLAY_PROFILE)
        self.service.install()

    def execute_profile(self, fields, gamescope=False):
        result = self.configuration.update_profile_config_fields(REMOTE_PLAY_PROFILE, fields)
        self.assertTrue(result['success'], result)
        from py_modules.mako_plugin.constants import (
            GAMESCOPE_WSI_MANIFEST_FILENAME_64, VKBASALT_MANIFEST_FILENAME_64,
            MANGOHUD_MANIFEST_FILENAME_64, SPATIAL_SCALING_JSON_FILENAME,
        )
        for directory, filename in (
            (self.configuration.gamescope_wsi_compatibility_dir, GAMESCOPE_WSI_MANIFEST_FILENAME_64),
            (self.configuration.vkbasalt_layer_dir, VKBASALT_MANIFEST_FILENAME_64),
            (self.configuration.mangohud_layer_dir, MANGOHUD_MANIFEST_FILENAME_64),
            (self.configuration.spatial_scaling_layer_dir, SPATIAL_SCALING_JSON_FILENAME),
        ):
            directory.mkdir(parents=True, exist_ok=True)
            (directory / filename).write_text('{}')
        environment = {'PATH': '/usr/bin:/bin', 'HOME': str(self.home), 'MAKO_PROFILE': 'wrong',
                       'GALLIUM_DRIVER': 'zink', 'ENABLE_GAMESCOPE_WSI': '1', 'ENABLE_VKBASALT': '1'}
        if gamescope:
            environment.update(GAMESCOPE_WAYLAND_DISPLAY='gamescope-0', WAYLAND_DISPLAY='gamescope-0')
        result = subprocess.run([str(self.service.client), '-0'], env=environment, capture_output=True, timeout=5)
        self.assertEqual(0, result.returncode, result.stderr.decode())
        return dict(item.decode().split('=', 1) for item in result.stdout.split(b'\0') if item)

    def test_real_launcher_feature_matrix_and_native_zink_exclusion(self):
        for fg, scaling, shaders in ((False, False, False), (True, False, False),
                                    (False, True, False), (False, False, True), (True, True, True)):
            with self.subTest(fg=fg, scaling=scaling, shaders=shaders):
                values = self.execute_profile({
                    'frame_generation_provisioned': fg, 'frame_generation_enabled': fg,
                    'scaling_enabled': scaling, 'external_vulkan_layer': 'vkbasalt' if shaders else '',
                    'vkbasalt_shader': 'vibrance', 'enable_zink': True,
                })
                self.assertEqual(REMOTE_PLAY_PROFILE, values['MAKO_PROFILE'])
                self.assertEqual(str(self.configuration.config_file_path), values['MAKO_CONFIG'])
                self.assertNotIn('GALLIUM_DRIVER', values)
                self.assertNotIn('MESA_LOADER_DRIVER_OVERRIDE', values)
                if fg or scaling:
                    self.assertEqual('1', values['ENABLE_MAKO'])
                else:
                    self.assertEqual('1', values['DISABLE_MAKO'])
                if shaders:
                    self.assertEqual('1', values['ENABLE_VKBASALT'])
                    self.assertNotIn('DISABLE_VKBASALT', values)
                    shader = Path(values['VKBASALT_CONFIG_FILE'])
                    self.assertTrue(shader.is_file())
                    self.assertIn('makoVibrance', shader.read_text())
                else:
                    self.assertEqual('1', values['DISABLE_VKBASALT'])

    def test_real_launcher_custom_shaders_wsi_scaling_and_compatible_options(self):
        fx = self.home / 'Custom.fx'
        fx.write_text('// portable registration fixture')
        self.assertTrue(self.configuration.add_profile_shader(REMOTE_PLAY_PROFILE, str(fx))['success'])
        values = self.execute_profile({
            'scaling_enabled': True, 'gamescope_wsi_compatibility': True,
            'external_vulkan_layer': 'vkbasalt', 'vkbasalt_shader': 'custom/Custom',
            'vkbasalt_manage_custom_shaders': True, 'force_alsa_audio': True,
            'disable_steamdeck_mode': True, 'disable_hdr_exposure': True,
        }, gamescope=True)
        from py_modules.mako_plugin.constants import (
            MAKO_LAYER_NAME, GAMESCOPE_WSI_LAYER_NAME_64, SPATIAL_SCALING_LAYER_NAME, VKBASALT_LAYER_NAME_64,
        )
        self.assertEqual(':'.join((MAKO_LAYER_NAME, GAMESCOPE_WSI_LAYER_NAME_64,
                                  SPATIAL_SCALING_LAYER_NAME, VKBASALT_LAYER_NAME_64)), values['VK_INSTANCE_LAYERS'])
        self.assertEqual('2', values['MAKO_SPLIT_LAYER_CHAIN'])
        # Explicit ordered layers replace the implicit activation flags.
        self.assertNotIn('ENABLE_GAMESCOPE_WSI', values)
        self.assertNotIn('DISABLE_GAMESCOPE_WSI', values)
        self.assertNotIn('ENABLE_MAKO_SPATIAL_SCALING', values)
        self.assertNotIn('DISABLE_MAKO_SPATIAL_SCALING', values)
        self.assertEqual('alsa', values['SDL_AUDIODRIVER'])
        self.assertEqual('0', values['SteamDeck'])
        self.assertEqual('1', values['MAKO_DISABLE_HDR_EXPOSURE'])
        self.assertIn(str(fx), Path(values['VKBASALT_CONFIG_FILE']).read_text())

    def test_real_launcher_mangohud_and_alternate_power_provisioning(self):
        self.assertTrue(self.configuration.set_profile_power_modes(REMOTE_PLAY_PROFILE, True)['success'])
        for mode in ('shared', 'handheld'):
            self.assertTrue(self.configuration.update_profile_config_fields(REMOTE_PLAY_PROFILE,
                {'frame_generation_provisioned': False, 'frame_generation_enabled': False}, mode)['success'])
        values = self.execute_profile({'external_vulkan_layer': 'mangohud'})
        self.assertEqual('1', values['ENABLE_MAKO'])
        self.assertEqual('1', values['MANGOHUD'])
        self.assertNotIn('DISABLE_MANGOHUD', values)
        self.assertNotIn('ENABLE_VKBASALT', values)

    def test_qt_style_sidecar_edit_is_used_without_regenerating_the_launcher(self):
        self.execute_profile({'external_vulkan_layer': 'vkbasalt', 'vkbasalt_shader': 'vibrance'})
        self.execute_profile({'external_vulkan_layer': '', 'frame_generation_provisioned': False,
                              'frame_generation_enabled': False, 'scaling_enabled': False})
        before = self.configuration.mako_script_path.read_bytes()
        settings = json.loads(self.configuration.wrapper_profile_settings_path.read_text())
        settings['profiles'][REMOTE_PLAY_PROFILE]['external_vulkan_layer'] = 'vkbasalt'
        self.configuration.wrapper_profile_settings_path.write_text(json.dumps(settings))
        data = self.configuration._get_profile_data()
        data['profiles'][REMOTE_PLAY_PROFILE]['scaling_enabled'] = True
        self.configuration._save_profile_data(data)
        result = subprocess.run([str(self.service.client), '-0'], capture_output=True,
                                env={'PATH': '/usr/bin:/bin', 'HOME': str(self.home)}, timeout=5)
        self.assertEqual(0, result.returncode, result.stderr.decode())
        values = dict(item.decode().split('=', 1) for item in result.stdout.split(b'\0') if item)
        self.assertEqual('1', values['ENABLE_VKBASALT'])
        self.assertEqual('1', values['ENABLE_MAKO'])
        self.assertEqual(before, self.configuration.mako_script_path.read_bytes())
        self.assertFalse(any(key.startswith('MAKO_LAUNCH_') for key in values))


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

    def test_special_records_and_symlink_config_use_safe_passthrough(self):
        for key in ('state', 'config'):
            with self.subTest(key=key):
                content = self.paths[key].read_bytes()
                self.paths[key].unlink()
                os.mkfifo(self.paths[key])
                self.calls.clear()
                self.assertEqual(0, self.run_launch())
                self.assertEqual(str(self.paths['original']), self.calls[0][0])
                self.paths[key].unlink()
                self.paths[key].write_bytes(content)
        target = self.paths['config'].with_suffix('.target')
        self.paths['config'].replace(target)
        self.paths['config'].symlink_to(target)
        self.calls.clear()
        self.assertEqual(0, self.run_launch())
        self.assertEqual(str(self.paths['original']), self.calls[0][0])
        self.paths['checksum'].unlink()
        os.mkfifo(self.paths['checksum'])
        self.calls.clear()
        self.assertEqual(126, self.run_launch())
        self.assertEqual([], self.calls)

    def test_missing_launch_lease_uses_only_verified_native_passthrough(self):
        self.assertEqual(0, launch(self.paths, self.args, lambda *args: self.calls.append(args),
                                  {"ENABLE_MAKO_SPATIAL_SCALING": "1"}, allow_override=False))
        self.assertEqual(str(self.paths["original"]), self.calls[0][0])
        self.assertEqual("1", self.calls[0][2]["DISABLE_MAKO"])
        self.assertEqual("1", self.calls[0][2]["DISABLE_MAKO_SPATIAL_SCALING"])

    def test_passthrough_disables_the_complete_managed_chain_but_preserves_other_layers(self):
        from py_modules.mako_plugin import constants
        self.assertEqual(set(MANAGED_LAYERS), {
            constants.MAKO_LAYER_NAME, constants.SPATIAL_SCALING_LAYER_NAME,
            constants.GAMESCOPE_WSI_LAYER_NAME_64, constants.VKBASALT_LAYER_NAME_64,
            constants.MANGOHUD_LAYER_NAME_64, constants.MANGOHUD_LAYER_NAME_32,
        })
        launch(self.paths, [], lambda *args: self.calls.append(args), {
            'VK_INSTANCE_LAYERS': ':'.join((*MANAGED_LAYERS, 'VK_LAYER_VALVE_steam_overlay_64', 'custom')),
            'ENABLE_GAMESCOPE_WSI': '1', 'MANGOHUD': '1', 'ENABLE_VKBASALT': '1',
            'MAKO_SPLIT_LAYER_CHAIN': '2', 'VKBASALT_CONFIG_RELOAD': '1',
        }, allow_override=False)
        env = self.calls[0][2]
        self.assertEqual('VK_LAYER_VALVE_steam_overlay_64:custom', env['VK_INSTANCE_LAYERS'])
        for key in ('DISABLE_MAKO','DISABLE_GAMESCOPE_WSI','DISABLE_MAKO_SPATIAL_SCALING','DISABLE_VKBASALT','DISABLE_MANGOHUD'):
            self.assertEqual('1', env[key])
        for key in ('ENABLE_GAMESCOPE_WSI', 'MANGOHUD', 'ENABLE_VKBASALT', 'MAKO_SPLIT_LAYER_CHAIN', 'VKBASALT_CONFIG_RELOAD'):
            self.assertNotIn(key, env)

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
