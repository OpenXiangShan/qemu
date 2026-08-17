// SPDX-License-Identifier: Apache-2.0
//
// Minimal burst-capable memory-to-memory DMAC used by the RTL io-system.

module rtl_mem2mem_dmac #(
    parameter int unsigned ADDR_WIDTH = 64,
    parameter int unsigned DATA_WIDTH = 64,
    parameter int unsigned STRB_WIDTH = DATA_WIDTH / 8
) (
    input  logic                    clk,
    input  logic                    rstn,

    input  logic                    reg_wr_valid,
    input  logic [11:0]             reg_wr_addr,
    input  logic [DATA_WIDTH-1:0]   reg_wr_data,
    input  logic [STRB_WIDTH-1:0]   reg_wr_strb,
    output logic                    reg_wr_ready,
    output logic [1:0]              reg_wr_resp,

    input  logic                    reg_rd_valid,
    input  logic [11:0]             reg_rd_addr,
    output logic                    reg_rd_ready,
    output logic [DATA_WIDTH-1:0]   reg_rd_data,
    output logic [1:0]              reg_rd_resp,

    output logic [15:0]             m_axi_awid,
    output logic [ADDR_WIDTH-1:0]   m_axi_awaddr,
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
    output logic [ADDR_WIDTH-1:0]   m_axi_araddr,
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
    localparam logic [31:0] DMAC_ID       = 32'h4d445452; // "RTDM"
    localparam logic [31:0] DMAC_VERSION  = 32'h0000_0001;

    localparam logic [11:0] REG_ID        = 12'h000;
    localparam logic [11:0] REG_VERSION   = 12'h004;
    localparam logic [11:0] REG_CONTROL   = 12'h008;
    localparam logic [11:0] REG_STATUS    = 12'h00c;
    localparam logic [11:0] REG_SRC_LO    = 12'h010;
    localparam logic [11:0] REG_SRC_HI    = 12'h014;
    localparam logic [11:0] REG_DST_LO    = 12'h018;
    localparam logic [11:0] REG_DST_HI    = 12'h01c;
    localparam logic [11:0] REG_SIZE      = 12'h020;

    localparam logic [1:0] AXI_RESP_OKAY  = 2'b00;
    localparam logic [1:0] AXI_RESP_SLVERR = 2'b10;
    localparam int unsigned DMAC_MAX_BURST_BEATS = 8;

    typedef enum logic [2:0] {
        DMA_IDLE,
        DMA_READ_ADDR,
        DMA_READ_DATA,
        DMA_WRITE_ADDR_DATA,
        DMA_WRITE_RESP,
        DMA_DONE
    } dma_state_t;

    dma_state_t state_q, state_d;

    logic [63:0] src_addr_q, src_addr_d;
    logic [63:0] dst_addr_q, dst_addr_d;
    logic [31:0] size_q, size_d;

    logic [63:0] cur_src_q, cur_src_d;
    logic [63:0] cur_dst_q, cur_dst_d;
    logic [31:0] remain_q, remain_d;
    logic [7:0]  burst_len_q, burst_len_d;
    logic [7:0]  rd_beat_q, rd_beat_d;
    logic [7:0]  wr_beat_q, wr_beat_d;
    logic [7:0]  byte_buf_q [DMAC_MAX_BURST_BEATS];
    logic [7:0]  byte_buf_d [DMAC_MAX_BURST_BEATS];

    logic busy_q, busy_d;
    logic done_q, done_d;
    logic error_q, error_d;
    logic aw_done_q, aw_done_d;
    logic w_done_q, w_done_d;

    function automatic logic [31:0] apply_wstrb32(
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

    function automatic logic [DATA_WIDTH-1:0] byte_to_axi_wdata(
        input logic [7:0] byte_value,
        input logic [2:0] byte_lane
    );
        logic [DATA_WIDTH-1:0] ret;
        ret = '0;
        ret[byte_lane * 8 +: 8] = byte_value;
        return ret;
    endfunction

    function automatic logic [STRB_WIDTH-1:0] byte_to_axi_wstrb(
        input logic [2:0] byte_lane
    );
        logic [STRB_WIDTH-1:0] ret;
        ret = '0;
        ret[byte_lane] = 1'b1;
        return ret;
    endfunction

    function automatic logic [7:0] choose_burst_len(
        input logic [31:0] remain
    );
        if (remain >= 32'd8) begin
            return 8'd8;
        end
        return remain[7:0];
    endfunction

    function automatic logic [2:0] burst_byte_lane(
        input logic [63:0] base,
        input logic [7:0]  beat
    );
        logic [63:0] addr;
        addr = base + {56'd0, beat};
        return addr[2:0];
    endfunction

    always_comb begin
        for (int i = 0; i < DMAC_MAX_BURST_BEATS; i++) begin
            byte_buf_d[i] = byte_buf_q[i];
        end

        src_addr_d = src_addr_q;
        dst_addr_d = dst_addr_q;
        size_d = size_q;
        cur_src_d = cur_src_q;
        cur_dst_d = cur_dst_q;
        remain_d = remain_q;
        burst_len_d = burst_len_q;
        rd_beat_d = rd_beat_q;
        wr_beat_d = wr_beat_q;
        busy_d = busy_q;
        done_d = done_q;
        error_d = error_q;
        aw_done_d = aw_done_q;
        w_done_d = w_done_q;
        state_d = state_q;

        reg_wr_ready = 1'b1;
        reg_wr_resp = AXI_RESP_OKAY;
        reg_rd_ready = 1'b1;
        reg_rd_resp = AXI_RESP_OKAY;
        reg_rd_data = '0;

        m_axi_awid = 16'h0001;
        m_axi_awaddr = cur_dst_q;
        m_axi_awlen = burst_len_q == 8'd0 ? 8'd0 : burst_len_q - 8'd1;
        m_axi_awsize = 3'd0;
        m_axi_awburst = 2'b01;
        m_axi_awvalid = 1'b0;

        m_axi_wdata = byte_to_axi_wdata(byte_buf_q[wr_beat_q],
                                        burst_byte_lane(cur_dst_q,
                                                        wr_beat_q));
        m_axi_wstrb = byte_to_axi_wstrb(burst_byte_lane(cur_dst_q,
                                                        wr_beat_q));
        m_axi_wlast = wr_beat_q + 8'd1 >= burst_len_q;
        m_axi_wvalid = 1'b0;

        m_axi_bready = 1'b0;

        m_axi_arid = 16'h0001;
        m_axi_araddr = cur_src_q;
        m_axi_arlen = burst_len_q == 8'd0 ? 8'd0 : burst_len_q - 8'd1;
        m_axi_arsize = 3'd0;
        m_axi_arburst = 2'b01;
        m_axi_arvalid = 1'b0;

        m_axi_rready = 1'b0;

        unique case (reg_rd_addr)
        REG_ID: begin
            reg_rd_data[31:0] = DMAC_ID;
        end
        REG_VERSION: begin
            reg_rd_data[31:0] = DMAC_VERSION;
        end
        REG_CONTROL: begin
            reg_rd_data[31:0] = 32'h0;
        end
        REG_STATUS: begin
            reg_rd_data[0] = busy_q;
            reg_rd_data[1] = done_q;
            reg_rd_data[2] = error_q;
        end
        REG_SRC_LO: begin
            reg_rd_data[31:0] = src_addr_q[31:0];
        end
        REG_SRC_HI: begin
            reg_rd_data[31:0] = src_addr_q[63:32];
        end
        REG_DST_LO: begin
            reg_rd_data[31:0] = dst_addr_q[31:0];
        end
        REG_DST_HI: begin
            reg_rd_data[31:0] = dst_addr_q[63:32];
        end
        REG_SIZE: begin
            reg_rd_data[31:0] = size_q;
        end
        default: begin
            reg_rd_resp = AXI_RESP_SLVERR;
            reg_rd_data = '1;
        end
        endcase

        if (reg_wr_valid) begin
            unique case (reg_wr_addr)
            REG_CONTROL: begin
                if (reg_wr_data[1]) begin
                    done_d = 1'b0;
                    error_d = 1'b0;
                end
                if (reg_wr_data[0] && !busy_q) begin
                    done_d = 1'b0;
                    error_d = 1'b0;
                    cur_src_d = src_addr_q;
                    cur_dst_d = dst_addr_q;
                    remain_d = size_q;
                    burst_len_d = choose_burst_len(size_q);
                    rd_beat_d = 8'd0;
                    wr_beat_d = 8'd0;
                    aw_done_d = 1'b0;
                    w_done_d = 1'b0;
                    if (size_q == 32'd0) begin
                        done_d = 1'b1;
                        busy_d = 1'b0;
                        state_d = DMA_DONE;
                    end else begin
                        busy_d = 1'b1;
                        state_d = DMA_READ_ADDR;
                    end
                end
            end
            REG_SRC_LO: begin
                src_addr_d[31:0] = apply_wstrb32(src_addr_q[31:0],
                                                 reg_wr_data[31:0],
                                                 reg_wr_strb[3:0]);
            end
            REG_SRC_HI: begin
                src_addr_d[63:32] = apply_wstrb32(src_addr_q[63:32],
                                                  reg_wr_data[31:0],
                                                  reg_wr_strb[3:0]);
            end
            REG_DST_LO: begin
                dst_addr_d[31:0] = apply_wstrb32(dst_addr_q[31:0],
                                                 reg_wr_data[31:0],
                                                 reg_wr_strb[3:0]);
            end
            REG_DST_HI: begin
                dst_addr_d[63:32] = apply_wstrb32(dst_addr_q[63:32],
                                                  reg_wr_data[31:0],
                                                  reg_wr_strb[3:0]);
            end
            REG_SIZE: begin
                size_d = apply_wstrb32(size_q, reg_wr_data[31:0],
                                       reg_wr_strb[3:0]);
            end
            default: begin
                reg_wr_resp = AXI_RESP_SLVERR;
            end
            endcase
        end

        unique case (state_q)
        DMA_IDLE: begin
            busy_d = 1'b0;
        end
        DMA_READ_ADDR: begin
            m_axi_arvalid = 1'b1;
            if (m_axi_arready) begin
                rd_beat_d = 8'd0;
                state_d = DMA_READ_DATA;
            end
        end
        DMA_READ_DATA: begin
            m_axi_rready = 1'b1;
            if (m_axi_rvalid) begin
                byte_buf_d[rd_beat_q] =
                    m_axi_rdata[burst_byte_lane(cur_src_q, rd_beat_q) *
                                8 +: 8];
                if (m_axi_rresp != AXI_RESP_OKAY ||
                    m_axi_rlast != (rd_beat_q + 8'd1 >= burst_len_q)) begin
                    error_d = 1'b1;
                    busy_d = 1'b0;
                    done_d = 1'b1;
                    state_d = DMA_DONE;
                end else if (rd_beat_q + 8'd1 < burst_len_q) begin
                    rd_beat_d = rd_beat_q + 8'd1;
                end else begin
                    aw_done_d = 1'b0;
                    w_done_d = 1'b0;
                    wr_beat_d = 8'd0;
                    state_d = DMA_WRITE_ADDR_DATA;
                end
            end
        end
        DMA_WRITE_ADDR_DATA: begin
            m_axi_awvalid = !aw_done_q;
            m_axi_wvalid = !w_done_q;
            if (m_axi_awvalid && m_axi_awready) begin
                aw_done_d = 1'b1;
            end
            if (m_axi_wvalid && m_axi_wready) begin
                if (m_axi_wlast) begin
                    w_done_d = 1'b1;
                end else begin
                    wr_beat_d = wr_beat_q + 8'd1;
                end
            end
            if ((aw_done_q || (m_axi_awvalid && m_axi_awready)) &&
                (w_done_q || (m_axi_wvalid && m_axi_wready &&
                              m_axi_wlast))) begin
                state_d = DMA_WRITE_RESP;
            end
        end
        DMA_WRITE_RESP: begin
            m_axi_bready = 1'b1;
            if (m_axi_bvalid) begin
                if (m_axi_bresp != AXI_RESP_OKAY) begin
                    error_d = 1'b1;
                    busy_d = 1'b0;
                    done_d = 1'b1;
                    state_d = DMA_DONE;
                end else if (remain_q <= {24'd0, burst_len_q}) begin
                    remain_d = 32'd0;
                    busy_d = 1'b0;
                    done_d = 1'b1;
                    state_d = DMA_DONE;
                end else begin
                    cur_src_d = cur_src_q + {56'd0, burst_len_q};
                    cur_dst_d = cur_dst_q + {56'd0, burst_len_q};
                    remain_d = remain_q - {24'd0, burst_len_q};
                    burst_len_d =
                        choose_burst_len(remain_q - {24'd0, burst_len_q});
                    rd_beat_d = 8'd0;
                    wr_beat_d = 8'd0;
                    aw_done_d = 1'b0;
                    w_done_d = 1'b0;
                    state_d = DMA_READ_ADDR;
                end
            end
        end
        DMA_DONE: begin
            busy_d = 1'b0;
            if (reg_wr_valid && reg_wr_addr == REG_CONTROL && reg_wr_data[1]) begin
                state_d = DMA_IDLE;
            end
        end
        default: begin
            state_d = DMA_IDLE;
        end
        endcase
    end

    always_ff @(posedge clk or negedge rstn) begin
        if (!rstn) begin
            state_q <= DMA_IDLE;
            src_addr_q <= 64'd0;
            dst_addr_q <= 64'd0;
            size_q <= 32'd0;
            cur_src_q <= 64'd0;
            cur_dst_q <= 64'd0;
            remain_q <= 32'd0;
            burst_len_q <= 8'd0;
            rd_beat_q <= 8'd0;
            wr_beat_q <= 8'd0;
            for (int i = 0; i < DMAC_MAX_BURST_BEATS; i++) begin
                byte_buf_q[i] <= 8'd0;
            end
            busy_q <= 1'b0;
            done_q <= 1'b0;
            error_q <= 1'b0;
            aw_done_q <= 1'b0;
            w_done_q <= 1'b0;
        end else begin
            state_q <= state_d;
            src_addr_q <= src_addr_d;
            dst_addr_q <= dst_addr_d;
            size_q <= size_d;
            cur_src_q <= cur_src_d;
            cur_dst_q <= cur_dst_d;
            remain_q <= remain_d;
            burst_len_q <= burst_len_d;
            rd_beat_q <= rd_beat_d;
            wr_beat_q <= wr_beat_d;
            for (int i = 0; i < DMAC_MAX_BURST_BEATS; i++) begin
                byte_buf_q[i] <= byte_buf_d[i];
            end
            busy_q <= busy_d;
            done_q <= done_d;
            error_q <= error_d;
            aw_done_q <= aw_done_d;
            w_done_q <= w_done_d;
        end
    end
endmodule
