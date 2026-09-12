// SPDX-License-Identifier: Apache-2.0
// Characterize the original E6 bridge, including its known AXI ordering limit.
`timescale 1ns/1ps
module e6_bridge_order_tb;
    logic clk = 0, rstn = 0;
    always #5 clk = ~clk;

    logic [3:0] arid = 0, wide_arid, rid, wide_rid = 0;
    logic [63:0] araddr = 0, wide_araddr, rdata;
    logic [7:0] arlen = 0, wide_arlen;
    logic [2:0] wide_arsize;
    logic [1:0] wide_arburst, rresp;
    logic arvalid = 0, arready, wide_arvalid;
    logic rvalid, rlast, wide_rvalid = 0, wide_rlast = 0, wide_rready;
    logic [511:0] wide_rdata = 0;
    logic [63:0] request_addr [16];
    integer response_beat [16];
    integer mismatches, responses;

    AXIAXO_64T512 #(.STD(1'b1), .IDW(4)) dut (
        .aclk(clk), .aresetn(rstn),
        .slv_axi_awid(4'b0), .slv_axi_awaddr(64'b0), .slv_axi_awregion(4'b0),
        .slv_axi_awlen(8'b0), .slv_axi_awsize(3'b0), .slv_axi_awburst(2'b01),
        .slv_axi_awlock(1'b0), .slv_axi_awcache(4'b0), .slv_axi_awprot(3'b0),
        .slv_axi_awqos(4'b0), .slv_axi_awvalid(1'b0),
        .slv_axi_wdata(64'b0), .slv_axi_wstrb(8'b0),
        .slv_axi_wlast(1'b0), .slv_axi_wvalid(1'b0), .slv_axi_bready(1'b1),
        .mst_axi_awready(1'b1), .mst_axi_wready(1'b1),
        .mst_axi_bid(4'b0), .mst_axi_bresp(2'b0), .mst_axi_bvalid(1'b0),
        .slv_axi_arid(arid), .slv_axi_araddr(araddr), .slv_axi_arlen(arlen),
        .slv_axi_arsize(3'd3), .slv_axi_arburst(2'b01),
        .slv_axi_arregion(4'b0), .slv_axi_arlock(1'b0),
        .slv_axi_arcache(4'b0), .slv_axi_arprot(3'b0), .slv_axi_arqos(4'b0),
        .slv_axi_arvalid(arvalid), .slv_axi_arready(arready),
        .slv_axi_rid(rid), .slv_axi_rdata(rdata), .slv_axi_rresp(rresp),
        .slv_axi_rlast(rlast), .slv_axi_rvalid(rvalid), .slv_axi_rready(1'b1),
        .mst_axi_arid(wide_arid), .mst_axi_araddr(wide_araddr),
        .mst_axi_arlen(wide_arlen), .mst_axi_arsize(wide_arsize),
        .mst_axi_arburst(wide_arburst), .mst_axi_arvalid(wide_arvalid),
        .mst_axi_arready(1'b1), .mst_axi_rid(wide_rid),
        .mst_axi_rdata(wide_rdata), .mst_axi_rresp(2'b0),
        .mst_axi_rlast(wide_rlast), .mst_axi_rvalid(wide_rvalid),
        .mst_axi_rready(wide_rready)
    );

    // Address-derived reference memory: every 64-bit lane has distinct data.
    function automatic logic [63:0] memory_word(input logic [63:0] address);
        return 64'hcafe_1234_0000_0000 ^ address;
    endfunction

    task automatic reset_case;
        @(negedge clk);
        rstn = 0;
        arvalid = 0;
        wide_rvalid = 0;
        mismatches = 0;
        responses = 0;
        for (int i = 0; i < 16; i++) response_beat[i] = 0;
        repeat (3) @(posedge clk);
        @(negedge clk);
        rstn = 1;
    endtask

    task automatic request(input logic [3:0] id,
                           input logic [63:0] address, input logic [7:0] len);
        @(negedge clk);
        arid = id;
        araddr = address;
        arlen = len;
        arvalid = 1;
        do @(posedge clk); while (!arready);
        if (!wide_arvalid || wide_arid != id || wide_araddr != address ||
            wide_arlen != len || wide_arsize != 3 || wide_arburst != 1)
            $fatal(1, "AR did not preserve the legal narrow INCR request");
        request_addr[id] = address;
        $display("AR id=%0d address=0x%016h len=%0d size=3", id, address, len);
        @(negedge clk);
        arvalid = 0;
    endtask

    task automatic respond(input logic [3:0] id, input logic last);
        logic [63:0] address, expected;
        address = request_addr[id] + 64'(response_beat[id] * 8);
        expected = memory_word(address);
        @(negedge clk);
        // Inactive AXI lanes may contain arbitrary data; use distinct poison
        // values so selecting another request's lane cannot accidentally pass.
        for (int lane = 0; lane < 8; lane++)
            wide_rdata[64*lane +: 64] = memory_word((address & ~64'h3f) + 64'(lane*8));
        wide_rid = id;
        wide_rlast = last;
        wide_rvalid = 1;
        do @(posedge clk); while (!wide_rready);
        if (!rvalid || rid != id || rresp != 0 || rlast != last)
            $fatal(1, "Unexpected handshake/RID/RRESP/RLAST failure");
        responses++;
        if (rdata != expected) mismatches++;
        $display("R id=%0d beat=%0d address=0x%016h actual=0x%016h expected=0x%016h match=%0d",
                 rid, response_beat[id], address, rdata, expected, rdata == expected);
        response_beat[id]++;
        @(negedge clk);
        wide_rvalid = 0;
    endtask

    task automatic result(input string name, input bit limitation, input int beats);
        if (responses != beats || (limitation ? mismatches == 0 : mismatches != 0))
            $fatal(1, "Unexpected result case=%s responses=%0d mismatches=%0d",
                   name, responses, mismatches);
        if (limitation)
            $display("EXPECTED_LIMITATION case=%s responses=%0d mismatches=%0d rid_preserved=1",
                     name, responses, mismatches);
        else
            $display("IN_ORDER_PASS case=%s responses=%0d mismatches=0", name, responses);
    endtask

    initial begin
        reset_case();
        request(1, 64'h80001008, 0);
        request(2, 64'h80002028, 0);
        respond(1, 1);
        respond(2, 1);
        result("single_ordered", 0, 2);

        reset_case();
        request(1, 64'h80001008, 0);
        request(2, 64'h80002028, 0);
        respond(2, 1);
        respond(1, 1);
        result("single_reversed", 1, 2);

        reset_case();
        request(1, 64'h80001008, 1);
        request(2, 64'h80002028, 1);
        respond(1, 0);
        respond(1, 1);
        respond(2, 0);
        respond(2, 1);
        result("burst_ordered", 0, 4);

        reset_case();
        request(1, 64'h80001008, 1);
        request(2, 64'h80002028, 1);
        respond(1, 0);
        respond(2, 0);
        respond(1, 1);
        respond(2, 1);
        result("burst_interleaved", 1, 4);
        $display("E6_BRIDGE_ORDER_LIMITATION_CONFIRMED");
        $finish;
    end

    initial begin
        #10000;
        $fatal(1, "E6 bridge ordering experiment timed out");
    end
endmodule
