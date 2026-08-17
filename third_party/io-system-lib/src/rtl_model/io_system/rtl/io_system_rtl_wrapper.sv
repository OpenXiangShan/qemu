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
        .s_axi_awid(s_axi_awid),
        .s_axi_awaddr(s_axi_awaddr),
        .s_axi_awlen(s_axi_awlen),
        .s_axi_awsize(s_axi_awsize),
        .s_axi_awburst(s_axi_awburst),
        .s_axi_awvalid(s_axi_awvalid),
        .s_axi_awready(s_axi_awready),
        .s_axi_wdata(s_axi_wdata),
        .s_axi_wstrb(s_axi_wstrb),
        .s_axi_wlast(s_axi_wlast),
        .s_axi_wvalid(s_axi_wvalid),
        .s_axi_wready(s_axi_wready),
        .s_axi_bid(s_axi_bid),
        .s_axi_bresp(s_axi_bresp),
        .s_axi_bvalid(s_axi_bvalid),
        .s_axi_bready(s_axi_bready),
        .s_axi_arid(s_axi_arid),
        .s_axi_araddr(s_axi_araddr),
        .s_axi_arlen(s_axi_arlen),
        .s_axi_arsize(s_axi_arsize),
        .s_axi_arburst(s_axi_arburst),
        .s_axi_arvalid(s_axi_arvalid),
        .s_axi_arready(s_axi_arready),
        .s_axi_rid(s_axi_rid),
        .s_axi_rdata(s_axi_rdata),
        .s_axi_rresp(s_axi_rresp),
        .s_axi_rlast(s_axi_rlast),
        .s_axi_rvalid(s_axi_rvalid),
        .s_axi_rready(s_axi_rready),
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
        .m_axi_bid(m_axi_bid),
        .m_axi_bresp(m_axi_bresp),
        .m_axi_bvalid((write_owner_q == WRITE_OWNER_APLIC_M) &&
                     m_axi_bvalid),
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
        .m_axi_bid(m_axi_bid),
        .m_axi_bresp(m_axi_bresp),
        .m_axi_bvalid((write_owner_q == WRITE_OWNER_APLIC_S) &&
                     m_axi_bvalid),
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
        .m_axi_bid(m_axi_bid),
        .m_axi_bresp(m_axi_bresp),
        .m_axi_bvalid((write_owner_q == WRITE_OWNER_DMAC) && m_axi_bvalid),
        .m_axi_bready(dmac_m_bready),
        .m_axi_arid(m_axi_arid),
        .m_axi_araddr(m_axi_araddr),
        .m_axi_arlen(m_axi_arlen),
        .m_axi_arsize(m_axi_arsize),
        .m_axi_arburst(m_axi_arburst),
        .m_axi_arvalid(m_axi_arvalid),
        .m_axi_arready(m_axi_arready),
        .m_axi_rid(m_axi_rid),
        .m_axi_rdata(m_axi_rdata),
        .m_axi_rresp(m_axi_rresp),
        .m_axi_rlast(m_axi_rlast),
        .m_axi_rvalid(m_axi_rvalid),
        .m_axi_rready(m_axi_rready)
    );

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
endmodule
