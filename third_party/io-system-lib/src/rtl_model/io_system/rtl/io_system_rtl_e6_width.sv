// SPDX-License-Identifier: Apache-2.0
// E6 AXIM adapter for co-simulation. Keep the original channel buffers, but
// replace the legacy global lane/beat FIFO with per-ID PULP read contexts.
`ifdef IO_SYSTEM_RTL_ARENA
`include "axi/typedef.svh"
module io_system_rtl_e6_width #(
    parameter int unsigned IDW = 8,
    parameter bit STD = 1'b1,
    parameter int unsigned LVL = 4,
    parameter int unsigned OSL = 8,
    parameter int unsigned DTW = 64,
    parameter int unsigned STW = 8,
    parameter int unsigned DOW = 512,
    parameter int unsigned SOW = 64,
    parameter int unsigned MaxReads = 16
) (
    input  logic             aresetn,
    input  logic             aclk,
    input  logic [IDW-1:0]   slv_axi_awid,
    input  logic [ 63:0]     slv_axi_awaddr,
    input  logic [  3:0]     slv_axi_awregion,
    input  logic [  7:0]     slv_axi_awlen,
    input  logic [  2:0]     slv_axi_awsize,
    input  logic [  1:0]     slv_axi_awburst,
    input  logic             slv_axi_awlock,
    input  logic [  3:0]     slv_axi_awcache,
    input  logic [  2:0]     slv_axi_awprot,
    input  logic [  3:0]     slv_axi_awqos,
    input  logic             slv_axi_awvalid,
    output logic             slv_axi_awready,
    input  logic [DTW-1:0]   slv_axi_wdata,
    input  logic [STW-1:0]   slv_axi_wstrb,
    input  logic             slv_axi_wlast,
    input  logic             slv_axi_wvalid,
    output logic             slv_axi_wready,
    output logic [IDW-1:0]   slv_axi_bid,
    output logic [  1:0]     slv_axi_bresp,
    output logic             slv_axi_bvalid,
    input  logic             slv_axi_bready,
    input  logic [IDW-1:0]   slv_axi_arid,
    input  logic [ 63:0]     slv_axi_araddr,
    input  logic [  3:0]     slv_axi_arregion,
    input  logic [  7:0]     slv_axi_arlen,
    input  logic [  2:0]     slv_axi_arsize,
    input  logic [  1:0]     slv_axi_arburst,
    input  logic             slv_axi_arlock,
    input  logic [  3:0]     slv_axi_arcache,
    input  logic [  2:0]     slv_axi_arprot,
    input  logic [  3:0]     slv_axi_arqos,
    input  logic             slv_axi_arvalid,
    output logic             slv_axi_arready,
    output logic [IDW-1:0]   slv_axi_rid,
    output logic [DTW-1:0]   slv_axi_rdata,
    output logic [  1:0]     slv_axi_rresp,
    output logic             slv_axi_rlast,
    output logic             slv_axi_rvalid,
    input  logic             slv_axi_rready,
    output logic [IDW-1:0]   mst_axi_awid,
    output logic [ 63:0]     mst_axi_awaddr,
    output logic [  3:0]     mst_axi_awregion,
    output logic [  7:0]     mst_axi_awlen,
    output logic [  2:0]     mst_axi_awsize,
    output logic [  1:0]     mst_axi_awburst,
    output logic             mst_axi_awlock,
    output logic [  3:0]     mst_axi_awcache,
    output logic [  2:0]     mst_axi_awprot,
    output logic [  3:0]     mst_axi_awqos,
    output logic             mst_axi_awvalid,
    input  logic             mst_axi_awready,
    output logic [DOW-1:0]   mst_axi_wdata,
    output logic [SOW-1:0]   mst_axi_wstrb,
    output logic             mst_axi_wlast,
    output logic             mst_axi_wvalid,
    input  logic             mst_axi_wready,
    input  logic [IDW-1:0]   mst_axi_bid,
    input  logic [  1:0]     mst_axi_bresp,
    input  logic             mst_axi_bvalid,
    output logic             mst_axi_bready,
    output logic [IDW-1:0]   mst_axi_arid,
    output logic [ 63:0]     mst_axi_araddr,
    output logic [  3:0]     mst_axi_arregion,
    output logic [  7:0]     mst_axi_arlen,
    output logic [  2:0]     mst_axi_arsize,
    output logic [  1:0]     mst_axi_arburst,
    output logic             mst_axi_arlock,
    output logic [  3:0]     mst_axi_arcache,
    output logic [  2:0]     mst_axi_arprot,
    output logic [  3:0]     mst_axi_arqos,
    output logic             mst_axi_arvalid,
    input  logic             mst_axi_arready,
    input  logic [IDW-1:0]   mst_axi_rid,
    input  logic [DOW-1:0]   mst_axi_rdata,
    input  logic [  1:0]     mst_axi_rresp,
    input  logic             mst_axi_rlast,
    input  logic             mst_axi_rvalid,
    output logic             mst_axi_rready
);
    typedef logic [63:0] addr_t;
    typedef logic [IDW-1:0] id_t;
    typedef logic [DTW-1:0] narrow_data_t;
    typedef logic [STW-1:0] narrow_strb_t;
    typedef logic [DOW-1:0] wide_data_t;
    typedef logic [SOW-1:0] wide_strb_t;
    typedef logic user_t;
    `AXI_TYPEDEF_ALL(narrow, addr_t, id_t, narrow_data_t, narrow_strb_t, user_t)
    `AXI_TYPEDEF_ALL(wide, addr_t, id_t, wide_data_t, wide_strb_t, user_t)

    narrow_req_t narrow_req, checked_req;
    narrow_resp_t narrow_resp, checked_resp;
    wide_req_t wide_req;
    wide_resp_t wide_resp;

    // No ATOP/USER signals exist on the E6 AXIM port.
    assign narrow_req.aw.atop = '0;
    assign narrow_req.aw.user = '0;
    assign narrow_req.ar.user = '0;
    assign narrow_req.w.user = '0;
    assign wide_resp.b.user = '0;
    assign wide_resp.r.user = '0;

    // The narrow input buffer absorbs independent AW/W timing from the core.
    // All R metadata, including RID, travels together through each buffer.
    AXIBRF_64T512 #(.IDW(IDW), .DTW(DTW), .LVL(LVL)) u_core_buffer (
        .aresetn(aresetn),
        .aclk(aclk),
        .slv_axi_awid(slv_axi_awid),
        .slv_axi_awaddr(slv_axi_awaddr),
        .slv_axi_awregion(slv_axi_awregion),
        .slv_axi_awlen(slv_axi_awlen),
        .slv_axi_awsize(slv_axi_awsize),
        .slv_axi_awburst(slv_axi_awburst),
        .slv_axi_awlock(slv_axi_awlock),
        .slv_axi_awcache(slv_axi_awcache),
        .slv_axi_awprot(slv_axi_awprot),
        .slv_axi_awqos(slv_axi_awqos),
        .slv_axi_awvalid(slv_axi_awvalid),
        .slv_axi_awready(slv_axi_awready),
        .slv_axi_wdata(slv_axi_wdata),
        .slv_axi_wstrb(slv_axi_wstrb),
        .slv_axi_wlast(slv_axi_wlast),
        .slv_axi_wvalid(slv_axi_wvalid),
        .slv_axi_wready(slv_axi_wready),
        .slv_axi_bid(slv_axi_bid),
        .slv_axi_bresp(slv_axi_bresp),
        .slv_axi_bvalid(slv_axi_bvalid),
        .slv_axi_bready(slv_axi_bready),
        .slv_axi_arid(slv_axi_arid),
        .slv_axi_araddr(slv_axi_araddr),
        .slv_axi_arregion(slv_axi_arregion),
        .slv_axi_arlen(slv_axi_arlen),
        .slv_axi_arsize(slv_axi_arsize),
        .slv_axi_arburst(slv_axi_arburst),
        .slv_axi_arlock(slv_axi_arlock),
        .slv_axi_arcache(slv_axi_arcache),
        .slv_axi_arprot(slv_axi_arprot),
        .slv_axi_arqos(slv_axi_arqos),
        .slv_axi_arvalid(slv_axi_arvalid),
        .slv_axi_arready(slv_axi_arready),
        .slv_axi_rid(slv_axi_rid),
        .slv_axi_rdata(slv_axi_rdata),
        .slv_axi_rresp(slv_axi_rresp),
        .slv_axi_rlast(slv_axi_rlast),
        .slv_axi_rvalid(slv_axi_rvalid),
        .slv_axi_rready(slv_axi_rready),
        .mst_axi_awid(narrow_req.aw.id),
        .mst_axi_awaddr(narrow_req.aw.addr),
        .mst_axi_awregion(narrow_req.aw.region),
        .mst_axi_awlen(narrow_req.aw.len),
        .mst_axi_awsize(narrow_req.aw.size),
        .mst_axi_awburst(narrow_req.aw.burst),
        .mst_axi_awlock(narrow_req.aw.lock),
        .mst_axi_awcache(narrow_req.aw.cache),
        .mst_axi_awprot(narrow_req.aw.prot),
        .mst_axi_awqos(narrow_req.aw.qos),
        .mst_axi_awvalid(narrow_req.aw_valid),
        .mst_axi_awready(narrow_resp.aw_ready),
        .mst_axi_wdata(narrow_req.w.data),
        .mst_axi_wstrb(narrow_req.w.strb),
        .mst_axi_wlast(narrow_req.w.last),
        .mst_axi_wvalid(narrow_req.w_valid),
        .mst_axi_wready(narrow_resp.w_ready),
        .mst_axi_bid(narrow_resp.b.id),
        .mst_axi_bresp(narrow_resp.b.resp),
        .mst_axi_bvalid(narrow_resp.b_valid),
        .mst_axi_bready(narrow_req.b_ready),
        .mst_axi_arid(narrow_req.ar.id),
        .mst_axi_araddr(narrow_req.ar.addr),
        .mst_axi_arregion(narrow_req.ar.region),
        .mst_axi_arlen(narrow_req.ar.len),
        .mst_axi_arsize(narrow_req.ar.size),
        .mst_axi_arburst(narrow_req.ar.burst),
        .mst_axi_arlock(narrow_req.ar.lock),
        .mst_axi_arcache(narrow_req.ar.cache),
        .mst_axi_arprot(narrow_req.ar.prot),
        .mst_axi_arqos(narrow_req.ar.qos),
        .mst_axi_arvalid(narrow_req.ar_valid),
        .mst_axi_arready(narrow_resp.ar_ready),
        .mst_axi_rid(narrow_resp.r.id),
        .mst_axi_rdata(narrow_resp.r.data),
        .mst_axi_rresp(narrow_resp.r.resp),
        .mst_axi_rlast(narrow_resp.r.last),
        .mst_axi_rvalid(narrow_resp.r_valid),
        .mst_axi_rready(narrow_req.r_ready)
    );

    // E6 uses aligned WRAP for cache refills. Convert only the form whose
    // addresses equal INCR; reject other WRAPs, exclusives and illegal bursts
    // with complete AXI error responses. Do not restrict the SoC address map.
    io_system_rtl_axi_guard #(
        .ID_WIDTH(IDW), .MAX_SIZE(3), .MAX_TRANS(MaxReads),
        .ALLOW_ALIGNED_READ_WRAP(1'b1),
        .aw_t(narrow_aw_chan_t), .w_t(narrow_w_chan_t), .b_t(narrow_b_chan_t),
        .ar_t(narrow_ar_chan_t), .r_t(narrow_r_chan_t),
        .req_t(narrow_req_t), .resp_t(narrow_resp_t)
    ) u_guard (
        .clk(aclk), .rstn(aresetn), .req_i(narrow_req), .resp_o(narrow_resp),
        .req_o(checked_req), .resp_i(checked_resp)
    );

    // RID selects the saved address/size and independent burst position.
    // Another AR with an active ID is backpressured by the upsizer; different
    // IDs may complete in any order and may interleave their R beats.
    axi_dw_converter #(
        .AxiMaxReads(MaxReads), .AxiSlvPortDataWidth(DTW), .AxiMstPortDataWidth(DOW),
        .AxiAddrWidth(64), .AxiIdWidth(IDW),
        .aw_chan_t(narrow_aw_chan_t), .ar_chan_t(narrow_ar_chan_t),
        .b_chan_t(narrow_b_chan_t),
        .slv_w_chan_t(narrow_w_chan_t), .mst_w_chan_t(wide_w_chan_t),
        .slv_r_chan_t(narrow_r_chan_t), .mst_r_chan_t(wide_r_chan_t),
        .axi_slv_req_t(narrow_req_t), .axi_slv_resp_t(narrow_resp_t),
        .axi_mst_req_t(wide_req_t), .axi_mst_resp_t(wide_resp_t)
    ) u_width (
        .clk_i(aclk), .rst_ni(aresetn),
        .slv_req_i(checked_req), .slv_resp_o(checked_resp),
        .mst_req_o(wide_req), .mst_resp_i(wide_resp)
    );

    // Retain the original SoC-side buffering and optional outstanding limit.
    // This FIFO preserves received R order without assuming AR/R order match.
    AXIBUF_64T512 #(.IDW(IDW), .DTW(DOW), .LVL(LVL), .OSL(OSL)) u_soc_buffer (
        .aresetn(aresetn),
        .aclk(aclk),
        .slv_axi_awid(wide_req.aw.id),
        .slv_axi_awaddr(wide_req.aw.addr),
        .slv_axi_awregion(wide_req.aw.region),
        .slv_axi_awlen(wide_req.aw.len),
        .slv_axi_awsize(wide_req.aw.size),
        .slv_axi_awburst(wide_req.aw.burst),
        .slv_axi_awlock(wide_req.aw.lock),
        .slv_axi_awcache(wide_req.aw.cache),
        .slv_axi_awprot(wide_req.aw.prot),
        .slv_axi_awqos(wide_req.aw.qos),
        .slv_axi_awvalid(wide_req.aw_valid),
        .slv_axi_awready(wide_resp.aw_ready),
        .slv_axi_wdata(wide_req.w.data),
        .slv_axi_wstrb(wide_req.w.strb),
        .slv_axi_wlast(wide_req.w.last),
        .slv_axi_wvalid(wide_req.w_valid),
        .slv_axi_wready(wide_resp.w_ready),
        .slv_axi_bid(wide_resp.b.id),
        .slv_axi_bresp(wide_resp.b.resp),
        .slv_axi_bvalid(wide_resp.b_valid),
        .slv_axi_bready(wide_req.b_ready),
        .slv_axi_arid(wide_req.ar.id),
        .slv_axi_araddr(wide_req.ar.addr),
        .slv_axi_arregion(wide_req.ar.region),
        .slv_axi_arlen(wide_req.ar.len),
        .slv_axi_arsize(wide_req.ar.size),
        .slv_axi_arburst(wide_req.ar.burst),
        .slv_axi_arlock(wide_req.ar.lock),
        .slv_axi_arcache(wide_req.ar.cache),
        .slv_axi_arprot(wide_req.ar.prot),
        .slv_axi_arqos(wide_req.ar.qos),
        .slv_axi_arvalid(wide_req.ar_valid),
        .slv_axi_arready(wide_resp.ar_ready),
        .slv_axi_rid(wide_resp.r.id),
        .slv_axi_rdata(wide_resp.r.data),
        .slv_axi_rresp(wide_resp.r.resp),
        .slv_axi_rlast(wide_resp.r.last),
        .slv_axi_rvalid(wide_resp.r_valid),
        .slv_axi_rready(wide_req.r_ready),
        .mst_axi_awid(mst_axi_awid),
        .mst_axi_awaddr(mst_axi_awaddr),
        .mst_axi_awregion(mst_axi_awregion),
        .mst_axi_awlen(mst_axi_awlen),
        .mst_axi_awsize(mst_axi_awsize),
        .mst_axi_awburst(mst_axi_awburst),
        .mst_axi_awlock(mst_axi_awlock),
        .mst_axi_awcache(mst_axi_awcache),
        .mst_axi_awprot(mst_axi_awprot),
        .mst_axi_awqos(mst_axi_awqos),
        .mst_axi_awvalid(mst_axi_awvalid),
        .mst_axi_awready(mst_axi_awready),
        .mst_axi_wdata(mst_axi_wdata),
        .mst_axi_wstrb(mst_axi_wstrb),
        .mst_axi_wlast(mst_axi_wlast),
        .mst_axi_wvalid(mst_axi_wvalid),
        .mst_axi_wready(mst_axi_wready),
        .mst_axi_bid(mst_axi_bid),
        .mst_axi_bresp(mst_axi_bresp),
        .mst_axi_bvalid(mst_axi_bvalid),
        .mst_axi_bready(mst_axi_bready),
        .mst_axi_arid(mst_axi_arid),
        .mst_axi_araddr(mst_axi_araddr),
        .mst_axi_arregion(mst_axi_arregion),
        .mst_axi_arlen(mst_axi_arlen),
        .mst_axi_arsize(mst_axi_arsize),
        .mst_axi_arburst(mst_axi_arburst),
        .mst_axi_arlock(mst_axi_arlock),
        .mst_axi_arcache(mst_axi_arcache),
        .mst_axi_arprot(mst_axi_arprot),
        .mst_axi_arqos(mst_axi_arqos),
        .mst_axi_arvalid(mst_axi_arvalid),
        .mst_axi_arready(mst_axi_arready),
        .mst_axi_rid(mst_axi_rid),
        .mst_axi_rdata(mst_axi_rdata),
        .mst_axi_rresp(mst_axi_rresp),
        .mst_axi_rlast(mst_axi_rlast),
        .mst_axi_rvalid(mst_axi_rvalid),
        .mst_axi_rready(mst_axi_rready)
    );

    initial begin
        assert (STD == 1'b1 && DTW == 64 && STW == 8 && DOW == 512 && SOW == 64)
            else $fatal(1, "E6 co-simulation bridge requires standard 64-to-512 AXI");
        assert (MaxReads > 0) else $fatal(1, "E6 bridge requires a read context");
    end
endmodule
`endif
