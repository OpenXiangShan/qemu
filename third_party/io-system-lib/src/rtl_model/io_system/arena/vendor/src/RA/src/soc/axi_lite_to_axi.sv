// Copyright (c) 2014-2018 ETH Zurich, University of Bologna
// MODIFIED FOR OOO READ/WRITE SUPPORT via Reorder Buffer (ROB)
// Authors: ZhengSihan

module axi_lite_to_axi #(
  parameter int unsigned AxiDataWidth   = 32'd0,
  parameter int unsigned MaxOutstanding = 8,
  parameter type  req_lite_t = logic,
  parameter type resp_lite_t = logic,
  parameter type       req_t = logic,
  parameter type      resp_t = logic
) (
  input  logic            clk_i,
  input  logic            rst_ni,
  input  req_lite_t       slv_req_lite_i,
  output resp_lite_t      slv_resp_lite_o,
  input  axi_pkg::cache_t slv_aw_cache_i,
  input  axi_pkg::cache_t slv_ar_cache_i,
  output req_t            mst_req_o,
  input  resp_t           mst_resp_i
);
  localparam int unsigned AxiSize = axi_pkg::size_t'($unsigned($clog2(AxiDataWidth/8)));
  localparam int unsigned IdWidth = $clog2(MaxOutstanding);
  typedef logic [IdWidth-1:0] id_t;

  // -------------------------------------------------------------
  // READ REORDER BUFFER (AR/R Channels)
  // -------------------------------------------------------------
  id_t  ar_head_q, ar_tail_q;
  logic [MaxOutstanding-1:0] r_done_q;
  
  logic [AxiDataWidth-1:0] r_data_ram [MaxOutstanding];
  axi_pkg::resp_t          r_resp_ram [MaxOutstanding];

  logic [$clog2(MaxOutstanding+1)-1:0] r_inflight_q;
  logic r_rob_full, r_rob_empty;
  
  assign r_rob_full  = (r_inflight_q == MaxOutstanding);
  assign r_rob_empty = (r_inflight_q == 0);

  // -------------------------------------------------------------
  // WRITE REORDER BUFFER (AW/W/B Channels)
  // -------------------------------------------------------------
  id_t  aw_head_q, aw_tail_q;
  logic [MaxOutstanding-1:0] b_done_q;
  
  axi_pkg::resp_t          b_resp_ram [MaxOutstanding];
  
  logic [$clog2(MaxOutstanding+1)-1:0] w_inflight_q;
  logic w_rob_full, w_rob_empty;
  
  assign w_rob_full  = (w_inflight_q == MaxOutstanding);
  assign w_rob_empty = (w_inflight_q == 0);

  // -------------------------------------------------------------
  // OUTPUT DATA GATING
  //
  // In simulation, the RAM arrays start as 'X'. If we always read them
  // unconditionally, the packed struct `slv_resp_lite_o` will look like all-X
  // even when `*_valid` is 0 (which is legal on AXI). Gate data/resp with valid
  // to keep waveforms sane and avoid confusing X-propagation.
  // -------------------------------------------------------------
  logic                      r_out_valid;
  logic [AxiDataWidth-1:0]    r_out_data;
  axi_pkg::resp_t             r_out_resp;
  logic                      b_out_valid;
  axi_pkg::resp_t             b_out_resp;

  assign r_out_valid = r_done_q[ar_tail_q];
  assign r_out_data  = r_out_valid ? r_data_ram[ar_tail_q] : '0;
  assign r_out_resp  = r_out_valid ? r_resp_ram[ar_tail_q] : '0;

  assign b_out_valid = b_done_q[aw_tail_q];
  assign b_out_resp  = b_out_valid ? b_resp_ram[aw_tail_q] : '0;

  // -------------------------------------------------------------
  // AGGREGATE STRUCT ASSIGNMENTS
  // -------------------------------------------------------------
  assign mst_req_o = '{
    aw: '{
      id:     aw_head_q,
      addr:   slv_req_lite_i.aw.addr,
      size:   AxiSize,
      burst:  axi_pkg::BURST_FIXED,
      cache:  slv_aw_cache_i,
      prot:   slv_req_lite_i.aw.prot,
      default: '0
    },
    aw_valid: slv_req_lite_i.aw_valid & ~w_rob_full,
    w: '{
      data: slv_req_lite_i.w.data,
      strb: slv_req_lite_i.w.strb,
      last: 1'b1,
      default: '0
    },
    w_valid: slv_req_lite_i.w_valid,
    b_ready: 1'b1,
    ar: '{
      id:     ar_head_q,
      addr:   slv_req_lite_i.ar.addr,
      size:   AxiSize,
      burst:  axi_pkg::BURST_FIXED,
      cache:  slv_ar_cache_i,
      prot:   slv_req_lite_i.ar.prot,
      default: '0
    },
    ar_valid: slv_req_lite_i.ar_valid & ~r_rob_full,
    r_ready:  1'b1,
    default:   '0
  };

  assign slv_resp_lite_o = '{
    aw_ready: mst_resp_i.aw_ready & ~w_rob_full,
    w_ready:  mst_resp_i.w_ready,
    b: '{
      resp: b_out_resp,
      default: '0
    },
    b_valid:  b_out_valid,
    ar_ready: mst_resp_i.ar_ready & ~r_rob_full,
    r: '{
      data: r_out_data,
      resp: r_out_resp,
      default: '0
    },
    r_valid: r_out_valid,
    default: '0
  };

  // -------------------------------------------------------------
  // HELPER SIGNALS
  // -------------------------------------------------------------
  logic r_fire, ar_fire;
  assign ar_fire = mst_req_o.ar_valid & mst_resp_i.ar_ready;
  assign r_fire  = mst_resp_i.r_valid & mst_req_o.r_ready;

  logic slv_r_fire;
  assign slv_r_fire = r_out_valid & slv_req_lite_i.r_ready;

  logic aw_fire, b_fire;
  assign aw_fire = mst_req_o.aw_valid & mst_resp_i.aw_ready;
  assign b_fire  = mst_resp_i.b_valid & mst_req_o.b_ready;

  logic slv_b_fire;
  assign slv_b_fire = b_out_valid & slv_req_lite_i.b_ready;

  // -------------------------------------------------------------
  // Sequential Logic updating the queues (X-Propagation Safe!)
  // -------------------------------------------------------------
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      ar_head_q    <= '0;
      ar_tail_q    <= '0;
      r_inflight_q <= '0;
      r_done_q     <= '0;

      aw_head_q    <= '0;
      aw_tail_q    <= '0;
      w_inflight_q <= '0;
      b_done_q     <= '0;
    end else begin
      // ~~~~~~~~~~~~~~~~~~~~ R CHANNEL ~~~~~~~~~~~~~~~~~~~~~
      if (ar_fire === 1'b1) begin
        ar_head_q <= ar_head_q + 1'b1;
      end
      
      if (r_fire === 1'b1) begin
        // Downstream may widen/retag AXI IDs (e.g. prepend a master-select bit).
        // Only the low IdWidth bits correspond to our ROB slot index.
        id_t incoming_id;
        incoming_id = mst_resp_i.r.id[IdWidth-1:0];
        r_data_ram[incoming_id] <= mst_resp_i.r.data;
        r_resp_ram[incoming_id] <= mst_resp_i.r.resp;
        r_done_q[incoming_id]   <= 1'b1;
      end

      if (slv_r_fire === 1'b1) begin
        r_done_q[ar_tail_q] <= 1'b0;
        ar_tail_q <= ar_tail_q + 1'b1;
      end

      // Prevent X-Propagation on math operations by using case block
      case ({ar_fire === 1'b1, slv_r_fire === 1'b1})
        2'b10: r_inflight_q <= r_inflight_q + 1'b1;
        2'b01: r_inflight_q <= r_inflight_q - 1'b1;
        default: r_inflight_q <= r_inflight_q;
      endcase

      // ~~~~~~~~~~~~~~~~~~~~ W CHANNEL ~~~~~~~~~~~~~~~~~~~~~
      if (aw_fire === 1'b1) begin
        aw_head_q <= aw_head_q + 1'b1;
      end
      
      if (b_fire === 1'b1) begin
        id_t incoming_b_id;
        incoming_b_id = mst_resp_i.b.id[IdWidth-1:0];
        b_resp_ram[incoming_b_id] <= mst_resp_i.b.resp;
        b_done_q[incoming_b_id]   <= 1'b1;
      end

      if (slv_b_fire === 1'b1) begin
        b_done_q[aw_tail_q] <= 1'b0;
        aw_tail_q <= aw_tail_q + 1'b1;
      end

      // Prevent X-Propagation on math operations
      case ({aw_fire === 1'b1, slv_b_fire === 1'b1})
        2'b10: w_inflight_q <= w_inflight_q + 1'b1;
        2'b01: w_inflight_q <= w_inflight_q - 1'b1;
        default: w_inflight_q <= w_inflight_q;
      endcase
    end
  end

endmodule