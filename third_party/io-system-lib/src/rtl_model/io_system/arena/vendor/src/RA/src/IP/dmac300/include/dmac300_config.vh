`ifdef DMAC300_CONFIG_VH
`else
`define DMAC300_CONFIG_VH

//-------------------------------------------------
// DMAC Channel Number
//-------------------------------------------------
// Available value: 1~8
`define DMAC300_CH_NUM_4

//-------------------------------------------------
// DMAC FIFO DEPTH
//-------------------------------------------------
//`define DMAC300_FIFO_DEPTH_4
`define DMAC300_FIFO_DEPTH_8
//`define DMAC300_FIFO_DEPTH_16
//`define DMAC300_FIFO_DEPTH_32

//-------------------------------------------------
// DMAC Request/Acknowledge Handshake
//-------------------------------------------------
`define DMAC300_REQ_ACK_NUM	16
//`define DMAC300_REQ_SYNC_SUPPORT

//-------------------------------------------------
// DMAC Chain Transfer Support
//-------------------------------------------------
//`define DMAC300_CHAIN_TRANSFER_SUPPORT

//-------------------------------------------------
// DMAC Dual DMA Cores Support
//-------------------------------------------------
//`define DMAC300_DUAL_DMA_CORE_SUPPORT

//-------------------------------------------------
// DMAC Dual Master Interfaces Support
//-------------------------------------------------
//`define DMAC300_DUAL_MASTER_IF_SUPPORT

//-------------------------------------------------
// DMAC Address Width
//-------------------------------------------------
`define DMAC300_ADDR_WIDTH 64

//-------------------------------------------------
// DMAC Data Width
//-------------------------------------------------
`define DMAC300_DATA_WIDTH_512

`endif // DMAC300_CONFIG_VH

