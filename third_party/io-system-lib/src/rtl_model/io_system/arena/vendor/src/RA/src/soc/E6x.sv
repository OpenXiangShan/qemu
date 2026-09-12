// Copyright 2017-2019 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License"); you may not use this file except in
// compliance with the License.  You may obtain a copy of the License at
// http://solderpad.org/licenses/SHL-0.51. Unless required by applicable law
// or agreed to in writing, software, hardware and materials distributed under
// this License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.
//
// Author: Florian Zaruba, ETH Zurich
// Date: 19.03.2017
// Description: Ariane Top-level module
`include "cvxif_types.svh"


module E6x #(
  // AME is enabled by default. Integrators without an AME AXI user port may
  // disable its instruction path explicitly while retaining RVV operation.
  parameter bit AME_ENABLE = 1'b1,
  parameter type rvfi_probes_t = struct packed {
    logic csr;
    logic instr;
  },
  /////////////////////////////////////
  // AXI-Lite-512 types
  /////////////////////////////////////
  parameter type lite_req_512_t = ariane_axi_lite_512::lite_req_t,
  parameter type lite_resp_512_t = ariane_axi_lite_512::lite_resp_t,
  /////////////////////////////////////
  // AXI-512 types
  /////////////////////////////////////
  parameter type req_512_t = ariane_axi_512::req_t,
  parameter type resp_512_t = ariane_axi_512::resp_t

) (
  input  logic                         clk_i,
  input  logic                         rst_ni,

  //[31:13]:Start Addr [12:9]:Reserve [8]Wait(nouse)  [7]:Reserve [6:2]Size [0]:Eanbel
  input logic [50:0]                config_itcm_startAddr_i,
  input logic [4:0]                 config_itcm_size_i,
  input logic [50:0]                config_dtcm_startAddr_i,
  input logic [4:0]                 config_dtcm_size_i,
  //[31:12]:Start Addr [11:7]:Reserve [6:2]Size [0]:Enable
  input logic [51:0]                config_llpp_startAddr_i,
  input logic [4:0]                 config_llpp_size_i,
  //[31:27]:ADDR [26:7]:Reserve [6:2]:Size [0]:Enable
  input logic [36:0]                config_flash_startAddr_i,
  input logic [4:0]                 config_flash_size_i,

  //@pengzhuo 20250821 add the timeout port
  input  [1:0]   io_cluster_utid_rs_i, 
  input  [1:0]   io_cpu_i, 
  input  [7:0]   io_timeout_limitby_xx_i, 

  //@penghzuo 20260302 add the external irq port
  input  logic [31:0]                  external_irq_i,

  input  logic                         rtc_i,

  /*
      Debug interface
  */
  // ports added by the soc team
  input logic                       jtag_tck_i    , // JTAG clk
  input logic                       jtag_tms_i    , // JTAG TMS
  input logic                       jtag_tdi_i    , // JTAG tdi
  input logic                       jtag_trst_n_i , // JTAG Reset
  output logic                      jtag_tdo_o    , // JTAG TDO

  // Core ID, Cluster ID and boot address are considered more or less static
  input  logic [63:0]               boot_addr_i,  // reset boot address
  input  logic [63:0]               hart_id_i,    // hart id in a multicore environment (reflected in a CSR)

  // Interrupt inputs
  input  logic [1:0]                   irq_i,        // level sensitive IR lines, mip & sip (async)
  input  logic                         ipi_i,        // inter-processor interrupts (async)
  // Timer facilities
  input  logic                         time_irq_i,   // timer interrupt in (async)
  input  logic                         debug_req_i,  // debug request (async)

  // RISC-V formal interface port (`rvfi`):
  // Can be left open when formal tracing is not needed.
  output rvfi_probes_t rvfi_probes_o,
  // memory side
//  output noc_req_t                     noc_req_o,
//  input  noc_resp_t                    noc_resp_i,

  input          io_axim_aw_ready_i,
  output         io_axim_aw_valid_o,
  output [3:0]   io_axim_aw_bits_id_o,
  output [63:0]  io_axim_aw_bits_addr_o,
  output [7:0]   io_axim_aw_bits_len_o,
  output [2:0]   io_axim_aw_bits_size_o,
  output [1:0]   io_axim_aw_bits_burst_o,
  output         io_axim_aw_bits_lock_o,
  output [3:0]   io_axim_aw_bits_cache_o,
  output [2:0]   io_axim_aw_bits_prot_o,
  input          io_axim_w_ready_i,
  output         io_axim_w_valid_o,
  output [3:0]   io_axim_w_bits_id_o,
  output [511:0] io_axim_w_bits_data_o,
  output [63:0]  io_axim_w_bits_strb_o,
  output         io_axim_w_bits_last_o,
  output         io_axim_b_ready_o,
  input          io_axim_b_valid_i,
  input  [1:0]   io_axim_b_bits_resp_i,
  input  [3:0]   io_axim_b_bits_id_i,
  input          io_axim_ar_ready_i,
  output         io_axim_ar_valid_o,
  output [3:0]   io_axim_ar_bits_id_o,
  output [63:0]  io_axim_ar_bits_addr_o,
  output [7:0]   io_axim_ar_bits_len_o,
  output [2:0]   io_axim_ar_bits_size_o,
  output [1:0]   io_axim_ar_bits_burst_o,
  output         io_axim_ar_bits_lock_o,
  output [3:0]   io_axim_ar_bits_cache_o,
  output [2:0]   io_axim_ar_bits_prot_o,
  output         io_axim_r_ready_o,
  input          io_axim_r_valid_i,
  input  [3:0]   io_axim_r_bits_id_i,
  input  [511:0] io_axim_r_bits_data_i,
  input  [1:0]   io_axim_r_bits_resp_i,
  input          io_axim_r_bits_last_i,

  input          io_flash_ar_ready_i,
  output         io_flash_ar_valid_o,
  output         io_flash_ar_bits_id_o,
  output [63:0]  io_flash_ar_bits_addr_o,
  output [7:0]   io_flash_ar_bits_len_o,
  output [2:0]   io_flash_ar_bits_size_o,
  output [1:0]   io_flash_ar_bits_burst_o,
  output         io_flash_ar_bits_lock_o,
  output [3:0]   io_flash_ar_bits_cache_o,
  output [2:0]   io_flash_ar_bits_prot_o,
  output         io_flash_r_ready_o,
  input          io_flash_r_valid_i,
  input          io_flash_r_bits_id_i,
  input  [511:0] io_flash_r_bits_data_i,
  input  [1:0]   io_flash_r_bits_resp_i,
  input          io_flash_r_bits_last_i,

  input          io_llpp_aw_ready_i,
  output         io_llpp_aw_valid_o,
  output         io_llpp_aw_bits_id_o,
  output [63:0]  io_llpp_aw_bits_addr_o,
  output [7:0]   io_llpp_aw_bits_len_o,
  output [2:0]   io_llpp_aw_bits_size_o,
  output [1:0]   io_llpp_aw_bits_burst_o,
  output         io_llpp_aw_bits_lock_o,
  output [3:0]   io_llpp_aw_bits_cache_o,
  output [2:0]   io_llpp_aw_bits_prot_o,
  input          io_llpp_w_ready_i,
  output         io_llpp_w_valid_o,
  output         io_llpp_w_bits_id_o,
  output [63:0]  io_llpp_w_bits_data_o,
  output [7:0]   io_llpp_w_bits_strb_o,
  output         io_llpp_w_bits_last_o,
  output         io_llpp_b_ready_o,
  input          io_llpp_b_valid_i,
  input  [1:0]   io_llpp_b_bits_resp_i,
  input          io_llpp_b_bits_id_i,
  input          io_llpp_ar_ready_i,
  output         io_llpp_ar_valid_o,
  output         io_llpp_ar_bits_id_o,
  output [63:0]  io_llpp_ar_bits_addr_o,
  output [7:0]   io_llpp_ar_bits_len_o,
  output [2:0]   io_llpp_ar_bits_size_o,
  output [1:0]   io_llpp_ar_bits_burst_o,
  output         io_llpp_ar_bits_lock_o,
  output [3:0]   io_llpp_ar_bits_cache_o,
  output [2:0]   io_llpp_ar_bits_prot_o,
  output         io_llpp_r_ready_o,
  input          io_llpp_r_valid_i,
  input          io_llpp_r_bits_id_i,
  input  [63:0]  io_llpp_r_bits_data_i,
  input  [1:0]   io_llpp_r_bits_resp_i,
  input          io_llpp_r_bits_last_i,

  input          io_mmu_axi_ar_req_o_ready,
  output         io_mmu_axi_ar_req_o_valid,
  output [3:0]   io_mmu_axi_ar_req_o_bits_id,
  output [63:0]  io_mmu_axi_ar_req_o_bits_addr,
  output [7:0]   io_mmu_axi_ar_req_o_bits_len,
  output [2:0]   io_mmu_axi_ar_req_o_bits_size,
  output [1:0]   io_mmu_axi_ar_req_o_bits_burst,
                 io_mmu_axi_ar_req_o_bits_lock,
  output [3:0]   io_mmu_axi_ar_req_o_bits_cache,
  output [2:0]   io_mmu_axi_ar_req_o_bits_prot,
  output [3:0]   io_mmu_axi_ar_req_o_bits_qos,
                 io_mmu_axi_ar_req_o_bits_region,
  output         io_mmu_axi_r_resp_i_ready,
  input          io_mmu_axi_r_resp_i_valid,
  input  [3:0]   io_mmu_axi_r_resp_i_bits_id,
  input  [511:0] io_mmu_axi_r_resp_i_bits_data,
  input  [1:0]   io_mmu_axi_r_resp_i_bits_resp,
  input          io_mmu_axi_r_resp_i_bits_last,


  input          io_icache_ar_ready_i,
  output         io_icache_ar_valid_o,
  output         io_icache_ar_bits_id_o,
  output [63:0]  io_icache_ar_bits_addr_o,
  output [7:0]   io_icache_ar_bits_len_o,
  output [2:0]   io_icache_ar_bits_size_o,
  output [1:0]   io_icache_ar_bits_burst_o,
  output         io_icache_ar_bits_lock_o,
  output [3:0]   io_icache_ar_bits_cache_o,
  output [2:0]   io_icache_ar_bits_prot_o,
  output         io_icache_r_ready_o,
  input          io_icache_r_valid_i,
  input          io_icache_r_bits_id_i,
  input  [511:0] io_icache_r_bits_data_i,
  input  [1:0]   io_icache_r_bits_resp_i,
  input          io_icache_r_bits_last_i,

  output          io_axis_aw_ready_o,
  input           io_axis_aw_valid_i,
  input   [7:0]   io_axis_aw_bits_id_i,
  input   [63:0]  io_axis_aw_bits_addr_i,
  input   [7:0]   io_axis_aw_bits_len_i,
  input   [2:0]   io_axis_aw_bits_size_i,
  input   [1:0]   io_axis_aw_bits_burst_i,
  input           io_axis_aw_bits_lock_i,
  input   [3:0]   io_axis_aw_bits_cache_i,
  input   [2:0]   io_axis_aw_bits_prot_i,
  output          io_axis_w_ready_o,
  input           io_axis_w_valid_i,
  input   [7:0]   io_axis_w_bits_id_i,
  input   [511:0] io_axis_w_bits_data_i,
  input   [63:0]  io_axis_w_bits_strb_i,
  input           io_axis_w_bits_last_i,
  input           io_axis_b_ready_i,
  output          io_axis_b_valid_o,
  output  [1:0]   io_axis_b_bits_resp_o,
  output  [7:0]   io_axis_b_bits_id_o,
  output          io_axis_ar_ready_o,
  input           io_axis_ar_valid_i,
  input   [7:0]   io_axis_ar_bits_id_i,
  input   [63:0]  io_axis_ar_bits_addr_i,
  input   [7:0]   io_axis_ar_bits_len_i,
  input   [2:0]   io_axis_ar_bits_size_i,
  input   [1:0]   io_axis_ar_bits_burst_i,
  input           io_axis_ar_bits_lock_i,
  input   [3:0]   io_axis_ar_bits_cache_i,
  input   [2:0]   io_axis_ar_bits_prot_i,
  input           io_axis_r_ready_i,
  output          io_axis_r_valid_o,
  output  [7:0]   io_axis_r_bits_id_o,
  output  [511:0] io_axis_r_bits_data_o,
  output  [1:0]   io_axis_r_bits_resp_o,
  output          io_axis_r_bits_last_o,

  input          io_coprocessor_aw_ready_i,
  output         io_coprocessor_aw_valid_o,
  output [9:0]   io_coprocessor_aw_bits_id_o,
  output [63:0]  io_coprocessor_aw_bits_addr_o,
  output [7:0]   io_coprocessor_aw_bits_len_o,
  output [2:0]   io_coprocessor_aw_bits_size_o,
  output [1:0]   io_coprocessor_aw_bits_burst_o,
  output         io_coprocessor_aw_bits_lock_o,
  output [3:0]   io_coprocessor_aw_bits_cache_o,
  output [2:0]   io_coprocessor_aw_bits_prot_o,
  input          io_coprocessor_w_ready_i,
  output         io_coprocessor_w_valid_o,
  output [9:0]   io_coprocessor_w_bits_id_o,
  output [511:0] io_coprocessor_w_bits_data_o,
  output [63:0]  io_coprocessor_w_bits_strb_o,
  output         io_coprocessor_w_bits_last_o,
  output         io_coprocessor_b_ready_o,
  input          io_coprocessor_b_valid_i,
  input  [1:0]   io_coprocessor_b_bits_resp_i,
  input  [9:0]   io_coprocessor_b_bits_id_i,
  input          io_coprocessor_ar_ready_i,
  output         io_coprocessor_ar_valid_o,
  output [9:0]   io_coprocessor_ar_bits_id_o,
  output [63:0]  io_coprocessor_ar_bits_addr_o,
  output [7:0]   io_coprocessor_ar_bits_len_o,
  output [2:0]   io_coprocessor_ar_bits_size_o,
  output [1:0]   io_coprocessor_ar_bits_burst_o,
  output         io_coprocessor_ar_bits_lock_o,
  output [3:0]   io_coprocessor_ar_bits_cache_o,
  output [2:0]   io_coprocessor_ar_bits_prot_o,
  output         io_coprocessor_r_ready_o,
  input          io_coprocessor_r_valid_i,
  input  [9:0]   io_coprocessor_r_bits_id_i,
  input  [511:0] io_coprocessor_r_bits_data_i,
  input  [1:0]   io_coprocessor_r_bits_resp_i,
  input          io_coprocessor_r_bits_last_i,

  // AMECore DMA AXI master (kept independent from the RVV master above).
  input          io_ame_aw_ready_i,
  output         io_ame_aw_valid_o,
  output [3:0]   io_ame_aw_bits_id_o,
  output [63:0]  io_ame_aw_bits_addr_o,
  output [7:0]   io_ame_aw_bits_len_o,
  output [2:0]   io_ame_aw_bits_size_o,
  output [1:0]   io_ame_aw_bits_burst_o,
  output         io_ame_aw_bits_lock_o,
  output [3:0]   io_ame_aw_bits_cache_o,
  output [2:0]   io_ame_aw_bits_prot_o,
  input          io_ame_w_ready_i,
  output         io_ame_w_valid_o,
  output [511:0] io_ame_w_bits_data_o,
  output [63:0]  io_ame_w_bits_strb_o,
  output         io_ame_w_bits_last_o,
  output         io_ame_b_ready_o,
  input          io_ame_b_valid_i,
  input  [1:0]   io_ame_b_bits_resp_i,
  input  [3:0]   io_ame_b_bits_id_i,
  input          io_ame_ar_ready_i,
  output         io_ame_ar_valid_o,
  output [3:0]   io_ame_ar_bits_id_o,
  output [63:0]  io_ame_ar_bits_addr_o,
  output [7:0]   io_ame_ar_bits_len_o,
  output [2:0]   io_ame_ar_bits_size_o,
  output [1:0]   io_ame_ar_bits_burst_o,
  output         io_ame_ar_bits_lock_o,
  output [3:0]   io_ame_ar_bits_cache_o,
  output [2:0]   io_ame_ar_bits_prot_o,
  output         io_ame_r_ready_o,
  input          io_ame_r_valid_i,
  input  [3:0]   io_ame_r_bits_id_i,
  input  [511:0] io_ame_r_bits_data_i,
  input  [1:0]   io_ame_r_bits_resp_i,
  input          io_ame_r_bits_last_i,
  output         io_ame_hasStore_o

);
  //Core
  logic          io_coprecessor_issue_req_ready;
  logic          io_coprecessor_issue_req_valid;
  logic [63:0]   io_coprecessor_issue_req_bits_hart_id;
  logic [3:0]    io_coprecessor_issue_req_bits_inst_id;
  logic [31:0]   io_coprecessor_issue_req_bits_inst;
  logic          io_coprecessor_issue_resp_accept;
  logic          io_coprecessor_issue_resp_rd_valid_0;
  logic          io_coprecessor_issue_resp_rd_valid_1;
  logic          io_coprecessor_issue_resp_frd_valid_0;
  logic          io_coprecessor_issue_resp_frd_valid_1;
  logic          io_coprecessor_issue_resp_wb_valid;
  logic          io_coprecessor_issue_resp_fwb_valid;
  logic          io_coprecessor_register_valid;
  logic [63:0]   io_coprecessor_register_bits_hart_id;
  logic [3:0]    io_coprecessor_register_bits_inst_id;
  logic [63:0]   io_coprecessor_register_bits_data_0;
  logic [63:0]   io_coprecessor_register_bits_data_1;
  logic [63:0]   io_coprecessor_register_bits_fdata_0;
  logic [63:0]   io_coprecessor_register_bits_fdata_1;
  logic          io_coprecessor_commit_valid;
  logic [63:0]   io_coprecessor_commit_bits_hart_id;
  logic [3:0]    io_coprecessor_commit_bits_inst_id;
  logic          io_coprecessor_commit_bits_kill;
  logic          io_coprecessor_completed_ready;
  logic          io_coprecessor_completed_valid;
  logic  [63:0]  io_coprecessor_completed_bits_hart_id;
  logic  [3:0]   io_coprecessor_completed_bits_inst_id;
  logic  [63:0]  io_coprecessor_completed_bits_data;
  logic  [3:0]   io_coprecessor_completed_bits_excp;

  logic          io_coprecessor_forwarding_valid;
  logic  [31:0]  io_coprecessor_forwarding_bits_inst;
  logic          io_coprecessor_syncSend_valid;
  logic          io_coprecessor_syncSend_bits_syncId;
  logic          io_coprecessor_syncSend_bits_syncCpu;
  logic          io_coprecessor_syncRecv_ready;

  logic          io_coprocessor_hasStore;
  logic          coprocessor_store_empty;

  assign coprocessor_store_empty =
    !(io_coprocessor_hasStore || io_ame_hasStore_o);

`ifndef SYNTHESIS
  always_ff @(posedge clk_i) begin
    if (rst_ni) assert (!$isunknown(coprocessor_store_empty))
      else $error("Coprocessor store-empty indication is unknown");
  end
`endif

  // RVV AII slave side of the SoC-local AICB.
  logic          rvv_issue_valid, rvv_issue_ready;
  logic [63:0]   rvv_issue_hart_id;
  logic [3:0]    rvv_issue_inst_id;
  logic [31:0]   rvv_issue_inst;
  logic          rvv_issue_accept, rvv_issue_rd_valid_0, rvv_issue_rd_valid_1;
  logic          rvv_issue_frd_valid_0, rvv_issue_frd_valid_1;
  logic          rvv_issue_wb_valid, rvv_issue_fwb_valid;
  logic          rvv_register_valid;
  logic [63:0]   rvv_register_hart_id;
  logic [3:0]    rvv_register_inst_id;
  logic [63:0]   rvv_register_data_0, rvv_register_data_1;
  logic [63:0]   rvv_register_fdata_0, rvv_register_fdata_1;
  logic          rvv_commit_valid;
  logic [63:0]   rvv_commit_hart_id;
  logic [3:0]    rvv_commit_inst_id;
  logic          rvv_commit_kill;
  logic          rvv_completed_ready, rvv_completed_valid;
  logic [63:0]   rvv_completed_hart_id, rvv_completed_data;
  logic [3:0]    rvv_completed_inst_id, rvv_completed_excp;

  //AXIM
  logic        axim_aw_ready_i;
  logic        axim_aw_valid_o;
  logic [3:0]  axim_aw_id_o;
  logic [63:0] axim_aw_addr_o;
  logic [7:0]  axim_aw_len_o;
  logic [2:0]  axim_aw_size_o;
  logic [1:0]  axim_aw_burst_o;
  logic        axim_aw_lock_o;
  logic [3:0]  axim_aw_cache_o;
  logic [2:0]  axim_aw_prot_o;

  logic        axim_w_ready_i;
  logic        axim_w_valid_o;
  logic [3:0]  axim_w_id_o;
  logic [63:0] axim_w_data_o;
  logic [7:0]  axim_w_strb_o;
  logic        axim_w_last_o;

  logic        axim_b_ready_o;
  logic        axim_b_valid_i;
  logic [1:0]  axim_b_resp_i;
  logic [3:0]  axim_b_id_i;

  logic        axim_ar_ready_i;
  logic        axim_ar_valid_o;
  logic [3:0]  axim_ar_id_o;
  logic [63:0] axim_ar_addr_o;
  logic [7:0]  axim_ar_len_o;
  logic [2:0]  axim_ar_size_o;
  logic [1:0]  axim_ar_burst_o;
  logic        axim_ar_lock_o;
  logic [3:0]  axim_ar_cache_o;
  logic [2:0]  axim_ar_prot_o;

  logic        axim_r_ready_o;
  logic        axim_r_valid_i;
  logic [3:0]  axim_r_id_i;
  logic [63:0] axim_r_data_i;
  logic [1:0]  axim_r_resp_i;
  logic        axim_r_last_i;

 //AXIM_S
  logic         axim_s_aw_ready_i;
  logic         axim_s_aw_valid_o;
  logic [3:0]   axim_s_aw_id_o;
  logic [63:0]  axim_s_aw_addr_o;
  logic [7:0]   axim_s_aw_len_o;
  logic [2:0]   axim_s_aw_size_o;
  logic [1:0]   axim_s_aw_burst_o;
  logic         axim_s_aw_lock_o;
  logic [3:0]   axim_s_aw_cache_o;
  logic [2:0]   axim_s_aw_prot_o;
               
  logic         axim_s_w_ready_i;
  logic         axim_s_w_valid_o;
  logic [3:0]   axim_s_w_id_o;
  logic [511:0] axim_s_w_data_o;
  logic [63:0]  axim_s_w_strb_o;
  logic         axim_s_w_last_o;
               
  logic         axim_s_b_ready_o;
  logic         axim_s_b_valid_i;
  logic [1:0]   axim_s_b_resp_i;
  logic [3:0]   axim_s_b_id_i;
               
  logic         axim_s_ar_ready_i;
  logic         axim_s_ar_valid_o;
  logic [3:0]   axim_s_ar_id_o;
  logic [63:0]  axim_s_ar_addr_o;
  logic [7:0]   axim_s_ar_len_o;
  logic [2:0]   axim_s_ar_size_o;
  logic [1:0]   axim_s_ar_burst_o;
  logic         axim_s_ar_lock_o;
  logic [3:0]   axim_s_ar_cache_o;
  logic [2:0]   axim_s_ar_prot_o;
               
  logic         axim_s_r_ready_o;
  logic         axim_s_r_valid_i;
  logic [3:0]   axim_s_r_id_i;
  logic [511:0] axim_s_r_data_i;
  logic [1:0]   axim_s_r_resp_i;
  logic         axim_s_r_last_i;

  //FLASH
  logic        flash_ar_ready_i;
  logic        flash_ar_valid_o;
  logic        flash_ar_id_o;
  logic [63:0] flash_ar_addr_o;
  logic [7:0]  flash_ar_len_o;
  logic [2:0]  flash_ar_size_o;
  logic [1:0]  flash_ar_burst_o;
  logic        flash_ar_lock_o;
  logic [3:0]  flash_ar_cache_o;
  logic [2:0]  flash_ar_prot_o;

  logic        flash_r_ready_o;
  logic        flash_r_valid_i;
  logic        flash_r_id_i;
  logic [63:0] flash_r_data_i;
  logic [1:0]  flash_r_resp_i;
  logic        flash_r_last_i;

  logic        flash_s_ar_ready_i;
  logic        flash_s_ar_valid_o;
  logic        flash_s_ar_id_o;
  logic [63:0] flash_s_ar_addr_o;
  logic [7:0]  flash_s_ar_len_o;
  logic [2:0]  flash_s_ar_size_o;
  logic [1:0]  flash_s_ar_burst_o;
  logic        flash_s_ar_lock_o;
  logic [3:0]  flash_s_ar_cache_o;
  logic [2:0]  flash_s_ar_prot_o;

  logic        flash_s_r_ready_o;
  logic        flash_s_r_valid_i;
  logic        flash_s_r_id_i;
  logic [63:0] flash_s_r_data_i;
  logic [1:0]  flash_s_r_resp_i;
  logic        flash_s_r_last_i;

  //AXIM
  logic        llpp_aw_ready_i;
  logic        llpp_aw_valid_o;
  logic [3:0]  llpp_aw_id_o;
  logic [63:0] llpp_aw_addr_o;
  logic [7:0]  llpp_aw_len_o;
  logic [2:0]  llpp_aw_size_o;
  logic [1:0]  llpp_aw_burst_o;
  logic        llpp_aw_lock_o;
  logic [3:0]  llpp_aw_cache_o;
  logic [2:0]  llpp_aw_prot_o;

  logic        llpp_w_ready_i;
  logic        llpp_w_valid_o;
  logic [3:0]  llpp_w_id_o;
  logic [63:0] llpp_w_data_o;
  logic [7:0]  llpp_w_strb_o;
  logic        llpp_w_last_o;

  logic        llpp_b_ready_o;
  logic        llpp_b_valid_i;
  logic [1:0]  llpp_b_resp_i;
  logic [3:0]  llpp_b_id_i;

  logic        llpp_ar_ready_i;
  logic        llpp_ar_valid_o;
  logic [3:0]  llpp_ar_id_o;
  logic [63:0] llpp_ar_addr_o;
  logic [7:0]  llpp_ar_len_o;
  logic [2:0]  llpp_ar_size_o;
  logic [1:0]  llpp_ar_burst_o;
  logic        llpp_ar_lock_o;
  logic [3:0]  llpp_ar_cache_o;
  logic [2:0]  llpp_ar_prot_o;

  logic        llpp_r_ready_o;
  logic        llpp_r_valid_i;
  logic [3:0]  llpp_r_id_i;
  logic [63:0] llpp_r_data_i;
  logic [1:0]  llpp_r_resp_i;
  logic        llpp_r_last_i;

  logic          axis_aw_ready_o;
  logic          axis_aw_valid_i;
  logic  [7:0]   axis_aw_id_i;
  logic  [63:0]  axis_aw_addr_i;
  logic  [7:0]   axis_aw_len_i;
  logic  [2:0]   axis_aw_size_i;
  logic  [1:0]   axis_aw_burst_i;
  logic          axis_aw_lock_i;
  logic  [3:0]   axis_aw_cache_i;
  logic  [2:0]   axis_aw_prot_i;
  logic          axis_w_ready_o;
  logic          axis_w_valid_i;
  logic  [7:0]   axis_w_id_i;
  logic  [63:0]  axis_w_data_i;
  logic  [7:0]   axis_w_strb_i;
  logic          axis_w_last_i;
  logic          axis_b_ready_i;
  logic          axis_b_valid_o;
  logic  [1:0]   axis_b_resp_o;
  logic  [7:0]   axis_b_id_o;
  logic          axis_ar_ready_o;
  logic          axis_ar_valid_i;
  logic  [7:0]   axis_ar_id_i;
  logic  [63:0]  axis_ar_addr_i;
  logic  [7:0]   axis_ar_len_i;
  logic  [2:0]   axis_ar_size_i;
  logic  [1:0]   axis_ar_burst_i;
  logic          axis_ar_lock_i;
  logic  [3:0]   axis_ar_cache_i;
  logic  [2:0]   axis_ar_prot_i;
  logic          axis_r_ready_i;
  logic          axis_r_valid_o;
  logic  [7:0]   axis_r_id_o;
  logic  [63:0]  axis_r_data_o;
  logic  [1:0]   axis_r_resp_o;
  logic          axis_r_last_o;

  logic         axi_ar_ready;
  logic         axi_ar_valid;
  logic         axi_ar_bits_id;
  logic [47:0]  axi_ar_bits_addr;
  logic [7:0]   axi_ar_bits_len;
  logic [2:0]   axi_ar_bits_size;
  logic [1:0]   axi_ar_bits_burst;
  logic         axi_ar_bits_lock;
  logic [3:0]   axi_ar_bits_cache;
  logic [2:0]   axi_ar_bits_prot;
  logic         axi_r_ready;
  logic         axi_r_valid;
  logic         axi_r_bits_id;
  logic [255:0] axi_r_bits_data;
  logic [1:0]   axi_r_bits_resp;
  logic         axi_r_bits_last;


  logic         mmu_axi_ar_req_o_ready;
  logic         mmu_axi_ar_req_o_valid;
  logic [3:0]   mmu_axi_ar_req_o_bits_id;
  logic [63:0]  mmu_axi_ar_req_o_bits_addr;
  logic [7:0]   mmu_axi_ar_req_o_bits_len;
  logic [2:0]   mmu_axi_ar_req_o_bits_size;
  logic [1:0]   mmu_axi_ar_req_o_bits_burst;
  logic         mmu_axi_ar_req_o_bits_lock;
  logic [3:0]   mmu_axi_ar_req_o_bits_cache;
  logic [2:0]   mmu_axi_ar_req_o_bits_prot;
  logic [3:0]   mmu_axi_ar_req_o_bits_qos;
  logic         mmu_axi_ar_req_o_bits_region;
  logic         mmu_axi_r_resp_i_ready;
  logic         mmu_axi_r_resp_i_valid;
  logic [3:0]   mmu_axi_r_resp_i_bits_id;
  logic [63:0]  mmu_axi_r_resp_i_bits_data;
  logic [1:0]   mmu_axi_r_resp_i_bits_resp;
  logic         mmu_axi_r_resp_i_bits_last;

  lite_req_512_t lite_req_512;
  lite_resp_512_t lite_resp_512;

  req_512_t req_coprocessor;
  resp_512_t resp_coprocessor;



  ChiselCoreWrapper u_ChiselCoreWrapper (

        .clock(clk_i),
        .reset(rst_ni),

        .io_pri_aplic_mode      (2'b0),
        .io_pri_aplic_pri       (8'b0),
        .io_pri_aplic_id        (10'd33),
        .io_pri_core_to_core_out(),

        .io_scan_mode  (1'b0),

        //TODO
        .io_uvm_env_enable_i                (1'b0),

        .io_debug_req_i                     (1'b0),
        .io_debug_instr_valid_o             (),
        .io_debug_instr_addr_o              (),
        .io_debug_instr_rdata_i             (128'b0),

        .io_config_i_hart_id                (hart_id_i),

        /*
            Coprocesser
        */
       .io_copreSpeclativeFlag_i                (1'b0),//1:Support the copreSpec
       .io_copreStoreEmptyFlag_i                (coprocessor_store_empty),
       .io_coprecessor_issue_ready              (io_coprecessor_issue_req_ready),
       .io_coprecessor_issue_valid              (io_coprecessor_issue_req_valid),
       .io_coprecessor_issue_req_hartId         (io_coprecessor_issue_req_bits_hart_id),
       .io_coprecessor_issue_req_instId         (io_coprecessor_issue_req_bits_inst_id),
       .io_coprecessor_issue_req_inst           (io_coprecessor_issue_req_bits_inst),
       .io_coprecessor_issue_resp_accept        (io_coprecessor_issue_resp_accept),
       .io_coprecessor_issue_resp_rdValid_0     (io_coprecessor_issue_resp_rd_valid_0),
       .io_coprecessor_issue_resp_rdValid_1     (io_coprecessor_issue_resp_rd_valid_1),
       .io_coprecessor_issue_resp_frdValid_0     (io_coprecessor_issue_resp_frd_valid_0),
       .io_coprecessor_issue_resp_frdValid_1     (io_coprecessor_issue_resp_frd_valid_1),
       .io_coprecessor_issue_resp_wbValid       (io_coprecessor_issue_resp_wb_valid),
       .io_coprecessor_issue_resp_fwbValid       (io_coprecessor_issue_resp_fwb_valid),
       .io_coprecessor_register_valid           (io_coprecessor_register_valid),
       .io_coprecessor_register_bits_hartId     (io_coprecessor_register_bits_hart_id),
       .io_coprecessor_register_bits_instId     (io_coprecessor_register_bits_inst_id),
       .io_coprecessor_register_bits_data_0     (io_coprecessor_register_bits_data_0),
       .io_coprecessor_register_bits_data_1     (io_coprecessor_register_bits_data_1),
       .io_coprecessor_register_bits_fdata_0     (io_coprecessor_register_bits_fdata_0),
       .io_coprecessor_register_bits_fdata_1     (io_coprecessor_register_bits_fdata_1),
       .io_coprecessor_commit_valid             (io_coprecessor_commit_valid),
       .io_coprecessor_commit_bits_hartId       (io_coprecessor_commit_bits_hart_id),
       .io_coprecessor_commit_bits_instId       (io_coprecessor_commit_bits_inst_id),
       .io_coprecessor_commit_bits_kill         (io_coprecessor_commit_bits_kill),
       .io_coprecessor_completed_ready          (io_coprecessor_completed_ready),
       .io_coprecessor_completed_valid          (io_coprecessor_completed_valid),
       .io_coprecessor_completed_bits_hartId    (io_coprecessor_completed_bits_hart_id[3:0]),
       .io_coprecessor_completed_bits_instId    (io_coprecessor_completed_bits_inst_id),
       .io_coprecessor_completed_bits_data      (io_coprecessor_completed_bits_data),
       .io_coprecessor_completed_bits_excp      (io_coprecessor_completed_bits_excp),
       .io_coprecessor_forwarding_valid         (io_coprecessor_forwarding_valid),
       // The legacy ChiselCoreWrapper forwarding port is only 4 bits wide;
       // the AICB/AME boundary remains 32 bits and is explicitly truncated
       // here until the core-side generated interface is widened.
       .io_coprecessor_forwarding_bits_inst     (io_coprecessor_forwarding_bits_inst[3:0]),
       .io_coprecessor_syncSend_valid           (io_coprecessor_syncSend_valid),
       .io_coprecessor_syncSend_bits_syncId     (io_coprecessor_syncSend_bits_syncId),
       .io_coprecessor_syncSend_bits_syncCpu    (io_coprecessor_syncSend_bits_syncCpu),
       .io_coprecessor_syncRecv_ready           (io_coprecessor_syncRecv_ready),
        /*
            Prim Core
        */
        .io_config_i_boot_addr         (boot_addr_i),
        .io_config_i_itcm_startAddr    (config_itcm_startAddr_i),
        .io_config_i_itcm_size         (config_itcm_size_i),
        .io_config_i_dtcm_startAddr    (config_dtcm_startAddr_i),
        .io_config_i_dtcm_size         (config_dtcm_size_i),
        .io_config_i_llpp_startAddr    (config_llpp_startAddr_i),
        .io_config_i_llpp_size         (config_llpp_size_i),
        .io_config_i_flash_startAddr   (config_flash_startAddr_i),
        .io_config_i_flash_size        (config_flash_size_i),

        .io_config_i_cluster_id        (io_cluster_utid_rs_i),
        .io_config_i_cpu_id            (io_cpu_i),
        
        .io_pri_irq_i_nmi              (1'b0),
//        .io_pri_irq_i_external         (1'b0),
//        .io_pri_irq_i_soft             (1'b0),
//        .io_pri_irq_i_timer            (time_irq_i),

        .io_pri_irq_i_m_external       (1'b0),
        .io_pri_irq_i_m_soft           (1'b0),
        .io_pri_irq_i_m_timer          (1'b0),
        .io_pri_irq_i_s_external       (1'b0),
        .io_pri_irq_i_s_soft           (1'b0),
        .io_pri_irq_i_s_timer          (1'b0),



        //TODO:
        .io_wfe_event_i                (1'b0),
        .io_sleep_flag_o               (),
        .io_cpu_halt_flag_i            (1'b0),

        .io_eccerr_dtcm_singleError    (),
        .io_eccerr_dtcm_doubleError    (),
        .io_eccerr_dcache_singleError  (),
        .io_eccerr_dcache_doubleError  (),
        .io_eccerr_itcm_singleError    (),
        .io_eccerr_itcm_doubleError    (),
        .io_eccerr_icache_singleError  (),
        .io_eccerr_icache_doubleError  (),

        .io_axim_utid_err              (io_axim_utid_err),
        .io_axim_timeout_o             (io_axim_timeout_limit_reached_o),
        .io_flash_utid_err             (io_flash_utid_err),
        .io_flash_timeout_o            (io_flash_timeout_limit_reached_o),
        .io_llpp_utid_err              (io_llpp_utid_err),
        .io_llpp_timeout_o             (io_llpp_timeout_limit_reached_o),

        .io_axim_aw_ready            (axim_aw_ready_i),
        .io_axim_aw_valid            (axim_aw_valid_o),
        .io_axim_aw_bits_id          (axim_aw_id_o),
        .io_axim_aw_bits_addr        (axim_aw_addr_o),
        .io_axim_aw_bits_len         (axim_aw_len_o),
        .io_axim_aw_bits_size        (axim_aw_size_o),
        .io_axim_aw_bits_burst       (axim_aw_burst_o),
        .io_axim_aw_bits_lock        (axim_aw_lock_o),
        .io_axim_aw_bits_cache       (axim_aw_cache_o),
        .io_axim_aw_bits_prot        (axim_aw_prot_o),
        //lsu_axi_awqos lsu_axi_awregion

        .io_axim_w_ready             (axim_w_ready_i),
        .io_axim_w_valid             (axim_w_valid_o),
        .io_axim_w_bits_id           (axim_w_id_o),
        .io_axim_w_bits_data         (axim_w_data_o),
        .io_axim_w_bits_strb         (axim_w_strb_o),
        .io_axim_w_bits_last         (axim_w_last_o),

        .io_axim_b_ready             (axim_b_ready_o),
        .io_axim_b_valid             (axim_b_valid_i),
        .io_axim_b_bits_resp         (axim_b_resp_i),
        .io_axim_b_bits_id           (axim_b_id_i),

        .io_axim_ar_ready            (axim_ar_ready_i),
        .io_axim_ar_valid            (axim_ar_valid_o),
        .io_axim_ar_bits_id          (axim_ar_id_o),
        .io_axim_ar_bits_addr        (axim_ar_addr_o),
        .io_axim_ar_bits_len         (axim_ar_len_o),
        .io_axim_ar_bits_size        (axim_ar_size_o),
        .io_axim_ar_bits_burst       (axim_ar_burst_o),
        .io_axim_ar_bits_lock        (axim_ar_lock_o),
        .io_axim_ar_bits_cache       (axim_ar_cache_o),
        .io_axim_ar_bits_prot        (axim_ar_prot_o),

        .io_axim_r_ready             (axim_r_ready_o),
        .io_axim_r_valid             (axim_r_valid_i),
        .io_axim_r_bits_id           (axim_r_id_i),
        .io_axim_r_bits_data         (axim_r_data_i),
        .io_axim_r_bits_resp         (axim_r_resp_i),
        .io_axim_r_bits_last         (axim_r_last_i),

        .io_axim_aw_utid_o           (),
        .io_axim_w_utid_o            (),
        .io_axim_b_utid_i            (10'b0),
        .io_axim_ar_utid_o           (),
        .io_axim_r_utid_i            (10'b0),

        .io_flash_ar_ready         (flash_ar_ready_i),
        .io_flash_ar_valid         (flash_ar_valid_o),
        .io_flash_ar_bits_id       (flash_ar_id_o),
        .io_flash_ar_bits_addr     (flash_ar_addr_o),
        .io_flash_ar_bits_len      (flash_ar_len_o),
        .io_flash_ar_bits_size     (flash_ar_size_o),
        .io_flash_ar_bits_burst    (flash_ar_burst_o),
        .io_flash_ar_bits_lock     (flash_ar_lock_o),
        .io_flash_ar_bits_cache    (flash_ar_cache_o),
        .io_flash_ar_bits_prot     (flash_ar_prot_o),

        .io_flash_r_ready          (flash_r_ready_o),
        .io_flash_r_valid          (flash_r_valid_i),
        .io_flash_r_bits_id        (flash_r_id_i),
        .io_flash_r_bits_data      (flash_r_data_i),
        .io_flash_r_bits_resp      (flash_r_resp_i),
        .io_flash_r_bits_last      (flash_r_last_i),

        .io_flash_ar_utid_o        (),
        .io_flash_r_utid_i         (10'b0),

        //TODO:LLPP Channel not connect
        .io_llpp_aw_ready            (io_llpp_aw_ready_i),
        .io_llpp_aw_valid            (io_llpp_aw_valid_o),
        .io_llpp_aw_bits_id          (io_llpp_aw_bits_id_o),
        .io_llpp_aw_bits_addr        (io_llpp_aw_bits_addr_o),
        .io_llpp_aw_bits_len         (io_llpp_aw_bits_len_o),
        .io_llpp_aw_bits_size        (io_llpp_aw_bits_size_o),
        .io_llpp_aw_bits_burst       (io_llpp_aw_bits_burst_o),
        .io_llpp_aw_bits_lock        (io_llpp_aw_bits_lock_o),
        .io_llpp_aw_bits_cache       (io_llpp_aw_bits_cache_o),
        .io_llpp_aw_bits_prot        (io_llpp_aw_bits_prot_o),

        .io_llpp_w_ready             (io_llpp_w_ready_i),
        .io_llpp_w_valid             (io_llpp_w_valid_o),
        .io_llpp_w_bits_id           (io_llpp_w_bits_id_o),
        .io_llpp_w_bits_data         (io_llpp_w_bits_data_o),
        .io_llpp_w_bits_strb         (io_llpp_w_bits_strb_o),
        .io_llpp_w_bits_last         (io_llpp_w_bits_last_o),
                                                
        .io_llpp_b_ready             (io_llpp_b_ready_o),
        .io_llpp_b_valid             (io_llpp_b_valid_i),
        .io_llpp_b_bits_resp         (io_llpp_b_bits_resp_i),
        .io_llpp_b_bits_id           (io_llpp_b_bits_id_i),

        .io_llpp_ar_ready            (io_llpp_ar_ready_i),
        .io_llpp_ar_valid            (io_llpp_ar_valid_o),
        .io_llpp_ar_bits_id          (io_llpp_ar_bits_id_o),
        .io_llpp_ar_bits_addr        (io_llpp_ar_bits_addr_o),
        .io_llpp_ar_bits_len         (io_llpp_ar_bits_len_o),
        .io_llpp_ar_bits_size        (io_llpp_ar_bits_size_o),
        .io_llpp_ar_bits_burst       (io_llpp_ar_bits_burst_o),
        .io_llpp_ar_bits_lock        (io_llpp_ar_bits_lock_o),
        .io_llpp_ar_bits_cache       (io_llpp_ar_bits_cache_o),
        .io_llpp_ar_bits_prot        (io_llpp_ar_bits_prot_o),

        .io_llpp_r_ready             (io_llpp_r_ready_o),
        .io_llpp_r_valid             (io_llpp_r_valid_i),
        .io_llpp_r_bits_id           (io_llpp_r_bits_id_i),
        .io_llpp_r_bits_data         (io_llpp_r_bits_data_i),
        .io_llpp_r_bits_resp         (io_llpp_r_bits_resp_i),
        .io_llpp_r_bits_last         (io_llpp_r_bits_last_i),


        .io_llpp_aw_utid_o         (),
        .io_llpp_w_utid_o          (),
        .io_llpp_b_utid_i          (10'b0),
        .io_llpp_ar_utid_o         (),
        .io_llpp_r_utid_i          (10'b0),

        //TODO:AXIS Channel not connect

        .io_axis_aw_ready            (axis_aw_ready_o),
        .io_axis_aw_valid            (axis_aw_valid_i),
        .io_axis_aw_bits_id          (axis_aw_id_i),
        .io_axis_aw_bits_addr        (axis_aw_addr_i),
        .io_axis_aw_bits_len         (axis_aw_len_i),
        .io_axis_aw_bits_size        (axis_aw_size_i),
        .io_axis_aw_bits_burst       (axis_aw_burst_i),
        .io_axis_aw_bits_lock        (axis_aw_lock_i),
        .io_axis_aw_bits_cache       (axis_aw_cache_i),
        .io_axis_aw_bits_prot        (axis_aw_prot_i),

        .io_axis_w_ready             (axis_w_ready_o),
        .io_axis_w_valid             (axis_w_valid_i),
        .io_axis_w_bits_id           (axis_w_id_i),
        .io_axis_w_bits_data         (axis_w_data_i),
        .io_axis_w_bits_strb         (axis_w_strb_i),
        .io_axis_w_bits_last         (axis_w_last_i),

        .io_axis_b_ready             (axis_b_ready_i),
        .io_axis_b_valid             (axis_b_valid_o),
        .io_axis_b_bits_resp         (axis_b_resp_o),
        .io_axis_b_bits_id           (axis_b_id_o),

        .io_axis_ar_ready            (axis_ar_ready_o),
        .io_axis_ar_valid            (axis_ar_valid_i),
        .io_axis_ar_bits_id          (axis_ar_id_i),
        .io_axis_ar_bits_addr        (axis_ar_addr_i),
        .io_axis_ar_bits_len         (axis_ar_len_i),
        .io_axis_ar_bits_size        (axis_ar_size_i),
        .io_axis_ar_bits_burst       (axis_ar_burst_i),
        .io_axis_ar_bits_lock        (axis_ar_lock_i),
        .io_axis_ar_bits_cache       (axis_ar_cache_i),
        .io_axis_ar_bits_prot        (axis_ar_prot_i),

        .io_axis_r_ready             (axis_r_ready_i),
        .io_axis_r_valid             (axis_r_valid_o),
        .io_axis_r_bits_id           (axis_r_id_o),
        .io_axis_r_bits_data         (axis_r_data_o),
        .io_axis_r_bits_resp         (axis_r_resp_o),
        .io_axis_r_bits_last         (axis_r_last_o),

        .io_axis_ar_utid_i     (10'b0),
        .io_axis_r_utid_o      (),
        .io_axis_aw_utid_i     (10'b0),
        .io_axis_w_utid_i      (10'b0),
        .io_axis_b_utid_o      (),

        //TODO:EIM
        .io_eim_en                 (1'b0),
        .io_eim_chkbit_mask0       (8'b0),
        .io_eim_chdata_mask0       (64'b0),
        .io_eim_sel                (6'b0),

        .io_trace_valid_0          (),
        .io_trace_instr_0          (),
        .io_trace_pc_0             (),
        .io_trace_dvalid_0         (),
        .io_trace_dpaddr_0         (),
        .io_trace_dvaddr_0         (),
        .io_trace_valid_1          (),
        .io_trace_instr_1          (),
        .io_trace_pc_1             (),
        .io_trace_dvalid_1         (),
        .io_trace_dpaddr_1         (),
        .io_trace_dvaddr_1         (),
        .io_trace_valid_2          (),
        .io_trace_instr_2          (),
        .io_trace_pc_2             (),
        .io_trace_dvalid_2         (),
        .io_trace_dpaddr_2         (),
        .io_trace_dvaddr_2         (),
        .io_trace_exception        (),
        .io_trace_ecause           (),
        .io_trace_interrupt_ip     (),
        .io_trace_interrupt_type   (),
        .io_trace_tval_ip          (),
        .io_trace_debug_ip         (),
        .io_trace_debug_mode       (),
        
        .io_debug_tck                       (jtag_tck_i),
        .io_debug_tms                       (jtag_tms_i),
        .io_debug_trst                      (jtag_trst_n_i),
        .io_debug_tdi                       (jtag_tdi_i),
        .io_debug_tdo                       (jtag_tdo_o),
        .io_debug_tdo_oe                    (),

        .io_dmi_dmi_req_valid               (1'b0),
        .io_dmi_dmi_req_ready               (),
        .io_dmi_dmi_req                     (41'b0),
        .io_dmi_dmi_resp_valid              (),
        .io_dmi_dmi_resp_ready              (1'b1),
        .io_dmi_dmi_resp                    (),
        .io_dmi_dmi_rst_n                   (1'b0),

        .io_intSrc_intr_src_i_0             (external_irq_i[0]),//external_irq_i[0 ]
        .io_intSrc_intr_src_i_1             (external_irq_i[1]),//external_irq_i[1 ]
        .io_intSrc_intr_src_i_2             (external_irq_i[2]),//external_irq_i[2 ]
        .io_intSrc_intr_src_i_3             (external_irq_i[3]),//external_irq_i[3 ]
        .io_intSrc_intr_src_i_4             (external_irq_i[4]),//external_irq_i[4 ]
        .io_intSrc_intr_src_i_5             (external_irq_i[5]),//external_irq_i[5 ]
        .io_intSrc_intr_src_i_6             (external_irq_i[6]),//external_irq_i[6 ]
        .io_intSrc_intr_src_i_7             (external_irq_i[7]),//external_irq_i[7 ]
        .io_intSrc_intr_src_i_8             (external_irq_i[8 ]),
        .io_intSrc_intr_src_i_9             (external_irq_i[9 ]),
        .io_intSrc_intr_src_i_10            (external_irq_i[10]),
        .io_intSrc_intr_src_i_11            (external_irq_i[11]),
        .io_intSrc_intr_src_i_12            (external_irq_i[12]),
        .io_intSrc_intr_src_i_13            (external_irq_i[13]),
        .io_intSrc_intr_src_i_14            (external_irq_i[14]),
        .io_intSrc_intr_src_i_15            (external_irq_i[15]),
        .io_intSrc_intr_src_i_16            (external_irq_i[16]),
        .io_intSrc_intr_src_i_17            (external_irq_i[17]),
        .io_intSrc_intr_src_i_18            (external_irq_i[18]),
        .io_intSrc_intr_src_i_19            (external_irq_i[19]),
        .io_intSrc_intr_src_i_20            (external_irq_i[20]),
        .io_intSrc_intr_src_i_21            (external_irq_i[21]),
        .io_intSrc_intr_src_i_22            (external_irq_i[22]),
        .io_intSrc_intr_src_i_23            (external_irq_i[23]),
        .io_intSrc_intr_src_i_24            (external_irq_i[24]),
        .io_intSrc_intr_src_i_25            (external_irq_i[25]),
        .io_intSrc_intr_src_i_26            (external_irq_i[26]),
        .io_intSrc_intr_src_i_27            (external_irq_i[27]),
        .io_intSrc_intr_src_i_28            (external_irq_i[28]),
        .io_intSrc_intr_src_i_29            (external_irq_i[29]),
        .io_intSrc_intr_src_i_30            (external_irq_i[30]),
        .io_intSrc_intr_src_i_31            (external_irq_i[31]),
        .io_rtc_i                           (rtc_i),

        //TODO:DEBUG SBA AXI Channel
        .io_debugHost_pri_awChannel_o_ready         (1'b0),
        .io_debugHost_pri_awChannel_o_valid         (),
        .io_debugHost_pri_awChannel_o_bits_addr     (),
        .io_debugHost_pri_awChannel_o_bits_id       (),
        .io_debugHost_pri_awChannel_o_bits_len      (),
        .io_debugHost_pri_awChannel_o_bits_size     (),
        .io_debugHost_pri_awChannel_o_bits_burst    (),
        .io_debugHost_pri_awChannel_o_bits_lock     (),
        .io_debugHost_pri_awChannel_o_bits_cache    (),
        .io_debugHost_pri_awChannel_o_bits_prot     (),
        .io_debugHost_pri_awChannel_o_bits_qos      (),

        .io_debugHost_pri_wChannel_o_ready          (1'b0),
        .io_debugHost_pri_wChannel_o_valid          (),
        .io_debugHost_pri_wChannel_o_bits_id        (),
        .io_debugHost_pri_wChannel_o_bits_data      (),
        .io_debugHost_pri_wChannel_o_bits_strb      (),
        .io_debugHost_pri_wChannel_o_bits_last      (),

        .io_debugHost_pri_bChannel_i_ready          (),
        .io_debugHost_pri_bChannel_i_valid          (1'b0),
        .io_debugHost_pri_bChannel_i_bits_id        (1'b0),
        .io_debugHost_pri_bChannel_i_bits_resp      (2'b0),

        .io_debugHost_pri_arChannel_o_ready         (1'b0),
        .io_debugHost_pri_arChannel_o_valid         (),
        .io_debugHost_pri_arChannel_o_bits_addr     (),
        .io_debugHost_pri_arChannel_o_bits_id       (),
        .io_debugHost_pri_arChannel_o_bits_len      (),
        .io_debugHost_pri_arChannel_o_bits_size     (),
        .io_debugHost_pri_arChannel_o_bits_burst    (),
        .io_debugHost_pri_arChannel_o_bits_lock     (),
        .io_debugHost_pri_arChannel_o_bits_cache    (),
        .io_debugHost_pri_arChannel_o_bits_prot     (),
        .io_debugHost_pri_arChannel_o_bits_qos      (),

        .io_debugHost_pri_rChannel_i_ready          (),
        .io_debugHost_pri_rChannel_i_valid          (1'b0),
        .io_debugHost_pri_rChannel_i_bits_data      (32'b0),
        .io_debugHost_pri_rChannel_i_bits_id        (1'b0),
        .io_debugHost_pri_rChannel_i_bits_last      (1'b0),
        .io_debugHost_pri_rChannel_i_bits_resp      (2'b0),

        .io_mmu_axi_ar_req_o_ready        (mmu_axi_ar_req_o_ready        ),
        .io_mmu_axi_ar_req_o_valid        (mmu_axi_ar_req_o_valid        ),
        .io_mmu_axi_ar_req_o_bits_id      (mmu_axi_ar_req_o_bits_id      ),
        .io_mmu_axi_ar_req_o_bits_addr    (mmu_axi_ar_req_o_bits_addr    ),
        .io_mmu_axi_ar_req_o_bits_len     (mmu_axi_ar_req_o_bits_len     ),
        .io_mmu_axi_ar_req_o_bits_size    (mmu_axi_ar_req_o_bits_size    ),
        .io_mmu_axi_ar_req_o_bits_burst   (mmu_axi_ar_req_o_bits_burst   ),
        .io_mmu_axi_ar_req_o_bits_lock    (mmu_axi_ar_req_o_bits_lock    ),
        .io_mmu_axi_ar_req_o_bits_cache   (mmu_axi_ar_req_o_bits_cache   ),
        .io_mmu_axi_ar_req_o_bits_prot    (mmu_axi_ar_req_o_bits_prot    ),
        .io_mmu_axi_ar_req_o_bits_qos     (mmu_axi_ar_req_o_bits_qos     ),
        .io_mmu_axi_ar_req_o_bits_region  (mmu_axi_ar_req_o_bits_region  ),

        .io_mmu_axi_r_resp_i_ready        (mmu_axi_r_resp_i_ready        ),
        .io_mmu_axi_r_resp_i_valid        (mmu_axi_r_resp_i_valid        ),
        .io_mmu_axi_r_resp_i_bits_id      (mmu_axi_r_resp_i_bits_id      ),
        .io_mmu_axi_r_resp_i_bits_data    (mmu_axi_r_resp_i_bits_data    ),
        .io_mmu_axi_r_resp_i_bits_resp    (mmu_axi_r_resp_i_bits_resp    ),
        .io_mmu_axi_r_resp_i_bits_last    (mmu_axi_r_resp_i_bits_last    ),

        //TODO:APLIC CSR 
        .io_pri_aplic_csr_req_i_ready         (1'b0     ),
        .io_pri_aplic_csr_req_i_valid         (),
        .io_pri_aplic_csr_req_i_bits_addr     (),
        .io_pri_aplic_csr_req_i_bits_write    (),
        .io_pri_aplic_csr_req_i_bits_wdata    (),
        .io_pri_aplic_csr_req_i_bits_wstrb    (),
        .io_pri_aplic_csr_resp_o_rdata        (32'b0    ),
        .io_pri_aplic_csr_resp_o_error        (1'b0     ),
        // .io_sec_aplic_csr_req_i_ready         (io_sec_aplic_csr_req_i_ready     ),
        // .io_sec_aplic_csr_req_i_valid         (io_sec_aplic_csr_req_i_valid     ),
        // .io_sec_aplic_csr_req_i_bits_addr     (io_sec_aplic_csr_req_i_bits_addr ),
        // .io_sec_aplic_csr_req_i_bits_write    (io_sec_aplic_csr_req_i_bits_write),
        // .io_sec_aplic_csr_req_i_bits_wdata    (io_sec_aplic_csr_req_i_bits_wdata),
        // .io_sec_aplic_csr_req_i_bits_wstrb    (io_sec_aplic_csr_req_i_bits_wstrb),
        // .io_sec_aplic_csr_resp_o_rdata        (io_sec_aplic_csr_resp_o_rdata    ),
        // .io_sec_aplic_csr_resp_o_error        (io_sec_aplic_csr_resp_o_error    ),
        .io_axi_ar_ready            (axi_ar_ready        ),
        .io_axi_ar_valid            (axi_ar_valid        ),
        .io_axi_ar_bits_id          (axi_ar_bits_id      ),
        .io_axi_ar_bits_addr        (axi_ar_bits_addr    ),
        .io_axi_ar_bits_len         (axi_ar_bits_len     ),
        .io_axi_ar_bits_size        (axi_ar_bits_size    ),
        .io_axi_ar_bits_burst       (axi_ar_bits_burst   ),
        .io_axi_ar_bits_lock        (axi_ar_bits_lock    ),
        .io_axi_ar_bits_cache       (axi_ar_bits_cache   ),
        .io_axi_ar_bits_prot        (axi_ar_bits_prot    ),
        .io_axi_r_ready             (axi_r_ready         ),
        .io_axi_r_valid             (axi_r_valid         ),
        .io_axi_r_bits_id           (axi_r_bits_id       ),
        .io_axi_r_bits_data         (axi_r_bits_data     ),
        .io_axi_r_bits_resp         (axi_r_bits_resp     ),
        .io_axi_r_bits_last         (axi_r_bits_last     )
    );

      // CPU AII master -> AICB -> AME (slave 0) and VecTop (slave 1).
      ame_aicb_wrapper #(.ENABLE_AME(AME_ENABLE)) u_ame_aicb (
        .clock (clk_i), .reset (~rst_ni),
        .cpu_issue_valid (io_coprecessor_issue_req_valid), .cpu_issue_ready (io_coprecessor_issue_req_ready),
        .cpu_issue_hart_id (io_coprecessor_issue_req_bits_hart_id), .cpu_issue_inst_id (io_coprecessor_issue_req_bits_inst_id), .cpu_issue_inst (io_coprecessor_issue_req_bits_inst),
        .cpu_issue_accept (io_coprecessor_issue_resp_accept), .cpu_issue_rd_valid_0 (io_coprecessor_issue_resp_rd_valid_0), .cpu_issue_rd_valid_1 (io_coprecessor_issue_resp_rd_valid_1),
        .cpu_issue_frd_valid_0 (io_coprecessor_issue_resp_frd_valid_0), .cpu_issue_frd_valid_1 (io_coprecessor_issue_resp_frd_valid_1), .cpu_issue_wb_valid (io_coprecessor_issue_resp_wb_valid), .cpu_issue_fwb_valid (io_coprecessor_issue_resp_fwb_valid),
        .cpu_register_valid (io_coprecessor_register_valid), .cpu_register_hart_id (io_coprecessor_register_bits_hart_id), .cpu_register_inst_id (io_coprecessor_register_bits_inst_id), .cpu_register_data_0 (io_coprecessor_register_bits_data_0), .cpu_register_data_1 (io_coprecessor_register_bits_data_1), .cpu_register_fdata_0 (io_coprecessor_register_bits_fdata_0), .cpu_register_fdata_1 (io_coprecessor_register_bits_fdata_1),
        .cpu_commit_valid (io_coprecessor_commit_valid), .cpu_commit_hart_id (io_coprecessor_commit_bits_hart_id), .cpu_commit_inst_id (io_coprecessor_commit_bits_inst_id), .cpu_commit_kill (io_coprecessor_commit_bits_kill),
        .cpu_completed_ready (io_coprecessor_completed_ready), .cpu_completed_valid (io_coprecessor_completed_valid), .cpu_completed_hart_id (io_coprecessor_completed_bits_hart_id), .cpu_completed_inst_id (io_coprecessor_completed_bits_inst_id), .cpu_completed_data (io_coprecessor_completed_bits_data), .cpu_completed_excp (io_coprecessor_completed_bits_excp),
        .cpu_forwarding_valid (io_coprecessor_forwarding_valid), .cpu_forwarding_inst (io_coprecessor_forwarding_bits_inst), .cpu_sync_send_ready (1'b0), .cpu_sync_send_valid (io_coprecessor_syncSend_valid), .cpu_sync_send_id (io_coprecessor_syncSend_bits_syncId), .cpu_sync_send_cpu (io_coprecessor_syncSend_bits_syncCpu), .cpu_sync_recv_ready (io_coprecessor_syncRecv_ready), .cpu_sync_recv_valid (1'b0), .cpu_sync_recv_id (1'b0),
        .rvv_issue_valid (rvv_issue_valid), .rvv_issue_ready (rvv_issue_ready), .rvv_issue_hart_id (rvv_issue_hart_id), .rvv_issue_inst_id (rvv_issue_inst_id), .rvv_issue_inst (rvv_issue_inst), .rvv_issue_accept (rvv_issue_accept), .rvv_issue_rd_valid_0 (rvv_issue_rd_valid_0), .rvv_issue_rd_valid_1 (rvv_issue_rd_valid_1), .rvv_issue_frd_valid_0 (rvv_issue_frd_valid_0), .rvv_issue_frd_valid_1 (rvv_issue_frd_valid_1), .rvv_issue_wb_valid (rvv_issue_wb_valid), .rvv_issue_fwb_valid (rvv_issue_fwb_valid),
        .rvv_register_valid (rvv_register_valid), .rvv_register_hart_id (rvv_register_hart_id), .rvv_register_inst_id (rvv_register_inst_id), .rvv_register_data_0 (rvv_register_data_0), .rvv_register_data_1 (rvv_register_data_1), .rvv_register_fdata_0 (rvv_register_fdata_0), .rvv_register_fdata_1 (rvv_register_fdata_1), .rvv_commit_valid (rvv_commit_valid), .rvv_commit_hart_id (rvv_commit_hart_id), .rvv_commit_inst_id (rvv_commit_inst_id), .rvv_commit_kill (rvv_commit_kill), .rvv_completed_ready (rvv_completed_ready), .rvv_completed_valid (rvv_completed_valid), .rvv_completed_hart_id (rvv_completed_hart_id), .rvv_completed_inst_id (rvv_completed_inst_id), .rvv_completed_data (rvv_completed_data), .rvv_completed_excp (rvv_completed_excp), .rvv_forwarding_valid (1'b0), .rvv_forwarding_inst (32'b0), .rvv_sync_send_ready (), .rvv_sync_send_valid (1'b0), .rvv_sync_send_id (1'b0), .rvv_sync_send_cpu (1'b0), .rvv_sync_recv_ready (1'b0), .rvv_sync_recv_valid (), .rvv_sync_recv_id (),
        .ame_axi_aw_ready (io_ame_aw_ready_i), .ame_axi_aw_valid (io_ame_aw_valid_o), .ame_axi_aw_id (io_ame_aw_bits_id_o), .ame_axi_aw_addr (io_ame_aw_bits_addr_o), .ame_axi_aw_len (io_ame_aw_bits_len_o), .ame_axi_aw_size (io_ame_aw_bits_size_o), .ame_axi_aw_burst (io_ame_aw_bits_burst_o), .ame_axi_aw_lock (io_ame_aw_bits_lock_o), .ame_axi_aw_cache (io_ame_aw_bits_cache_o), .ame_axi_aw_prot (io_ame_aw_bits_prot_o), .ame_axi_w_ready (io_ame_w_ready_i), .ame_axi_w_valid (io_ame_w_valid_o), .ame_axi_w_data (io_ame_w_bits_data_o), .ame_axi_w_strb (io_ame_w_bits_strb_o), .ame_axi_w_last (io_ame_w_bits_last_o), .ame_axi_b_valid (io_ame_b_valid_i), .ame_axi_b_id (io_ame_b_bits_id_i), .ame_axi_b_resp (io_ame_b_bits_resp_i), .ame_axi_b_ready (io_ame_b_ready_o), .ame_axi_ar_ready (io_ame_ar_ready_i), .ame_axi_ar_valid (io_ame_ar_valid_o), .ame_axi_ar_id (io_ame_ar_bits_id_o), .ame_axi_ar_addr (io_ame_ar_bits_addr_o), .ame_axi_ar_len (io_ame_ar_bits_len_o), .ame_axi_ar_size (io_ame_ar_bits_size_o), .ame_axi_ar_burst (io_ame_ar_bits_burst_o), .ame_axi_ar_lock (io_ame_ar_bits_lock_o), .ame_axi_ar_cache (io_ame_ar_bits_cache_o), .ame_axi_ar_prot (io_ame_ar_bits_prot_o), .ame_axi_r_valid (io_ame_r_valid_i), .ame_axi_r_id (io_ame_r_bits_id_i), .ame_axi_r_data (io_ame_r_bits_data_i), .ame_axi_r_resp (io_ame_r_bits_resp_i), .ame_axi_r_last (io_ame_r_bits_last_i), .ame_axi_r_ready (io_ame_r_ready_o), .ame_has_store (io_ame_hasStore_o)
      );

      VecTop u_coprocessor (
        .clock (clk_i), .reset (~rst_ni), .io_aicb_issue_valid (rvv_issue_valid), .io_aicb_issue_ready (rvv_issue_ready), .io_aicb_issue_req_hartId (rvv_issue_hart_id), .io_aicb_issue_req_instId (rvv_issue_inst_id), .io_aicb_issue_req_inst (rvv_issue_inst), .io_aicb_issue_resp_accept (rvv_issue_accept), .io_aicb_issue_resp_rdValid_0 (rvv_issue_rd_valid_0), .io_aicb_issue_resp_rdValid_1 (rvv_issue_rd_valid_1), .io_aicb_issue_resp_frdValid_0 (rvv_issue_frd_valid_0), .io_aicb_issue_resp_frdValid_1 (rvv_issue_frd_valid_1), .io_aicb_issue_resp_wbValid (rvv_issue_wb_valid), .io_aicb_issue_resp_fwbValid (rvv_issue_fwb_valid), .io_aicb_register_valid (rvv_register_valid), .io_aicb_register_bits_hartId (rvv_register_hart_id), .io_aicb_register_bits_instId (rvv_register_inst_id), .io_aicb_register_bits_data_0 (rvv_register_data_0), .io_aicb_register_bits_data_1 (rvv_register_data_1), .io_aicb_register_bits_fdata_0 (rvv_register_fdata_0), .io_aicb_register_bits_fdata_1 (rvv_register_fdata_1), .io_aicb_commit_valid (rvv_commit_valid), .io_aicb_commit_bits_hartId (rvv_commit_hart_id), .io_aicb_commit_bits_instId (rvv_commit_inst_id), .io_aicb_commit_bits_kill (rvv_commit_kill), .io_aicb_completed_valid (rvv_completed_valid), .io_aicb_completed_ready (rvv_completed_ready), .io_aicb_completed_bits_hartId (rvv_completed_hart_id), .io_aicb_completed_bits_instId (rvv_completed_inst_id), .io_aicb_completed_bits_data (rvv_completed_data), .io_aicb_completed_bits_excp (rvv_completed_excp),
        .io_axi_awid (io_coprocessor_aw_bits_id_o), .io_axi_awaddr (io_coprocessor_aw_bits_addr_o), .io_axi_awlen (io_coprocessor_aw_bits_len_o), .io_axi_awsize (io_coprocessor_aw_bits_size_o), .io_axi_awburst (io_coprocessor_aw_bits_burst_o), .io_axi_awlock (io_coprocessor_aw_bits_lock_o), .io_axi_awcache (io_coprocessor_aw_bits_cache_o), .io_axi_awprot (io_coprocessor_aw_bits_prot_o), .io_axi_awqos (), .io_axi_awvalid (io_coprocessor_aw_valid_o), .io_axi_awready (io_coprocessor_aw_ready_i), .io_axi_wdata (io_coprocessor_w_bits_data_o), .io_axi_wstrb (io_coprocessor_w_bits_strb_o), .io_axi_wlast (io_coprocessor_w_bits_last_o), .io_axi_wvalid (io_coprocessor_w_valid_o), .io_axi_wready (io_coprocessor_w_ready_i), .io_axi_bid (io_coprocessor_b_bits_id_i), .io_axi_bresp (io_coprocessor_b_bits_resp_i), .io_axi_bvalid (io_coprocessor_b_valid_i), .io_axi_bready (io_coprocessor_b_ready_o), .io_axi_arid (io_coprocessor_ar_bits_id_o), .io_axi_araddr (io_coprocessor_ar_bits_addr_o), .io_axi_arlen (io_coprocessor_ar_bits_len_o), .io_axi_arsize (io_coprocessor_ar_bits_size_o), .io_axi_arburst (io_coprocessor_ar_bits_burst_o), .io_axi_arlock (io_coprocessor_ar_bits_lock_o), .io_axi_arcache (io_coprocessor_ar_bits_cache_o), .io_axi_arprot (io_coprocessor_ar_bits_prot_o), .io_axi_arqos (), .io_axi_arvalid (io_coprocessor_ar_valid_o), .io_axi_arready (io_coprocessor_ar_ready_i), .io_axi_rid (io_coprocessor_r_bits_id_i), .io_axi_rdata (io_coprocessor_r_bits_data_i), .io_axi_rresp (io_coprocessor_r_bits_resp_i), .io_axi_rlast (io_coprocessor_r_bits_last_i), .io_axi_rvalid (io_coprocessor_r_valid_i), .io_axi_rready (io_coprocessor_r_ready_o), .io_hasStore (io_coprocessor_hasStore)
      );




    //   //@pengzhuo 20260724
    //   assign io_coprocessor_aw_bits_id_o = 'b0;
    //   assign io_coprocessor_aw_bits_size_o = 'b0;
    //   assign io_coprocessor_aw_bits_len_o = 'b0;
    //   assign io_coprocessor_aw_bits_burst_o = 'b0;
    //   assign io_coprocessor_aw_bits_lock_o = 'b0;
    //   assign io_coprocessor_aw_bits_prot_o = 'b0;
    //   assign io_coprocessor_aw_bits_cache_o = 'b0;

      assign io_coprocessor_w_bits_id_o = 'b0;

    //   assign io_coprocessor_ar_bits_id_o = 'b0;
    //   assign io_coprocessor_ar_bits_len_o = 'b0;
    //   assign io_coprocessor_ar_bits_size_o = 'b0;
    //   assign io_coprocessor_ar_bits_burst_o = 'b0;
    //   assign io_coprocessor_ar_bits_lock_o = 'b0;
    //   assign io_coprocessor_ar_bits_prot_o = 'b0;

      //@pengzhuo 20260302 
      // assign coprocessor_register_req_o_ready = cvxif_resp.register_ready;

assign io_axim_w_utid_o = 10'b0;


AXIAXI_64T512 #
(
    .IDW(4)                          ,   //ID 位宽设定.
    .STD(1)                          ,   //是否完全按照AXI窄传输:0=多周期窄传输适配为满传输;1=按标准窄传输适配.
    .OSL(8)                              //Outstanding: 2**OSL为真正深度.为安全,缺省为零.|
) u_AXIAXI_axim
(
    //1.0 复位时钟系统
    .aresetn         (rst_ni),                       //AXI asyn. reset. active low.
    .aclk            (clk_i),                       //AXI clock input. rising edge work
    //2.0 主侧slave AXI接主设备
    .slv_axi_awid    (axim_aw_id_o    ),                       //SLV AXI接口awid
    .slv_axi_awaddr  (axim_aw_addr_o  ),                       //SLV AXI接口awaddr
    .slv_axi_awregion(4'b0            ),                       //SLV AXI接口awregion
    .slv_axi_awlen   (axim_aw_len_o   ),                       //SLV AXI接口awlen
    .slv_axi_awsize  (axim_aw_size_o  ),                       //SLV AXI接口awsize
    .slv_axi_awburst (axim_aw_burst_o ),                       //SLV AXI接口awburst
    .slv_axi_awlock  (axim_aw_lock_o  ),                       //SLV AXI接口awlock
    .slv_axi_awcache (axim_aw_cache_o ),                       //SLV AXI接口awcache
    .slv_axi_awprot  (axim_aw_prot_o  ),                       //SLV AXI接口awprot
    .slv_axi_awqos   (4'b0            ),                       //SLV AXI接口awqos
    .slv_axi_awvalid (axim_aw_valid_o ),                       //SLV AXI接口awvalid
    .slv_axi_awready (axim_aw_ready_i ),                       //SLV AXI接口awready
    .slv_axi_wdata   (axim_w_data_o   ),                       //SLV AXI接口wdata
    .slv_axi_wstrb   (axim_w_strb_o   ),                       //SLV AXI接口wstrb
    .slv_axi_wlast   (axim_w_last_o   ),                       //SLV AXI接口wlast
    .slv_axi_wvalid  (axim_w_valid_o  ),                       //SLV AXI接口wvalid
    .slv_axi_wready  (axim_w_ready_i  ),                       //SLV AXI接口wready
    .slv_axi_bid     (axim_b_id_i     ),                       //SLV AXI接口bid
    .slv_axi_bresp   (axim_b_resp_i   ),                       //SLV AXI接口bresp
    .slv_axi_bvalid  (axim_b_valid_i  ),                       //SLV AXI接口bvalid
    .slv_axi_bready  (axim_b_ready_o  ),                       //SLV AXI接口bready
    .slv_axi_arid    (axim_ar_id_o    ),                       //SLV AXI接口arid
    .slv_axi_araddr  (axim_ar_addr_o  ),                       //SLV AXI接口araddr
    .slv_axi_arregion(4'b0            ),                       //SLV AXI接口arregion
    .slv_axi_arlen   (axim_ar_len_o   ),                       //SLV AXI接口arlen
    .slv_axi_arsize  (axim_ar_size_o  ),                       //SLV AXI接口arsize
    .slv_axi_arburst (axim_ar_burst_o ),                       //SLV AXI接口arburst
    .slv_axi_arlock  (axim_ar_lock_o  ),                       //SLV AXI接口arlock
    .slv_axi_arcache (axim_ar_cache_o ),                       //SLV AXI接口arcache
    .slv_axi_arprot  (axim_ar_prot_o  ),                       //SLV AXI接口arprot
    .slv_axi_arqos   (4'b0            ),                       //SLV AXI接口arqos
    .slv_axi_arvalid (axim_ar_valid_o ),                       //SLV AXI接口arvalid
    .slv_axi_arready (axim_ar_ready_i ),                       //SLV AXI接口arready
    .slv_axi_rid     (axim_r_id_i     ),                       //SLV AXI接口rid
    .slv_axi_rdata   (axim_r_data_i   ),                       //SLV AXI接口rdata
    .slv_axi_rresp   (axim_r_resp_i   ),                       //SLV AXI接口rresp
    .slv_axi_rlast   (axim_r_last_i   ),                       //SLV AXI接口rlast
    .slv_axi_rvalid  (axim_r_valid_i  ),                       //SLV AXI接口rvalid
    .slv_axi_rready  (axim_r_ready_o  ),                       //SLV AXI接口rready
    //3.0 从侧master AXI接从设备
    .mst_axi_awid    (io_axim_aw_bits_id_o    ),                       //MST AXI接口awid
    .mst_axi_awaddr  (io_axim_aw_bits_addr_o  ),                       //MST AXI接口awaddr
    .mst_axi_awregion(                ),                       //MST AXI接口awregion
    .mst_axi_awlen   (io_axim_aw_bits_len_o   ),                       //MST AXI接口awlen
    .mst_axi_awsize  (io_axim_aw_bits_size_o  ),                       //MST AXI接口awsize
    .mst_axi_awburst (io_axim_aw_bits_burst_o ),                       //MST AXI接口awburst
    .mst_axi_awlock  (io_axim_aw_bits_lock_o  ),                       //MST AXI接口awlock
    .mst_axi_awcache (io_axim_aw_bits_cache_o ),                       //MST AXI接口awcache
    .mst_axi_awprot  (io_axim_aw_bits_prot_o  ),                       //MST AXI接口awprot
    .mst_axi_awqos   (                ),                       //MST AXI接口awqos
    .mst_axi_awvalid (io_axim_aw_valid_o ),                       //MST AXI接口awvalid
    .mst_axi_awready (io_axim_aw_ready_i ),                       //MST AXI接口awready
    .mst_axi_wdata   (io_axim_w_bits_data_o   ),                       //MST AXI接口wdata
    .mst_axi_wstrb   (io_axim_w_bits_strb_o   ),                       //MST AXI接口wstrb
    .mst_axi_wlast   (io_axim_w_bits_last_o   ),                       //MST AXI接口wlast
    .mst_axi_wvalid  (io_axim_w_valid_o  ),                       //MST AXI接口wvalid
    .mst_axi_wready  (io_axim_w_ready_i  ),                       //MST AXI接口wready
    .mst_axi_bid     (io_axim_b_bits_id_i     ),                       //MST AXI接口bid
    .mst_axi_bresp   (io_axim_b_bits_resp_i   ),                       //MST AXI接口bresp
    .mst_axi_bvalid  (io_axim_b_valid_i  ),                       //MST AXI接口bvalid
    .mst_axi_bready  (io_axim_b_ready_o  ),                       //MST AXI接口bready
    .mst_axi_arid    (io_axim_ar_bits_id_o    ),                       //MST AXI接口arid
    .mst_axi_araddr  (io_axim_ar_bits_addr_o  ),                       //MST AXI接口araddr
    .mst_axi_arregion(                ),                       //MST AXI接口arregion
    .mst_axi_arlen   (io_axim_ar_bits_len_o   ),                       //MST AXI接口arlen
    .mst_axi_arsize  (io_axim_ar_bits_size_o  ),                       //MST AXI接口arsize
    .mst_axi_arburst (io_axim_ar_bits_burst_o ),                       //MST AXI接口arburst
    .mst_axi_arlock  (io_axim_ar_bits_lock_o  ),                       //MST AXI接口arlock
    .mst_axi_arcache (io_axim_ar_bits_cache_o ),                       //MST AXI接口arcache
    .mst_axi_arprot  (io_axim_ar_bits_prot_o  ),                       //MST AXI接口arprot
    .mst_axi_arqos   (                ),                       //MST AXI接口arqos
    .mst_axi_arvalid (io_axim_ar_valid_o ),                       //MST AXI接口arvalid
    .mst_axi_arready (io_axim_ar_ready_i ),                       //MST AXI接口arready
    .mst_axi_rid     (io_axim_r_bits_id_i     ),                       //MST AXI接口rid
    .mst_axi_rdata   (io_axim_r_bits_data_i   ),                       //MST AXI接口rdata
    .mst_axi_rresp   (io_axim_r_bits_resp_i   ),                       //MST AXI接口rresp
    .mst_axi_rlast   (io_axim_r_bits_last_i   ),                       //MST AXI接口rlast
    .mst_axi_rvalid  (io_axim_r_valid_i  ),                       //MST AXI接口rvalid
    .mst_axi_rready  (io_axim_r_ready_o  )                        //MST AXI接口rready
);






AXIAXI_64T512 #
(
    .IDW(1)                          ,   //ID 位宽设定.
    .STD(1)                          ,   //是否完全按照AXI窄传输:0=多周期窄传输适配为满传输;1=按标准窄传输适配.
    .OSL(8)                              //Outstanding: 2**OSL为真正深度.为安全,缺省为零.|
) u_AXIAXI_flash
(
    //1.0 复位时钟系统
    .aresetn         (rst_ni),                       //AXI asyn. reset. active low.
    .aclk            (clk_i),                       //AXI clock input. rising edge work
    //2.0 主侧slave AXI接主设备
    .slv_axi_awid    (1'b0 ),                       //SLV AXI接口awid
    .slv_axi_awaddr  (64'b0 ),                       //SLV AXI接口awaddr
    .slv_axi_awregion(4'b0 ),                       //SLV AXI接口awregion
    .slv_axi_awlen   (8'b0 ),                       //SLV AXI接口awlen
    .slv_axi_awsize  (3'b0 ),                       //SLV AXI接口awsize
    .slv_axi_awburst (2'b0 ),                       //SLV AXI接口awburst
    .slv_axi_awlock  (1'b0 ),                       //SLV AXI接口awlock
    .slv_axi_awcache (4'b0 ),                       //SLV AXI接口awcache
    .slv_axi_awprot  (3'b0 ),                       //SLV AXI接口awprot
    .slv_axi_awqos   (4'b0 ),                       //SLV AXI接口awqos
    .slv_axi_awvalid (1'b0 ),                       //SLV AXI接口awvalid
    .slv_axi_awready (  ),                       //SLV AXI接口awready
    .slv_axi_wdata   (64'b0 ),                       //SLV AXI接口wdata
    .slv_axi_wstrb   (8'b0 ),                       //SLV AXI接口wstrb
    .slv_axi_wlast   (1'b0 ),                       //SLV AXI接口wlast
    .slv_axi_wvalid  (1'b0 ),                       //SLV AXI接口wvalid
    .slv_axi_wready  (   ),                       //SLV AXI接口wready
    .slv_axi_bid     (   ),                       //SLV AXI接口bid
    .slv_axi_bresp   (   ),                       //SLV AXI接口bresp
    .slv_axi_bvalid  (   ),                       //SLV AXI接口bvalid
    .slv_axi_bready  (1'b0 ),                       //SLV AXI接口bready
    .slv_axi_arid    (flash_ar_id_o    ),                       //SLV AXI接口arid
    .slv_axi_araddr  (flash_ar_addr_o  ),                       //SLV AXI接口araddr
    .slv_axi_arregion(4'b0            ),                        //SLV AXI接口arregion
    .slv_axi_arlen   (flash_ar_len_o   ),                       //SLV AXI接口arlen
    .slv_axi_arsize  (flash_ar_size_o  ),                       //SLV AXI接口arsize
    .slv_axi_arburst (flash_ar_burst_o ),                       //SLV AXI接口arburst
    .slv_axi_arlock  (flash_ar_lock_o  ),                       //SLV AXI接口arlock
    .slv_axi_arcache (flash_ar_cache_o ),                       //SLV AXI接口arcache
    .slv_axi_arprot  (flash_ar_prot_o  ),                       //SLV AXI接口arprot
    .slv_axi_arqos   (4'b0            ),                        //SLV AXI接口arqos
    .slv_axi_arvalid (flash_ar_valid_o ),                       //SLV AXI接口arvalid
    .slv_axi_arready (flash_ar_ready_i ),                       //SLV AXI接口arready
    .slv_axi_rid     (flash_r_id_i     ),                       //SLV AXI接口rid
    .slv_axi_rdata   (flash_r_data_i   ),                       //SLV AXI接口rdata
    .slv_axi_rresp   (flash_r_resp_i   ),                       //SLV AXI接口rresp
    .slv_axi_rlast   (flash_r_last_i   ),                       //SLV AXI接口rlast
    .slv_axi_rvalid  (flash_r_valid_i  ),                       //SLV AXI接口rvalid
    .slv_axi_rready  (flash_r_ready_o  ),                       //SLV AXI接口rready
    //3.0 从侧master AXI接从设备
    .mst_axi_awid    ( ),                       //MST AXI接口awid
    .mst_axi_awaddr  ( ),                       //MST AXI接口awaddr
    .mst_axi_awregion( ),                       //MST AXI接口awregion
    .mst_axi_awlen   ( ),                       //MST AXI接口awlen
    .mst_axi_awsize  ( ),                       //MST AXI接口awsize
    .mst_axi_awburst ( ),                       //MST AXI接口awburst
    .mst_axi_awlock  ( ),                       //MST AXI接口awlock
    .mst_axi_awcache ( ),                       //MST AXI接口awcache
    .mst_axi_awprot  ( ),                       //MST AXI接口awprot
    .mst_axi_awqos   ( ),                       //MST AXI接口awqos
    .mst_axi_awvalid ( ),                       //MST AXI接口awvalid
    .mst_axi_awready (1'b0 ),                    //MST AXI接口awready
    .mst_axi_wdata   (  ),                      //MST AXI接口wdata
    .mst_axi_wstrb   (  ),                      //MST AXI接口wstrb
    .mst_axi_wlast   (  ),                      //MST AXI接口wlast
    .mst_axi_wvalid  (  ),                      //MST AXI接口wvalid
    .mst_axi_wready  (1'b0  ),                   //MST AXI接口wready
    .mst_axi_bid     (1'b0  ),                   //MST AXI接口bid
    .mst_axi_bresp   (2'b0  ),                   //MST AXI接口bresp
    .mst_axi_bvalid  (1'b0  ),                   //MST AXI接口bvalid
    .mst_axi_bready  (  ),                      //MST AXI接口bready
    .mst_axi_arid    (io_flash_ar_bits_id_o    ),                       //MST AXI接口arid
    .mst_axi_araddr  (io_flash_ar_bits_addr_o  ),                       //MST AXI接口araddr
    .mst_axi_arregion(                   ),                       //MST AXI接口arregion
    .mst_axi_arlen   (io_flash_ar_bits_len_o   ),                       //MST AXI接口arlen
    .mst_axi_arsize  (io_flash_ar_bits_size_o  ),                       //MST AXI接口arsize
    .mst_axi_arburst (io_flash_ar_bits_burst_o ),                       //MST AXI接口arburst
    .mst_axi_arlock  (io_flash_ar_bits_lock_o  ),                       //MST AXI接口arlock
    .mst_axi_arcache (io_flash_ar_bits_cache_o ),                       //MST AXI接口arcache
    .mst_axi_arprot  (io_flash_ar_bits_prot_o  ),                       //MST AXI接口arprot
    .mst_axi_arqos   (                   ),                       //MST AXI接口arqos
    .mst_axi_arvalid (io_flash_ar_valid_o ),                       //MST AXI接口arvalid
    .mst_axi_arready (io_flash_ar_ready_i ),                       //MST AXI接口arready
    .mst_axi_rid     (io_flash_r_bits_id_i     ),                       //MST AXI接口rid
    .mst_axi_rdata   (io_flash_r_bits_data_i   ),                       //MST AXI接口rdata
    .mst_axi_rresp   (io_flash_r_bits_resp_i   ),                       //MST AXI接口rresp
    .mst_axi_rlast   (io_flash_r_bits_last_i   ),                       //MST AXI接口rlast
    .mst_axi_rvalid  (io_flash_r_valid_i  ),                       //MST AXI接口rvalid
    .mst_axi_rready  (io_flash_r_ready_o  )                        //MST AXI接口rready
);


AXIAXI_64T512 #
(
    .IDW(1)                          ,   //ID 位宽设定.
    .STD(1)                          ,   //是否完全按照AXI窄传输:0=多周期窄传输适配为满传输;1=按标准窄传输适配.
    .OSL(8)                              //Outstanding: 2**OSL为真正深度.为安全,缺省为零.|
) u_AXIAXI_mmu
(
    //1.0 复位时钟系统
    .aresetn         (rst_ni),                       //AXI asyn. reset. active low.
    .aclk            (clk_i),                       //AXI clock input. rising edge work
    //2.0 主侧slave AXI接主设备
    .slv_axi_awid    (1'b0 ),                       //SLV AXI接口awid
    .slv_axi_awaddr  (64'b0 ),                       //SLV AXI接口awaddr
    .slv_axi_awregion(4'b0 ),                       //SLV AXI接口awregion
    .slv_axi_awlen   (8'b0 ),                       //SLV AXI接口awlen
    .slv_axi_awsize  (3'b0 ),                       //SLV AXI接口awsize
    .slv_axi_awburst (2'b0 ),                       //SLV AXI接口awburst
    .slv_axi_awlock  (1'b0 ),                       //SLV AXI接口awlock
    .slv_axi_awcache (4'b0 ),                       //SLV AXI接口awcache
    .slv_axi_awprot  (3'b0 ),                       //SLV AXI接口awprot
    .slv_axi_awqos   (4'b0 ),                       //SLV AXI接口awqos
    .slv_axi_awvalid (1'b0 ),                       //SLV AXI接口awvalid
    .slv_axi_awready (  ),                       //SLV AXI接口awready
    .slv_axi_wdata   (64'b0 ),                       //SLV AXI接口wdata
    .slv_axi_wstrb   (8'b0 ),                       //SLV AXI接口wstrb
    .slv_axi_wlast   (1'b0 ),                       //SLV AXI接口wlast
    .slv_axi_wvalid  (1'b0 ),                       //SLV AXI接口wvalid
    .slv_axi_wready  (   ),                       //SLV AXI接口wready
    .slv_axi_bid     (   ),                       //SLV AXI接口bid
    .slv_axi_bresp   (   ),                       //SLV AXI接口bresp
    .slv_axi_bvalid  (   ),                       //SLV AXI接口bvalid
    .slv_axi_bready  (1'b0 ),                       //SLV AXI接口bready
    .slv_axi_arid    (mmu_axi_ar_req_o_bits_id    ),                       //SLV AXI接口arid
    .slv_axi_araddr  (mmu_axi_ar_req_o_bits_addr  ),                       //SLV AXI接口araddr
    .slv_axi_arregion(mmu_axi_ar_req_o_bits_region),                       //SLV AXI接口arregion
    .slv_axi_arlen   (mmu_axi_ar_req_o_bits_len   ),                       //SLV AXI接口arlen
    .slv_axi_arsize  (mmu_axi_ar_req_o_bits_size  ),                       //SLV AXI接口arsize
    .slv_axi_arburst (mmu_axi_ar_req_o_bits_burst ),                       //SLV AXI接口arburst
    .slv_axi_arlock  (mmu_axi_ar_req_o_bits_lock  ),                       //SLV AXI接口arlock
    .slv_axi_arcache (mmu_axi_ar_req_o_bits_cache ),                       //SLV AXI接口arcache
    .slv_axi_arprot  (mmu_axi_ar_req_o_bits_prot  ),                       //SLV AXI接口arprot
    .slv_axi_arqos   (mmu_axi_ar_req_o_bits_qos   ),                       //SLV AXI接口arqos
    .slv_axi_arvalid (mmu_axi_ar_req_o_valid      ),                       //SLV AXI接口arvalid
    .slv_axi_arready (mmu_axi_ar_req_o_ready      ),                       //SLV AXI接口arready
    .slv_axi_rid     (mmu_axi_r_resp_i_bits_id    ),                       //SLV AXI接口rid
    .slv_axi_rdata   (mmu_axi_r_resp_i_bits_data  ),                       //SLV AXI接口rdata
    .slv_axi_rresp   (mmu_axi_r_resp_i_bits_resp  ),                       //SLV AXI接口rresp
    .slv_axi_rlast   (mmu_axi_r_resp_i_bits_last  ),                       //SLV AXI接口rlast
    .slv_axi_rvalid  (mmu_axi_r_resp_i_valid      ),                       //SLV AXI接口rvalid
    .slv_axi_rready  (mmu_axi_r_resp_i_ready      ),                       //SLV AXI接口rready
    //3.0 从侧master AXI接从设备
    .mst_axi_awid    ( ),                       //MST AXI接口awid
    .mst_axi_awaddr  ( ),                       //MST AXI接口awaddr
    .mst_axi_awregion( ),                       //MST AXI接口awregion
    .mst_axi_awlen   ( ),                       //MST AXI接口awlen
    .mst_axi_awsize  ( ),                       //MST AXI接口awsize
    .mst_axi_awburst ( ),                       //MST AXI接口awburst
    .mst_axi_awlock  ( ),                       //MST AXI接口awlock
    .mst_axi_awcache ( ),                       //MST AXI接口awcache
    .mst_axi_awprot  ( ),                       //MST AXI接口awprot
    .mst_axi_awqos   ( ),                       //MST AXI接口awqos
    .mst_axi_awvalid ( ),                       //MST AXI接口awvalid
    .mst_axi_awready (1'b0 ),                    //MST AXI接口awready
    .mst_axi_wdata   (  ),                      //MST AXI接口wdata
    .mst_axi_wstrb   (  ),                      //MST AXI接口wstrb
    .mst_axi_wlast   (  ),                      //MST AXI接口wlast
    .mst_axi_wvalid  (  ),                      //MST AXI接口wvalid
    .mst_axi_wready  (1'b0  ),                   //MST AXI接口wready
    .mst_axi_bid     (1'b0  ),                   //MST AXI接口bid
    .mst_axi_bresp   (2'b0  ),                   //MST AXI接口bresp
    .mst_axi_bvalid  (1'b0  ),                   //MST AXI接口bvalid
    .mst_axi_bready  (  ),                      //MST AXI接口bready
    .mst_axi_arid    (io_mmu_axi_ar_req_o_bits_id    ),                       //MST AXI接口arid
    .mst_axi_araddr  (io_mmu_axi_ar_req_o_bits_addr  ),                       //MST AXI接口araddr
    .mst_axi_arregion(io_mmu_axi_ar_req_o_bits_region),                       //MST AXI接口arregion
    .mst_axi_arlen   (io_mmu_axi_ar_req_o_bits_len   ),                       //MST AXI接口arlen
    .mst_axi_arsize  (io_mmu_axi_ar_req_o_bits_size  ),                       //MST AXI接口arsize
    .mst_axi_arburst (io_mmu_axi_ar_req_o_bits_burst ),                       //MST AXI接口arburst
    .mst_axi_arlock  (io_mmu_axi_ar_req_o_bits_lock  ),                       //MST AXI接口arlock
    .mst_axi_arcache (io_mmu_axi_ar_req_o_bits_cache ),                       //MST AXI接口arcache
    .mst_axi_arprot  (io_mmu_axi_ar_req_o_bits_prot  ),                       //MST AXI接口arprot
    .mst_axi_arqos   (io_mmu_axi_ar_req_o_bits_qos   ),                       //MST AXI接口arqos
    .mst_axi_arvalid (io_mmu_axi_ar_req_o_valid      ),                       //MST AXI接口arvalid
    .mst_axi_arready (io_mmu_axi_ar_req_o_ready      ),                       //MST AXI接口arready
    .mst_axi_rid     (io_mmu_axi_r_resp_i_bits_id    ),                       //MST AXI接口rid
    .mst_axi_rdata   (io_mmu_axi_r_resp_i_bits_data  ),                       //MST AXI接口rdata
    .mst_axi_rresp   (io_mmu_axi_r_resp_i_bits_resp  ),                       //MST AXI接口rresp
    .mst_axi_rlast   (io_mmu_axi_r_resp_i_bits_last  ),                       //MST AXI接口rlast
    .mst_axi_rvalid  (io_mmu_axi_r_resp_i_valid      ),                       //MST AXI接口rvalid
    .mst_axi_rready  (io_mmu_axi_r_resp_i_ready      )                        //MST AXI接口rready
);





AXIAXI_high_width #
(
    .MOD(2)                          ,   //模式设定:0=128-256  1=256-128  2=256-512  3=512-256
    .STD(1)                          ,   //是否完全按照AXI窄传输:0=多周期窄传输适配为满传输;1=按标准窄传输适配.
    .OSL(8)                          ,   //Outstanding: 2**OSL为真正深度.为安全,缺省为零.|
    .IDW(1)                          
) u_icache_sizedn
(
    //1.0 复位时钟系统
    .aresetn         (rst_ni),                    //AXI asyn. reset. active low.
    .aclk            (clk_i),                       //AXI clock input. rising edge work
    //2.0 主侧slave AXI接主设备
    .slv_axi_awid    (1'b0              ),                       //SLV AXI接口awid
    .slv_axi_awaddr  (64'b0             ),                       //SLV AXI接口awaddr
    .slv_axi_awregion(4'b0              ),                       //SLV AXI接口awregion
    .slv_axi_awlen   (8'b0              ),                       //SLV AXI接口awlen
    .slv_axi_awsize  (3'b0              ),                       //SLV AXI接口awsize
    .slv_axi_awburst (2'b0              ),                       //SLV AXI接口awburst
    .slv_axi_awlock  (1'b0              ),                       //SLV AXI接口awlock
    .slv_axi_awcache (4'b0              ),                       //SLV AXI接口awcache
    .slv_axi_awprot  (3'b0              ),                       //SLV AXI接口awprot
    .slv_axi_awqos   (3'b0              ),                       //SLV AXI接口awqos
    .slv_axi_awvalid (1'b0              ),                       //SLV AXI接口awvalid
    .slv_axi_awready (                  ),                       //SLV AXI接口awready
    .slv_axi_wdata   (256'b0            ),                       //SLV AXI接口wdata
    .slv_axi_wstrb   (32'b0             ),                       //SLV AXI接口wstrb
    .slv_axi_wlast   (1'b0              ),                       //SLV AXI接口wlast
    .slv_axi_wvalid  (1'b0              ),                       //SLV AXI接口wvalid
    .slv_axi_wready  (                  ),                       //SLV AXI接口wready
    .slv_axi_bid     (                  ),                       //SLV AXI接口bid
    .slv_axi_bresp   (                  ),                       //SLV AXI接口bresp
    .slv_axi_bvalid  (                  ),                       //SLV AXI接口bvalid
    .slv_axi_bready  (1'b0              ),                       //SLV AXI接口bready
    .slv_axi_arid    (axi_ar_bits_id    ),                       //SLV AXI接口arid
    .slv_axi_araddr  (axi_ar_bits_addr[31:0]  ),               //SLV AXI接口araddr
    .slv_axi_arregion(4'b0              ),                       //SLV AXI接口arregion
    .slv_axi_arlen   (axi_ar_bits_len   ),                       //SLV AXI接口arlen
    .slv_axi_arsize  (axi_ar_bits_size  ),                       //SLV AXI接口arsize
    .slv_axi_arburst (axi_ar_bits_burst ),                       //SLV AXI接口arburst
    .slv_axi_arlock  (axi_ar_bits_lock  ),                       //SLV AXI接口arlock
    .slv_axi_arcache (axi_ar_bits_cache ),                       //SLV AXI接口arcache
    .slv_axi_arprot  (axi_ar_bits_prot  ),                       //SLV AXI接口arprot
    .slv_axi_arqos   (3'b0              ),                       //SLV AXI接口arqos
    .slv_axi_arvalid (axi_ar_valid      ),                       //SLV AXI接口arvalid
    .slv_axi_arready (axi_ar_ready      ),                       //SLV AXI接口arready
    .slv_axi_rid     (axi_r_bits_id     ),                       //SLV AXI接口rid
    .slv_axi_rdata   (axi_r_bits_data   ),                       //SLV AXI接口rdata
    .slv_axi_rresp   (axi_r_bits_resp   ),                       //SLV AXI接口rresp
    .slv_axi_rlast   (axi_r_bits_last   ),                       //SLV AXI接口rlast
    .slv_axi_rvalid  (axi_r_valid       ),                       //SLV AXI接口rvalid
    .slv_axi_rready  (axi_r_ready       ),                       //SLV AXI接口rready
    //3.0 从侧master AXI接从设备
    .mst_axi_awid    (                         ),                //MST AXI接口awid
    .mst_axi_awaddr  (                         ),                //MST AXI接口awaddr
    .mst_axi_awregion(                         ),                //MST AXI接口awregion
    .mst_axi_awlen   (                         ),                //MST AXI接口awlen
    .mst_axi_awsize  (                         ),                //MST AXI接口awsize
    .mst_axi_awburst (                         ),                //MST AXI接口awburst
    .mst_axi_awlock  (                         ),                //MST AXI接口awlock
    .mst_axi_awcache (                         ),                //MST AXI接口awcache
    .mst_axi_awprot  (                         ),                //MST AXI接口awprot
    .mst_axi_awqos   (                         ),                //MST AXI接口awqos
    .mst_axi_awvalid (                         ),                //MST AXI接口awvalid
    .mst_axi_awready (1'b0                     ),                //MST AXI接口awready
    .mst_axi_wdata   (                         ),                //MST AXI接口wdata
    .mst_axi_wstrb   (                         ),                //MST AXI接口wstrb
    .mst_axi_wlast   (                         ),                //MST AXI接口wlast
    .mst_axi_wvalid  (                         ),                //MST AXI接口wvalid
    .mst_axi_wready  (1'b0                     ),                //MST AXI接口wready
    .mst_axi_bid     (                         ),                //MST AXI接口bid
    .mst_axi_bresp   (2'b0                     ),                //MST AXI接口bresp
    .mst_axi_bvalid  (1'b0                     ),                //MST AXI接口bvalid
    .mst_axi_bready  (                         ),                //MST AXI接口bready
    .mst_axi_arid    (io_icache_ar_bits_id_o   ),                //MST AXI接口arid
    .mst_axi_araddr  (io_icache_ar_bits_addr_o[31:0] ),                //MST AXI接口araddr
    .mst_axi_arregion(                         ),                //MST AXI接口arregion
    .mst_axi_arlen   (io_icache_ar_bits_len_o  ),                //MST AXI接口arlen
    .mst_axi_arsize  (io_icache_ar_bits_size_o ),                //MST AXI接口arsize
    .mst_axi_arburst (io_icache_ar_bits_burst_o),                //MST AXI接口arburst
    .mst_axi_arlock  (io_icache_ar_bits_lock_o ),                //MST AXI接口arlock
    .mst_axi_arcache (io_icache_ar_bits_cache_o),                //MST AXI接口arcache
    .mst_axi_arprot  (io_icache_ar_bits_prot_o ),                //MST AXI接口arprot
    .mst_axi_arqos   (                         ),                //MST AXI接口arqos
    .mst_axi_arvalid (io_icache_ar_valid_o     ),                //MST AXI接口arvalid
    .mst_axi_arready (io_icache_ar_ready_i     ),                //MST AXI接口arready
    .mst_axi_rid     (io_icache_r_bits_id_i    ),                //MST AXI接口rid
    .mst_axi_rdata   (io_icache_r_bits_data_i  ),                //MST AXI接口rdata
    .mst_axi_rresp   (io_icache_r_bits_resp_i  ),                //MST AXI接口rresp
    .mst_axi_rlast   (io_icache_r_bits_last_i  ),                //MST AXI接口rlast
    .mst_axi_rvalid  (io_icache_r_valid_i      ),                //MST AXI接口rvalid
    .mst_axi_rready  (io_icache_r_ready_o      )                 //MST AXI接口rready
);
assign io_icache_ar_bits_addr_o[63:32] = 32'b0;


sizedn300 #(
    .ID_WIDTH  (18),
    .ADDR_WIDTH  (64),
    .US_DATA_WIDTH  (512),
    .DS_DATA_WIDTH  (64)
  ) u_llpp_sizedn(

      .us_araddr               (io_axis_ar_bits_addr_i   ),
      .us_arburst              (io_axis_ar_bits_burst_i  ),
      .us_arcache              (io_axis_ar_bits_cache_i  ),
      .us_arid                 (io_axis_ar_bits_id_i     ),
      .us_arlen                (io_axis_ar_bits_len_i    ),
      .us_arlock               (io_axis_ar_bits_lock_i   ),
      .us_arprot               (io_axis_ar_bits_prot_i   ),
      .us_arready              (io_axis_ar_ready_o       ),
      .us_arsize               (io_axis_ar_bits_size_i   ),
      .us_arvalid              (io_axis_ar_valid_i       ),
      .us_awaddr               (io_axis_aw_bits_addr_i   ),
      .us_awburst              (io_axis_aw_bits_burst_i  ),
      .us_awcache              (io_axis_aw_bits_cache_i  ),
      .us_awid                 (io_axis_aw_bits_id_i     ),
      .us_awlen                (io_axis_aw_bits_len_i    ),
      .us_awlock               (io_axis_aw_bits_lock_i   ),
      .us_awprot               (io_axis_aw_bits_prot_i   ),
      .us_awready              (io_axis_aw_ready_o       ),
      .us_awsize               (io_axis_aw_bits_size_i   ),
      .us_awvalid              (io_axis_aw_valid_i       ),
      .us_bid                  (io_axis_b_bits_id_o      ),
      .us_bready               (io_axis_b_ready_i        ),
      .us_bresp                (io_axis_b_bits_resp_o    ),
      .us_bvalid               (io_axis_b_valid_o        ),
      .us_rdata                (io_axis_r_bits_data_o    ),
      .us_rid                  (io_axis_r_bits_id_o      ),
      .us_rlast                (io_axis_r_bits_last_o    ),
      .us_rready               (io_axis_r_ready_i        ),
      .us_rresp                (io_axis_r_bits_resp_o    ),
      .us_rvalid               (io_axis_r_valid_o        ),
      .us_wdata                (io_axis_w_bits_data_i    ),
      .us_wlast                (io_axis_w_bits_last_i    ),
      .us_wready               (io_axis_w_ready_o        ),
      .us_wstrb                (io_axis_w_bits_strb_i    ),
      .us_wvalid               (io_axis_w_valid_i        ),

      .ds_araddr               (axis_ar_addr_i   ),
      .ds_arburst              (axis_ar_burst_i  ),
      .ds_arcache              (axis_ar_cache_i  ),
      .ds_arlen                (axis_ar_len_i    ),
      .ds_arlock               (axis_ar_lock_i   ),
      .ds_arprot               (axis_ar_prot_i   ),
      .ds_arready              (axis_ar_ready_o  ),
      .ds_arsize               (axis_ar_size_i   ),
      .ds_arvalid              (axis_ar_valid_i  ),
      .ds_awaddr               (axis_aw_addr_i   ),
      .ds_awburst              (axis_aw_burst_i  ),
      .ds_awcache              (axis_aw_cache_i  ),
      .ds_awlen                (axis_aw_len_i    ),
      .ds_awlock               (axis_aw_lock_i   ),
      .ds_awprot               (axis_aw_prot_i   ),
      .ds_awready              (axis_aw_ready_o  ),
      .ds_awsize               (axis_aw_size_i   ),
      .ds_awvalid              (axis_aw_valid_i  ),
      .ds_bready               (axis_b_ready_i   ),
      .ds_bresp                (axis_b_resp_o    ),
      .ds_bvalid               (axis_b_valid_o   ),
      .ds_rdata                (axis_r_data_o    ),
      .ds_rlast                (axis_r_last_o    ),
      .ds_rready               (axis_r_ready_i   ),
      .ds_rresp                (axis_r_resp_o    ),
      .ds_rvalid               (axis_r_valid_o   ),
      .ds_wdata                (axis_w_data_i    ),
      .ds_wlast                (axis_w_last_i    ),
      .ds_wready               (axis_w_ready_o   ),
      .ds_wstrb                (axis_w_strb_i    ),
      .ds_wvalid               (axis_w_valid_i   ),

      .aclk                    (clk_i),
      .aresetn                 (rst_ni)
);



endmodule // ariane
