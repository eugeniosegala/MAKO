#!/usr/bin/env python3
"""Generate component-local launcher exclusions from the documented shared list."""

import argparse
import json
from pathlib import Path
import re
import sys
from typing import TypedDict


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
SOURCE = REPOSITORY_ROOT / "engine/mako-common/launcher_exclusions.json"
RENDERER_OUTPUT = REPOSITORY_ROOT / (
    "engine/mako-common/src/configuration/launcher_exclusions_generated.hpp"
)
DECKY_OUTPUT = REPOSITORY_ROOT / (
    "plugin/py_modules/mako_plugin/launcher_exclusions_generated.py"
)


class LauncherExclusion(TypedDict):
    launcher: str
    reason: str
    executables: list[str]


def load_exclusions(path: Path = SOURCE) -> list[LauncherExclusion]:
    """Validate documented, exact ASCII Windows basenames before generating code."""
    entries = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(entries, list):
        raise ValueError("Launcher exclusions must be a list")
    result: list[LauncherExclusion] = []
    launchers: set[str] = set()
    executables: set[str] = set()
    for entry in entries:
        if not isinstance(entry, dict) or set(entry) != {
            "launcher", "reason", "executables"
        }:
            raise ValueError("Each exclusion needs launcher, reason, and executables")
        for key in ("launcher", "reason"):
            value = entry[key]
            if not isinstance(value, str) or not value.strip():
                raise ValueError(f"Each exclusion needs a nonempty {key}")
        launcher = entry["launcher"].strip()
        if launcher.casefold() in launchers:
            raise ValueError(f"Duplicate launcher: {launcher}")
        launchers.add(launcher.casefold())
        names = entry["executables"]
        if not isinstance(names, list) or not names:
            raise ValueError(f"{launcher} needs at least one executable")
        normalized: list[str] = []
        for name in names:
            if not isinstance(name, str) or not re.fullmatch(
                r"[A-Za-z0-9_][A-Za-z0-9_.-]*\.[Ee][Xx][Ee]", name
            ):
                raise ValueError(f"Expected an exact ASCII .exe basename: {name!r}")
            lowered = name.lower()
            if lowered in executables:
                raise ValueError(f"Duplicate executable: {name}")
            executables.add(lowered)
            normalized.append(lowered)
        result.append(LauncherExclusion(
            launcher=launcher,
            reason=entry["reason"].strip(),
            executables=normalized,
        ))
    return result


def generated_files(entries: list[LauncherExclusion]) -> dict[Path, str]:
    names = [name for entry in entries for name in entry["executables"]]
    source = SOURCE.relative_to(REPOSITORY_ROOT)
    command = "python3 scripts/generate-launcher-exclusions.py"
    renderer = [
        "/* SPDX-License-Identifier: GPL-3.0-or-later */",
        f"// Generated from {source}; do not edit.",
        f"// Regenerate: {command}",
        "#pragma once",
        "",
        "#include <array>",
        "#include <string_view>",
        "",
        "namespace ls::detail {",
        f"    constexpr std::array<std::string_view, {len(names)}> "
        "excludedWindowsLauncherExecutables{",
        *(f"        std::string_view{{{json.dumps(name)}}}," for name in names),
        "    };",
        "}",
        "",
    ]
    decky = [
        f'"""Generated from {source}; do not edit."""',
        f"# Regenerate: {command}",
        "",
        "EXCLUDED_WINDOWS_LAUNCHERS: frozenset[str] = frozenset((",
        *(f"    {json.dumps(name)}," for name in names),
        "))",
        "",
    ]
    return {
        RENDERER_OUTPUT: "\n".join(renderer),
        DECKY_OUTPUT: "\n".join(decky),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check without writing")
    args = parser.parse_args()
    try:
        outputs = generated_files(load_exclusions())
        stale = []
        for path, content in outputs.items():
            if args.check:
                if not path.is_file() or path.read_text(encoding="utf-8") != content:
                    stale.append(str(path.relative_to(REPOSITORY_ROOT)))
            elif not path.is_file() or path.read_text(encoding="utf-8") != content:
                path.write_text(content, encoding="utf-8")
        if stale:
            print("Launcher exclusion bindings are stale: " + ", ".join(stale),
                  file=sys.stderr)
            print("Run: python3 scripts/generate-launcher-exclusions.py",
                  file=sys.stderr)
            return 1
    except (OSError, ValueError) as error:
        print(f"Launcher exclusions: {error}", file=sys.stderr)
        return 1
    print("Launcher exclusion bindings are current.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
