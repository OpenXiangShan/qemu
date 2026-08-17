// SPDX-License-Identifier: Apache-2.0
//
// RTL APLIC wrapper for io-system. It keeps the MSI base shadow locally and
// delegates the interrupt register logic to the copied scalable APLIC domain.

module io_system_rtl_aplic_msi #(
    parameter bit MMODE = 1'b0,
    parameter int unsigned DOMAIN_ADDR = 32'h0000_0000,
    parameter int unsigned NUM_SOURCES = 96,
    parameter int unsigned DATA_WIDTH = 64,
    parameter int unsigned STRB_WIDTH = DATA_WIDTH / 8
) (
    input  logic                    clk,
    input  logic                    rstn,

    input  logic [31:0]             parent_smsi_addr,
    input  logic [31:0]             parent_smsi_addrh,
    output logic [31:0]             smsi_addr,
    output logic [31:0]             smsi_addrh,

    input  logic                    irq_line,
    input  logic [31:0]             irq_source,
    output logic                    delegated_irq_line,

    input  logic                    reg_wr_valid,
    input  logic [13:0]             reg_wr_addr,
    input  logic [DATA_WIDTH-1:0]   reg_wr_data,
    input  logic [STRB_WIDTH-1:0]   reg_wr_strb,
    output logic                    reg_wr_ready,
    output logic [1:0]              reg_wr_resp,

    input  logic                    reg_rd_valid,
    input  logic [13:0]             reg_rd_addr,
    output logic                    reg_rd_ready,
    output logic [DATA_WIDTH-1:0]   reg_rd_data,
    output logic [1:0]              reg_rd_resp,

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
    output logic                    m_axi_bready
);
    localparam logic [1:0] AXI_RESP_OKAY   = 2'b00;
    localparam logic [1:0] AXI_RESP_SLVERR = 2'b10;

    localparam logic [13:0] REG_MMSICFGADDR  = 14'h1bc0;
    localparam logic [13:0] REG_MMSICFGADDRH = 14'h1bc4;
    localparam logic [13:0] REG_SMSICFGADDR  = 14'h1bc8;
    localparam logic [13:0] REG_SMSICFGADDRH = 14'h1bcc;

    logic [31:0] mmsi_cfg_lo_q;
    logic [31:0] mmsi_cfg_hi_q;
    logic [31:0] smsi_cfg_lo_q;
    logic [31:0] smsi_cfg_hi_q;

    logic [63:0] i_imsic_addr_target;
    logic [NUM_SOURCES:0] irq_sources;
    logic [NUM_SOURCES:0] irq_delegated_sources;

    reg_intf::reg_intf_req_a32_d64 top_req;
    reg_intf::reg_intf_resp_d64    top_resp;
    ariane_axi::req_t              msi_req;
    ariane_axi::resp_t             msi_resp;

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

    function automatic logic [63:0] cfg_to_addr(
        input logic [31:0] lo,
        input logic [31:0] hi
    );
        cfg_to_addr = {20'h0, hi[11:0], lo} << 12;
    endfunction

    function automatic logic local_cfg_hit(input logic [13:0] addr);
        local_cfg_hit = (addr == REG_MMSICFGADDR) ||
                        (addr == REG_MMSICFGADDRH) ||
                        (addr == REG_SMSICFGADDR) ||
                        (addr == REG_SMSICFGADDRH);
    endfunction

    function automatic logic [31:0] local_cfg_read(input logic [13:0] addr);
        unique case (addr)
        REG_MMSICFGADDR:  local_cfg_read = mmsi_cfg_lo_q;
        REG_MMSICFGADDRH: local_cfg_read = mmsi_cfg_hi_q;
        REG_SMSICFGADDR:  local_cfg_read = smsi_cfg_lo_q;
        REG_SMSICFGADDRH: local_cfg_read = smsi_cfg_hi_q;
        default:          local_cfg_read = 32'h0;
        endcase
    endfunction

    always_comb begin
        irq_sources = '0;
        if (irq_line && (irq_source <= NUM_SOURCES)) begin
            irq_sources[irq_source] = 1'b1;
        end
        delegated_irq_line = (irq_source <= NUM_SOURCES) ?
                             irq_delegated_sources[irq_source] : 1'b0;

        if (MMODE) begin
            smsi_addr = smsi_cfg_lo_q;
            smsi_addrh = smsi_cfg_hi_q;
            i_imsic_addr_target = cfg_to_addr(mmsi_cfg_lo_q, mmsi_cfg_hi_q);
        end else begin
            smsi_addr = parent_smsi_addr;
            smsi_addrh = parent_smsi_addrh;
            i_imsic_addr_target = cfg_to_addr(parent_smsi_addr, parent_smsi_addrh);
        end

        top_req.addr = DOMAIN_ADDR + (reg_wr_valid ? {18'h0, reg_wr_addr}
                                                   : {18'h0, reg_rd_addr});
        top_req.write = reg_wr_valid && !local_cfg_hit(reg_wr_addr);
        top_req.wdata = reg_wr_data;
        top_req.wstrb = reg_wr_strb;
        top_req.valid = (reg_wr_valid && !local_cfg_hit(reg_wr_addr)) ||
                        (reg_rd_valid && !local_cfg_hit(reg_rd_addr) &&
                         !reg_wr_valid);

        if (reg_wr_valid && local_cfg_hit(reg_wr_addr)) begin
            reg_wr_resp = AXI_RESP_OKAY;
        end else begin
            reg_wr_resp = top_resp.error ? AXI_RESP_SLVERR : AXI_RESP_OKAY;
        end

        if (reg_rd_valid && local_cfg_hit(reg_rd_addr)) begin
            reg_rd_data = DATA_WIDTH'(local_cfg_read(reg_rd_addr));
            reg_rd_resp = AXI_RESP_OKAY;
        end else begin
            reg_rd_data = top_resp.rdata;
            reg_rd_resp = top_resp.error ? AXI_RESP_SLVERR : AXI_RESP_OKAY;
        end

        reg_wr_ready = 1'b1;
        reg_rd_ready = 1'b1;

        msi_resp.aw_ready = m_axi_awready;
        msi_resp.ar_ready = 1'b0;
        msi_resp.w_ready = m_axi_awready & m_axi_wready;
        msi_resp.b_valid = m_axi_bvalid;
        msi_resp.b.id = m_axi_bid[ariane_axi::IdWidth-1:0];
        msi_resp.b.resp = m_axi_bresp;
        msi_resp.b.user = '0;
        msi_resp.r_valid = 1'b0;
        msi_resp.r = '0;

        m_axi_awid = msi_req.aw.id;
        m_axi_awaddr = msi_req.aw.addr;
        m_axi_awlen = msi_req.aw.len;
        m_axi_awsize = msi_req.aw.size;
        m_axi_awburst = msi_req.aw.burst;
        m_axi_awvalid = msi_req.aw_valid;

        m_axi_wdata = msi_req.w.data;
        m_axi_wstrb = msi_req.w.strb;
        m_axi_wlast = msi_req.w.last;
        m_axi_wvalid = msi_req.w_valid;

        m_axi_bready = msi_req.b_ready;
    end

    always_ff @(posedge clk or negedge rstn) begin
        if (!rstn) begin
            mmsi_cfg_lo_q <= 32'h0;
            mmsi_cfg_hi_q <= 32'h0;
            smsi_cfg_lo_q <= 32'h0;
            smsi_cfg_hi_q <= 32'h0;
        end else begin
            if (reg_wr_valid) begin
                unique case (reg_wr_addr)
                REG_MMSICFGADDR: begin
                    mmsi_cfg_lo_q <= apply_wstrb32(mmsi_cfg_lo_q,
                                                   reg_wr_data[31:0],
                                                   reg_wr_strb[3:0]);
                end
                REG_MMSICFGADDRH: begin
                    mmsi_cfg_hi_q <= apply_wstrb32(mmsi_cfg_hi_q,
                                                   reg_wr_data[31:0],
                                                   reg_wr_strb[3:0]);
                end
                REG_SMSICFGADDR: begin
                    smsi_cfg_lo_q <= apply_wstrb32(smsi_cfg_lo_q,
                                                   reg_wr_data[31:0],
                                                   reg_wr_strb[3:0]);
                end
                REG_SMSICFGADDRH: begin
                    smsi_cfg_hi_q <= apply_wstrb32(smsi_cfg_hi_q,
                                                   reg_wr_data[31:0],
                                                   reg_wr_strb[3:0]);
                end
                default: begin
                end
                endcase
            end
        end
    end

    aplic_domain_top #(
        .DOMAIN_ADDR(DOMAIN_ADDR),
        .NR_SRC(NUM_SOURCES + 1),
        .MIN_PRIO(6),
        .NR_IDCs(1),
        .APLIC(MMODE ? "NON-LEAF" : "LEAF"),
        .MODE("MSI"),
        .IMSIC_ADDR_TARGET(64'h0),
        .reg_req_t(reg_intf::reg_intf_req_a32_d64),
        .reg_rsp_t(reg_intf::reg_intf_resp_d64)
    ) u_aplic_domain_top (
        .i_clk(clk),
        .ni_rst(rstn),
        .i_req(top_req),
        .o_resp(top_resp),
        .i_irq_sources(irq_sources),
        .i_imsic_addr_target(i_imsic_addr_target),
        .o_irq_del_sources(irq_delegated_sources),
        .o_busy(),
        .o_req(msi_req),
        .i_resp(msi_resp)
    );
endmodule
