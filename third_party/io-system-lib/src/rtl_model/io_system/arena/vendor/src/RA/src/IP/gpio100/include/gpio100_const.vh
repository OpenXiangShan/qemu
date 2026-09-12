`ifdef GPIO100_CONST_VH
`else
`define GPIO100_CONST_VH

`define GPIO100_PRODUCT_ID        32'h02031002

`ifdef GPIO100_PULL_SUPPORT
	`define _GPIO100_PULL_EXIST		1'b1
`else
	`define _GPIO100_PULL_EXIST		1'b0
`endif
`ifdef GPIO100_INTR_SUPPORT
	`define _GPIO100_INTR_EXIST		1'b1
`else
	`define _GPIO100_INTR_EXIST		1'b0
`endif
`ifdef GPIO100_DEBOUNCE_SUPPORT
	`define _GPIO100_DEBOUNCE_EXIST	1'b1
`else
	`define _GPIO100_DEBOUNCE_EXIST	1'b0
`endif

`endif

