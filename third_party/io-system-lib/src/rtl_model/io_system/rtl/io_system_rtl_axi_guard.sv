// SPDX-License-Identifier: Apache-2.0
// Route unsupported requests to an AXI error slave before width conversion.
// Error responses retain ID and read beat count; rejected writes have no
// downstream memory side effects. AW routing also owns the associated W beats.
`ifdef IO_SYSTEM_RTL_ARENA
module io_system_rtl_axi_guard #(
    parameter int unsigned ID_WIDTH = 8,
    parameter int unsigned MAX_SIZE = 6,
    parameter int unsigned MAX_TRANS = 16,
    parameter bit ALLOW_ALIGNED_READ_WRAP = 1'b0,
    parameter bit CHECK_WINDOW = 1'b0,
    parameter logic [63:0] BASE = 64'h0,
    parameter logic [63:0] SIZE = 64'h0,
    parameter type aw_t = logic,
    parameter type w_t = logic,
    parameter type b_t = logic,
    parameter type ar_t = logic,
    parameter type r_t = logic,
    parameter type req_t = logic,
    parameter type resp_t = logic
) (
    input logic clk,
    input logic rstn,
    input req_t req_i,
    output resp_t resp_o,
    output req_t req_o,
    input resp_t resp_i
);
    req_t [1:0] branch_req;
    resp_t [1:0] branch_resp;
    req_t filtered_req, normalized_req;
    resp_t filtered_resp;

    // Reject atomic store and load classes before the board's demux, whose
    // atomic read-ID injection is disabled. Load-class rejection needs both
    // B and R; clearing ATOP alone would lose the required R response.
    axi_atop_filter #(
        .AxiIdWidth(ID_WIDTH), .AxiMaxWriteTxns(MAX_TRANS),
        .req_t(req_t), .resp_t(resp_t)
    ) u_atop_filter (
        .clk_i(clk), .rst_ni(rstn), .slv_req_i(req_i), .slv_resp_o(resp_o),
        .mst_req_o(filtered_req), .mst_resp_i(filtered_resp)
    );

    // A legal WRAP starting at the bottom of its wrap window has exactly
    // the same beat addresses/order as INCR. E6 D-cache refills use this
    // form. Normalize before width conversion; arbitrary WRAP starts still
    // need a splitter/reorderer and must continue to receive SLVERR here.
    function automatic logic aligned_wrap(input ar_t ar);
        logic [64:0] bytes;
        bytes = ({57'b0, ar.len} + 1) << ar.size;
        return ar.burst == axi_pkg::BURST_WRAP &&
               (ar.len == 1 || ar.len == 3 || ar.len == 7 || ar.len == 15) &&
               ar.size <= MAX_SIZE && !ar.lock &&
               (({1'b0, ar.addr} & (bytes - 1)) == 0);
    endfunction

    always_comb begin
        normalized_req = filtered_req;
        if (ALLOW_ALIGNED_READ_WRAP && aligned_wrap(filtered_req.ar))
            normalized_req.ar.burst = axi_pkg::BURST_INCR;
    end

    function automatic logic legal(input logic [63:0] addr,
                                    input logic [7:0] len,
                                    input logic [2:0] size,
                                    input logic [1:0] burst,
                                    input logic lock);
        logic [64:0] first, limit;
        first = {1'b0, addr} & ~((65'd1 << size) - 1);
        limit = first + (({57'b0, len} + 1) << size);
        legal = (size <= MAX_SIZE) && (burst == axi_pkg::BURST_INCR) && !lock &&
                !limit[64] && (first[63:12] == ((limit - 1) >> 12));
        if (CHECK_WINDOW)
            legal = legal && addr >= BASE && limit <= ({1'b0, BASE} + {1'b0, SIZE});
    endfunction

    axi_demux #(
        .AxiIdWidth(ID_WIDTH), .aw_chan_t(aw_t), .w_chan_t(w_t),
        .b_chan_t(b_t), .ar_chan_t(ar_t), .r_chan_t(r_t),
        .req_t(req_t), .resp_t(resp_t), .NoMstPorts(2),
        .MaxTrans(MAX_TRANS), .AxiLookBits(ID_WIDTH > 4 ? 4 : ID_WIDTH),
        .SpillAw(1'b1), .SpillAr(1'b1)
    ) u_route (
        .clk_i(clk), .rst_ni(rstn), .test_i(1'b0),
        .slv_req_i(normalized_req), .slv_resp_o(filtered_resp),
        .slv_aw_select_i(!legal(filtered_req.aw.addr, filtered_req.aw.len, filtered_req.aw.size,
                               filtered_req.aw.burst, filtered_req.aw.lock)),
        .slv_ar_select_i(!legal(normalized_req.ar.addr, normalized_req.ar.len, normalized_req.ar.size,
                               normalized_req.ar.burst, normalized_req.ar.lock)),
        .mst_reqs_o(branch_req), .mst_resps_i(branch_resp)
    );
    assign req_o = branch_req[0];
    assign branch_resp[0] = resp_i;
    axi_err_slv #(
        .AxiIdWidth(ID_WIDTH), .req_t(req_t), .resp_t(resp_t),
        .Resp(axi_pkg::RESP_SLVERR), .RespData(64'b0),
        .ATOPs(1'b0), .MaxTrans(MAX_TRANS)
    ) u_error (
        .clk_i(clk), .rst_ni(rstn), .test_i(1'b0),
        .slv_req_i(branch_req[1]), .slv_resp_o(branch_resp[1])
    );
endmodule
`endif
