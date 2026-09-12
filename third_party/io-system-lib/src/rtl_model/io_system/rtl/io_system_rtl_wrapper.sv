// SPDX-License-Identifier: Apache-2.0
//
// Minimal RTL io-system wrapper used to validate the q2io/RTL boundary.
//
// The wrapper deliberately exposes one AXI slave port and one AXI master port.
// Address decode is performed inside this RTL wrapper, not in QEMU.

module io_system_rtl_wrapper #(
    parameter logic [63:0] DMAC_BASE = 64'h0000_0000_3004_0000,
    parameter logic [63:0] DMAC_SIZE = 64'h0000_0000_0000_1000,
    parameter logic [63:0] MY_VIRTIO_BLK_BASE = 64'h0000_0000_310a_0000,
    parameter logic [63:0] MY_VIRTIO_BLK_SIZE = 64'h0000_0000_0000_1000,
    parameter logic [63:0] APLIC_M_BASE = 64'h0000_0000_3110_0000,
    parameter logic [63:0] APLIC_S_BASE = 64'h0000_0000_3112_0000,
    parameter logic [63:0] APLIC_SIZE = 64'h0000_0000_0000_4000,
    parameter int unsigned DATA_WIDTH = 64,
    parameter int unsigned STRB_WIDTH = DATA_WIDTH / 8
) (
    input  logic                    clk,
    input  logic                    rstn,

    input  logic [15:0]             s_axi_awid,
    input  logic [63:0]             s_axi_awaddr,
    input  logic [7:0]              s_axi_awlen,
    input  logic [2:0]              s_axi_awsize,
    input  logic [1:0]              s_axi_awburst,
    input  logic                    s_axi_awvalid,
    output logic                    s_axi_awready,

    input  logic [DATA_WIDTH-1:0]   s_axi_wdata,
    input  logic [STRB_WIDTH-1:0]   s_axi_wstrb,
    input  logic                    s_axi_wlast,
    input  logic                    s_axi_wvalid,
    output logic                    s_axi_wready,

    output logic [15:0]             s_axi_bid,
    output logic [1:0]              s_axi_bresp,
    output logic                    s_axi_bvalid,
    input  logic                    s_axi_bready,

    input  logic [15:0]             s_axi_arid,
    input  logic [63:0]             s_axi_araddr,
    input  logic [7:0]              s_axi_arlen,
    input  logic [2:0]              s_axi_arsize,
    input  logic [1:0]              s_axi_arburst,
    input  logic                    s_axi_arvalid,
    output logic                    s_axi_arready,

    output logic [15:0]             s_axi_rid,
    output logic [DATA_WIDTH-1:0]   s_axi_rdata,
    output logic [1:0]              s_axi_rresp,
    output logic                    s_axi_rlast,
    output logic                    s_axi_rvalid,
    input  logic                    s_axi_rready,

    input  logic [15:0]             gbus_axi_awid,
    input  logic [31:0]             gbus_axi_awaddr,
    input  logic [7:0]              gbus_axi_awlen,
    input  logic [2:0]              gbus_axi_awsize,
    input  logic [1:0]              gbus_axi_awburst,
    input  logic                    gbus_axi_awvalid,
    output logic                    gbus_axi_awready,

    input  logic [31:0]             gbus_axi_wdata,
    input  logic [3:0]              gbus_axi_wstrb,
    input  logic                    gbus_axi_wlast,
    input  logic                    gbus_axi_wvalid,
    output logic                    gbus_axi_wready,

    output logic [15:0]             gbus_axi_bid,
    output logic [1:0]              gbus_axi_bresp,
    output logic                    gbus_axi_bvalid,
    input  logic                    gbus_axi_bready,

    input  logic [15:0]             gbus_axi_arid,
    input  logic [31:0]             gbus_axi_araddr,
    input  logic [7:0]              gbus_axi_arlen,
    input  logic [2:0]              gbus_axi_arsize,
    input  logic [1:0]              gbus_axi_arburst,
    input  logic                    gbus_axi_arvalid,
    output logic                    gbus_axi_arready,

    output logic [15:0]             gbus_axi_rid,
    output logic [31:0]             gbus_axi_rdata,
    output logic [1:0]              gbus_axi_rresp,
    output logic                    gbus_axi_rlast,
    output logic                    gbus_axi_rvalid,
    input  logic                    gbus_axi_rready,

    output logic [15:0]             m_axi_awid,
    output logic [63:0]             m_axi_awaddr,
    output logic [7:0]              m_axi_awlen,
    output logic [2:0]              m_axi_awsize,
    output logic [1:0]              m_axi_awburst,
    output logic                    m_axi_awvalid,
    input  logic                    m_axi_awready,

    output logic [DATA_WIDTH-1:0]   m_axi_wdata,
    output logic [STRB_WIDTH-1:0]   m_axi_wstrb,
    output logic                    m_axi_wlast,
    output logic                    m_axi_wvalid,
    input  logic                    m_axi_wready,

    input  logic [15:0]             m_axi_bid,
    input  logic [1:0]              m_axi_bresp,
    input  logic                    m_axi_bvalid,
    output logic                    m_axi_bready,

    output logic [15:0]             m_axi_arid,
    output logic [63:0]             m_axi_araddr,
    output logic [7:0]              m_axi_arlen,
    output logic [2:0]              m_axi_arsize,
    output logic [1:0]              m_axi_arburst,
    output logic                    m_axi_arvalid,
    input  logic                    m_axi_arready,

    input  logic [15:0]             m_axi_rid,
    input  logic [DATA_WIDTH-1:0]   m_axi_rdata,
    input  logic [1:0]              m_axi_rresp,
    input  logic                    m_axi_rlast,
    input  logic                    m_axi_rvalid,
    output logic                    m_axi_rready
);



    logic                  dmac_wr_valid;
    logic [11:0]           dmac_wr_addr;
    logic [DATA_WIDTH-1:0] dmac_wr_data;
    logic [STRB_WIDTH-1:0] dmac_wr_strb;
    logic                  dmac_wr_ready;
    logic [1:0]            dmac_wr_resp;
    logic                  dmac_rd_valid;
    logic [11:0]           dmac_rd_addr;
    logic                  dmac_rd_ready;
    logic [DATA_WIDTH-1:0] dmac_rd_data;
    logic [1:0]            dmac_rd_resp;

    logic                  virtio_valid;
    logic                  virtio_write;
    logic [11:0]           virtio_addr;
    logic [31:0]           virtio_wdata;
    logic [2:0]            virtio_size;
    logic [31:0]           virtio_rdata;
    logic                  virtio_ready;
    logic                  virtio_interrupt;
    logic [31:0]           virtio_publish_addr;
    logic [31:0]           virtio_publish_data;
    logic                  virtio_todut_en;
    logic [31:0]           virtio_todut_addr;
    logic [31:0]           virtio_todut_data;

    logic                  aplic_m_wr_valid;
    logic [13:0]           aplic_m_wr_addr;
    logic [DATA_WIDTH-1:0] aplic_m_wr_data;
    logic [STRB_WIDTH-1:0] aplic_m_wr_strb;
    logic                  aplic_m_wr_ready;
    logic [1:0]            aplic_m_wr_resp;
    logic                  aplic_m_rd_valid;
    logic [13:0]           aplic_m_rd_addr;
    logic                  aplic_m_rd_ready;
    logic [DATA_WIDTH-1:0] aplic_m_rd_data;
    logic [1:0]            aplic_m_rd_resp;

    logic                  aplic_s_wr_valid;
    logic [13:0]           aplic_s_wr_addr;
    logic [DATA_WIDTH-1:0] aplic_s_wr_data;
    logic [STRB_WIDTH-1:0] aplic_s_wr_strb;
    logic                  aplic_s_wr_ready;
    logic [1:0]            aplic_s_wr_resp;
    logic                  aplic_s_rd_valid;
    logic [13:0]           aplic_s_rd_addr;
    logic                  aplic_s_rd_ready;
    logic [DATA_WIDTH-1:0] aplic_s_rd_data;
    logic [1:0]            aplic_s_rd_resp;

    logic [31:0] m_aplic_smsi_addr;
    logic [31:0] m_aplic_smsi_addrh;
    logic [31:0] s_aplic_smsi_addr_unused;
    logic [31:0] s_aplic_smsi_addrh_unused;
    logic        aplic_m_delegated_irq;

    logic [15:0]           dmac_m_awid;
    logic [63:0]           dmac_m_awaddr;
    logic [7:0]            dmac_m_awlen;
    logic [2:0]            dmac_m_awsize;
    logic [1:0]            dmac_m_awburst;
    logic                  dmac_m_awvalid;
    logic                  dmac_m_awready;
    logic [DATA_WIDTH-1:0] dmac_m_wdata;
    logic [STRB_WIDTH-1:0] dmac_m_wstrb;
    logic                  dmac_m_wlast;
    logic                  dmac_m_wvalid;
    logic                  dmac_m_wready;
    logic                  dmac_m_bready;

    logic [15:0]           aplic_m_m_awid;
    logic [63:0]           aplic_m_m_awaddr;
    logic [7:0]            aplic_m_m_awlen;
    logic [2:0]            aplic_m_m_awsize;
    logic [1:0]            aplic_m_m_awburst;
    logic                  aplic_m_m_awvalid;
    logic                  aplic_m_m_awready;
    logic [DATA_WIDTH-1:0] aplic_m_m_wdata;
    logic [STRB_WIDTH-1:0] aplic_m_m_wstrb;
    logic                  aplic_m_m_wlast;
    logic                  aplic_m_m_wvalid;
    logic                  aplic_m_m_wready;
    logic                  aplic_m_m_bready;

    logic [15:0]           aplic_s_m_awid;
    logic [63:0]           aplic_s_m_awaddr;
    logic [7:0]            aplic_s_m_awlen;
    logic [2:0]            aplic_s_m_awsize;
    logic [1:0]            aplic_s_m_awburst;
    logic                  aplic_s_m_awvalid;
    logic                  aplic_s_m_awready;
    logic [DATA_WIDTH-1:0] aplic_s_m_wdata;
    logic [STRB_WIDTH-1:0] aplic_s_m_wstrb;
    logic                  aplic_s_m_wlast;
    logic                  aplic_s_m_wvalid;
    logic                  aplic_s_m_wready;
    logic                  aplic_s_m_bready;

    typedef enum logic [1:0] {
        WRITE_OWNER_NONE,
        WRITE_OWNER_DMAC,
        WRITE_OWNER_APLIC_M,
        WRITE_OWNER_APLIC_S
    } write_owner_t;
    write_owner_t write_owner_q;
    write_owner_t write_owner_sel;

    // RA mode inserts a complete AXI branch in front of the existing register mux.
    logic [15:0] legacy_s_axi_awid;
    logic [63:0] legacy_s_axi_awaddr;
    logic [7:0] legacy_s_axi_awlen;
    logic [2:0] legacy_s_axi_awsize;
    logic [1:0] legacy_s_axi_awburst;
    logic  legacy_s_axi_awvalid;
    logic  legacy_s_axi_awready;
    logic [DATA_WIDTH-1:0] legacy_s_axi_wdata;
    logic [STRB_WIDTH-1:0] legacy_s_axi_wstrb;
    logic  legacy_s_axi_wlast;
    logic  legacy_s_axi_wvalid;
    logic  legacy_s_axi_wready;
    logic [15:0] legacy_s_axi_bid;
    logic [1:0] legacy_s_axi_bresp;
    logic  legacy_s_axi_bvalid;
    logic  legacy_s_axi_bready;
    logic [15:0] legacy_s_axi_arid;
    logic [63:0] legacy_s_axi_araddr;
    logic [7:0] legacy_s_axi_arlen;
    logic [2:0] legacy_s_axi_arsize;
    logic [1:0] legacy_s_axi_arburst;
    logic  legacy_s_axi_arvalid;
    logic  legacy_s_axi_arready;
    logic [15:0] legacy_s_axi_rid;
    logic [DATA_WIDTH-1:0] legacy_s_axi_rdata;
    logic [1:0] legacy_s_axi_rresp;
    logic  legacy_s_axi_rlast;
    logic  legacy_s_axi_rvalid;
    logic  legacy_s_axi_rready;
    logic [15:0] dmac_m_bid;
    logic [1:0] dmac_m_bresp;
    logic  dmac_m_bvalid;
    logic [15:0] aplic_m_m_bid;
    logic [1:0] aplic_m_m_bresp;
    logic  aplic_m_m_bvalid;
    logic [15:0] aplic_s_m_bid;
    logic [1:0] aplic_s_m_bresp;
    logic  aplic_s_m_bvalid;
    logic [15:0] dmac_m_arid;
    logic [63:0] dmac_m_araddr;
    logic [7:0] dmac_m_arlen;
    logic [2:0] dmac_m_arsize;
    logic [1:0] dmac_m_arburst;
    logic  dmac_m_arvalid;
    logic  dmac_m_arready;
    logic [15:0] dmac_m_rid;
    logic [DATA_WIDTH-1:0] dmac_m_rdata;
    logic [1:0] dmac_m_rresp;
    logic  dmac_m_rlast;
    logic  dmac_m_rvalid;
    logic  dmac_m_rready;

`ifndef IO_SYSTEM_RTL_ARENA
    assign legacy_s_axi_awid = s_axi_awid;
    assign legacy_s_axi_awaddr = s_axi_awaddr;
    assign legacy_s_axi_awlen = s_axi_awlen;
    assign legacy_s_axi_awsize = s_axi_awsize;
    assign legacy_s_axi_awburst = s_axi_awburst;
    assign legacy_s_axi_awvalid = s_axi_awvalid;
    assign s_axi_awready = legacy_s_axi_awready;
    assign legacy_s_axi_wdata = s_axi_wdata;
    assign legacy_s_axi_wstrb = s_axi_wstrb;
    assign legacy_s_axi_wlast = s_axi_wlast;
    assign legacy_s_axi_wvalid = s_axi_wvalid;
    assign s_axi_wready = legacy_s_axi_wready;
    assign s_axi_bid = legacy_s_axi_bid;
    assign s_axi_bresp = legacy_s_axi_bresp;
    assign s_axi_bvalid = legacy_s_axi_bvalid;
    assign legacy_s_axi_bready = s_axi_bready;
    assign legacy_s_axi_arid = s_axi_arid;
    assign legacy_s_axi_araddr = s_axi_araddr;
    assign legacy_s_axi_arlen = s_axi_arlen;
    assign legacy_s_axi_arsize = s_axi_arsize;
    assign legacy_s_axi_arburst = s_axi_arburst;
    assign legacy_s_axi_arvalid = s_axi_arvalid;
    assign s_axi_arready = legacy_s_axi_arready;
    assign s_axi_rid = legacy_s_axi_rid;
    assign s_axi_rdata = legacy_s_axi_rdata;
    assign s_axi_rresp = legacy_s_axi_rresp;
    assign s_axi_rlast = legacy_s_axi_rlast;
    assign s_axi_rvalid = legacy_s_axi_rvalid;
    assign legacy_s_axi_rready = s_axi_rready;
    assign dmac_m_bid = m_axi_bid;
    assign dmac_m_bresp = m_axi_bresp;
    assign dmac_m_bvalid = (write_owner_q == WRITE_OWNER_DMAC) && m_axi_bvalid;
    assign aplic_m_m_bid = m_axi_bid;
    assign aplic_m_m_bresp = m_axi_bresp;
    assign aplic_m_m_bvalid = (write_owner_q == WRITE_OWNER_APLIC_M) && m_axi_bvalid;
    assign aplic_s_m_bid = m_axi_bid;
    assign aplic_s_m_bresp = m_axi_bresp;
    assign aplic_s_m_bvalid = (write_owner_q == WRITE_OWNER_APLIC_S) && m_axi_bvalid;
    assign m_axi_arid = dmac_m_arid;
    assign m_axi_araddr = dmac_m_araddr;
    assign m_axi_arlen = dmac_m_arlen;
    assign m_axi_arsize = dmac_m_arsize;
    assign m_axi_arburst = dmac_m_arburst;
    assign m_axi_arvalid = dmac_m_arvalid;
    assign dmac_m_arready = m_axi_arready;
    assign dmac_m_rid = m_axi_rid;
    assign dmac_m_rdata = m_axi_rdata;
    assign dmac_m_rresp = m_axi_rresp;
    assign dmac_m_rlast = m_axi_rlast;
    assign dmac_m_rvalid = m_axi_rvalid;
    assign m_axi_rready = dmac_m_rready;
`else
    import io_system_rtl_arena_pkg::*;
    a64i16_req_t config_req, legacy_req;
    a64i16_resp_t config_resp, legacy_resp;
    a64i8_req_t [3:0] source_req;
    a64i8_resp_t [3:0] source_resp;
    a64i10_req_t master_req;
    a64i10_resp_t master_resp;
    always_comb begin
        config_req = '0;
        legacy_resp = '0;
        config_req.aw.id = s_axi_awid;
        config_req.aw.addr = s_axi_awaddr;
        config_req.aw.len = s_axi_awlen;
        config_req.aw.size = s_axi_awsize;
        config_req.aw.burst = s_axi_awburst;
        config_req.aw_valid = s_axi_awvalid;
        legacy_resp.aw_ready = legacy_s_axi_awready;
        config_req.w.data = s_axi_wdata;
        config_req.w.strb = s_axi_wstrb;
        config_req.w.last = s_axi_wlast;
        config_req.w_valid = s_axi_wvalid;
        legacy_resp.w_ready = legacy_s_axi_wready;
        legacy_resp.b.id = legacy_s_axi_bid;
        legacy_resp.b.resp = legacy_s_axi_bresp;
        legacy_resp.b_valid = legacy_s_axi_bvalid;
        config_req.b_ready = s_axi_bready;
        config_req.ar.id = s_axi_arid;
        config_req.ar.addr = s_axi_araddr;
        config_req.ar.len = s_axi_arlen;
        config_req.ar.size = s_axi_arsize;
        config_req.ar.burst = s_axi_arburst;
        config_req.ar_valid = s_axi_arvalid;
        legacy_resp.ar_ready = legacy_s_axi_arready;
        legacy_resp.r.id = legacy_s_axi_rid;
        legacy_resp.r.data = legacy_s_axi_rdata;
        legacy_resp.r.resp = legacy_s_axi_rresp;
        legacy_resp.r.last = legacy_s_axi_rlast;
        legacy_resp.r_valid = legacy_s_axi_rvalid;
        config_req.r_ready = s_axi_rready;
    end
    assign legacy_s_axi_awid = legacy_req.aw.id;
    assign legacy_s_axi_awaddr = legacy_req.aw.addr;
    assign legacy_s_axi_awlen = legacy_req.aw.len;
    assign legacy_s_axi_awsize = legacy_req.aw.size;
    assign legacy_s_axi_awburst = legacy_req.aw.burst;
    assign legacy_s_axi_awvalid = legacy_req.aw_valid;
    assign s_axi_awready = config_resp.aw_ready;
    assign legacy_s_axi_wdata = legacy_req.w.data;
    assign legacy_s_axi_wstrb = legacy_req.w.strb;
    assign legacy_s_axi_wlast = legacy_req.w.last;
    assign legacy_s_axi_wvalid = legacy_req.w_valid;
    assign s_axi_wready = config_resp.w_ready;
    assign s_axi_bid = config_resp.b.id;
    assign s_axi_bresp = config_resp.b.resp;
    assign s_axi_bvalid = config_resp.b_valid;
    assign legacy_s_axi_bready = legacy_req.b_ready;
    assign legacy_s_axi_arid = legacy_req.ar.id;
    assign legacy_s_axi_araddr = legacy_req.ar.addr;
    assign legacy_s_axi_arlen = legacy_req.ar.len;
    assign legacy_s_axi_arsize = legacy_req.ar.size;
    assign legacy_s_axi_arburst = legacy_req.ar.burst;
    assign legacy_s_axi_arvalid = legacy_req.ar_valid;
    assign s_axi_arready = config_resp.ar_ready;
    assign s_axi_rid = config_resp.r.id;
    assign s_axi_rdata = config_resp.r.data;
    assign s_axi_rresp = config_resp.r.resp;
    assign s_axi_rlast = config_resp.r.last;
    assign s_axi_rvalid = config_resp.r_valid;
    assign legacy_s_axi_rready = legacy_req.r_ready;

    io_system_rtl_arena u_arena_bridge (
        .clk(clk), .rstn(rstn), .config_req_i(config_req), .config_resp_o(config_resp),
        .legacy_req_o(legacy_req), .legacy_resp_i(legacy_resp),
        .ddr_req_o(source_req[3]), .ddr_resp_i(source_resp[3])
    );
    // Mux appends the two-bit source number above the complete eight-bit ID.
    axi_mux #(
        .SlvAxiIDWidth(8), .NoSlvPorts(4), .MaxWTrans(16),
        .slv_aw_chan_t(a64i8_aw_chan_t), .mst_aw_chan_t(a64i10_aw_chan_t), .w_chan_t(a64i8_w_chan_t),
        .slv_b_chan_t(a64i8_b_chan_t), .mst_b_chan_t(a64i10_b_chan_t),
        .slv_ar_chan_t(a64i8_ar_chan_t), .mst_ar_chan_t(a64i10_ar_chan_t),
        .slv_r_chan_t(a64i8_r_chan_t), .mst_r_chan_t(a64i10_r_chan_t),
        .slv_req_t(a64i8_req_t), .slv_resp_t(a64i8_resp_t),
        .mst_req_t(a64i10_req_t), .mst_resp_t(a64i10_resp_t)
    ) u_master_mux (
        .clk_i(clk), .rst_ni(rstn), .test_i(1'b0),
        .slv_reqs_i(source_req), .slv_resps_o(source_resp),
        .mst_req_o(master_req), .mst_resp_i(master_resp)
    );
    always_comb begin
        master_resp = '0;
        master_resp.aw_ready = m_axi_awready;
        master_resp.w_ready = m_axi_wready;
        master_resp.b.id = m_axi_bid[9:0];
        master_resp.b.resp = m_axi_bresp;
        master_resp.b_valid = m_axi_bvalid;
        master_resp.ar_ready = m_axi_arready;
        master_resp.r.id = m_axi_rid[9:0];
        master_resp.r.data = m_axi_rdata;
        master_resp.r.resp = m_axi_rresp;
        master_resp.r.last = m_axi_rlast;
        master_resp.r_valid = m_axi_rvalid;
    end
    assign m_axi_awid = {6'b0, master_req.aw.id};
    assign m_axi_awaddr = master_req.aw.addr;
    assign m_axi_awlen = master_req.aw.len;
    assign m_axi_awsize = master_req.aw.size;
    assign m_axi_awburst = master_req.aw.burst;
    assign m_axi_awvalid = master_req.aw_valid;
    assign m_axi_wdata = master_req.w.data;
    assign m_axi_wstrb = master_req.w.strb;
    assign m_axi_wlast = master_req.w.last;
    assign m_axi_wvalid = master_req.w_valid;
    assign m_axi_bready = master_req.b_ready;
    assign m_axi_arid = {6'b0, master_req.ar.id};
    assign m_axi_araddr = master_req.ar.addr;
    assign m_axi_arlen = master_req.ar.len;
    assign m_axi_arsize = master_req.ar.size;
    assign m_axi_arburst = master_req.ar.burst;
    assign m_axi_arvalid = master_req.ar_valid;
    assign m_axi_rready = master_req.r_ready;
    always_comb begin
        source_req[0] = '0;
        source_req[0].aw.id = dmac_m_awid[7:0];
        source_req[0].aw.addr = dmac_m_awaddr;
        source_req[0].aw.len = dmac_m_awlen;
        source_req[0].aw.size = dmac_m_awsize;
        source_req[0].aw.burst = dmac_m_awburst;
        source_req[0].aw_valid = dmac_m_awvalid;
        source_req[0].w.data = dmac_m_wdata;
        source_req[0].w.strb = dmac_m_wstrb;
        source_req[0].w.last = dmac_m_wlast;
        source_req[0].w_valid = dmac_m_wvalid;
        source_req[0].b_ready = dmac_m_bready;
        source_req[0].ar.id = dmac_m_arid[7:0];
        source_req[0].ar.addr = dmac_m_araddr;
        source_req[0].ar.len = dmac_m_arlen;
        source_req[0].ar.size = dmac_m_arsize;
        source_req[0].ar.burst = dmac_m_arburst;
        source_req[0].ar_valid = dmac_m_arvalid;
        source_req[0].r_ready = dmac_m_rready;
    end
    assign dmac_m_awready = source_resp[0].aw_ready;
    assign dmac_m_wready = source_resp[0].w_ready;
    assign dmac_m_bid = {8'b0, source_resp[0].b.id};
    assign dmac_m_bresp = source_resp[0].b.resp;
    assign dmac_m_bvalid = source_resp[0].b_valid;
    assign dmac_m_arready = source_resp[0].ar_ready;
    assign dmac_m_rid = {8'b0, source_resp[0].r.id};
    assign dmac_m_rdata = source_resp[0].r.data;
    assign dmac_m_rresp = source_resp[0].r.resp;
    assign dmac_m_rlast = source_resp[0].r.last;
    assign dmac_m_rvalid = source_resp[0].r_valid;
    always @(posedge clk) if (rstn) begin
        if (dmac_m_awvalid) assert (dmac_m_awid[15:8] == 0)
            else $error("io-system master 0 ID exceeds eight bits");
        if (dmac_m_arvalid) assert (dmac_m_arid[15:8] == 0)
            else $error("io-system DMAC read ID exceeds eight bits");
    end
    always_comb begin
        source_req[1] = '0;
        source_req[1].aw.id = aplic_m_m_awid[7:0];
        source_req[1].aw.addr = aplic_m_m_awaddr;
        source_req[1].aw.len = aplic_m_m_awlen;
        source_req[1].aw.size = aplic_m_m_awsize;
        source_req[1].aw.burst = aplic_m_m_awburst;
        source_req[1].aw_valid = aplic_m_m_awvalid;
        source_req[1].w.data = aplic_m_m_wdata;
        source_req[1].w.strb = aplic_m_m_wstrb;
        source_req[1].w.last = aplic_m_m_wlast;
        source_req[1].w_valid = aplic_m_m_wvalid;
        source_req[1].b_ready = aplic_m_m_bready;
    end
    assign aplic_m_m_awready = source_resp[1].aw_ready;
    assign aplic_m_m_wready = source_resp[1].w_ready;
    assign aplic_m_m_bid = {8'b0, source_resp[1].b.id};
    assign aplic_m_m_bresp = source_resp[1].b.resp;
    assign aplic_m_m_bvalid = source_resp[1].b_valid;
    always @(posedge clk) if (rstn) begin
        if (aplic_m_m_awvalid) assert (aplic_m_m_awid[15:8] == 0)
            else $error("io-system master 1 ID exceeds eight bits");
    end
    always_comb begin
        source_req[2] = '0;
        source_req[2].aw.id = aplic_s_m_awid[7:0];
        source_req[2].aw.addr = aplic_s_m_awaddr;
        source_req[2].aw.len = aplic_s_m_awlen;
        source_req[2].aw.size = aplic_s_m_awsize;
        source_req[2].aw.burst = aplic_s_m_awburst;
        source_req[2].aw_valid = aplic_s_m_awvalid;
        source_req[2].w.data = aplic_s_m_wdata;
        source_req[2].w.strb = aplic_s_m_wstrb;
        source_req[2].w.last = aplic_s_m_wlast;
        source_req[2].w_valid = aplic_s_m_wvalid;
        source_req[2].b_ready = aplic_s_m_bready;
    end
    assign aplic_s_m_awready = source_resp[2].aw_ready;
    assign aplic_s_m_wready = source_resp[2].w_ready;
    assign aplic_s_m_bid = {8'b0, source_resp[2].b.id};
    assign aplic_s_m_bresp = source_resp[2].b.resp;
    assign aplic_s_m_bvalid = source_resp[2].b_valid;
    always @(posedge clk) if (rstn) begin
        if (aplic_s_m_awvalid) assert (aplic_s_m_awid[15:8] == 0)
            else $error("io-system master 2 ID exceeds eight bits");
    end
    initial assert (DATA_WIDTH == 64 && STRB_WIDTH == 8)
        else $fatal(1, "Arena integration requires the 64-bit io-system port");
`endif

    io_system_rtl_mmio_mux #(
        .DMAC_BASE(DMAC_BASE),
        .DMAC_SIZE(DMAC_SIZE),
        .MY_VIRTIO_BLK_BASE(MY_VIRTIO_BLK_BASE),
        .MY_VIRTIO_BLK_SIZE(MY_VIRTIO_BLK_SIZE),
        .APLIC_M_BASE(APLIC_M_BASE),
        .APLIC_S_BASE(APLIC_S_BASE),
        .APLIC_SIZE(APLIC_SIZE),
        .DATA_WIDTH(DATA_WIDTH),
        .STRB_WIDTH(STRB_WIDTH)
    ) u_mmio_mux (
        .clk(clk),
        .rstn(rstn),
        .s_axi_awid(legacy_s_axi_awid),
        .s_axi_awaddr(legacy_s_axi_awaddr),
        .s_axi_awlen(legacy_s_axi_awlen),
        .s_axi_awsize(legacy_s_axi_awsize),
        .s_axi_awburst(legacy_s_axi_awburst),
        .s_axi_awvalid(legacy_s_axi_awvalid),
        .s_axi_awready(legacy_s_axi_awready),
        .s_axi_wdata(legacy_s_axi_wdata),
        .s_axi_wstrb(legacy_s_axi_wstrb),
        .s_axi_wlast(legacy_s_axi_wlast),
        .s_axi_wvalid(legacy_s_axi_wvalid),
        .s_axi_wready(legacy_s_axi_wready),
        .s_axi_bid(legacy_s_axi_bid),
        .s_axi_bresp(legacy_s_axi_bresp),
        .s_axi_bvalid(legacy_s_axi_bvalid),
        .s_axi_bready(legacy_s_axi_bready),
        .s_axi_arid(legacy_s_axi_arid),
        .s_axi_araddr(legacy_s_axi_araddr),
        .s_axi_arlen(legacy_s_axi_arlen),
        .s_axi_arsize(legacy_s_axi_arsize),
        .s_axi_arburst(legacy_s_axi_arburst),
        .s_axi_arvalid(legacy_s_axi_arvalid),
        .s_axi_arready(legacy_s_axi_arready),
        .s_axi_rid(legacy_s_axi_rid),
        .s_axi_rdata(legacy_s_axi_rdata),
        .s_axi_rresp(legacy_s_axi_rresp),
        .s_axi_rlast(legacy_s_axi_rlast),
        .s_axi_rvalid(legacy_s_axi_rvalid),
        .s_axi_rready(legacy_s_axi_rready),
        .dmac_wr_valid(dmac_wr_valid),
        .dmac_wr_addr(dmac_wr_addr),
        .dmac_wr_data(dmac_wr_data),
        .dmac_wr_strb(dmac_wr_strb),
        .dmac_wr_ready(dmac_wr_ready),
        .dmac_wr_resp(dmac_wr_resp),
        .dmac_rd_valid(dmac_rd_valid),
        .dmac_rd_addr(dmac_rd_addr),
        .dmac_rd_ready(dmac_rd_ready),
        .dmac_rd_data(dmac_rd_data),
        .dmac_rd_resp(dmac_rd_resp),
        .virtio_valid(virtio_valid),
        .virtio_write(virtio_write),
        .virtio_addr(virtio_addr),
        .virtio_wdata(virtio_wdata),
        .virtio_size(virtio_size),
        .virtio_rdata(virtio_rdata),
        .virtio_ready(virtio_ready),
        .aplic_m_wr_valid(aplic_m_wr_valid),
        .aplic_m_wr_addr(aplic_m_wr_addr),
        .aplic_m_wr_data(aplic_m_wr_data),
        .aplic_m_wr_strb(aplic_m_wr_strb),
        .aplic_m_wr_ready(aplic_m_wr_ready),
        .aplic_m_wr_resp(aplic_m_wr_resp),
        .aplic_m_rd_valid(aplic_m_rd_valid),
        .aplic_m_rd_addr(aplic_m_rd_addr),
        .aplic_m_rd_ready(aplic_m_rd_ready),
        .aplic_m_rd_data(aplic_m_rd_data),
        .aplic_m_rd_resp(aplic_m_rd_resp),
        .aplic_s_wr_valid(aplic_s_wr_valid),
        .aplic_s_wr_addr(aplic_s_wr_addr),
        .aplic_s_wr_data(aplic_s_wr_data),
        .aplic_s_wr_strb(aplic_s_wr_strb),
        .aplic_s_wr_ready(aplic_s_wr_ready),
        .aplic_s_wr_resp(aplic_s_wr_resp),
        .aplic_s_rd_valid(aplic_s_rd_valid),
        .aplic_s_rd_addr(aplic_s_rd_addr),
        .aplic_s_rd_ready(aplic_s_rd_ready),
        .aplic_s_rd_data(aplic_s_rd_data),
        .aplic_s_rd_resp(aplic_s_rd_resp)
    );

    virtio_gbus_mmio_blk_top u_virtio_blk (
        .clock(clk),
        .reset(!rstn),
        .addr(virtio_addr),
        .write(virtio_write),
        .wdata(virtio_wdata),
        .size(virtio_size),
        .rdata(virtio_rdata),
        .ready(virtio_ready),
        .valid(virtio_valid),
        .interrupt(virtio_interrupt),
        .tosysbus_addr(virtio_publish_addr),
        .tosysbus_data(virtio_publish_data),
        .todut_en(virtio_todut_en),
        .todut_addr(virtio_todut_addr),
        .todut_data(virtio_todut_data)
    );

    io_system_rtl_gbus_shadow u_gbus_shadow (
        .clk(clk),
        .rstn(rstn),
        .publish_addr(virtio_publish_addr),
        .publish_data(virtio_publish_data),
        .todut_en(virtio_todut_en),
        .todut_addr(virtio_todut_addr),
        .todut_data(virtio_todut_data),
        .s_axi_awid(gbus_axi_awid),
        .s_axi_awaddr(gbus_axi_awaddr),
        .s_axi_awlen(gbus_axi_awlen),
        .s_axi_awsize(gbus_axi_awsize),
        .s_axi_awburst(gbus_axi_awburst),
        .s_axi_awvalid(gbus_axi_awvalid),
        .s_axi_awready(gbus_axi_awready),
        .s_axi_wdata(gbus_axi_wdata),
        .s_axi_wstrb(gbus_axi_wstrb),
        .s_axi_wlast(gbus_axi_wlast),
        .s_axi_wvalid(gbus_axi_wvalid),
        .s_axi_wready(gbus_axi_wready),
        .s_axi_bid(gbus_axi_bid),
        .s_axi_bresp(gbus_axi_bresp),
        .s_axi_bvalid(gbus_axi_bvalid),
        .s_axi_bready(gbus_axi_bready),
        .s_axi_arid(gbus_axi_arid),
        .s_axi_araddr(gbus_axi_araddr),
        .s_axi_arlen(gbus_axi_arlen),
        .s_axi_arsize(gbus_axi_arsize),
        .s_axi_arburst(gbus_axi_arburst),
        .s_axi_arvalid(gbus_axi_arvalid),
        .s_axi_arready(gbus_axi_arready),
        .s_axi_rid(gbus_axi_rid),
        .s_axi_rdata(gbus_axi_rdata),
        .s_axi_rresp(gbus_axi_rresp),
        .s_axi_rlast(gbus_axi_rlast),
        .s_axi_rvalid(gbus_axi_rvalid),
        .s_axi_rready(gbus_axi_rready)
    );

    io_system_rtl_aplic_msi #(
        .MMODE(1'b1),
        .DOMAIN_ADDR(APLIC_M_BASE[31:0]),
        .DATA_WIDTH(DATA_WIDTH),
        .STRB_WIDTH(STRB_WIDTH)
    ) u_aplic_m (
        .clk(clk),
        .rstn(rstn),
        .parent_smsi_addr(32'h0),
        .parent_smsi_addrh(32'h0),
        .smsi_addr(m_aplic_smsi_addr),
        .smsi_addrh(m_aplic_smsi_addrh),
        .irq_line(virtio_interrupt),
        .irq_source(32'd15),
        .delegated_irq_line(aplic_m_delegated_irq),
        .reg_wr_valid(aplic_m_wr_valid),
        .reg_wr_addr(aplic_m_wr_addr),
        .reg_wr_data(aplic_m_wr_data),
        .reg_wr_strb(aplic_m_wr_strb),
        .reg_wr_ready(aplic_m_wr_ready),
        .reg_wr_resp(aplic_m_wr_resp),
        .reg_rd_valid(aplic_m_rd_valid),
        .reg_rd_addr(aplic_m_rd_addr),
        .reg_rd_ready(aplic_m_rd_ready),
        .reg_rd_data(aplic_m_rd_data),
        .reg_rd_resp(aplic_m_rd_resp),
        .m_axi_awid(aplic_m_m_awid),
        .m_axi_awaddr(aplic_m_m_awaddr),
        .m_axi_awlen(aplic_m_m_awlen),
        .m_axi_awsize(aplic_m_m_awsize),
        .m_axi_awburst(aplic_m_m_awburst),
        .m_axi_awvalid(aplic_m_m_awvalid),
        .m_axi_awready(aplic_m_m_awready),
        .m_axi_wdata(aplic_m_m_wdata),
        .m_axi_wstrb(aplic_m_m_wstrb),
        .m_axi_wlast(aplic_m_m_wlast),
        .m_axi_wvalid(aplic_m_m_wvalid),
        .m_axi_wready(aplic_m_m_wready),
        .m_axi_bid(aplic_m_m_bid),
        .m_axi_bresp(aplic_m_m_bresp),
        .m_axi_bvalid(aplic_m_m_bvalid),
        .m_axi_bready(aplic_m_m_bready)
    );

    io_system_rtl_aplic_msi #(
        .MMODE(1'b0),
        .DOMAIN_ADDR(APLIC_S_BASE[31:0]),
        .DATA_WIDTH(DATA_WIDTH),
        .STRB_WIDTH(STRB_WIDTH)
    ) u_aplic_s (
        .clk(clk),
        .rstn(rstn),
        .parent_smsi_addr(m_aplic_smsi_addr),
        .parent_smsi_addrh(m_aplic_smsi_addrh),
        .smsi_addr(s_aplic_smsi_addr_unused),
        .smsi_addrh(s_aplic_smsi_addrh_unused),
        .irq_line(aplic_m_delegated_irq),
        .irq_source(32'd15),
        .delegated_irq_line(),
        .reg_wr_valid(aplic_s_wr_valid),
        .reg_wr_addr(aplic_s_wr_addr),
        .reg_wr_data(aplic_s_wr_data),
        .reg_wr_strb(aplic_s_wr_strb),
        .reg_wr_ready(aplic_s_wr_ready),
        .reg_wr_resp(aplic_s_wr_resp),
        .reg_rd_valid(aplic_s_rd_valid),
        .reg_rd_addr(aplic_s_rd_addr),
        .reg_rd_ready(aplic_s_rd_ready),
        .reg_rd_data(aplic_s_rd_data),
        .reg_rd_resp(aplic_s_rd_resp),
        .m_axi_awid(aplic_s_m_awid),
        .m_axi_awaddr(aplic_s_m_awaddr),
        .m_axi_awlen(aplic_s_m_awlen),
        .m_axi_awsize(aplic_s_m_awsize),
        .m_axi_awburst(aplic_s_m_awburst),
        .m_axi_awvalid(aplic_s_m_awvalid),
        .m_axi_awready(aplic_s_m_awready),
        .m_axi_wdata(aplic_s_m_wdata),
        .m_axi_wstrb(aplic_s_m_wstrb),
        .m_axi_wlast(aplic_s_m_wlast),
        .m_axi_wvalid(aplic_s_m_wvalid),
        .m_axi_wready(aplic_s_m_wready),
        .m_axi_bid(aplic_s_m_bid),
        .m_axi_bresp(aplic_s_m_bresp),
        .m_axi_bvalid(aplic_s_m_bvalid),
        .m_axi_bready(aplic_s_m_bready)
    );

    rtl_mem2mem_dmac #(
        .ADDR_WIDTH(64),
        .DATA_WIDTH(DATA_WIDTH),
        .STRB_WIDTH(STRB_WIDTH)
    ) u_dmac (
        .clk(clk),
        .rstn(rstn),
        .reg_wr_valid(dmac_wr_valid),
        .reg_wr_addr(dmac_wr_addr),
        .reg_wr_data(dmac_wr_data),
        .reg_wr_strb(dmac_wr_strb),
        .reg_wr_ready(dmac_wr_ready),
        .reg_wr_resp(dmac_wr_resp),
        .reg_rd_valid(dmac_rd_valid),
        .reg_rd_addr(dmac_rd_addr),
        .reg_rd_ready(dmac_rd_ready),
        .reg_rd_data(dmac_rd_data),
        .reg_rd_resp(dmac_rd_resp),
        .m_axi_awid(dmac_m_awid),
        .m_axi_awaddr(dmac_m_awaddr),
        .m_axi_awlen(dmac_m_awlen),
        .m_axi_awsize(dmac_m_awsize),
        .m_axi_awburst(dmac_m_awburst),
        .m_axi_awvalid(dmac_m_awvalid),
        .m_axi_awready(dmac_m_awready),
        .m_axi_wdata(dmac_m_wdata),
        .m_axi_wstrb(dmac_m_wstrb),
        .m_axi_wlast(dmac_m_wlast),
        .m_axi_wvalid(dmac_m_wvalid),
        .m_axi_wready(dmac_m_wready),
        .m_axi_bid(dmac_m_bid),
        .m_axi_bresp(dmac_m_bresp),
        .m_axi_bvalid(dmac_m_bvalid),
        .m_axi_bready(dmac_m_bready),
        .m_axi_arid(dmac_m_arid),
        .m_axi_araddr(dmac_m_araddr),
        .m_axi_arlen(dmac_m_arlen),
        .m_axi_arsize(dmac_m_arsize),
        .m_axi_arburst(dmac_m_arburst),
        .m_axi_arvalid(dmac_m_arvalid),
        .m_axi_arready(dmac_m_arready),
        .m_axi_rid(dmac_m_rid),
        .m_axi_rdata(dmac_m_rdata),
        .m_axi_rresp(dmac_m_rresp),
        .m_axi_rlast(dmac_m_rlast),
        .m_axi_rvalid(dmac_m_rvalid),
        .m_axi_rready(dmac_m_rready)
    );

`ifndef IO_SYSTEM_RTL_ARENA
    always_comb begin
        write_owner_sel = write_owner_q;
        if (write_owner_q == WRITE_OWNER_NONE) begin
            if (aplic_s_m_awvalid || aplic_s_m_wvalid) begin
                write_owner_sel = WRITE_OWNER_APLIC_S;
            end else if (aplic_m_m_awvalid || aplic_m_m_wvalid) begin
                write_owner_sel = WRITE_OWNER_APLIC_M;
            end else if (dmac_m_awvalid || dmac_m_wvalid) begin
                write_owner_sel = WRITE_OWNER_DMAC;
            end
        end

        m_axi_awid = 16'h0;
        m_axi_awaddr = 64'h0;
        m_axi_awlen = 8'h0;
        m_axi_awsize = 3'h0;
        m_axi_awburst = 2'b01;
        m_axi_awvalid = 1'b0;
        m_axi_wdata = '0;
        m_axi_wstrb = '0;
        m_axi_wlast = 1'b0;
        m_axi_wvalid = 1'b0;
        m_axi_bready = 1'b0;

        dmac_m_awready = 1'b0;
        dmac_m_wready = 1'b0;
        aplic_m_m_awready = 1'b0;
        aplic_m_m_wready = 1'b0;
        aplic_s_m_awready = 1'b0;
        aplic_s_m_wready = 1'b0;

        unique case (write_owner_q)
        WRITE_OWNER_DMAC: begin
            m_axi_awid = dmac_m_awid;
            m_axi_awaddr = dmac_m_awaddr;
            m_axi_awlen = dmac_m_awlen;
            m_axi_awsize = dmac_m_awsize;
            m_axi_awburst = dmac_m_awburst;
            m_axi_awvalid = dmac_m_awvalid;
            dmac_m_awready = m_axi_awready;
            m_axi_wdata = dmac_m_wdata;
            m_axi_wstrb = dmac_m_wstrb;
            m_axi_wlast = dmac_m_wlast;
            m_axi_wvalid = dmac_m_wvalid;
            dmac_m_wready = m_axi_wready;
            m_axi_bready = dmac_m_bready;
        end
        WRITE_OWNER_APLIC_M: begin
            m_axi_awid = aplic_m_m_awid;
            m_axi_awaddr = aplic_m_m_awaddr;
            m_axi_awlen = aplic_m_m_awlen;
            m_axi_awsize = aplic_m_m_awsize;
            m_axi_awburst = aplic_m_m_awburst;
            m_axi_awvalid = aplic_m_m_awvalid;
            aplic_m_m_awready = m_axi_awready;
            m_axi_wdata = aplic_m_m_wdata;
            m_axi_wstrb = aplic_m_m_wstrb;
            m_axi_wlast = aplic_m_m_wlast;
            m_axi_wvalid = aplic_m_m_wvalid;
            aplic_m_m_wready = m_axi_wready;
            m_axi_bready = aplic_m_m_bready;
        end
        WRITE_OWNER_APLIC_S: begin
            m_axi_awid = aplic_s_m_awid;
            m_axi_awaddr = aplic_s_m_awaddr;
            m_axi_awlen = aplic_s_m_awlen;
            m_axi_awsize = aplic_s_m_awsize;
            m_axi_awburst = aplic_s_m_awburst;
            m_axi_awvalid = aplic_s_m_awvalid;
            aplic_s_m_awready = m_axi_awready;
            m_axi_wdata = aplic_s_m_wdata;
            m_axi_wstrb = aplic_s_m_wstrb;
            m_axi_wlast = aplic_s_m_wlast;
            m_axi_wvalid = aplic_s_m_wvalid;
            aplic_s_m_wready = m_axi_wready;
            m_axi_bready = aplic_s_m_bready;
        end
        default: begin
        end
        endcase
    end

    always_ff @(posedge clk or negedge rstn) begin
        if (!rstn) begin
            write_owner_q <= WRITE_OWNER_NONE;
        end else if (write_owner_q == WRITE_OWNER_NONE) begin
            write_owner_q <= write_owner_sel;
        end else if (m_axi_bvalid && m_axi_bready) begin
            write_owner_q <= WRITE_OWNER_NONE;
        end
    end
`endif
endmodule
