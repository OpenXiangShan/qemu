`ifdef DMAC300_CONST_VH
`else
`define DMAC300_CONST_VH

`define DMAC300_ID        32'h01023002


`define DMAC300_TTS_WIDTH 32

`ifdef DMAC300_CHAIN_TRANSFER_SUPPORT
	`define DMAC300_CHAIN_TRANSFER_EXIST		1'b1
`else
	`define DMAC300_CHAIN_TRANSFER_EXIST		1'b0
`endif
`ifdef DMAC300_REQ_SYNC_SUPPORT
	`define DMAC300_REQ_SYNC_EXIST		1'b1
`else
	`define DMAC300_REQ_SYNC_EXIST		1'b0
`endif
`ifdef DMAC300_DUAL_DMA_CORE_SUPPORT
	`define DMAC300_DUAL_DMA_CORE_EXIST		1'b1
`else
	`define DMAC300_DUAL_DMA_CORE_EXIST		1'b0
`endif
`ifdef DMAC300_DUAL_MASTER_IF_SUPPORT
	`define DMAC300_DUAL_MASTER_IF_EXIST		1'b1
`else
	`define DMAC300_DUAL_MASTER_IF_EXIST		1'b0
`endif
`ifdef DMAC300_FIFO_DEPTH_4
	`define DMAC300_FIFO_DEPTH 		6'd4
	`define DMAC300_FIFO_POINTER_WIDTH	3
`else
`ifdef DMAC300_FIFO_DEPTH_8
	`define DMAC300_FIFO_DEPTH 		6'd8
	`define DMAC300_FIFO_POINTER_WIDTH	4
`else
`ifdef DMAC300_FIFO_DEPTH_16
	`define DMAC300_FIFO_DEPTH 		6'd16
	`define DMAC300_FIFO_POINTER_WIDTH	5
`else
`ifdef DMAC300_FIFO_DEPTH_32
	`define DMAC300_FIFO_DEPTH 		6'd32
	`define DMAC300_FIFO_POINTER_WIDTH	6
`endif
`endif
`endif
`endif

`ifdef DMAC300_CH_NUM_1
	`define DMAC300_CH_NUM       4'h1
	`define DMAC_CONFIG_CH0
`else
`ifdef DMAC300_CH_NUM_2
	`define DMAC300_CH_NUM       4'h2
	`define DMAC_CONFIG_CH0
	`define DMAC_CONFIG_CH1
`else
`ifdef DMAC300_CH_NUM_3
	`define DMAC300_CH_NUM       4'h3
	`define DMAC_CONFIG_CH0
	`define DMAC_CONFIG_CH1
	`define DMAC_CONFIG_CH2
`else
`ifdef DMAC300_CH_NUM_4
	`define DMAC300_CH_NUM       4'h4
	`define DMAC_CONFIG_CH0
	`define DMAC_CONFIG_CH1
	`define DMAC_CONFIG_CH2
	`define DMAC_CONFIG_CH3
`else
`ifdef DMAC300_CH_NUM_5
	`define DMAC300_CH_NUM       4'h5
	`define DMAC_CONFIG_CH0
	`define DMAC_CONFIG_CH1
	`define DMAC_CONFIG_CH2
	`define DMAC_CONFIG_CH3
	`define DMAC_CONFIG_CH4
`else
`ifdef DMAC300_CH_NUM_6
	`define DMAC300_CH_NUM       4'h6
	`define DMAC_CONFIG_CH0
	`define DMAC_CONFIG_CH1
	`define DMAC_CONFIG_CH2
	`define DMAC_CONFIG_CH3
	`define DMAC_CONFIG_CH4
	`define DMAC_CONFIG_CH5
`else
`ifdef DMAC300_CH_NUM_7
	`define DMAC300_CH_NUM       4'h7
	`define DMAC_CONFIG_CH0
	`define DMAC_CONFIG_CH1
	`define DMAC_CONFIG_CH2
	`define DMAC_CONFIG_CH3
	`define DMAC_CONFIG_CH4
	`define DMAC_CONFIG_CH5
	`define DMAC_CONFIG_CH6
`else
`ifdef DMAC300_CH_NUM_8
	`define DMAC300_CH_NUM       4'h8
	`define DMAC_CONFIG_CH0
	`define DMAC_CONFIG_CH1
	`define DMAC_CONFIG_CH2
	`define DMAC_CONFIG_CH3
	`define DMAC_CONFIG_CH4
	`define DMAC_CONFIG_CH5
	`define DMAC_CONFIG_CH6
	`define DMAC_CONFIG_CH7
`endif
`endif
`endif
`endif
`endif
`endif
`endif
`endif

`ifdef DMAC300_DATA_WIDTH_512
	`define DMAC300_BYTE_OFFSET_WIDTH 6
	`define DMAC300_DATA_WIDTH_GE_256
	`define DMAC300_DATA_WIDTH_GE_128
	`define DMAC300_DATA_WIDTH_GE_64
	`define DMA_DATA_WIDTH 512
	`define DMA_DATA_WIDTH_REG 2'd3
	`define DMA_WSTRB_WIDTH 64
    `ifdef DMAC300_FIFO_DEPTH_4
	`define DMAC300_FIFO_BYTE 11'd128
    `endif
    `ifdef DMAC300_FIFO_DEPTH_8
	`define DMAC300_FIFO_BYTE 11'd256
    `endif
    `ifdef DMAC300_FIFO_DEPTH_16
	`define DMAC300_FIFO_BYTE 11'd512
    `endif
    `ifdef DMAC300_FIFO_DEPTH_32
	`define DMAC300_FIFO_BYTE 11'd1024
    `endif
`else
`ifdef DMAC300_DATA_WIDTH_256
	`define DMAC300_BYTE_OFFSET_WIDTH 5
	`define DMAC300_DATA_WIDTH_GE_256
	`define DMAC300_DATA_WIDTH_GE_128
	`define DMAC300_DATA_WIDTH_GE_64
	`define DMA_DATA_WIDTH 256
	`define DMA_DATA_WIDTH_REG 2'd3
	`define DMA_WSTRB_WIDTH 32
    `ifdef DMAC300_FIFO_DEPTH_4
	`define DMAC300_FIFO_BYTE 11'd128
    `endif
    `ifdef DMAC300_FIFO_DEPTH_8
	`define DMAC300_FIFO_BYTE 11'd256
    `endif
    `ifdef DMAC300_FIFO_DEPTH_16
	`define DMAC300_FIFO_BYTE 11'd512
    `endif
    `ifdef DMAC300_FIFO_DEPTH_32
	`define DMAC300_FIFO_BYTE 11'd1024
    `endif
`else
`ifdef DMAC300_DATA_WIDTH_128
	`define DMAC300_BYTE_OFFSET_WIDTH 4
	`define DMAC300_DATA_WIDTH_GE_128
	`define DMAC300_DATA_WIDTH_GE_64
	`define DMA_DATA_WIDTH 128
	`define DMA_DATA_WIDTH_REG 2'd2
	`define DMA_WSTRB_WIDTH 16
    `ifdef DMAC300_FIFO_DEPTH_4
	`define DMAC300_FIFO_BYTE 11'd64
    `endif
    `ifdef DMAC300_FIFO_DEPTH_8
	`define DMAC300_FIFO_BYTE 11'd128
    `endif
    `ifdef DMAC300_FIFO_DEPTH_16
	`define DMAC300_FIFO_BYTE 11'd256
    `endif
    `ifdef DMAC300_FIFO_DEPTH_32
	`define DMAC300_FIFO_BYTE 11'd512
    `endif
`else
`ifdef DMAC300_DATA_WIDTH_64
	`define DMAC300_BYTE_OFFSET_WIDTH 3
	`define DMAC300_DATA_WIDTH_GE_64
	`define DMA_DATA_WIDTH 64
	`define DMA_DATA_WIDTH_REG 2'd1
	`define DMA_WSTRB_WIDTH 8
    `ifdef DMAC300_FIFO_DEPTH_4
	`define DMAC300_FIFO_BYTE 11'd32
    `endif
    `ifdef DMAC300_FIFO_DEPTH_8
	`define DMAC300_FIFO_BYTE 11'd64
    `endif
    `ifdef DMAC300_FIFO_DEPTH_16
	`define DMAC300_FIFO_BYTE 11'd128
    `endif
    `ifdef DMAC300_FIFO_DEPTH_32
	`define DMAC300_FIFO_BYTE 11'd256
    `endif
`else
`ifdef DMAC300_DATA_WIDTH_32
	`define DMAC300_BYTE_OFFSET_WIDTH 2
	`define DMA_DATA_WIDTH 32
	`define DMA_DATA_WIDTH_REG 2'd0
	`define DMA_WSTRB_WIDTH 4
    `ifdef DMAC300_FIFO_DEPTH_4
	`define DMAC300_FIFO_BYTE 11'd16
    `endif
    `ifdef DMAC300_FIFO_DEPTH_8
	`define DMAC300_FIFO_BYTE 11'd32
    `endif
    `ifdef DMAC300_FIFO_DEPTH_16
	`define DMAC300_FIFO_BYTE 11'd64
    `endif
    `ifdef DMAC300_FIFO_DEPTH_32
	`define DMAC300_FIFO_BYTE 11'd128
    `endif
`else
	`define DMAC300_DATA_WIDTH_32
	`define DMAC300_BYTE_OFFSET_WIDTH 2
	`define DMA_DATA_WIDTH 32
	`define DMA_DATA_WIDTH_REG 2'd0
	`define DMA_WSTRB_WIDTH 4
    `ifdef DMAC300_FIFO_DEPTH_4
	`define DMAC300_FIFO_BYTE 11'd16
    `endif
    `ifdef DMAC300_FIFO_DEPTH_8
	`define DMAC300_FIFO_BYTE 11'd32
    `endif
    `ifdef DMAC300_FIFO_DEPTH_16
	`define DMAC300_FIFO_BYTE 11'd64
    `endif
    `ifdef DMAC300_FIFO_DEPTH_32
	`define DMAC300_FIFO_BYTE 11'd128
    `endif
`endif
`endif
`endif
`endif
`endif

`endif

