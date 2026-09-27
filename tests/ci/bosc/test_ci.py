"""Offline negative tests for the CI harness. GPL-2.0-or-later."""
import hashlib
import io
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import tarfile
import unittest
from unittest.mock import patch

import assets
import checks
from common import HERE, catalog, run_logged, write_json
from report import aggregate
from selection import changed_paths, choose, parse_status, select
from dependencies import prepare_dependencies, safe_members


def boot_text(token="TOKEN", cpus="0-15"):
    return ("OpenSBI\nLinux version\nBOSC KMH single-die SoC\n"
            "EXT4-fs (pmem0): mounted filesystem\n"
            "Switching root to overlay(root=/dev/pmem0)\n"
            f"{token}:BEGIN\n{token}:CPU\n{cpus}\n{token}:KERNEL\n"
            "20260824_6.18_g3520522921abc-minimal\n"
            f"{token}:MOUNTS\noverlay / overlay rw,lowerdir=/lower_root 0 0\n"
            f"{token}:RC=0\n{token}:END\n")


def itgen_text():
    return ("target-index=32 die-id=2\nimsic=0x20001c000000 clear=0x10000409014c\n"
            "controller=0 target=16 claimed=56\n"
            "PASS: test_RISCV_AIA_aplic_irq_priority_cross_die\n"
            "1 tests run, 0 failed, 1 passed\n")


def mcu_text(die, count):
    stages = [("start", count), ("qspi", 0)]
    stages += [("copy-begin", 0x8000000), ("copy-done", 0x80000000)] if not die else [("copy-seen", 1)]
    stages += [("ready", 1), ("release", 2)]
    return "".join(f"[kmh-mcu] die={die} uncore={die+1} stage={s} extra=0x{v:08x}\n" for s, v in stages)


class SelectionTests(unittest.TestCase):
    def owners(self, paths):
        chosen = choose(paths)["selected"]
        return {c["owner"] for c in catalog() if c["id"] in chosen}

    def test_bosc(self):
        self.assertEqual(self.owners(["hw/riscv/kmh_bosc_soc.c"]), {"bosc"})

    def test_kmh(self):
        self.assertEqual(self.owners(["hw/riscv/xiangshan_kmh.c", "include/hw/riscv/xiangshan_kmh.h"]), {"kmh"})

    def test_shared_unknown_dmac_ci(self):
        for path in ("hw/intc/riscv_aplic.c", "hw/dma/dw-axi-dmac.c", "new/device.c", "tests/ci/bosc/cases.json", "hw/riscv/meson.build"):
            self.assertEqual(self.owners([path]), {"bosc", "kmh"})

    def test_docs_and_union(self):
        self.assertEqual(self.owners(["docs/system/riscv/kmh.rst"]), set())
        self.assertEqual(self.owners(["docs/run.sh"]), {"kmh", "bosc"})
        self.assertEqual(self.owners(["hw/riscv/xiangshan_kmh.c", "hw/riscv/kmh_bosc_soc.c"]), {"kmh", "bosc"})

    def test_rename_delete_with_whitespace(self):
        def git_mock(*args, **kwargs):
            return b"base" if args[0] == "merge-base" else b"R100\0old name.c\0new name.c\0D\0deleted.c\0"
        with patch("selection.git", side_effect=git_mock):
            self.assertEqual(changed_paths("base", "head"), ["old name.c", "new name.c", "deleted.c"])

    def test_missing_diff_fallback(self):
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, {}, clear=True):
            with patch("selection.changed_paths", side_effect=RuntimeError("missing")):
                result = select(Path(tmp), "unknown")
            self.assertEqual(len(result["selected"]), len(catalog()))

    def test_incomplete_diff_rejected(self):
        with self.assertRaises(ValueError):
            parse_status(b"R100\0only-source\0")

    def test_mr_base_from_environment(self):
        env = {"CI_PIPELINE_SOURCE": "merge_request_event", "CI_MERGE_REQUEST_DIFF_BASE_SHA": "base", "CI_COMMIT_SHA": "head"}
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, env, clear=True):
            with patch("selection.changed_paths", return_value=["hw/riscv/xiangshan_kmh.c"]) as diff:
                result = select(Path(tmp))
            diff.assert_called_once_with("base", "head")
            self.assertEqual(result["selected"], ["kmh-linux-1", "kmh-linux-16"])

    def test_schedule_ignores_path_filter(self):
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, {"CI_PIPELINE_SOURCE": "schedule"}, clear=True):
            self.assertEqual(len(select(Path(tmp), "base")["selected"]), len(catalog()))


class AssetTests(unittest.TestCase):
    def test_missing_hash_and_size(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "asset"
            spec = {"size": 3, "sha256": hashlib.sha256(b"abc").hexdigest()}
            with self.assertRaises(ValueError):
                assets.verify(p, spec)
            p.write_bytes(b"abc")
            self.assertEqual(assets.verify(p, spec), p)
            p.write_bytes(b"abd")
            with self.assertRaises(ValueError):
                assets.verify(p, spec)
            p.write_bytes(b"abcde")
            with self.assertRaises(ValueError):
                assets.verify(p, spec)

    def test_unsafe_download_locations(self):
        for base, rel in (("http://example.test", "file"), ("https://user:secret@example.test", "file"), ("https://example.test", "../secret")):
            with self.assertRaises(ValueError):
                assets.download(Path("unused"), base, rel, None, {"size": 1})

    def test_redirect_refused(self):
        with self.assertRaises(ValueError):
            assets.NoRedirect().redirect_request(None, None, 302, "", {}, "https://elsewhere/")

    def test_http_requires_exact_private_origin_opt_in(self):
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaises(ValueError):
                assets.validate_base("http://10.13.18.1:8082/artifactory")
        with patch.dict(os.environ, {"BOSC_CI_ALLOW_HTTP_ORIGIN": "http://10.13.18.1:8082"}):
            assets.validate_base("http://10.13.18.1:8082/artifactory")
            assets.validate_base("https://example.org/artifactory")
            for url in ("http://10.13.18.2:8082/artifactory", "http://10.13.18.1:80/artifactory",
                        "http://token@10.13.18.1:8082/artifactory", "http://example.org"):
                with self.assertRaises(ValueError):
                    assets.validate_base(url)

    def test_no_cases_does_not_need_assets(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(assets.prepare(Path(tmp), []), {})

    def test_selected_missing_asset_fails(self):
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, {}, clear=True):
            case = next(c for c in catalog() if c["kind"] == "itgen")
            with self.assertRaises(ValueError):
                assets.prepare(Path(tmp), [case])

    def test_offline_asset_copy_is_verified_and_readonly(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "source.bin"
            source.write_bytes(b"abc")
            specs = {"assets": {"sample": {"path": "pkg/input.bin", "size": 3,
                     "sha256": hashlib.sha256(b"abc").hexdigest()}}}
            with patch("assets.read_json", return_value=specs):
                paths = assets.prepare(Path(tmp) / "run", [{"assets": ["sample"], "group": "test"}], overrides={"sample": source})
            dest = Path(paths["sample"])
            self.assertEqual(dest.read_bytes(), b"abc")
            self.assertEqual(dest.stat().st_mode & 0o222, 0)


class DependencyTests(unittest.TestCase):
    def test_reject_archive_traversal_and_links(self):
        for name, kind in [('dep/../../escape', tarfile.REGTYPE),
                           ('other/file', tarfile.REGTYPE),
                           ('dep/link', tarfile.SYMTYPE)]:
            data = io.BytesIO()
            with tarfile.open(fileobj=data, mode='w') as archive:
                member = tarfile.TarInfo(name)
                member.type = kind
                archive.addfile(member)
            data.seek(0)
            with tarfile.open(fileobj=data) as archive, self.assertRaises(ValueError):
                safe_members(archive, 'dep')

    def test_restore_verify_and_revision_mismatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprojects = root / 'source/subprojects'
            subprojects.mkdir(parents=True)
            (subprojects / 'dep.wrap').write_text('[wrap-git]\nrevision = abc\n')
            overlay = subprojects / 'packagefiles/dep'
            overlay.mkdir(parents=True)
            (overlay / 'meson.build').write_text('current-overlay')
            archive_path = root / 'dep.tar'
            with tarfile.open(archive_path, 'w') as archive:
                member = tarfile.TarInfo('dep/code.c')
                member.size = 3
                archive.addfile(member, io.BytesIO(b'abc'))
            specs = {'assets': {'dep': {'revision': 'abc', 'sha256': 'hash'}}}
            with patch('dependencies.read_json', return_value=specs), \
                 patch('dependencies.prepare', return_value={'dep': str(archive_path)}):
                for _ in range(2):
                    prepare_dependencies(root / 'run', [{}], source=root / 'source')
                self.assertEqual((subprojects / 'dep/code.c').read_bytes(), b'abc')
                self.assertEqual((subprojects / 'dep/meson.build').read_text(), 'current-overlay')
                (subprojects / 'dep/code.c').write_bytes(b'bad')
                with self.assertRaises(ValueError):
                    prepare_dependencies(root / 'run', [{}], source=root / 'source')
                (subprojects / 'dep.wrap').write_text('[wrap-git]\nrevision = changed\n')
                with self.assertRaises(ValueError):
                    prepare_dependencies(root / 'run', [{}], source=root / 'source')


class CheckTests(unittest.TestCase):
    case = {"owner": "bosc", "mode": "single", "cpus": 16}

    def test_linux_valid(self):
        self.assertEqual(checks.linux_assertions(boot_text(), "TOKEN", self.case)["online"], "0-15")

    def test_fake_pass_and_echo(self):
        for text in ("PASS\n", "echo TOKEN:BEGIN; cat /sys/devices/system/cpu/online; echo TOKEN:END\n", boot_text().replace("TOKEN:END", "echo TOKEN:END")):
            with self.assertRaises(AssertionError):
                checks.linux_assertions(text, "TOKEN", self.case)

    def test_missing_cpu_panic_bad_root(self):
        for text in (boot_text(cpus="0-14"), boot_text()+"Kernel panic\n", boot_text().replace("overlay / overlay", "tmpfs / tmpfs"), boot_text().replace("TOKEN:RC=0", "TOKEN:RC=1")):
            with self.assertRaises(AssertionError):
                checks.linux_assertions(text, "TOKEN", self.case)

    def test_itgen_valid_completion_tail(self):
        text = itgen_text()
        checks.itgen_assertions(text)
        text += ("TRAP in test_RISCV_AIA_aplic_irq_priority_cross_die: priv=M hart=0000 mcause=0x0002 mtval=0x005006b mepc=0x8000 mstatus=0x0\n"
                 "FATAL: double fault in test_RISCV_AIA_aplic_irq_priority_cross_die, halting test runner\n")
        checks.itgen_assertions(text)
        for invalid in (text.replace("5006b", "5006c"), "TRAP before success\n"+text, text+"FATAL: other\n", text.replace("claimed=56", "claimed=55")):
            with self.assertRaises(AssertionError):
                checks.itgen_assertions(invalid)

    def test_mcu_precise_xfail_xpass(self):
        self.assertEqual(checks.mcu_assertions([mcu_text(0, 0), "", "", ""], [0])["status"], "XFAIL")
        self.assertEqual(checks.mcu_assertions([mcu_text(0, 1), "", "", ""], [0])["status"], "XPASS")
        self.assertEqual(checks.mcu_assertions([mcu_text(d, 0) for d in range(4)], list(range(4)))["status"], "XFAIL")
        for logs in (["", "", "", ""], [mcu_text(0, 2), "", "", ""], [mcu_text(0, 0), mcu_text(1, 0), "", ""], [mcu_text(0, 0).replace("stage=release", "stage=wrong"), "", "", ""]):
            with self.assertRaises(AssertionError):
                checks.mcu_assertions(logs, [0])

    def test_qtest_missing_skipped(self):
        checks.qtest_assertions("ok 1 /a\nok 2 /b\n", ["/a", "/b"])
        checks.qtest_assertions("ok 1 /b\nok 2 /a\n", ["/a", "/b"])
        for text in ("ok 1 /a\n", "ok 1 /a # SKIP\n", "not ok 1 /a\n"):
            with self.assertRaises(AssertionError):
                checks.qtest_assertions(text, ["/a", "/b"])


class ProcessAndReportTests(unittest.TestCase):
    def test_timeout_cleans_group(self):
        with tempfile.TemporaryDirectory() as tmp:
            pidfile = Path(tmp) / "pid"
            code = ("import os,time; "
                    f"open({str(pidfile)!r},'w').write(str(os.getpid())); time.sleep(30)")
            start = time.monotonic()
            with self.assertRaises(subprocess.TimeoutExpired):
                run_logged([sys.executable, "-c", code], Path(tmp) / "timeout.log", timeout=0.3)
            self.assertLess(time.monotonic() - start, 7)
            with self.assertRaises(ProcessLookupError):
                os.kill(int(pidfile.read_text()), 0)

    def test_cancellation_cleans_only_owned_process(self):
        with tempfile.TemporaryDirectory() as tmp:
            pidfile = Path(tmp) / "child.pid"
            child = "import os,time; " + f"open({str(pidfile)!r},'w').write(str(os.getpid())); time.sleep(60)"
            outer = ("import signal,sys; from common import run_logged; "
                     "signal.signal(signal.SIGTERM, lambda *a: sys.exit(130)); "
                     f"run_logged([sys.executable,'-c',{child!r}],{str(Path(tmp)/'child.log')!r},timeout=60)")
            unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
            proc = subprocess.Popen([sys.executable, "-c", outer], cwd=HERE,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                deadline = time.monotonic() + 5
                while not pidfile.exists() and time.monotonic() < deadline:
                    time.sleep(0.02)
                self.assertTrue(pidfile.exists())
                proc.terminate()
                self.assertEqual(proc.wait(timeout=8), 130)
                with self.assertRaises(ProcessLookupError):
                    os.kill(int(pidfile.read_text()), 0)
                self.assertIsNone(unrelated.poll())
            finally:
                if proc.poll() is None:
                    proc.kill()
                proc.wait(timeout=5)
                unrelated.terminate()
                unrelated.wait(timeout=5)

    def test_missing_result_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            selected = choose(full_reason="test")
            selected["run_id"] = "test-run"
            write_json(work / "selected-tests.json", selected)
            result = aggregate(work)
            self.assertEqual(result["status"], "FAIL")
            self.assertEqual(len(result["missing_or_invalid"]), len(catalog()))

    def test_not_selected_is_not_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            selection = choose(["docs/example.md"])
            selection["run_id"] = "test-run"
            write_json(work / "selected-tests.json", selection)
            report = aggregate(work)
            self.assertEqual(report["counts"], {"NOT_SELECTED": len(catalog())})

    def test_xpass_blocks(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            selection = choose([])
            selection["selected"] = ["bosc-mcu-single"]
            selection["run_id"] = "test-run"
            write_json(work / "selected-tests.json", selection)
            write_json(work / "results/bosc-mcu-single/result.json", {
                "id": "bosc-mcu-single", "status": "PASS",
                "selection_sha256": selection["config_sha256"],
                "run_id": selection["run_id"],
                "checks": [{"id": "MCU-DIE-NUM-RESET", "status": "XPASS"}]})
            self.assertEqual(aggregate(work)["status"], "FAIL")

    def test_stale_result_and_missing_assertion_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            selection = choose([])
            selection.update({"selected": ["qtest-die"], "run_id": "current"})
            write_json(work / "selected-tests.json", selection)
            result = {"id": "qtest-die", "status": "PASS", "run_id": "old",
                      "selection_sha256": selection["config_sha256"], "checks": []}
            path = work / "results/qtest-die/result.json"
            write_json(path, result)
            self.assertEqual(aggregate(work)["status"], "FAIL")
            result["run_id"] = "current"
            write_json(path, result)
            self.assertEqual(aggregate(work)["status"], "FAIL")


if __name__ == "__main__":
    unittest.main()
