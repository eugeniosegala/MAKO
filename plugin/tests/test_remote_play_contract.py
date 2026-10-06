"""Keep independently installed Remote Play adapters on the shared contract."""
import ast
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import types as python_types
from typing import get_args, get_origin, Union
import unittest

from py_modules.mako_plugin import remote_play_core, remote_play_launch, types
from py_modules.mako_plugin.profile_storage import vkbasalt_profile_config_filename

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'engine/scripts/mako_remote_play'
GENERATOR = Path('engine/scripts/generate-remote-play-bindings.py')
MODULES = ('managed_files.py', 'remote_play_core.py', 'remote_play_launch.py')


def type_shape(annotation):
    origin = get_origin(annotation)
    if origin in (Union, python_types.UnionType):
        return ('union', frozenset(type_shape(value) for value in get_args(annotation)))
    return (origin, tuple(type_shape(value) for value in get_args(annotation))) if origin else annotation


class RemotePlayContractTests(unittest.TestCase):
    def test_generated_owner_and_response_shapes_match(self):
        for name in MODULES:
            self.assertEqual((SOURCE / name).read_bytes(),
                (ROOT / 'plugin/py_modules/mako_plugin' / name).read_bytes())
        self.assertEqual({key: type_shape(value) for key, value in remote_play_core.RemotePlayResponse.__annotations__.items()},
                         {key: type_shape(value) for key, value in types.RemotePlayResponse.__annotations__.items()})
        backend = (ROOT / 'engine/mako-ui/src/remote_play.cpp').read_text()
        self.assertIn('QStringLiteral("' + remote_play_core.REMOTE_PLAY_PROFILE + '")', backend)

    def test_discovery_inputs_and_shader_identity_use_existing_owners(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'conf.toml'
            sidecar = config.parent / 'profile-wrapper-settings.json'
            sidecar.write_text('{"version":1,"profiles":{"Remote-Play":{"external_vulkan_layer":"vkbasalt"}}}')
            settings = remote_play_launch.launch_settings({'name': 'Remote-Play'}, config)
            self.assertEqual(Path(settings['MAKO_LAUNCH_VKBASALT_CONFIG']).name,
                             vkbasalt_profile_config_filename('Remote-Play'))
            (config.parent / 'profile-metadata.json').write_text(
                '{"version":1,"profiles":{"Remote-Play":{"steam_app_id":"42"}}}')
            settings = remote_play_launch.launch_settings({'name': 'Remote-Play'}, config)
            self.assertEqual(Path(settings['MAKO_LAUNCH_VKBASALT_CONFIG']).name,
                             vkbasalt_profile_config_filename('Remote-Play', '42'))
        generator = (ROOT / 'plugin/py_modules/mako_plugin/wrapper_generation.py').read_text()
        tree = ast.parse(generator)
        literals = [node.value for node in ast.walk(tree) if isinstance(node, ast.Constant) and isinstance(node.value, str)]
        self.assertTrue(any('MAKO_PROFILE:-' in text and 'Remote-Play' in text for text in literals))
        for key in remote_play_launch.LaunchSettings.__annotations__:
            self.assertTrue(any(key in text for text in literals), key)
            self.assertTrue(any(text.startswith('unset ') and key in text for text in literals), key)
        qt = (ROOT / 'engine/mako-ui/src/backend.cpp').read_text()
        self.assertIn('QCryptographicHash::Sha256', qt)
        self.assertIn('.toHex().left(12)', qt)

    def test_native_package_contains_command_and_private_owner(self):
        cmake = (ROOT / 'engine/CMakeLists.txt').read_text()
        self.assertIn('scripts/mako-remote-play', cmake)
        self.assertIn('scripts/mako_remote_play/', cmake)
        packaging = (ROOT / 'engine/scripts/package-local.sh').read_text()
        self.assertIn('"bin/mako-remote-play"', packaging)
        for name in (*MODULES, '__init__.py'):
            self.assertIn('"share/mako-render/mako_remote_play/' + name + '"', packaging)
        self.assertIn(b'OVERRIDE_V2', remote_play_core.MARKER)
        self.assertIn(b'OVERRIDE_V1', remote_play_core.LEGACY_MARKER)

    def test_freshness_check_never_repairs_missing_or_stale_bindings(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = [GENERATOR, *(Path('engine/scripts/mako_remote_play') / name for name in MODULES)]
            for relative in paths:
                target = root / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / relative, target)
            destination = root / 'plugin/py_modules/mako_plugin'
            destination.mkdir(parents=True)
            def run(*arguments):
                return subprocess.run([sys.executable, str(root / GENERATOR), *arguments],
                    capture_output=True, text=True, timeout=5)
            self.assertNotEqual(0, run('--check').returncode)
            self.assertEqual([], list(destination.iterdir()))
            self.assertEqual(0, run().returncode)
            self.assertEqual(0, run('--check').returncode)
            stale = destination / MODULES[0]
            stale.write_text('# stale\n')
            result = run('--check')
            self.assertNotEqual(0, result.returncode)
            self.assertIn(MODULES[0], result.stderr)
            self.assertEqual('# stale\n', stale.read_text())


if __name__ == '__main__':
    unittest.main()
