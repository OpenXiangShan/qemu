`ifdef SPI200_CONFIG_VH
`else
`define SPI200_CONFIG_VH

// --------------------------------------------------------------------------
// Define register port:
// 1. The register port can be one of AHB/APB
// 2. Default is APB
// --------------------------------------------------------------------------
//`define SPI200_REG_AHB

// --------------------------------------------------------------------------
// Define memory access function:
// 1. AHB/EILM
// 2. Default is NA
// --------------------------------------------------------------------------
//`define SPI200_AHB_MEM_SUPPORT
//`define SPI200_EILM_MEM_SUPPORT

// --------------------------------------------------------------------------
// Define Slave Mode support
// define SPI200_SLAVE_SUPPORT to support SPI Slave Mode
// --------------------------------------------------------------------------
//`define SPI200_SLAVE_SUPPORT

// --------------------------------------------------------------------------
// The macros below will effect the SPI interfaces of SPI200
// define SPI200_DUALSPI_SUPPORT to support dual SPI device.
// define SPI200_QUADSPI_SUPPORT to support quad and dual SPI device
// --------------------------------------------------------------------------
//`define SPI200_DUALSPI_SUPPORT
//`define SPI200_QUADSPI_SUPPORT

// --------------------------------------------------------------------------
// Define the depth of TX and RX fifo
// The values must be one of 2, 4, 8, 16, 32, 64, 128 words
// The TX and RX values can be different
// --------------------------------------------------------------------------
`define SPI200_TXFIFO_DEPTH_4W
//`define SPI200_TXFIFO_DEPTH_8W
`define SPI200_RXFIFO_DEPTH_4W
//`define SPI200_RXFIFO_DEPTH_8W

// --------------------------------------------------------------------------
// Define the bit number of haddr
// if SPI200_ADDR_WIDTH_24 is defined, the haddr will be 24bits.
// if SPI200_ADDR_WIDTH_24 is not defined, the haddr will be 32bits.
// --------------------------------------------------------------------------
//`define SPI200_ADDR_WIDTH_24

// --------------------------------------------------------------------------
// Define SPI200_DIRECT_IO_SUPPORT to support the SPI direct I/O control.
// This macro will cost some area
// --------------------------------------------------------------------------
//`define SPI200_DIRECT_IO_SUPPORT

// ------------------------------------------------------------------------------
// Define register default values for MemRdCmd, CS2CLK, CSHT, and SCLKDIV fields
// ------------------------------------------------------------------------------
`define SPI200_MEM_RDCMD_DEFAULT	4'd0
`define SPI200_CS2CLK_DEFAULT	3'h0
`define SPI200_CSHT_DEFAULT		3'h2
`define SPI200_SCLKDIV_DEFAULT	8'h1

`endif // SPI200_CONFIG_VH

