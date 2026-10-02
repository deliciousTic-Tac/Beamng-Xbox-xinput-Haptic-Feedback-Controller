"""Exercise the release guardrails in isolated temporary copies (no DLL loading)."""
from pathlib import Path
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest
import zipfile

PROJECT = Path(__file__).resolve().parents[1]


class ArtifactTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="haptics-packaging-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.project = self.root / "xbox_proxy"
        (self.project / "tools").mkdir(parents=True)
        (self.project / "tests").mkdir()
        mod = self.root / "revlimiter_haptics" / "mod"
        mod.mkdir(parents=True)
        (mod / "fixture.lua").write_text("return {}", encoding="utf-8")
        for name in ("xinput_proxy.cpp", "xinput_proxy.def", "build.cmd", "version.h", "version.rc", "load_test.cpp", "README.md"):
            shutil.copy2(PROJECT / name, self.project / name)
        shutil.copy2(PROJECT / "tools/artifacts.ps1", self.project / "tools/artifacts.ps1")

    def invoke(self, action, *args):
        return subprocess.run(
            ["powershell.exe", "-NoProfile", "-File", str(self.project / "tools/artifacts.ps1"),
             "-Action", action, "-Mode", "test", *args], capture_output=True, text=True,
            errors="replace", cwd=self.root, timeout=30,
        )

    def test_no_build_receipt_no_package(self):
        result = self.invoke("Package")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("No build receipt", result.stderr)

    def test_source_drift_refuses_package(self):
        result = self.invoke("Begin")
        self.assertEqual(result.returncode, 0, result.stderr)
        with (self.project / "README.md").open("a", encoding="utf-8") as stream:
            stream.write("\nSource changed after compilation started.\n")
        result = self.invoke("Package")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Sources changed", result.stderr)

    def test_old_dll_rejected(self):
        if not (PROJECT / "XInput1_4.dll").exists():
            self.skipTest("Historical DLL not present (optional negative fixture)")
        result = self.invoke("Begin")
        self.assertEqual(result.returncode, 0, result.stderr)
        shutil.copy2(PROJECT / "XInput1_4.dll", self.project / "out/test/XInput1_4.dll")
        result = self.invoke("Finish")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Wrong DLL VERSIONINFO", result.stderr)
        state = json.loads((self.project / "out/test/build-state.json").read_text(encoding="utf-8-sig"))
        self.assertEqual(state["Status"], "building")

    def test_inspector_matches_historical_exports(self):
        if not (PROJECT / "XInput1_4.dll").exists():
            self.skipTest("Historical DLL not present")
        result = self.invoke("Inspect", "-Path", str(PROJECT / "XInput1_4.dll"))
        self.assertEqual(result.returncode, 0, result.stderr)
        facts = json.loads(result.stdout)
        self.assertEqual([e["Ordinal"] for e in facts["Exports"]], [1,2,3,4,5,7,8,10,100,101,102,103,104,108,109])

    def test_release_requires_certificate(self):
        env = dict(os.environ, CODE_SIGN_CERT_THUMBPRINT="")
        result = subprocess.run([str(self.project / "build.cmd"), "release"], cwd=self.root,
                                env=env, capture_output=True, text=True, errors="replace", timeout=30)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires CODE_SIGN_CERT_THUMBPRINT", result.stdout)

    def test_build_from_other_directory_reports_missing_toolchain(self):
        env = dict(os.environ, HAPTICS_VS_ROOT=str(self.root / "nonexistent-toolchain"))
        result = subprocess.run([str(self.project / "build.cmd"), "test"], cwd=self.root,
                                env=env, capture_output=True, text=True, errors="replace", timeout=30)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("MSVC x64 and Windows SDK are missing", result.stdout)

    def test_package_uses_portable_zip_paths(self):
        candidate = PROJECT / "out/test/XInput1_4.dll"
        if not candidate.exists():
            self.skipTest("Compiled test DLL not present")
        expected = re.search(r'HAPTICS_VERSION_STRING\s+"([^"]+)"',
                             (PROJECT / "version.h").read_text(encoding="utf-8"))
        inspected = self.invoke("Inspect", "-Path", str(candidate))
        self.assertEqual(inspected.returncode, 0, inspected.stderr)
        if json.loads(inspected.stdout)["Version"] != expected.group(1):
            self.skipTest("Compiled DLL is from an older version; rebuild first")
        result = self.invoke("Begin")
        self.assertEqual(result.returncode, 0, result.stderr)
        output = self.project / "out/test"
        shutil.copyfile(candidate, output / "XInput1_4.dll")
        for filename, contents in (
            ("native-tests.log", "PASS: native regression suite\n"),
            ("lua-tests.log", "OK\n"),
            ("artifact-tests.log", "OK\n"),
        ):
            (output / filename).write_text(contents, encoding="utf-8")
        result = self.invoke("Finish")
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.invoke("Package")
        self.assertEqual(result.returncode, 0, result.stderr)
        archive = Path(result.stdout.strip())
        with zipfile.ZipFile(archive) as package:
            self.assertIn("Bin64/XInput1_4.dll", package.namelist())
            self.assertFalse(any("\\" in name for name in package.namelist()))
            mod_name = next(name for name in package.namelist() if name.startswith("mods/"))
            with package.open(mod_name) as mod_stream:
                with zipfile.ZipFile(mod_stream) as mod_zip:
                    self.assertEqual(mod_zip.namelist(), ["fixture.lua"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
