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

LIB_CONFIGURE = [*(v for v in CONFIGURE if v != "--disable-my-virtio"),
                 "--enable-my-virtio", "--enable-libqemu", "--disable-plugins",
                 "--disable-modules", "--disable-opengl", "--disable-rust",
                 "--extra-cflags=-UNDEBUG"]


def bundle_dir(work, case):
    return work / ("bundle-libqemu" if case["kind"].startswith("libqemu") else "bundle")


def case_targets(case):
    if case["kind"].startswith("libqemu"):
        return ["libqemu-system-riscv64.so", "libqemu/" + case["binary"]]
    targets = ["qemu-system-riscv64"]
    if case["kind"] == "qtest":
        targets.append("tests/qtest/" + case["binary"])
    return targets


def doctor(work, cases):
    required = ["git", "python3"]
    if cases:
        required += ["cc", "c++", "make", "ninja", "pkg-config", "ldd"]
    if any(c["kind"] in ("linux", "mcu", "iommu-linux", "libqemu-linux", "libqemu-compose-linux") for c in cases):
        required += ["dtc", "fdtget", "fdtput"]
    if any(c["kind"] == "libqemu-compose-linux" and c["devices"] for c in cases):
        required += ["e2fsck", "cp"]
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


def build(work, cases, jobs=8, profile="all"):
    for name in ("system", "libqemu"):
        if profile not in ("all", name):
            continue
        selected = [c for c in cases if c["kind"].startswith("libqemu") == (name == "libqemu")]
        build_profile(work, selected, jobs, name)


def build_profile(work, cases, jobs, profile):
    library = profile == "libqemu"
    bundle = work / ("bundle-libqemu" if library else "bundle")
    if not cases:
        write_json(bundle / "build.json", {"status": "NOT_SELECTED", "profile": profile})
        return
    build_dir = work / ("libqemu-build" if library else "qemu-build")
    if build_dir.exists():
        raise ValueError("build directory already exists; use a fresh --work-dir")
    build_dir.mkdir()
    before = source_identity()
    env = os.environ.copy()
    env.update({"CCACHE_DIR": str(work / "ccache"),
                "PIP_CACHE_DIR": str(work / "pip-cache")})
    options = LIB_CONFIGURE if library else [*CONFIGURE, "--disable-libqemu"]
    prefix = "libqemu-" if library else ""
    run_logged([SOURCE / "configure", *options], work / (prefix + "configure.log"),
               cwd=build_dir, env=env, timeout=600)
    targets = list(dict.fromkeys(t for c in cases for t in case_targets(c)))
    run_logged(["ninja", "-C", build_dir, "-j", str(jobs), *targets],
               work / (prefix + "build.log"), env=env, timeout=3600)
    after = source_identity()
    if any(before[key] != after[key] for key in ("revision", "tracked_diff_sha256", "untracked_sources")):
        raise RuntimeError("source changed during build")
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
    if not library:
        run_logged([bundle / "qemu-system-riscv64", "--version"],
                   bundle / "qemu-version.log", timeout=30)
    write_json(bundle / "build.json", {"status": "PASS", "source": before,
               "profile": profile, "configure": options, "files": files,
               "host": platform.platform(), "jobs": jobs})


def verify_bundle(work, case):
    from common import read_json
    bundle = bundle_dir(work, case)
    manifest = read_json(bundle / "build.json")
    if manifest.get("status") != "PASS":
        raise ValueError("missing successful build")
    if case["kind"].startswith("libqemu") and manifest.get("profile") != "libqemu":
        raise ValueError("libqemu requires a library build")
    current = source_identity()
    for key in ("revision", "tracked_diff_sha256", "untracked_sources"):
        if manifest["source"][key] != current[key]:
            raise ValueError("build does not match current tracked source")
    for target in case_targets(case):
        path = bundle / target
        if sha256(path) != manifest["files"][target]:
            raise ValueError(f"build artifact hash mismatch: {target}")
        proc = subprocess.run(["ldd", path], capture_output=True, text=True, timeout=30)
        if proc.returncode or "not found" in proc.stdout:
            raise RuntimeError(f"missing shared libraries for {target}")
    for name, digest in manifest["files"].items():
        if name.startswith("share/") and sha256(bundle / name) != digest:
            raise ValueError(f"firmware bundle hash mismatch: {name}")
