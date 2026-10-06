#!/usr/bin/env python3
"""Exercise the shared command in temporary native Steam trees, without Vulkan."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent
ELF = b'\x7fELF\x02\x01' + b'\0' * 12 + b'\x3e\0' + b'test client'
SYSTEM_PYTHON_SUPPORTED = subprocess.run(
    ['/usr/bin/python3', '-I', '-c', "import tomllib, hashlib; assert hasattr(hashlib, 'file_digest')"],
    capture_output=True, timeout=5).returncode == 0


class RemotePlayCommandTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.home = Path(temporary.name)
        self.client = self.home / '.local/share/Steam/ubuntu12_64/streaming_client'
        self.client.parent.mkdir(parents=True)
        self.client.write_bytes(ELF)
        self.client.chmod(0o750)
        self.launcher = self.home / '.local/bin/mako-launch'
        self.launcher.parent.mkdir(parents=True)
        self.launcher.write_text('#!/bin/sh\nexec "$@"\n')
        self.launcher.chmod(0o750)
        self.config = self.home / '.config/mako-render/conf.toml'
        self.config.parent.mkdir(parents=True)
        self.config.write_text('version=2\n[[profile]]\nname="Remote-Play"\n')
        self.environment = {'PATH': '/usr/bin:/bin', 'HOME': str(self.home)}

    def command(self, action, *, config=None, success=True):
        if action == 'install' and success and not SYSTEM_PYTHON_SUPPORTED:
            self.skipTest('Remote Play activation requires system Python 3.11 or newer')
        result = subprocess.run([str(ROOT / 'mako-remote-play'), action,
            '--launcher', str(self.launcher), '--config', str(config or self.config)],
            env=self.environment, capture_output=True, timeout=5)
        response = json.loads(result.stdout)
        self.assertEqual(success, response['success'], response)
        self.assertEqual(0 if success else 1, result.returncode, result.stderr.decode())
        return response

    def test_disabled_status_and_refresh_never_install(self):
        self.assertFalse(self.command('status')['installed'])
        self.assertFalse(self.command('refresh')['installed'])
        self.assertEqual(ELF, self.client.read_bytes())

    def test_install_restore_and_legacy_state_keep_original(self):
        self.assertTrue(self.command('install')['installed'])
        state = self.config.parent / 'native-remote-play.json'
        saved = json.loads(state.read_text())
        self.assertEqual(2, saved['version'])
        self.assertEqual(str(self.launcher), saved['launcher_path'])
        # Version-1 removal does not require new launcher/configuration keys.
        del saved['launcher_path']
        del saved['configuration_path']
        saved['version'] = 1
        state.write_text(json.dumps(saved))
        self.assertFalse(self.command('remove')['installed'])
        self.assertEqual(ELF, self.client.read_bytes())
        self.assertEqual(0o750, self.client.stat().st_mode & 0o7777)

    def test_standalone_shader_launch_and_custom_config_are_used(self):
        self.client.write_bytes(Path('/usr/bin/env').read_bytes())
        config = self.home / 'other config/conf.toml'
        config.parent.mkdir()
        config.write_text(self.config.read_text())
        settings = {'version': 1, 'profiles': {'Remote-Play': {'external_vulkan_layer': 'vkbasalt'}}}
        (config.parent / 'profile-wrapper-settings.json').write_text(json.dumps(settings))
        self.command('install', config=config)
        result = subprocess.run([str(self.client), '-0'], env=self.environment, capture_output=True, timeout=5)
        self.assertEqual(0, result.returncode, result.stderr.decode())
        values = dict(item.decode().split('=', 1) for item in result.stdout.split(b'\0') if item)
        self.assertEqual(str(config), values['MAKO_CONFIG'])
        self.assertEqual('Remote-Play', values['MAKO_PROFILE'])
        self.assertEqual('1', values['ENABLE_VKBASALT'])
        expected = config.parent / 'vkbasalt' / ('profile-' + hashlib.sha256(b'Remote-Play').hexdigest()[:12] + '.conf')
        self.assertEqual(str(expected), values['VKBASALT_CONFIG_FILE'])
        self.assertNotIn('GALLIUM_DRIVER', values)
        # The stored path survives calls made by the other UI/default config.
        self.assertEqual(str(config), self.command('status')['configuration_path'])
        before = self.client.read_bytes()
        self.command('refresh')
        self.assertEqual(before, self.client.read_bytes())
        self.command('restore-before-uninstall')
        self.assertEqual(Path('/usr/bin/env').read_bytes(), self.client.read_bytes())

    def test_conflicts_leave_client_untouched(self):
        backup = self.client.with_name('streaming_client.mako-original')
        backup.write_bytes(ELF)
        self.command('install', success=False)
        self.assertEqual(ELF, self.client.read_bytes())

    def test_real_standalone_launcher_uses_current_shader_settings(self):
        self.client.write_bytes(Path('/usr/bin/env').read_bytes())
        shutil.copyfile(ROOT / 'mako-launch', self.launcher)
        # Native config overrides must not change the standalone owner's global
        # launcher.conf discovery. Its ALSA setting still applies; Zink does not.
        (self.config.parent / 'launcher.conf').write_text('version=1\nenable_zink=1\nforce_alsa_audio=1\n')
        configuration = self.home / 'alternate/conf.toml'
        configuration.parent.mkdir()
        configuration.write_text(self.config.read_text())
        self.config = configuration
        prefix = self.launcher.parent.parent
        for relative in ('share/mako-render/vulkan/vkbasalt.d/vkBasalt.json',
                         'share/mako-render/vulkan/vkbasalt.d/vkBasalt.x86.json',
                         'lib/vkbasalt/libvkbasalt.so', 'lib32/vkbasalt/libvkbasalt.so'):
            path = prefix / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        # Use the same profile identity calculation as both editors.
        shader = self.config.parent / 'vkbasalt' / ('profile-' + hashlib.sha256(b'Remote-Play').hexdigest()[:12] + '.conf')
        shader.parent.mkdir()
        shader.write_text('effects=cas\n')
        self.command('install')
        for enabled in (False, True, False):
            settings = {'version': 1, 'profiles': {'Remote-Play': {
                'external_vulkan_layer': 'vkbasalt' if enabled else ''}}}
            (self.config.parent / 'profile-wrapper-settings.json').write_text(json.dumps(settings))
            result = subprocess.run([str(self.client), '-0'], env=self.environment, capture_output=True, timeout=5)
            self.assertEqual(0, result.returncode, result.stderr.decode())
            values = dict(item.decode().split('=', 1) for item in result.stdout.split(b'\0') if item)
            self.assertEqual('Remote-Play', values['MAKO_PROFILE'])
            self.assertEqual(str(self.config), values['MAKO_CONFIG'])
            self.assertEqual(enabled, 'VK_LAYER_VKBASALT_post_processing' in values.get('VK_INSTANCE_LAYERS', ''))
            self.assertEqual('1', values['DISABLE_GAMESCOPE_WSI'])
            self.assertEqual('alsa', values['SDL_AUDIODRIVER'])
            self.assertNotIn('GALLIUM_DRIVER', values)
            if enabled:
                self.assertEqual(str(shader), values['VKBASALT_CONFIG_FILE'])

    def test_legacy_cached_decky_launcher_refuses_upgrade(self):
        self.command('install')
        legacy_runner = self.launcher.with_name('mako-run')
        legacy_runner.write_bytes(self.launcher.read_bytes())
        legacy_runner.chmod(0o750)
        payload = self.client.read_text().replace('OVERRIDE_V2', 'OVERRIDE_V1')
        payload = payload.replace(repr(str(self.launcher)), repr(str(legacy_runner)))
        self.client.write_text(payload)
        state = self.config.parent / 'native-remote-play.json'
        saved = {'version': 1, 'enabled': True, 'profile': 'Remote-Play',
                 'wrapper_sha256': hashlib.sha256(payload.encode()).hexdigest()}
        state.write_text(json.dumps(saved))
        response = self.command('refresh', success=False)
        self.assertIn('Update MAKO Decky', response['error'])
        self.assertEqual(payload, self.client.read_text())
        self.assertEqual(saved, json.loads(state.read_text()))
        self.command('remove')
        self.assertEqual(ELF, self.client.read_bytes())

    def test_version_one_upgrade_preserves_its_launcher_and_blocks_old_readers(self):
        self.command('install')
        legacy_runner = self.launcher.with_name('mako-run')
        legacy_runner.write_bytes(self.launcher.read_bytes() + b'# MAKO_LAUNCH_RENDERER_REQUIRED\n')
        legacy_runner.chmod(0o750)
        payload = self.client.read_text().replace('OVERRIDE_V2', 'OVERRIDE_V1')
        payload = payload.replace(repr(str(self.launcher)), repr(str(legacy_runner)))
        self.client.write_text(payload)
        state = self.config.parent / 'native-remote-play.json'
        state.write_text(json.dumps({'version': 1, 'enabled': True, 'profile': 'Remote-Play',
                                    'wrapper_sha256': hashlib.sha256(payload.encode()).hexdigest()}))
        self.command('refresh')
        saved = json.loads(state.read_text())
        self.assertEqual(2, saved['version'])
        self.assertEqual(str(legacy_runner), saved['launcher_path'])
        self.assertNotIn(b'OVERRIDE_V1', self.client.read_bytes()[:256])
        self.command('remove')
        self.assertEqual(ELF, self.client.read_bytes())


if __name__ == '__main__':
    unittest.main()
