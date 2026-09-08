"""Exercise preparation against real Flatpak serialization in temporary storage."""

import logging
from pathlib import Path
import shutil
import sys
import tempfile
from types import SimpleNamespace
import unittest


sys.modules.setdefault("decky", SimpleNamespace(logger=logging.getLogger(__name__)))

from py_modules.mako_plugin.flatpak_service import FlatpakService  # noqa: E402


@unittest.skipUnless(
    sys.platform == "linux" and shutil.which("flatpak"),
    "requires Linux and the Flatpak command (installed in Decky CI)",
)
class FlatpakOverrideIntegrationTests(unittest.TestCase):
    def test_prepare_refresh_remove_with_real_flatpak_overrides(self):
        for app_id, app_name in (
            ("com.heroicgameslauncher.hgl", "Heroic"),
            ("net.lutris.Lutris", "Lutris"),
            ("org.DolphinEmu.dolphin-emu", "Dolphin"),
        ):
            with self.subTest(app_id=app_id), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                service = FlatpakService(logger=logging.getLogger(__name__))
                service.user_home = root
                service.config_dir = root / "config"
                service.config_file_path = service.config_dir / "conf.toml"
                service.mako_script_path = root / "mako-run"
                service.mako_script_path.touch()
                service.gamescope_wsi_compatibility_dir = root / "wsi"

                # FLATPAK_USER_DIR isolates every --user override from installed
                # applications. No runtime, Renderer, DLL, or GPU is needed.
                environment = service._get_clean_env()
                flatpak_directory = root / "flatpak"
                environment["FLATPAK_USER_DIR"] = str(flatpak_directory)
                service._get_clean_env = lambda: environment.copy()
                self.assertTrue(service.check_flatpak_available())

                # Supply only application/runtime inventory. Preparation,
                # removal, serialization, parsing, and RPC payloads stay real.
                service._host_architecture_supported = lambda: True
                service._get_app_runtime_version = lambda _app: "25.08"
                service._is_extension_installed = lambda _version: True
                run_flatpak = service._run_flatpak_command

                def run(args, **kwargs):
                    if args == ["list", "--app"]:
                        return SimpleNamespace(
                            stdout=f"{app_name}\t{app_id}\n", returncode=0,
                        )
                    kwargs.setdefault("timeout", 10)
                    return run_flatpak(args, **kwargs)

                service._run_flatpak_command = run

                def read_app():
                    response = service.get_flatpak_apps()
                    self.assertTrue(response["success"], response)
                    self.assertEqual(len(response["apps"]), 1)
                    return response["apps"][0]

                def assert_prepared(expected):
                    app = read_app()
                    self.assertEqual(app["has_filesystem_override"], expected)
                    self.assertEqual(app["has_wrapper_override"], expected)
                    if expected:
                        self.assertTrue(app["has_required_env_override"])
                    else:
                        self.assertFalse(app["has_env_override"])

                run(
                    ["override", "--user", "--env=MANGOHUD=1",
                     f"--filesystem={root}/unrelated:ro", app_id],
                    check=True, capture_output=True, text=True,
                )
                assert_prepared(False)

                for _ in range(2):
                    response = service.set_app_override(app_id)
                    self.assertTrue(response["success"], response)
                    # Repeated list refreshes must retain the Prepared state.
                    assert_prepared(True)
                    assert_prepared(True)

                response = service.remove_app_override(app_id)
                self.assertTrue(response["success"], response)
                assert_prepared(False)

                override = (flatpak_directory / "overrides" / app_id).read_text()
                self.assertIn("MANGOHUD=1", override)
                self.assertIn(f"{root}/unrelated:ro;", override)


if __name__ == "__main__":
    unittest.main()
