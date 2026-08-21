#!/usr/bin/env python3
"""Print the canonical MAKO Decky Flatpak runtime contract for shell tools."""

import argparse
from pathlib import Path
import sys


PLUGIN_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PLUGIN_ROOT))

from py_modules.mako_plugin.constants import (  # noqa: E402
    FLATPAK_RUNTIME_BUNDLES,
    LIB_FILENAME,
    LOCAL_LIB,
    LOCAL_LIB32,
)


def _summary(versions: tuple[str, ...]) -> str:
    if len(versions) == 1:
        return versions[0]
    if len(versions) == 2:
        return f"{versions[0]} and {versions[1]}"
    return f"{', '.join(versions[:-1])}, and {versions[-1]}"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "field",
        choices=("versions", "bundles", "summary", "renderer-paths"),
    )
    args = parser.parse_args()
    versions = tuple(FLATPAK_RUNTIME_BUNDLES)

    if args.field == "versions":
        print(*versions, sep="\n")
    elif args.field == "bundles":
        print(
            *(bundle.filename for bundle in FLATPAK_RUNTIME_BUNDLES.values()),
            sep="\n",
        )
    elif args.field == "summary":
        print(_summary(versions))
    else:
        print(
            LIB_FILENAME,
            f"{LOCAL_LIB}/{LIB_FILENAME}",
            f"{LOCAL_LIB32}/{LIB_FILENAME}",
            sep="\n",
        )


if __name__ == "__main__":
    main()
