"""Portable catalog and sample staging contracts; runtime QTranslator acceptance is separate."""
from pathlib import Path
import importlib.util
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("package_portable", ROOT / "scripts/package_portable.py")
packaging = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(packaging)


class CompiledCatalogPackaging(unittest.TestCase):
    def setUp(self):
        temporary_root = Path(os.environ.get("SLOPSTUDIO_TEST_TMP_ROOT", ROOT / ".agents/tmp/package-translations")).resolve()
        temporary_root.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="catalogs-", dir=temporary_root)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        (self.source / "i18n").mkdir(parents=True)
        (self.source / "docs").mkdir()
        (self.source / "README.md").write_text("Packaging fixture\n")
        (self.source / "VERSION").write_text("fixture\n")
        for locale in ["en", "ar"]:
            (self.source / "i18n" / f"slopstudio_{locale}.ts").write_text(f'<TS language="{locale}"/>')
        # A source-tree artifact must not masquerade as the selected build's runtime catalog.
        (self.source / "i18n/slopstudio_en.qm").write_bytes(b"stale source-tree bytes")
        self.binary = self.root / "build/src/slopstudio"
        self.binary.parent.mkdir(parents=True)
        self.binary.write_bytes(b"opaque executable fixture")
        self.compiled = self.root / "build/i18n"
        self.compiled.mkdir()
        self.addCleanup(patch.stopall)
        patch.object(packaging, "repo_root", return_value=self.source).start()
        # License bundling has its own whole-package validator; isolate these byte-copy contracts.
        patch.object(packaging, "write_license_bundle", return_value=("licenses/fixture", [])).start()

    def stage(self, explicit=None, output="output", archive=False):
        package, _ = packaging.create_package(self.binary, self.root / output, "fixture", False, archive,
                                               "linux", "x86_64", explicit)
        return package, json.loads((package / "package-manifest.json").read_text())

    def test_automatic_complete_and_partial_inventory(self):
        for locale in ["en", "ar"]:
            (self.compiled / f"slopstudio_{locale}.qm").write_bytes(f"compiled {locale}".encode())
        (self.compiled / "unrelated.qm").write_bytes(b"unrelated")
        package, manifest = self.stage()
        self.assertEqual(manifest["compiledLocalization"]["status"], "complete")
        self.assertEqual((package / "i18n/slopstudio_en.qm").read_bytes(), b"compiled en")
        self.assertFalse((package / "i18n/unrelated.qm").exists())
        self.assertIn("i18n/slopstudio_ar.qm", (package / "CHECKSUMS.sha256").read_text())
        (self.compiled / "slopstudio_ar.qm").unlink()
        package, manifest = self.stage(output="partial")
        self.assertEqual(manifest["compiledLocalization"]["status"], "partial")
        self.assertEqual(manifest["compiledLocalization"]["missingCatalogs"], ["i18n/slopstudio_ar.qm"])

    def test_missing_runtime_keeps_source_language_fallback_explicit(self):
        package, manifest = self.stage()
        self.assertEqual(manifest["compiledLocalization"]["status"], "unavailable")
        self.assertEqual(manifest["compiledLocalization"]["catalogs"], [])
        self.assertTrue((package / "i18n/slopstudio_en.ts").exists())
        self.assertFalse((package / "i18n/slopstudio_en.qm").exists())

    def test_required_catalogs_fail_before_replacing_existing_package(self):
        package, _ = self.stage()
        sentinel = package / "sentinel"
        sentinel.write_bytes(b"preserve existing output")
        with self.assertRaisesRegex(ValueError, "every application catalog"):
            self.stage(self.compiled)
        self.assertEqual(sentinel.read_bytes(), b"preserve existing output")
        override = self.root / "override"
        override.mkdir()
        for locale in ["en", "ar"]:
            (override / f"slopstudio_{locale}.qm").write_bytes(f"override {locale}".encode())
        package, manifest = self.stage(override)
        self.assertTrue(manifest["compiledLocalization"]["required"])
        self.assertEqual((package / "i18n/slopstudio_en.qm").read_bytes(), b"override en")

    def test_repackaging_cannot_delete_its_binary_or_catalog_inputs(self):
        for locale in ["en", "ar"]:
            (self.compiled / f"slopstudio_{locale}.qm").write_bytes(f"compiled {locale}".encode())
        package, _ = self.stage()
        self.binary = package / "bin/slopstudio"
        with self.assertRaisesRegex(ValueError, "required input"):
            self.stage()
        self.assertEqual(self.binary.read_bytes(), b"opaque executable fixture")
        self.binary = self.root / "build/src/slopstudio"
        with self.assertRaisesRegex(ValueError, "required input"):
            self.stage(package / "i18n")
        self.assertEqual((package / "i18n/slopstudio_en.qm").read_bytes(), b"compiled en")

    def link(self, path, target):
        try:
            path.symlink_to(target, target_is_directory=target.is_dir())
        except OSError as error:
            self.skipTest(f"Symbolic links are unavailable: {error}")
        # Remove the fixture link before TemporaryDirectory's recursive cleanup.
        self.addCleanup(path.unlink)

    def test_linked_package_output_preserves_target(self):
        output = self.root / "output"
        target = output / "keep"
        target.mkdir(parents=True)
        sentinel = target / "sentinel"
        sentinel.write_bytes(b"untouched")
        self.link(output / "slopstudio-fixture-linux-x86_64", target)
        with self.assertRaisesRegex(ValueError, "link or reparse point"):
            self.stage()
        self.assertEqual(sentinel.read_bytes(), b"untouched")

    @unittest.skipUnless(os.name == "nt", "Windows junction fixture")
    def test_junction_package_output_preserves_target(self):
        output = self.root / "output"
        target = output / "keep"
        target.mkdir(parents=True)
        sentinel = target / "sentinel"
        sentinel.write_bytes(b"untouched")
        link = output / "slopstudio-fixture-linux-x86_64"
        env = os.environ.copy()
        env["SLOPSTUDIO_TEST_LINK_PATH"] = str(link)
        env["SLOPSTUDIO_TEST_LINK_TARGET"] = str(target)
        subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
                        "$ErrorActionPreference = 'Stop'; New-Item -ItemType Junction "
                        "-Path $env:SLOPSTUDIO_TEST_LINK_PATH -Target $env:SLOPSTUDIO_TEST_LINK_TARGET | Out-Null"],
                       env=env, check=True, capture_output=True, text=True)
        # rmdir removes the junction itself without traversing the target.
        self.addCleanup(link.rmdir)
        with self.assertRaisesRegex(ValueError, "link or reparse point"):
            self.stage()
        self.assertEqual(sentinel.read_bytes(), b"untouched")

    def test_nested_output_link_preserves_existing_package(self):
        package, _ = self.stage()
        original = (package / "bin/slopstudio").read_bytes()
        target = self.root / "keep"
        target.write_bytes(b"untouched")
        link = package / "linked-file"
        self.link(link, target)
        with self.assertRaisesRegex(ValueError, "link or reparse point"):
            self.stage()
        self.assertEqual(target.read_bytes(), b"untouched")
        self.assertEqual((package / "bin/slopstudio").read_bytes(), original)

    def test_linked_archive_preserves_target_and_package(self):
        package, _ = self.stage()
        sentinel = package / "sentinel"
        sentinel.write_bytes(b"existing package")
        target = self.root / "keep.zip"
        target.write_bytes(b"untouched")
        self.link(package.parent / (package.name + ".zip"), target)
        with self.assertRaisesRegex(ValueError, "link or reparse point"):
            self.stage(archive=True)
        self.assertEqual(target.read_bytes(), b"untouched")
        self.assertEqual(sentinel.read_bytes(), b"existing package")


class SampleStaging(unittest.TestCase):
    """samples/ ships only what the repository tracks; stray local files cannot break a package."""

    # The same source, binary and catalog fixture, without rerunning the catalog tests.
    stage = CompiledCatalogPackaging.stage

    # A path shaped like the evidence files earlier sample work leaves behind:
    # nested directories and a hash-suffixed name, together well past Windows'
    # 259-character limit once the staging directory is added in front.
    LONG_RELATIVE = Path("projects/quake2-sample/evidence/history/alpha2-full-candidate-evidence-folder/"
                         "validation-reports-for-the-superseded-source-freeze-of-the-candidate/"
                         "test-source-freeze-superseded-" + "6f6791b40651fad65c94a8cc70ed95fd1d817117a4bac6d10d54f337a94d14f3" * 2 + ".json")

    def setUp(self):
        CompiledCatalogPackaging.setUp(self)
        self.samples = self.source / "samples"
        (self.samples / "projects/quake-minimal/maps").mkdir(parents=True)
        (self.samples / "README.md").write_text("Sample projects\n")
        (self.samples / "projects/quake-minimal/maps/start.map").write_text("{ }\n")

    def write_long_untracked(self):
        path = self.samples / self.LONG_RELATIVE
        # Windows refuses to create the file through its ordinary path; the
        # extended prefix bypasses MAX_PATH so the fixture can exist at all.
        raw = ("\\\\?\\" + str(path)) if os.name == "nt" else str(path)
        os.makedirs(os.path.dirname(raw), exist_ok=True)
        with open(raw, "w", encoding="utf-8") as handle:
            handle.write("{}\n")
        # TemporaryDirectory's cleanup cannot reach the file by its plain path either.
        self.addCleanup(os.remove, raw)
        self.assertGreater(len(str(self.root / "output/slopstudio-fixture-linux-x86_64/samples" / self.LONG_RELATIVE)),
                           packaging.MAX_PACKAGE_PATH)
        return path

    def stage_samples(self, output="output"):
        package, _ = packaging.create_package(self.binary, self.root / output, "fixture", True, False,
                                               "linux", "x86_64", None)
        return package, json.loads((package / "package-manifest.json").read_text())

    def git(self, *arguments):
        return subprocess.run(["git", "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                               "-c", "commit.gpgsign=false", "-C", str(self.source), *arguments],
                              check=True, capture_output=True, text=True)

    def test_checkout_ships_only_tracked_samples(self):
        if shutil.which("git") is None:
            self.skipTest("git is unavailable")
        self.git("init", "-q")
        self.git("add", "samples/README.md", "samples/projects/quake-minimal/maps/start.map")
        self.git("commit", "-q", "-m", "Track the shipped samples")
        (self.samples / "projects/quake-minimal/maps/local-notes.txt").write_text("not tracked\n")
        (self.samples / "projects/quake2-sample").mkdir()
        (self.samples / "projects/quake2-sample/local.map").write_text("ignored local project\n")
        (self.source / ".gitignore").write_text("/samples/projects/quake2-sample/\n")
        self.write_long_untracked()
        package, manifest = self.stage_samples()
        self.assertEqual(manifest["includedSamples"], True)
        self.assertEqual(manifest["samplesSelection"], "git-tracked")
        self.assertEqual((package / "samples/README.md").read_text(), "Sample projects\n")
        self.assertTrue((package / "samples/projects/quake-minimal/maps/start.map").exists())
        self.assertFalse((package / "samples/projects/quake-minimal/maps/local-notes.txt").exists())
        self.assertFalse((package / "samples/projects/quake2-sample").exists())
        checksums = (package / "CHECKSUMS.sha256").read_text()
        self.assertIn("samples/README.md", checksums)
        self.assertNotIn("local-notes.txt", checksums)
        # Tracked files deleted locally are not an error either.
        (self.samples / "projects/quake-minimal/maps/start.map").unlink()
        package, _ = self.stage_samples(output="deleted")
        self.assertTrue((package / "samples/README.md").exists())
        self.assertFalse((package / "samples/projects/quake-minimal/maps/start.map").exists())

    def test_source_archive_without_git_ships_the_working_tree(self):
        (self.samples / "projects/quake-minimal/maps/local-notes.txt").write_text("shipped\n")
        (self.samples / "projects/quake-minimal/__pycache__").mkdir()
        (self.samples / "projects/quake-minimal/__pycache__/cache.pyc").write_bytes(b"")
        package, manifest = self.stage_samples()
        self.assertEqual(manifest["samplesSelection"], "working-tree")
        self.assertEqual((package / "samples/projects/quake-minimal/maps/local-notes.txt").read_text(), "shipped\n")
        self.assertFalse((package / "samples/projects/quake-minimal/__pycache__").exists())

    def test_over_long_destination_names_the_file_instead_of_failing_midway(self):
        path = self.write_long_untracked()
        with self.assertRaisesRegex(ValueError, "too long for Windows") as raised:
            self.stage_samples()
        self.assertIn(path.name, str(raised.exception))
        self.assertIn("samples", str(raised.exception))

    def test_samples_can_be_left_out(self):
        package, manifest = self.stage()
        self.assertFalse((package / "samples").exists())
        self.assertEqual(manifest["includedSamples"], False)
        self.assertEqual(manifest["samplesSelection"], "")


if __name__ == "__main__":
    unittest.main()
