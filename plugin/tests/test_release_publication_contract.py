"""Regression tests for immutable and synchronized release publication."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
ASSET_HELPER = REPOSITORY_ROOT / "scripts/upload-release-assets.mjs"


class ReleaseAssetHelperTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        self.bin_directory = self.root / "bin"
        self.bin_directory.mkdir()
        self.state_path = self.root / "state.json"
        self.log_path = self.root / "gh-log.jsonl"
        self.remote_directory = self.root / "remote"
        self.remote_directory.mkdir()
        fake_gh = self.bin_directory / "gh"
        fake_gh.write_text(
            textwrap.dedent(
                """\
                #!/usr/bin/env python3
                import json
                import os
                from pathlib import Path
                import shutil
                import sys

                arguments = sys.argv[1:]
                state_path = Path(os.environ["FAKE_GH_STATE"])
                log_path = Path(os.environ["FAKE_GH_LOG"])
                state = json.loads(state_path.read_text(encoding="utf-8"))
                with log_path.open("a", encoding="utf-8") as log:
                    log.write(json.dumps(arguments) + "\\n")

                if arguments[:2] == ["release", "view"]:
                    print(json.dumps({"assets": state.get("assets", [])}))
                elif arguments[:2] == ["release", "download"]:
                    name = arguments[arguments.index("--pattern") + 1]
                    destination = Path(arguments[arguments.index("--dir") + 1])
                    shutil.copy(Path(state["remote_directory"]) / name, destination / name)
                elif arguments[:2] == ["release", "upload"]:
                    pass
                else:
                    print(f"Unexpected gh arguments: {arguments}", file=sys.stderr)
                    sys.exit(2)
                """
            ),
            encoding="utf-8",
        )
        fake_gh.chmod(0o755)

    def tearDown(self):
        self.temporary_directory.cleanup()

    def _write_candidate(self, name, content):
        path = self.root / name
        path.write_bytes(content)
        return path

    def _run(self, assets, *candidate_paths):
        self.state_path.write_text(
            json.dumps(
                {
                    "assets": assets,
                    "remote_directory": str(self.remote_directory),
                }
            ),
            encoding="utf-8",
        )
        environment = os.environ.copy()
        environment["PATH"] = (
            f"{self.bin_directory}{os.pathsep}{environment['PATH']}"
        )
        environment["FAKE_GH_STATE"] = str(self.state_path)
        environment["FAKE_GH_LOG"] = str(self.log_path)
        return subprocess.run(
            [
                "node",
                str(ASSET_HELPER),
                "owner/repository",
                "render-v1.2.3",
                *(str(path) for path in candidate_paths),
            ],
            check=False,
            capture_output=True,
            text=True,
            env=environment,
        )

    def _gh_calls(self):
        if not self.log_path.exists():
            return []
        return [
            json.loads(line)
            for line in self.log_path.read_text(encoding="utf-8").splitlines()
        ]

    def test_uploads_only_missing_assets_after_all_existing_assets_match(self):
        existing = self._write_candidate("existing.tar.xz", b"existing")
        missing = self._write_candidate("missing.tar.xz", b"missing")
        digest = hashlib.sha256(existing.read_bytes()).hexdigest()

        result = self._run(
            [{"name": existing.name, "digest": f"sha256:{digest}"}],
            existing,
            missing,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        upload_calls = [call for call in self._gh_calls() if call[:2] == ["release", "upload"]]
        self.assertEqual(len(upload_calls), 1)
        self.assertIn(str(missing), upload_calls[0])
        self.assertNotIn(str(existing), upload_calls[0])

    def test_rejects_a_conflicting_published_digest_before_upload(self):
        missing = self._write_candidate("missing.tar.xz", b"missing")
        candidate = self._write_candidate("candidate.zip", b"candidate")

        result = self._run(
            [{"name": candidate.name, "digest": f"sha256:{'0' * 64}"}],
            missing,
            candidate,
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("published assets are immutable", result.stderr)
        self.assertFalse(
            any(call[:2] == ["release", "upload"] for call in self._gh_calls())
        )

    def test_downloads_and_verifies_an_asset_when_github_has_no_digest(self):
        candidate = self._write_candidate("candidate.zip", b"same bytes")
        (self.remote_directory / candidate.name).write_bytes(b"same bytes")

        result = self._run(
            [{"name": candidate.name, "digest": None}],
            candidate,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(
            any(call[:2] == ["release", "download"] for call in self._gh_calls())
        )
        self.assertFalse(
            any(call[:2] == ["release", "upload"] for call in self._gh_calls())
        )

    def test_rejects_a_downloaded_asset_with_different_content(self):
        candidate = self._write_candidate("candidate.zip", b"candidate")
        (self.remote_directory / candidate.name).write_bytes(b"published")

        result = self._run(
            [{"name": candidate.name, "digest": None}],
            candidate,
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("published assets are immutable", result.stderr)
        self.assertFalse(
            any(call[:2] == ["release", "upload"] for call in self._gh_calls())
        )


class ReleasePublisherSourceContractTests(unittest.TestCase):
    def test_component_publishers_never_clobber_existing_assets(self):
        for path in (
            REPOSITORY_ROOT / "engine/scripts/publish-package.sh",
            REPOSITORY_ROOT / "plugin/scripts/publish-package.sh",
        ):
            source = path.read_text(encoding="utf-8")
            self.assertNotIn("--clobber", source)
            self.assertIn("upload-release-assets.mjs", source)

    def test_every_publisher_requires_exact_remote_main(self):
        for path in (
            REPOSITORY_ROOT / "scripts/publish-release.sh",
            REPOSITORY_ROOT / "engine/scripts/publish-package.sh",
            REPOSITORY_ROOT / "plugin/scripts/publish-package.sh",
        ):
            source = path.read_text(encoding="utf-8")
            self.assertIn("must exactly match", source)
            self.assertIn("rev-parse HEAD", source)

    def test_script_created_version_commits_are_pushed_before_packaging(self):
        cases = (
            (
                REPOSITORY_ROOT / "engine/scripts/publish-package.sh",
                'git commit -m "Release MAKO Renderer',
                'git push "$release_remote" "$release_branch"',
                'scripts/package-local.sh "$archive"',
            ),
            (
                REPOSITORY_ROOT / "plugin/scripts/publish-package.sh",
                'git -C "$repository_root" commit -m "Release MAKO Decky',
                'git -C "$repository_root" push origin "$current_branch"',
                '"$script_dir/package-local.sh"',
            ),
        )
        for path, commit_marker, push_marker, package_marker in cases:
            source = path.read_text(encoding="utf-8")
            commit_index = source.index(commit_marker)
            push_index = source.index(push_marker, commit_index)
            package_index = source.index(package_marker, push_index)
            self.assertLess(commit_index, push_index)
            self.assertLess(push_index, package_index)

    def test_paired_publisher_preflights_the_shared_codename(self):
        source = (
            REPOSITORY_ROOT / "scripts/publish-release.sh"
        ).read_text(encoding="utf-8")
        self.assertEqual(source.count("read-release-info.mjs"), 2)
        self.assertIn("Paired release notes must use the same codename", source)


if __name__ == "__main__":
    unittest.main()
