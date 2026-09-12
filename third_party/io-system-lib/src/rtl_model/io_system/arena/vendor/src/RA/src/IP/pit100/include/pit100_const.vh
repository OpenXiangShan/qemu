`ifdef PIT100_CONST_VH
`else
`define PIT100_CONST_VH

`ifdef PIT100_NUM_CHANNEL_2
	`define PIT100_NUM_CHANNEL 3'h2
    `define PIT100_CH1_SUPPORT
`else
`ifdef PIT100_NUM_CHANNEL_3
	`define PIT100_NUM_CHANNEL 3'h3
    `define PIT100_CH1_SUPPORT
    `define PIT100_CH2_SUPPORT
`else
`ifdef PIT100_NUM_CHANNEL_4
	`define PIT100_NUM_CHANNEL 3'h4
    `define PIT100_CH1_SUPPORT
    `define PIT100_CH2_SUPPORT
    `define PIT100_CH3_SUPPORT
`else
	`define PIT100_NUM_CHANNEL 3'h1
`endif
`endif
`endif

`define PIT100_PRODUCT_ID        32'h03031003

`endif
