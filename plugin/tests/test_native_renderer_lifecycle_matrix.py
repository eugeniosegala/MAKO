"""End-to-end filesystem matrix for managed native Renderer lifecycles."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


class _Logger:
    def __getattr__(self, _name):
        return lambda *_args, **_kwargs: None


sys.modules.setdefault("decky", SimpleNamespace(logger=_Logger()))

from py_modules.mako_plugin.constants import (  # noqa: E402
    ACTIVE_RENDERER_OWNER_STANDALONE,
    ACTIVE_RENDERER_STATE_SCHEMA_VERSION,
)
from py_modules.mako_plugin.installation import InstallationService  # noqa: E402
from py_modules.mako_plugin import base_service as base_service_module  # noqa: E402


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
STANDALONE_INSTALLER = REPOSITORY_ROOT / "engine/scripts/mako-installer"
DECKY_VERSION = "3.0.0"
STANDALONE_VERSION = "3.1.0"


class NativeRendererLifecycleMatrixTests(unittest.TestCase):
    @staticmethod
    def _metadata(version: str = DECKY_VERSION) -> dict:
        return {
            "name": f"MAKO-Renderer-v{version}-linux.tar.xz",
            "version": version,
            "sha256hash": "a" * 64,
            "architectures": ["64"],
            "host_architectures": ["x86_64"],
        }

    @staticmethod
    def _service(home: Path) -> InstallationService:
        with patch.object(
            base_service_module,
            "resolve_user_home",
            return_value=home,
        ):
            return InstallationService(logger=_Logger())

    def _install_decky(self, service: InstallationService) -> list[Path]:
        managed_files = service._decky_renderer_files()
        for path in managed_files:
            if path in (
                service.engine_state_file,
                service.active_renderer_state_file,
            ):
                continue
            path.parent.mkdir(parents=True, exist_ok=True)
            if path == service.json_file:
                path.write_text(
                    json.dumps({
                        "layer": {
                            "library_path": str(service.lib_file),
                        },
                    }),
                    encoding="utf-8",
                )
            else:
                path.write_bytes(f"decky:{path.name}".encode("utf-8"))
        metadata = self._metadata()
        service.engine_state_file.write_text(
            json.dumps({
                "archive": metadata["name"],
                "version": metadata["version"],
                "sha256hash": metadata["sha256hash"],
            }),
            encoding="utf-8",
        )
        service._write_active_renderer_state(metadata)
        return managed_files

    def _install_standalone(
            self,
            service: InstallationService,
            version: str = STANDALONE_VERSION,
    ) -> list[Path]:
        prefix = service.standalone_install_prefix
        standalone_files = [
            prefix / "lib/libmako-render.so",
            prefix / "lib/libmako-render-scaling.so",
            prefix / "bin/mako-ui",
            prefix / "bin/mako-installer",
            prefix / "bin/mako-launch",
            prefix / "bin/mako-diagnostics",
            prefix / "share/applications/io.github.eugeniosegala.mako.desktop",
            service.json_file,
            service.spatial_scaling_json_file,
            service.registered_json_file,
            prefix / (
                "share/vulkan/implicit_layer.d/"
                "VkLayer_MAKO_spatial_scaling.json"
            ),
        ]
        for path in standalone_files:
            path.parent.mkdir(parents=True, exist_ok=True)
            if path == service.json_file:
                path.write_text(
                    json.dumps({
                        "layer": {
                            "library_path": str(
                                prefix / "lib/libmako-render.so"
                            ),
                        },
                    }),
                    encoding="utf-8",
                )
            else:
                path.write_bytes(f"standalone:{path.name}".encode("utf-8"))

        service.standalone_installer_state_file.parent.mkdir(
            parents=True, exist_ok=True
        )
        ownership_lines = []
        for path in standalone_files:
            relative_path = path.relative_to(prefix).as_posix()
            checksum = hashlib.sha256(path.read_bytes()).hexdigest()
            ownership_lines.append(f"{checksum}  {relative_path}")
        service.standalone_installer_state_file.write_text(
            "\n".join(ownership_lines) + "\n",
            encoding="utf-8",
        )
        service.active_renderer_state_file.write_text(
            json.dumps({
                "schema_version": ACTIVE_RENDERER_STATE_SCHEMA_VERSION,
                "owner": ACTIVE_RENDERER_OWNER_STANDALONE,
                "version": version,
            }),
            encoding="utf-8",
        )
        return standalone_files

    @staticmethod
    def _run_standalone_uninstaller(home: Path, prefix: Path | None = None) -> None:
        environment = os.environ.copy()
        environment.update({
            "HOME": str(home),
            "XDG_CONFIG_HOME": str(home / ".config"),
            "MAKO_INSTALLER_ASSUME_YES": "1",
        })
        if prefix is not None:
            environment["MAKO_INSTALL_PREFIX"] = str(prefix)
        result = subprocess.run(
            [str(STANDALONE_INSTALLER), "--uninstall"],
            check=False,
            capture_output=True,
            text=True,
            env=environment,
        )
        if result.returncode != 0:
            raise AssertionError(result.stderr or result.stdout)

    def _status(self, service: InstallationService) -> dict:
        with (
            patch.object(
                service,
                "_bundled_archive_metadata",
                return_value=self._metadata(),
            ),
            patch.object(
                service,
                "_host_compatibility",
                return_value=("x86_64", True, None),
            ),
        ):
            return service.check_installation()

    def _run_scenario(self, install_order: str, uninstaller: str) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            home = Path(temporary_directory) / "home"
            home.mkdir()
            service = self._service(home)
            plugin_manifest = home / "homebrew/plugins/Mako/plugin.json"
            plugin_manifest.parent.mkdir(parents=True)
            plugin_manifest.write_text("{}\n", encoding="utf-8")
            service.config_file_path.parent.mkdir(parents=True, exist_ok=True)
            service.config_file_path.write_text(
                "version = 2\n",
                encoding="utf-8",
            )
            flatpak_marker = home / ".local/share/flatpak/mako-runtime.keep"
            flatpak_marker.parent.mkdir(parents=True)
            flatpak_marker.write_text("shared runtime\n", encoding="utf-8")

            decky_files: list[Path] = []
            standalone_files: list[Path] = []
            if install_order == "decky_only":
                decky_files = self._install_decky(service)
            elif install_order == "standalone_only":
                standalone_files = self._install_standalone(service)
            elif install_order == "decky_then_standalone":
                decky_files = self._install_decky(service)
                standalone_files = self._install_standalone(service)
            elif install_order == "standalone_then_decky":
                standalone_files = self._install_standalone(service)
                decky_files = self._install_decky(service)
            else:
                self.fail(f"Unknown install order: {install_order}")

            if install_order == "standalone_only":
                with (
                    patch.object(
                        service,
                        "_bundled_archive_metadata",
                        return_value=self._metadata(),
                    ),
                    patch.object(
                        service,
                        "_host_compatibility",
                        return_value=("x86_64", True, None),
                    ),
                ):
                    self.assertTrue(
                        service.prepare_active_standalone_for_decky()
                    )

            status = self._status(service)
            if install_order == "standalone_only":
                self.assertTrue(status["installed"])
                self.assertEqual(
                    status["installed_engine_version"], STANDALONE_VERSION
                )
                self.assertTrue(status["engine_update_required"])
            elif install_order == "decky_then_standalone":
                self.assertTrue(status["installed"])
                self.assertEqual(
                    status["installed_engine_version"], STANDALONE_VERSION
                )
                self.assertTrue(status["engine_update_required"])
            else:
                self.assertTrue(status["installed"])
                self.assertEqual(
                    status["installed_engine_version"], DECKY_VERSION
                )
                self.assertFalse(status["engine_update_required"])

            if uninstaller == "decky_renderer":
                result = service.uninstall()
                self.assertTrue(result["success"], result.get("error"))
            elif uninstaller == "decky_plugin_cleanup":
                service.cleanup_on_uninstall()
            elif uninstaller == "standalone":
                self._run_standalone_uninstaller(home)
            else:
                self.fail(f"Unknown uninstaller: {uninstaller}")

            managed_paths = {
                *service._decky_renderer_files(),
                *decky_files,
                *standalone_files,
            }
            for path in managed_paths:
                self.assertFalse(path.exists(), f"Renderer file remains: {path}")
            self.assertFalse(service.active_renderer_state_file.exists())
            self.assertFalse(service.standalone_installer_state_file.exists())
            self.assertFalse(
                service.local_lib_dir.parent.exists(),
                "managed Renderer data directory remains after uninstall",
            )
            self.assertTrue(plugin_manifest.exists())
            self.assertTrue(service.config_file_path.exists())
            self.assertTrue(flatpak_marker.exists())

    def test_every_managed_install_order_and_uninstaller_combination(self):
        scenarios = (
            ("decky_only", "decky_renderer"),
            ("decky_only", "decky_plugin_cleanup"),
            ("standalone_only", "decky_renderer"),
            ("standalone_only", "decky_plugin_cleanup"),
            ("standalone_only", "standalone"),
            ("decky_then_standalone", "decky_renderer"),
            ("decky_then_standalone", "decky_plugin_cleanup"),
            ("decky_then_standalone", "standalone"),
            ("standalone_then_decky", "decky_renderer"),
            ("standalone_then_decky", "decky_plugin_cleanup"),
            ("standalone_then_decky", "standalone"),
        )
        for install_order, uninstaller in scenarios:
            with self.subTest(
                install_order=install_order,
                uninstaller=uninstaller,
            ):
                self._run_scenario(install_order, uninstaller)

    def test_decky_can_reinstall_after_renderer_removal(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            home = Path(temporary_directory) / "home"
            home.mkdir()
            service = self._service(home)
            service.config_file_path.parent.mkdir(parents=True)
            service.config_file_path.write_text(
                "version = 2\n",
                encoding="utf-8",
            )
            self._install_decky(service)
            self._install_standalone(service)

            result = service.uninstall()
            self.assertTrue(result["success"], result.get("error"))
            self.assertFalse(self._status(service)["installed"])

            self._install_decky(service)
            status = self._status(service)
            self.assertTrue(status["installed"])
            self.assertEqual(status["installed_engine_version"], DECKY_VERSION)
            self.assertFalse(status["engine_update_required"])

            second_result = service.uninstall()
            self.assertTrue(
                second_result["success"], second_result.get("error")
            )
            self.assertFalse(self._status(service)["installed"])
            self.assertFalse(service.local_lib_dir.parent.exists())
            self.assertFalse(service.active_renderer_state_file.exists())
            self.assertFalse(service.standalone_installer_state_file.exists())
            self.assertTrue(service.config_file_path.exists())

    def test_decky_can_reinstall_after_plugin_cleanup(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            home = Path(temporary_directory) / "home"
            home.mkdir()
            service = self._service(home)
            service.config_file_path.parent.mkdir(parents=True)
            service.config_file_path.write_text(
                "version = 2\n",
                encoding="utf-8",
            )
            self._install_decky(service)
            self._install_standalone(service)

            service.cleanup_on_uninstall()
            self.assertFalse(self._status(service)["installed"])
            self.assertFalse(service.local_lib_dir.parent.exists())

            reinstalled_service = self._service(home)
            self._install_decky(reinstalled_service)
            status = self._status(reinstalled_service)
            self.assertTrue(status["installed"])
            self.assertEqual(status["installed_engine_version"], DECKY_VERSION)
            self.assertFalse(status["engine_update_required"])

            reinstalled_service.cleanup_on_uninstall()
            self.assertFalse(self._status(reinstalled_service)["installed"])
            self.assertFalse(reinstalled_service.local_lib_dir.parent.exists())
            self.assertFalse(
                reinstalled_service.active_renderer_state_file.exists()
            )
            self.assertFalse(
                reinstalled_service.standalone_installer_state_file.exists()
            )
            self.assertTrue(reinstalled_service.config_file_path.exists())

    def test_matching_standalone_renderer_is_adopted_without_update(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            home = Path(temporary_directory) / "home"
            home.mkdir()
            service = self._service(home)
            self._install_standalone(service, DECKY_VERSION)

            with (
                patch.object(
                    service,
                    "_bundled_archive_metadata",
                    return_value=self._metadata(),
                ),
                patch.object(
                    service,
                    "_host_compatibility",
                    return_value=("x86_64", True, None),
                ),
            ):
                self.assertTrue(
                    service.prepare_active_standalone_for_decky()
                )

            status = self._status(service)
            self.assertTrue(status["installed"])
            self.assertEqual(status["installed_engine_version"], DECKY_VERSION)
            self.assertFalse(status["engine_update_required"])

    def test_custom_standalone_prefix_does_not_remove_decky_renderer(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            home = Path(temporary_directory) / "home"
            home.mkdir()
            service = self._service(home)
            self._install_decky(service)
            custom_prefix = home / "custom-prefix"
            custom_file = custom_prefix / "bin/mako-ui"
            custom_file.parent.mkdir(parents=True)
            custom_file.write_bytes(b"custom standalone")
            state_file = (
                custom_prefix
                / "share/mako-render/installer/installed-files.sha256"
            )
            state_file.parent.mkdir(parents=True)
            state_file.write_text(
                f"{hashlib.sha256(custom_file.read_bytes()).hexdigest()}  bin/mako-ui\n",
                encoding="utf-8",
            )
            active_state = custom_prefix / "share/mako-render/active-renderer.json"
            active_state.write_text("{}\n", encoding="utf-8")

            self._run_standalone_uninstaller(home, custom_prefix)

            self.assertFalse(custom_file.exists())
            self.assertFalse(active_state.exists())
            self.assertTrue(service.lib_file.exists())
            self.assertTrue(service.active_renderer_state_file.exists())


if __name__ == "__main__":
    unittest.main()
