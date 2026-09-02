"""Cross-component contracts for MAKO Decky's stable package layout."""

import ast
import json
from pathlib import Path
import re
import unittest

from py_modules.mako_plugin.constants import (
    JSON32_FILENAME,
    JSON_FILENAME,
    LIB_FILENAME,
    LOSSLESS_DLL_NAME,
    LOSSLESS_SCALING_DIRECTORY,
    MAKO_LAYER_BUILD_MARKER,
    MAKO_LAYER_DISABLE_ENV,
    MAKO_LAYER_ENABLE_ENV,
    MAKO_LAYER_NAME,
    MAKO_PROFILE_FALLBACK_MARKER,
    SPATIAL_SCALING_JSON32_FILENAME,
    SPATIAL_SCALING_JSON_FILENAME,
    SPATIAL_SCALING_LAYER_BUILD_MARKER,
    SPATIAL_SCALING_LAYER_DISABLE_ENV,
    SPATIAL_SCALING_LAYER_ENABLE_ENV,
    SPATIAL_SCALING_LAYER_NAME,
    SPATIAL_SCALING_LIB_FILENAME,
    PLUGIN_ROOT,
    STEAM_COMMON_PATH,
)


PLUGIN_DIR = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = PLUGIN_DIR.parent
INSTALLATION_SOURCE = PLUGIN_DIR / "py_modules/mako_plugin/installation.py"
FLATPAK_SOURCE = PLUGIN_DIR / "py_modules/mako_plugin/flatpak_service.py"
DLL_DETECTION_SOURCE = PLUGIN_DIR / "py_modules/mako_plugin/dll_detection.py"
PLUGIN_PACKAGE_SCRIPT = PLUGIN_DIR / "scripts/package-local.sh"
PLUGIN_DEPLOY_SCRIPT = PLUGIN_DIR / "scripts/deploy-dev.sh"
VALIDATED_DEPLOY_SCRIPT = PLUGIN_DIR / "scripts/deploy-validated-package.py"
DECKY_CLIENT_SOURCE = PLUGIN_DIR / "scripts/decky-loader-client.mjs"
ENGINE_PACKAGE_SCRIPT = REPOSITORY_ROOT / "engine/scripts/package-local.sh"
ENGINE_DEV_BUILD_SCRIPT = REPOSITORY_ROOT / "engine/scripts/build-steamos-dev.sh"
ENGINE_LAUNCHER = REPOSITORY_ROOT / "engine/scripts/mako-launch"
ENGINE_CMAKE = REPOSITORY_ROOT / "engine/CMakeLists.txt"
RENDERER_CMAKE = REPOSITORY_ROOT / "engine/mako-render/CMakeLists.txt"
RENDERER_MANIFEST = (
    REPOSITORY_ROOT / "engine/mako-render/VkLayer_MAKO_render.json.in"
)
SPATIAL_SCALING_MANIFEST = (
    REPOSITORY_ROOT /
    "engine/mako-render/VkLayer_MAKO_spatial_scaling.json.in"
)
RENDERER_PATHS_SOURCE = (
    REPOSITORY_ROOT / "engine/mako-common/src/helpers/paths.cpp"
)


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _python_literal_assignment(path: Path, name: str):
    tree = ast.parse(_read(path))
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        if any(
            isinstance(target, ast.Name) and target.id == name
            for target in node.targets
        ):
            return ast.literal_eval(node.value)
    raise AssertionError(f"Missing assignment {name} in {path}")


def _render_string(node: ast.expr, names: dict[str, str]) -> str:
    if isinstance(node, ast.Constant) and isinstance(node.value, str):
        return node.value
    if not isinstance(node, ast.JoinedStr):
        raise AssertionError(
            f"Unsupported archive-member expression: {ast.dump(node)}"
        )

    parts = []
    for value in node.values:
        if isinstance(value, ast.Constant) and isinstance(value.value, str):
            parts.append(value.value)
        elif (
            isinstance(value, ast.FormattedValue)
            and isinstance(value.value, ast.Name)
            and value.value.id in names
        ):
            parts.append(names[value.value.id])
        else:
            raise AssertionError(
                f"Unsupported archive-member interpolation: {ast.dump(value)}"
            )
    return "".join(parts)


def _installer_archive_members() -> tuple[set[str], set[str]]:
    tree = ast.parse(_read(INSTALLATION_SOURCE))
    assignments: dict[str, set[str]] = {}
    names = {
        "LIB_FILENAME": LIB_FILENAME,
        "JSON_FILENAME": JSON_FILENAME,
        "JSON32_FILENAME": JSON32_FILENAME,
        "SPATIAL_SCALING_LIB_FILENAME": SPATIAL_SCALING_LIB_FILENAME,
        "SPATIAL_SCALING_JSON_FILENAME": SPATIAL_SCALING_JSON_FILENAME,
        "SPATIAL_SCALING_JSON32_FILENAME": SPATIAL_SCALING_JSON32_FILENAME,
    }
    for node in ast.walk(tree):
        if not isinstance(node, ast.Assign) or not isinstance(node.value, ast.Dict):
            continue
        for target in node.targets:
            if not isinstance(target, ast.Name) or target.id not in {
                "required_destinations",
                "optional_32bit_destinations",
            }:
                continue
            assignments[target.id] = {
                _render_string(key, names)
                for key in node.value.keys
                if key is not None
            }

    return (
        assignments["required_destinations"],
        assignments["optional_32bit_destinations"],
    )


class PathAndPackageContractTests(unittest.TestCase):
    def test_services_share_the_installed_plugin_root(self):
        self.assertEqual(PLUGIN_ROOT.resolve(), PLUGIN_DIR)
        self.assertTrue((PLUGIN_ROOT / "package.json").is_file())

        installation = _read(INSTALLATION_SOURCE)
        flatpak = _read(FLATPAK_SOURCE)
        for source in (installation, flatpak):
            self.assertNotIn("Path(__file__).parent.parent.parent", source)

        self.assertIn("_bundled_archive_metadata(PLUGIN_ROOT)", installation)
        self.assertIn("PLUGIN_ROOT / BIN_DIR", installation)
        self.assertIn("_diagnostics_helper_source(PLUGIN_ROOT)", installation)
        self.assertIn("_install_diagnostics_helper(PLUGIN_ROOT)", installation)
        self.assertIn("PLUGIN_ROOT / BIN_DIR", flatpak)
        self.assertIn("FLATPAK_RUNTIME_BUNDLES[version].filename", flatpak)

    def test_installer_archive_members_match_both_package_gates(self):
        required, optional_32bit = _installer_archive_members()
        expected_required = {
            f"lib/{LIB_FILENAME}",
            f"share/vulkan/implicit_layer.d/{JSON_FILENAME}",
            f"lib/{SPATIAL_SCALING_LIB_FILENAME}",
            f"share/vulkan/implicit_layer.d/{SPATIAL_SCALING_JSON_FILENAME}",
        }
        expected_optional_32bit = {
            f"lib32/{LIB_FILENAME}",
            f"share/vulkan/implicit_layer.d/{JSON32_FILENAME}",
            f"lib32/{SPATIAL_SCALING_LIB_FILENAME}",
            f"share/vulkan/implicit_layer.d/{SPATIAL_SCALING_JSON32_FILENAME}",
        }
        self.assertEqual(required, expected_required)
        self.assertEqual(optional_32bit, expected_optional_32bit)

        engine_package = _read(ENGINE_PACKAGE_SCRIPT)
        plugin_package = _read(PLUGIN_PACKAGE_SCRIPT)
        for member in expected_required | expected_optional_32bit:
            self.assertIn(f'"{member}"', engine_package)
            self.assertIn(f'"./{member}"', plugin_package)

        self.assertIn(
            f"set(MAKO_LAYER_LIBRARY_PATH {LIB_FILENAME}",
            _read(ENGINE_CMAKE),
        )
        self.assertIn("add_library(mako-render SHARED", _read(RENDERER_CMAKE))
        self.assertIn(
            "add_library(mako-render-scaling SHARED", _read(RENDERER_CMAKE)
        )
        self.assertEqual(LIB_FILENAME, "libmako-render.so")
        self.assertEqual(
            SPATIAL_SCALING_LIB_FILENAME, "libmako-render-scaling.so"
        )

    def test_direct_development_builds_keep_private_layer_paths_resolvable(self):
        dev_build = _read(ENGINE_DEV_BUILD_SCRIPT)
        self.assertIn('local install_libdir=lib', dev_build)
        self.assertIn('install_libdir=lib32', dev_build)
        self.assertIn(
            '-DMAKO_LAYER_LIBRARY_PATH="../$install_libdir/'
            f'{LIB_FILENAME}"',
            dev_build,
        )
        self.assertIn(
            '-DMAKO_SCALING_LAYER_LIBRARY_PATH="../$install_libdir/'
            f'{SPATIAL_SCALING_LIB_FILENAME}"',
            dev_build,
        )

        deploy = _read(PLUGIN_DEPLOY_SCRIPT)
        self.assertIn(
            'built_spatial_manifest_64="$engine_build_dir/mako-render/'
            'private-scaling-manifest/',
            deploy,
        )
        self.assertIn(
            'copy_file "$built_spatial_manifest_64" '
            '"$installed_spatial_manifest_64"',
            deploy,
        )
        self.assertIn("verify_private_layer_manifest()", deploy)
        self.assertIn(
            '"$built_spatial_manifest_64" "$installed_spatial_manifest_64"',
            deploy,
        )
        self.assertIn(
            '"$built_spatial_manifest_32" "$installed_spatial_manifest_32"',
            deploy,
        )

    def test_private_manifests_share_the_architecture_suffix_contract(self):
        manifest_stem = Path(JSON_FILENAME).stem
        self.assertEqual(JSON32_FILENAME, f"{manifest_stem}.x86.json")

        private_manifest_dir = "share/mako-render/vulkan/implicit_layer.d"
        engine_package = _read(ENGINE_PACKAGE_SCRIPT)
        for filename in (JSON_FILENAME, JSON32_FILENAME):
            self.assertIn(f'"{private_manifest_dir}/{filename}"', engine_package)

        private_scaling_manifest_dir = (
            "share/mako-render/vulkan/spatial_scaling.d"
        )
        for filename in (
            SPATIAL_SCALING_JSON_FILENAME,
            SPATIAL_SCALING_JSON32_FILENAME,
        ):
            self.assertIn(
                f'"{private_scaling_manifest_dir}/{filename}"',
                engine_package,
            )

        renderer_cmake = _read(RENDERER_CMAKE)
        self.assertIn(
            f'"{manifest_stem}${{MAKO_LAYER_MANIFEST_SUFFIX}}.json"',
            renderer_cmake,
        )
        self.assertIn(
            '"${CMAKE_CURRENT_BINARY_DIR}/private-manifest/${MAKO_LAYER_MANIFEST_FILE}"',
            renderer_cmake,
        )
        self.assertIn(
            '"${CMAKE_INSTALL_DATAROOTDIR}/mako-render/vulkan/implicit_layer.d"',
            renderer_cmake,
        )
        self.assertIn(
            '"${CMAKE_INSTALL_DATAROOTDIR}/mako-render/vulkan/spatial_scaling.d"',
            renderer_cmake,
        )
        self.assertIn("-DMAKO_LAYER_MANIFEST_SUFFIX=.x86", engine_package)
        self.assertIn(
            "share/mako-render/vulkan/implicit_layer.d",
            _read(ENGINE_LAUNCHER),
        )

    def test_manifest_identity_gates_and_binary_markers_are_cross_checked(self):
        manifest = json.loads(_read(RENDERER_MANIFEST))
        layer = manifest["layer"]
        self.assertEqual(layer["name"], MAKO_LAYER_NAME)
        self.assertEqual(
            layer["enable_environment"], {MAKO_LAYER_ENABLE_ENV: "1"}
        )
        self.assertEqual(
            layer["disable_environment"], {MAKO_LAYER_DISABLE_ENV: "1"}
        )
        scaling_manifest = json.loads(_read(SPATIAL_SCALING_MANIFEST))
        scaling_layer = scaling_manifest["layer"]
        self.assertEqual(scaling_layer["name"], SPATIAL_SCALING_LAYER_NAME)
        self.assertEqual(
            scaling_layer["enable_environment"],
            {SPATIAL_SCALING_LAYER_ENABLE_ENV: "1"},
        )
        self.assertEqual(
            scaling_layer["disable_environment"],
            {SPATIAL_SCALING_LAYER_DISABLE_ENV: "1"},
        )

        identity_fragments = (
            f'"name": "{MAKO_LAYER_NAME}"',
            f'"{MAKO_LAYER_ENABLE_ENV}": "1"',
            f'"{MAKO_LAYER_DISABLE_ENV}": "1"',
        )
        marker_prefix = MAKO_LAYER_BUILD_MARKER.decode("ascii")
        scaling_marker_prefix = SPATIAL_SCALING_LAYER_BUILD_MARKER.decode(
            "ascii"
        )
        fallback_marker = MAKO_PROFILE_FALLBACK_MARKER.decode("ascii")
        for path in (ENGINE_PACKAGE_SCRIPT, PLUGIN_PACKAGE_SCRIPT):
            source = _read(path)
            for fragment in identity_fragments:
                self.assertIn(fragment, source)
            if path == ENGINE_PACKAGE_SCRIPT:
                self.assertIn(
                    "MAKO Renderer: render layer active; "
                    "identity=$expected_identity; build=$version",
                    source,
                )
            else:
                self.assertIn(
                    "MAKO Renderer: render layer active; "
                    "identity=$expected_identity; build=$archive_version",
                    source,
                )
            self.assertIn(fallback_marker, source)

        self.assertTrue(marker_prefix.startswith(
            "MAKO Renderer: render layer active; identity="
        ))
        self.assertTrue(scaling_marker_prefix.startswith(
            "MAKO Renderer: render layer active; identity="
        ))

        installation = _read(INSTALLATION_SOURCE)
        for symbol in (
            "MAKO_LAYER_NAME",
            "MAKO_LAYER_ENABLE_ENV",
            "MAKO_LAYER_DISABLE_ENV",
            "MAKO_LAYER_BUILD_MARKER",
            "SPATIAL_SCALING_LAYER_BUILD_MARKER",
            "MAKO_PROFILE_FALLBACK_MARKER",
        ):
            self.assertIn(symbol, installation)

    def test_lossless_scaling_directory_and_dll_identity_are_shared(self):
        relative_dll = LOSSLESS_SCALING_DIRECTORY / LOSSLESS_DLL_NAME
        self.assertEqual(relative_dll, Path("Lossless Scaling/Lossless.dll"))
        self.assertEqual(
            STEAM_COMMON_PATH,
            Path("steamapps/common") / LOSSLESS_SCALING_DIRECTORY,
        )
        detection = _read(DLL_DETECTION_SOURCE)
        self.assertIn("STEAM_COMMON_PATH", detection)
        self.assertIn("LOSSLESS_DLL_NAME", detection)

        renderer_paths = _read(RENDERER_PATHS_SOURCE)
        renderer_relative_dll = (
            f'/ "{LOSSLESS_SCALING_DIRECTORY.as_posix()}" '
            f'/ "{LOSSLESS_DLL_NAME}"'
        )
        self.assertEqual(renderer_paths.count(renderer_relative_dll), 2)
        self.assertIn(
            f'current_path() / "{LOSSLESS_DLL_NAME}"',
            renderer_paths,
        )

    def test_decky_slug_listing_and_developer_aliases_match_independent_tools(self):
        slug = "Mako"
        listing_name = json.loads(_read(PLUGIN_DIR / "plugin.json"))["name"]
        self.assertEqual(listing_name, "MAKO - Frame Generation")

        package_script = _read(PLUGIN_PACKAGE_SCRIPT)
        established_identity = re.search(
            r'^established_decky_identity="([^"]+)"$',
            package_script,
            re.MULTILINE,
        )
        self.assertIsNotNone(established_identity)
        self.assertEqual(established_identity.group(1), listing_name)
        self.assertIn(
            'manifest_decky_identity" != "$established_decky_identity',
            package_script,
        )
        package_slug = re.search(
            r'^package_name="([^"]+)"$', package_script, re.MULTILINE
        )
        self.assertIsNotNone(package_slug)
        self.assertEqual(package_slug.group(1), slug)

        validated_slug = _python_literal_assignment(
            VALIDATED_DEPLOY_SCRIPT, "PACKAGE_ROOT"
        )
        supported_names = _python_literal_assignment(
            VALIDATED_DEPLOY_SCRIPT, "SUPPORTED_PLUGIN_NAMES"
        )
        developer_aliases = {
            "MAKO Decky",
            "MAKO",
            "Mako",
        }
        self.assertEqual(validated_slug, slug)
        self.assertEqual(supported_names, developer_aliases | {listing_name})

        deploy_script = _read(PLUGIN_DEPLOY_SCRIPT)
        self.assertIn(f"homebrew/plugins/{slug}", deploy_script)
        for name in supported_names:
            self.assertIn(f'"$plugin_name" != "{name}"', deploy_script)

        client = _read(DECKY_CLIENT_SOURCE)
        self.assertIn(f'const DEFAULT_PLUGIN_NAME = "{listing_name}";', client)
        self.assertIn(f"homebrew/plugins/{slug}", client)


if __name__ == "__main__":
    unittest.main()
