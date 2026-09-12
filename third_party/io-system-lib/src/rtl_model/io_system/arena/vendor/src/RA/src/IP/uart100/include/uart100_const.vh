`ifdef UART100_CONST_VH
`else
`define UART100_CONST_VH

`define UART100_PRODUCT_ID		32'h02011009

`ifdef UART100_FIFO_DEPTH_128
	`define UART100_FIFO_DEPTH_BIT	7
	`define UART100_HWCFG		4'h3
`else
	`ifdef UART100_FIFO_DEPTH_64
		`define UART100_FIFO_DEPTH_BIT	6
		`define UART100_HWCFG		4'h2
	`else
		`ifdef UART100_FIFO_DEPTH_32
			`define UART100_FIFO_DEPTH_BIT	5
			`define UART100_HWCFG		4'h1
		`else
			`define UART100_FIFO_DEPTH_BIT	4
			`define UART100_HWCFG		4'h0
		`endif
	`endif
`endif

`endif
