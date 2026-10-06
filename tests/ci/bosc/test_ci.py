"""Offline negative tests for the CI harness. GPL-2.0-or-later."""
import hashlib
import io
import contextlib
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
from live_log import CaseConsole, JobConsole, LINE_CHARS, TAIL_LINES


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
        return {c["owner"] for c in catalog() if c["id"] in chosen and "owners" not in c}

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
            self.assertEqual(len(result["selected"]), len([c for c in catalog() if c["tier"] == "mr"]))

    def test_incomplete_diff_rejected(self):
        with self.assertRaises(ValueError):
            parse_status(b"R100\0only-source\0")

    def test_mr_base_from_environment(self):
        env = {"CI_PIPELINE_SOURCE": "merge_request_event", "CI_MERGE_REQUEST_DIFF_BASE_SHA": "base", "CI_COMMIT_SHA": "head"}
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, env, clear=True):
            with patch("selection.changed_paths", return_value=["hw/riscv/xiangshan_kmh.c"]) as diff:
                result = select(Path(tmp))
            diff.assert_called_once_with("base", "head")
            self.assertEqual(result["selected"], ["qtest-pcie", "kmh-linux-1", "kmh-linux-16",
                                                   "kmh-iommu-linux", "kmh-iommu-linux-dual-root"] +
                             [c["id"] for c in catalog() if c["group"] == "libqemu" and c["tier"] == "mr"])

    def test_schedule_ignores_path_filter(self):
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, {"CI_PIPELINE_SOURCE": "schedule"}, clear=True):
            self.assertEqual(len(select(Path(tmp), "base")["selected"]), len(catalog()))


    def test_iommu_impact_and_tiers(self):
        extended = {"bosc-iommu-linux-multi", "bosc-iommu-linux-all-roots"}
        for path in ("hw/riscv/riscv-iommu.c", "hw/riscv/riscv-iommu.h",
                     "hw/pci-host/designware.c", "hw/pci/pci.c", "hw/nvme/ctrl.c"):
            result = choose([path])
            self.assertTrue({"kmh-iommu-linux", "kmh-iommu-linux-dual-root",
                             "bosc-iommu-linux-single"}.issubset(result["selected"]))
            self.assertTrue(extended.issubset(result["not_selected"]))
            self.assertTrue(extended.issubset(choose([path], extended=True)["selected"]))
        self.assertTrue(extended.issubset(choose(["unknown.c"])["not_selected"]))

    def test_pipeline_extended_rules(self):
        for env, expected in [
            ({"CI_PIPELINE_SOURCE": "merge_request_event"}, False),
            ({"CI_PIPELINE_SOURCE": "web"}, False),
            ({"CI_PIPELINE_SOURCE": "schedule"}, True),
            ({"CI_PIPELINE_SOURCE": "push", "CI_COMMIT_TAG": "v1"}, True),
            ({"CI_PIPELINE_SOURCE": "push", "CI_COMMIT_BRANCH": "main", "CI_DEFAULT_BRANCH": "main"}, True),
            ({"CI_PIPELINE_SOURCE": "merge_request_event", "BOSC_CI_FULL": "1"}, True),
        ]:
            with self.subTest(env=env), tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, env, clear=True):
                result = select(Path(tmp))
                self.assertEqual("bosc-iommu-linux-multi" in result["selected"], expected)
                self.assertEqual("bosc-iommu-linux-all-roots" in result["selected"], expected)
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, {}, clear=True):
            self.assertIn("bosc-iommu-linux-multi", select(Path(tmp), full=True)["selected"])


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


class IommuTests(unittest.TestCase):
    case = next(c for c in catalog() if c["id"] == "bosc-iommu-linux-single")

    def valid_log(self, segments=(0,), kmh=False):
        text = "iommu: Default domain type: Translated\n"
        rows = []
        for i, segment in enumerate(segments):
            bdf = f"{segment:04x}:01:00.0"
            addr = f"{0x311f0000 + segment * 0x1000:x}" if kmh else "4f80200000"
            text += f"riscv,iommu {addr}.iommu: using MSIs\n"
            text += f"pci {bdf}: Adding to iommu group {i}\nnvme nvme{i}: pci function {bdf}\nnvme nvme{i}: 1/0/0 default/read/poll queues\n"
            rows += [f"TOKEN:DEVICE=BOSC-CI-{i}|nvme{i}|{bdf}|{i}|DMA-FQ|live|nvme|riscv-iommu@{addr}.iommu",
                     f"TOKEN:DMA=BOSC-CI-{i}|{'a'*64}|{'a'*64}|10|20|1048576|direct|msi"]
        return text + "TOKEN:IOMMU_BEGIN\n" + "\n".join(rows) + f"\nTOKEN:NVME_COUNT={len(segments)}\nTOKEN:IOMMU_RC=0\nTOKEN:IOMMU_END\n"

    def test_valid_single_and_multi(self):
        self.assertEqual(len(checks.iommu_assertions(self.valid_log(), "TOKEN", self.case)), 1)
        case = next(c for c in catalog() if c["id"] == "bosc-iommu-linux-multi")
        result = checks.iommu_assertions(self.valid_log((0, 3, 6, 9)), "TOKEN", case)
        self.assertEqual([r["did"] for r in result], [0x100, 0x30100, 0x60100, 0x90100])

    def test_kmh_fixed_domain_numbering_and_unrelated_dmac_probe(self):
        case = next(c for c in catalog() if c["id"] == "kmh-iommu-linux")
        text = self.valid_log(kmh=True)
        result = checks.iommu_assertions(text, "TOKEN", case)
        self.assertEqual(result[0]["did"], 0x100)
        with self.assertRaises(AssertionError):
            checks.iommu_assertions(text.replace("0000:01:00.0", "0001:01:00.0"), "TOKEN", case)
        text = self.valid_log() + "dw_axi_dmac_platform b000000.dma-controller: probe with driver dw_axi_dmac_platform failed with error -2\n"
        checks.iommu_assertions(text, "TOKEN", self.case)

    def test_dual_and_all_roots(self):
        for case_id, segments, kmh in (("kmh-iommu-linux-dual-root", range(2), True),
                                       ("bosc-iommu-linux-all-roots", range(12), False)):
            case = next(c for c in catalog() if c["id"] == case_id)
            text = self.valid_log(segments, kmh)
            result = checks.iommu_assertions(text, "TOKEN", case)
            self.assertEqual([r["did"] for r in result],
                             [0x100 if kmh else (s << 16) | 0x100 for s in segments])
            self.assertEqual([r["bus"] for r in result], case["nvme_buses"])
            # Enumeration order is not controller/serial order (nvme10 sorts before nvme2).
            lines = text.splitlines()
            start, end = lines.index("TOKEN:IOMMU_BEGIN") + 1, lines.index(f"TOKEN:NVME_COUNT={len(segments)}")
            lines[start:end] = reversed(lines[start:end])
            self.assertEqual(len(checks.iommu_assertions("\n".join(lines), "TOKEN", case)), len(segments))
            for old, new in (("0001:01:00.0", "0000:01:00.0"),
                             ("|1|DMA-FQ", "|0|DMA-FQ"),
                             ("BOSC-CI-1|", "BOSC-CI-0|"),
                             ("TOKEN:DMA=BOSC-CI-1|", "echo TOKEN:DMA=BOSC-CI-1|")):
                with self.subTest(case=case_id, old=old), self.assertRaises(AssertionError):
                    checks.iommu_assertions(text.replace(old, new), "TOKEN", case)
            if kmh:
                with self.assertRaises(AssertionError):
                    checks.iommu_assertions(text.replace("311f1000", "311f0000"), "TOKEN", case)
            else:
                for index in (10, 11):
                    with self.assertRaises(AssertionError):
                        checks.iommu_assertions(text.replace(f"TOKEN:DMA=BOSC-CI-{index}|", "missing|"), "TOKEN", case)

    def test_missing_bad_and_echo_evidence(self):
        good = self.valid_log()
        for old, new in [("Translated", "Passthrough"), ("using MSIs", "using wired interrupts"),
                         ("DMA-FQ", "identity"), ("|live|", "|dead|"), ("|10|20|", "|20|20|"),
                         ("a"*64 + "|" + "a"*64, "a"*64 + "|" + "b"*64),
                         ("NVME_COUNT=1", "NVME_COUNT=0"), ("IOMMU_RC=0", "IOMMU_RC=1"),
                         ("TOKEN:DEVICE=", "echo TOKEN:DEVICE="), ("Adding to iommu group", "group"),
                         ("default/read/poll queues", "no queues"), ("|direct|", "|buffered|"),
                         ("BOSC-CI-0", "BOSC-CI-1"), ("TOKEN:IOMMU_END", "")]:
            with self.subTest(old=old), self.assertRaises(AssertionError):
                checks.iommu_assertions(good.replace(old, new), "TOKEN", self.case)
        with self.assertRaises(AssertionError):
            checks.iommu_assertions("/sys/class/iommu\n/dev/nvme0n1\n", "TOKEN", self.case)
        with self.assertRaises(AssertionError):
            checks.iommu_assertions(good, "TOKEN", dict(self.case, disks=4))

    def test_fatal_uart_and_qemu(self):
        for error in ("IOMMU fault", "nvme nvme0: I/O 1 timeout", "I/O error", "Kernel panic", "riscv-iommu: translation fault"):
            with self.subTest(error=error), self.assertRaises(AssertionError):
                checks.iommu_assertions(self.valid_log()+error, "TOKEN", self.case)
            with self.assertRaises(AssertionError):
                checks.iommu_assertions(self.valid_log(), "TOKEN", self.case, error)

    def test_sparse_private_disks_and_failure_cleanup(self):
        from runner import nvme_disks, cleanup_nvme
        from common import read_json
        scenarios = [(c, e) for c in catalog() if c["kind"] == "iommu-linux"
                     for e in (None, RuntimeError, KeyboardInterrupt)]
        for case, error in scenarios:
            with tempfile.TemporaryDirectory() as tmp:
                work = Path(tmp)
                directory = work / "results" / case["id"]
                directory.mkdir(parents=True)
                try:
                    with nvme_disks(work, case, directory) as args:
                        disks = list((work / "scratch" / case["id"]).glob("*.raw"))
                        self.assertEqual(len(disks), case["disks"])
                        for disk in disks:
                            self.assertEqual(disk.stat().st_size, 64*1024*1024)
                            self.assertEqual(disk.stat().st_blocks, 0)
                        for i, bus in enumerate(case["nvme_buses"]):
                            self.assertIn(f"nvme,drive=nvme{i},bus={bus},addr=0,serial=BOSC-CI-{i}", args)
                        if error:
                            raise error()
                except (RuntimeError, KeyboardInterrupt):
                    pass
                self.assertFalse(any(d.exists() for d in disks))
                self.assertTrue(read_json(directory / "nvme-disks.json")["cleaned"])
                cleanup_nvme(work, case, directory)  # Parent cleanup is idempotent.

    def test_parent_cleans_killed_worker_disks(self):
        import ci
        from types import SimpleNamespace
        from runner import nvme_disks
        from common import read_json
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            write_json(work / "selected-tests.json", {"config_sha256": "hash", "run_id": "run"})
            args = SimpleNamespace(assets_dir=None, asset=[], cache_dir=None, jf_paths=None)
            held = []
            def killed(*args, **kwargs):
                directory = work / "results" / self.case["id"]
                ctx = nvme_disks(work, self.case, directory)
                ctx.__enter__()
                held.append(ctx)  # Simulate SIGKILL, so the worker finally did not run.
                raise RuntimeError("worker killed")
            with patch("ci.prepare", return_value={}), patch("ci.run_logged", side_effect=killed), contextlib.redirect_stdout(io.StringIO()):
                ci.run_cases(args, work, [self.case])
            self.assertFalse(list((work / "scratch").rglob("*.raw")))
            self.assertTrue(read_json(work / "results" / self.case["id"] / "nvme-disks.json")["cleaned"])
            held[0].__exit__(None, None, None)

    def test_schema_and_kernel_lock(self):
        from lint_ci import validate_cases
        from common import read_json
        specs = read_json(HERE / "images.lock.json")["assets"]
        validate_cases(catalog(), specs)
        self.assertEqual(specs["iommu_kernel"]["jf_path"], "xs-sw/st-release/v0.9.99/xiangshan/Image")
        for edits in ({"assets": ["fw_jump", "kernel", "rootfs"]}, {"disks": 4},
                      {"tier": "xfail"}, {"nvme_buses": ["wrong"]}):
            with self.assertRaises(ValueError):
                validate_cases([dict(self.case, **edits)], specs)
        for case_id in ("kmh-iommu-linux-dual-root", "bosc-iommu-linux-all-roots"):
            case = next(c for c in catalog() if c["id"] == case_id)
            for edits in ({"nvme_buses": case["nvme_buses"][:-1]}, {"topology": "single-root"},
                          {"cpus": 16}, {"owner": "other"}):
                with self.assertRaises(ValueError):
                    validate_cases([dict(case, **edits)], specs)
        case = next(c for c in catalog() if c["id"] == "bosc-iommu-linux-all-roots")
        for edits in ({"mode": "single"}, {"die_mask": "0x1"}, {"tier": "mr"}):
            with self.assertRaises(ValueError):
                validate_cases([dict(case, **edits)], specs)

    def test_iommu_missing_result_is_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            select(work, case_ids=["bosc-iommu-linux-multi"])
            self.assertEqual(aggregate(work, "iommu")["status"], "FAIL")

    def test_report_rejects_pass_without_endpoint_evidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            selected = select(work, case_ids=[self.case["id"]])
            write_json(work / "results" / self.case["id"] / "result.json",
                       {"id": self.case["id"], "status": "PASS", "checks": [],
                        "run_id": selected["run_id"], "selection_sha256": selected["config_sha256"]})
            self.assertEqual(aggregate(work, "iommu")["status"], "FAIL")

    def test_new_qtest_source_is_part_of_build_identity(self):
        import common
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            src = root / "tests/qtest/new.c"
            src.parent.mkdir(parents=True)
            src.write_text("first")
            def git_mock(*args, **kwargs):
                return b"tests/qtest/new.c\0" if args[0] == "ls-files" else b"HEAD"
            with patch("common.SOURCE", root), patch("common.git", side_effect=git_mock):
                first = common.source_identity()
                src.write_text("changed")
                second = common.source_identity()
            self.assertNotEqual(first["untracked_sources"], second["untracked_sources"])


class LibqemuTests(unittest.TestCase):
    cases = [c for c in catalog() if c["group"] == "libqemu"]

    def test_selection_and_schema(self):
        from lint_ci import validate_cases
        specs = assets.read_json(HERE / "images.lock.json")["assets"]
        validate_cases(self.cases, specs)
        for path in ("libqemu/core.c", "libqemu/compose-boot.c", "tests/libqemu/rom.c",
                     "system/vl.c", "util/main-loop.c", "chardev/char-stdio.c"):
            self.assertTrue({c["id"] for c in self.cases if c["tier"] == "mr"}.issubset(choose([path])["selected"]))
        self.assertTrue({c["id"] for c in self.cases}.isdisjoint(
            choose(["docs/devel/libqemu.rst"])["selected"]))
        for field, value in (("binary", "qemu-system-riscv64"), ("assets", ["iommu_kernel"]),
                             ("split_init", "yes"), ("cpus", 1)):
            case = dict(next(c for c in self.cases if c["kind"] == "libqemu-linux"))
            case[field] = value
            with self.assertRaises(ValueError):
                validate_cases([case], specs)

    def test_host_survival_rejects_echo_wrong_status_and_fatal(self):
        good = "HOST_STARTED\nHOST_CONTINUES status=7\n"
        checks.libqemu_host_assertions(good, 7)
        for bad in ("echo " + good, good.replace("status=7", "status=0"),
                    good + "Kernel panic\n", good + good, "HOST_CONTINUES status=7\n"):
            with self.assertRaises(AssertionError):
                checks.libqemu_host_assertions(bad, 7)

    def test_report_requires_library_evidence(self):
        for case in self.cases:
            with self.subTest(case=case["id"]), tempfile.TemporaryDirectory() as tmp:
                work = Path(tmp)
                selection = select(work, case_ids=[case["id"]])
                write_json(work / "results" / case["id"] / "result.json",
                           {"id": case["id"], "status": "PASS", "checks": [],
                            "selection_sha256": selection["config_sha256"],
                            "run_id": selection["run_id"], "evidence": {}})
                self.assertEqual(aggregate(work, "libqemu")["status"], "FAIL")
        case = next(c for c in self.cases if c["kind"] == "libqemu-exit")
        evidence = {"exit_statuses": [0, 7], "host_continues": [0, 7], "split_init": False}
        checks.libqemu_evidence(case, evidence)
        for field, value in (("exit_statuses", [0]), ("host_continues", []), ("split_init", True)):
            with self.assertRaises(ValueError):
                checks.libqemu_evidence(case, dict(evidence, **{field: value}))

    def test_library_hash_and_source_identity(self):
        import build
        from common import sha256
        case = self.cases[0]
        identity = {"revision": "head", "tracked_diff_sha256": "diff", "untracked_sources": {}}
        with tempfile.TemporaryDirectory() as tmp, patch("build.source_identity", return_value=identity):
            work = Path(tmp)
            bundle = build.bundle_dir(work, case)
            files = {}
            for target in build.case_targets(case):
                path = bundle / target
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("binary")
                files[target] = sha256(path)
            manifest = {"status": "PASS", "profile": "libqemu", "source": identity, "files": files}
            write_json(bundle / "build.json", manifest)
            with patch("build.subprocess.run", return_value=subprocess.CompletedProcess([], 0, "libc")):
                build.verify_bundle(work, case)
                (bundle / "libqemu-system-riscv64.so").write_text("changed")
                with self.assertRaisesRegex(ValueError, "artifact hash mismatch"):
                    build.verify_bundle(work, case)
            manifest["profile"] = "system"
            write_json(bundle / "build.json", manifest)
            with self.assertRaisesRegex(ValueError, "library build"):
                build.verify_bundle(work, case)

    def test_expected_nonzero_exit_is_not_a_blanket_waiver(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "exit.log"
            run_logged([sys.executable, "-c", "raise SystemExit(7)"], log, expected_exit=7)
            with self.assertRaises(RuntimeError):
                run_logged([sys.executable, "-c", "raise SystemExit(0)"], log, expected_exit=7)

    def test_linux_timeout_cleans_owned_host(self):
        from libqemu_runner import linux_host
        from common import read_json
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            script = directory / "host.py"
            script.write_text("import time\nprint('HOST_STARTED', flush=True)\ntime.sleep(60)\n")
            case = dict(next(c for c in self.cases if c["kind"] == "libqemu-linux"), timeout=0.2)
            with self.assertRaises(TimeoutError):
                linux_host([sys.executable, str(script)], case, directory)
            cleanup = read_json(directory / "cleanup.json")
            self.assertFalse(cleanup["running"])
            self.assertTrue(cleanup["forced"])


class ComposeLinuxTests(unittest.TestCase):
    cases = [c for c in catalog() if c["kind"] == "libqemu-compose-linux"]

    def fixture(self, case):
        from compose_checks import device_mode
        body = ""
        for name in case["devices"]:
            path = ("/sys/devices/platform/310a0000.virtio/virtio0/block/vda" if name == "vda"
                    else "/sys/devices/platform/32000000.pcie/0000:01:00.0/nvme/nvme0/nvme0n1")
            serial = "-" if name == "vda" else "LIBQEMU-KMH-NVME"
            body += f"T:DEVICE={name}|{path}|{serial}\n"
            irq = "APLIC-MSI- 15 Edge virtio0" if name == "vda" else "PCI-MSIX-0000:01:00.0 1-edge nvme0q1"
            body += f"T:IRQ_BEFORE={name}\n 18: 4 {irq}\n"
            body += f"T:IRQ_AFTER={name}\n 18: 7 {irq}\n"
            body += f"T:DISK={name}|{'a'*64}|{'a'*64}|262144|unmounted\n"
        return (f"HOST_ASSEMBLED Kunminghu: devices={device_mode(case)} builder={case['builder']}\n"
                "OpenSBI\nLinux version\nMachine model: Host-assembled Xiangshan Kunminghu\n"
                "T:COMPOSE_BEGIN\nT:CPU\n0\nT:KERNEL\n20260824_6.18_g3520522921abc-xiangshan\n"
                "T:MODEL\nHost-assembled Xiangshan Kunminghu\nT:MOUNTS_BEFORE\nrootfs / rootfs rw 0 0\n"
                "T:DEVICES\n" + body + "T:MOUNTS_AFTER\nrootfs / rootfs rw 0 0\n"
                "T:COMPOSE_RC=0\nT:COMPOSE_END\n")

    def test_guest_and_report_contracts(self):
        from compose_checks import guest_assertions, validate_evidence
        for case in self.cases:
            text = self.fixture(case)
            evidence = guest_assertions(text, "T", case)
            evidence.update({"dt": {"sha256": "a"*64}, "exitcode": 0, "cleaned": True,
                             "host_alive_after_wait": True, "disk_fds_closed": True,
                             "fsck": {n: "PASS" for n in case["devices"]}})
            validate_evidence(case, evidence)
            for field in ("disk_fds_closed", "host_alive_after_wait", "cleaned", "dt"):
                bad = dict(evidence)
                del bad[field]
                with self.assertRaises(ValueError):
                    validate_evidence(case, bad)
            for bad in (text.replace("rootfs / rootfs", "/dev/vda / ext4"),
                        text.replace("T:COMPOSE_RC=0", "T:COMPOSE_RC=1"),
                        text + "Kernel panic\n", text.replace("T:COMPOSE_BEGIN", "echo T:COMPOSE_BEGIN"),
                        text.replace("T:CPU\n0", "T:CPU\n0-3")):
                with self.assertRaises(AssertionError):
                    guest_assertions(bad, "T", case)
            if case["devices"]:
                for bad in (text.replace("18: 7", "18: 4"),
                            text.replace("|unmounted", "|mounted"),
                            text.replace("a"*64, "wrong", 1),
                            text.replace("310a0000.virtio", "deadbeef.virtio").replace("32000000.pcie", "deadbeef.pcie"),
                            text.replace("T:MOUNTS_AFTER\n", "T:MOUNTS_AFTER\n/dev/vda /mnt/vda ext4 rw 0 0\n"),
                            text + "nvme nvme0: I/O timeout\n"):
                    with self.assertRaises(AssertionError):
                        guest_assertions(bad, "T", case)

    def test_fsck_warning_is_failure_even_with_zero_exit(self):
        from compose_checks import fsck_assertions
        good = "\n".join(f"Pass {n}: Checking" for n in range(1, 6)) + "\nopenEuler: 1/2 files, 3/4 blocks\n"
        fsck_assertions(good)
        for bad in (good + "Free blocks count wrong\n", good + "Warning: skipping journal recovery\n", ""):
            with self.assertRaises(AssertionError):
                fsck_assertions(bad)

    def test_private_copies_cleanup_and_fd_leak(self):
        from compose_runner import prepare_disks, cleanup_disks, disk_fds_closed
        from common import read_json
        case = next(c for c in self.cases if len(c["devices"]) == 2)
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            directory = work / "results" / case["id"]
            directory.mkdir(parents=True)
            source = work / "source.ext4"
            source.write_bytes(b"immutable source")
            source.chmod(0o444)
            disks = prepare_disks(work, case, {"rootfs": str(source)}, directory)
            self.assertEqual(len(disks), 2)
            with disks["vda"].open("r+b") as fd:
                fd.write(b"changed")
                with self.assertRaises(AssertionError):
                    disk_fds_closed(os.getpid(), disks)
            disk_fds_closed(os.getpid(), disks)
            cleanup_disks(work, case, directory)  # Also the parent kill path.
            self.assertEqual(source.read_bytes(), b"immutable source")
            self.assertFalse(source.stat().st_mode & 0o222)
            self.assertTrue(read_json(directory / "compose-disks.json")["cleaned"])
            self.assertTrue(all(not p.exists() for p in disks.values()))
            cleanup_disks(work, case, directory)

    def test_probe_shell_syntax_and_tier(self):
        from compose_runner import probe
        for case in self.cases:
            script = probe("TOKEN", case)
            subprocess.run(["bash", "-n"], input=script, text=True, check=True)
            self.assertLess(max(map(len, script.splitlines())), 4096)
        dual = "libqemu-compose-linux-dual"
        self.assertNotIn(dual, choose(["libqemu/compose.c"])["selected"])
        self.assertIn(dual, choose(["libqemu/compose.c"], extended=True)["selected"])


class LiveLogTests(unittest.TestCase):
    def test_late_files_split_utf8_partial_lines_and_all_uarts(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            output = io.StringIO()
            console = JobConsole(output, gitlab=False)
            data = "串口启动\r\nlast partial".encode()
            with CaseConsole(directory, "multi", console) as live:
                live.poll()  # Files do not exist yet.
                (directory / "uart0.log").write_bytes(data[:2])
                live.poll()
                self.assertNotIn("\ufffd", output.getvalue())
                with (directory / "uart0.log").open("ab") as stream:
                    stream.write(data[2:])
                for die in range(1, 4):
                    (directory / f"uart{die}.log").write_text(f"die{die} ready\n")
                live.poll()
                self.assertIn("[multi][uart0] 串口启动\n", output.getvalue())
                self.assertNotIn("last partial", output.getvalue())
                for die in range(1, 4):
                    self.assertIn(f"[multi][uart{die}] die{die} ready\n", output.getvalue())
            self.assertEqual(output.getvalue().count("last partial"), 1)
            self.assertEqual((directory / "uart0.log").read_bytes(), data)

    def test_streaming_is_visible_before_worker_exits(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            output = io.StringIO()
            ready = directory / "parent-saw-output"
            child = ("from pathlib import Path; import time; "
                     f"Path({str(directory / 'uart0.log')!r}).write_text('booting\\n'); "
                     f"p=Path({str(ready)!r}); "
                     "\nwhile not p.exists(): time.sleep(0.01)\n")
            with CaseConsole(directory, "live", JobConsole(output, gitlab=False)) as live:
                def tick():
                    live.poll()
                    if "[live][uart0] booting" in output.getvalue():
                        ready.touch()
                run_logged([sys.executable, "-c", child], directory / "worker.log",
                           timeout=3, on_poll=tick)
            self.assertTrue(ready.exists())

    def test_live_limit_shared_across_cases_preserves_tail_budget(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            output = io.StringIO()
            console = JobConsole(output, live_bytes=100, diagnostic_bytes=4096, gitlab=True)
            raw = (b"before flood\n" + b"X" * 5000 + b"\nLATEST FAILURE\n")
            (directory / "uart0.log").write_bytes(raw)
            (directory / "qemu.log").write_text("QEMU error detail\n")
            with CaseConsole(directory, "first", console) as live:
                live.poll()
            self.assertEqual(console.remaining["live"], 0)
            live.diagnostics("timeout")
            self.assertIn("LATEST FAILURE", output.getvalue())
            self.assertIn("QEMU error detail", output.getvalue())
            with CaseConsole(directory, "second", console) as second:
                second.poll()
            self.assertNotIn("[second][uart0]", output.getvalue())
            self.assertEqual(output.getvalue().count("live output limit reached"), 1)
            self.assertEqual((directory / "uart0.log").read_bytes(), raw)
            self.assertEqual(output.getvalue().count("section_start:"), 3)
            self.assertEqual(output.getvalue().count("section_end:"), 3)
            self.assertLess(len(output.getvalue().encode()), 8192)

    def test_control_sequences_are_safe_only_in_display(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            output = io.StringIO()
            raw = b"\x1b[31mPANIC\x1b[0m\r\n\x1b]0;unsafe-title\x07next\x00\n"
            (directory / "uart0.log").write_bytes(raw)
            with CaseConsole(directory, "safe", JobConsole(output, gitlab=False)) as live:
                live.poll()
            self.assertIn("[safe][uart0] PANIC\n", output.getvalue())
            self.assertIn("[safe][uart0] next\n", output.getvalue())
            self.assertNotIn("\x1b", output.getvalue())
            self.assertNotIn("\x00", output.getvalue())
            self.assertNotIn("unsafe-title", output.getvalue())
            self.assertEqual((directory / "uart0.log").read_bytes(), raw)

    def test_no_newline_output_stays_bounded(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            output = io.StringIO()
            raw = b"x" * (128 * 1024)
            (directory / "uart0.log").write_bytes(raw)
            with CaseConsole(directory, "flood", JobConsole(output, live_bytes=8192, gitlab=False)) as live:
                live.poll()
                self.assertLess(len(live.followers[0].pending), LINE_CHARS)
            self.assertLess(len(output.getvalue()), 10000)
            self.assertEqual((directory / "uart0.log").stat().st_size, len(raw))

    def test_failure_tails_bounded_and_symlinks_not_followed(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            output = io.StringIO()
            (directory / "uart0.log").write_text("".join(f"line-{i}\n" for i in range(1000)))
            secret = directory / "not-a-log"
            secret.write_text("do not display me")
            (directory / "uart1.log").symlink_to(secret)
            live = CaseConsole(directory, "tail", JobConsole(output, live_bytes=0, gitlab=False))
            live.diagnostics("failure")
            text = output.getvalue()
            self.assertEqual(text.count("[tail][uart0.log tail]"), TAIL_LINES)
            self.assertIn("line-999", text)
            self.assertNotIn("line-100\n", text)
            self.assertNotIn("do not display me", text)

    def test_job_budget_environment_validation(self):
        with patch.dict(os.environ, {"BOSC_CI_LIVE_LOG_KIB": "0"}):
            self.assertEqual(JobConsole.from_environment().remaining["live"], 0)
        for value in ("-1", "2049", "not-a-number"):
            with patch.dict(os.environ, {"BOSC_CI_LIVE_LOG_KIB": value}), self.assertRaises(ValueError):
                JobConsole.from_environment()

    def test_output_failure_does_not_replace_guest_result(self):
        class BrokenOutput:
            def write(self, _text):
                raise BrokenPipeError()
        console = JobConsole(BrokenOutput(), gitlab=False)
        console.line("test", "uart0", "hello")
        self.assertTrue(console.disabled)

    def test_parent_failure_and_cancel_print_diagnostics_after_cleanup(self):
        import argparse
        import ci
        for cancel in (False, True):
            with self.subTest(cancel=cancel), tempfile.TemporaryDirectory() as tmp:
                work = Path(tmp)
                write_json(work / "selected-tests.json", {"config_sha256": "test", "run_id": "test"})
                case = {"id": "fake", "timeout": 1}
                args = argparse.Namespace(assets_dir=None, asset=[], cache_dir=None, jf_paths=None)
                output = io.StringIO()
                def fake_worker(_argv, log, **kwargs):
                    (log.parent / "uart0.log").write_text("early UART\n")
                    kwargs["on_poll"]()
                    self.assertIn("early UART", output.getvalue())
                    (log.parent / "qemu.log").write_text("final QEMU error\n")
                    raise KeyboardInterrupt("cancelled") if cancel else RuntimeError("worker failed")
                with patch("ci.prepare", return_value={}), patch("ci.run_logged", side_effect=fake_worker), \
                        patch.dict(os.environ, {"GITLAB_CI": "true", "BOSC_CI_LIVE_LOG_KIB": "1"}), \
                        contextlib.redirect_stdout(output):
                    if cancel:
                        with self.assertRaises(KeyboardInterrupt):
                            ci.run_cases(args, work, [case])
                    else:
                        ci.run_cases(args, work, [case])
                self.assertIn("final QEMU error", output.getvalue())
                self.assertIn("FAIL fake", output.getvalue())
                self.assertEqual(output.getvalue().count("section_start:"), 2)
                self.assertEqual(output.getvalue().count("section_end:"), 2)


class ProcessAndReportTests(unittest.TestCase):
    def test_timeout_cleans_group(self):
        with tempfile.TemporaryDirectory() as tmp:
            pidfile = Path(tmp) / "pid"
            code = ("import os,time; "
                    f"open({str(pidfile)!r},'w').write(str(os.getpid())); time.sleep(30)")
            start = time.monotonic()
            with self.assertRaises(subprocess.TimeoutExpired):
                run_logged([sys.executable, "-c", code], Path(tmp) / "timeout.log", timeout=0.3,
                           on_poll=lambda: None)
            self.assertLess(time.monotonic() - start, 7)
            with self.assertRaises(ProcessLookupError):
                os.kill(int(pidfile.read_text()), 0)

    def test_cancellation_cleans_only_owned_process(self):
        with tempfile.TemporaryDirectory() as tmp:
            pidfile = Path(tmp) / "child.pid"
            child = "import os,time; " + f"open({str(pidfile)!r},'w').write(str(os.getpid())); time.sleep(60)"
            outer = ("import signal,sys; from common import run_logged; "
                     "signal.signal(signal.SIGTERM, lambda *a: sys.exit(130)); "
                     f"run_logged([sys.executable,'-c',{child!r}],{str(Path(tmp)/'child.log')!r},timeout=60,on_poll=lambda:None)")
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
            selected = choose(full_reason="test", extended=True)
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
