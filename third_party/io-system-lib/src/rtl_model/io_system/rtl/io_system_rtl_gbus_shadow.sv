// SPDX-License-Identifier: Apache-2.0
//
// AXI-visible shadow of the virtio gbus CSR space. Guest-side register
// updates are published by the RTL virtio-mmio block; host-side writes are
// mirrored into the shadow and forwarded back to that block.

module io_system_rtl_gbus_shadow (
    input  logic          clk,
    input  logic          rstn,

    input  logic [31:0]   publish_addr,
    input  logic [31:0]   publish_data,
    output logic          todut_en,
    output logic [31:0]   todut_addr,
    output logic [31:0]   todut_data,

    input  logic [15:0]   s_axi_awid,
    input  logic [31:0]   s_axi_awaddr,
    input  logic [7:0]    s_axi_awlen,
    input  logic [2:0]    s_axi_awsize,
    input  logic [1:0]    s_axi_awburst,
    input  logic          s_axi_awvalid,
    output logic          s_axi_awready,

    input  logic [31:0]   s_axi_wdata,
    input  logic [3:0]    s_axi_wstrb,
    input  logic          s_axi_wlast,
    input  logic          s_axi_wvalid,
    output logic          s_axi_wready,

    output logic [15:0]   s_axi_bid,
    output logic [1:0]    s_axi_bresp,
    output logic          s_axi_bvalid,
    input  logic          s_axi_bready,

    input  logic [15:0]   s_axi_arid,
    input  logic [31:0]   s_axi_araddr,
    input  logic [7:0]    s_axi_arlen,
    input  logic [2:0]    s_axi_arsize,
    input  logic [1:0]    s_axi_arburst,
    input  logic          s_axi_arvalid,
    output logic          s_axi_arready,

    output logic [15:0]   s_axi_rid,
    output logic [31:0]   s_axi_rdata,
    output logic [1:0]    s_axi_rresp,
    output logic          s_axi_rlast,
    output logic          s_axi_rvalid,
    input  logic          s_axi_rready
);
    localparam logic [1:0] AXI_RESP_OKAY   = 2'b00;
    localparam logic [1:0] AXI_RESP_DECERR = 2'b11;

    localparam logic [31:0] GBUS_MAGIC             = 32'h0000;
    localparam logic [31:0] GBUS_UPDATE_SEQ        = 32'h0008;
    localparam logic [31:0] GBUS_STATUS            = 32'h0010;
    localparam logic [31:0] GBUS_DRIVER_FEATURES_0 = 32'h0014;
    localparam logic [31:0] GBUS_DRIVER_FEATURES_1 = 32'h0018;
    localparam logic [31:0] GBUS_RESET_SEQ         = 32'h0020;
    localparam logic [31:0] GBUS_QUEUE_BASE        = 32'h0100;
    localparam int unsigned GBUS_QUEUE_STRIDE_WORDS = 8;
    localparam int unsigned GBUS_NUM_QUEUES = 3;
    localparam int unsigned GBUS_LAST_WORD = 128;

    logic [31:0] shadow_q [0:GBUS_LAST_WORD];

    logic        aw_pending_q;
    logic [15:0] awid_q;
    logic [31:0] awaddr_q;
    logic        aw_protocol_ok_q;
    logic        w_pending_q;
    logic [31:0] wdata_q;
    logic [3:0]  wstrb_q;
    logic        w_protocol_ok_q;

    logic aw_take;
    logic w_take;
    logic ar_take;

    function automatic logic addr_valid(input logic [31:0] addr);
        addr_valid = (addr[1:0] == 2'b00) && (addr <= 32'h0000_0200);
    endfunction

    function automatic logic [31:0] apply_wstrb(
        input logic [31:0] old_value,
        input logic [31:0] new_value,
        input logic [3:0]  wstrb
    );
        logic [31:0] ret;
        ret = old_value;
        for (int i = 0; i < 4; i++) begin
            if (wstrb[i]) begin
                ret[i * 8 +: 8] = new_value[i * 8 +: 8];
            end
        end
        return ret;
    endfunction

    assign s_axi_awready = !aw_pending_q && !s_axi_bvalid;
    assign s_axi_wready = !w_pending_q && !s_axi_bvalid;
    assign s_axi_arready = !s_axi_rvalid;
    assign s_axi_rlast = 1'b1;

    assign aw_take = s_axi_awvalid && s_axi_awready;
    assign w_take = s_axi_wvalid && s_axi_wready;
    assign ar_take = s_axi_arvalid && s_axi_arready;

    always_ff @(posedge clk or negedge rstn) begin
        if (!rstn) begin
            aw_pending_q <= 1'b0;
            awid_q <= 16'h0;
            awaddr_q <= 32'h0;
            aw_protocol_ok_q <= 1'b0;
            w_pending_q <= 1'b0;
            wdata_q <= 32'h0;
            wstrb_q <= 4'h0;
            w_protocol_ok_q <= 1'b0;
            s_axi_bid <= 16'h0;
            s_axi_bresp <= AXI_RESP_OKAY;
            s_axi_bvalid <= 1'b0;
            s_axi_rid <= 16'h0;
            s_axi_rdata <= 32'h0;
            s_axi_rresp <= AXI_RESP_OKAY;
            s_axi_rvalid <= 1'b0;
            todut_en <= 1'b0;
            todut_addr <= 32'h0;
            todut_data <= 32'h0;
            for (int i = 0; i <= GBUS_LAST_WORD; i++) begin
                shadow_q[i] <= 32'h0;
            end
            shadow_q[0] <= 32'h7375_6267;
            shadow_q[1] <= 32'h0000_0001;
            shadow_q[GBUS_UPDATE_SEQ[9:2]] <= 32'h0;
            shadow_q[18] <= 32'd126;
            shadow_q[19] <= 32'd512;
        end else begin
            todut_en <= 1'b0;

            if (s_axi_bvalid && s_axi_bready) begin
                s_axi_bvalid <= 1'b0;
            end
            if (s_axi_rvalid && s_axi_rready) begin
                s_axi_rvalid <= 1'b0;
            end

            if (aw_take) begin
                aw_pending_q <= 1'b1;
                awid_q <= s_axi_awid;
                awaddr_q <= s_axi_awaddr;
                aw_protocol_ok_q <= (s_axi_awlen == 8'd0) &&
                                    (s_axi_awsize == 3'd2) &&
                                    (s_axi_awburst == 2'b01);
            end
            if (w_take) begin
                w_pending_q <= 1'b1;
                wdata_q <= s_axi_wdata;
                wstrb_q <= s_axi_wstrb;
                w_protocol_ok_q <= s_axi_wlast;
            end

            if (!s_axi_bvalid && (aw_pending_q || aw_take) &&
                (w_pending_q || w_take)) begin
                logic [15:0] write_id;
                logic [31:0] write_addr;
                logic [31:0] write_data;
                logic [3:0] write_strb;
                logic protocol_ok;
                logic [31:0] merged_data;

                write_id = aw_pending_q ? awid_q : s_axi_awid;
                write_addr = aw_pending_q ? awaddr_q : s_axi_awaddr;
                write_data = w_pending_q ? wdata_q : s_axi_wdata;
                write_strb = w_pending_q ? wstrb_q : s_axi_wstrb;
                protocol_ok = (aw_pending_q ? aw_protocol_ok_q :
                               ((s_axi_awlen == 8'd0) &&
                                (s_axi_awsize == 3'd2) &&
                                (s_axi_awburst == 2'b01))) &&
                              (w_pending_q ? w_protocol_ok_q : s_axi_wlast);

                s_axi_bid <= write_id;
                s_axi_bvalid <= 1'b1;
                aw_pending_q <= 1'b0;
                w_pending_q <= 1'b0;

                if (protocol_ok && addr_valid(write_addr)) begin
                    merged_data = apply_wstrb(
                        shadow_q[write_addr[9:2]], write_data, write_strb);
                    if (write_addr != GBUS_UPDATE_SEQ) begin
                        shadow_q[write_addr[9:2]] <= merged_data;
                        todut_en <= 1'b1;
                        todut_addr <= write_addr;
                        todut_data <= merged_data;
                    end
                    s_axi_bresp <= AXI_RESP_OKAY;
                end else begin
                    s_axi_bresp <= AXI_RESP_DECERR;
                end
            end

            if (ar_take) begin
                s_axi_rid <= s_axi_arid;
                s_axi_rvalid <= 1'b1;
                if ((s_axi_arlen == 8'd0) &&
                    (s_axi_arsize == 3'd2) &&
                    (s_axi_arburst == 2'b01) &&
                    addr_valid(s_axi_araddr)) begin
                    s_axi_rdata <= shadow_q[s_axi_araddr[9:2]];
                    s_axi_rresp <= AXI_RESP_OKAY;
                end else begin
                    s_axi_rdata <= 32'hffff_ffff;
                    s_axi_rresp <= AXI_RESP_DECERR;
                end
            end

            if (addr_valid(publish_addr) &&
                publish_addr != GBUS_UPDATE_SEQ) begin
                if (publish_addr != GBUS_MAGIC &&
                    publish_data != shadow_q[publish_addr[9:2]]) begin
                    shadow_q[GBUS_UPDATE_SEQ[9:2]] <=
                        shadow_q[GBUS_UPDATE_SEQ[9:2]] + 32'd1;
                end
                shadow_q[publish_addr[9:2]] <= publish_data;
                if (publish_addr == GBUS_RESET_SEQ) begin
                    shadow_q[GBUS_STATUS[9:2]] <= 32'h0;
                    shadow_q[GBUS_DRIVER_FEATURES_0[9:2]] <= 32'h0;
                    shadow_q[GBUS_DRIVER_FEATURES_1[9:2]] <= 32'h0;
                    for (int q = 0; q < GBUS_NUM_QUEUES; q++) begin
                        for (int word = 0; word < 5; word++) begin
                            shadow_q[32'(GBUS_QUEUE_BASE[9:2]) +
                                     q * GBUS_QUEUE_STRIDE_WORDS + word] <=
                                32'h0;
                        end
                    end
                end
            end
        end
    end
endmodule
