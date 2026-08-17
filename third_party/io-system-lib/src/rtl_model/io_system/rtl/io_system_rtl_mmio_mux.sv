// SPDX-License-Identifier: Apache-2.0
//
// MMIO mux for the RTL io-system wrapper. Device decode stays inside the
// RTL io-system instead of being split in QEMU.

module io_system_rtl_mmio_mux #(
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

    output logic                    dmac_wr_valid,
    output logic [11:0]             dmac_wr_addr,
    output logic [DATA_WIDTH-1:0]   dmac_wr_data,
    output logic [STRB_WIDTH-1:0]   dmac_wr_strb,
    input  logic                    dmac_wr_ready,
    input  logic [1:0]              dmac_wr_resp,

    output logic                    dmac_rd_valid,
    output logic [11:0]             dmac_rd_addr,
    input  logic                    dmac_rd_ready,
    input  logic [DATA_WIDTH-1:0]   dmac_rd_data,
    input  logic [1:0]              dmac_rd_resp,

    output logic                    virtio_valid,
    output logic                    virtio_write,
    output logic [11:0]             virtio_addr,
    output logic [31:0]             virtio_wdata,
    output logic [2:0]              virtio_size,
    input  logic [31:0]             virtio_rdata,
    input  logic                    virtio_ready,

    output logic                    aplic_m_wr_valid,
    output logic [13:0]             aplic_m_wr_addr,
    output logic [DATA_WIDTH-1:0]   aplic_m_wr_data,
    output logic [STRB_WIDTH-1:0]   aplic_m_wr_strb,
    input  logic                    aplic_m_wr_ready,
    input  logic [1:0]              aplic_m_wr_resp,

    output logic                    aplic_m_rd_valid,
    output logic [13:0]             aplic_m_rd_addr,
    input  logic                    aplic_m_rd_ready,
    input  logic [DATA_WIDTH-1:0]   aplic_m_rd_data,
    input  logic [1:0]              aplic_m_rd_resp,

    output logic                    aplic_s_wr_valid,
    output logic [13:0]             aplic_s_wr_addr,
    output logic [DATA_WIDTH-1:0]   aplic_s_wr_data,
    output logic [STRB_WIDTH-1:0]   aplic_s_wr_strb,
    input  logic                    aplic_s_wr_ready,
    input  logic [1:0]              aplic_s_wr_resp,

    output logic                    aplic_s_rd_valid,
    output logic [13:0]             aplic_s_rd_addr,
    input  logic                    aplic_s_rd_ready,
    input  logic [DATA_WIDTH-1:0]   aplic_s_rd_data,
    input  logic [1:0]              aplic_s_rd_resp
);
    localparam logic [1:0] AXI_RESP_OKAY   = 2'b00;
    localparam logic [1:0] AXI_RESP_DECERR = 2'b11;

    typedef enum logic [2:0] {
        DEV_NONE,
        DEV_DMAC,
        DEV_VIRTIO_BLK,
        DEV_APLIC_M,
        DEV_APLIC_S
    } dev_t;

    logic write_pending_q, write_pending_d;
    logic write_resp_pending_q, write_resp_pending_d;
    logic [15:0] write_id_q, write_id_d;
    logic [63:0] write_addr_q, write_addr_d;
    logic [2:0] write_size_q, write_size_d;
    logic [1:0] write_resp_q, write_resp_d;
    dev_t write_dev_q, write_dev_d;

    logic read_pending_q, read_pending_d;
    logic [15:0] read_id_q, read_id_d;
    logic [DATA_WIDTH-1:0] read_data_q, read_data_d;
    logic [1:0] read_resp_q, read_resp_d;

    dev_t aw_dev;
    dev_t ar_dev;
    logic [63:0] aw_offset;
    logic [63:0] ar_offset;
    logic [2:0] write_lane;
    logic write_data_accept;

    function automatic logic in_window(
        input logic [63:0] addr,
        input logic [63:0] base,
        input logic [63:0] size
    );
        in_window = (addr >= base) && (addr < (base + size));
    endfunction

    function automatic dev_t decode_addr(input logic [63:0] addr);
        if (in_window(addr, DMAC_BASE, DMAC_SIZE)) begin
            decode_addr = DEV_DMAC;
        end else if (in_window(addr, MY_VIRTIO_BLK_BASE, MY_VIRTIO_BLK_SIZE)) begin
            decode_addr = DEV_VIRTIO_BLK;
        end else if (in_window(addr, APLIC_M_BASE, APLIC_SIZE)) begin
            decode_addr = DEV_APLIC_M;
        end else if (in_window(addr, APLIC_S_BASE, APLIC_SIZE)) begin
            decode_addr = DEV_APLIC_S;
        end else begin
            decode_addr = DEV_NONE;
        end
    endfunction

    function automatic logic [63:0] dev_offset(
        input dev_t dev,
        input logic [63:0] addr
    );
        unique case (dev)
        DEV_DMAC:       dev_offset = addr - DMAC_BASE;
        DEV_VIRTIO_BLK: dev_offset = addr - MY_VIRTIO_BLK_BASE;
        DEV_APLIC_M:    dev_offset = addr - APLIC_M_BASE;
        DEV_APLIC_S:    dev_offset = addr - APLIC_S_BASE;
        default:        dev_offset = addr;
        endcase
    endfunction

    function automatic logic [2:0] axi_bytes(input logic [2:0] axsize);
        unique case (axsize)
        3'd0: axi_bytes = 3'd1;
        3'd1: axi_bytes = 3'd2;
        3'd2: axi_bytes = 3'd4;
        default: axi_bytes = 3'd4;
        endcase
    endfunction

    function automatic logic dev_write_ready(input dev_t dev);
        unique case (dev)
        DEV_DMAC:       dev_write_ready = dmac_wr_ready;
        DEV_VIRTIO_BLK: dev_write_ready = virtio_ready;
        DEV_APLIC_M:    dev_write_ready = aplic_m_wr_ready;
        DEV_APLIC_S:    dev_write_ready = aplic_s_wr_ready;
        default:        dev_write_ready = 1'b1;
        endcase
    endfunction

    function automatic logic [1:0] dev_write_resp(input dev_t dev);
        unique case (dev)
        DEV_DMAC:       dev_write_resp = dmac_wr_resp;
        DEV_VIRTIO_BLK: dev_write_resp = AXI_RESP_OKAY;
        DEV_APLIC_M:    dev_write_resp = aplic_m_wr_resp;
        DEV_APLIC_S:    dev_write_resp = aplic_s_wr_resp;
        default:        dev_write_resp = AXI_RESP_DECERR;
        endcase
    endfunction

    function automatic logic dev_read_ready(input dev_t dev);
        unique case (dev)
        DEV_DMAC:       dev_read_ready = dmac_rd_ready;
        DEV_VIRTIO_BLK: dev_read_ready = virtio_ready;
        DEV_APLIC_M:    dev_read_ready = aplic_m_rd_ready;
        DEV_APLIC_S:    dev_read_ready = aplic_s_rd_ready;
        default:        dev_read_ready = 1'b1;
        endcase
    endfunction

    function automatic logic [1:0] dev_read_resp(input dev_t dev);
        unique case (dev)
        DEV_DMAC:       dev_read_resp = dmac_rd_resp;
        DEV_VIRTIO_BLK: dev_read_resp = AXI_RESP_OKAY;
        DEV_APLIC_M:    dev_read_resp = aplic_m_rd_resp;
        DEV_APLIC_S:    dev_read_resp = aplic_s_rd_resp;
        default:        dev_read_resp = AXI_RESP_DECERR;
        endcase
    endfunction

    function automatic logic [DATA_WIDTH-1:0] dev_read_data(
        input dev_t dev,
        input logic [2:0] lane
    );
        unique case (dev)
        DEV_DMAC:       dev_read_data = dmac_rd_data << (lane * 8);
        DEV_VIRTIO_BLK: dev_read_data = DATA_WIDTH'(virtio_rdata) << (lane * 8);
        DEV_APLIC_M:    dev_read_data = aplic_m_rd_data << (lane * 8);
        DEV_APLIC_S:    dev_read_data = aplic_s_rd_data << (lane * 8);
        default:        dev_read_data = '1;
        endcase
    endfunction

    assign aw_dev = decode_addr(s_axi_awaddr);
    assign ar_dev = decode_addr(s_axi_araddr);
    assign aw_offset = dev_offset(aw_dev, s_axi_awaddr);
    assign ar_offset = dev_offset(ar_dev, s_axi_araddr);
    assign write_lane = write_addr_q[2:0];
    assign write_data_accept = write_pending_q && !write_resp_pending_q &&
                               s_axi_wvalid && s_axi_wlast &&
                               dev_write_ready(write_dev_q);
    assign dmac_rd_addr = ar_offset[11:0];
    assign virtio_addr = (write_data_accept &&
                          (write_dev_q == DEV_VIRTIO_BLK)) ?
                         write_addr_q[11:0] : ar_offset[11:0];
    assign virtio_wdata = 32'(s_axi_wdata >> (write_lane * 8));
    assign virtio_size = (write_data_accept &&
                          (write_dev_q == DEV_VIRTIO_BLK)) ?
                         write_size_q : axi_bytes(s_axi_arsize);
    assign aplic_m_rd_addr = ar_offset[13:0];
    assign aplic_s_rd_addr = ar_offset[13:0];

    always_comb begin
        write_pending_d = write_pending_q;
        write_resp_pending_d = write_resp_pending_q;
        write_id_d = write_id_q;
        write_addr_d = write_addr_q;
        write_size_d = write_size_q;
        write_dev_d = write_dev_q;
        write_resp_d = write_resp_q;

        read_pending_d = read_pending_q;
        read_id_d = read_id_q;
        read_data_d = read_data_q;
        read_resp_d = read_resp_q;

        s_axi_awready = !write_pending_q && !write_resp_pending_q &&
                        (s_axi_awlen == 8'd0) &&
                        (s_axi_awburst == 2'b01);
        s_axi_wready = write_pending_q && !write_resp_pending_q &&
                       s_axi_wlast && dev_write_ready(write_dev_q);
        s_axi_bid = write_id_q;
        s_axi_bresp = write_resp_q;
        s_axi_bvalid = write_resp_pending_q;

        s_axi_arready = !read_pending_q &&
                        !write_pending_q && !write_resp_pending_q &&
                        (s_axi_arlen == 8'd0) &&
                        (s_axi_arburst == 2'b01);
        s_axi_rid = read_id_q;
        s_axi_rdata = read_data_q;
        s_axi_rresp = read_resp_q;
        s_axi_rlast = 1'b1;
        s_axi_rvalid = read_pending_q;

        dmac_wr_valid = write_data_accept && (write_dev_q == DEV_DMAC);
        dmac_wr_addr = write_addr_q[11:0];
        dmac_wr_data = s_axi_wdata >> (write_lane * 8);
        dmac_wr_strb = s_axi_wstrb >> write_lane;

        dmac_rd_valid = 1'b0;

        virtio_valid = 1'b0;
        virtio_write = 1'b0;

        aplic_m_wr_valid = write_data_accept && (write_dev_q == DEV_APLIC_M);
        aplic_m_wr_addr = write_addr_q[13:0];
        aplic_m_wr_data = s_axi_wdata >> (write_lane * 8);
        aplic_m_wr_strb = s_axi_wstrb >> write_lane;
        aplic_m_rd_valid = 1'b0;

        aplic_s_wr_valid = write_data_accept && (write_dev_q == DEV_APLIC_S);
        aplic_s_wr_addr = write_addr_q[13:0];
        aplic_s_wr_data = s_axi_wdata >> (write_lane * 8);
        aplic_s_wr_strb = s_axi_wstrb >> write_lane;
        aplic_s_rd_valid = 1'b0;

        if (s_axi_awvalid && s_axi_awready) begin
            write_pending_d = 1'b1;
            write_id_d = s_axi_awid;
            write_addr_d = aw_offset;
            write_size_d = axi_bytes(s_axi_awsize);
            write_dev_d = aw_dev;
        end

        if (write_data_accept) begin
            write_pending_d = 1'b0;
            write_resp_pending_d = 1'b1;
            write_resp_d = dev_write_resp(write_dev_q);
            if (write_dev_q == DEV_VIRTIO_BLK) begin
                virtio_valid = 1'b1;
                virtio_write = 1'b1;
            end
        end

        if (s_axi_bvalid && s_axi_bready) begin
            write_resp_pending_d = 1'b0;
        end

        if (s_axi_arvalid && s_axi_arready) begin
            read_id_d = s_axi_arid;
            read_pending_d = 1'b1;
            unique case (ar_dev)
            DEV_DMAC: begin
                dmac_rd_valid = 1'b1;
            end
            DEV_VIRTIO_BLK: begin
                virtio_valid = 1'b1;
                virtio_write = 1'b0;
            end
            DEV_APLIC_M: begin
                aplic_m_rd_valid = 1'b1;
            end
            DEV_APLIC_S: begin
                aplic_s_rd_valid = 1'b1;
            end
            default: begin
            end
            endcase
            read_data_d = dev_read_data(ar_dev, s_axi_araddr[2:0]);
            read_resp_d = dev_read_ready(ar_dev) ? dev_read_resp(ar_dev) :
                          AXI_RESP_DECERR;
        end

        if (s_axi_rvalid && s_axi_rready) begin
            read_pending_d = 1'b0;
        end
    end

    always_ff @(posedge clk or negedge rstn) begin
        if (!rstn) begin
            write_pending_q <= 1'b0;
            write_resp_pending_q <= 1'b0;
            write_id_q <= 16'd0;
            write_addr_q <= 64'd0;
            write_size_q <= 3'd4;
            write_dev_q <= DEV_NONE;
            write_resp_q <= AXI_RESP_OKAY;
            read_pending_q <= 1'b0;
            read_id_q <= 16'd0;
            read_data_q <= '0;
            read_resp_q <= AXI_RESP_OKAY;
        end else begin
            write_pending_q <= write_pending_d;
            write_resp_pending_q <= write_resp_pending_d;
            write_id_q <= write_id_d;
            write_addr_q <= write_addr_d;
            write_size_q <= write_size_d;
            write_dev_q <= write_dev_d;
            write_resp_q <= write_resp_d;
            read_pending_q <= read_pending_d;
            read_id_q <= read_id_d;
            read_data_q <= read_data_d;
            read_resp_q <= read_resp_d;
        end
    end
endmodule
