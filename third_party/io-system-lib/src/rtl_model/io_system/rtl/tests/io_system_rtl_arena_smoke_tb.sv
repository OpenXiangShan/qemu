// SPDX-License-Identifier: Apache-2.0
// Actual SoC config/slave smoke; standalone testbench, not part of picker RTL.
module io_system_rtl_arena_smoke_tb;
    localparam DATA_WIDTH = 64;
    localparam STRB_WIDTH = 8;
    logic  clk;
    logic  rstn;
    logic [15:0] s_axi_awid;
    logic [63:0] s_axi_awaddr;
    logic [7:0] s_axi_awlen;
    logic [2:0] s_axi_awsize;
    logic [1:0] s_axi_awburst;
    logic  s_axi_awvalid;
    logic  s_axi_awready;
    logic [DATA_WIDTH-1:0] s_axi_wdata;
    logic [STRB_WIDTH-1:0] s_axi_wstrb;
    logic  s_axi_wlast;
    logic  s_axi_wvalid;
    logic  s_axi_wready;
    logic [15:0] s_axi_bid;
    logic [1:0] s_axi_bresp;
    logic  s_axi_bvalid;
    logic  s_axi_bready;
    logic [15:0] s_axi_arid;
    logic [63:0] s_axi_araddr;
    logic [7:0] s_axi_arlen;
    logic [2:0] s_axi_arsize;
    logic [1:0] s_axi_arburst;
    logic  s_axi_arvalid;
    logic  s_axi_arready;
    logic [15:0] s_axi_rid;
    logic [DATA_WIDTH-1:0] s_axi_rdata;
    logic [1:0] s_axi_rresp;
    logic  s_axi_rlast;
    logic  s_axi_rvalid;
    logic  s_axi_rready;
    logic [15:0] gbus_axi_awid;
    logic [31:0] gbus_axi_awaddr;
    logic [7:0] gbus_axi_awlen;
    logic [2:0] gbus_axi_awsize;
    logic [1:0] gbus_axi_awburst;
    logic  gbus_axi_awvalid;
    logic  gbus_axi_awready;
    logic [31:0] gbus_axi_wdata;
    logic [3:0] gbus_axi_wstrb;
    logic  gbus_axi_wlast;
    logic  gbus_axi_wvalid;
    logic  gbus_axi_wready;
    logic [15:0] gbus_axi_bid;
    logic [1:0] gbus_axi_bresp;
    logic  gbus_axi_bvalid;
    logic  gbus_axi_bready;
    logic [15:0] gbus_axi_arid;
    logic [31:0] gbus_axi_araddr;
    logic [7:0] gbus_axi_arlen;
    logic [2:0] gbus_axi_arsize;
    logic [1:0] gbus_axi_arburst;
    logic  gbus_axi_arvalid;
    logic  gbus_axi_arready;
    logic [15:0] gbus_axi_rid;
    logic [31:0] gbus_axi_rdata;
    logic [1:0] gbus_axi_rresp;
    logic  gbus_axi_rlast;
    logic  gbus_axi_rvalid;
    logic  gbus_axi_rready;
    logic [15:0] m_axi_awid;
    logic [63:0] m_axi_awaddr;
    logic [7:0] m_axi_awlen;
    logic [2:0] m_axi_awsize;
    logic [1:0] m_axi_awburst;
    logic  m_axi_awvalid;
    logic  m_axi_awready;
    logic [DATA_WIDTH-1:0] m_axi_wdata;
    logic [STRB_WIDTH-1:0] m_axi_wstrb;
    logic  m_axi_wlast;
    logic  m_axi_wvalid;
    logic  m_axi_wready;
    logic [15:0] m_axi_bid;
    logic [1:0] m_axi_bresp;
    logic  m_axi_bvalid;
    logic  m_axi_bready;
    logic [15:0] m_axi_arid;
    logic [63:0] m_axi_araddr;
    logic [7:0] m_axi_arlen;
    logic [2:0] m_axi_arsize;
    logic [1:0] m_axi_arburst;
    logic  m_axi_arvalid;
    logic  m_axi_arready;
    logic [15:0] m_axi_rid;
    logic [DATA_WIDTH-1:0] m_axi_rdata;
    logic [1:0] m_axi_rresp;
    logic  m_axi_rlast;
    logic  m_axi_rvalid;
    logic  m_axi_rready;
    io_system_rtl_wrapper dut (
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
        .gbus_axi_awid(gbus_axi_awid),
        .gbus_axi_awaddr(gbus_axi_awaddr),
        .gbus_axi_awlen(gbus_axi_awlen),
        .gbus_axi_awsize(gbus_axi_awsize),
        .gbus_axi_awburst(gbus_axi_awburst),
        .gbus_axi_awvalid(gbus_axi_awvalid),
        .gbus_axi_awready(gbus_axi_awready),
        .gbus_axi_wdata(gbus_axi_wdata),
        .gbus_axi_wstrb(gbus_axi_wstrb),
        .gbus_axi_wlast(gbus_axi_wlast),
        .gbus_axi_wvalid(gbus_axi_wvalid),
        .gbus_axi_wready(gbus_axi_wready),
        .gbus_axi_bid(gbus_axi_bid),
        .gbus_axi_bresp(gbus_axi_bresp),
        .gbus_axi_bvalid(gbus_axi_bvalid),
        .gbus_axi_bready(gbus_axi_bready),
        .gbus_axi_arid(gbus_axi_arid),
        .gbus_axi_araddr(gbus_axi_araddr),
        .gbus_axi_arlen(gbus_axi_arlen),
        .gbus_axi_arsize(gbus_axi_arsize),
        .gbus_axi_arburst(gbus_axi_arburst),
        .gbus_axi_arvalid(gbus_axi_arvalid),
        .gbus_axi_arready(gbus_axi_arready),
        .gbus_axi_rid(gbus_axi_rid),
        .gbus_axi_rdata(gbus_axi_rdata),
        .gbus_axi_rresp(gbus_axi_rresp),
        .gbus_axi_rlast(gbus_axi_rlast),
        .gbus_axi_rvalid(gbus_axi_rvalid),
        .gbus_axi_rready(gbus_axi_rready),
        .m_axi_awid(m_axi_awid),
        .m_axi_awaddr(m_axi_awaddr),
        .m_axi_awlen(m_axi_awlen),
        .m_axi_awsize(m_axi_awsize),
        .m_axi_awburst(m_axi_awburst),
        .m_axi_awvalid(m_axi_awvalid),
        .m_axi_awready(m_axi_awready),
        .m_axi_wdata(m_axi_wdata),
        .m_axi_wstrb(m_axi_wstrb),
        .m_axi_wlast(m_axi_wlast),
        .m_axi_wvalid(m_axi_wvalid),
        .m_axi_wready(m_axi_wready),
        .m_axi_bid(m_axi_bid),
        .m_axi_bresp(m_axi_bresp),
        .m_axi_bvalid(m_axi_bvalid),
        .m_axi_bready(m_axi_bready),
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

    always #5 clk = !clk;
    int watchdog;
    always @(posedge clk) begin
        watchdog <= watchdog + 1;
        if (watchdog > 100000) $fatal(1, "RTL smoke timeout");
        if (rstn && (m_axi_awvalid || m_axi_arvalid))
            $fatal(1, "DDR request while E6 is held reset");
    end
    task automatic write32(input logic [63:0] addr, input logic [31:0] data,
                           input logic [15:0] id = 16'hba98,
                           input logic [1:0] burst = 2'b01,
                           input logic [1:0] expected = 2'b00);
        @(negedge clk);
        s_axi_awaddr = addr; s_axi_awid = id; s_axi_awlen = 0;
        s_axi_awsize = 2; s_axi_awburst = burst; s_axi_awvalid = 1;
        s_axi_wdata = {32'b0, data} << (8 * addr[2:0]);
        s_axi_wstrb = 8'h0f << addr[2:0]; s_axi_wlast = 1; s_axi_wvalid = 1;
        fork
            begin
                do @(posedge clk); while (!s_axi_awready);
                @(negedge clk); s_axi_awvalid = 0;
            end
            begin
                do @(posedge clk); while (!s_axi_wready);
                @(negedge clk); s_axi_wvalid = 0;
            end
        join
        do @(posedge clk); while (!s_axi_bvalid);
        if (s_axi_bid !== id || s_axi_bresp !== expected)
            $fatal(1, "write addr=%h id=%h resp=%h expected=%h", addr, s_axi_bid, s_axi_bresp, expected);
        @(negedge clk); s_axi_bready = 1;
        @(posedge clk); @(negedge clk); s_axi_bready = 0;
    endtask
    task automatic read32(input logic [63:0] addr, output logic [31:0] data,
                          input logic [15:0] id = 16'hfedc,
                          input logic [1:0] burst = 2'b01,
                          input logic [1:0] expected = 2'b00,
                          input logic [7:0] len = 0);
        @(negedge clk);
        s_axi_araddr = addr; s_axi_arid = id; s_axi_arlen = len;
        s_axi_arsize = 2; s_axi_arburst = burst; s_axi_arvalid = 1;
        do @(posedge clk); while (!s_axi_arready);
        @(negedge clk); s_axi_arvalid = 0;
        for (int beat = 0; beat <= len; beat++) begin
            do @(posedge clk); while (!s_axi_rvalid);
            if (s_axi_rid !== id || s_axi_rresp !== expected || s_axi_rlast !== (beat == len))
                $fatal(1, "read addr=%h id=%h resp=%h last=%b beat=%d", addr, s_axi_rid, s_axi_rresp, s_axi_rlast, beat);
            data = s_axi_rdata >> (8 * addr[2:0]);
            @(negedge clk); s_axi_rready = 1;
            @(posedge clk); @(negedge clk); s_axi_rready = 0;
        end
    endtask
    logic [31:0] data;
    initial begin
        clk = '0;
        rstn = '0;
        s_axi_awid = '0;
        s_axi_awaddr = '0;
        s_axi_awlen = '0;
        s_axi_awsize = '0;
        s_axi_awburst = '0;
        s_axi_awvalid = '0;
        s_axi_wdata = '0;
        s_axi_wstrb = '0;
        s_axi_wlast = '0;
        s_axi_wvalid = '0;
        s_axi_bready = '0;
        s_axi_arid = '0;
        s_axi_araddr = '0;
        s_axi_arlen = '0;
        s_axi_arsize = '0;
        s_axi_arburst = '0;
        s_axi_arvalid = '0;
        s_axi_rready = '0;
        gbus_axi_awid = '0;
        gbus_axi_awaddr = '0;
        gbus_axi_awlen = '0;
        gbus_axi_awsize = '0;
        gbus_axi_awburst = '0;
        gbus_axi_awvalid = '0;
        gbus_axi_wdata = '0;
        gbus_axi_wstrb = '0;
        gbus_axi_wlast = '0;
        gbus_axi_wvalid = '0;
        gbus_axi_bready = '0;
        gbus_axi_arid = '0;
        gbus_axi_araddr = '0;
        gbus_axi_arlen = '0;
        gbus_axi_arsize = '0;
        gbus_axi_arburst = '0;
        gbus_axi_arvalid = '0;
        gbus_axi_rready = '0;
        m_axi_awready = '0;
        m_axi_wready = '0;
        m_axi_bid = '0;
        m_axi_bresp = '0;
        m_axi_bvalid = '0;
        m_axi_arready = '0;
        m_axi_rid = '0;
        m_axi_rdata = '0;
        m_axi_rresp = '0;
        m_axi_rlast = '0;
        m_axi_rvalid = '0;
        watchdog = 0;
        repeat (16) @(negedge clk);
        rstn = 1;
        repeat (2048) @(negedge clk);
        read32(64'h50000060, data);
        if (data[0] !== 0) $fatal(1, "E6 not held reset: %h", data);
        write32(64'h50000100, 32'h80010000);
        read32(64'h50000100, data);
        if (data !== 32'h80010000) $fatal(1, "SYSCTRL boot mismatch %h", data);
        write32(64'h50000120, 32'h01234567, 16'h007f);
        read32(64'h50000120, data, 16'hff7f);
        if (data !== 32'h01234567) $fatal(1, "SYSCTRL lane 32 mismatch %h", data);
        write32(64'h50000140, 32'h89abcdef, 16'h1234);
        read32(64'h50000140, data);
        if (data !== 32'h89abcdef) $fatal(1, "SYSCTRL lane 0 mismatch %h", data);
        write32(64'h50000160, 32'h13572468, 16'h3141);
        read32(64'h50000160, data);
        if (data !== 32'h13572468) $fatal(1, "SYSCTRL lane 32 mismatch %h", data);
        // Unsupported FIXED and WRAP requests must complete without side effects.
        write32(64'h50000100, 32'hdeadbeef, 16'hffff, 2'b00, 2'b10);
        read32(64'h50000100, data, 16'hffff, 2'b10, 2'b10, 3);
        read32(64'h50000ffc, data, 16'haaaa, 2'b01, 2'b10, 1);
        read32(64'h50000100, data);
        if (data !== 32'h80010000) $fatal(1, "rejected request changed boot register");
        // A legacy unmapped address must still be a DECERR.
        read32(64'h300f0000, data, 16'h2222, 2'b01, 2'b11);
        $display("ARENA_RTL_CONFIG_SMOKE_PASS");
        $finish;
    end
endmodule
