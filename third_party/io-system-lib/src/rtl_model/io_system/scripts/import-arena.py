#!/usr/bin/env python3
"""Reproduce the pinned arena_soc dependency import; never read dirty source files."""
from __future__ import annotations

import argparse
import difflib
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import tarfile

COMMIT = "6c4d6dc1ab8197cac1a7ef2cb0e8a61fb9513402"
RA = "src/RA/src/"
CC = RA + "imports/vendor/pulp-platform/common_cells/"
CVCC = RA + "imports/core/cvfpu/src/common_cells/src/"
PACKAGES = [
    RA + "E6/define/E6CORE_pkg.sv", RA + "E6/define/E6FPU_pkg.sv",
    RA + "soc/include/config_pkg.sv", RA + "soc/include/cv64a6_imafdc_sv39_config_pkg.sv",
    RA + "soc/include/axi_pkg.sv", CC + "src/cf_math_pkg.sv",
    RA + "soc/include/ariane_axi_pkg.sv", RA + "soc/include/build_config_pkg.sv",
    RA + "soc/include/ariane_axi_512_pkg.sv", RA + "soc/include/ariane_axi_lite_512_pkg.sv",
    RA + "soc/include/axi_intf.sv", RA + "soc/include/riscv_pkg.sv",
    RA + "soc/include/ariane_pkg.sv",
]
MODULE_DIRS = [RA + x for x in [
    "E6/core", "coprocessor", "imports/core/ame",
    "IP/AXIAXI_64T512", "IP/AXIAXI_high_width", "IP/AXIAXI", "IP/bmc300",
    "IP/bmc300_llpp", "IP/uart100", "IP/axi2ahb", "IP/dmac300", "IP/apbbrg100",
    "IP/gpio100", "IP/sizedn300", "IP/busdec200", "IP/busdec200_llpp",
    "IP/rambrg300", "IP/sizeup300", "IP/exmon300", "IP/spi200",
]]
MODULES = [RA + "soc/" + x for x in [
    "arena_soc_mini.sv", "ame_aicb_wrapper.sv", "E6x.sv", "rstgen_bypass.sv",
    "rstgen.sv", "axi_bram_512MB.sv", "axi_bram_4MB_stub.sv", "axi_lite_to_axi.sv",
    "axi2mem.sv", "bootrom_64.sv",
]] + [RA + "clint/clint.sv", RA + "clint/axi_lite_interface.sv"]
MODULES += [RA + "IP/sysctrl/" + x for x in [
    "sysctrl.sv", "syscell.sv", "reset_syn.v", "clock_switch.v", "sysdivx.v",
    "signal_sync.v", "clock_switch_2.v", "filter_rst.v",
]]
MODULES += [CC + "src/" + x + ".sv" for x in [
    "counter", "stream_register", "spill_register_flushable", "spill_register",
    # ChiselCoreWrapper already embeds compatible lzc and rr_arb_tree modules.
    "delta_counter", "fifo_v3", "stream_arbiter",
    "stream_arbiter_flushable", "stream_mux", "stream_demux",
]] + [CC + "src/deprecated/fifo_v2.sv", CVCC + "id_queue.sv", CVCC + "onehot_to_bin.sv"]
MODULES += ["src/axi/src/" + x + ".sv" for x in [
    "axi_atop_filter", "axi_err_slv", "axi_demux", "axi_mux", "axi_id_prepend",
    "axi_id_remap", "axi_dw_downsizer", "axi_dw_upsizer", "axi_dw_converter",
]]


def patch_source(path: str, data: bytes) -> bytes:
    # Only these two handwritten integration files are changed. Generated E6,
    # RVV and AME sources are copied byte-for-byte from the pinned Git object.
    if path == RA + "soc/arena_soc_mini.sv":
        text = data.decode()
        text = text.replace("module arena_soc (", """module arena_soc #(
  // Preserve the original board behavior unless a co-simulation wrapper opts in.
  parameter bit COSIM = 1'b0
) (""", 1)
        text = text.replace("integer fd;", "generate if (!COSIM) begin : gen_mailbox_monitor\ninteger fd;", 1)
        marker = "// ---------------------\n// Board peripherals"
        text = text.replace(marker, "end endgenerate\n\n" + marker, 1)
        text = text.replace("sysctrl u_sysctrl(", "sysctrl #(.CORE0_RESET_RELEASE_DEFAULT(!COSIM)) u_sysctrl(", 1)
        text = text.replace("logic [15:0] w_gpio_i;", """logic [15:0] w_gpio_i;

// The mini board left these active peripheral inputs undriven. AXI-only
// co-simulation provides stable idle values without adding pin models.
generate if (COSIM) begin : gen_cosim_idle_inputs
  assign w_gpio_i = 16'b0;
  assign w_spi_clk_i = 1'b0;
  assign w_spi_miso_i = 1'b1;
  assign w_spi_mosi_i = 1'b1;
end endgenerate""", 1)
        text = text.replace(".spi_default_mode3  ()", ".spi_default_mode3  (1'b0)", 1)
        # The mini top had widened the CLINT parameter to 512 without widening
        # its 64-bit register implementation. Keep the assertion and provide a
        # real AXI converter, including byte-lane placement for mtime at +bff8.
        clint_start = text.index("clint #(\n")
        clint_end = text.index("\n);", clint_start) + len("\n);")
        original_clint = text[clint_start:clint_end]
        narrow_clint = original_clint.replace("( AxiDataWidth     )", "( 64               )")
        narrow_clint = narrow_clint.replace("( AxiIdWidthSlaves )", "( ariane_axi::IdWidth )")
        narrow_clint = narrow_clint.replace("ariane_axi::req_t", "clint64_req_t").replace("ariane_axi::resp_t", "clint64_resp_t")
        narrow_clint = narrow_clint.replace("( axi_clint_req  )", "( clint64_req    )").replace("( axi_clint_resp )", "( clint64_resp   )")
        clint_bridge = """`AXI_TYPEDEF_ALL(clint64, logic [63:0], ariane_axi::id_t,
                   logic [63:0], logic [7:0], ariane_axi::user_t)
generate if (COSIM) begin : gen_cosim_clint
  clint64_req_t clint64_req;
  clint64_resp_t clint64_resp;
  axi_dw_converter #(
    .AxiMaxReads(4), .AxiSlvPortDataWidth(512), .AxiMstPortDataWidth(64),
    .AxiAddrWidth(64), .AxiIdWidth(ariane_axi::IdWidth),
    .aw_chan_t(ariane_axi::aw_chan_t), .ar_chan_t(ariane_axi::ar_chan_t),
    .b_chan_t(ariane_axi::b_chan_t),
    .slv_w_chan_t(ariane_axi::w_chan_t), .mst_w_chan_t(clint64_w_chan_t),
    .slv_r_chan_t(ariane_axi::r_chan_t), .mst_r_chan_t(clint64_r_chan_t),
    .axi_slv_req_t(ariane_axi::req_t), .axi_slv_resp_t(ariane_axi::resp_t),
    .axi_mst_req_t(clint64_req_t), .axi_mst_resp_t(clint64_resp_t)
  ) u_clint_width (
    .clk_i(sys_clk), .rst_ni(ndmreset_n),
    .slv_req_i(axi_clint_req), .slv_resp_o(axi_clint_resp),
    .mst_req_o(clint64_req), .mst_resp_i(clint64_resp)
  );
""" + narrow_clint + "\nend else begin : gen_board_clint\n" + original_clint + "\nend endgenerate"
        text = text[:clint_start] + clint_bridge + text[clint_end:]
        text = '`include "axi/typedef.svh"\n' + text
        return text.encode()
    if path == RA + "IP/sysctrl/sysctrl.sv":
        text = data.decode()
        text = text.replace("module sysctrl (", """module sysctrl #(
          parameter bit CORE0_RESET_RELEASE_DEFAULT = 1'b1
) (""", 1)
        old = "r_core0_soft_reset   <= 1'b1;"
        if text.count(old) != 1:
            raise ValueError("unexpected SYSCTRL reset implementation")
        return text.replace(old, "r_core0_soft_reset   <= CORE0_RESET_RELEASE_DEFAULT;").encode()
    return data


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="nanhu-fpga-aps Git repository")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "arena")
    args = parser.parse_args()
    paths = subprocess.check_output(["git", "-C", str(args.source), "ls-tree", "-r", "--name-only", COMMIT], text=True).splitlines()
    modules = list(MODULES)
    modules.extend(p for p in paths if any(p.startswith(d + "/") for d in MODULE_DIRS) and p.endswith((".v", ".sv")))
    modules = sorted(set(modules))
    headers = sorted(p for p in paths if p.endswith((".vh", ".svh")) and (
        p.startswith(RA + "E6/include/") or p.startswith(RA + "soc/include/") or
        p.startswith(RA + "IP/") or p.startswith(CC + "include/") or p.startswith("src/axi/include/")))
    files = sorted(set(PACKAGES + modules + headers + ["src/axi/LICENSE"]))
    missing = set(files) - set(paths)
    if missing:
        raise ValueError(f"missing pinned files: {sorted(missing)}")
    archive = subprocess.check_output(["git", "-C", str(args.source), "archive", COMMIT, "--", *files])
    originals = {}
    with tarfile.open(fileobj=io.BytesIO(archive)) as stream:
        for member in stream:
            if member.isfile():
                originals[member.name] = stream.extractfile(member).read()
    dest = args.output.resolve()
    vendor = dest / "vendor"
    # Remove only stale files created by an earlier run of this importer.
    manifest = dest / "SOURCE_MANIFEST.json"
    if manifest.exists():
        previous = json.loads(manifest.read_text())
        for entry in previous["files"]:
            if entry["source_path"] not in files:
                (vendor / entry["source_path"]).unlink(missing_ok=True)
    records, patches, symbols = [], [], {}
    for path in files:
        original = originals[path]
        data = patch_source(path, original)
        target = vendor / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        records.append({"source_path": path, "source_sha256": hashlib.sha256(original).hexdigest(),
                        "imported_sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data),
                        "modified": data != original})
        if data != original:
            patches.extend(difflib.unified_diff(original.decode().splitlines(True), data.decode().splitlines(True),
                                               fromfile="a/" + path, tofile="b/" + path))
        if path in modules or path in PACKAGES:
            clean = re.sub(r"/\*.*?\*/|//[^\n]*", "", data.decode(errors="replace"), flags=re.S)
            for name in re.findall(r"\b(?:module|interface|package)\s+(\w+)", clean):
                if name in symbols:
                    raise ValueError(f"duplicate declaration {name}: {symbols[name]}, {path}")
                symbols[name] = path
    manifest.write_text(json.dumps({"repository": "nanhu-fpga-aps", "commit": COMMIT,
                                   "top": "arena_soc", "files": records}, indent=2) + "\n")
    (dest / "cosim.patch").write_text("".join(patches))
    include_dirs = {str(Path(x).parent) for x in headers}
    include_dirs.update([CC + "include", "src/axi/include"])
    (dest / "sources.json").write_text(json.dumps({"include_dirs": sorted(include_dirs),
                                                 "packages": PACKAGES, "modules": modules}, indent=2) + "\n")
    print(f"Imported {len(records)} files, {sum(r['bytes'] for r in records):,} bytes; {len(symbols)} unique declarations")
    print(f"Provenance: {manifest}")


if __name__ == "__main__":
    main()
