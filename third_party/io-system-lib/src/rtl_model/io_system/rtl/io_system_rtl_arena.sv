// SPDX-License-Identifier: Apache-2.0
`ifdef IO_SYSTEM_RTL_ARENA
module io_system_rtl_arena (
    input logic clk,
    input logic rstn,
    input io_system_rtl_arena_pkg::a64i16_req_t config_req_i,
    output io_system_rtl_arena_pkg::a64i16_resp_t config_resp_o,
    output io_system_rtl_arena_pkg::a64i16_req_t legacy_req_o,
    input io_system_rtl_arena_pkg::a64i16_resp_t legacy_resp_i,
    output io_system_rtl_arena_pkg::a64i8_req_t ddr_req_o,
    input io_system_rtl_arena_pkg::a64i8_resp_t ddr_resp_i
);
    import io_system_rtl_arena_pkg::*;
    a64i16_req_t [1:0] config_branch_req;
    a64i16_resp_t [1:0] config_branch_resp;
    a64i16_req_t checked_config_req;
    a64i16_resp_t checked_config_resp;
    a64i4_req_t remapped_config_req;
    a64i4_resp_t remapped_config_resp;
    a512i4_req_t arena_config_req;
    a512i4_resp_t arena_config_resp;
    a512i8_req_t arena_ddr_req, checked_ddr_req;
    a512i8_resp_t arena_ddr_resp, checked_ddr_resp;

    // Complete AXI routing: a stalled RA response never acknowledges MMIO early.
    axi_demux #(
        .AxiIdWidth(16), .aw_chan_t(a64i16_aw_chan_t), .w_chan_t(a64i16_w_chan_t),
        .b_chan_t(a64i16_b_chan_t), .ar_chan_t(a64i16_ar_chan_t), .r_chan_t(a64i16_r_chan_t),
        .req_t(a64i16_req_t), .resp_t(a64i16_resp_t), .NoMstPorts(2),
        .MaxTrans(4), .AxiLookBits(4)
    ) u_config_route (
        .clk_i(clk), .rst_ni(rstn), .test_i(1'b0),
        .slv_req_i(config_req_i), .slv_resp_o(config_resp_o),
        .slv_aw_select_i(config_req_i.aw.addr[63:16] == 48'h5000),
        .slv_ar_select_i(config_req_i.ar.addr[63:16] == 48'h5000),
        .mst_reqs_o(config_branch_req), .mst_resps_i(config_branch_resp)
    );
    assign legacy_req_o = config_branch_req[0];
    assign config_branch_resp[0] = legacy_resp_i;

    io_system_rtl_axi_guard #(
        .ID_WIDTH(16), .MAX_SIZE(3), .MAX_TRANS(4),
        .CHECK_WINDOW(1'b1), .BASE(64'h50000000), .SIZE(64'h10000),
        .aw_t(a64i16_aw_chan_t), .w_t(a64i16_w_chan_t), .b_t(a64i16_b_chan_t),
        .ar_t(a64i16_ar_chan_t), .r_t(a64i16_r_chan_t),
        .req_t(a64i16_req_t), .resp_t(a64i16_resp_t)
    ) u_config_guard (
        .clk(clk), .rstn(rstn), .req_i(config_branch_req[1]),
        .resp_o(config_branch_resp[1]), .req_o(checked_config_req),
        .resp_i(checked_config_resp)
    );

    axi_id_remap #(
        .AxiSlvPortIdWidth(16), .AxiSlvPortMaxUniqIds(4),
        .AxiMaxTxnsPerId(1), .AxiMstPortIdWidth(4),
        .slv_req_t(a64i16_req_t), .slv_resp_t(a64i16_resp_t),
        .mst_req_t(a64i4_req_t), .mst_resp_t(a64i4_resp_t)
    ) u_config_ids (
        .clk_i(clk), .rst_ni(rstn), .slv_req_i(checked_config_req),
        .slv_resp_o(checked_config_resp), .mst_req_o(remapped_config_req),
        .mst_resp_i(remapped_config_resp)
    );

    axi_dw_converter #(
        .AxiMaxReads(4), .AxiSlvPortDataWidth(64), .AxiMstPortDataWidth(512),
        .AxiAddrWidth(64), .AxiIdWidth(4),
        .aw_chan_t(a64i4_aw_chan_t), .ar_chan_t(a64i4_ar_chan_t), .b_chan_t(a64i4_b_chan_t),
        .slv_w_chan_t(a64i4_w_chan_t), .mst_w_chan_t(a512i4_w_chan_t),
        .slv_r_chan_t(a64i4_r_chan_t), .mst_r_chan_t(a512i4_r_chan_t),
        .axi_slv_req_t(a64i4_req_t), .axi_slv_resp_t(a64i4_resp_t),
        .axi_mst_req_t(a512i4_req_t), .axi_mst_resp_t(a512i4_resp_t)
    ) u_config_width (
        .clk_i(clk), .rst_ni(rstn), .slv_req_i(remapped_config_req), .slv_resp_o(remapped_config_resp),
        .mst_req_o(arena_config_req), .mst_resp_i(arena_config_resp)
    );

    io_system_rtl_axi_guard #(
        .ID_WIDTH(8), .MAX_SIZE(6), .MAX_TRANS(16),
        .ALLOW_ALIGNED_READ_WRAP(1'b1),
        .aw_t(a512i8_aw_chan_t), .w_t(a512i8_w_chan_t), .b_t(a512i8_b_chan_t),
        .ar_t(a512i8_ar_chan_t), .r_t(a512i8_r_chan_t),
        .req_t(a512i8_req_t), .resp_t(a512i8_resp_t)
    ) u_ddr_guard (
        .clk(clk), .rstn(rstn), .req_i(arena_ddr_req), .resp_o(arena_ddr_resp),
        .req_o(checked_ddr_req), .resp_i(checked_ddr_resp)
    );
    a64i8_req_t narrow_ddr_req;
    a64i8_resp_t narrow_ddr_resp;
    assign ddr_req_o = narrow_ddr_req;
    assign narrow_ddr_resp = ddr_resp_i;

    axi_dw_converter #(
        // E6 AXIM now matches read lane metadata by RID, so distinct read
        // contexts may complete out of order through the shared DDR port.
        .AxiMaxReads(16), .AxiSlvPortDataWidth(512), .AxiMstPortDataWidth(64),
        .AxiAddrWidth(64), .AxiIdWidth(8),
        .aw_chan_t(a512i8_aw_chan_t), .ar_chan_t(a512i8_ar_chan_t), .b_chan_t(a512i8_b_chan_t),
        .slv_w_chan_t(a512i8_w_chan_t), .mst_w_chan_t(a64i8_w_chan_t),
        .slv_r_chan_t(a512i8_r_chan_t), .mst_r_chan_t(a64i8_r_chan_t),
        .axi_slv_req_t(a512i8_req_t), .axi_slv_resp_t(a512i8_resp_t),
        .axi_mst_req_t(a64i8_req_t), .axi_mst_resp_t(a64i8_resp_t)
    ) u_ddr_width (
        .clk_i(clk), .rst_ni(rstn), .slv_req_i(checked_ddr_req), .slv_resp_o(checked_ddr_resp),
        .mst_req_o(narrow_ddr_req), .mst_resp_i(narrow_ddr_resp)
    );
    assign arena_ddr_req.aw.qos = '0;
    assign arena_ddr_req.aw.region = '0;
    assign arena_ddr_req.aw.user = '0;
    assign arena_ddr_req.aw.atop = '0;
    assign arena_ddr_req.ar.qos = '0;
    assign arena_ddr_req.ar.region = '0;
    assign arena_ddr_req.ar.user = '0;
    assign arena_ddr_req.w.user = 1'b0;
    assign arena_config_resp.b.user = 1'b0;
    assign arena_config_resp.r.user = 1'b0;

    tri1 io_sdio_cmd;
    tri1 io_sdio_data0;
    tri1 io_sdio_data1;
    tri1 io_sdio_data2;
    tri1 io_sdio_data3;
    tri1 io_sdio_data4;
    tri1 io_sdio_data5;
    tri1 io_sdio_data6;
    tri1 io_sdio_data7;
    tri1 io_iis_sd;
    tri1 io_iis_ws;
    tri1 io_iis_ck;
    tri1 io_iis_mck;
    tri1 io_spi_clk;
    tri1 io_spi_miso;
    tri1 io_spi_mosi;
    tri1 io_spi_cs_n;
    tri1 [15:0] io_gpio;
    tri1 io_iic_scl;
    tri1 io_iic_sda;

    arena_soc #(.COSIM(1'b1)) u_arena (
        .sys_clk(clk),
        .cpu_reset(!rstn),
        .config_araddr(arena_config_req.ar.addr),
        .config_arburst(arena_config_req.ar.burst),
        .config_arcache(arena_config_req.ar.cache),
        .config_arid(arena_config_req.ar.id),
        .config_arlen(arena_config_req.ar.len),
        .config_arlock(arena_config_req.ar.lock),
        .config_arprot(arena_config_req.ar.prot),
        .config_arready(arena_config_resp.ar_ready),
        .config_arsize(arena_config_req.ar.size),
        .config_arvalid(arena_config_req.ar_valid),
        .config_awaddr(arena_config_req.aw.addr),
        .config_awburst(arena_config_req.aw.burst),
        .config_awcache(arena_config_req.aw.cache),
        .config_awid(arena_config_req.aw.id),
        .config_awlen(arena_config_req.aw.len),
        .config_awlock(arena_config_req.aw.lock),
        .config_awprot(arena_config_req.aw.prot),
        .config_awready(arena_config_resp.aw_ready),
        .config_awsize(arena_config_req.aw.size),
        .config_awvalid(arena_config_req.aw_valid),
        .config_bid(arena_config_resp.b.id),
        .config_bready(arena_config_req.b_ready),
        .config_bresp(arena_config_resp.b.resp),
        .config_bvalid(arena_config_resp.b_valid),
        .config_rdata(arena_config_resp.r.data),
        .config_rid(arena_config_resp.r.id),
        .config_rlast(arena_config_resp.r.last),
        .config_rready(arena_config_req.r_ready),
        .config_rresp(arena_config_resp.r.resp),
        .config_rvalid(arena_config_resp.r_valid),
        .config_wdata(arena_config_req.w.data),
        .config_wlast(arena_config_req.w.last),
        .config_wready(arena_config_resp.w_ready),
        .config_wstrb(arena_config_req.w.strb),
        .config_wvalid(arena_config_req.w_valid),
        .axi_ddr_araddr(arena_ddr_req.ar.addr),
        .axi_ddr_arburst(arena_ddr_req.ar.burst),
        .axi_ddr_arcache(arena_ddr_req.ar.cache),
        .axi_ddr_arid(arena_ddr_req.ar.id),
        .axi_ddr_arlen(arena_ddr_req.ar.len),
        .axi_ddr_arlock(arena_ddr_req.ar.lock),
        .axi_ddr_arprot(arena_ddr_req.ar.prot),
        .axi_ddr_arready(arena_ddr_resp.ar_ready),
        .axi_ddr_arsize(arena_ddr_req.ar.size),
        .axi_ddr_arvalid(arena_ddr_req.ar_valid),
        .axi_ddr_awaddr(arena_ddr_req.aw.addr),
        .axi_ddr_awburst(arena_ddr_req.aw.burst),
        .axi_ddr_awcache(arena_ddr_req.aw.cache),
        .axi_ddr_awid(arena_ddr_req.aw.id),
        .axi_ddr_awlen(arena_ddr_req.aw.len),
        .axi_ddr_awlock(arena_ddr_req.aw.lock),
        .axi_ddr_awprot(arena_ddr_req.aw.prot),
        .axi_ddr_awready(arena_ddr_resp.aw_ready),
        .axi_ddr_awsize(arena_ddr_req.aw.size),
        .axi_ddr_awvalid(arena_ddr_req.aw_valid),
        .axi_ddr_bid(arena_ddr_resp.b.id),
        .axi_ddr_bready(arena_ddr_req.b_ready),
        .axi_ddr_bresp(arena_ddr_resp.b.resp),
        .axi_ddr_bvalid(arena_ddr_resp.b_valid),
        .axi_ddr_rdata(arena_ddr_resp.r.data),
        .axi_ddr_rid(arena_ddr_resp.r.id),
        .axi_ddr_rlast(arena_ddr_resp.r.last),
        .axi_ddr_rready(arena_ddr_req.r_ready),
        .axi_ddr_rresp(arena_ddr_resp.r.resp),
        .axi_ddr_rvalid(arena_ddr_resp.r_valid),
        .axi_ddr_wdata(arena_ddr_req.w.data),
        .axi_ddr_wlast(arena_ddr_req.w.last),
        .axi_ddr_wready(arena_ddr_resp.w_ready),
        .axi_ddr_wstrb(arena_ddr_req.w.strb),
        .axi_ddr_wvalid(arena_ddr_req.w_valid),
        .i_rtc(1'b0),
        .i_jtag_tck(1'b0),
        .i_jtag_tms(1'b1),
        .i_jtag_tdi(1'b0),
        .i_jtag_trst_n(1'b0),
        .o_jtag_tdo(),
        .i_rx(1'b1),
        .o_tx(),
        .i_uart_rx(1'b1),
        .o_uart_tx(),
        .o_sdio_ck(),
        .io_sdio_cmd(io_sdio_cmd),
        .io_sdio_data0(io_sdio_data0),
        .io_sdio_data1(io_sdio_data1),
        .io_sdio_data2(io_sdio_data2),
        .io_sdio_data3(io_sdio_data3),
        .io_sdio_data4(io_sdio_data4),
        .io_sdio_data5(io_sdio_data5),
        .io_sdio_data6(io_sdio_data6),
        .io_sdio_data7(io_sdio_data7),
        .io_iis_sd(io_iis_sd),
        .io_iis_ws(io_iis_ws),
        .io_iis_ck(io_iis_ck),
        .io_iis_mck(io_iis_mck),
        .io_spi_clk(io_spi_clk),
        .io_spi_miso(io_spi_miso),
        .io_spi_mosi(io_spi_mosi),
        .io_spi_cs_n(io_spi_cs_n),
        .io_gpio(io_gpio),
        .io_iic_scl(io_iic_scl),
        .io_iic_sda(io_iic_sda)
    );
endmodule
`endif
