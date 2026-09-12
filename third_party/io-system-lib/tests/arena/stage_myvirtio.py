#!/usr/bin/env python3
"""Copy MyVirtio archives while isolating their embedded IOMMU model symbols.

Only staged copies are rewritten. The io-system and MyVirtio reference models
may be different versions and must not accidentally share functions/globals.
"""
import argparse
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile


def stage(source, destination, nm="nm", objcopy="objcopy"):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if source == destination:
        raise ValueError("staging destination must differ from source")
    archives = [source / "libMyVirtio.a", source / "libMyVirtio_backend.a"]
    symbols = set()
    for archive in archives:
        listing = subprocess.check_output(shlex.split(nm) +
            ["-A", "-g", "--defined-only", str(archive)], text=True)
        for line in listing.splitlines():
            fields = line.rsplit(":", 2)
            if len(fields) != 3:
                continue
            member, definition = fields[1], fields[2].split()
            if member.startswith("iommu_") and member.endswith(".o") and len(definition) == 3:
                symbols.add(definition[2])
    destination.mkdir(parents=True, exist_ok=True)
    mapping = destination / "myvirtio-iommu-symbols.txt"
    mapping.write_text("".join(f"{name} io_system_myvirtio_private_{name}\n"
                               for name in sorted(symbols)))
    # objcopy rewrites definitions AND undefined references in every member.
    for archive in archives:
        with tempfile.NamedTemporaryFile(dir=destination, suffix=".a", delete=False) as stream:
            temporary = Path(stream.name)
        try:
            if symbols:
                subprocess.run(shlex.split(objcopy) + ["--redefine-syms=" + str(mapping),
                    str(archive), str(temporary)], check=True)
            else:
                shutil.copyfile(archive, temporary)
            temporary.replace(destination / archive.name)
        finally:
            temporary.unlink(missing_ok=True)
    for name in ["virtio_backend.h", "virtio_wrapper.h"]:
        shutil.copyfile(source / name, destination / name)
    print(f"staged MyVirtio with {len(symbols)} private IOMMU symbols")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--nm", default="nm")
    parser.add_argument("--objcopy", default="objcopy")
    args = parser.parse_args()
    stage(args.source, args.destination, args.nm, args.objcopy)
