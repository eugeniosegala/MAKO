#!/usr/bin/env python3
"""Exercise contributor identity handling without changing repository history."""

import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).with_name("update-contributors.py")
SPEC = importlib.util.spec_from_file_location("update_contributors", SCRIPT)
contributors = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(contributors)


class ContributorGalleryTests(unittest.TestCase):
    def render(self, log):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / "contributors.json"
            config.write_text(json.dumps({
                "login_to_names": {"developer": ["Developer"]},
                "nonhuman_coauthors": [
                    {"name": "Claude Opus 5.5", "email": "noreply@anthropic.com"}
                ],
                "avatar_revision": 1,
            }))
            with patch.object(contributors, "CONFIG", config), patch.object(
                    contributors.subprocess, "run", return_value=SimpleNamespace(stdout=log)):
                return contributors.render()

    def test_named_ai_coauthor_does_not_need_an_invented_github_account(self):
        gallery = self.render(
            "Developer\x1fSubject\n\nCo-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\n\x1e"
        )
        self.assertIn("github.com/developer", gallery)
        self.assertNotIn("Claude", gallery)

    def test_same_name_with_a_different_email_is_not_excluded(self):
        with self.assertRaisesRegex(ValueError, "claude opus 5.5"):
            self.render("Developer\x1fCo-authored-by: Claude Opus 5.5 <person@example.com>\n\x1e")

    def test_exclusion_never_suppresses_an_unmapped_commit_author(self):
        with self.assertRaisesRegex(ValueError, "claude opus 5.5"):
            self.render("Claude Opus 5.5\x1fSubject\n\x1e")


if __name__ == "__main__":
    unittest.main()
