#!/usr/bin/env python3
"""Exercise SteamOS prerequisite installation with an isolated host and commands."""

import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


INSTALLER = Path(__file__).with_name("install-steamos-build-tools.sh")


class SteamOSBuildToolsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.log = self.root / "commands.log"
        source = INSTALLER.read_text()
        # Redirect the script's absolute host reads, never system writes.
        source = source.replace("/etc/os-release", str(self.root / "os-release"))
        source = source.replace("/usr/", str(self.root / "usr") + "/")
        (self.root / "os-release").write_text("ID=steamos\n")
        self.script = self.root / "installer.sh"
        self.script.write_text(source)
        for header in re.findall(r"^\s+(/\S+)\s*$", source, re.MULTILINE):
            path = Path(header)
            self.assertTrue(path.is_relative_to(self.root))
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        for keyring in ("archlinux", "holo"):
            path = self.root / "usr/share/pacman/keyrings" / f"{keyring}.gpg"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        self.bin = self.root / "bin"
        self.bin.mkdir()
        shim = """#!/bin/bash
set -eu
name=${0##*/}
case "$name" in
    sudo)
        case "$1" in
            -v) exit 0 ;;
            steamos-readonly|pacman|pacman-key) exec "${0%/*}/$1" "${@:2}" ;;
            *) exit 99 ;;
        esac ;;
    steamos-readonly)
        if [[ "$1" == status ]]; then
            printf '%s\\n' "$MAKO_TEST_STATE"
            exit "$MAKO_TEST_STATUS"
        fi
        printf '%s %s\\n' "$name" "$*" >> "$MAKO_TEST_LOG"
        if [[ "$1" == enable ]]; then exit "$MAKO_TEST_RESTORE_EXIT"; fi ;;
    pacman|pacman-key)
        printf '%s %s\\n' "$name" "$*" >> "$MAKO_TEST_LOG"
        if [[ "$name" == pacman ]]; then exit "$MAKO_TEST_PACMAN_EXIT"; fi ;;
esac
"""
        commands = re.search(r"local commands=\(([^)]+)\)", source).group(1).split()
        for name in commands + ["sudo", "pacman", "pacman-key", "steamos-readonly"]:
            path = self.bin / name
            path.write_text(shim)
            path.chmod(0o755)

    def run_installer(self, state, status, *, pacman_exit=0, restore_exit=0):
        environment = dict(os.environ, PATH=str(self.bin),
                           MAKO_TEST_STATE=state, MAKO_TEST_STATUS=str(status),
                           MAKO_TEST_LOG=str(self.log),
                           MAKO_TEST_PACMAN_EXIT=str(pacman_exit),
                           MAKO_TEST_RESTORE_EXIT=str(restore_exit))
        result = subprocess.run(["/bin/bash", str(self.script), "--install"],
                                env=environment, text=True, capture_output=True)
        calls = self.log.read_text().splitlines() if self.log.exists() else []
        return result, calls

    def test_writable_root_with_nonzero_status_can_install(self):
        result, calls = self.run_installer("disabled", 1)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(any(call.startswith("pacman -S ") for call in calls))
        self.assertFalse(any(call.startswith("steamos-readonly ") for call in calls))

    def test_readonly_root_is_restored_after_installation(self):
        result, calls = self.run_installer("enabled", 0)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(calls[0], "steamos-readonly disable")
        self.assertEqual(calls[-1], "steamos-readonly enable")

    def test_failed_package_transaction_restores_readonly_state(self):
        result, calls = self.run_installer("enabled", 0, pacman_exit=42)
        self.assertEqual(result.returncode, 42, result.stderr)
        self.assertEqual(calls[-1], "steamos-readonly enable")

    def test_failed_restoration_is_reported(self):
        result, _ = self.run_installer("enabled", 0, restore_exit=1)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Could not restore SteamOS read-only protection", result.stderr)

    def test_invalid_query_results_abort_before_mutation(self):
        for state, status in (("disabled", 2), ("enabled", 1), ("", 1),
                              ("disabled, but loaded sysexts", 1)):
            with self.subTest(state=state, status=status):
                result, calls = self.run_installer(state, status)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Unexpected SteamOS read-only state", result.stderr)
                self.assertEqual(calls, [])


if __name__ == "__main__":
    unittest.main()
