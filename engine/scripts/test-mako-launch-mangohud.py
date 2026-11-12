#!/usr/bin/env python3
"""Exercise the real standalone launcher without loading a graphics driver."""
import concurrent.futures
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

LAUNCHER = Path(__file__).with_name("mako-launch")


class MangoHudLauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="mako-mangohud-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.environment = {
            "PATH": "/usr/bin:/bin", "HOME": str(self.root),
            "XDG_CONFIG_HOME": str(self.root / "config with spaces'"),
            "XDG_CONFIG_DIRS": str(self.root / "config-dirs"),
            "XDG_DATA_HOME": str(self.root / "data"),
            "XDG_DATA_DIRS": str(self.root / "system"),
            "XDG_CACHE_HOME": str(self.root / "cache"),
        }
        self.manifests = self.root / "system/vulkan/implicit_layer.d"
        self.manifests.mkdir(parents=True)
        self.private = self.root / "cache/mako-render/mangohud"
        self.shared = Path(self.environment["XDG_CONFIG_HOME"]) / "mako-render/mangohud.conf"
        self.shared.parent.mkdir(parents=True)

    def manifest(self, bits="64", **updates):
        suffix = "x86_64" if bits == "64" else "x86"
        library = self.root / ("libMangoHud" + bits + ".so")
        library.write_bytes(b"synthetic library; never loaded")
        layer = {
            "name": "VK_LAYER_MANGOHUD_overlay_" + suffix,
            "type": "GLOBAL", "library_path": str(library),
            "enable_environment": {"MANGOHUD": "1"},
            "disable_environment": {"DISABLE_MANGOHUD": "1"},
        }
        layer.update(updates)
        path = self.manifests / ("MangoHud." + suffix + ".json")
        path.write_text(json.dumps({"file_format_version": "1.0.0", "layer": layer}))
        return path

    def run_launcher(self, *args, **environment):
        return subprocess.run(
            ["bash", str(LAUNCHER), *args],
            env={**self.environment, **environment},
            text=True, capture_output=True, timeout=10, cwd=self.root,
        )

    def game(self, **environment):
        result = self.run_launcher("/usr/bin/env", **environment)
        self.assertEqual(result.returncode, 0, result.stderr)
        return dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)

    def test_availability_check_is_read_only(self):
        result = self.run_launcher("--check-mangohud")
        self.assertEqual(json.loads(result.stdout), {"available": False})
        self.manifest()
        result = self.run_launcher("--check-mangohud")
        self.assertEqual(json.loads(result.stdout), {"available": True})
        self.assertFalse(self.private.exists())
        self.assertFalse(self.shared.exists())

    def test_native_order_and_configuration_precedence_for_both_architectures(self):
        for bits in ("64", "32"):
            with self.subTest(bits=bits):
                for path in self.manifests.iterdir():
                    path.unlink()
                self.manifest(bits)
                self.shared.write_text("fps\nframetime\n")
                for inherited in ("", "/run/gamescope/mangohud.conf", "/custom.conf"):
                    env = self.game(MANGOHUD="1", MANGOHUD_CONFIGFILE=inherited,
                                    MANGOHUD_CONFIG="read_cfg,fps", DISABLE_MANGOHUD="1",
                                    VK_INSTANCE_LAYERS="VK_LAYER_MANGOHUD_overlay_x86:VK_LAYER_TEST:VK_LAYER_MANGOHUD_overlay_x86_64:VK_LAYER_MAKO_render")
                    self.assertEqual(env["MANGOHUD_CONFIGFILE"], str(self.shared))
                    self.assertEqual(env["MANGOHUD_CONFIG"], "read_cfg,fps")
                    self.assertNotIn("DISABLE_MANGOHUD", env)
                    self.assertTrue(env["VK_IMPLICIT_LAYER_PATH"].endswith(":" + str(self.private)))
                    self.assertNotIn(str(self.manifests), env["VK_IMPLICIT_LAYER_PATH"])
                    if bits == "64":
                        # Older loaders, such as 1.3.239, need the gates beside the list.
                        self.assertEqual(env["MANGOHUD"], "1")
                        self.assertEqual(env["ENABLE_MAKO"], "1")
                        self.assertEqual(env["VK_INSTANCE_LAYERS"], "VK_LAYER_MAKO_render:VK_LAYER_MANGOHUD_overlay_x86_64:VK_LAYER_TEST")
                    else:
                        self.assertEqual(env["MANGOHUD"], "1")
                        self.assertNotIn("MANGOHUD", env["VK_INSTANCE_LAYERS"])
                staged = list(self.private.glob("*.json"))
                self.assertEqual(len(staged), 1)
                self.assertEqual(json.loads(staged[0].read_text())["layer"]["library_arch"], bits)

    def test_desktop_steam_overlay_stays_below_renderer_while_mangohud_counts_game_frames(self):
        self.manifest()
        config = self.root / "overlay-enabled.conf"
        config.write_text("version=1\ndisable_steam_overlay=0\n")
        overlay = self.root / "data/vulkan/implicit_layer.d"
        overlay.mkdir(parents=True)
        (overlay / "steamoverlay_x86_64.json").write_text("{}")
        for real, expected in (
                ("0", "VK_LAYER_MAKO_render:VK_LAYER_MANGOHUD_overlay_x86_64"),
                ("1", "VK_LAYER_MANGOHUD_overlay_x86_64:VK_LAYER_MAKO_render")):
            with self.subTest(real=real):
                env = self.game(MANGOHUD="1", MAKO_LAUNCH_CONFIG=str(config),
                                MAKO_MANGOHUD_REAL_FRAMES=real,
                                ENABLE_VK_LAYER_VALVE_steam_overlay_1="1",
                                VK_INSTANCE_LAYERS="VK_LAYER_TEST:VK_LAYER_VALVE_steam_overlay_64")
                self.assertEqual(env["VK_INSTANCE_LAYERS"],
                                 expected + ":VK_LAYER_VALVE_steam_overlay_64:VK_LAYER_TEST")
                # The overlay's list predates MangoHud ordering and keeps both gates off.
                self.assertNotIn("MANGOHUD", env)
                self.assertNotIn("ENABLE_MAKO", env)

    def test_missing_or_invalid_shared_file_preserves_inheritance(self):
        self.manifest()
        for kind in ("missing", "directory", "fifo", "dangling"):
            with self.subTest(kind=kind):
                if kind == "directory": self.shared.mkdir()
                elif kind == "fifo": os.mkfifo(self.shared)
                elif kind == "dangling": self.shared.symlink_to(self.root / "absent")
                env = self.game(MANGOHUD="1", MANGOHUD_CONFIGFILE="/previous.conf")
                self.assertEqual(env["MANGOHUD_CONFIGFILE"], "/previous.conf")
                if kind == "directory": self.shared.rmdir()
                elif kind != "missing": self.shared.unlink()

    def test_live_file_changes_and_per_game_override_do_not_regenerate_profiles(self):
        self.manifest()
        self.assertNotIn("MANGOHUD_CONFIGFILE", self.game(MANGOHUD="1"))
        self.shared.write_text("fps\n$(touch should-not-exist)\n")
        self.assertEqual(self.game(MANGOHUD="1")["MANGOHUD_CONFIGFILE"], str(self.shared))
        result = self.run_launcher("env", "MANGOHUD_CONFIGFILE=/game.conf", "/usr/bin/env", MANGOHUD="1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("MANGOHUD_CONFIGFILE=/game.conf\n", result.stdout)
        self.assertFalse((self.root / "should-not-exist").exists())
        self.shared.unlink()
        self.assertNotIn("MANGOHUD_CONFIGFILE", self.game(MANGOHUD="1"))

    def test_inactive_unavailable_disabled_and_flatpak_are_isolated(self):
        self.shared.write_text("fps\n")
        for extra in ({}, {"MANGOHUD": "0"}, {"MANGOHUD": "1"},
                      {"MANGOHUD": "1", "DISABLE_MAKO": "1"}):
            with self.subTest(extra=extra):
                env = self.game(MANGOHUD_CONFIGFILE="/inherited", **extra)
                self.assertEqual(env["MANGOHUD_CONFIGFILE"], "/inherited")
                self.assertNotIn("MANGOHUD", env)
                self.assertFalse(self.private.exists())
        self.manifest()
        env = self.game(MANGOHUD="1", FLATPAK_ID="test.app", MANGOHUD_CONFIGFILE="/inherited")
        self.assertNotIn("MANGOHUD", env)
        self.assertEqual(env["MANGOHUD_CONFIGFILE"], "/inherited")
        flatpak = self.root / "flatpak"
        flatpak.write_text("#!/bin/sh\nexec /usr/bin/env\n")
        flatpak.chmod(0o755)
        result = self.run_launcher(str(flatpak), "run", "test.app", MANGOHUD="1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("MANGOHUD=1", result.stdout.splitlines())
        self.assertFalse(self.private.exists())
        self.assertEqual(json.loads(self.run_launcher("--check-mangohud", FLATPAK_ID="test.app").stdout), {"available": False})

    def test_invalid_manifest_shapes_and_gates_are_rejected(self):
        for updates in ({"name": "VK_LAYER_TEST"}, {"type": "INSTANCE"},
                        {"library_arch": "32"}, {"enable_environment": {}},
                        {"disable_environment": {"WRONG": "1"}},
                        {"library_path": "/does/not/exist.so"}, {"library_path": ""}):
            with self.subTest(updates=updates):
                self.manifest(**updates)
                self.assertNotIn("MANGOHUD", self.game(MANGOHUD="1"))
                self.assertFalse(self.private.exists())
        path = self.manifest()
        for content in ("[]", "null", "{bad", "x" * 65537,
                        '{"layers":[{"name":"VK_LAYER_TEST"}]}'):
            path.write_text(content)
            self.assertFalse(json.loads(self.run_launcher("--check-mangohud").stdout)["available"])
        path.unlink()
        os.mkfifo(path)
        self.assertFalse(json.loads(self.run_launcher("--check-mangohud").stdout)["available"])

    def test_relative_library_is_resolved_and_foreign_layers_are_not_staged(self):
        (self.manifests / "libMangoHud.so").write_bytes(b"fixture")
        self.manifest(library_path="./libMangoHud.so")
        (self.manifests / "unrelated.json").write_text('{"layer":{"name":"foreign"}}')
        self.game(MANGOHUD="1")
        files = list(self.private.iterdir())
        self.assertEqual(len(files), 1)
        self.assertEqual(json.loads(files[0].read_text())["layer"]["library_path"], str(self.manifests / "libMangoHud.so"))

    def test_invalid_cache_cannot_admit_foreign_layers(self):
        self.manifest()
        self.private.mkdir(parents=True)
        (self.private / "foreign.json").write_text("{}")
        env = self.game(MANGOHUD="1")
        self.assertNotIn("MANGOHUD", env)
        self.assertNotIn(str(self.private), env["VK_IMPLICIT_LAYER_PATH"])
        self.assertEqual((self.private / "foreign.json").read_text(), "{}")

    def test_unmodified_manifests_are_not_rewritten(self):
        self.manifest()
        self.game(MANGOHUD="1")
        path = self.private / "MangoHud.x86_64.json"
        before = path.stat().st_mtime_ns
        self.game(MANGOHUD="1")
        self.assertEqual(before, path.stat().st_mtime_ns)

    def test_concurrent_launches_share_a_complete_manifest_directory(self):
        self.manifest("64")
        self.manifest("32")
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            results = list(pool.map(lambda _: self.game(MANGOHUD="1"), range(12)))
        for result in results:
            self.assertTrue(result["VK_INSTANCE_LAYERS"].startswith(
                "VK_LAYER_MAKO_render:VK_LAYER_MANGOHUD_overlay_x86_64:VK_LAYER_MANGOHUD_overlay_x86"))
        self.assertEqual(len(list(self.private.iterdir())), 2)

    def test_real_frame_count_orders_both_identities_above_renderer(self):
        self.manifest("64")
        self.manifest("32")
        both = "VK_LAYER_MANGOHUD_overlay_x86_64:VK_LAYER_MANGOHUD_overlay_x86"
        for real, expected in (("0", "VK_LAYER_MAKO_render:" + both),
                               ("1", both + ":VK_LAYER_MAKO_render")):
            with self.subTest(real=real):
                env = self.game(MANGOHUD="1", MAKO_MANGOHUD_REAL_FRAMES=real,
                                VK_INSTANCE_LAYERS="VK_LAYER_TEST")
                self.assertEqual(env["VK_INSTANCE_LAYERS"], expected + ":VK_LAYER_TEST")
                # With both gates set, the Steam Linux Runtime's loader orders
                # by discovery, so MangoHud's directory follows the list.
                self.assertEqual(env["MANGOHUD"], "1")
                self.assertEqual(env["ENABLE_MAKO"], "1")
                implicit = env["VK_IMPLICIT_LAYER_PATH"]
                if real == "1":
                    self.assertTrue(implicit.startswith(str(self.private) + ":"), implicit)
                else:
                    self.assertTrue(implicit.endswith(":" + str(self.private)), implicit)
                self.assertNotIn("MAKO_MANGOHUD_REAL_FRAMES", env)

    def test_invalid_requests_and_shader_conflict_fail_before_game_exec(self):
        for options in ({"MANGOHUD": "yes"}, {"MANGOHUD": "1", "ENABLE_VKBASALT": "1"},
                        {"MANGOHUD": "1", "MAKO_MANGOHUD_REAL_FRAMES": "yes"}):
            result = self.run_launcher("/usr/bin/env", **options)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main()
