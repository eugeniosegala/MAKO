#!/usr/bin/env python3
"""Fail when tracked Decky bindings differ from their shared source."""

from pathlib import Path
import subprocess
import sys


PLUGIN_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PLUGIN_ROOT / "scripts"))


def main() -> None:
    subprocess.run(
        [sys.executable, str(PLUGIN_ROOT.parent / "scripts/generate-flatpak-runtimes.py"),
         "--check"],
        check=True,
    )
    from generate_python_boilerplate import generate_complete_schema_file
    from generate_ts_schema import generate_typescript_schema

    subprocess.run(
        [sys.executable, str(PLUGIN_ROOT.parent / "scripts/generate-launcher-exclusions.py"),
         "--check"],
        check=True,
    )
    subprocess.run(
        [sys.executable, str(PLUGIN_ROOT.parent / 'engine/scripts/generate-remote-play-bindings.py'), '--check'],
        check=True,
    )
    expected_files = {
        PLUGIN_ROOT / "src/config/generatedConfigSchema.ts": (
            generate_typescript_schema()
        ),
        PLUGIN_ROOT / "py_modules/mako_plugin/config_schema_generated.py": (
            generate_complete_schema_file()
        ),
    }
    stale = [
        path.relative_to(PLUGIN_ROOT)
        for path, expected in expected_files.items()
        if path.read_text(encoding="utf-8") != expected
    ]
    if stale:
        print(
            "Generated configuration bindings are stale: "
            + ", ".join(map(str, stale)),
            file=sys.stderr,
        )
        print(
            "Run: python3 scripts/generate_ts_schema.py",
            file=sys.stderr,
        )
        raise SystemExit(1)

    print("Generated configuration bindings are current.")


if __name__ == "__main__":
    main()
