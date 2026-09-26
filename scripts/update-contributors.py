#!/usr/bin/env python3
"""Generate the README gallery from commit authors and co-author trailers."""

import argparse
import json
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
README = ROOT / "README.md"
CONFIG = ROOT / "scripts/contributors.json"
START = "<!-- mako-contributors:start -->"
END = "<!-- mako-contributors:end -->"
COAUTHOR = re.compile(r"^Co-authored-by:\s*([^\n<>]+?)\s*<[^\n<>]+>\s*$", re.IGNORECASE | re.MULTILINE)
LOGIN = re.compile(r"[A-Za-z0-9](?:[A-Za-z0-9-]{0,37}[A-Za-z0-9])?")


def git_names():
    result = subprocess.run(
        ["git", "log", "--no-merges", "--format=%an%x1f%B%x1e", "HEAD"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    names = set()
    for record in result.stdout.split("\x1e"):
        if "\x1f" not in record:
            continue
        author_name, body = record.split("\x1f", 1)
        names.add(author_name.strip().casefold())
        names.update(match.group(1).strip().casefold() for match in COAUTHOR.finditer(body))
    return names


def render():
    config = json.loads(CONFIG.read_text(encoding="utf-8"))
    name_to_login = {}
    for login, aliases in config["login_to_names"].items():
        for alias in aliases:
            name = alias.casefold()
            if name in name_to_login and name_to_login[name] != login:
                raise ValueError(f"Commit name {alias!r} maps to multiple GitHub logins")
            name_to_login[name] = login
    names = git_names()
    missing = names - name_to_login.keys()
    if missing:
        raise ValueError(
            "Map these commit author/co-author names in scripts/contributors.json: "
            + ", ".join(sorted(missing))
        )
    logins = set(name_to_login[name] for name in names)
    logins.update(config.get("additional_logins", []))
    for login in logins:
        if not LOGIN.fullmatch(login):
            raise ValueError(f"Invalid GitHub login in scripts/contributors.json: {login!r}")
    portraits = [
        f'<a href="https://github.com/{login}"><img src="https://github.com/{login}.png?size=48" width="48" height="48" alt="@{login}" /></a>'
        for login in sorted(logins, key=str.casefold)
    ]
    return START + "\n\n" + " ".join(portraits) + "\n" + END


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if the README gallery is stale")
    args = parser.parse_args()
    readme = README.read_text(encoding="utf-8")
    if readme.count(START) != 1 or readme.count(END) != 1:
        raise ValueError("README.md must contain exactly one contributor gallery marker pair")
    before, rest = readme.split(START, 1)
    _, after = rest.split(END, 1)
    updated = before + render() + after
    if args.check:
        if updated != readme:
            parser.exit(1, "README contributor gallery is stale; run python3 scripts/update-contributors.py\n")
        print("README contributor gallery is current")
    elif updated != readme:
        README.write_text(updated, encoding="utf-8")
        print("Updated README contributor gallery")
    else:
        print("README contributor gallery is already current")


if __name__ == "__main__":
    main()
