#!/usr/bin/env python3
"""Package matching NVMe modules for the fixed mapper CI kernel (manual).

CI consumes the locked artifacts; it never builds them. GPL-2.0-or-later.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

from common import sha256, write_json


def package(build, output):
    release = (build / "include/config/kernel.release").read_text().strip()
    config = (build / ".config").read_text()
    for option in ("CONFIG_BLK_DEV_NVME=m", "CONFIG_NVME_CORE=m", "CONFIG_DEBUG_FS=y",
                   "CONFIG_RISCV_IOMMU=y"):
        if option + "\n" not in config:
            raise ValueError(f"required kernel option missing: {option}")
    modules = []
    with tempfile.TemporaryDirectory(prefix="mapper-modules-") as temp:
        root = Path(temp)
        for name, dependencies in (("nvme-core", ""), ("nvme", "nvme-core")):
            source = build / f"drivers/nvme/host/{name}.ko"
            actual = subprocess.check_output(["modinfo", "-F", "depends", source], text=True).strip()
            vermagic = subprocess.check_output(["modinfo", "-F", "vermagic", source], text=True).split()[0]
            if actual != dependencies or vermagic != release:
                raise ValueError(f"unexpected dependencies/vermagic in {name}")
            dest = root / source.name
            shutil.copyfile(source, dest)
            subprocess.run(["riscv64-linux-gnu-strip", "--strip-debug", dest], check=True)
            modules.append({"name": source.name, "sha256": sha256(dest),
                            "size": dest.stat().st_size, "depends": actual, "release": release})
        (root / "mapper-module-order").write_text("nvme-core.ko\nnvme.ko\n")
        write_json(root / "modules.json", modules)
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("xb") as stream:
            stream.truncate(16 * 1024 * 1024)
        subprocess.run(["mke2fs", "-q", "-t", "ext4", "-F", "-L", "mapper-modules",
                        "-d", root, output], check=True)
    write_json(output.with_suffix(".json"), {"kernel_release": release,
               "config_sha256": sha256(build / ".config"), "modules": modules,
               "sha256": sha256(output), "size": output.stat().st_size})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    package(args.build.resolve(), args.output.resolve())
