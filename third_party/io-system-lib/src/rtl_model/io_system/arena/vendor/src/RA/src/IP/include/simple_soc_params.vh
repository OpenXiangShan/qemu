`define BUILD_AHBL

`ifdef AHBL_MEMORY
`define BUILD_AHBL
`undef BUILD_AXIL
`endif

`ifdef AXIL_MEMORY
`define BUILD_AXIL
`undef BUILD_AHBL
`endif

//`define AHBL_SYSTEM
`ifdef AHBL_MEMORY
    `define START_ADDR_PT  32'h20000000
`elsif AHBL_DIRMEMORY_TB
    `define START_ADDR_PT  32'h30000000
`elsif AHBL_DBUS
    `define START_ADDR_PT  32'h40000000
`elsif AHBL_SYSTEM
    `define START_ADDR_PT  32'h70000000
`elsif AXIL_MEMORY
    `define START_ADDR_PT  32'h20000000
`else 
    `define START_ADDR_PT  32'h30000000
`endif

`define IBUS_START_PT   4'h1
`define IBUS_END_PT     4'h3
`define DBUS_START_PT   4'h4
`define DBUS_END_PT     4'h6
`define SYSTEM_START_PT 4'h7
`define SYSTEM_END_PT   4'hf


//thy add
//`define PLicIrqNumber_PT   7
//`define START_ADDR_PT   32'h0

`define ITCM_SIZE_PT   16*1024
`define DTCM_SIZE_PT   16*1024

//`define ITCM_SIZE_PT   24*1024
//`define DTCM_SIZE_PT   8*1024

`define IBUS_DW   64

`ifdef FPGA
    `define CHISEL
`endif


