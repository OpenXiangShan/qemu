"""Host preflight and one relocatable TCG build. GPL-2.0-or-later."""
import os
import platform
import shutil
import socket
import subprocess
import sys
import tempfile
import importlib.metadata

from common import SOURCE, run_logged, sha256, source_identity, write_json

CONFIGURE = ["--target-list=riscv64-softmmu", "--enable-tcg", "--disable-kvm",
             "--disable-werror", "--disable-docs", "--disable-gtk",
             "--disable-sdl", "--disable-vnc", "--disable-guest-agent",
             "--disable-tools", "--disable-download", "--disable-my-virtio",
             "--disable-io-system"]


def doctor(work, cases):
    required = ["git", "python3"]
    if cases:
        required += ["cc", "c++", "make", "ninja", "pkg-config", "ldd"]
    if any(c["kind"] in ("linux", "mcu") for c in cases):
        required += ["dtc", "fdtget", "fdtput"]
    tools = {tool: shutil.which(tool) for tool in required}
    errors = [f"missing tool: {key}" for key, value in tools.items() if not value]
    if sys.version_info < (3, 10):
        errors.append("Python >= 3.10 is required")
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        errors.append("first-phase host profile requires Linux x86_64")
    versions = {"python": sys.version, "platform": platform.platform()}
    try:
        versions["PyYAML"] = importlib.metadata.version("PyYAML")
    except importlib.metadata.PackageNotFoundError:
        errors.append("missing PyYAML (install tests/ci/bosc/requirements.txt in a local venv)")
    if cases and tools.get("pkg-config"):
        for package in ("glib-2.0", "pixman-1", "zlib"):
            proc = subprocess.run(["pkg-config", "--modversion", package],
                                  capture_output=True, text=True, timeout=10)
            if proc.returncode:
                errors.append(f"missing development package: {package}")
            else:
                versions[package] = proc.stdout.strip()
    if cases:
        try:
            with tempfile.TemporaryDirectory(prefix="bosc-ci-") as tmp:
                with socket.socket(socket.AF_UNIX) as sock:
                    sock.bind(tmp + "/probe.sock")
        except OSError as exc:
            errors.append(f"Unix socket unavailable: {exc}")
    for tool in ("cc", "ninja", "dtc"):
        if shutil.which(tool):
            result = subprocess.run([tool, "--version"], capture_output=True,
                                    text=True, timeout=10)
            versions[tool] = (result.stdout or result.stderr).splitlines()[:1]
    report = {"tools": tools, "versions": versions, "errors": errors,
              "free_bytes": shutil.disk_usage(work).free,
              "docker_required": False, "cross_compiler_required": False}
    write_json(work / "doctor.json", report)
    if errors:
        raise RuntimeError("preflight failed: " + "; ".join(errors))
    return report


def build(work, cases, jobs=8):
    if not cases:
        write_json(work / "bundle" / "build.json", {"status": "NOT_SELECTED"})
        return
    build_dir = work / "qemu-build"
    if build_dir.exists():
        raise ValueError("build directory already exists; use a fresh --work-dir")
    build_dir.mkdir()
    before = source_identity()
    env = os.environ.copy()
    env.update({"CCACHE_DIR": str(work / "ccache"),
                "PIP_CACHE_DIR": str(work / "pip-cache")})
    run_logged([SOURCE / "configure", *CONFIGURE], work / "configure.log",
               cwd=build_dir, env=env, timeout=600)
    targets = ["qemu-system-riscv64"]
    targets += ["tests/qtest/" + c["binary"] for c in cases if c["kind"] == "qtest"]
    run_logged(["ninja", "-C", build_dir, "-j", str(jobs), *targets],
               work / "build.log", env=env, timeout=3600)
    after = source_identity()
    if before["revision"] != after["revision"] or before["tracked_diff_sha256"] != after["tracked_diff_sha256"]:
        raise RuntimeError("tracked source changed during build")
    bundle = work / "bundle"
    bundle.mkdir(exist_ok=True)
    files = {}
    for target in targets:
        dest = bundle / target
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(build_dir / target, dest)
        files[target] = sha256(dest)
        run_logged(["ldd", dest], dest.with_suffix(".ldd.log"), timeout=30)
    # Explicit -L is used by guest commands. qtest does not load default
    # firmware under -accel qtest; its TCG subcase supplies its own raw BIOS.
    firmware = bundle / "share" / "qemu"
    firmware.mkdir(parents=True)
    for name in ("opensbi-riscv64-generic-fw_dynamic.bin", "opensbi-riscv64-xiangshan-kmh-fw_dynamic.bin"):
        shutil.copy2(SOURCE / "pc-bios" / name, firmware / name)
        files[str((firmware / name).relative_to(bundle))] = sha256(firmware / name)
    run_logged([bundle / "qemu-system-riscv64", "--version"],
               bundle / "qemu-version.log", timeout=30)
    write_json(bundle / "build.json", {"status": "PASS", "source": before,
               "configure": CONFIGURE, "files": files,
               "host": platform.platform(), "jobs": jobs})


def verify_bundle(work, case):
    from common import read_json
    manifest = read_json(work / "bundle" / "build.json")
    current = source_identity()
    for key in ("revision", "tracked_diff_sha256"):
        if manifest["source"][key] != current[key]:
            raise ValueError("build does not match current tracked source")
    targets = ["qemu-system-riscv64"]
    if case["kind"] == "qtest":
        targets.append("tests/qtest/" + case["binary"])
    for target in targets:
        path = work / "bundle" / target
        if sha256(path) != manifest["files"][target]:
            raise ValueError(f"build artifact hash mismatch: {target}")
        proc = subprocess.run(["ldd", path], capture_output=True, text=True, timeout=30)
        if proc.returncode or "not found" in proc.stdout:
            raise RuntimeError(f"missing shared libraries for {target}")
    for name, digest in manifest["files"].items():
        if name.startswith("share/") and sha256(work / "bundle" / name) != digest:
            raise ValueError(f"firmware bundle hash mismatch: {name}")
