import hashlib
import importlib.util
import io
import json
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path


SCRIPT_PATH = (
    Path(__file__).resolve().parents[1]
    / "scripts"
    / "deploy-validated-package.py"
)
SPEC = importlib.util.spec_from_file_location("deploy_validated_package", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
DEPLOY_MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DEPLOY_MODULE)


def _write_renderer_archive(path: Path, layer_64: bytes, layer_32: bytes) -> None:
    with tarfile.open(path, "w:xz") as archive:
        for member_name, content in (
            ("./lib/libmako-render.so", layer_64),
            ("./lib32/libmako-render.so", layer_32),
        ):
            member = tarfile.TarInfo(member_name)
            member.size = len(content)
            member.mode = 0o644
            archive.addfile(member, io.BytesIO(content))


class DeployValidatedPackageTests(unittest.TestCase):
    def _create_package(self, root: Path) -> Path:
        package_root = root / "package" / "Mako"
        (package_root / "bin").mkdir(parents=True)
        (package_root / "dist").mkdir()
        (package_root / "py_modules/mako_plugin").mkdir(parents=True)
        (package_root / "plugin.json").write_text(
            json.dumps({"name": "MAKO - Scaling & Frame Generation"}),
            encoding="utf-8",
        )
        (package_root / "main.py").write_text("new-main\n", encoding="utf-8")
        (package_root / "dist/index.js").write_text("new-ui\n", encoding="utf-8")
        (package_root / "py_modules/mako_plugin/plugin.py").write_text(
            "new-backend\n", encoding="utf-8"
        )
        (package_root / "bin/new.flatpak").write_text("flatpak\n", encoding="utf-8")

        renderer_name = "MAKO-Renderer-test-linux.tar.xz"
        renderer_archive = package_root / "bin" / renderer_name
        _write_renderer_archive(renderer_archive, b"new-layer-64", b"new-layer-32")
        checksum = hashlib.sha256(renderer_archive.read_bytes()).hexdigest()
        (package_root / "package.json").write_text(
            json.dumps(
                {
                    "bundled_renderer": {
                        "name": renderer_name,
                        "version": "test",
                        "sha256hash": checksum,
                        "architectures": ["64", "32"],
                        "host_architectures": ["x86_64"],
                    }
                }
            ),
            encoding="utf-8",
        )

        archive_path = root / "MAKO-Decky-test.zip"
        with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED) as archive:
            for source in package_root.rglob("*"):
                if source.is_file():
                    archive.write(source, source.relative_to(package_root.parent))
        return archive_path

    def _create_installation(self, root: Path) -> Path:
        plugin_root = root / "homebrew/plugins/Mako"
        (plugin_root / "bin").mkdir(parents=True)
        (plugin_root / "dist").mkdir()
        (plugin_root / "py_modules").mkdir()
        (plugin_root / "plugin.json").write_text(
            json.dumps({"name": "MAKO - Frame Generation"}), encoding="utf-8"
        )
        (plugin_root / "main.py").write_text("old-main\n", encoding="utf-8")
        (plugin_root / "bin/stale.bin").write_text("stale\n", encoding="utf-8")
        (plugin_root / "dist/stale.js").write_text("stale\n", encoding="utf-8")
        (plugin_root / "py_modules/stale.py").write_text("stale\n", encoding="utf-8")

        return plugin_root

    def test_deploys_exact_self_contained_package(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = self._create_package(root)
            plugin_root = self._create_installation(root)

            DEPLOY_MODULE.deploy(archive, plugin_root)

            self.assertEqual((plugin_root / "main.py").read_text(), "new-main\n")
            self.assertEqual((plugin_root / "dist/index.js").read_text(), "new-ui\n")
            self.assertEqual(
                json.loads((plugin_root / "plugin.json").read_text())["name"],
                "MAKO - Scaling & Frame Generation",
            )
            self.assertFalse((plugin_root / "bin/stale.bin").exists())
            self.assertFalse((plugin_root / "dist/stale.js").exists())
            self.assertFalse((plugin_root / "py_modules/stale.py").exists())
            self.assertTrue(
                (plugin_root / "bin/MAKO-Renderer-test-linux.tar.xz").is_file()
            )

    def test_rejects_zip_path_traversal_before_changing_installation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plugin_root = self._create_installation(root)
            archive = root / "unsafe.zip"
            with zipfile.ZipFile(archive, "w") as package:
                package.writestr("Mako/../outside", "unsafe")

            with self.assertRaises(DEPLOY_MODULE.DeploymentError):
                DEPLOY_MODULE.deploy(archive, plugin_root)

            self.assertEqual((plugin_root / "main.py").read_text(), "old-main\n")
            self.assertFalse((root / "outside").exists())


if __name__ == "__main__":
    unittest.main()
