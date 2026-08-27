"""Prevent unsupported Steam focus-flow values from reaching Decky."""

from pathlib import Path
import re
import unittest


class FrontendFocusContractTests(unittest.TestCase):
    def test_focusable_usage_is_centralized_and_uses_supported_flows(self):
        frontend = Path(__file__).resolve().parents[1] / "src"
        valid_flows = {
            "column",
            "column-reverse",
            "row",
            "row-reverse",
            "grid",
            "geometric",
        }

        for source_path in sorted(frontend.rglob("*.tsx")):
            source = source_path.read_text(encoding="utf-8")
            if source_path.name != "MakoUi.tsx":
                self.assertNotIn(
                    "<Focusable",
                    source,
                    f"{source_path.relative_to(frontend)} bypasses the typed "
                    "MakoFocusable boundary",
                )
            for flow in re.findall(r'flow-children="([^"]+)"', source):
                self.assertIn(
                    flow,
                    valid_flows,
                    f"{source_path.relative_to(frontend)} uses unsupported "
                    f"Steam focus flow {flow!r}",
                )


if __name__ == "__main__":
    unittest.main()
