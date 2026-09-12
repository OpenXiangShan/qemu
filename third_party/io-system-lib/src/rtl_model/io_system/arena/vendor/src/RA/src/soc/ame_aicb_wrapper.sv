// AME v0.5 and RVV integration wrapper.
//
// The generated AME/AICB RTL uses packed aggregate ports.  E6 and VecTop
// still expose the legacy scalar ports, so this module is the single boundary
// where widths and the two-device AII topology are adapted.
module ame_aicb_wrapper #(
  // The FPGA top currently uses an older, externally generated crossbar
  // without an AME user port. Keep the AME datapath compiled, but let that
  // top reject AME instructions explicitly until its crossbar is regenerated.
  parameter bit ENABLE_AME = 1'b1
) (
  input  logic clock,
  input  logic reset,

  input  logic        cpu_issue_valid,
  output logic        cpu_issue_ready,
  input  logic [63:0] cpu_issue_hart_id,
  input  logic [3:0]  cpu_issue_inst_id,
  input  logic [31:0] cpu_issue_inst,
  output logic        cpu_issue_accept,
  output logic        cpu_issue_rd_valid_0,
  output logic        cpu_issue_rd_valid_1,
  output logic        cpu_issue_frd_valid_0,
  output logic        cpu_issue_frd_valid_1,
  output logic        cpu_issue_wb_valid,
  output logic        cpu_issue_fwb_valid,
  input  logic        cpu_register_valid,
  input  logic [63:0] cpu_register_hart_id,
  input  logic [3:0]  cpu_register_inst_id,
  input  logic [63:0] cpu_register_data_0,
  input  logic [63:0] cpu_register_data_1,
  input  logic [63:0] cpu_register_fdata_0,
  input  logic [63:0] cpu_register_fdata_1,
  input  logic        cpu_commit_valid,
  input  logic [63:0] cpu_commit_hart_id,
  input  logic [3:0]  cpu_commit_inst_id,
  input  logic        cpu_commit_kill,
  input  logic        cpu_completed_ready,
  output logic        cpu_completed_valid,
  output logic [63:0] cpu_completed_hart_id,
  output logic [3:0]  cpu_completed_inst_id,
  output logic [63:0] cpu_completed_data,
  output logic [3:0]  cpu_completed_excp,
  output logic        cpu_forwarding_valid,
  output logic [31:0] cpu_forwarding_inst,
  input  logic        cpu_sync_send_ready,
  output logic        cpu_sync_send_valid,
  output logic        cpu_sync_send_id,
  output logic        cpu_sync_send_cpu,
  output logic        cpu_sync_recv_ready,
  input  logic        cpu_sync_recv_valid,
  input  logic        cpu_sync_recv_id,

  // Existing RVV scalar AII interface.
  output logic        rvv_issue_valid,
  input  logic        rvv_issue_ready,
  output logic [63:0] rvv_issue_hart_id,
  output logic [3:0]  rvv_issue_inst_id,
  output logic [31:0] rvv_issue_inst,
  input  logic        rvv_issue_accept,
  input  logic        rvv_issue_rd_valid_0,
  input  logic        rvv_issue_rd_valid_1,
  input  logic        rvv_issue_frd_valid_0,
  input  logic        rvv_issue_frd_valid_1,
  input  logic        rvv_issue_wb_valid,
  input  logic        rvv_issue_fwb_valid,
  output logic        rvv_register_valid,
  output logic [63:0] rvv_register_hart_id,
  output logic [3:0]  rvv_register_inst_id,
  output logic [63:0] rvv_register_data_0,
  output logic [63:0] rvv_register_data_1,
  output logic [63:0] rvv_register_fdata_0,
  output logic [63:0] rvv_register_fdata_1,
  output logic        rvv_commit_valid,
  output logic [63:0] rvv_commit_hart_id,
  output logic [3:0]  rvv_commit_inst_id,
  output logic        rvv_commit_kill,
  output logic        rvv_completed_ready,
  input  logic        rvv_completed_valid,
  input  logic [63:0] rvv_completed_hart_id,
  input  logic [3:0]  rvv_completed_inst_id,
  input  logic [63:0] rvv_completed_data,
  input  logic [3:0]  rvv_completed_excp,
  input  logic        rvv_forwarding_valid,
  input  logic [31:0] rvv_forwarding_inst,
  output logic        rvv_sync_send_ready,
  input  logic        rvv_sync_send_valid,
  input  logic        rvv_sync_send_id,
  input  logic        rvv_sync_send_cpu,
  input  logic        rvv_sync_recv_ready,
  output logic        rvv_sync_recv_valid,
  output logic        rvv_sync_recv_id,

  // AME v0.5 AXI master (512-bit data, 4-bit ID).
  input  logic        ame_axi_aw_ready,
  output logic        ame_axi_aw_valid,
  output logic [3:0]  ame_axi_aw_id,
  output logic [63:0] ame_axi_aw_addr,
  output logic [7:0]  ame_axi_aw_len,
  output logic [2:0]  ame_axi_aw_size,
  output logic [1:0]  ame_axi_aw_burst,
  output logic        ame_axi_aw_lock,
  output logic [3:0]  ame_axi_aw_cache,
  output logic [2:0]  ame_axi_aw_prot,
  input  logic        ame_axi_w_ready,
  output logic        ame_axi_w_valid,
  output logic [511:0] ame_axi_w_data,
  output logic [63:0] ame_axi_w_strb,
  output logic        ame_axi_w_last,
  input  logic        ame_axi_b_valid,
  input  logic [3:0]  ame_axi_b_id,
  input  logic [1:0]  ame_axi_b_resp,
  output logic        ame_axi_b_ready,
  input  logic        ame_axi_ar_ready,
  output logic        ame_axi_ar_valid,
  output logic [3:0]  ame_axi_ar_id,
  output logic [63:0] ame_axi_ar_addr,
  output logic [7:0]  ame_axi_ar_len,
  output logic [2:0]  ame_axi_ar_size,
  output logic [1:0]  ame_axi_ar_burst,
  output logic        ame_axi_ar_lock,
  output logic [3:0]  ame_axi_ar_cache,
  output logic [2:0]  ame_axi_ar_prot,
  input  logic        ame_axi_r_valid,
  input  logic [3:0]  ame_axi_r_id,
  input  logic [511:0] ame_axi_r_data,
  input  logic [1:0]  ame_axi_r_resp,
  input  logic        ame_axi_r_last,
  output logic        ame_axi_r_ready,
  output logic        ame_has_store
);

  typedef struct packed { logic [3:0] hartId; logic [3:0] instId; logic [31:0] inst; } issue_req_t;
  typedef struct packed { logic accept; logic [1:0] rdValid; logic wbValid; } issue_resp_t;
  typedef struct packed { logic valid; logic [3:0] hartId; logic [3:0] instId; logic [1:0][63:0] data; } register_t;
  typedef struct packed { logic valid; logic [3:0] hartId; logic [3:0] instId; logic kill; } commit_t;
  typedef struct packed { logic [3:0] hartId; logic [3:0] instId; logic [63:0] data; logic [3:0] excp; } completed_t;
  typedef struct packed { logic valid; logic [31:0] inst; } forwarding_t;
  typedef struct packed { logic syncId; logic syncCpu; } sync_send_t;
  typedef struct packed { logic syncId; } sync_recv_t;

  issue_req_t   aicb_master_issue_req, aicb_ame_issue_req, aicb_rvv_issue_req;
  issue_resp_t  aicb_master_issue_resp, aicb_ame_issue_resp, aicb_rvv_issue_resp;
  register_t    aicb_master_register, aicb_ame_register, aicb_rvv_register;
  commit_t      aicb_master_commit, aicb_ame_commit, aicb_rvv_commit;
  completed_t   aicb_master_completed_bits, aicb_ame_completed_bits, aicb_rvv_completed_bits;
  forwarding_t  aicb_master_forwarding, aicb_ame_forwarding, aicb_rvv_forwarding;
  sync_send_t   aicb_master_sync_send_bits, aicb_ame_sync_send_bits, aicb_rvv_sync_send_bits;
  sync_recv_t   aicb_master_sync_recv_bits, aicb_ame_sync_recv_bits, aicb_rvv_sync_recv_bits;

  logic aicb_master_issue_valid;
  logic aicb_master_sync_send_valid, aicb_master_sync_recv_ready;
  logic aicb_master_issue_ready, aicb_master_completed_ready, aicb_master_completed_valid;
  logic aicb_ame_sync_send_valid, aicb_ame_sync_recv_valid;
  logic aicb_rvv_sync_send_valid, aicb_rvv_sync_recv_valid, rvv_sync_send_ready_aicb;
  logic aicb_ame_issue_valid, aicb_rvv_issue_valid;
  logic aicb_ame_completed_ready, aicb_rvv_completed_ready;

  issue_resp_t ame_issue_resp;
  completed_t ame_completed_bits;
  forwarding_t ame_forwarding;
  logic ame_issue_ready, ame_completed_valid;
  logic ame_core_issue_valid, ame_issue_unsupported_sync;
  logic ame_sync_send_ready, ame_sync_send_valid, ame_sync_recv_ready, ame_sync_recv_valid;
  sync_send_t ame_sync_send_bits;
  sync_recv_t ame_sync_recv_bits;

  // AME generated aggregate ports.
  issue_req_t ame_core_issue_req;
  issue_resp_t ame_core_issue_resp;
  register_t ame_core_register;
  commit_t ame_core_commit;
  completed_t ame_core_completed_bits;
  forwarding_t ame_core_forwarding;
  typedef struct packed { logic [3:0] syncId; logic syncCpu; } ame_core_sync_send_t;
  typedef struct packed { logic [3:0] syncId; } ame_core_sync_recv_t;
  ame_core_sync_send_t ame_core_sync_send_bits;
  ame_core_sync_recv_t ame_core_sync_recv_bits;
  logic ame_core_issue_ready, ame_core_completed_valid, ame_core_sync_send_valid;
  logic ame_core_sync_send_ready, ame_core_sync_recv_ready, ame_core_sync_recv_valid;
  logic ame_core_axib_ready, ame_core_axir_ready;
  typedef struct packed { logic [3:0] id; logic [63:0] addr; logic [7:0] len; logic [2:0] size; logic [1:0] burst; logic lock; logic [3:0] cache; logic [2:0] prot; logic [3:0] qos; logic [3:0] region; } ame_aw_t;
  typedef struct packed { logic [511:0] data; logic [63:0] strb; logic last; } ame_w_t;
  typedef struct packed { logic [3:0] id; logic [1:0] resp; } ame_b_t;
  typedef struct packed { logic [3:0] id; logic [63:0] addr; logic [7:0] len; logic [2:0] size; logic [1:0] burst; logic lock; logic [3:0] cache; logic [2:0] prot; logic [3:0] qos; logic [3:0] region; } ame_ar_t;
  typedef struct packed { logic [3:0] id; logic [511:0] data; logic [1:0] resp; logic last; } ame_r_t;
  ame_aw_t ame_axiaw_bits, ame_axiar_bits;
  ame_w_t ame_axiw_bits;
  ame_b_t ame_axib_bits;
  ame_r_t ame_axir_bits;
  logic ame_rvv_spm_in_ready, ame_rvv_spm_out_valid;

  // AII has one CPU-facing ready, but the instruction selects exactly one
  // coprocessor.  Do not let the non-target slave's backpressure reject a
  // request that the target could accept.  RVV uses three opcode families in
  // the current VecTop decoder: OP-V (0x57), vector load (0x07), and vector
  // store (0x27).  Treating only OP-V as RVV lets an AME-ready/RVV-busy cycle
  // report ready with no accepting slave for a vector load/store, which the
  // CPU turns into an illegal-instruction trap.
  // Decode directly from the CPU payload so the target selection is already
  // stable before issue valid asserts. AMECore isolates the payload from its
  // internal decoder while AME issue valid is low.
  wire issue_targets_ame = cpu_issue_inst[6:0] == 7'b1110111;
  wire issue_targets_rvv =
    (cpu_issue_inst[6:0] == 7'b1010111) ||
    (cpu_issue_inst[6:0] == 7'b0000111) ||
    (cpu_issue_inst[6:0] == 7'b0100111);
  // Aicb broadcasts issue valid/req to both slave ports and ORs their ready
  // signals.  Qualify ready at that boundary so only the decoded target can
  // complete the handshake.  Unknown coprocessor opcodes use an always-ready
  // reject path and therefore return accept=0 instead of stalling forever.
  wire aicb_ame_issue_ready_selected = issue_targets_ame && ame_issue_ready;
  wire aicb_rvv_issue_ready_selected = issue_targets_rvv
    ? rvv_issue_ready
    : !issue_targets_ame;
  wire ame_issue_fire = ame_core_issue_valid && ame_core_issue_ready && ame_core_issue_resp.accept;
  wire rvv_issue_fire = rvv_issue_valid && rvv_issue_ready && rvv_issue_accept;
  wire ame_completed_fire = ame_completed_valid && aicb_ame_completed_ready;
  wire ame_kill_fire = aicb_ame_commit.valid && aicb_ame_commit.kill;
  // The first SoC revision has no sync peer. Reject all D/M/V signal and wait
  // encodings at issue instead of accepting an operation that cannot finish.
  assign ame_issue_unsupported_sync =
    !ENABLE_AME ||
    ((aicb_ame_issue_req.inst[31:25] == 7'b1111010 ||
     aicb_ame_issue_req.inst[31:25] == 7'b1111100) &&
    aicb_ame_issue_req.inst[24:20] == 5'b0 &&
    aicb_ame_issue_req.inst[14:12] <= 3'b010 &&
    aicb_ame_issue_req.inst[11:7] == 5'b0 &&
    aicb_ame_issue_req.inst[6:0] == 7'b1110111);

  // Valid must not depend combinationally on ready.  The previous ready-gated
  // valid formed a feedback path through VecTop for some vector loads.
  assign aicb_master_issue_valid = cpu_issue_valid;

  // Keep the decode payload independent of issue valid.  E6 consumes the
  // combinational decoder response as part of the issue handshake, so
  // valid-gating the instruction would create a protocol loop.
  assign aicb_master_issue_req =
    '{hartId: cpu_issue_hart_id[3:0], instId: cpu_issue_inst_id, inst: cpu_issue_inst};
  assign aicb_master_register = '{valid: cpu_register_valid, hartId: cpu_register_hart_id[3:0], instId: cpu_register_inst_id,
                                  data: {{cpu_register_data_1}, {cpu_register_data_0}}};
  assign aicb_master_commit = '{valid: cpu_commit_valid, hartId: cpu_commit_hart_id[3:0], instId: cpu_commit_inst_id, kill: cpu_commit_kill};
  assign cpu_issue_ready = aicb_master_issue_ready;
  // E6 consumes the issue response as a combinational decode result: its
  // issue-valid generation depends on accept/legal.  Aicb intentionally gates
  // its master response with valid && ready, so feeding that response back to
  // E6 creates a zero-delay loop.  Return the opcode-selected slave decoder
  // response directly; Aicb still uses its gated response internally to write
  // the route table only on a real issue handshake.
  assign cpu_issue_accept = aicb_ame_issue_resp.accept | aicb_rvv_issue_resp.accept;
  assign cpu_issue_rd_valid_0 = aicb_ame_issue_resp.rdValid[0] | aicb_rvv_issue_resp.rdValid[0];
  assign cpu_issue_rd_valid_1 = aicb_ame_issue_resp.rdValid[1] | aicb_rvv_issue_resp.rdValid[1];
  assign cpu_issue_wb_valid = aicb_ame_issue_resp.wbValid | aicb_rvv_issue_resp.wbValid;
  assign aicb_master_completed_ready = cpu_completed_ready;
  assign cpu_issue_frd_valid_0 = issue_targets_rvv && rvv_issue_frd_valid_0;
  assign cpu_issue_frd_valid_1 = issue_targets_rvv && rvv_issue_frd_valid_1;
  assign cpu_issue_fwb_valid = issue_targets_rvv && rvv_issue_fwb_valid;
  assign cpu_completed_valid = aicb_master_completed_valid;
  assign cpu_completed_hart_id = {60'b0, aicb_master_completed_bits.hartId};
  assign cpu_completed_inst_id = aicb_master_completed_bits.instId;
  assign cpu_completed_data = aicb_master_completed_bits.data;
  assign cpu_completed_excp = aicb_master_completed_bits.excp;
  assign cpu_forwarding_valid = 1'b0;
  assign cpu_forwarding_inst = 32'b0;
  assign cpu_sync_send_valid = 1'b0;
  assign cpu_sync_send_id = 1'b0;
  assign cpu_sync_send_cpu = 1'b0;
  assign cpu_sync_recv_ready = 1'b0;

  // Keep VecTop's CPU-facing phases at their pre-AME timing. Aicb observes the
  // same issue/register/commit traffic in parallel to route AME and arbitrate
  // completions, but its registered route-table reads must not add latency to
  // VecTop: E6 can reuse a 4-bit tag shortly after a speculative kill.
  assign rvv_issue_valid = cpu_issue_valid && issue_targets_rvv;
  assign rvv_issue_hart_id = cpu_issue_hart_id;
  assign rvv_issue_inst_id = cpu_issue_inst_id;
  assign rvv_issue_inst = cpu_issue_inst;
  assign rvv_register_valid = cpu_register_valid;
  assign rvv_register_hart_id = cpu_register_hart_id;
  assign rvv_register_inst_id = cpu_register_inst_id;
  assign rvv_register_data_0 = cpu_register_data_0;
  assign rvv_register_data_1 = cpu_register_data_1;
  assign rvv_register_fdata_0 = cpu_register_fdata_0;
  assign rvv_register_fdata_1 = cpu_register_fdata_1;
  // Preserve VecTop's pre-AME commit semantics. E6 broadcasts commit/kill for
  // speculative coprocessor entries, including kills for which Aicb may not
  // have a surviving route-table entry. Routing this phase through Aicb drops
  // those kills and leaves stale VecTop issue entries alive until a 4-bit tag
  // is reused. AME still consumes the route-selected Aicb commit below.
  assign rvv_commit_valid = cpu_commit_valid;
  assign rvv_commit_hart_id = cpu_commit_hart_id;
  assign rvv_commit_inst_id = cpu_commit_inst_id;
  assign rvv_commit_kill = cpu_commit_kill;
  assign rvv_completed_ready = aicb_rvv_completed_ready;
  assign rvv_sync_send_ready = 1'b0;
  assign rvv_sync_recv_valid = aicb_rvv_sync_recv_valid;
  assign rvv_sync_recv_id = aicb_rvv_sync_recv_bits.syncId;

  // AICB slave 0 is AME; slave 1 is VecTop/RVV.
  assign aicb_ame_issue_resp = issue_targets_ame ? ame_issue_resp : '0;
  assign aicb_rvv_issue_resp = issue_targets_rvv
    ? '{accept: rvv_issue_accept, rdValid: {rvv_issue_rd_valid_1, rvv_issue_rd_valid_0}, wbValid: rvv_issue_wb_valid}
    : '0;
  assign aicb_ame_completed_bits = ame_completed_bits;
  assign aicb_rvv_completed_bits = '{hartId: rvv_completed_hart_id[3:0], instId: rvv_completed_inst_id,
                                     data: rvv_completed_data, excp: rvv_completed_excp};
  // Neither integrated core uses AICB cross-slave instruction forwarding.
  // Keeping it disabled also prevents the generated Aicb aggregate from
  // forming an RVV response -> AME forwarding -> RVV request combinational
  // loop.  AMECore v0.5 already drives forwarding.valid permanently low.
  assign aicb_ame_forwarding = '0;
  assign aicb_rvv_forwarding = '0;

  Aicb u_aicb (
    .clock(clock), .reset(reset),
    .io_master_issue_valid(aicb_master_issue_valid), .io_master_issue_ready(aicb_master_issue_ready),
    .io_master_issue_req(aicb_master_issue_req), .io_master_issue_resp(aicb_master_issue_resp),
    .io_master_register(aicb_master_register), .io_master_commit(aicb_master_commit),
    .io_master_completed_ready(aicb_master_completed_ready), .io_master_completed_valid(aicb_master_completed_valid),
    .io_master_completed_bits(aicb_master_completed_bits), .io_master_forwarding(aicb_master_forwarding),
    .io_master_syncSend_ready(1'b1), .io_master_syncSend_valid(aicb_master_sync_send_valid),
    .io_master_syncSend_bits(aicb_master_sync_send_bits), .io_master_syncRecv_ready(aicb_master_sync_recv_ready),
    .io_master_syncRecv_valid(1'b0), .io_master_syncRecv_bits('{syncId: 1'b0}),
    .io_slaves_0_issue_valid(aicb_ame_issue_valid), .io_slaves_0_issue_ready(aicb_ame_issue_ready_selected),
    .io_slaves_0_issue_req(aicb_ame_issue_req), .io_slaves_0_issue_resp(aicb_ame_issue_resp),
    .io_slaves_0_register(aicb_ame_register), .io_slaves_0_commit(aicb_ame_commit),
    .io_slaves_0_completed_ready(aicb_ame_completed_ready), .io_slaves_0_completed_valid(ame_completed_valid),
    .io_slaves_0_completed_bits(ame_completed_bits), .io_slaves_0_forwarding(ame_forwarding),
    .io_slaves_0_syncSend_ready(ame_sync_send_ready), .io_slaves_0_syncSend_valid(ame_sync_send_valid),
    .io_slaves_0_syncSend_bits(ame_sync_send_bits), .io_slaves_0_syncRecv_ready(ame_core_sync_recv_ready),
    .io_slaves_0_syncRecv_valid(ame_sync_recv_valid), .io_slaves_0_syncRecv_bits(ame_sync_recv_bits),
    .io_slaves_1_issue_valid(aicb_rvv_issue_valid), .io_slaves_1_issue_ready(aicb_rvv_issue_ready_selected),
    .io_slaves_1_issue_req(aicb_rvv_issue_req), .io_slaves_1_issue_resp(aicb_rvv_issue_resp),
    .io_slaves_1_register(aicb_rvv_register), .io_slaves_1_commit(aicb_rvv_commit),
    .io_slaves_1_completed_ready(aicb_rvv_completed_ready), .io_slaves_1_completed_valid(rvv_completed_valid),
    .io_slaves_1_completed_bits(aicb_rvv_completed_bits), .io_slaves_1_forwarding(aicb_rvv_forwarding),
    .io_slaves_1_syncSend_ready(rvv_sync_send_ready_aicb), .io_slaves_1_syncSend_valid(1'b0),
    .io_slaves_1_syncSend_bits('{syncId: 1'b0, syncCpu: 1'b0}),
    .io_slaves_1_syncRecv_ready(1'b0), .io_slaves_1_syncRecv_valid(aicb_rvv_sync_recv_valid),
    .io_slaves_1_syncRecv_bits(aicb_rvv_sync_recv_bits)
  );

  assign ame_completed_valid = ame_core_completed_valid;
  assign ame_completed_bits = ame_core_completed_bits;
  // Only the selected custom opcode may reach AME. The request remains
  // transparent for an AME opcode even before valid asserts because AII issue
  // responses are combinational; non-AME payloads are isolated here.
  assign ame_core_issue_valid =
    aicb_ame_issue_valid && issue_targets_ame && !ame_issue_unsupported_sync;
  assign ame_issue_ready = ame_issue_unsupported_sync ? 1'b1 : ame_core_issue_ready;
  assign ame_issue_resp = ame_issue_unsupported_sync ? '0 : ame_core_issue_resp;
  assign ame_forwarding = ame_core_forwarding;
  assign ame_sync_send_valid = ame_core_sync_send_valid;
  assign ame_sync_send_bits = '{syncId: ame_core_sync_send_bits.syncId[0], syncCpu: ame_core_sync_send_bits.syncCpu};
  assign ame_core_sync_send_ready = ame_sync_send_ready;

  AMECore u_ame (
    .clock(clock), .reset(reset),
    .io_aii_issue_valid(ame_core_issue_valid), .io_aii_issue_ready(ame_core_issue_ready),
    .io_aii_issue_req(ame_core_issue_req), .io_aii_issue_resp(ame_core_issue_resp),
    .io_aii_register(ame_core_register), .io_aii_commit(ame_core_commit),
    .io_aii_completed_ready(aicb_ame_completed_ready), .io_aii_completed_valid(ame_core_completed_valid),
    .io_aii_completed_bits(ame_core_completed_bits), .io_aii_forwarding(ame_core_forwarding),
    .io_aii_syncSend_ready(ame_core_sync_send_ready), .io_aii_syncSend_valid(ame_core_sync_send_valid),
    .io_aii_syncSend_bits(ame_core_sync_send_bits), .io_aii_syncRecv_ready(ame_core_sync_recv_ready),
    .io_aii_syncRecv_valid(ame_core_sync_recv_valid), .io_aii_syncRecv_bits(ame_core_sync_recv_bits),
    .io_axiaw_ready(ame_axi_aw_ready), .io_axiaw_valid(ame_axi_aw_valid), .io_axiaw_bits(ame_axiaw_bits),
    .io_axiw_ready(ame_axi_w_ready), .io_axiw_valid(ame_axi_w_valid), .io_axiw_bits(ame_axiw_bits),
    .io_axib_ready(ame_core_axib_ready), .io_axib_valid(ame_axi_b_valid), .io_axib_bits(ame_axib_bits),
    .io_axiar_ready(ame_axi_ar_ready), .io_axiar_valid(ame_axi_ar_valid), .io_axiar_bits(ame_axiar_bits),
    .io_axir_ready(ame_core_axir_ready), .io_axir_valid(ame_axi_r_valid), .io_axir_bits(ame_axir_bits),
    .io_rvvSpmInput_ready(ame_rvv_spm_in_ready), .io_rvvSpmInput_valid(1'b0), .io_rvvSpmInput_bits('0),
    .io_rvvSpmOutput_ready(1'b1), .io_rvvSpmOutput_valid(ame_rvv_spm_out_valid), .io_rvvSpmOutput_bits(),
    .io_debug()
  );
  assign ame_core_issue_req = issue_targets_ame ? aicb_ame_issue_req : '0;
  assign ame_core_register = aicb_ame_register;
  assign ame_core_commit = aicb_ame_commit;
  assign ame_core_sync_recv_valid = ame_sync_recv_valid;
  assign ame_core_sync_recv_bits = '{syncId: {{3{1'b0}}, ame_sync_recv_bits.syncId}};
  assign ame_axi_aw_id = ame_axiaw_bits.id; assign ame_axi_aw_addr = ame_axiaw_bits.addr;
  assign ame_axi_aw_len = ame_axiaw_bits.len; assign ame_axi_aw_size = ame_axiaw_bits.size;
  assign ame_axi_aw_burst = ame_axiaw_bits.burst; assign ame_axi_aw_lock = ame_axiaw_bits.lock;
  assign ame_axi_aw_cache = ame_axiaw_bits.cache; assign ame_axi_aw_prot = ame_axiaw_bits.prot;
  assign ame_axi_w_data = ame_axiw_bits.data; assign ame_axi_w_strb = ame_axiw_bits.strb; assign ame_axi_w_last = ame_axiw_bits.last;
  assign ame_axib_bits = '{id: ame_axi_b_id, resp: ame_axi_b_resp};
  assign ame_axi_b_ready = ame_core_axib_ready;
  assign ame_axi_ar_id = ame_axiar_bits.id; assign ame_axi_ar_addr = ame_axiar_bits.addr;
  assign ame_axi_ar_len = ame_axiar_bits.len; assign ame_axi_ar_size = ame_axiar_bits.size;
  assign ame_axi_ar_burst = ame_axiar_bits.burst; assign ame_axi_ar_lock = ame_axiar_bits.lock;
  assign ame_axi_ar_cache = ame_axiar_bits.cache; assign ame_axi_ar_prot = ame_axiar_bits.prot;
  assign ame_axir_bits = '{id: ame_axi_r_id, data: ame_axi_r_data, resp: ame_axi_r_resp, last: ame_axi_r_last};
  assign ame_axi_r_ready = ame_core_axir_ready;

  // Keep the CPU-side store-empty indication conservative from AME issue
  // acceptance through completion/kill. The AXI counter separately covers
  // writes whose B response has not yet been consumed.
  // This count tracks accepted AME AII operations until completion/kill.  It
  // is deliberately wider than instId (which is only a tag) so a burst of
  // independent operations cannot wrap the CPU's store-empty indication.
  localparam int AME_ACTIVITY_COUNT_W = 16;
  logic [AME_ACTIVITY_COUNT_W-1:0] ame_activity_count;
  logic [7:0] ame_store_count;
  wire ame_aw_fire = ame_axi_aw_valid && ame_axi_aw_ready;
  wire ame_b_fire  = ame_axi_b_valid && ame_axi_b_ready;
  always_ff @(posedge clock) begin
    if (reset) begin
      ame_activity_count <= '0;
    end else begin
      case ({ame_issue_fire, ame_completed_fire, ame_kill_fire})
        3'b100: if (ame_activity_count != {AME_ACTIVITY_COUNT_W{1'b1}})
                  ame_activity_count <= ame_activity_count + 1'b1;
        3'b010, 3'b001:
                if (ame_activity_count != '0)
                  ame_activity_count <= ame_activity_count - 1'b1;
        3'b011: if (ame_activity_count >= 2)
                  ame_activity_count <= ame_activity_count - 2;
                else
                  ame_activity_count <= '0;
        3'b110, 3'b101, 3'b111, 3'b000:
                ame_activity_count <= ame_activity_count;
        default: ame_activity_count <= ame_activity_count;
      endcase
    end
  end
  always_ff @(posedge clock) begin
    if (reset) ame_store_count <= 8'h0;
    else case ({ame_aw_fire, ame_b_fire})
      2'b10: ame_store_count <= ame_store_count + 8'h1;
      2'b01: if (ame_store_count != 0) ame_store_count <= ame_store_count - 8'h1;
      default: ame_store_count <= ame_store_count;
    endcase
  end
  assign ame_has_store = |ame_activity_count || |ame_store_count;

  `ifndef SYNTHESIS
  always_ff @(posedge clock) begin
    if (!reset && ame_b_fire) assert (ame_store_count != 0 || ame_aw_fire)
      else $error("AME AXI B response without an outstanding write");
    if (!reset && ame_completed_fire)
      assert (ame_activity_count != 0 || ame_issue_fire)
        else $error("AME completion without an outstanding issue");
    if (!reset && ame_kill_fire)
      assert (ame_activity_count != 0 || ame_issue_fire)
        else $error("AME kill without an outstanding issue");
    if (!reset && aicb_master_issue_valid && aicb_master_issue_ready)
      assert (!(ame_issue_fire && rvv_issue_fire))
        else $error("AME and RVV accepted the same AII instruction");
    if (!reset && cpu_issue_valid) assert (cpu_issue_hart_id[63:4] == 0)
      else $error("CPU AII issue hart ID exceeds AME/AICB 4-bit width");
    if (!reset && cpu_register_valid) assert (cpu_register_hart_id[63:4] == 0)
      else $error("CPU AII register hart ID exceeds AME/AICB 4-bit width");
    if (!reset && cpu_commit_valid) assert (cpu_commit_hart_id[63:4] == 0)
      else $error("CPU AII commit hart ID exceeds AME/AICB 4-bit width");
    if (!reset && rvv_completed_valid) assert (rvv_completed_hart_id[63:4] == 0)
      else $error("RVV AII completed hart ID exceeds AME/AICB 4-bit width");
  end
  `endif
endmodule
