# Arena AXI-only co-simulation source import

This directory vendors the `arena_soc` boundary from nanhu-fpga-aps commit
`6c4d6dc1ab8197cac1a7ef2cb0e8a61fb9513402`. It includes the real generated E6,
RVV and AME RTL, BMC300, the internal memory models and active mini-SoC
peripherals. No LinkNan, FPGA DDR controller or board testbench is imported.
The `vendor/` paths retain their upstream repository-relative names.

`SOURCE_MANIFEST.json` records upstream and imported SHA256 for every file.
`cosim.patch` is the complete local adaptation of the handwritten mini top and
SYSCTRL. The generated cores are byte-identical to the pinned Git objects.
Original copyright and license notices are retained; the AXI project license
is at `vendor/src/axi/LICENSE`.

## Reproduction and filelist

From io-system-lib:

```sh
python3 src/rtl_model/io_system/scripts/import-arena.py \
  --source /path/to/nanhu-fpga-aps
python3 src/rtl_model/io_system/scripts/arena-filelist.py --verify > arena.f
```

The importer reads Git objects at the pinned commit, never local modified
source files. Ordinary builds need only this directory; no source repository
or network access is needed. The generated filelist contains absolute paths
resolved at invocation time, so it can be consumed from picker build folders.
Use `--packages-only` and `--modules-only` to place io-system packages between
the RA packages and modules. Compile SystemVerilog with `-timescale=1ns/1ps`.

There is one shared `axi_pkg`, sourced from the mini-SoC include directory.
RA's `ariane_axi` package has 512-bit data, so io-system's 64-bit APLIC package
must use a separate name when compiling both. `axi_lite_to_axi` uses the RA
modified implementation, not the different upstream PULP module of that name.
Generated `ChiselCoreWrapper.sv` embeds `lzc` and `rr_arb_tree`; their standalone
common_cells copies are deliberately excluded. The actual width-converter
dependency `id_queue.sv` comes from `imports/core/cvfpu`, as does
`onehot_to_bin.sv`. The importer rejects duplicate declarations in its filelist.

## Co-simulation configuration

Instantiate `arena_soc #(.COSIM(1'b1))`. This makes the following changes:

- E6's SYSCTRL reset-release register starts at zero. The controlling QEMU
  CPU loads RAM and programs the existing SYSCTRL boot address before release.
- The original mailbox console monitor is not elaborated. It cannot create
  files or call `$finish` in the hosting QEMU process.
- Internally undriven GPIO and SPI inputs have deterministic idle values.
- The internal CLINT path passes through a 512-to-64 AXI converter. The mini
  top originally passed 512-bit data to a 64-bit-only CLINT register bus,
  which triggered its width assertion at simulation time zero. The assertion
  remains enabled and generated core RTL is unchanged.

`COSIM=0` retains the original reset-release and mailbox behavior. The SPI
default-mode input, previously unconnected, is explicitly tied low in either
configuration. The enclosing io-system wrapper must still supply the clock,
active-high `cpu_reset`, idle UART/JTAG inputs and peripheral pin tie-offs.

The DDR AXI master is 64-bit address / 512-bit data / 8-bit ID. The config AXI
slave is 64-bit address / 512-bit data / 4-bit ID. These are the original full
AXI interfaces, not picker-exported independent peripheral models. The parent
io-system integration owns the external width conversion, mux and runtime.
