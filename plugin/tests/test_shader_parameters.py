"""Typed parameter validation and atomic profile persistence regression coverage."""
import copy
import json
import os
from pathlib import Path
import subprocess
import threading
import unittest
from unittest.mock import patch

import test_game_profiles as profiles_fixture
from py_modules.mako_plugin import shader_parameters, profile_storage

KEY = "makoShader_546f6e65_537472656e677468_0"
PARAMETER = dict(key=KEY, label="Strength", description="", category="", type="float", default=0.5, value=0.5, min=0, max=1, step=0.01, items=[])
EFFECTS = [dict(id="Tone", name="Tone", path="/tmp/tone.fx", error=None, parameters=[PARAMETER])]


class ShaderValidationTests(unittest.TestCase):
    def test_validation_rejects_injection_nonfinite_unknown_fractional_and_out_of_range(self):
        for changes in ({"effects": 1}, {KEY: float("nan")}, {KEY: float("inf")}, {KEY: True}, {KEY: "1\neffects = other"}, {KEY: 2}, {}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                shader_parameters.validate_changes(EFFECTS, changes)
        self.assertEqual(shader_parameters.validate_changes(EFFECTS, {KEY: None}), {KEY: "default"})
        integer = copy.deepcopy(EFFECTS)
        integer[0]["parameters"][0]["type"] = "int"
        with self.assertRaises(ValueError): shader_parameters.validate_changes(integer, {KEY: 0.5})
        self.assertEqual(shader_parameters.validate_changes(integer, {KEY: 1}), {KEY: "1"})

    def test_reset_restores_authored_default_even_outside_the_ui_range(self):
        effects = copy.deepcopy(EFFECTS)
        effects[0]["parameters"][0]["min"] = 0.75
        self.assertEqual(shader_parameters.validate_changes(effects, {KEY: None}), {KEY: "default"})
        with self.assertRaises(ValueError): shader_parameters.validate_changes(effects, {KEY: 0.5})

    def test_merge_preserves_unknown_options_comments_and_removes_duplicate_overrides(self):
        source = f'# header\neffects = Tone:Other\n  {KEY} = 0.2 # note\n{KEY} = 0.9\nOther = "shader #2.fx"\n'
        result = profile_storage.update_shader_parameters(source, {KEY: "0.7"})
        self.assertEqual(result, f'# header\neffects = Tone:Other\n  {KEY} = 0.7 # note\nOther = "shader #2.fx"\n')
        for values in ({"effects": "Tone"}, {KEY: "1\nOther = 0"}):
            with self.assertRaises(ValueError): profile_storage.update_shader_parameters(source, values)

    def test_discovery_validates_protocol_and_retains_one_shader_failure(self):
        process = subprocess.CompletedProcess([], 0, json.dumps({"schema": 1, "effects": EFFECTS}), "")
        with patch.object(Path, "is_file", return_value=True), patch.object(subprocess, "run", return_value=process):
            self.assertEqual(shader_parameters.discover(Path("tool"), Path("config")), EFFECTS)
            process.stdout = json.dumps({"schema": 2, "effects": EFFECTS})
            with self.assertRaises(ValueError): shader_parameters.discover(Path("tool"), Path("config"))

    def test_external_helper_uses_host_libraries_without_mutating_decky(self):
        process = subprocess.CompletedProcess([], 0, json.dumps({"schema": 1, "effects": EFFECTS}), "")
        for original in (None, "", "/opt/native/lib:/usr/local/lib"):
            environment = {"LD_LIBRARY_PATH": "/tmp/_MEI-decky", "HOME": "/home/test", "PATH": "/usr/bin", "VKBASALT_LOG_LEVEL": "debug"}
            if original is not None:
                environment["LD_LIBRARY_PATH_ORIG"] = original
            with self.subTest(original=original), patch.dict(os.environ, environment, clear=True), patch.object(Path, "is_file", return_value=True), patch.object(subprocess, "run", return_value=process) as run:
                self.assertEqual(shader_parameters.discover(Path("/native/tool"), Path("/config/shader.conf")), EFFECTS)
                child = run.call_args.kwargs["env"]
                self.assertEqual(child.get("LD_LIBRARY_PATH"), original or None)
                self.assertNotIn("LD_LIBRARY_PATH_ORIG", child)
                self.assertEqual(child["HOME"], environment["HOME"])
                self.assertEqual(child["PATH"], environment["PATH"])
                self.assertEqual(child["VKBASALT_LOG_LEVEL"], "none")
                self.assertEqual(child["VKBASALT_LOG_FILE"], "stderr")
                self.assertEqual(dict(os.environ), environment)
                self.assertEqual(run.call_args.args[0], ["/native/tool", "/config/shader.conf"])

    def test_deleting_an_import_removes_only_its_own_parameter_values(self):
        other = KEY.replace("546f6e65", "4f74686572")
        source = f'effects = Tone:Other\nTone = /tone.fx\nOther = /other.fx\n{KEY} = 0.7\n{other} = 0.4\nStrength = 0.6\n'
        result = profile_storage.remove_custom_shader_content(source, {"custom/Tone"})
        self.assertNotIn(KEY, result)
        self.assertIn(f"{other} = 0.4", result)
        self.assertIn("Strength = 0.6", result)
        self.assertIn("effects = Other", result)

    def test_additive_presentation_is_validated_without_breaking_older_helpers(self):
        effects = copy.deepcopy(EFFECTS)
        hint = dict(widget="color", group=KEY, label="Tint", component=0, count=3)
        effects[0]["parameters"][0]["presentation"] = hint
        process = subprocess.CompletedProcess([], 0, "", "")
        with patch.object(Path, "is_file", return_value=True), patch.object(subprocess, "run", return_value=process):
            process.stdout = json.dumps({"schema": 1, "effects": effects})
            self.assertEqual(shader_parameters.discover(Path("tool"), Path("config")), effects)
            for invalid in (None, {}, {**hint, "widget": "guess"}, {**hint, "count": True}, {**hint, "component": 3}, {**hint, "group": "effects"}):
                effects[0]["parameters"][0]["presentation"] = invalid
                process.stdout = json.dumps({"schema": 1, "effects": effects})
                self.assertNotIn("presentation", shader_parameters.discover(Path("tool"), Path("config"))[0]["parameters"][0])

    def test_author_reset_policy_and_float32_boundaries(self):
        effects = copy.deepcopy(EFFECTS)
        p = effects[0]["parameters"][0]
        p["resettable"] = False
        with self.assertRaises(ValueError): shader_parameters.validate_changes(effects, {KEY: None})
        p["min"] = .100000001
        p["max"] = .899999976
        self.assertEqual(shader_parameters.validate_changes(effects, {KEY: .1}), {KEY: "0.1"})
        self.assertEqual(shader_parameters.validate_changes(effects, {KEY: .9}), {KEY: "0.9"})
        for value in (.099, .901, 1e100):
            with self.assertRaises(ValueError): shader_parameters.validate_changes(effects, {KEY: value})

    def test_discovery_rejects_ambiguous_identities_types_and_missing_fields(self):
        source = dict(schema=1, effects=copy.deepcopy(EFFECTS))
        invalid = []
        for field, value in (("key", KEY.replace("546f6e65", "41")), ("type", "double"), ("value", True), ("default", "0.5"), ("items", {}), ("resettable", "false"), ("label", [])):
            data = copy.deepcopy(source); data["effects"][0]["parameters"][0][field] = value; invalid.append(data)
        data = copy.deepcopy(source); data["effects"][0]["parameters"] *= 2; invalid.append(data)
        data = copy.deepcopy(source); data["effects"] *= 2; invalid.append(data)
        data = copy.deepcopy(source); data["effects"][0]["parameters"] = {}; invalid.append(data)
        data = copy.deepcopy(source); data["effects"][0]["parameters"][0]["type"] = "uint"; invalid.append(data)
        for data in invalid:
            with self.subTest(data=data), self.assertRaises(ValueError): shader_parameters.normalize_metadata(data)
        source["schema"] = True
        with self.assertRaises(ValueError): shader_parameters.normalize_metadata(source)

    def test_older_presentation_buttons_are_not_reintroduced_as_toggles(self):
        source = dict(schema=1, effects=copy.deepcopy(EFFECTS))
        p = source["effects"][0]["parameters"][0]
        p.update(type="bool", value=0, default=0, presentation=dict(widget="button", group=KEY, label="Capture", count=1, component=0))
        self.assertEqual(shader_parameters.normalize_metadata(source)[0]["parameters"], [])


# Reuse the established profile fixture without inheriting and rerunning its suite.
class ShaderPersistenceTests(unittest.TestCase):
    setUp = profiles_fixture.GameProfileTests.setUp

    def test_shader_metadata_uses_selected_renderer_without_other_owner_fallback(self):
        from py_modules.mako_plugin.constants import JSON_FILENAME, LIB_FILENAME, SHADER_TOOL_FILENAME
        self.service.standalone_install_prefix = self.service.config_dir.parent / ".local"
        manifest = self.service.local_share_dir / JSON_FILENAME
        manifest.parent.mkdir(parents=True, exist_ok=True)
        manifest.write_text(json.dumps({"layer": {
            "library_path": str(self.service.standalone_install_prefix / "lib" / LIB_FILENAME),
        }}))
        with patch.object(shader_parameters, "discover", return_value=copy.deepcopy(EFFECTS)) as discover:
            self.assertTrue(self.service.get_shader_parameters("mako")["success"])
            self.assertEqual(discover.call_args.args[0], self.service.standalone_install_prefix / "bin" / SHADER_TOOL_FILENAME)

    def prepare(self):
        path = self.service._vkbasalt_config_path("mako")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('effects = Tone\nTone = /tmp/tone.fx\n')
        with patch.object(shader_parameters, "discover", return_value=copy.deepcopy(EFFECTS)):
            result = self.service.get_shader_parameters("mako")
            self.assertTrue(result["success"], result)
        return path

    def test_save_reset_and_other_profile_isolation(self):
        path = self.prepare()
        before = self.service.config_file_path.read_bytes()
        result = self.service.set_shader_parameters("mako", {KEY: 0.8})
        self.assertTrue(result["success"], result)
        self.assertIn(KEY + " = 0.8", path.read_text())
        self.assertEqual(self.service.config_file_path.read_bytes(), before)
        self.assertFalse(self.service.set_shader_parameters("missing", {KEY: 0.4})["success"])
        self.assertTrue(self.service.set_shader_parameters("mako", {KEY: None})["success"])
        self.assertIn(KEY + " = default", path.read_text())

    def test_external_edit_blocks_stale_editor_and_failed_projection_rolls_back(self):
        path = self.prepare()
        original = path.read_text()
        with patch.object(self.service, "_write_selected_shader_config", side_effect=OSError("disk full")):
            self.assertFalse(self.service.set_shader_parameters("mako", {KEY: 0.8})["success"])
        self.assertEqual(path.read_text(), original)
        self.assertTrue(self.service.set_shader_parameters("mako", {KEY: 0.8})["success"])
        path.write_text(original + "# External edit\n")
        self.assertFalse(self.service.set_shader_parameters("mako", {KEY: 0.3})["success"])
        self.assertTrue(path.read_text().endswith("# External edit\n"))

    def test_external_edit_during_discovery_rejects_the_snapshot(self):
        path = self.prepare()
        def changed(*args):
            path.write_text(path.read_text() + "# External edit\n")
            return copy.deepcopy(EFFECTS)
        with patch.object(shader_parameters, "discover", side_effect=changed):
            self.assertFalse(self.service.get_shader_parameters("mako")["success"])
        self.assertFalse(self.service.set_shader_parameters("mako", {KEY: 0.3})["success"])

    def test_discovery_does_not_hold_the_profile_write_lock(self):
        self.prepare()
        acquired = []
        def discover(*args):
            def check_lock():
                locked = self.service._configuration_write_lock.acquire(blocking=False)
                acquired.append(locked)
                if locked: self.service._configuration_write_lock.release()
            worker = threading.Thread(target=check_lock)
            worker.start()
            worker.join(timeout=1)
            return copy.deepcopy(EFFECTS)
        with patch.object(shader_parameters, "discover", side_effect=discover):
            self.assertTrue(self.service.get_shader_parameters("mako")["success"])
        self.assertEqual(acquired, [True])

    def test_failed_refresh_invalidates_previous_controls(self):
        path = self.prepare()
        before = path.read_bytes()
        with patch.object(shader_parameters, "discover", side_effect=ValueError("Invalid metadata")):
            self.assertFalse(self.service.get_shader_parameters("mako")["success"])
        self.assertFalse(self.service.set_shader_parameters("mako", {KEY: .3})["success"])
        self.assertEqual(path.read_bytes(), before)

    def test_older_discovery_cannot_replace_newer_controls(self):
        self.prepare()
        newer = copy.deepcopy(EFFECTS)
        newer[0]["parameters"][0]["max"] = .6
        def old_discovery(*args):
            with patch.object(shader_parameters, "discover", return_value=newer):
                self.assertTrue(self.service.get_shader_parameters("mako")["success"])
            return copy.deepcopy(EFFECTS)
        with patch.object(shader_parameters, "discover", side_effect=old_discovery):
            self.assertFalse(self.service.get_shader_parameters("mako")["success"])
        self.assertFalse(self.service.set_shader_parameters("mako", {KEY: .8})["success"])
        self.assertTrue(self.service.set_shader_parameters("mako", {KEY: .6})["success"])
