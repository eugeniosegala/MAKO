"""Cross-language checks for MAKO Decky's public RPC method names."""

import ast
from pathlib import Path
import re
import sys
from types import SimpleNamespace
from typing import get_args, get_type_hints
import unittest


class _Logger:
    def __getattr__(self, _name):
        return lambda *_args, **_kwargs: None


sys.modules.setdefault("decky", SimpleNamespace(logger=_Logger()))

from py_modules.mako_plugin.flatpak_service import (  # noqa: E402
    FlatpakApp,
    FlatpakAppInfo,
    FlatpakOverrideResponse,
)
from py_modules.mako_plugin.types import (  # noqa: E402
    ConfigSchemaResponse,
    ConfigurationResponse,
    DllDetectionResponse,
    DllStatsResponse,
    FgmodCheckResponse,
    FileContentResponse,
    InstallationCheckResponse,
    InstallationResult,
    LaunchOptionResponse,
    ProfileDetails,
    ProfileResponse,
    ProfilesResponse,
    RuntimeContextState,
    RuntimePendingState,
    RuntimeProfileSnapshot,
    RuntimeSpatialScalingState,
    RuntimeStatusResponse,
)


PLUGIN_ROOT = Path(__file__).resolve().parents[1]


class RpcContractTests(unittest.TestCase):
    @staticmethod
    def _frontend_interface_contract(source: str, name: str):
        match = re.search(
            rf"export interface {name}\s*\{{(.*?)\n\}}",
            source,
            re.DOTALL,
        )
        if match is None:
            raise AssertionError(f"frontend interface {name} was not found")
        return {
            field_match.group(1): {
                "optional": bool(field_match.group(2)),
                "nullable": (
                    "Nullable<" in field_match.group(3)
                    or re.search(r"(?:^|\W)null(?:$|\W)", field_match.group(3))
                    is not None
                ),
            }
            for field_match in re.finditer(
                r"^\s+([a-zA-Z_][a-zA-Z0-9_]*)(\?)?:\s*([^;]+);",
                match.group(1),
                re.MULTILINE,
            )
        }

    @staticmethod
    def _annotation_is_nullable(annotation) -> bool:
        return type(None) in get_args(annotation)

    def test_frontend_bindings_match_backend_public_async_methods(self):
        backend_tree = ast.parse(
            (PLUGIN_ROOT / "py_modules/mako_plugin/plugin.py").read_text(
                encoding="utf-8"
            )
        )
        plugin_class = next(
            node
            for node in backend_tree.body
            if isinstance(node, ast.ClassDef) and node.name == "Plugin"
        )
        backend_methods = {
            node.name
            for node in plugin_class.body
            if isinstance(node, ast.AsyncFunctionDef)
            and not node.name.startswith("_")
        }

        frontend_source = (
            PLUGIN_ROOT / "src/api/makoApi.ts"
        ).read_text(encoding="utf-8")
        frontend_bindings = re.findall(
            r'callable(?:<[^;]*?>)?\s*\(\s*"([a-z0-9_]+)"',
            frontend_source,
            re.DOTALL,
        )

        self.assertEqual(len(frontend_bindings), len(set(frontend_bindings)))
        self.assertEqual(set(frontend_bindings), backend_methods)

    def test_shared_response_field_names_match_frontend_interfaces(self):
        frontend_source = (
            PLUGIN_ROOT / "src/api/makoApi.ts"
        ).read_text(encoding="utf-8")
        response_pairs = {
            InstallationResult: "InstallationResult",
            InstallationCheckResponse: "InstallationStatus",
            DllDetectionResponse: "DllDetectionResult",
            DllStatsResponse: "DllStatsResult",
            ConfigurationResponse: "ConfigResult",
            ConfigSchemaResponse: "ConfigSchemaResult",
            LaunchOptionResponse: "LaunchOptionResult",
            FileContentResponse: "FileContentResult",
            FgmodCheckResponse: "FgmodCheckResult",
            ProfileDetails: "ProfileDetails",
            ProfilesResponse: "ProfilesResult",
            ProfileResponse: "ProfileResult",
            RuntimeProfileSnapshot: "RuntimeProfileSnapshot",
            RuntimePendingState: "RuntimePendingState",
            RuntimeSpatialScalingState: "RuntimeSpatialScalingState",
            RuntimeContextState: "RuntimeContextState",
            RuntimeStatusResponse: "RuntimeStatusResult",
            FlatpakApp: "FlatpakApp",
            FlatpakAppInfo: "FlatpakAppInfo",
            FlatpakOverrideResponse: "FlatpakOperationResult",
        }

        for backend_response, frontend_interface in response_pairs.items():
            with self.subTest(frontend_interface=frontend_interface):
                frontend_contract = self._frontend_interface_contract(
                    frontend_source,
                    frontend_interface,
                )
                frontend_fields = set(frontend_contract)
                self.assertEqual(
                    frontend_fields,
                    set(backend_response.__annotations__),
                )
                self.assertEqual(
                    {
                        field_name
                        for field_name, contract in frontend_contract.items()
                        if not contract["optional"]
                    },
                    set(backend_response.__required_keys__),
                )
                self.assertEqual(
                    {
                        field_name
                        for field_name, contract in frontend_contract.items()
                        if contract["optional"]
                    },
                    set(backend_response.__optional_keys__),
                )
                backend_hints = get_type_hints(backend_response)
                self.assertEqual(
                    {
                        field_name
                        for field_name, annotation in backend_hints.items()
                        if self._annotation_is_nullable(annotation)
                    },
                    {
                        field_name
                        for field_name, contract in frontend_contract.items()
                        if contract["nullable"]
                    },
                )


if __name__ == "__main__":
    unittest.main()
