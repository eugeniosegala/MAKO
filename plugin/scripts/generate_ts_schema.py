#!/usr/bin/env python3
"""
Generate TypeScript schema from Python shared_config.py

This script reads the canonical schema from shared_config.py and generates
the corresponding TypeScript files, ensuring single source of truth.
"""

import sys
from pathlib import Path

# Add project root to path to import shared_config
project_root = Path(__file__).parent.parent
sys.path.insert(0, str(project_root))

from shared_config import (
    ADAPTIVE_MAX_MULTIPLIER_MAX,
    ADAPTIVE_MAX_MULTIPLIER_MIN,
    ADAPTIVE_MINIMUM_BASE_FPS,
    BASE_FPS_CAP_MAX,
    BASE_FPS_CAP_MIN,
    BASE_FPS_CAP_UI_MAX,
    CONFIG_SCHEMA_DEF,
    DEFAULT_PROFILE_NAME,
    DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_MAX,
    DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_MIN,
    EXTERNAL_VULKAN_LAYER_GAMESCOPE_WSI,
    EXTERNAL_VULKAN_LAYER_MANGOHUD,
    EXTERNAL_VULKAN_LAYER_NONE,
    EXTERNAL_VULKAN_LAYER_VKBASALT,
    FIXED_MULTIPLIER_MIN,
    FIXED_MULTIPLIER_UI_MAX,
    FIXED_MULTIPLIER_UI_MIN,
    FLOW_SCALE_MAX,
    FLOW_SCALE_MIN,
    FRAME_GENERATION_REFRESH_THRESHOLD_MAX,
    FRAME_GENERATION_REFRESH_THRESHOLD_MIN,
    FRAME_GENERATION_REFRESH_THRESHOLD_PRESET,
    FRAME_GENERATION_REFRESH_THRESHOLD_UI_MIN,
    MAKO_WRAPPER_RELATIVE_PATH,
    PER_GAME_WRAPPER_FLATPAK_APP_IDS,
    PROFILE_KIND_VALUES,
    SUPPORTED_FLATPAK_RUNTIME_VERSIONS,
    TARGET_FPS_MAX,
    TARGET_FPS_MIN,
    ULTRA_PERFORMANCE_FLOW_SCALE,
    ConfigFieldType,
)


def generate_typescript_schema():
    """Generate generatedConfigSchema.ts from Python schema"""

    runtime_descriptors = []
    for version in SUPPORTED_FLATPAK_RUNTIME_VERSIONS:
        major, separator, minor = version.partition(".")
        if not separator or not major.isdigit() or not minor.isdigit():
            raise ValueError(f"Invalid Flatpak runtime version: {version}")
        status_field = f"installed_{major}_{minor}"
        i18n_key = "FLATPAK_RUNTIME_VERSION"
        runtime_descriptors.append(
            "  { "
            f'version: "{version}", '
            f'statusField: "{status_field}", '
            f'i18nKey: "{i18n_key}" '
            "},"
        )

    profile_kind_constants = [
        f'export const PROFILE_KIND_{kind.upper()} = "{kind}" as const;'
        for kind in PROFILE_KIND_VALUES
    ]
    profile_kind_references = [
        f"  PROFILE_KIND_{kind.upper()},"
        for kind in PROFILE_KIND_VALUES
    ]

    # Generate field name constants
    field_constants = []
    for field_name in CONFIG_SCHEMA_DEF.keys():
        const_name = field_name.upper()
        field_constants.append(f'export const {const_name} = "{field_name}" as const;')

    # Generate enum
    enum_lines = [
        "// src/config/generatedConfigSchema.ts",
        "// Stable cross-language profile contract",
        f'export const DEFAULT_PROFILE_NAME = "{DEFAULT_PROFILE_NAME}" as const;',
        "export const MAKO_WRAPPER_RELATIVE_PATH = "
        f'"{MAKO_WRAPPER_RELATIVE_PATH}" as const;',
        "export const PER_GAME_WRAPPER_FLATPAK_APP_IDS = [",
        *(
            f'  "{app_id}",'
            for app_id in PER_GAME_WRAPPER_FLATPAK_APP_IDS
        ),
        "] as const;",
        *profile_kind_constants,
        "export const PROFILE_KIND_VALUES = [",
        *profile_kind_references,
        "] as const;",
        "export type ProfileKind = (typeof PROFILE_KIND_VALUES)[number];",
        "",
        "// Ordered Flatpak runtime contract generated from shared_config.py",
        "export const SUPPORTED_FLATPAK_RUNTIMES = [",
        *runtime_descriptors,
        "] as const;",
        "export type FlatpakRuntimeVersion =",
        "  (typeof SUPPORTED_FLATPAK_RUNTIMES)[number][\"version\"];",
        "export type FlatpakRuntimeStatusField =",
        "  (typeof SUPPORTED_FLATPAK_RUNTIMES)[number][\"statusField\"];",
        "export type FlatpakRuntimeI18nKey =",
        "  (typeof SUPPORTED_FLATPAK_RUNTIMES)[number][\"i18nKey\"];",
        "",
        "// Shared backend validation and Decky UI limits",
        f"export const BASE_FPS_CAP_MIN = {BASE_FPS_CAP_MIN} as const;",
        f"export const BASE_FPS_CAP_MAX = {BASE_FPS_CAP_MAX} as const;",
        f"export const BASE_FPS_CAP_UI_MAX = {BASE_FPS_CAP_UI_MAX} as const;",
        f"export const TARGET_FPS_MIN = {TARGET_FPS_MIN} as const;",
        f"export const TARGET_FPS_MAX = {TARGET_FPS_MAX} as const;",
        "export const ADAPTIVE_MAX_MULTIPLIER_MIN = "
        f"{ADAPTIVE_MAX_MULTIPLIER_MIN} as const;",
        "export const ADAPTIVE_MAX_MULTIPLIER_MAX = "
        f"{ADAPTIVE_MAX_MULTIPLIER_MAX} as const;",
        "export const ADAPTIVE_MINIMUM_BASE_FPS = "
        f"{ADAPTIVE_MINIMUM_BASE_FPS} as const;",
        "export const DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_MIN = "
        f"{DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_MIN} as const;",
        "export const DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_MAX = "
        f"{DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS_MAX} as const;",
        f"export const FLOW_SCALE_MIN = {FLOW_SCALE_MIN} as const;",
        f"export const FLOW_SCALE_MAX = {FLOW_SCALE_MAX} as const;",
        "export const ULTRA_PERFORMANCE_FLOW_SCALE = "
        f"{ULTRA_PERFORMANCE_FLOW_SCALE} as const;",
        f"export const FIXED_MULTIPLIER_MIN = {FIXED_MULTIPLIER_MIN} as const;",
        "export const FIXED_MULTIPLIER_UI_MIN = "
        f"{FIXED_MULTIPLIER_UI_MIN} as const;",
        "export const FIXED_MULTIPLIER_UI_MAX = "
        f"{FIXED_MULTIPLIER_UI_MAX} as const;",
        "export const FRAME_GENERATION_REFRESH_THRESHOLD_MIN = "
        f"{FRAME_GENERATION_REFRESH_THRESHOLD_MIN} as const;",
        "export const FRAME_GENERATION_REFRESH_THRESHOLD_MAX = "
        f"{FRAME_GENERATION_REFRESH_THRESHOLD_MAX} as const;",
        "export const FRAME_GENERATION_REFRESH_THRESHOLD_UI_MIN = "
        f"{FRAME_GENERATION_REFRESH_THRESHOLD_UI_MIN} as const;",
        "export const FRAME_GENERATION_REFRESH_THRESHOLD_PRESET = "
        f"{FRAME_GENERATION_REFRESH_THRESHOLD_PRESET} as const;",
        "",
        "// Stable persisted values for the optional external Vulkan layer",
        "export const EXTERNAL_VULKAN_LAYER_NONE = "
        f'"{EXTERNAL_VULKAN_LAYER_NONE}" as const;',
        "export const EXTERNAL_VULKAN_LAYER_GAMESCOPE_WSI = "
        f'"{EXTERNAL_VULKAN_LAYER_GAMESCOPE_WSI}" as const;',
        "export const EXTERNAL_VULKAN_LAYER_MANGOHUD = "
        f'"{EXTERNAL_VULKAN_LAYER_MANGOHUD}" as const;',
        "export const EXTERNAL_VULKAN_LAYER_VKBASALT = "
        f'"{EXTERNAL_VULKAN_LAYER_VKBASALT}" as const;',
        "export const EXTERNAL_VULKAN_LAYER_VALUES = [",
        "  EXTERNAL_VULKAN_LAYER_NONE,",
        "  EXTERNAL_VULKAN_LAYER_GAMESCOPE_WSI,",
        "  EXTERNAL_VULKAN_LAYER_MANGOHUD,",
        "  EXTERNAL_VULKAN_LAYER_VKBASALT,",
        "] as const;",
        "export type ExternalVulkanLayer =",
        "  (typeof EXTERNAL_VULKAN_LAYER_VALUES)[number];",
        "",
        "// Configuration field type enum - matches Python",
        "export enum ConfigFieldType {",
        "  BOOLEAN = \"boolean\",",
        "  INTEGER = \"integer\",",
        "  FLOAT = \"float\",",
        "  STRING = \"string\"",
        "}",
        "",
        "// Field name constants for type-safe access",
    ] + field_constants + [
        "",
        "// Configuration field definition",
        "export interface ConfigField {",
        "  name: string;",
        "  fieldType: ConfigFieldType;",
        "  default: boolean | number | string;",
        "  description: string;",
        "}",
        "",
        "// Configuration schema - auto-generated from Python",
        "export const CONFIG_SCHEMA: Record<string, ConfigField> = {"
    ]

    # Generate schema entries
    schema_entries = []
    interface_fields = []
    defaults_fields = []
    field_types = []

    for field_name, field_def in CONFIG_SCHEMA_DEF.items():
        # Schema entry
        default_value = field_def["default"]
        if isinstance(default_value, str):
            default_str = f'"{default_value}"'
        elif isinstance(default_value, bool):
            default_str = "true" if default_value else "false"
        else:
            default_str = str(default_value)

        schema_entries.append(f'  {field_name}: {{')
        schema_entries.append(f'    name: "{field_name}",')
        schema_entries.append(f'    fieldType: ConfigFieldType.{field_def["fieldType"].upper()},')
        schema_entries.append(f'    default: {default_str},')
        schema_entries.append(f'    description: "{field_def["description"]}"')
        schema_entries.append('  },')

        # Interface field
        if field_def["fieldType"] == ConfigFieldType.BOOLEAN:
            ts_type = "boolean"
        elif field_def["fieldType"] == ConfigFieldType.INTEGER:
            ts_type = "number"
        elif field_def["fieldType"] == ConfigFieldType.FLOAT:
            ts_type = "number"
        elif field_def["fieldType"] == ConfigFieldType.STRING:
            ts_type = "string"
        else:
            ts_type = "any"

        interface_fields.append(f'  {field_name}: {ts_type};')
        defaults_fields.append(f'    {field_name}: {default_str},')
        field_types.append(f'    {field_name}: ConfigFieldType.{field_def["fieldType"].upper()},')

    # Complete the file
    all_lines = enum_lines + schema_entries + [
        "};",
        "",
        "// Type-safe configuration data structure",
        "export interface ConfigurationData {",
    ] + interface_fields + [
        "}",
        "",
        "// Validated partial profile update sent through Decky's RPC boundary",
        "export type ConfigurationPatch = Partial<ConfigurationData>;",
        "",
        "// Helper functions",
        "export function getFieldNames(): string[] {",
        "  return Object.keys(CONFIG_SCHEMA);",
        "}",
        "",
        "export function getDefaults(): ConfigurationData {",
        "  return {",
    ] + defaults_fields + [
        "  };",
        "}",
        "",
        "export function getFieldTypes(): Record<string, ConfigFieldType> {",
        "  return {",
    ] + field_types + [
        "  };",
        "}",
        ""
    ]

    return "\n".join(all_lines)


def main():
    """Main function to generate TypeScript schema and Python boilerplate"""
    try:
        # Generate the TypeScript content
        ts_content = generate_typescript_schema()

        # Write to the target file
        target_file = project_root / "src" / "config" / "generatedConfigSchema.ts"
        target_file.write_text(ts_content)

        print(f"✅ Generated {target_file} from shared_config.py")
        print(f"   Fields: {len(CONFIG_SCHEMA_DEF)}")

        # Also generate Python boilerplate
        print("\n🔄 Generating Python boilerplate...")
        from pathlib import Path
        import subprocess

        boilerplate_script = project_root / "scripts" / "generate_python_boilerplate.py"
        result = subprocess.run(
            [sys.executable, str(boilerplate_script)],
            capture_output=True,
            text=True,
            check=True,
        )
        print(result.stdout)

    except Exception as e:
        print(f"❌ Error generating schema: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
