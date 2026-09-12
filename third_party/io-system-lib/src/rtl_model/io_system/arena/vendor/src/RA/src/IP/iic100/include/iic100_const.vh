`ifdef IIC100_CONST_VH
`else
	`define IIC100_PRODUCT_ID 32'h02021012
	`define IIC100_DATA_WIDTH 8

	`ifdef IIC100_FIFO_DEPTH_4
		`define IIC100_INDEX_WIDTH	3
		`define IIC100_FIFO_CONFIG	2'h1
	`else
		`ifdef IIC100_FIFO_DEPTH_8
			`define IIC100_INDEX_WIDTH		4
			`define IIC100_FIFO_CONFIG		2'h2
		`else
			`ifdef IIC100_FIFO_DEPTH_16
				`define IIC100_INDEX_WIDTH		5
				`define IIC100_FIFO_CONFIG		2'h3
			`else
				`define IIC100_INDEX_WIDTH		2
				`define IIC100_FIFO_CONFIG		2'h0
			`endif
		`endif
	`endif
`endif
