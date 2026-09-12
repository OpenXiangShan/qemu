`ifdef UART100_CONFIG_VH
`else
`define UART100_CONFIG_VH

//------------------------------------------------------------------------------
// depth of FIFO, the number could be 16/32/64/128
// select to enable one of macro as below, if don't select anyone, the default
// is UART100_FIFO_DEPTH_16
//------------------------------------------------------------------------------
`define UART100_FIFO_DEPTH_16
//`define UART100_FIFO_DEPTH_32
//`define UART100_FIFO_DEPTH_64
//`define UART100_FIFO_DEPTH_128

//------------------------------------------------------------------------------
// undefine the macro to define the system clock, uclk, is synchronous with pclk to remove the 
// asynchronous interface circuits.
//------------------------------------------------------------------------------
`define UART100_UCLK_PCLK_SAME

`endif // UART100_CONFIG_VH
