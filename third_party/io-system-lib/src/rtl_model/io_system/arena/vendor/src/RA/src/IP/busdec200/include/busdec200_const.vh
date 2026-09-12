`ifdef BUSDEC200_CONST_VH
`else
`define BUSDEC200_CONST_VH

`define BUSDEC200_PRODUCT_ID			32'h00032003

`ifdef BUSDEC200_ADDR_WIDTH_24
`define	BUSDEC200_ADDR_WIDTH			24
`else
`define	BUSDEC200_ADDR_WIDTH			32
`endif

`ifdef BUSDEC200_DATA_WIDTH_64
`define BUSDEC200_DATA_WIDTH                 64
`else
`ifdef BUSDEC200_DATA_WIDTH_128
`define BUSDEC200_DATA_WIDTH                 128
`else
`ifdef BUSDEC200_DATA_WIDTH_256
`define BUSDEC200_DATA_WIDTH                 256
`else
`define BUSDEC200_DATA_WIDTH                 32
`endif
`endif
`endif

`ifdef BUSDEC200_ADDR_WIDTH_24
`define	BUSDEC200_SLV0_SIZE			3
`else
`define	BUSDEC200_SLV0_SIZE			1
`endif

`endif
