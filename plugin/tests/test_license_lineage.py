"""Regression tests for MAKO Renderer's inherited license lineage."""

from pathlib import Path
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
UPSTREAM_BASELINE = "8b0da2661c6f3473a7fccc8ba643880050e71642"
PRE_MIGRATION_REVISION = "276030d4925c40038a61ecd66bd49ce777faec8c"
MONOREPO_MIGRATION = "8ed1cdbe7f5496fade8ad01d85ac3d671957fcac"


class LicenseLineageTests(unittest.TestCase):
    def test_license_records_the_exact_gpl_renderer_lineage(self):
        license_text = (REPOSITORY_ROOT / "LICENSE.md").read_text(encoding="utf-8")

        self.assertIn("## lsfg-vk renderer lineage", license_text)
        self.assertIn("GPL-3.0-or-later", license_text)
        self.assertIn(UPSTREAM_BASELINE, license_text)
        self.assertIn(PRE_MIGRATION_REVISION, license_text)
        self.assertIn(MONOREPO_MIGRATION, license_text)
        self.assertIn(
            "Copyright in the incorporated portions remains with the respective lsfg-vk copyright holders",
            license_text,
        )
        self.assertIn("Bjoern Hoehrmann UTF-8 decoder", license_text)
        self.assertIn(
            "the MIT License that applied to lsfg-vk version 1 does not describe MAKO Renderer",
            license_text,
        )

    def test_public_docs_do_not_describe_the_renderer_lineage_as_mit(self):
        public_docs = (
            REPOSITORY_ROOT / "LICENSE.md",
            REPOSITORY_ROOT / "README.md",
            REPOSITORY_ROOT / "THIRD_PARTY_NOTICES.md",
            REPOSITORY_ROOT / "engine/README.md",
        )
        forbidden_claims = (
            "earlier MIT-licensed revision",
            "before upstream adopted GPLv3",
            "later GPLv3-licensed lsfg-vk revisions",
            "5a293c428409a866bbe91be9eb54b49e71521e14",
        )

        for path in public_docs:
            text = path.read_text(encoding="utf-8")
            for claim in forbidden_claims:
                with self.subTest(path=path, claim=claim):
                    self.assertNotIn(claim, text)


if __name__ == "__main__":
    unittest.main()
