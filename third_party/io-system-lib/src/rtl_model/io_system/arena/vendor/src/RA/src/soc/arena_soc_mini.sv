`include "axi/typedef.svh"
// Copyright 2018 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License"); you may not use this file except in
// compliance with the License.  You may obtain a copy of the License at
// http://solderpad.org/licenses/SHL-0.51. Unless required by applicable law
// or agreed to in writing, software, hardware and materials distributed under
// this License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

// Description: Xilinx FPGA top-level
// Author: Florian Zaruba <zarubaf@iis.ee.ethz.ch>

module arena_soc #(
  // Preserve the original board behavior unless a co-simulation wrapper opts in.
  parameter bit COSIM = 1'b0
) (
  input  logic         sys_clk         ,  // 50M
  input  logic         cpu_reset       ,  // CPU subsystem reset

  input  logic [63:0]  config_araddr,
  input  logic [1:0]   config_arburst,
  input  logic [3:0]   config_arcache,
  input  logic [3:0]   config_arid,
  input  logic [7:0]   config_arlen,
  input  logic         config_arlock,
  input  logic [2:0]   config_arprot,
  output logic         config_arready,
  input  logic [2:0]   config_arsize,
  input  logic         config_arvalid,
  input  logic [63:0]  config_awaddr,
  input  logic [1:0]   config_awburst,
  input  logic [3:0]   config_awcache,
  input  logic [3:0]   config_awid,
  input  logic [7:0]   config_awlen,
  input  logic         config_awlock,
  input  logic [2:0]   config_awprot,
  output logic         config_awready,
  input  logic [2:0]   config_awsize,
  input  logic         config_awvalid,
  output logic [3:0]   config_bid,
  input  logic         config_bready,
  output logic [1:0]   config_bresp,
  output logic         config_bvalid,
  output logic [511:0] config_rdata,
  output logic [3:0]   config_rid,
  output logic         config_rlast,
  input  logic         config_rready,
  output logic [1:0]   config_rresp,
  output logic         config_rvalid,
  input  logic [511:0] config_wdata,
  input  logic         config_wlast,
  output logic         config_wready,
  input  logic [63:0]  config_wstrb,
  input  logic         config_wvalid,

  output logic [63:0]  axi_ddr_araddr,
  output logic [1:0]   axi_ddr_arburst,
  output logic [3:0]   axi_ddr_arcache,
  output logic [7:0]   axi_ddr_arid,
  output logic [7:0]   axi_ddr_arlen,
  output logic         axi_ddr_arlock,
  output logic [2:0]   axi_ddr_arprot,
  input  logic         axi_ddr_arready,
  output logic [2:0]   axi_ddr_arsize,
  output logic         axi_ddr_arvalid,
  output logic [63:0]  axi_ddr_awaddr,
  output logic [1:0]   axi_ddr_awburst,
  output logic [3:0]   axi_ddr_awcache,
  output logic [7:0]   axi_ddr_awid,
  output logic [7:0]   axi_ddr_awlen,
  output logic         axi_ddr_awlock,
  output logic [2:0]   axi_ddr_awprot,
  input  logic         axi_ddr_awready,
  output logic [2:0]   axi_ddr_awsize,
  output logic         axi_ddr_awvalid,
  input  logic [7:0]   axi_ddr_bid,
  output logic         axi_ddr_bready,
  input  logic [1:0]   axi_ddr_bresp,
  input  logic         axi_ddr_bvalid,
  input  logic [511:0] axi_ddr_rdata,
  input  logic [7:0]   axi_ddr_rid,
  input  logic         axi_ddr_rlast,
  output logic         axi_ddr_rready,
  input  logic [1:0]   axi_ddr_rresp,
  input  logic         axi_ddr_rvalid,
  output logic [511:0] axi_ddr_wdata,
  output logic         axi_ddr_wlast,
  input  logic         axi_ddr_wready,
  output logic [63:0]  axi_ddr_wstrb,
  output logic         axi_ddr_wvalid,


  input  logic         i_rtc         ,
              
              
/////////   JTAG IO   /////////
  input  logic         i_jtag_tck    ,
  input  logic         i_jtag_tms    ,
  input  logic         i_jtag_tdi    ,
  input  logic         i_jtag_trst_n ,
  output logic         o_jtag_tdo    ,

/////////   UART_AXI IO   /////////
  input  logic         i_rx              ,
  output logic         o_tx              ,

/////////   UART_APB IO   /////////
  input  logic         i_uart_rx       ,
  output logic         o_uart_tx       ,

/////////   SD IO   /////////
  output o_sdio_ck,
  inout  io_sdio_cmd,
  inout  io_sdio_data0,
  inout  io_sdio_data1,
  inout  io_sdio_data2,
  inout  io_sdio_data3,
  inout  io_sdio_data4,
  inout  io_sdio_data5,
  inout  io_sdio_data6,
  inout  io_sdio_data7,

/////////   IIS IO   /////////
  inout  io_iis_sd,
  inout  io_iis_ws,
  inout  io_iis_ck,
  inout  io_iis_mck,

/////////   SPI IO   /////////
  inout  io_spi_clk,
  inout  io_spi_miso,
  inout  io_spi_mosi,
  inout  io_spi_cs_n,

/////////   GPIO IO   /////////
  inout [15:0] io_gpio,

/////////   IIC IO   /////////
  inout io_iic_scl,
  inout io_iic_sda

);

// Useless, only to unify ports with arena_soc.sv
assign io_sdio_data4 = 'z;
assign io_sdio_data5 = 'z;
assign io_sdio_data6 = 'z;
assign io_sdio_data7 = 'z;

assign io_iis_sd = 'z;
assign io_iis_ws = 'z;
assign io_iis_ck = 'z;
assign io_iis_mck = 'z;

assign io_gpio = 'z;


// CVA6 Xilinx configuration
localparam type rvfi_probes_t = struct packed {
  logic csr;
  logic instr;
};


// CVA6 Xilinx configuration
function automatic config_pkg::cva6_cfg_t build_fpga_config(config_pkg::cva6_user_cfg_t CVA6UserCfg);
  config_pkg::cva6_user_cfg_t cfg = CVA6UserCfg;
  cfg.RVZiCond = bit'(0);
  cfg.NrNonIdempotentRules = unsigned'(1);
  cfg.NonIdempotentAddrBase = 1024'({64'b0});
  cfg.NonIdempotentLength = 1024'({64'h80000000});
  return build_config_pkg::build_config(cfg);
endfunction

// CVA6 Xilinx configuration
localparam config_pkg::cva6_cfg_t CVA6Cfg = build_fpga_config(cva6_config_pkg::cva6_cfg);



logic [7:0] w_sdio_data_oe;
logic [7:0] w_sdio_data_i;
logic [7:0] w_sdio_data_o;
logic w_sdio_cmd_oe;
logic w_sdio_cmd_i;
logic w_sdio_cmd_o;



logic w_iis_sd_i  ;
logic w_iis_sd_o  ;
logic w_iis_sd_oe ;
logic w_iis_ws_i  ;
logic w_iis_ws_o  ;
logic w_iis_ws_oe ;
logic w_iis_ck_i  ;
logic w_iis_ck_o  ;
logic w_iis_ck_oe ;
logic w_iis_mck_o ;
logic w_iis_mck_oe;



logic w_spi_clk_i  ;
logic w_spi_clk_oe ;
logic w_spi_clk_o  ;
logic w_spi_cs_n_oe;
logic w_spi_cs_n_o ;
logic w_spi_miso_i ;
logic w_spi_miso_oe;
logic w_spi_miso_o ;
logic w_spi_mosi_i ;
logic w_spi_mosi_oe;
logic w_spi_mosi_o ;

logic [15:0] w_gpio_oe;
logic [15:0] w_gpio_o;
logic [15:0] w_gpio_i;

// The mini board left these active peripheral inputs undriven. AXI-only
// co-simulation provides stable idle values without adding pin models.
generate if (COSIM) begin : gen_cosim_idle_inputs
  assign w_gpio_i = 16'b0;
  assign w_spi_clk_i = 1'b0;
  assign w_spi_miso_i = 1'b1;
  assign w_spi_mosi_i = 1'b1;
end endgenerate


logic w_iic_scl_i;
logic w_iic_sda_i;
logic w_iic_scl_o;
logic w_iic_sda_o;



// 24 MByte in 8 byte words
localparam NumWords = (24 * 1024 * 1024) / 8;
  
// WARNING: If NBSlave is modified, Xilinx's IPs under fpga/xilinx need to be updated with the new AXI id width and regenerated.
// Otherwise reads and writes to DRAM may be returned to the wrong master and the crossbar will freeze. See issue #568.
localparam AxiAddrWidth = 64;
localparam AxiDataWidth = 512;// 64 -> 512
localparam AxiIdWidthMaster = 8;	// 4  -> 8
localparam AxiIdWidthSlaves = AxiIdWidthMaster + 4; // 5 -> 9 ->10(DMA)
localparam AxiUserWidth = CVA6Cfg.AxiUserWidth;

/*
`AXI_TYPEDEF_ALL(axi_slave,
                 logic [    AxiAddrWidth-1:0],
                 logic [AxiIdWidthSlaves-1:0],
                 logic [    AxiDataWidth-1:0],
                 logic [(AxiDataWidth/8)-1:0],
                 logic [    AxiUserWidth-1:0])
*/


// disable test-enable
logic test_en;
logic ndmreset;
logic ndmreset_n;
logic debug_req_irq;
logic timer_irq;
logic ipi;

logic eth_clk;
logic spi_clk_i;
logic phy_tx_clk;
logic sd_clk_sys;

logic ddr_sync_reset;
logic ddr_clk;

logic rst_n, rst;
logic rtc;

logic          axim_aw_ready;
logic          axim_aw_valid;
logic  [3:0]   axim_aw_bits_id;
logic  [63:0]  axim_aw_bits_addr;
logic  [7:0]   axim_aw_bits_len;
logic  [2:0]   axim_aw_bits_size;
logic  [1:0]   axim_aw_bits_burst;
logic          axim_aw_bits_lock;
logic  [3:0]   axim_aw_bits_cache;
logic  [2:0]   axim_aw_bits_prot;
logic          axim_w_ready;
logic          axim_w_valid;
logic  [3:0]   axim_w_bits_id;
logic  [511:0] axim_w_bits_data;
logic  [63:0]  axim_w_bits_strb;
logic          axim_w_bits_last;
logic          axim_b_ready;
logic          axim_b_valid;
logic  [1:0]   axim_b_bits_resp;
logic  [3:0]   axim_b_bits_id;
logic          axim_ar_ready;
logic          axim_ar_valid;
logic  [3:0]   axim_ar_bits_id;
logic  [63:0]  axim_ar_bits_addr;
logic  [7:0]   axim_ar_bits_len;
logic  [2:0]   axim_ar_bits_size;
logic  [1:0]   axim_ar_bits_burst;
logic          axim_ar_bits_lock;
logic  [3:0]   axim_ar_bits_cache;
logic  [2:0]   axim_ar_bits_prot;
logic          axim_r_ready;
logic          axim_r_valid;
logic  [3:0]   axim_r_bits_id;
logic  [511:0] axim_r_bits_data;
logic  [1:0]   axim_r_bits_resp;
logic          axim_r_bits_last;

logic          flash_ar_ready;
logic          flash_ar_valid;
logic          flash_ar_bits_id;
logic  [63:0]  flash_ar_bits_addr;
logic  [7:0]   flash_ar_bits_len;
logic  [2:0]   flash_ar_bits_size;
logic  [1:0]   flash_ar_bits_burst;
logic          flash_ar_bits_lock;
logic  [3:0]   flash_ar_bits_cache;
logic  [2:0]   flash_ar_bits_prot;
logic          flash_r_ready;
logic          flash_r_valid;
logic          flash_r_bits_id;
logic  [511:0] flash_r_bits_data;
logic  [1:0]   flash_r_bits_resp;
logic          flash_r_bits_last;
logic  [2:0]   flash_r_bits_id_nc3;

logic          icache_ar_ready;
logic          icache_ar_valid;
logic          icache_ar_bits_id;
logic  [63:0]  icache_ar_bits_addr;
logic  [7:0]   icache_ar_bits_len;
logic  [2:0]   icache_ar_bits_size;
logic  [1:0]   icache_ar_bits_burst;
logic          icache_ar_bits_lock;
logic  [3:0]   icache_ar_bits_cache;
logic  [2:0]   icache_ar_bits_prot;
logic          icache_r_ready;
logic          icache_r_valid;
logic          icache_r_bits_id;
logic  [511:0] icache_r_bits_data;
logic  [1:0]   icache_r_bits_resp;
logic          icache_r_bits_last;
logic  [2:0]   icache_r_bits_id_nc3;

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
logic [511:0] mmu_axi_r_resp_i_bits_data;
logic [1:0]   mmu_axi_r_resp_i_bits_resp;
logic         mmu_axi_r_resp_i_bits_last;


logic          llpp_aw_ready;
logic          llpp_aw_valid;
logic          llpp_aw_bits_id;
logic  [63:0]  llpp_aw_bits_addr;
logic  [7:0]   llpp_aw_bits_len;
logic  [2:0]   llpp_aw_bits_size;
logic  [1:0]   llpp_aw_bits_burst;
logic          llpp_aw_bits_lock;
logic  [3:0]   llpp_aw_bits_cache;
logic  [2:0]   llpp_aw_bits_prot;
logic          llpp_w_ready;
logic          llpp_w_valid;
logic          llpp_w_bits_id;
logic  [63:0]  llpp_w_bits_data;
logic  [7:0]   llpp_w_bits_strb;
logic          llpp_w_bits_last;
logic          llpp_b_ready;
logic          llpp_b_valid;
logic  [1:0]   llpp_b_bits_resp;
logic          llpp_b_bits_id;
logic          llpp_ar_ready;
logic          llpp_ar_valid;
logic          llpp_ar_bits_id;
logic  [63:0]  llpp_ar_bits_addr;
logic  [7:0]   llpp_ar_bits_len;
logic  [2:0]   llpp_ar_bits_size;
logic  [1:0]   llpp_ar_bits_burst;
logic          llpp_ar_bits_lock;
logic  [3:0]   llpp_ar_bits_cache;
logic  [2:0]   llpp_ar_bits_prot;
logic          llpp_r_ready;
logic          llpp_r_valid;
logic          llpp_r_bits_id;
logic  [63:0]  llpp_r_bits_data;
logic  [1:0]   llpp_r_bits_resp;
logic          llpp_r_bits_last;

logic          axis_aw_ready;
logic          axis_aw_valid;
logic  [7:0]   axis_aw_bits_id;
logic  [63:0]  axis_aw_bits_addr;
logic  [7:0]   axis_aw_bits_len;
logic  [2:0]   axis_aw_bits_size;
logic  [1:0]   axis_aw_bits_burst;
logic          axis_aw_bits_lock;
logic  [3:0]   axis_aw_bits_cache;
logic  [2:0]   axis_aw_bits_prot;
logic          axis_w_ready;
logic          axis_w_valid;
logic  [7:0]   axis_w_bits_id;
logic  [511:0] axis_w_bits_data;
logic  [63:0]  axis_w_bits_strb;
logic          axis_w_bits_last;
logic          axis_b_ready;
logic          axis_b_valid;
logic  [1:0]   axis_b_bits_resp;
logic  [7:0]   axis_b_bits_id;
logic          axis_ar_ready;
logic          axis_ar_valid;
logic  [7:0]   axis_ar_bits_id;
logic  [63:0]  axis_ar_bits_addr;
logic  [7:0]   axis_ar_bits_len;
logic  [2:0]   axis_ar_bits_size;
logic  [1:0]   axis_ar_bits_burst;
logic          axis_ar_bits_lock;
logic  [3:0]   axis_ar_bits_cache;
logic  [2:0]   axis_ar_bits_prot;
logic          axis_r_ready;
logic          axis_r_valid;
logic  [7:0]   axis_r_bits_id;
logic  [511:0] axis_r_bits_data;
logic  [1:0]   axis_r_bits_resp;
logic          axis_r_bits_last;


logic          coprocessor_aw_ready;
logic          coprocessor_aw_valid;
logic  [3:0]   coprocessor_aw_bits_id;
logic  [63:0]  coprocessor_aw_bits_addr;
logic  [7:0]   coprocessor_aw_bits_len;
logic  [2:0]   coprocessor_aw_bits_size;
logic  [1:0]   coprocessor_aw_bits_burst;
logic          coprocessor_aw_bits_lock;
logic  [3:0]   coprocessor_aw_bits_cache;
logic  [2:0]   coprocessor_aw_bits_prot;
logic          coprocessor_w_ready;
logic          coprocessor_w_valid;
logic  [3:0]   coprocessor_w_bits_id;
logic  [511:0] coprocessor_w_bits_data;
logic  [63:0]  coprocessor_w_bits_strb;
logic          coprocessor_w_bits_last;
logic          coprocessor_b_ready;
logic          coprocessor_b_valid;
logic  [1:0]   coprocessor_b_bits_resp;
logic  [3:0]   coprocessor_b_bits_id;
logic          coprocessor_ar_ready;
logic          coprocessor_ar_valid;
logic  [3:0]   coprocessor_ar_bits_id;
logic  [63:0]  coprocessor_ar_bits_addr;
logic  [7:0]   coprocessor_ar_bits_len;
logic  [2:0]   coprocessor_ar_bits_size;
logic  [1:0]   coprocessor_ar_bits_burst;
logic          coprocessor_ar_bits_lock;
logic  [3:0]   coprocessor_ar_bits_cache;
logic  [2:0]   coprocessor_ar_bits_prot;
logic          coprocessor_r_ready;
logic          coprocessor_r_valid;
logic  [3:0]   coprocessor_r_bits_id;
logic  [511:0] coprocessor_r_bits_data;
logic  [1:0]   coprocessor_r_bits_resp;
logic          coprocessor_r_bits_last;

// AME has an independent AXI master and reaches DDR through bmc300.us6.
logic          ame_aw_ready, ame_aw_valid;
logic  [3:0]   ame_aw_bits_id;
logic  [63:0]  ame_aw_bits_addr;
logic  [7:0]   ame_aw_bits_len;
logic  [2:0]   ame_aw_bits_size;
logic  [1:0]   ame_aw_bits_burst;
logic          ame_aw_bits_lock;
logic  [3:0]   ame_aw_bits_cache;
logic  [2:0]   ame_aw_bits_prot;
logic          ame_w_ready, ame_w_valid;
logic  [511:0] ame_w_bits_data;
logic  [63:0]  ame_w_bits_strb;
logic          ame_w_bits_last;
logic          ame_b_ready, ame_b_valid;
logic  [1:0]   ame_b_bits_resp;
logic  [3:0]   ame_b_bits_id;
logic          ame_ar_ready, ame_ar_valid;
logic  [3:0]   ame_ar_bits_id;
logic  [63:0]  ame_ar_bits_addr;
logic  [7:0]   ame_ar_bits_len;
logic  [2:0]   ame_ar_bits_size;
logic  [1:0]   ame_ar_bits_burst;
logic          ame_ar_bits_lock;
logic  [3:0]   ame_ar_bits_cache;
logic  [2:0]   ame_ar_bits_prot;
logic          ame_r_ready, ame_r_valid;
logic  [3:0]   ame_r_bits_id;
logic  [511:0] ame_r_bits_data;
logic  [1:0]   ame_r_bits_resp;
logic          ame_r_bits_last;
logic          ame_hasStore;

// we need to switch reset polarity
assign cpu_resetn = ~cpu_reset;

logic pll_locked;

// ROM
logic                    rom_req;
logic [AxiAddrWidth-1:0] rom_addr;
logic [AxiDataWidth-1:0] rom_rdata;

// IRQ
logic [1:0] irq;
assign test_en    = 1'b0;

logic [31:0] w_core0_boot_addr;
logic [18:0] w_core0_itcm_startAddr;
logic [4:0]  w_core0_itcm_size;
logic [18:0] w_core0_dtcm_startAddr;
logic [4:0]  w_core0_dtcm_size;
logic [19:0] w_core0_llpp_startAddr;
logic [4:0]  w_core0_llpp_size;
logic [4:0]  w_core0_flash_startAddr;
logic [4:0]  w_core0_flash_size;
logic [31:0] w_core1_boot_addr;
logic [18:0] w_core1_itcm_startAddr;
logic [4:0]  w_core1_itcm_size;
logic [18:0] w_core1_dtcm_startAddr;
logic [4:0]  w_core1_dtcm_size;
logic [19:0] w_core1_llpp_startAddr;
logic [4:0]  w_core1_llpp_size;
logic [4:0]  w_core1_flash_startAddr;
logic [4:0]  w_core1_flash_size;
logic [31:0] w_core2_boot_addr;
logic [18:0] w_core2_itcm_startAddr;
logic [4:0]  w_core2_itcm_size;
logic [18:0] w_core2_dtcm_startAddr;
logic [4:0]  w_core2_dtcm_size;
logic [19:0] w_core2_llpp_startAddr;
logic [4:0]  w_core2_llpp_size;
logic [4:0]  w_core2_flash_startAddr;
logic [4:0]  w_core2_flash_size;
logic [31:0] w_core3_boot_addr;
logic [18:0] w_core3_itcm_startAddr;
logic [4:0]  w_core3_itcm_size;
logic [18:0] w_core3_dtcm_startAddr;
logic [4:0]  w_core3_dtcm_size;
logic [19:0] w_core3_llpp_startAddr;
logic [4:0]  w_core3_llpp_size;
logic [4:0]  w_core3_flash_startAddr;
logic [4:0]  w_core3_flash_size;

logic w_core0_clk;
logic w_core1_clk;
logic w_core2_clk;
logic w_core3_clk;
logic w_dpu_clk;
logic w_dma_clk;
logic w_uart_clk;
logic w_sdio_clk;
logic w_hsm_clk;


logic w_sys_pclk;
logic w_spi_pclk;
logic w_iic_pclk;
logic w_iis_pclk;
logic w_wdt_pclk;
logic w_wdt_extclk;
logic w_timer_pclk;
logic w_timer_extclk;
logic w_rtc_pclk;
logic w_rtc_extclk;
logic w_analog_pclk;
logic w_dma_pclk;
logic w_uart_pclk;
logic w_gpio_pclk;
logic w_gpio_extclk;


logic w_core0_rst_n;
logic w_core1_rst_n;
logic w_core2_rst_n;
logic w_core3_rst_n;
logic w_dpu_rst_n;
logic w_dma_rst_n;
logic w_uart_rst_n;
logic w_sdio_rst_n;
logic w_hsm_rst_n;
logic w_sys_prst_n;
logic w_spi_prst_n;
logic w_iic_prst_n;
logic w_iis_prst_n;
logic w_wdt_prst_n;
logic w_timer_prst_n;
logic w_timer_extrst_n;
logic w_rtc_prst_n;
logic w_rtc_extrst_n;
logic w_analog_prst_n;
logic w_dma_prst_n;
logic w_uart_prst_n;
logic w_gpio_prst_n;



logic          axi_ram_aw_ready;
logic          axi_ram_aw_valid;
logic  [7:0]   axi_ram_aw_id;
logic  [63:0]  axi_ram_aw_addr;
logic  [7:0]   axi_ram_aw_len;
logic  [2:0]   axi_ram_aw_size;
logic  [1:0]   axi_ram_aw_burst;
logic          axi_ram_aw_lock;
logic  [3:0]   axi_ram_aw_cache;
logic  [2:0]   axi_ram_aw_prot;
logic          axi_ram_w_ready;
logic          axi_ram_w_valid;
logic  [7:0]   axi_ram_w_id;
logic  [511:0] axi_ram_w_data;
logic  [63:0]  axi_ram_w_strb;
logic          axi_ram_w_last;
logic          axi_ram_b_ready;
logic          axi_ram_b_valid;
logic  [1:0]   axi_ram_b_resp;
logic  [7:0]   axi_ram_b_id;
logic          axi_ram_ar_ready;
logic          axi_ram_ar_valid;
logic  [7:0]   axi_ram_ar_id;
logic  [63:0]  axi_ram_ar_addr;
logic  [7:0]   axi_ram_ar_len;
logic  [2:0]   axi_ram_ar_size;
logic  [1:0]   axi_ram_ar_burst;
logic          axi_ram_ar_lock;
logic  [3:0]   axi_ram_ar_cache;
logic  [2:0]   axi_ram_ar_prot;
logic          axi_ram_r_ready;
logic          axi_ram_r_valid;
logic  [7:0]   axi_ram_r_id;
logic  [511:0] axi_ram_r_data;
logic  [1:0]   axi_ram_r_resp;
logic          axi_ram_r_last;

logic          axi_dram_aw_ready;
logic          axi_dram_aw_valid;
logic  [7:0]   axi_dram_aw_id;
logic  [63:0]  axi_dram_aw_addr;
logic  [7:0]   axi_dram_aw_len;
logic  [2:0]   axi_dram_aw_size;
logic  [1:0]   axi_dram_aw_burst;
logic          axi_dram_aw_lock;
logic  [3:0]   axi_dram_aw_cache;
logic  [2:0]   axi_dram_aw_prot;
logic          axi_dram_w_ready;
logic          axi_dram_w_valid;
logic  [7:0]   axi_dram_w_id;
logic  [511:0] axi_dram_w_data;
logic  [63:0]  axi_dram_w_strb;
logic          axi_dram_w_last;
logic          axi_dram_b_ready;
logic          axi_dram_b_valid;
logic  [1:0]   axi_dram_b_resp;
logic  [7:0]   axi_dram_b_id;
logic          axi_dram_ar_ready;
logic          axi_dram_ar_valid;
logic  [7:0]   axi_dram_ar_id;
logic  [63:0]  axi_dram_ar_addr;
logic  [7:0]   axi_dram_ar_len;
logic  [2:0]   axi_dram_ar_size;
logic  [1:0]   axi_dram_ar_burst;
logic          axi_dram_ar_lock;
logic  [3:0]   axi_dram_ar_cache;
logic  [2:0]   axi_dram_ar_prot;
logic          axi_dram_r_ready;
logic          axi_dram_r_valid;
logic  [7:0]   axi_dram_r_id;
logic  [511:0] axi_dram_r_data;
logic  [1:0]   axi_dram_r_resp;
logic          axi_dram_r_last;

logic          axi_rom_aw_ready;
logic          axi_rom_aw_valid;
logic  [7:0]   axi_rom_aw_id;
logic  [63:0]  axi_rom_aw_addr;
logic  [7:0]   axi_rom_aw_len;
logic  [2:0]   axi_rom_aw_size;
logic  [1:0]   axi_rom_aw_burst;
logic          axi_rom_aw_lock;
logic  [3:0]   axi_rom_aw_cache;
logic  [2:0]   axi_rom_aw_prot;
logic          axi_rom_w_ready;
logic          axi_rom_w_valid;
logic  [7:0]   axi_rom_w_id;
logic  [511:0] axi_rom_w_data;
logic  [63:0]  axi_rom_w_strb;
logic          axi_rom_w_last;
logic          axi_rom_b_ready;
logic          axi_rom_b_valid;
logic  [1:0]   axi_rom_b_resp;
logic  [7:0]   axi_rom_b_id;
logic          axi_rom_ar_ready;
logic          axi_rom_ar_valid;
logic  [7:0]   axi_rom_ar_id;
logic  [63:0]  axi_rom_ar_addr;
logic  [7:0]   axi_rom_ar_len;
logic  [2:0]   axi_rom_ar_size;
logic  [1:0]   axi_rom_ar_burst;
logic          axi_rom_ar_lock;
logic  [3:0]   axi_rom_ar_cache;
logic  [2:0]   axi_rom_ar_prot;
logic          axi_rom_r_ready;
logic          axi_rom_r_valid;
logic  [7:0]   axi_rom_r_id;
logic  [511:0] axi_rom_r_data;
logic  [1:0]   axi_rom_r_resp;
logic          axi_rom_r_last;


logic          axi_clint_aw_ready;
logic          axi_clint_aw_valid;
logic  [7:0]   axi_clint_aw_id;
logic  [63:0]  axi_clint_aw_addr;
logic  [7:0]   axi_clint_aw_len;
logic  [2:0]   axi_clint_aw_size;
logic  [1:0]   axi_clint_aw_burst;
logic          axi_clint_aw_lock;
logic  [3:0]   axi_clint_aw_cache;
logic  [2:0]   axi_clint_aw_prot;
logic          axi_clint_w_ready;
logic          axi_clint_w_valid;
logic  [7:0]   axi_clint_w_id;
logic  [511:0] axi_clint_w_data;
logic  [63:0]  axi_clint_w_strb;
logic          axi_clint_w_last;
logic          axi_clint_b_ready;
logic          axi_clint_b_valid;
logic  [1:0]   axi_clint_b_resp;
logic  [7:0]   axi_clint_b_id;
logic          axi_clint_ar_ready;
logic          axi_clint_ar_valid;
logic  [7:0]   axi_clint_ar_id;
logic  [63:0]  axi_clint_ar_addr;
logic  [7:0]   axi_clint_ar_len;
logic  [2:0]   axi_clint_ar_size;
logic  [1:0]   axi_clint_ar_burst;
logic          axi_clint_ar_lock;
logic  [3:0]   axi_clint_ar_cache;
logic  [2:0]   axi_clint_ar_prot;
logic          axi_clint_r_ready;
logic          axi_clint_r_valid;
logic  [7:0]   axi_clint_r_id;
logic  [511:0] axi_clint_r_data;
logic  [1:0]   axi_clint_r_resp;
logic          axi_clint_r_last;


logic          axi_sram_aw_ready;
logic          axi_sram_aw_valid;
logic  [7:0]   axi_sram_aw_id;
logic  [63:0]  axi_sram_aw_addr;
logic  [7:0]   axi_sram_aw_len;
logic  [2:0]   axi_sram_aw_size;
logic  [1:0]   axi_sram_aw_burst;
logic          axi_sram_aw_lock;
logic  [3:0]   axi_sram_aw_cache;
logic  [2:0]   axi_sram_aw_prot;
logic          axi_sram_w_ready;
logic          axi_sram_w_valid;
logic  [7:0]   axi_sram_w_id;
logic  [511:0] axi_sram_w_data;
logic  [63:0]  axi_sram_w_strb;
logic          axi_sram_w_last;
logic          axi_sram_b_ready;
logic          axi_sram_b_valid;
logic  [1:0]   axi_sram_b_resp;
logic  [7:0]   axi_sram_b_id;
logic          axi_sram_ar_ready;
logic          axi_sram_ar_valid;
logic  [7:0]   axi_sram_ar_id;
logic  [63:0]  axi_sram_ar_addr;
logic  [7:0]   axi_sram_ar_len;
logic  [2:0]   axi_sram_ar_size;
logic  [1:0]   axi_sram_ar_burst;
logic          axi_sram_ar_lock;
logic  [3:0]   axi_sram_ar_cache;
logic  [2:0]   axi_sram_ar_prot;
logic          axi_sram_r_ready;
logic          axi_sram_r_valid;
logic  [7:0]   axi_sram_r_id;
logic  [511:0] axi_sram_r_data;
logic  [1:0]   axi_sram_r_resp;
logic          axi_sram_r_last;

logic          axi_ethernet_aw_ready;
logic          axi_ethernet_aw_valid;
logic  [7:0]   axi_ethernet_aw_id;
logic  [63:0]  axi_ethernet_aw_addr;
logic  [7:0]   axi_ethernet_aw_len;
logic  [2:0]   axi_ethernet_aw_size;
logic  [1:0]   axi_ethernet_aw_burst;
logic          axi_ethernet_aw_lock;
logic  [3:0]   axi_ethernet_aw_cache;
logic  [2:0]   axi_ethernet_aw_prot;
logic          axi_ethernet_w_ready;
logic          axi_ethernet_w_valid;
logic  [7:0]   axi_ethernet_w_id;
logic  [511:0] axi_ethernet_w_data;
logic  [63:0]  axi_ethernet_w_strb;
logic          axi_ethernet_w_last;
logic          axi_ethernet_b_ready;
logic          axi_ethernet_b_valid;
logic  [1:0]   axi_ethernet_b_resp;
logic  [7:0]   axi_ethernet_b_id;
logic          axi_ethernet_ar_ready;
logic          axi_ethernet_ar_valid;
logic  [7:0]   axi_ethernet_ar_id;
logic  [63:0]  axi_ethernet_ar_addr;
logic  [7:0]   axi_ethernet_ar_len;
logic  [2:0]   axi_ethernet_ar_size;
logic  [1:0]   axi_ethernet_ar_burst;
logic          axi_ethernet_ar_lock;
logic  [3:0]   axi_ethernet_ar_cache;
logic  [2:0]   axi_ethernet_ar_prot;
logic          axi_ethernet_r_ready;
logic          axi_ethernet_r_valid;
logic  [7:0]   axi_ethernet_r_id;
logic  [511:0] axi_ethernet_r_data;
logic  [1:0]   axi_ethernet_r_resp;
logic          axi_ethernet_r_last;

logic          axi_ahb_aw_ready;
logic          axi_ahb_aw_valid;
logic  [7:0]   axi_ahb_aw_id;
logic  [63:0]  axi_ahb_aw_addr;
logic  [7:0]   axi_ahb_aw_len;
logic  [2:0]   axi_ahb_aw_size;
logic  [1:0]   axi_ahb_aw_burst;
logic          axi_ahb_aw_lock;
logic  [3:0]   axi_ahb_aw_cache;
logic  [2:0]   axi_ahb_aw_prot;
logic          axi_ahb_w_ready;
logic          axi_ahb_w_valid;
logic  [7:0]   axi_ahb_w_id;
logic  [511:0] axi_ahb_w_data;
logic  [63:0]  axi_ahb_w_strb;
logic          axi_ahb_w_last;
logic          axi_ahb_b_ready;
logic          axi_ahb_b_valid;
logic  [1:0]   axi_ahb_b_resp;
logic  [7:0]   axi_ahb_b_id;
logic          axi_ahb_ar_ready;
logic          axi_ahb_ar_valid;
logic  [7:0]   axi_ahb_ar_id;
logic  [63:0]  axi_ahb_ar_addr;
logic  [7:0]   axi_ahb_ar_len;
logic  [2:0]   axi_ahb_ar_size;
logic  [1:0]   axi_ahb_ar_burst;
logic          axi_ahb_ar_lock;
logic  [3:0]   axi_ahb_ar_cache;
logic  [2:0]   axi_ahb_ar_prot;
logic          axi_ahb_r_ready;
logic          axi_ahb_r_valid;
logic  [7:0]   axi_ahb_r_id;
logic  [511:0] axi_ahb_r_data;
logic  [1:0]   axi_ahb_r_resp;
logic          axi_ahb_r_last;


logic          axi_uart_aw_ready;
logic          axi_uart_aw_valid;
logic  [7:0]   axi_uart_aw_id;
logic  [63:0]  axi_uart_aw_addr;
logic  [7:0]   axi_uart_aw_len;
logic  [2:0]   axi_uart_aw_size;
logic  [1:0]   axi_uart_aw_burst;
logic          axi_uart_aw_lock;
logic  [3:0]   axi_uart_aw_cache;
logic  [2:0]   axi_uart_aw_prot;
logic          axi_uart_w_ready;
logic          axi_uart_w_valid;
logic  [7:0]   axi_uart_w_id;
logic  [511:0] axi_uart_w_data;
logic  [63:0]  axi_uart_w_strb;
logic          axi_uart_w_last;
logic          axi_uart_b_ready;
logic          axi_uart_b_valid;
logic  [1:0]   axi_uart_b_resp;
logic  [7:0]   axi_uart_b_id;
logic          axi_uart_ar_ready;
logic          axi_uart_ar_valid;
logic  [7:0]   axi_uart_ar_id;
logic  [63:0]  axi_uart_ar_addr;
logic  [7:0]   axi_uart_ar_len;
logic  [2:0]   axi_uart_ar_size;
logic  [1:0]   axi_uart_ar_burst;
logic          axi_uart_ar_lock;
logic  [3:0]   axi_uart_ar_cache;
logic  [2:0]   axi_uart_ar_prot;
logic          axi_uart_r_ready;
logic          axi_uart_r_valid;
logic  [7:0]   axi_uart_r_id;
logic  [511:0] axi_uart_r_data;
logic  [1:0]   axi_uart_r_resp;
logic          axi_uart_r_last;

logic          axi_uart_64_aw_ready;
logic          axi_uart_64_aw_valid;
logic  [7:0]   axi_uart_64_aw_id;
logic  [63:0]  axi_uart_64_aw_addr;
logic  [7:0]   axi_uart_64_aw_len;
logic  [2:0]   axi_uart_64_aw_size;
logic  [1:0]   axi_uart_64_aw_burst;
logic          axi_uart_64_aw_lock;
logic  [3:0]   axi_uart_64_aw_cache;
logic  [2:0]   axi_uart_64_aw_prot;
logic          axi_uart_64_w_ready;
logic          axi_uart_64_w_valid;
logic  [7:0]   axi_uart_64_w_id;
logic  [63:0]  axi_uart_64_w_data;
logic  [7:0]   axi_uart_64_w_strb;
logic          axi_uart_64_w_last;
logic          axi_uart_64_b_ready;
logic          axi_uart_64_b_valid;
logic  [63:0]  axi_uart_64_b_user;
logic  [1:0]   axi_uart_64_b_resp;
logic  [7:0]   axi_uart_64_b_id;
logic          axi_uart_64_ar_ready;
logic          axi_uart_64_ar_valid;
logic  [7:0]   axi_uart_64_ar_id;
logic  [63:0]  axi_uart_64_ar_addr;
logic  [7:0]   axi_uart_64_ar_len;
logic  [2:0]   axi_uart_64_ar_size;
logic  [1:0]   axi_uart_64_ar_burst;
logic          axi_uart_64_ar_lock;
logic  [3:0]   axi_uart_64_ar_cache;
logic  [2:0]   axi_uart_64_ar_prot;
logic          axi_uart_64_r_ready;
logic          axi_uart_64_r_valid;
logic  [7:0]   axi_uart_64_r_id;
logic  [63:0]  axi_uart_64_r_data;
logic  [1:0]   axi_uart_64_r_resp;
logic          axi_uart_64_r_last;
logic  [63:0]  axi_uart_64_r_user;

logic          axi2llpp_aw_ready;
logic          axi2llpp_aw_valid;
logic  [7:0]   axi2llpp_aw_id;
logic  [63:0]  axi2llpp_aw_addr;
logic  [7:0]   axi2llpp_aw_len;
logic  [2:0]   axi2llpp_aw_size;
logic  [1:0]   axi2llpp_aw_burst;
logic          axi2llpp_aw_lock;
logic  [3:0]   axi2llpp_aw_cache;
logic  [2:0]   axi2llpp_aw_prot;
logic          axi2llpp_w_ready;
logic          axi2llpp_w_valid;
logic  [7:0]   axi2llpp_w_id;
logic  [511:0] axi2llpp_w_data;
logic  [63:0]  axi2llpp_w_strb;
logic          axi2llpp_w_last;
logic          axi2llpp_b_ready;
logic          axi2llpp_b_valid;
logic  [1:0]   axi2llpp_b_resp;
logic  [7:0]   axi2llpp_b_id;
logic          axi2llpp_ar_ready;
logic          axi2llpp_ar_valid;
logic  [7:0]   axi2llpp_ar_id;
logic  [63:0]  axi2llpp_ar_addr;
logic  [7:0]   axi2llpp_ar_len;
logic  [2:0]   axi2llpp_ar_size;
logic  [1:0]   axi2llpp_ar_burst;
logic          axi2llpp_ar_lock;
logic  [3:0]   axi2llpp_ar_cache;
logic  [2:0]   axi2llpp_ar_prot;
logic          axi2llpp_r_ready;
logic          axi2llpp_r_valid;
logic  [7:0]   axi2llpp_r_id;
logic  [511:0] axi2llpp_r_data;
logic  [1:0]   axi2llpp_r_resp;
logic          axi2llpp_r_last;


logic          axi2llpp_32_aw_ready;
logic          axi2llpp_32_aw_valid;
logic  [7:0]   axi2llpp_32_aw_id;
logic  [63:0]  axi2llpp_32_aw_addr;
logic  [7:0]   axi2llpp_32_aw_len;
logic  [2:0]   axi2llpp_32_aw_size;
logic  [1:0]   axi2llpp_32_aw_burst;
logic          axi2llpp_32_aw_lock;
logic  [3:0]   axi2llpp_32_aw_cache;
logic  [2:0]   axi2llpp_32_aw_prot;
logic          axi2llpp_32_w_ready;
logic          axi2llpp_32_w_valid;
logic  [7:0]   axi2llpp_32_w_id;
logic  [31:0]  axi2llpp_32_w_data;
logic  [3:0]   axi2llpp_32_w_strb;
logic          axi2llpp_32_w_last;
logic          axi2llpp_32_b_ready;
logic          axi2llpp_32_b_valid;
logic  [1:0]   axi2llpp_32_b_resp;
logic  [7:0]   axi2llpp_32_b_id;
logic          axi2llpp_32_ar_ready;
logic          axi2llpp_32_ar_valid;
logic  [7:0]   axi2llpp_32_ar_id;
logic  [63:0]  axi2llpp_32_ar_addr;
logic  [7:0]   axi2llpp_32_ar_len;
logic  [2:0]   axi2llpp_32_ar_size;
logic  [1:0]   axi2llpp_32_ar_burst;
logic          axi2llpp_32_ar_lock;
logic  [3:0]   axi2llpp_32_ar_cache;
logic  [2:0]   axi2llpp_32_ar_prot;
logic          axi2llpp_32_r_ready;
logic          axi2llpp_32_r_valid;
logic  [7:0]   axi2llpp_32_r_id;
logic  [31:0]  axi2llpp_32_r_data;
logic  [1:0]   axi2llpp_32_r_resp;
logic          axi2llpp_32_r_last;



  logic         w_axi2ahb_axis_aw_ready;
  logic         w_axi2ahb_axis_aw_valid;
  logic [63:0]  w_axi2ahb_axis_aw_bits_addr;
  logic [7:0]   w_axi2ahb_axis_aw_bits_len;
  logic [2:0]   w_axi2ahb_axis_aw_bits_size;
  logic [1:0]   w_axi2ahb_axis_aw_bits_burst;
  logic         w_axi2ahb_axis_aw_bits_lock;
  logic [3:0]   w_axi2ahb_axis_aw_bits_cache;
  logic [2:0]   w_axi2ahb_axis_aw_bits_prot;
  logic         w_axi2ahb_axis_w_ready;
  logic         w_axi2ahb_axis_w_valid;
  logic [63:0]  w_axi2ahb_axis_w_bits_data;
  logic [7:0]   w_axi2ahb_axis_w_bits_strb;
  logic         w_axi2ahb_axis_w_bits_last;
  logic         w_axi2ahb_axis_b_ready;
  logic         w_axi2ahb_axis_b_valid;
  logic [1:0]   w_axi2ahb_axis_b_bits_resp;
  logic         w_axi2ahb_axis_ar_ready;
  logic         w_axi2ahb_axis_ar_valid;
  logic [63:0]  w_axi2ahb_axis_ar_bits_addr;
  logic [7:0]   w_axi2ahb_axis_ar_bits_len;
  logic [2:0]   w_axi2ahb_axis_ar_bits_size;
  logic [1:0]   w_axi2ahb_axis_ar_bits_burst;
  logic         w_axi2ahb_axis_ar_bits_lock;
  logic [3:0]   w_axi2ahb_axis_ar_bits_cache;
  logic [2:0]   w_axi2ahb_axis_ar_bits_prot;
  logic         w_axi2ahb_axis_r_ready;
  logic         w_axi2ahb_axis_r_valid;
  logic [63:0]  w_axi2ahb_axis_r_bits_data;
  logic [1:0]   w_axi2ahb_axis_r_bits_resp;
  logic         w_axi2ahb_axis_r_bits_last;



  logic         w_axi2apb_axis_aw_ready;
  logic         w_axi2apb_axis_aw_valid;
  logic [63:0]  w_axi2apb_axis_aw_bits_addr;
  logic [7:0]   w_axi2apb_axis_aw_bits_len;
  logic [2:0]   w_axi2apb_axis_aw_bits_size;
  logic [1:0]   w_axi2apb_axis_aw_bits_burst;
  logic         w_axi2apb_axis_aw_bits_lock;
  logic [3:0]   w_axi2apb_axis_aw_bits_cache;
  logic [2:0]   w_axi2apb_axis_aw_bits_prot;
  logic         w_axi2apb_axis_w_ready;
  logic         w_axi2apb_axis_w_valid;
  logic [31:0]  w_axi2apb_axis_w_bits_data;
  logic [3:0]   w_axi2apb_axis_w_bits_strb;
  logic         w_axi2apb_axis_w_bits_last;
  logic         w_axi2apb_axis_b_ready;
  logic         w_axi2apb_axis_b_valid;
  logic [1:0]   w_axi2apb_axis_b_bits_resp;
  logic         w_axi2apb_axis_ar_ready;
  logic         w_axi2apb_axis_ar_valid;
  logic [63:0]  w_axi2apb_axis_ar_bits_addr;
  logic [7:0]   w_axi2apb_axis_ar_bits_len;
  logic [2:0]   w_axi2apb_axis_ar_bits_size;
  logic [1:0]   w_axi2apb_axis_ar_bits_burst;
  logic         w_axi2apb_axis_ar_bits_lock;
  logic [3:0]   w_axi2apb_axis_ar_bits_cache;
  logic [2:0]   w_axi2apb_axis_ar_bits_prot;
  logic         w_axi2apb_axis_r_ready;
  logic         w_axi2apb_axis_r_valid;
  logic [31:0]  w_axi2apb_axis_r_bits_data;
  logic [1:0]   w_axi2apb_axis_r_bits_resp;
  logic         w_axi2apb_axis_r_bits_last;

  logic         w_dma_axim_aw_valid;
  logic         w_dma_axim_aw_ready;
  logic [3:0]   w_dma_axim_aw_bits_id;
  logic [63:0]  w_dma_axim_aw_bits_addr;
  logic [7:0]   w_dma_axim_aw_bits_len;
  logic [2:0]   w_dma_axim_aw_bits_size;
  logic [1:0]   w_dma_axim_aw_bits_burst;
  logic         w_dma_axim_aw_bits_lock;
  logic [3:0]   w_dma_axim_aw_bits_cache;
  logic [2:0]   w_dma_axim_aw_bits_prot;
  logic         w_dma_axim_w_ready;
  logic         w_dma_axim_w_valid;
  logic [3:0]   w_dma_axim_w_bits_id;
  logic [511:0] w_dma_axim_w_bits_data;
  logic [63:0]  w_dma_axim_w_bits_strb;
  logic         w_dma_axim_w_bits_last;
  logic         w_dma_axim_b_ready;
  logic         w_dma_axim_b_valid;
  logic [1:0]   w_dma_axim_b_bits_resp;
  logic [3:0]   w_dma_axim_b_bits_id;
  logic         w_dma_axim_ar_ready;
  logic         w_dma_axim_ar_valid;
  logic [3:0]   w_dma_axim_ar_bits_id;
  logic [63:0]  w_dma_axim_ar_bits_addr;
  logic [7:0]   w_dma_axim_ar_bits_len;
  logic [2:0]   w_dma_axim_ar_bits_size;
  logic [1:0]   w_dma_axim_ar_bits_burst;
  logic         w_dma_axim_ar_bits_lock;
  logic [3:0]   w_dma_axim_ar_bits_cache;
  logic [2:0]   w_dma_axim_ar_bits_prot;
  logic         w_dma_axim_r_ready;
  logic         w_dma_axim_r_valid;
  logic [3:0]   w_dma_axim_r_bits_id;
  logic [511:0] w_dma_axim_r_bits_data;
  logic [1:0]   w_dma_axim_r_bits_resp;
  logic         w_dma_axim_r_bits_last;


logic          llpp_32_aw_ready;
logic          llpp_32_aw_valid;
logic          llpp_32_aw_bits_id;
logic  [63:0]  llpp_32_aw_bits_addr;
logic  [7:0]   llpp_32_aw_bits_len;
logic  [2:0]   llpp_32_aw_bits_size;
logic  [1:0]   llpp_32_aw_bits_burst;
logic          llpp_32_aw_bits_lock;
logic  [3:0]   llpp_32_aw_bits_cache;
logic  [2:0]   llpp_32_aw_bits_prot;
logic          llpp_32_w_ready;
logic          llpp_32_w_valid;
logic          llpp_32_w_bits_id;
logic  [31:0]  llpp_32_w_bits_data;
logic  [3:0]   llpp_32_w_bits_strb;
logic          llpp_32_w_bits_last;
logic          llpp_32_b_ready;
logic          llpp_32_b_valid;
logic  [1:0]   llpp_32_b_bits_resp;
logic          llpp_32_b_bits_id;
logic          llpp_32_ar_ready;
logic          llpp_32_ar_valid;
logic          llpp_32_ar_bits_id;
logic  [63:0]  llpp_32_ar_bits_addr;
logic  [7:0]   llpp_32_ar_bits_len;
logic  [2:0]   llpp_32_ar_bits_size;
logic  [1:0]   llpp_32_ar_bits_burst;
logic          llpp_32_ar_bits_lock;
logic  [3:0]   llpp_32_ar_bits_cache;
logic  [2:0]   llpp_32_ar_bits_prot;
logic          llpp_32_r_ready;
logic          llpp_32_r_valid;
logic          llpp_32_r_bits_id;
logic  [31:0]  llpp_32_r_bits_data;
logic  [1:0]   llpp_32_r_bits_resp;
logic          llpp_32_r_bits_last;


logic          llpp2apb_aw_ready;
logic          llpp2apb_aw_valid;
logic  [11:0]  llpp2apb_aw_bits_id;
logic  [63:0]  llpp2apb_aw_bits_addr;
logic  [7:0]   llpp2apb_aw_bits_len;
logic  [2:0]   llpp2apb_aw_bits_size;
logic  [1:0]   llpp2apb_aw_bits_burst;
logic          llpp2apb_aw_bits_lock;
logic  [3:0]   llpp2apb_aw_bits_cache;
logic  [2:0]   llpp2apb_aw_bits_prot;
logic          llpp2apb_w_ready;
logic          llpp2apb_w_valid;
logic  [11:0]  llpp2apb_w_bits_id;
logic  [31:0]  llpp2apb_w_bits_data;
logic  [3:0]   llpp2apb_w_bits_strb;
logic          llpp2apb_w_bits_last;
logic          llpp2apb_b_ready;
logic          llpp2apb_b_valid;
logic  [1:0]   llpp2apb_b_bits_resp;
logic  [11:0]  llpp2apb_b_bits_id;
logic          llpp2apb_ar_ready;
logic          llpp2apb_ar_valid;
logic  [11:0]  llpp2apb_ar_bits_id;
logic  [63:0]  llpp2apb_ar_bits_addr;
logic  [7:0]   llpp2apb_ar_bits_len;
logic  [2:0]   llpp2apb_ar_bits_size;
logic  [1:0]   llpp2apb_ar_bits_burst;
logic          llpp2apb_ar_bits_lock;
logic  [3:0]   llpp2apb_ar_bits_cache;
logic  [2:0]   llpp2apb_ar_bits_prot;
logic          llpp2apb_r_ready;
logic          llpp2apb_r_valid;
logic  [11:0]  llpp2apb_r_bits_id;
logic  [31:0]  llpp2apb_r_bits_data;
logic  [1:0]   llpp2apb_r_bits_resp;
logic          llpp2apb_r_bits_last;

logic          llpp2ahb_aw_ready;
logic          llpp2ahb_aw_valid;
logic  [11:0]  llpp2ahb_aw_bits_id;
logic  [63:0]  llpp2ahb_aw_bits_addr;
logic  [7:0]   llpp2ahb_aw_bits_len;
logic  [2:0]   llpp2ahb_aw_bits_size;
logic  [1:0]   llpp2ahb_aw_bits_burst;
logic          llpp2ahb_aw_bits_lock;
logic  [3:0]   llpp2ahb_aw_bits_cache;
logic  [2:0]   llpp2ahb_aw_bits_prot;
logic          llpp2ahb_w_ready;
logic          llpp2ahb_w_valid;
logic  [11:0]  llpp2ahb_w_bits_id;
logic  [31:0]  llpp2ahb_w_bits_data;
logic  [3:0]   llpp2ahb_w_bits_strb;
logic          llpp2ahb_w_bits_last;
logic          llpp2ahb_b_ready;
logic          llpp2ahb_b_valid;
logic  [1:0]   llpp2ahb_b_bits_resp;
logic  [11:0]  llpp2ahb_b_bits_id;
logic          llpp2ahb_ar_ready;
logic          llpp2ahb_ar_valid;
logic  [11:0]  llpp2ahb_ar_bits_id;
logic  [63:0]  llpp2ahb_ar_bits_addr;
logic  [7:0]   llpp2ahb_ar_bits_len;
logic  [2:0]   llpp2ahb_ar_bits_size;
logic  [1:0]   llpp2ahb_ar_bits_burst;
logic          llpp2ahb_ar_bits_lock;
logic  [3:0]   llpp2ahb_ar_bits_cache;
logic  [2:0]   llpp2ahb_ar_bits_prot;
logic          llpp2ahb_r_ready;
logic          llpp2ahb_r_valid;
logic  [11:0]  llpp2ahb_r_bits_id;
logic  [31:0]  llpp2ahb_r_bits_data;
logic  [1:0]   llpp2ahb_r_bits_resp;
logic          llpp2ahb_r_bits_last;



  logic         w_llpp_hsel;
  logic [63:0]  w_llpp_haddr;
  logic         w_llpp_hwrite;
  logic [2:0]   w_llpp_hsize;
  logic [2:0]   w_llpp_hburst;
  logic [3:0]   w_llpp_hprot;
  logic [1:0]   w_llpp_htrans;
  logic [31:0]  w_llpp_hwdata;
  logic         w_llpp_hreadyin;
  logic [31:0]  w_llpp_hrdata;
  logic         w_llpp_hready;
  logic         w_llpp_hresp;

  logic         w_sdio_hsel;
  logic [63:0]  w_sdio_haddr;
  logic         w_sdio_hwrite;
  logic [2:0]   w_sdio_hsize;
  logic [2:0]   w_sdio_hburst;
  logic [3:0]   w_sdio_hprot;
  logic [1:0]   w_sdio_htrans;
  logic [31:0]  w_sdio_hwdata;
  logic         w_sdio_hreadyin;
  logic [31:0]  w_sdio_hrdata;
  logic         w_sdio_hready;
  logic         w_sdio_hresp;

  logic         w_ahbm_hsel;
  logic [63:0]  w_ahbm_haddr;
  logic         w_ahbm_hwrite;
  logic [2:0]   w_ahbm_hsize;
  logic [2:0]   w_ahbm_hburst;
  logic [3:0]   w_ahbm_hprot;
  logic [1:0]   w_ahbm_htrans;
  logic [63:0]  w_ahbm_hwdata;
  logic         w_ahbm_hreadyin;
  logic [63:0]  w_ahbm_hrdata;
  logic         w_ahbm_hready;
  logic         w_ahbm_hresp;

  logic         w_flash_hsel;
  logic [63:0]  w_flash_haddr;
  logic         w_flash_hwrite;
  logic [2:0]   w_flash_hsize;
  logic [2:0]   w_flash_hburst;
  logic [3:0]   w_flash_hprot;
  logic [1:0]   w_flash_htrans;
  logic [63:0]  w_flash_hwdata;
  logic         w_flash_hreadyin;
  logic [63:0]  w_flash_hrdata;
  logic         w_flash_hready;
  logic         w_flash_hresp;



  logic [31:0]  ahbbrg_haddr;
  logic         ahbbrg_hwrite;
  logic [2:0]   ahbbrg_hsize;
  logic [2:0]   ahbbrg_hburst;
  logic [3:0]   ahbbrg_hprot;
  logic [1:0]   ahbbrg_htrans;
  logic [31:0]  ahbbrg_hwdata;
  logic         ahbbrg_hreadyin;
  logic [31:0]  ahbbrg_hrdata;
  logic         ahbbrg_hready;
  logic         ahbbrg_hresp;


  logic         w_apb_hsel;
  logic [31:0]  w_apb_haddr;
  logic         w_apb_hwrite;
  logic [2:0]   w_apb_hsize;
  logic [2:0]   w_apb_hburst;
  logic [3:0]   w_apb_hprot;
  logic [1:0]   w_apb_htrans;
  logic [31:0]  w_apb_hwdata;
  logic         w_apb_hreadyin;
  logic [31:0]  w_apb_hrdata;
  logic         w_apb_hready;
  logic         w_apb_hresp;


  logic [  2:0]   w_pprot;
  logic [  3:0]   w_pstrb;
  logic [ 31:0]   w_paddr;
  logic           w_penable;
  logic           w_pwrite;
  logic [ 31:0]   w_pwdata;


  logic           apb_psel_m;
  logic           apb_penable_m;
  logic [ 31:0]   apb_paddr_m;
  logic           apb_pwrite_m;
  logic [  3:0]   apb_pstrb_m;
  logic [ 31:0]   apb_pwdata_m;
  logic [ 31:0]   apb_prdata_m;
  logic           apb_prdy_m;

  logic         w_dma_psel;
  logic         w_dma_penable;
  logic         w_dma_pwrite;
  logic [16:0]  w_dma_paddr;
  logic [31:0]  w_dma_pwdata;
  logic [31:0]  w_dma_prdata;

  logic         w_spi_psel;
  logic         w_spi_penable;
  logic         w_spi_pwrite;
  logic [16:0]  w_spi_paddr;
  logic [31:0]  w_spi_pwdata;
  logic [31:0]  w_spi_prdata;

  logic         w_iic_psel;
  logic         w_iic_penable;
  logic         w_iic_pwrite;
  logic [16:0]  w_iic_paddr;
  logic [31:0]  w_iic_pwdata;
  logic [31:0]  w_iic_prdata;

  logic         w_spi_iis_psel;
  logic         w_spi_iis_penable;
  logic         w_spi_iis_pwrite;
  logic [16:0]  w_spi_iis_paddr;
  logic [31:0]  w_spi_iis_pwdata;
  logic [31:0]  w_spi_iis_prdata;

  logic         w_wdt_psel;
  logic         w_wdt_penable;
  logic         w_wdt_pwrite;
  logic [16:0]  w_wdt_paddr;
  logic [31:0]  w_wdt_pwdata;
  logic [31:0]  w_wdt_prdata;

  logic         w_timer_psel;
  logic         w_timer_penable;
  logic         w_timer_pwrite;
  logic [16:0]  w_timer_paddr;
  logic [31:0]  w_timer_pwdata;
  logic [31:0]  w_timer_prdata;

  logic         w_rtc_psel;
  logic         w_rtc_penable;
  logic         w_rtc_pwrite;
  logic [16:0]  w_rtc_paddr;
  logic [31:0]  w_rtc_pwdata;
  logic [31:0]  w_rtc_prdata;

  logic         w_analog_psel;
  logic         w_analog_penable;
  logic         w_analog_pwrite;
  logic [16:0]  w_analog_paddr;
  logic [31:0]  w_analog_pwdata;
  logic [31:0]  w_analog_prdata;

  logic         w_uart_psel;
  logic         w_uart_penable;
  logic         w_uart_pwrite;
  logic [16:0]  w_uart_paddr;
  logic [31:0]  w_uart_pwdata;
  logic [31:0]  w_uart_prdata;

  logic         w_gpio_psel;
  logic         w_gpio_penable;
  logic         w_gpio_pwrite;
  logic [16:0]  w_gpio_paddr;
  logic [31:0]  w_gpio_pwdata;
  logic [31:0]  w_gpio_prdata;

  logic         w_psel_11_nc;
  logic         w_penable_11_nc;
  logic         w_pwrite_11_nc;
  logic [16:0]  w_paddr_11_nc;
  logic [31:0]  w_pwdata_11_nc;
  logic [31:0]  w_prdata_11_nc;

  logic         w_sys_psel;
  logic         w_sys_penable;
  logic         w_sys_pwrite;
  logic [16:0]  w_sys_paddr;
  logic [31:0]  w_sys_pwdata;
  logic [31:0]  w_sys_prdata;

logic [31:0] external_irq;


logic w_iic_dma_ack;
logic w_iic_dma_req;

logic w_iic_int;
logic w_dma_int;
logic w_spi_int;
logic w_iis_int;
logic w_wdt_int;
logic w_pit_int;
logic w_rtc_int;
logic w_uart_int;
logic w_gpio_int;
logic w_sdio_int;

AXI_BUS #(
    .AXI_ADDR_WIDTH ( AxiAddrWidth     ),
    .AXI_DATA_WIDTH ( AxiDataWidth     ),
    .AXI_ID_WIDTH   ( AxiIdWidthSlaves ),
    .AXI_USER_WIDTH ( AxiUserWidth     )
) master_rom();


ariane_axi::req_t  axi_clint_req;
ariane_axi::resp_t axi_clint_resp;

rstgen i_rstgen_main (
    .clk_i        ( sys_clk                      ),
    .rst_ni       ( cpu_resetn     ),
    .test_mode_i  ( test_en                  ),
    .rst_no       ( ndmreset_n               ),
    .init_no      (                          ) // keep open
);


assign rst_n = ndmreset_n;
assign rst = ~ndmreset_n;

// ---------------
// AXI Xbar
// ---------------


// ---------------
// Core
// ---------------

logic [50:0] config_itcm_startAddr;
logic [4:0]  config_itcm_size;
logic [50:0] config_dtcm_startAddr;
logic [4:0]  config_dtcm_size;
logic [51:0] config_llpp_startAddr;
logic [4:0]  config_llpp_size;
logic [36:0] config_flash_startAddr;
logic [4:0]  config_flash_size;
logic [63:0] boot_addr;


// assign config_itcm_startAddr = 19'h00000;
// assign config_itcm_startAddr = 19'h00038;//0x0007_0000
assign config_itcm_startAddr = 50'h00200;//0x0040_0000
assign config_itcm_size = 5'b00111;
//assign config_dtcm_startAddr = 19'h00080;
// assign config_dtcm_startAddr = 19'h00040;//0x0008_0000
assign config_dtcm_startAddr = 50'h00400;//0x0080_0000

assign config_dtcm_size = 5'b00111;
assign config_llpp_startAddr = 51'h50000;//51'hB0000;
//@pengzhuo 20260119 change the llpp size to 128KB
// assign config_llpp_size = 5'b10001;//64MB
//assign config_llpp_size = 5'b01000;//128KB
//@wanglei 20260429 change the llpp size to 256KB
assign config_llpp_size = 5'b01010;//256KB
assign config_flash_startAddr = 36'h04_0000;
assign config_flash_size = 5'b10010;

assign external_irq[15:0] = 16'b0;
assign external_irq[31:26] = 7'b0;

assign external_irq[16] =  w_dma_int;
assign external_irq[19:17] = 5'b0;
assign external_irq[20] =  w_gpio_int;
assign external_irq[21] =  w_uart_int;
assign external_irq[22] = 4'b0;
assign external_irq[23] =  w_spi_int;
assign external_irq[25:24] = 4'b0;
/*assign external_irq[17] =  w_wdt_int;
assign external_irq[18] =  w_rtc_int;
assign external_irq[19] =  w_pit_int;
assign external_irq[21] =  w_uart_int;
assign external_irq[22] =  w_sdio_int;
assign external_irq[24] =  w_iis_int;
assign external_irq[25] =  w_iic_int;*/




assign flash_ar_ready = 1'b0;
assign flash_r_valid = 1'b0;
assign flash_r_bits_id = 1'b0;
assign flash_r_bits_data = 512'b0;
assign flash_r_bits_resp = 2'b0;
assign flash_r_bits_last = 1'b0;


assign axis_aw_valid = 1'b0;
assign axis_aw_bits_id = 8'b0;
assign axis_aw_bits_addr = 64'b0;
assign axis_aw_bits_len = 8'b0;
assign axis_aw_bits_size = 3'b0;
assign axis_aw_bits_burst = 2'b0;
assign axis_aw_bits_lock = 1'b0;
assign axis_aw_bits_cache = 4'b0;
assign axis_aw_bits_prot = 3'b0;
assign axis_w_valid = 1'b0;
assign axis_w_bits_id = 8'b0;
assign axis_w_bits_data = 512'b0;
assign axis_w_bits_strb = 64'b0;
assign axis_w_bits_last = 1'b0;
assign axis_b_ready = 1'b0;
assign axis_ar_valid = 1'b0;
assign axis_ar_bits_id = 8'b0;
assign axis_ar_bits_addr = 64'b0;
assign axis_ar_bits_len = 8'b0;
assign axis_ar_bits_size = 3'b0;
assign axis_ar_bits_burst = 2'b0;
assign axis_ar_bits_lock = 1'b0;
assign axis_ar_bits_cache = 4'b0;
assign axis_ar_bits_prot = 3'b0;
assign axis_r_ready = 1'b0;


assign irq       = 2'b0;
assign debug_req_irq = 1'b0;

E6x #(
    .rvfi_probes_t ( rvfi_probes_t )
) i_E6x (
    .clk_i        ( w_core0_clk /*sys_clk*/             ),
    .rst_ni       ( w_core0_rst_n /*ndmreset_n*/          ),

    //@pengzhuo 20250922 change the config port
    .config_itcm_startAddr_i    ({'h0, w_core0_itcm_startAddr}),
    .config_itcm_size_i         ({'h0, w_core0_itcm_size}),
    .config_dtcm_startAddr_i    ({'h0, w_core0_dtcm_startAddr}),
    .config_dtcm_size_i         ({'h0, w_core0_dtcm_size}),
    .config_llpp_startAddr_i    ({'h0, w_core0_llpp_startAddr}),
    .config_llpp_size_i         ({'h0, w_core0_llpp_size}),
    .config_flash_startAddr_i   ({'h0, w_core0_flash_startAddr}),
    .config_flash_size_i        ({'h0, w_core0_flash_size}),


    /**** TimeOut ****/
    .io_cluster_utid_rs_i       (2'b0), 
    .io_cpu_i                   (2'b0), 
    .io_timeout_limitby_xx_i    (8'b0), 

    // .external_irq_i     (clic_if.clic_external_source     ),
    .external_irq_i     (external_irq                  ),

    .rtc_i              (i_rtc                         ),

    /**** JTAG ****/
    .jtag_tck_i         (i_jtag_tck                             ),
    .jtag_tms_i         (i_jtag_tms                             ),
    .jtag_tdi_i         (i_jtag_tdi                             ),
    .jtag_trst_n_i      (i_jtag_trst_n                          ),
    .jtag_tdo_o         (o_jtag_tdo                             ),

    .boot_addr_i  ( {'h0, w_core0_boot_addr}), // start fetching from ROM // Debug from DRAM
    .hart_id_i    ( '0                  ),
    .irq_i        ( irq                 ),
    .ipi_i        ( ipi                 ),
    .time_irq_i   ( timer_irq           ),
    .rvfi_probes_o( /* open */          ),
    .debug_req_i  ( debug_req_irq       ),

    .io_axim_aw_ready_i      (axim_aw_ready      ),
    .io_axim_aw_valid_o      (axim_aw_valid      ),
    .io_axim_aw_bits_id_o    (axim_aw_bits_id    ),
    .io_axim_aw_bits_addr_o  (axim_aw_bits_addr  ),
    .io_axim_aw_bits_len_o   (axim_aw_bits_len   ),
    .io_axim_aw_bits_size_o  (axim_aw_bits_size  ),
    .io_axim_aw_bits_burst_o (axim_aw_bits_burst ),
    .io_axim_aw_bits_lock_o  (axim_aw_bits_lock  ),
    .io_axim_aw_bits_cache_o (axim_aw_bits_cache ),
    .io_axim_aw_bits_prot_o  (axim_aw_bits_prot  ),
    .io_axim_w_ready_i       (axim_w_ready       ),
    .io_axim_w_valid_o       (axim_w_valid       ),
    .io_axim_w_bits_id_o     (axim_w_bits_id     ),
    .io_axim_w_bits_data_o   (axim_w_bits_data   ),
    .io_axim_w_bits_strb_o   (axim_w_bits_strb   ),
    .io_axim_w_bits_last_o   (axim_w_bits_last   ),
    .io_axim_b_ready_o       (axim_b_ready       ),
    .io_axim_b_valid_i       (axim_b_valid       ),
    .io_axim_b_bits_resp_i   (axim_b_bits_resp   ),
    .io_axim_b_bits_id_i     (axim_b_bits_id     ),
    .io_axim_ar_ready_i      (axim_ar_ready      ),
    .io_axim_ar_valid_o      (axim_ar_valid      ),
    .io_axim_ar_bits_id_o    (axim_ar_bits_id    ),
    .io_axim_ar_bits_addr_o  (axim_ar_bits_addr  ),
    .io_axim_ar_bits_len_o   (axim_ar_bits_len   ),
    .io_axim_ar_bits_size_o  (axim_ar_bits_size  ),
    .io_axim_ar_bits_burst_o (axim_ar_bits_burst ),
    .io_axim_ar_bits_lock_o  (axim_ar_bits_lock  ),
    .io_axim_ar_bits_cache_o (axim_ar_bits_cache ),
    .io_axim_ar_bits_prot_o  (axim_ar_bits_prot  ),
    .io_axim_r_ready_o       (axim_r_ready       ),
    .io_axim_r_valid_i       (axim_r_valid       ),
    .io_axim_r_bits_id_i     (axim_r_bits_id     ),
    .io_axim_r_bits_data_i   (axim_r_bits_data   ),
    .io_axim_r_bits_resp_i   (axim_r_bits_resp   ),
    .io_axim_r_bits_last_i   (axim_r_bits_last   ),

    .io_flash_ar_ready_i     (flash_ar_ready     ),
    .io_flash_ar_valid_o     (flash_ar_valid     ),
    .io_flash_ar_bits_id_o   (flash_ar_bits_id   ),
    .io_flash_ar_bits_addr_o (flash_ar_bits_addr ),
    .io_flash_ar_bits_len_o  (flash_ar_bits_len  ),
    .io_flash_ar_bits_size_o (flash_ar_bits_size ),
    .io_flash_ar_bits_burst_o(flash_ar_bits_burst),
    .io_flash_ar_bits_lock_o (flash_ar_bits_lock ),
    .io_flash_ar_bits_cache_o(flash_ar_bits_cache),
    .io_flash_ar_bits_prot_o (flash_ar_bits_prot ),
    .io_flash_r_ready_o      (flash_r_ready      ),
    .io_flash_r_valid_i      (flash_r_valid      ),
    .io_flash_r_bits_id_i    (flash_r_bits_id    ),
    .io_flash_r_bits_data_i  (flash_r_bits_data  ),
    .io_flash_r_bits_resp_i  (flash_r_bits_resp  ),
    .io_flash_r_bits_last_i  (flash_r_bits_last  ),

    .io_icache_ar_ready_i     (icache_ar_ready     ),
    .io_icache_ar_valid_o     (icache_ar_valid     ),
    .io_icache_ar_bits_id_o   (icache_ar_bits_id   ),
    .io_icache_ar_bits_addr_o (icache_ar_bits_addr ),
    .io_icache_ar_bits_len_o  (icache_ar_bits_len  ),
    .io_icache_ar_bits_size_o (icache_ar_bits_size ),
    .io_icache_ar_bits_burst_o(icache_ar_bits_burst),
    .io_icache_ar_bits_lock_o (icache_ar_bits_lock ),
    .io_icache_ar_bits_cache_o(icache_ar_bits_cache),
    .io_icache_ar_bits_prot_o (icache_ar_bits_prot ),
    .io_icache_r_ready_o      (icache_r_ready      ),
    .io_icache_r_valid_i      (icache_r_valid      ),
    .io_icache_r_bits_id_i    (icache_r_bits_id    ),
    .io_icache_r_bits_data_i  (icache_r_bits_data  ),
    .io_icache_r_bits_resp_i  (icache_r_bits_resp  ),
    .io_icache_r_bits_last_i  (icache_r_bits_last  ),

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



    .io_llpp_aw_ready_i      (llpp_aw_ready      ),
    .io_llpp_aw_valid_o      (llpp_aw_valid      ),
    .io_llpp_aw_bits_id_o    (llpp_aw_bits_id    ),
    .io_llpp_aw_bits_addr_o  (llpp_aw_bits_addr  ),
    .io_llpp_aw_bits_len_o   (llpp_aw_bits_len   ),
    .io_llpp_aw_bits_size_o  (llpp_aw_bits_size  ),
    .io_llpp_aw_bits_burst_o (llpp_aw_bits_burst ),
    .io_llpp_aw_bits_lock_o  (llpp_aw_bits_lock  ),
    .io_llpp_aw_bits_cache_o (llpp_aw_bits_cache ),
    .io_llpp_aw_bits_prot_o  (llpp_aw_bits_prot  ),
    .io_llpp_w_ready_i       (llpp_w_ready       ),
    .io_llpp_w_valid_o       (llpp_w_valid       ),
    .io_llpp_w_bits_id_o     (llpp_w_bits_id     ),
    .io_llpp_w_bits_data_o   (llpp_w_bits_data   ),
    .io_llpp_w_bits_strb_o   (llpp_w_bits_strb   ),
    .io_llpp_w_bits_last_o   (llpp_w_bits_last   ),
    .io_llpp_b_ready_o       (llpp_b_ready       ),
    .io_llpp_b_valid_i       (llpp_b_valid       ),
    .io_llpp_b_bits_resp_i   (llpp_b_bits_resp   ),
    .io_llpp_b_bits_id_i     (llpp_b_bits_id     ),
    .io_llpp_ar_ready_i      (llpp_ar_ready      ),
    .io_llpp_ar_valid_o      (llpp_ar_valid      ),
    .io_llpp_ar_bits_id_o    (llpp_ar_bits_id    ),
    .io_llpp_ar_bits_addr_o  (llpp_ar_bits_addr  ),
    .io_llpp_ar_bits_len_o   (llpp_ar_bits_len   ),
    .io_llpp_ar_bits_size_o  (llpp_ar_bits_size  ),
    .io_llpp_ar_bits_burst_o (llpp_ar_bits_burst ),
    .io_llpp_ar_bits_lock_o  (llpp_ar_bits_lock  ),
    .io_llpp_ar_bits_cache_o (llpp_ar_bits_cache ),
    .io_llpp_ar_bits_prot_o  (llpp_ar_bits_prot  ),
    .io_llpp_r_ready_o       (llpp_r_ready       ),
    .io_llpp_r_valid_i       (llpp_r_valid       ),
    .io_llpp_r_bits_id_i     (llpp_r_bits_id     ),
    .io_llpp_r_bits_data_i   (llpp_r_bits_data   ),
    .io_llpp_r_bits_resp_i   (llpp_r_bits_resp   ),
    .io_llpp_r_bits_last_i   (llpp_r_bits_last   ),

    .io_axis_aw_ready_o      (axis_aw_ready      ),
    .io_axis_aw_valid_i      (axis_aw_valid      ),
    .io_axis_aw_bits_id_i    (axis_aw_bits_id    ),
    .io_axis_aw_bits_addr_i  (axis_aw_bits_addr  ),
    .io_axis_aw_bits_len_i   (axis_aw_bits_len   ),
    .io_axis_aw_bits_size_i  (axis_aw_bits_size  ),
    .io_axis_aw_bits_burst_i (axis_aw_bits_burst ),
    .io_axis_aw_bits_lock_i  (axis_aw_bits_lock  ),
    .io_axis_aw_bits_cache_i (axis_aw_bits_cache ),
    .io_axis_aw_bits_prot_i  (axis_aw_bits_prot  ),
    .io_axis_w_ready_o       (axis_w_ready       ),
    .io_axis_w_valid_i       (axis_w_valid       ),
    .io_axis_w_bits_id_i     (axis_w_bits_id     ),
    .io_axis_w_bits_data_i   (axis_w_bits_data   ),
    .io_axis_w_bits_strb_i   (axis_w_bits_strb   ),
    .io_axis_w_bits_last_i   (axis_w_bits_last   ),
    .io_axis_b_ready_i       (axis_b_ready       ),
    .io_axis_b_valid_o       (axis_b_valid       ),
    .io_axis_b_bits_resp_o   (axis_b_bits_resp   ),
    .io_axis_b_bits_id_o     (axis_b_bits_id     ),
    .io_axis_ar_ready_o      (axis_ar_ready      ),
    .io_axis_ar_valid_i      (axis_ar_valid      ),
    .io_axis_ar_bits_id_i    (axis_ar_bits_id    ),
    .io_axis_ar_bits_addr_i  (axis_ar_bits_addr  ),
    .io_axis_ar_bits_len_i   (axis_ar_bits_len   ),
    .io_axis_ar_bits_size_i  (axis_ar_bits_size  ),
    .io_axis_ar_bits_burst_i (axis_ar_bits_burst ),
    .io_axis_ar_bits_lock_i  (axis_ar_bits_lock  ),
    .io_axis_ar_bits_cache_i (axis_ar_bits_cache ),
    .io_axis_ar_bits_prot_i  (axis_ar_bits_prot  ),
    .io_axis_r_ready_i       (axis_r_ready       ),
    .io_axis_r_valid_o       (axis_r_valid       ),
    .io_axis_r_bits_id_o     (axis_r_bits_id     ),
    .io_axis_r_bits_data_o   (axis_r_bits_data   ),
    .io_axis_r_bits_resp_o   (axis_r_bits_resp   ),
    .io_axis_r_bits_last_o   (axis_r_bits_last   ),


    .io_coprocessor_aw_ready_i      (coprocessor_aw_ready      ),
    .io_coprocessor_aw_valid_o      (coprocessor_aw_valid      ),
    .io_coprocessor_aw_bits_id_o    (coprocessor_aw_bits_id    ),
    .io_coprocessor_aw_bits_addr_o  (coprocessor_aw_bits_addr  ),
    .io_coprocessor_aw_bits_len_o   (coprocessor_aw_bits_len   ),
    .io_coprocessor_aw_bits_size_o  (coprocessor_aw_bits_size  ),
    .io_coprocessor_aw_bits_burst_o (coprocessor_aw_bits_burst ),
    .io_coprocessor_aw_bits_lock_o  (coprocessor_aw_bits_lock  ),
    .io_coprocessor_aw_bits_cache_o (coprocessor_aw_bits_cache ),
    .io_coprocessor_aw_bits_prot_o  (coprocessor_aw_bits_prot  ),
    .io_coprocessor_w_ready_i       (coprocessor_w_ready       ),
    .io_coprocessor_w_valid_o       (coprocessor_w_valid       ),
    .io_coprocessor_w_bits_id_o     (coprocessor_w_bits_id     ),
    .io_coprocessor_w_bits_data_o   (coprocessor_w_bits_data   ),
    .io_coprocessor_w_bits_strb_o   (coprocessor_w_bits_strb   ),
    .io_coprocessor_w_bits_last_o   (coprocessor_w_bits_last   ),
    .io_coprocessor_b_ready_o       (coprocessor_b_ready       ),
    .io_coprocessor_b_valid_i       (coprocessor_b_valid       ),
    .io_coprocessor_b_bits_resp_i   (coprocessor_b_bits_resp   ),
    .io_coprocessor_b_bits_id_i     (coprocessor_b_bits_id     ),
    .io_coprocessor_ar_ready_i      (coprocessor_ar_ready      ),
    .io_coprocessor_ar_valid_o      (coprocessor_ar_valid      ),
    .io_coprocessor_ar_bits_id_o    (coprocessor_ar_bits_id    ),
    .io_coprocessor_ar_bits_addr_o  (coprocessor_ar_bits_addr  ),
    .io_coprocessor_ar_bits_len_o   (coprocessor_ar_bits_len   ),
    .io_coprocessor_ar_bits_size_o  (coprocessor_ar_bits_size  ),
    .io_coprocessor_ar_bits_burst_o (coprocessor_ar_bits_burst ),
    .io_coprocessor_ar_bits_lock_o  (coprocessor_ar_bits_lock  ),
    .io_coprocessor_ar_bits_cache_o (coprocessor_ar_bits_cache ),
    .io_coprocessor_ar_bits_prot_o  (coprocessor_ar_bits_prot  ),
    .io_coprocessor_r_ready_o       (coprocessor_r_ready       ),
    .io_coprocessor_r_valid_i       (coprocessor_r_valid       ),
    .io_coprocessor_r_bits_id_i     (coprocessor_r_bits_id     ),
    .io_coprocessor_r_bits_data_i   (coprocessor_r_bits_data   ),
    .io_coprocessor_r_bits_resp_i   (coprocessor_r_bits_resp   ),
    .io_coprocessor_r_bits_last_i   (coprocessor_r_bits_last   ),

    .io_ame_aw_ready_i              (ame_aw_ready              ),
    .io_ame_aw_valid_o              (ame_aw_valid              ),
    .io_ame_aw_bits_id_o            (ame_aw_bits_id            ),
    .io_ame_aw_bits_addr_o          (ame_aw_bits_addr          ),
    .io_ame_aw_bits_len_o           (ame_aw_bits_len           ),
    .io_ame_aw_bits_size_o          (ame_aw_bits_size          ),
    .io_ame_aw_bits_burst_o         (ame_aw_bits_burst         ),
    .io_ame_aw_bits_lock_o          (ame_aw_bits_lock          ),
    .io_ame_aw_bits_cache_o         (ame_aw_bits_cache         ),
    .io_ame_aw_bits_prot_o          (ame_aw_bits_prot          ),
    .io_ame_w_ready_i               (ame_w_ready               ),
    .io_ame_w_valid_o               (ame_w_valid               ),
    .io_ame_w_bits_data_o           (ame_w_bits_data           ),
    .io_ame_w_bits_strb_o           (ame_w_bits_strb           ),
    .io_ame_w_bits_last_o           (ame_w_bits_last           ),
    .io_ame_b_ready_o               (ame_b_ready               ),
    .io_ame_b_valid_i               (ame_b_valid               ),
    .io_ame_b_bits_resp_i           (ame_b_bits_resp           ),
    .io_ame_b_bits_id_i             (ame_b_bits_id             ),
    .io_ame_ar_ready_i              (ame_ar_ready              ),
    .io_ame_ar_valid_o              (ame_ar_valid              ),
    .io_ame_ar_bits_id_o            (ame_ar_bits_id            ),
    .io_ame_ar_bits_addr_o          (ame_ar_bits_addr          ),
    .io_ame_ar_bits_len_o           (ame_ar_bits_len           ),
    .io_ame_ar_bits_size_o          (ame_ar_bits_size          ),
    .io_ame_ar_bits_burst_o         (ame_ar_bits_burst         ),
    .io_ame_ar_bits_lock_o          (ame_ar_bits_lock          ),
    .io_ame_ar_bits_cache_o         (ame_ar_bits_cache         ),
    .io_ame_ar_bits_prot_o          (ame_ar_bits_prot          ),
    .io_ame_r_ready_o               (ame_r_ready               ),
    .io_ame_r_valid_i               (ame_r_valid               ),
    .io_ame_r_bits_id_i             (ame_r_bits_id             ),
    .io_ame_r_bits_data_i           (ame_r_bits_data           ),
    .io_ame_r_bits_resp_i           (ame_r_bits_resp           ),
    .io_ame_r_bits_last_i           (ame_r_bits_last           ),
    .io_ame_hasStore_o              (ame_hasStore              )

);



bmc300 i_axi_xbar(
      .us0_araddr                (axim_ar_bits_addr   ),
      .us0_arburst               (axim_ar_bits_burst  ),
      .us0_arcache               (axim_ar_bits_cache  ),
      .us0_arid                  (axim_ar_bits_id     ),
      .us0_arlen                 (axim_ar_bits_len    ),
      .us0_arlock                (axim_ar_bits_lock   ),
      .us0_arprot                (axim_ar_bits_prot   ),
      .us0_arready               (axim_ar_ready       ),
      .us0_arsize                (axim_ar_bits_size   ),
      .us0_arvalid               (axim_ar_valid       ),
      .us0_awaddr                (axim_aw_bits_addr   ),
      .us0_awburst               (axim_aw_bits_burst  ),
      .us0_awcache               (axim_aw_bits_cache  ),
      .us0_awid                  (axim_aw_bits_id     ),
      .us0_awlen                 (axim_aw_bits_len    ),
      .us0_awlock                (axim_aw_bits_lock   ),
      .us0_awprot                (axim_aw_bits_prot   ),
      .us0_awready               (axim_aw_ready       ),
      .us0_awsize                (axim_aw_bits_size   ),
      .us0_awvalid               (axim_aw_valid       ),
      .us0_bid                   (axim_b_bits_id      ),
      .us0_bready                (axim_b_ready        ),
      .us0_bresp                 (axim_b_bits_resp    ),
      .us0_bvalid                (axim_b_valid        ),
      .us0_rdata                 (axim_r_bits_data    ),
      .us0_rid                   (axim_r_bits_id      ),
      .us0_rlast                 (axim_r_bits_last    ),
      .us0_rready                (axim_r_ready        ),
      .us0_rresp                 (axim_r_bits_resp    ),
      .us0_rvalid                (axim_r_valid        ),
      .us0_wdata                 (axim_w_bits_data    ),
      .us0_wlast                 (axim_w_bits_last    ),
      .us0_wready                (axim_w_ready        ),
      .us0_wstrb                 (axim_w_bits_strb    ),
      .us0_wvalid                (axim_w_valid        ),
      .us1_araddr                (icache_ar_bits_addr   ),
      .us1_arburst               (icache_ar_bits_burst  ),
      .us1_arcache               (icache_ar_bits_cache  ),
      .us1_arid                  ({3'b0,icache_ar_bits_id}     ),
      .us1_arlen                 (icache_ar_bits_len    ),
      .us1_arlock                (icache_ar_bits_lock   ),
      .us1_arprot                (icache_ar_bits_prot   ),
      .us1_arready               (icache_ar_ready       ),
      .us1_arsize                (icache_ar_bits_size   ),
      .us1_arvalid               (icache_ar_valid       ),
      .us1_awaddr                (64'b0),
      .us1_awburst               (2'b0),
      .us1_awcache               (4'b0),
      .us1_awid                  (4'b0),
      .us1_awlen                 (8'b0),
      .us1_awlock                (1'b0),
      .us1_awprot                (3'b0),
      .us1_awready               (   ),
      .us1_awsize                (3'b0),
      .us1_awvalid               (1'b0),
      .us1_bid                   (   ),
      .us1_bready                (   ),
      .us1_bresp                 (   ),
      .us1_bvalid                (   ),
      .us1_rdata                 (icache_r_bits_data    ),
      .us1_rid                   ({icache_r_bits_id_nc3,icache_r_bits_id}      ),
      .us1_rlast                 (icache_r_bits_last    ),
      .us1_rready                (icache_r_ready        ),
      .us1_rresp                 (icache_r_bits_resp    ),
      .us1_rvalid                (icache_r_valid        ),
      .us1_wdata                 (512'b0),
      .us1_wlast                 (1'b0),
      .us1_wready                (   ),
      .us1_wstrb                 (64'b0),
      .us1_wvalid                (1'b0),

      .us2_araddr                (mmu_axi_ar_req_o_bits_addr   ),
      .us2_arburst               (mmu_axi_ar_req_o_bits_burst  ),
      .us2_arcache               (mmu_axi_ar_req_o_bits_cache  ),
      .us2_arid                  (mmu_axi_ar_req_o_bits_id     ),
      .us2_arlen                 (mmu_axi_ar_req_o_bits_len    ),
      .us2_arlock                (mmu_axi_ar_req_o_bits_lock   ),
      .us2_arprot                (mmu_axi_ar_req_o_bits_prot   ),
      .us2_arready               (mmu_axi_ar_req_o_ready       ),
      .us2_arsize                (mmu_axi_ar_req_o_bits_size   ),
      .us2_arvalid               (mmu_axi_ar_req_o_valid       ),
      .us2_awaddr                (64'b0),
      .us2_awburst               (2'b0),
      .us2_awcache               (4'b0),
      .us2_awid                  (4'b0),
      .us2_awlen                 (8'b0),
      .us2_awlock                (1'b0),
      .us2_awprot                (3'b0),
      .us2_awready               (   ),
      .us2_awsize                (3'b0),
      .us2_awvalid               (1'b0),
      .us2_bid                   (   ),
      .us2_bready                (   ),
      .us2_bresp                 (   ),
      .us2_bvalid                (   ),
      .us2_rdata                 (mmu_axi_r_resp_i_bits_data    ),
      .us2_rid                   (mmu_axi_r_resp_i_bits_id      ),
      .us2_rlast                 (mmu_axi_r_resp_i_bits_last    ),
      .us2_rready                (mmu_axi_r_resp_i_ready        ),
      .us2_rresp                 (mmu_axi_r_resp_i_bits_resp    ),
      .us2_rvalid                (mmu_axi_r_resp_i_valid        ),
      .us2_wdata                 (512'b0),
      .us2_wlast                 (1'b0),
      .us2_wready                (   ),
      .us2_wstrb                 (64'b0),
      .us2_wvalid                (1'b0),

      .us3_araddr                (coprocessor_ar_bits_addr   ),
      .us3_arburst               (coprocessor_ar_bits_burst  ),
      .us3_arcache               (coprocessor_ar_bits_cache  ),
      .us3_arid                  (coprocessor_ar_bits_id     ),
      .us3_arlen                 (coprocessor_ar_bits_len    ),
      .us3_arlock                (coprocessor_ar_bits_lock   ),
      .us3_arprot                (coprocessor_ar_bits_prot   ),
      .us3_arready               (coprocessor_ar_ready       ),
      .us3_arsize                (coprocessor_ar_bits_size   ),
      .us3_arvalid               (coprocessor_ar_valid       ),
      .us3_awaddr                (coprocessor_aw_bits_addr   ),
      .us3_awburst               (coprocessor_aw_bits_burst  ),
      .us3_awcache               (coprocessor_aw_bits_cache  ),
      .us3_awid                  (coprocessor_aw_bits_id     ),
      .us3_awlen                 (coprocessor_aw_bits_len    ),
      .us3_awlock                (coprocessor_aw_bits_lock   ),
      .us3_awprot                (coprocessor_aw_bits_prot   ),
      .us3_awready               (coprocessor_aw_ready       ),
      .us3_awsize                (coprocessor_aw_bits_size   ),
      .us3_awvalid               (coprocessor_aw_valid       ),
      .us3_bid                   (coprocessor_b_bits_id      ),
      .us3_bready                (coprocessor_b_ready        ),
      .us3_bresp                 (coprocessor_b_bits_resp    ),
      .us3_bvalid                (coprocessor_b_valid        ),
      .us3_rdata                 (coprocessor_r_bits_data    ),
      .us3_rid                   (coprocessor_r_bits_id      ),
      .us3_rlast                 (coprocessor_r_bits_last    ),
      .us3_rready                (coprocessor_r_ready        ),
      .us3_rresp                 (coprocessor_r_bits_resp    ),
      .us3_rvalid                (coprocessor_r_valid        ),
      .us3_wdata                 (coprocessor_w_bits_data    ),
      .us3_wlast                 (coprocessor_w_bits_last    ),
      .us3_wready                (coprocessor_w_ready        ),
      .us3_wstrb                 (coprocessor_w_bits_strb    ),
      .us3_wvalid                (coprocessor_w_valid        ),

      .us4_araddr                (w_dma_axim_ar_bits_addr   ),
      .us4_arburst               (w_dma_axim_ar_bits_burst  ),
      .us4_arcache               (w_dma_axim_ar_bits_cache  ),
      .us4_arid                  (w_dma_axim_ar_bits_id     ),
      .us4_arlen                 (w_dma_axim_ar_bits_len    ),
      .us4_arlock                (w_dma_axim_ar_bits_lock   ),
      .us4_arprot                (w_dma_axim_ar_bits_prot   ),
      .us4_arready               (w_dma_axim_ar_ready       ),
      .us4_arsize                (w_dma_axim_ar_bits_size   ),
      .us4_arvalid               (w_dma_axim_ar_valid       ),
      .us4_awaddr                (w_dma_axim_aw_bits_addr   ),
      .us4_awburst               (w_dma_axim_aw_bits_burst  ),
      .us4_awcache               (w_dma_axim_aw_bits_cache  ),
      .us4_awid                  (w_dma_axim_aw_bits_id     ),
      .us4_awlen                 (w_dma_axim_aw_bits_len    ),
      .us4_awlock                (w_dma_axim_aw_bits_lock   ),
      .us4_awprot                (w_dma_axim_aw_bits_prot   ),
      .us4_awready               (w_dma_axim_aw_ready       ),
      .us4_awsize                (w_dma_axim_aw_bits_size   ),
      .us4_awvalid               (w_dma_axim_aw_valid       ),
      .us4_bid                   (w_dma_axim_b_bits_id      ),
      .us4_bready                (w_dma_axim_b_ready        ),
      .us4_bresp                 (w_dma_axim_b_bits_resp    ),
      .us4_bvalid                (w_dma_axim_b_valid        ),
      .us4_rdata                 (w_dma_axim_r_bits_data    ),
      .us4_rid                   (w_dma_axim_r_bits_id      ),
      .us4_rlast                 (w_dma_axim_r_bits_last    ),
      .us4_rready                (w_dma_axim_r_ready        ),
      .us4_rresp                 (w_dma_axim_r_bits_resp    ),
      .us4_rvalid                (w_dma_axim_r_valid        ),
      .us4_wdata                 (w_dma_axim_w_bits_data    ),
      .us4_wlast                 (w_dma_axim_w_bits_last    ),
      .us4_wready                (w_dma_axim_w_ready        ),
      .us4_wstrb                 (w_dma_axim_w_bits_strb    ),
      .us4_wvalid                (w_dma_axim_w_valid        ),

      .us5_araddr                (config_araddr       ),
      .us5_arburst               (config_arburst      ),
      .us5_arcache               (config_arcache      ),
      .us5_arid                  (config_arid         ),
      .us5_arlen                 (config_arlen        ),
      .us5_arlock                (config_arlock       ),
      .us5_arprot                (config_arprot       ),
      .us5_arready               (config_arready      ),
      .us5_arsize                (config_arsize       ),
      .us5_arvalid               (config_arvalid      ),
      .us5_awaddr                (config_awaddr       ),
      .us5_awburst               (config_awburst      ),
      .us5_awcache               (config_awcache      ),
      .us5_awid                  (config_awid         ),
      .us5_awlen                 (config_awlen        ),
      .us5_awlock                (config_awlock       ),
      .us5_awprot                (config_awprot       ),
      .us5_awready               (config_awready      ),
      .us5_awsize                (config_awsize       ),
      .us5_awvalid               (config_awvalid      ),
      .us5_bid                   (config_bid          ),
      .us5_bready                (config_bready       ),
      .us5_bresp                 (config_bresp        ),
      .us5_bvalid                (config_bvalid       ),
      .us5_rdata                 (config_rdata        ),
      .us5_rid                   (config_rid          ),
      .us5_rlast                 (config_rlast        ),
      .us5_rready                (config_rready       ),
      .us5_rresp                 (config_rresp        ),
      .us5_rvalid                (config_rvalid       ),
      .us5_wdata                 (config_wdata        ),
      .us5_wlast                 (config_wlast        ),
      .us5_wready                (config_wready       ),
      .us5_wstrb                 (config_wstrb        ),
      .us5_wvalid                (config_wvalid       ),

      // AME shares no AXI channel with RVV: us6 has its own outstanding and
      // response-ID tracking and is allowed to access DDR (ds1) only.
      .us6_araddr                (ame_ar_bits_addr   ),
      .us6_arburst               (ame_ar_bits_burst  ),
      .us6_arcache               (ame_ar_bits_cache  ),
      .us6_arid                  (ame_ar_bits_id     ),
      .us6_arlen                 (ame_ar_bits_len    ),
      .us6_arlock                (ame_ar_bits_lock   ),
      .us6_arprot                (ame_ar_bits_prot   ),
      .us6_arready               (ame_ar_ready       ),
      .us6_arsize                (ame_ar_bits_size   ),
      .us6_arvalid               (ame_ar_valid       ),
      .us6_awaddr                (ame_aw_bits_addr   ),
      .us6_awburst               (ame_aw_bits_burst  ),
      .us6_awcache               (ame_aw_bits_cache  ),
      .us6_awid                  (ame_aw_bits_id     ),
      .us6_awlen                 (ame_aw_bits_len    ),
      .us6_awlock                (ame_aw_bits_lock   ),
      .us6_awprot                (ame_aw_bits_prot   ),
      .us6_awready               (ame_aw_ready       ),
      .us6_awsize                (ame_aw_bits_size   ),
      .us6_awvalid               (ame_aw_valid       ),
      .us6_bid                   (ame_b_bits_id      ),
      .us6_bready                (ame_b_ready        ),
      .us6_bresp                 (ame_b_bits_resp    ),
      .us6_bvalid                (ame_b_valid        ),
      .us6_rdata                 (ame_r_bits_data    ),
      .us6_rid                   (ame_r_bits_id      ),
      .us6_rlast                 (ame_r_bits_last    ),
      .us6_rready                (ame_r_ready        ),
      .us6_rresp                 (ame_r_bits_resp    ),
      .us6_rvalid                (ame_r_valid        ),
      .us6_wdata                 (ame_w_bits_data    ),
      .us6_wlast                 (ame_w_bits_last    ),
      .us6_wready                (ame_w_ready        ),
      .us6_wstrb                 (ame_w_bits_strb    ),
      .us6_wvalid                (ame_w_valid        ),

      .ds1_araddr                (axi_ddr_araddr   ),
      .ds1_arburst               (axi_ddr_arburst  ),
      .ds1_arcache               (axi_ddr_arcache  ),
      .ds1_arid                  (axi_ddr_arid     ),
      .ds1_arlen                 (axi_ddr_arlen    ),
      .ds1_arlock                (axi_ddr_arlock   ),
      .ds1_arprot                (axi_ddr_arprot   ),
      .ds1_arready               (axi_ddr_arready  ),
      .ds1_arsize                (axi_ddr_arsize   ),
      .ds1_arvalid               (axi_ddr_arvalid  ),
      .ds1_awaddr                (axi_ddr_awaddr   ),
      .ds1_awburst               (axi_ddr_awburst  ),
      .ds1_awcache               (axi_ddr_awcache  ),
      .ds1_awid                  (axi_ddr_awid     ),
      .ds1_awlen                 (axi_ddr_awlen    ),
      .ds1_awlock                (axi_ddr_awlock   ),
      .ds1_awprot                (axi_ddr_awprot   ),
      .ds1_awready               (axi_ddr_awready  ),
      .ds1_awsize                (axi_ddr_awsize   ),
      .ds1_awvalid               (axi_ddr_awvalid  ),
      .ds1_bid                   (axi_ddr_bid      ),
      .ds1_bready                (axi_ddr_bready   ),
      .ds1_bresp                 (axi_ddr_bresp    ),
      .ds1_bvalid                (axi_ddr_bvalid   ),
      .ds1_rdata                 (axi_ddr_rdata    ),
      .ds1_rid                   (axi_ddr_rid      ),
      .ds1_rlast                 (axi_ddr_rlast    ),
      .ds1_rready                (axi_ddr_rready   ),
      .ds1_rresp                 (axi_ddr_rresp    ),
      .ds1_rvalid                (axi_ddr_rvalid   ),
      .ds1_wdata                 (axi_ddr_wdata    ),
      .ds1_wlast                 (axi_ddr_wlast    ),
      .ds1_wready                (axi_ddr_wready   ),
      .ds1_wstrb                 (axi_ddr_wstrb    ),
      .ds1_wvalid                (axi_ddr_wvalid   ),

      .ds2_araddr                (axi_rom_ar_addr    ),
      .ds2_arburst               (axi_rom_ar_burst   ),
      .ds2_arcache               (axi_rom_ar_cache   ),
      .ds2_arid                  (axi_rom_ar_id      ),
      .ds2_arlen                 (axi_rom_ar_len     ),
      .ds2_arlock                (axi_rom_ar_lock    ),
      .ds2_arprot                (axi_rom_ar_prot    ),
      .ds2_arready               (axi_rom_ar_ready   ),
      .ds2_arsize                (axi_rom_ar_size    ),
      .ds2_arvalid               (axi_rom_ar_valid   ),
      .ds2_awaddr                (axi_rom_aw_addr    ),
      .ds2_awburst               (axi_rom_aw_burst   ),
      .ds2_awcache               (axi_rom_aw_cache   ),
      .ds2_awid                  (axi_rom_aw_id      ),
      .ds2_awlen                 (axi_rom_aw_len     ),
      .ds2_awlock                (axi_rom_aw_lock    ),
      .ds2_awprot                (axi_rom_aw_prot    ),
      .ds2_awready               (axi_rom_aw_ready   ),
      .ds2_awsize                (axi_rom_aw_size    ),
      .ds2_awvalid               (axi_rom_aw_valid   ),
      .ds2_bid                   (axi_rom_b_id       ),
      .ds2_bready                (axi_rom_b_ready    ),
      .ds2_bresp                 (axi_rom_b_resp     ),
      .ds2_bvalid                (axi_rom_b_valid    ),
      .ds2_rdata                 (axi_rom_r_data     ),
      .ds2_rid                   (axi_rom_r_id       ),
      .ds2_rlast                 (axi_rom_r_last     ),
      .ds2_rready                (axi_rom_r_ready    ),
      .ds2_rresp                 (axi_rom_r_resp     ),
      .ds2_rvalid                (axi_rom_r_valid    ),
      .ds2_wdata                 (axi_rom_w_data     ),
      .ds2_wlast                 (axi_rom_w_last     ),
      .ds2_wready                (axi_rom_w_ready    ),
      .ds2_wstrb                 (axi_rom_w_strb     ),
      .ds2_wvalid                (axi_rom_w_valid    ),

      .ds3_araddr                (axi_sram_ar_addr   ),
      .ds3_arburst               (axi_sram_ar_burst  ),
      .ds3_arcache               (axi_sram_ar_cache  ),
      .ds3_arid                  (axi_sram_ar_id     ),
      .ds3_arlen                 (axi_sram_ar_len    ),
      .ds3_arlock                (axi_sram_ar_lock   ),
      .ds3_arprot                (axi_sram_ar_prot   ),
      .ds3_arready               (axi_sram_ar_ready  ),
      .ds3_arsize                (axi_sram_ar_size   ),
      .ds3_arvalid               (axi_sram_ar_valid  ),
      .ds3_awaddr                (axi_sram_aw_addr   ),
      .ds3_awburst               (axi_sram_aw_burst  ),
      .ds3_awcache               (axi_sram_aw_cache  ),
      .ds3_awid                  (axi_sram_aw_id     ),
      .ds3_awlen                 (axi_sram_aw_len    ),
      .ds3_awlock                (axi_sram_aw_lock   ),
      .ds3_awprot                (axi_sram_aw_prot   ),
      .ds3_awready               (axi_sram_aw_ready  ),
      .ds3_awsize                (axi_sram_aw_size   ),
      .ds3_awvalid               (axi_sram_aw_valid  ),
      .ds3_bid                   (axi_sram_b_id      ),
      .ds3_bready                (axi_sram_b_ready   ),
      .ds3_bresp                 (axi_sram_b_resp    ),
      .ds3_bvalid                (axi_sram_b_valid   ),
      .ds3_rdata                 (axi_sram_r_data    ),
      .ds3_rid                   (axi_sram_r_id      ),
      .ds3_rlast                 (axi_sram_r_last    ),
      .ds3_rready                (axi_sram_r_ready   ),
      .ds3_rresp                 (axi_sram_r_resp    ),
      .ds3_rvalid                (axi_sram_r_valid   ),
      .ds3_wdata                 (axi_sram_w_data    ),
      .ds3_wlast                 (axi_sram_w_last    ),
      .ds3_wready                (axi_sram_w_ready   ),
      .ds3_wstrb                 (axi_sram_w_strb    ),
      .ds3_wvalid                (axi_sram_w_valid   ),

      .ds4_araddr                (axi_ethernet_ar_addr   ),
      .ds4_arburst               (axi_ethernet_ar_burst  ),
      .ds4_arcache               (axi_ethernet_ar_cache  ),
      .ds4_arid                  (axi_ethernet_ar_id     ),
      .ds4_arlen                 (axi_ethernet_ar_len    ),
      .ds4_arlock                (axi_ethernet_ar_lock   ),
      .ds4_arprot                (axi_ethernet_ar_prot   ),
      .ds4_arready               (axi_ethernet_ar_ready  ),
      .ds4_arsize                (axi_ethernet_ar_size   ),
      .ds4_arvalid               (axi_ethernet_ar_valid  ),
      .ds4_awaddr                (axi_ethernet_aw_addr   ),
      .ds4_awburst               (axi_ethernet_aw_burst  ),
      .ds4_awcache               (axi_ethernet_aw_cache  ),
      .ds4_awid                  (axi_ethernet_aw_id     ),
      .ds4_awlen                 (axi_ethernet_aw_len    ),
      .ds4_awlock                (axi_ethernet_aw_lock   ),
      .ds4_awprot                (axi_ethernet_aw_prot   ),
      .ds4_awready               (axi_ethernet_aw_ready  ),
      .ds4_awsize                (axi_ethernet_aw_size   ),
      .ds4_awvalid               (axi_ethernet_aw_valid  ),
      .ds4_bid                   (axi_ethernet_b_id      ),
      .ds4_bready                (axi_ethernet_b_ready   ),
      .ds4_bresp                 (axi_ethernet_b_resp    ),
      .ds4_bvalid                (axi_ethernet_b_valid   ),
      .ds4_rdata                 (axi_ethernet_r_data    ),
      .ds4_rid                   (axi_ethernet_r_id      ),
      .ds4_rlast                 (axi_ethernet_r_last    ),
      .ds4_rready                (axi_ethernet_r_ready   ),
      .ds4_rresp                 (axi_ethernet_r_resp    ),
      .ds4_rvalid                (axi_ethernet_r_valid   ),
      .ds4_wdata                 (axi_ethernet_w_data    ),
      .ds4_wlast                 (axi_ethernet_w_last    ),
      .ds4_wready                (axi_ethernet_w_ready   ),
      .ds4_wstrb                 (axi_ethernet_w_strb    ),
      .ds4_wvalid                (axi_ethernet_w_valid   ),

      .ds8_araddr               (axi2llpp_ar_addr   ),
      .ds8_arburst              (axi2llpp_ar_burst  ),
      .ds8_arcache              (axi2llpp_ar_cache  ),
      .ds8_arid                 (axi2llpp_ar_id     ),
      .ds8_arlen                (axi2llpp_ar_len    ),
      .ds8_arlock               (axi2llpp_ar_lock   ),
      .ds8_arprot               (axi2llpp_ar_prot   ),
      .ds8_arready              (axi2llpp_ar_ready  ),
      .ds8_arsize               (axi2llpp_ar_size   ),
      .ds8_arvalid              (axi2llpp_ar_valid  ),
      .ds8_awaddr               (axi2llpp_aw_addr   ),
      .ds8_awburst              (axi2llpp_aw_burst  ),
      .ds8_awcache              (axi2llpp_aw_cache  ),
      .ds8_awid                 (axi2llpp_aw_id     ),
      .ds8_awlen                (axi2llpp_aw_len    ),
      .ds8_awlock               (axi2llpp_aw_lock   ),
      .ds8_awprot               (axi2llpp_aw_prot   ),
      .ds8_awready              (axi2llpp_aw_ready  ),
      .ds8_awsize               (axi2llpp_aw_size   ),
      .ds8_awvalid              (axi2llpp_aw_valid  ),
      .ds8_bid                  (axi2llpp_b_id      ),
      .ds8_bready               (axi2llpp_b_ready   ),
      .ds8_bresp                (axi2llpp_b_resp    ),
      .ds8_bvalid               (axi2llpp_b_valid   ),
      .ds8_rdata                (axi2llpp_r_data    ),
      .ds8_rid                  (axi2llpp_r_id      ),
      .ds8_rlast                (axi2llpp_r_last    ),
      .ds8_rready               (axi2llpp_r_ready   ),
      .ds8_rresp                (axi2llpp_r_resp    ),
      .ds8_rvalid               (axi2llpp_r_valid   ),
      .ds8_wdata                (axi2llpp_w_data    ),
      .ds8_wlast                (axi2llpp_w_last    ),
      .ds8_wready               (axi2llpp_w_ready   ),
      .ds8_wstrb                (axi2llpp_w_strb    ),
      .ds8_wvalid               (axi2llpp_w_valid   ),

      .ds9_araddr               (axi_clint_ar_addr    ),
      .ds9_arburst              (axi_clint_ar_burst   ),
      .ds9_arcache              (axi_clint_ar_cache   ),
      .ds9_arid                 (axi_clint_ar_id      ),
      .ds9_arlen                (axi_clint_ar_len     ),
      .ds9_arlock               (axi_clint_ar_lock    ),
      .ds9_arprot               (axi_clint_ar_prot    ),
      .ds9_arready              (axi_clint_ar_ready   ),
      .ds9_arsize               (axi_clint_ar_size    ),
      .ds9_arvalid              (axi_clint_ar_valid   ),
      .ds9_awaddr               (axi_clint_aw_addr    ),
      .ds9_awburst              (axi_clint_aw_burst   ),
      .ds9_awcache              (axi_clint_aw_cache   ),
      .ds9_awid                 (axi_clint_aw_id      ),
      .ds9_awlen                (axi_clint_aw_len     ),
      .ds9_awlock               (axi_clint_aw_lock    ),
      .ds9_awprot               (axi_clint_aw_prot    ),
      .ds9_awready              (axi_clint_aw_ready   ),
      .ds9_awsize               (axi_clint_aw_size    ),
      .ds9_awvalid              (axi_clint_aw_valid   ),
      .ds9_bid                  (axi_clint_b_id       ),
      .ds9_bready               (axi_clint_b_ready    ),
      .ds9_bresp                (axi_clint_b_resp     ),
      .ds9_bvalid               (axi_clint_b_valid    ),
      .ds9_rdata                (axi_clint_r_data     ),
      .ds9_rid                  (axi_clint_r_id       ),
      .ds9_rlast                (axi_clint_r_last     ),
      .ds9_rready               (axi_clint_r_ready    ),
      .ds9_rresp                (axi_clint_r_resp     ),
      .ds9_rvalid               (axi_clint_r_valid    ),
      .ds9_wdata                (axi_clint_w_data     ),
      .ds9_wlast                (axi_clint_w_last     ),
      .ds9_wready               (axi_clint_w_ready    ),
      .ds9_wstrb                (axi_clint_w_strb     ),
      .ds9_wvalid               (axi_clint_w_valid    ),


      .aclk                      (sys_clk),
      .aresetn                   (ndmreset_n)
);












// // @zhengsihan 260323 to replace original UART
// // on ariane_xilinx.sv ,before CLINT

// ============================================================================
// Mailbox Console Monitor for UART (Address 0x10000000)
// ============================================================================

generate if (!COSIM) begin : gen_mailbox_monitor
integer fd;
reg uart_write_pending;

initial begin
    // 打开日志文件
    fd = $fopen("simulation_console.log", "w");
    uart_write_pending = 1'b0;
end

// 组合逻辑判断 AXI 写地址和写数据通道
wire aw_handshake = axim_aw_valid & axim_aw_ready;
wire w_handshake  = axim_w_valid  & axim_w_ready;

// 判断目标地址是否是 UART 的地址 (0x10000000)
wire is_uart_addr = (axim_aw_bits_addr[31:0] == 32'h10000000); 

// 提取实际写入的字符 (截取低 8 位)
wire [7:0] write_char = axim_w_bits_data[7:0];

// 判断写入的是否是可打印的有效 ASCII 字符，或者换行符
wire char_is_printable = (write_char >= 8'h20 && write_char <= 8'h7e) || (write_char == 8'h0a) || (write_char == 8'h0d);

always @(posedge sys_clk) begin
    // 1. 捕捉对 UART 地址发起的写请求
    if (aw_handshake) begin
        if (is_uart_addr) begin
            uart_write_pending <= 1'b1;
        end else begin
            uart_write_pending <= 1'b0;
        end
    end
    
    // 2. 当写数据通过时，且目标是 UART，进行打印
    if (w_handshake && uart_write_pending) begin
        if (char_is_printable) begin
            // 写入文件并刷新
            $fwrite(fd, "%c", write_char);
            $fflush(fd);
            
            // 在 VCS 中的标准输出 (Terminal) 实时打印
            // 提示：VCS 中有时由于行缓冲的机制，$write 配合 $fflush(32'h80000001) 能强制刷新到控制台
            // $display("\nleonleon");
            $write("%c", write_char);
            $fflush(32'h80000001); 
        end
        
        // 结束标记判断 (0xFF)
        if (write_char == 8'hff) begin
            $display("\n==================================");
            $display("Test PASS triggered by Mailbox");
            $display("==================================\n");
            $fclose(fd);
            $finish;
        end

         if (write_char == 8'hee) begin
            $display("\n==================================");
            $display("Test FAIL triggered by Mailbox");
            $display("==================================\n");
            $fclose(fd);
            $finish;
        end
        

        uart_write_pending <= 1'b0;
    end
end



end endgenerate

// ---------------------
// Board peripherals
// ---------------------
// ---------------
// DDR
// ---------------

// Uses external DDR instead

// exmon300 #(
//           .ID_WIDTH    (8),
//           .ADDR_WIDTH  (AxiAddrWidth),
//           .DATA_WIDTH  (AxiDataWidth),
//           .NUM_EX_SEQ  (32)
//           )u_exmon300 (
// 	  .aclk           (sys_clk       ),
// 	  .aresetn        (ndmreset_n    ),

// 	  .us_awid        (axi_ram_aw_id       ),
// 	  .us_awaddr      (axi_ram_aw_addr     ),
// 	  .us_awlock      (axi_ram_aw_lock     ),
// 	  .us_awburst     (axi_ram_aw_burst    ),
// 	  .us_awlen       (axi_ram_aw_len      ),
// 	  .us_awcache     (axi_ram_aw_cache    ),
// 	  .us_awprot      (axi_ram_aw_prot     ),
// 	  .us_awsize      (axi_ram_aw_size     ),
// 	  .us_awqos       (4'b0                ),
// 	  .us_awregion    (4'b0                ),
// 	  .us_awvalid     (axi_ram_aw_valid    ),
// 	  .us_awready     (axi_ram_aw_ready    ),

// 	  .us_wdata       (axi_ram_w_data      ),
// 	  .us_wstrb       (axi_ram_w_strb      ),
// 	  .us_wlast       (axi_ram_w_last      ),
// 	  .us_wvalid      (axi_ram_w_valid     ),
// 	  .us_wready      (axi_ram_w_ready     ),

// 	  .us_bresp       (axi_ram_b_resp      ),
// 	  .us_bid         (axi_ram_b_id        ),
// 	  .us_bvalid      (axi_ram_b_valid     ),
// 	  .us_bready      (axi_ram_b_ready     ),

// 	  .us_arid        (axi_ram_ar_id       ),
// 	  .us_araddr      (axi_ram_ar_addr     ),
// 	  .us_arlock      (axi_ram_ar_lock     ),
// 	  .us_arburst     (axi_ram_ar_burst    ),
// 	  .us_arlen       (axi_ram_ar_len      ),
// 	  .us_arcache     (axi_ram_ar_cache    ),
// 	  .us_arprot      (axi_ram_ar_prot     ),
// 	  .us_arsize      (axi_ram_ar_size     ),
// 	  .us_arqos       (4'b0                ),
// 	  .us_arregion    (4'b0                ),
// 	  .us_arvalid     (axi_ram_ar_valid    ),
// 	  .us_arready     (axi_ram_ar_ready    ),
                                         
// 	  .us_rid         (axi_ram_r_id        ),
// 	  .us_rresp       (axi_ram_r_resp      ),
// 	  .us_rdata       (axi_ram_r_data      ),
// 	  .us_rlast       (axi_ram_r_last      ),
// 	  .us_rvalid      (axi_ram_r_valid     ),
// 	  .us_rready      (axi_ram_r_ready     ),
                                         
                                         
// 	  .ds_awid        (axi_dram_aw_id       ),
// 	  .ds_awaddr      (axi_dram_aw_addr     ),
// 	  .ds_awlock      (axi_dram_aw_lock     ),
// 	  .ds_awburst     (axi_dram_aw_burst    ),
// 	  .ds_awlen       (axi_dram_aw_len      ),
// 	  .ds_awcache     (axi_dram_aw_cache    ),
// 	  .ds_awprot      (axi_dram_aw_prot     ),
// 	  .ds_awsize      (axi_dram_aw_size     ),
// 	  .ds_awqos       (axi_dram_aw_qos      ),
// 	  .ds_awregion    (axi_dram_aw_region   ),
// 	  .ds_awvalid     (axi_dram_aw_valid    ),
// 	  .ds_awready     (axi_dram_aw_ready    ),
                                         
// 	  .ds_wdata       (axi_dram_w_data      ),
// 	  .ds_wstrb       (axi_dram_w_strb      ),
// 	  .ds_wlast       (axi_dram_w_last      ),
// 	  .ds_wvalid      (axi_dram_w_valid     ),
// 	  .ds_wready      (axi_dram_w_ready     ),
                                          
// 	  .ds_bresp       (axi_dram_b_resp      ),
// 	  .ds_bid         (axi_dram_b_id        ),
// 	  .ds_bvalid      (axi_dram_b_valid     ),
// 	  .ds_bready      (axi_dram_b_ready     ),
                                         
// 	  .ds_arid        (axi_dram_ar_id       ),
// 	  .ds_araddr      (axi_dram_ar_addr     ),
// 	  .ds_arlock      (axi_dram_ar_lock     ),
// 	  .ds_arburst     (axi_dram_ar_burst    ),
// 	  .ds_arlen       (axi_dram_ar_len      ),
// 	  .ds_arcache     (axi_dram_ar_cache    ),
// 	  .ds_arprot      (axi_dram_ar_prot     ),
// 	  .ds_arsize      (axi_dram_ar_size     ),
// 	  .ds_arqos       (axi_dram_ar_qos      ),
// 	  .ds_arregion    (axi_dram_ar_region   ),
// 	  .ds_arvalid     (axi_dram_ar_valid    ),
// 	  .ds_arready     (axi_dram_ar_ready    ),
                                         
// 	  .ds_rid         (axi_dram_r_id        ),
// 	  .ds_rresp       (axi_dram_r_resp      ),
// 	  .ds_rdata       (axi_dram_r_data      ),
// 	  .ds_rlast       (axi_dram_r_last      ),
// 	  .ds_rvalid      (axi_dram_r_valid     ),
// 	  .ds_rready      (axi_dram_r_ready     )
// );



// assign axi_dram_r_user = '0;
// assign axi_dram_b_user = '0;
// logic nc_b_id,nc_r_id;


// // ddr4

// // fix DDRBase

// `ifdef FPGA

//    logic [63:0]  s_axi_araddr_fix;
//    logic [63:0]  s_axi_awaddr_fix;
//    assign s_axi_araddr_fix = axi_dram_ar_addr - 64'h80000000;
//    assign s_axi_awaddr_fix = axi_dram_aw_addr - 64'h80000000;
  
//    uvw_axi4_to_ddr4 i_ddr (
//    .ddr4ip_dut_axi_aclk (sys_clk)/* synthesis syn_isclock = 1 */, 
//    .ddr4ip_dut_axi_aresetn(ndmreset_n),
//    .ddr4ip_dut_axi_awaddr(s_axi_awaddr_fix[33:0]),
//    .ddr4ip_dut_axi_awburst(axi_dram_aw_burst),
//    .ddr4ip_dut_axi_awcache(axi_dram_aw_cache),
//    .ddr4ip_dut_axi_awid({1'b0,axi_dram_aw_id}),//fix
//    .ddr4ip_dut_axi_awlen(axi_dram_aw_len),
//    .ddr4ip_dut_axi_awlock(axi_dram_aw_lock),
//    .ddr4ip_dut_axi_awprot(axi_dram_aw_prot),
//    .ddr4ip_dut_axi_awqos(axi_dram_aw_qos),
//    .ddr4ip_dut_axi_awready(axi_dram_aw_ready),
//    .ddr4ip_dut_axi_awregion('0),
//    .ddr4ip_dut_axi_awsize(axi_dram_aw_size),
//    .ddr4ip_dut_axi_awvalid(axi_dram_aw_valid),
//    .ddr4ip_dut_axi_wdata(axi_dram_w_data),
//    .ddr4ip_dut_axi_wlast(axi_dram_w_last),
//    .ddr4ip_dut_axi_wready(axi_dram_w_ready),
//    .ddr4ip_dut_axi_wstrb(axi_dram_w_strb),
//    .ddr4ip_dut_axi_wvalid(axi_dram_w_valid),
//    .ddr4ip_dut_axi_bid    ({nc_b_id,axi_dram_b_id}),//fix
//    .ddr4ip_dut_axi_bready(axi_dram_b_ready),
//    .ddr4ip_dut_axi_bresp(axi_dram_b_resp),
//    .ddr4ip_dut_axi_bvalid(axi_dram_b_valid),
//    .ddr4ip_dut_axi_araddr(s_axi_araddr_fix[33:0]),
//    .ddr4ip_dut_axi_arburst(axi_dram_ar_burst),
//    .ddr4ip_dut_axi_arcache(axi_dram_ar_cache),
//    .ddr4ip_dut_axi_arid({1'b0,axi_dram_ar_id}),//fix
//    .ddr4ip_dut_axi_arlen(axi_dram_ar_len),
//    .ddr4ip_dut_axi_arlock(axi_dram_ar_lock),
//    .ddr4ip_dut_axi_arprot(axi_dram_ar_prot),
//    .ddr4ip_dut_axi_arqos(axi_dram_ar_qos),
//    .ddr4ip_dut_axi_arready(axi_dram_ar_ready),
//    .ddr4ip_dut_axi_arregion('0),
//    .ddr4ip_dut_axi_arsize(axi_dram_ar_size),
//    .ddr4ip_dut_axi_arvalid(axi_dram_ar_valid),
//    .ddr4ip_dut_axi_rdata(axi_dram_r_data),
//    .ddr4ip_dut_axi_rid    ({nc_r_id,axi_dram_r_id}),//fix
//    .ddr4ip_dut_axi_rlast(axi_dram_r_last),
//    .ddr4ip_dut_axi_rready(axi_dram_r_ready),
//    .ddr4ip_dut_axi_rresp(axi_dram_r_resp),
//    .ddr4ip_dut_axi_rvalid(axi_dram_r_valid),
//    .ddr4ip_dut_axi_aclk_en(1'b1),
//    .sysbus_ghbd_i(),
//    .sysbus_ghbd_o(),
//    .sysbus_gsbd_o(),
//    .sysbus_gsbd_i()
//    );

// `else

//   logic [63:0]  s_axi_araddr_fix;
//   logic [63:0]  s_axi_awaddr_fix;
//   assign s_axi_araddr_fix = axi_dram_ar_addr - 64'h80000000;
//   assign s_axi_awaddr_fix = axi_dram_aw_addr - 64'h80000000;

// axi_bram_512MB u_main_memory (
//   .s_axi_aclk       ( sys_clk                             ), // input wire s_axi_aclk
//   .s_axi_aresetn    ( ndmreset_n                          ), // input wire s_axi_aresetn
//   .s_axi_awid       ( axi_dram_aw_id     ), // input wire [8 : 0] s_axi_awid
//   .s_axi_awaddr     ( s_axi_awaddr_fix   ), // input wire [21 : 0] s_axi_awaddr
//   .s_axi_awlen      ( axi_dram_aw_len    ), // input wire [7 : 0] s_axi_awlen
//   .s_axi_awsize     ( axi_dram_aw_size   ), // input wire [2 : 0] s_axi_awsize
//   .s_axi_awburst    ( axi_dram_aw_burst  ), // input wire [1 : 0] s_axi_awburst
//   .s_axi_awlock     ( axi_dram_aw_lock   ), // input wire s_axi_awlock
//   .s_axi_awcache    ( axi_dram_aw_cache  ), // input wire [3 : 0] s_axi_awcache
//   .s_axi_awprot     ( axi_dram_aw_prot   ), // input wire [2 : 0] s_axi_awprot
//   .s_axi_awvalid    ( axi_dram_aw_valid  ), // input wire s_axi_awvalid
//   .s_axi_awready    ( axi_dram_aw_ready  ), // output wire s_axi_awready

//   .s_axi_wdata      ( axi_dram_w_data    ), // input wire [511 : 0] s_axi_wdata
//   .s_axi_wstrb      ( axi_dram_w_strb    ), // input wire [63 : 0] s_axi_wstrb
//   .s_axi_wlast      ( axi_dram_w_last    ), // input wire s_axi_wlast
//   .s_axi_wvalid     ( axi_dram_w_valid   ), // input wire s_axi_wvalid
//   .s_axi_wready     ( axi_dram_w_ready   ), // output wire s_axi_wready

//   .s_axi_bid        ( axi_dram_b_id      ), // output wire [8 : 0] s_axi_bid
//   .s_axi_bresp      ( axi_dram_b_resp    ), // output wire [1 : 0] s_axi_bresp
//   .s_axi_bvalid     ( axi_dram_b_valid   ), // output wire s_axi_bvalid
//   .s_axi_bready     ( axi_dram_b_ready   ), // input wire s_axi_bready

//   .s_axi_arid       ( axi_dram_ar_id     ), // input wire [8 : 0] s_axi_arid
//   .s_axi_araddr     ( s_axi_araddr_fix   ), // input wire [21 : 0] s_axi_araddr
//   .s_axi_arlen      ( axi_dram_ar_len    ), // input wire [7 : 0] s_axi_arlen
//   .s_axi_arsize     ( axi_dram_ar_size   ), // input wire [2 : 0] s_axi_arsize
//   // .s_axi_arburst    ( 2'b01  ), // input wire [1 : 0] s_axi_arburst
//   .s_axi_arburst    ( axi_dram_ar_burst  ), // input wire [1 : 0] s_axi_arburst
//   .s_axi_arlock     ( axi_dram_ar_lock   ), // input wire s_axi_arlock
//   .s_axi_arcache    ( axi_dram_ar_cache  ), // input wire [3 : 0] s_axi_arcache
//   .s_axi_arprot     ( axi_dram_ar_prot   ), // input wire [2 : 0] s_axi_arprot
//   .s_axi_arvalid    ( axi_dram_ar_valid  ), // input wire s_axi_arvalid
//   .s_axi_arready    ( axi_dram_ar_ready  ), // output wire s_axi_arready

//   .s_axi_rid        ( axi_dram_r_id      ), // output wire [8 : 0] s_axi_rid
//   .s_axi_rdata      ( axi_dram_r_data    ), // output wire [511 : 0] s_axi_rdata
//   .s_axi_rresp      ( axi_dram_r_resp    ), // output wire [1 : 0] s_axi_rresp
//   .s_axi_rlast      ( axi_dram_r_last    ), // output wire s_axi_rlast
//   .s_axi_rvalid     ( axi_dram_r_valid   ), // output wire s_axi_rvalid
//   .s_axi_rready     ( axi_dram_r_ready   )  // input wire s_axi_rready
// );

// `endif



// ---------------
// CLINT
// ---------------
// divide clock by two

assign axi_clint_req.aw.id     = axi_clint_aw_id       ;
assign axi_clint_req.aw.addr   = axi_clint_aw_addr     ;
assign axi_clint_req.aw.len    = axi_clint_aw_len      ;
assign axi_clint_req.aw.size   = axi_clint_aw_size     ;
assign axi_clint_req.aw.burst  = axi_clint_aw_burst    ;
assign axi_clint_req.aw.lock   = axi_clint_aw_lock     ;
assign axi_clint_req.aw.cache  = axi_clint_aw_cache    ;
assign axi_clint_req.aw.prot   = axi_clint_aw_prot     ;
assign axi_clint_req.aw.region = 'b0                 ;
assign axi_clint_req.aw.user   = 'b0                 ;
assign axi_clint_req.aw.qos    = 'b0                 ;
assign axi_clint_req.aw_valid  = axi_clint_aw_valid    ;
assign axi_clint_aw_ready      = axi_clint_resp.aw_ready ;
assign axi_clint_req.w.data    = axi_clint_w_data      ;
assign axi_clint_req.w.strb    = axi_clint_w_strb      ;
assign axi_clint_req.w.last    = axi_clint_w_last      ;
assign axi_clint_req.w.user    = 'b0                 ;
assign axi_clint_req.w_valid   = axi_clint_w_valid     ;
assign axi_clint_w_ready       = axi_clint_resp.w_ready  ;
assign axi_clint_b_id          = axi_clint_resp.b.id     ;
assign axi_clint_b_resp        = axi_clint_resp.b.resp   ;
assign axi_clint_b_valid       = axi_clint_resp.b_valid  ;
assign axi_clint_b_user        = axi_clint_resp.b.user   ;
assign axi_clint_req.b_ready   = axi_clint_b_ready     ;
assign axi_clint_req.ar.id     = axi_clint_ar_id       ;
assign axi_clint_req.ar.addr   = axi_clint_ar_addr     ;
assign axi_clint_req.ar.len    = axi_clint_ar_len      ;
assign axi_clint_req.ar.size   = axi_clint_ar_size     ;
assign axi_clint_req.ar.burst  = axi_clint_ar_burst    ;
assign axi_clint_req.ar.lock   = axi_clint_ar_lock     ;
assign axi_clint_req.ar.cache  = axi_clint_ar_cache    ;
assign axi_clint_req.ar.prot   = axi_clint_ar_prot     ;
assign axi_clint_req.ar.region = 'b0                 ;
assign axi_clint_req.ar.user   = 'b0                 ;
assign axi_clint_req.ar.qos    = 'b0                 ;
assign axi_clint_req.ar_valid  = axi_clint_ar_valid    ;
assign axi_clint_ar_ready      = axi_clint_resp.ar_ready ;
assign axi_clint_r_id          = axi_clint_resp.r.id     ;
assign axi_clint_r_data        = axi_clint_resp.r.data   ;
assign axi_clint_r_resp        = axi_clint_resp.r.resp   ;
assign axi_clint_r_last        = axi_clint_resp.r.last   ;
assign axi_clint_r_user        = axi_clint_resp.r.user   ;
assign axi_clint_r_valid       = axi_clint_resp.r_valid  ;
assign axi_clint_req.r_ready   = axi_clint_r_ready     ;

always_ff @(posedge sys_clk or negedge ndmreset_n) begin
  if (~ndmreset_n) begin
    rtc <= 0;
  end else begin
    rtc <= rtc ^ 1'b1;
  end
end


`AXI_TYPEDEF_ALL(clint64, logic [63:0], ariane_axi::id_t,
                   logic [63:0], logic [7:0], ariane_axi::user_t)
generate if (COSIM) begin : gen_cosim_clint
  clint64_req_t clint64_req;
  clint64_resp_t clint64_resp;
  axi_dw_converter #(
    .AxiMaxReads(4), .AxiSlvPortDataWidth(512), .AxiMstPortDataWidth(64),
    .AxiAddrWidth(64), .AxiIdWidth(ariane_axi::IdWidth),
    .aw_chan_t(ariane_axi::aw_chan_t), .ar_chan_t(ariane_axi::ar_chan_t),
    .b_chan_t(ariane_axi::b_chan_t),
    .slv_w_chan_t(ariane_axi::w_chan_t), .mst_w_chan_t(clint64_w_chan_t),
    .slv_r_chan_t(ariane_axi::r_chan_t), .mst_r_chan_t(clint64_r_chan_t),
    .axi_slv_req_t(ariane_axi::req_t), .axi_slv_resp_t(ariane_axi::resp_t),
    .axi_mst_req_t(clint64_req_t), .axi_mst_resp_t(clint64_resp_t)
  ) u_clint_width (
    .clk_i(sys_clk), .rst_ni(ndmreset_n),
    .slv_req_i(axi_clint_req), .slv_resp_o(axi_clint_resp),
    .mst_req_o(clint64_req), .mst_resp_i(clint64_resp)
  );
clint #(
    .CVA6Cfg        ( CVA6Cfg          ),
    .AXI_ADDR_WIDTH ( AxiAddrWidth     ),
    .AXI_DATA_WIDTH ( 64               ),
    .AXI_ID_WIDTH   ( ariane_axi::IdWidth ),
    .NR_CORES       ( 1                ),
    .axi_req_t      ( clint64_req_t  ),
    .axi_resp_t     ( clint64_resp_t )
) i_clint (
    .clk_i       ( sys_clk            ),
    .rst_ni      ( ndmreset_n     ),
    .testmode_i  ( test_en        ),
    .axi_req_i   ( clint64_req    ),
    .axi_resp_o  ( clint64_resp   ),
    .rtc_i       ( rtc            ),
    .timer_irq_o ( timer_irq      ),
    .ipi_o       ( ipi            )
);
end else begin : gen_board_clint
clint #(
    .CVA6Cfg        ( CVA6Cfg          ),
    .AXI_ADDR_WIDTH ( AxiAddrWidth     ),
    .AXI_DATA_WIDTH ( AxiDataWidth     ),
    .AXI_ID_WIDTH   ( AxiIdWidthSlaves ),
    .NR_CORES       ( 1                ),
    .axi_req_t      ( ariane_axi::req_t  ),
    .axi_resp_t     ( ariane_axi::resp_t )
) i_clint (
    .clk_i       ( sys_clk            ),
    .rst_ni      ( ndmreset_n     ),
    .testmode_i  ( test_en        ),
    .axi_req_i   ( axi_clint_req  ),
    .axi_resp_o  ( axi_clint_resp ),
    .rtc_i       ( rtc            ),
    .timer_irq_o ( timer_irq      ),
    .ipi_o       ( ipi            )
);
end endgenerate

// ---------------
// ROM
// ---------------
assign master_rom.aw_id     = axi_rom_aw_id       ;
assign master_rom.aw_addr   = axi_rom_aw_addr     ;
assign master_rom.aw_len    = axi_rom_aw_len      ;
assign master_rom.aw_size   = axi_rom_aw_size     ;
assign master_rom.aw_burst  = axi_rom_aw_burst    ;
assign master_rom.aw_lock   = axi_rom_aw_lock     ;
assign master_rom.aw_cache  = axi_rom_aw_cache    ;
assign master_rom.aw_prot   = axi_rom_aw_prot     ;
assign master_rom.aw_region = 'b0                 ;
assign master_rom.aw_user   = 'b0                 ;
assign master_rom.aw_qos    = 'b0                 ;
assign master_rom.aw_valid  = axi_rom_aw_valid    ;
assign axi_rom_aw_ready     = master_rom.aw_ready ;
assign master_rom.w_data    = axi_rom_w_data      ;
assign master_rom.w_strb    = axi_rom_w_strb      ;
assign master_rom.w_last    = axi_rom_w_last      ;
assign master_rom.w_user    = 'b0                 ;
assign master_rom.w_valid   = axi_rom_w_valid     ;
assign axi_rom_w_ready      = master_rom.w_ready  ;
assign axi_rom_b_id         = master_rom.b_id     ;
assign axi_rom_b_resp       = master_rom.b_resp   ;
assign axi_rom_b_valid      = master_rom.b_valid  ;
assign axi_rom_b_user       = master_rom.b_user   ;
assign master_rom.b_ready   = axi_rom_b_ready     ;
assign master_rom.ar_id     = axi_rom_ar_id       ;
assign master_rom.ar_addr   = axi_rom_ar_addr     ;
assign master_rom.ar_len    = axi_rom_ar_len      ;
assign master_rom.ar_size   = axi_rom_ar_size     ;
assign master_rom.ar_burst  = axi_rom_ar_burst    ;
assign master_rom.ar_lock   = axi_rom_ar_lock     ;
assign master_rom.ar_cache  = axi_rom_ar_cache    ;
assign master_rom.ar_prot   = axi_rom_ar_prot     ;
assign master_rom.ar_region = 'b0                 ;
assign master_rom.ar_user   = 'b0                 ;
assign master_rom.ar_qos    = 'b0                 ;
assign master_rom.ar_valid  = axi_rom_ar_valid    ;
assign axi_rom_ar_ready     = master_rom.ar_ready ;
assign axi_rom_r_id         = master_rom.r_id     ;
assign axi_rom_r_data       = master_rom.r_data   ;
assign axi_rom_r_resp       = master_rom.r_resp   ;
assign axi_rom_r_last       = master_rom.r_last   ;
assign axi_rom_r_user       = master_rom.r_user   ;
assign axi_rom_r_valid      = master_rom.r_valid  ;
assign master_rom.r_ready   = axi_rom_r_ready     ;


axi2mem #(
    .AXI_ID_WIDTH   ( AxiIdWidthSlaves ),
    .AXI_ADDR_WIDTH ( AxiAddrWidth     ),
    .AXI_DATA_WIDTH ( AxiDataWidth     ),
    .AXI_USER_WIDTH ( AxiUserWidth     )
) i_axi2rom (
    .clk_i  ( sys_clk                     ),
    .rst_ni ( ndmreset_n              ),
    .slave  ( master_rom              ),
    .req_o  ( rom_req                 ),
    .we_o   (                         ),
    .addr_o ( rom_addr                ),
    .be_o   (                         ),
    .data_o (                         ),
    .user_o (                         ),
    .data_i ( rom_rdata               ),
    .user_i ( 64'b0                   )
);

bootrom_64 i_bootrom (
    .clk_i   ( sys_clk       ),
    .req_i   ( rom_req   ),
    .addr_i  ( rom_addr  ),
    .rdata_o ( rom_rdata )
);

// ---------------
// SRAM
// ---------------
assign axi_sram_r_user = '0;
assign axi_sram_b_user = '0;

axi_bram_4MB i_axi_bram (
  .s_axi_aclk       ( sys_clk                             ), // input wire s_axi_aclk
  .s_axi_aresetn    ( ndmreset_n                          ), // input wire s_axi_aresetn
  .s_axi_awid       ( axi_sram_aw_id     ), // input wire [8 : 0] s_axi_awid
  .s_axi_awaddr     ( axi_sram_aw_addr   ), // input wire [21 : 0] s_axi_awaddr
  .s_axi_awlen      ( axi_sram_aw_len    ), // input wire [7 : 0] s_axi_awlen
  .s_axi_awsize     ( axi_sram_aw_size   ), // input wire [2 : 0] s_axi_awsize
  .s_axi_awburst    ( axi_sram_aw_burst  ), // input wire [1 : 0] s_axi_awburst
  .s_axi_awlock     ( axi_sram_aw_lock   ), // input wire s_axi_awlock
  .s_axi_awcache    ( axi_sram_aw_cache  ), // input wire [3 : 0] s_axi_awcache
  .s_axi_awprot     ( axi_sram_aw_prot   ), // input wire [2 : 0] s_axi_awprot
  .s_axi_awvalid    ( axi_sram_aw_valid  ), // input wire s_axi_awvalid
  .s_axi_awready    ( axi_sram_aw_ready  ), // output wire s_axi_awready
  .s_axi_wdata      ( axi_sram_w_data    ), // input wire [511 : 0] s_axi_wdata
  .s_axi_wstrb      ( axi_sram_w_strb    ), // input wire [63 : 0] s_axi_wstrb
  .s_axi_wlast      ( axi_sram_w_last    ), // input wire s_axi_wlast
  .s_axi_wvalid     ( axi_sram_w_valid   ), // input wire s_axi_wvalid
  .s_axi_wready     ( axi_sram_w_ready   ), // output wire s_axi_wready
  .s_axi_bid        ( axi_sram_b_id      ), // output wire [8 : 0] s_axi_bid
  .s_axi_bresp      ( axi_sram_b_resp    ), // output wire [1 : 0] s_axi_bresp
  .s_axi_bvalid     ( axi_sram_b_valid   ), // output wire s_axi_bvalid
  .s_axi_bready     ( axi_sram_b_ready   ), // input wire s_axi_bready
  .s_axi_arid       ( axi_sram_ar_id     ), // input wire [8 : 0] s_axi_arid
  .s_axi_araddr     ( axi_sram_ar_addr   ), // input wire [21 : 0] s_axi_araddr
  .s_axi_arlen      ( axi_sram_ar_len    ), // input wire [7 : 0] s_axi_arlen
  .s_axi_arsize     ( axi_sram_ar_size   ), // input wire [2 : 0] s_axi_arsize
  .s_axi_arburst    ( axi_sram_ar_burst  ), // input wire [1 : 0] s_axi_arburst
  .s_axi_arlock     ( axi_sram_ar_lock   ), // input wire s_axi_arlock
  .s_axi_arcache    ( axi_sram_ar_cache  ), // input wire [3 : 0] s_axi_arcache
  .s_axi_arprot     ( axi_sram_ar_prot   ), // input wire [2 : 0] s_axi_arprot
  .s_axi_arvalid    ( axi_sram_ar_valid  ), // input wire s_axi_arvalid
  .s_axi_arready    ( axi_sram_ar_ready  ), // output wire s_axi_arready
  .s_axi_rid        ( axi_sram_r_id      ), // output wire [8 : 0] s_axi_rid
  .s_axi_rdata      ( axi_sram_r_data    ), // output wire [511 : 0] s_axi_rdata
  .s_axi_rresp      ( axi_sram_r_resp    ), // output wire [1 : 0] s_axi_rresp
  .s_axi_rlast      ( axi_sram_r_last    ), // output wire s_axi_rlast
  .s_axi_rvalid     ( axi_sram_r_valid   ), // output wire s_axi_rvalid
  .s_axi_rready     ( axi_sram_r_ready   )  // input wire s_axi_rready
);



assign axi_ethernet_r_user = '0;
assign axi_ethernet_b_user = '0;

axi_bram_4MB i_ethernet (
  .s_axi_aclk       ( sys_clk                             ), // input wire s_axi_aclk
  .s_axi_aresetn    ( ndmreset_n                          ), // input wire s_axi_aresetn
  .s_axi_awid       ( axi_ethernet_aw_id     ), // input wire [8 : 0] s_axi_awid
  .s_axi_awaddr     ( axi_ethernet_aw_addr   ), // input wire [21 : 0] s_axi_awaddr
  .s_axi_awlen      ( axi_ethernet_aw_len    ), // input wire [7 : 0] s_axi_awlen
  .s_axi_awsize     ( axi_ethernet_aw_size   ), // input wire [2 : 0] s_axi_awsize
  .s_axi_awburst    ( axi_ethernet_aw_burst  ), // input wire [1 : 0] s_axi_awburst
  .s_axi_awlock     ( axi_ethernet_aw_lock   ), // input wire s_axi_awlock
  .s_axi_awcache    ( axi_ethernet_aw_cache  ), // input wire [3 : 0] s_axi_awcache
  .s_axi_awprot     ( axi_ethernet_aw_prot   ), // input wire [2 : 0] s_axi_awprot
  .s_axi_awvalid    ( axi_ethernet_aw_valid  ), // input wire s_axi_awvalid
  .s_axi_awready    ( axi_ethernet_aw_ready  ), // output wire s_axi_awready
  .s_axi_wdata      ( axi_ethernet_w_data    ), // input wire [511 : 0] s_axi_wdata
  .s_axi_wstrb      ( axi_ethernet_w_strb    ), // input wire [63 : 0] s_axi_wstrb
  .s_axi_wlast      ( axi_ethernet_w_last    ), // input wire s_axi_wlast
  .s_axi_wvalid     ( axi_ethernet_w_valid   ), // input wire s_axi_wvalid
  .s_axi_wready     ( axi_ethernet_w_ready   ), // output wire s_axi_wready
  .s_axi_bid        ( axi_ethernet_b_id      ), // output wire [8 : 0] s_axi_bid
  .s_axi_bresp      ( axi_ethernet_b_resp    ), // output wire [1 : 0] s_axi_bresp
  .s_axi_bvalid     ( axi_ethernet_b_valid   ), // output wire s_axi_bvalid
  .s_axi_bready     ( axi_ethernet_b_ready   ), // input wire s_axi_bready
  .s_axi_arid       ( axi_ethernet_ar_id     ), // input wire [8 : 0] s_axi_arid
  .s_axi_araddr     ( axi_ethernet_ar_addr   ), // input wire [21 : 0] s_axi_araddr
  .s_axi_arlen      ( axi_ethernet_ar_len    ), // input wire [7 : 0] s_axi_arlen
  .s_axi_arsize     ( axi_ethernet_ar_size   ), // input wire [2 : 0] s_axi_arsize
  .s_axi_arburst    ( axi_ethernet_ar_burst  ), // input wire [1 : 0] s_axi_arburst
  .s_axi_arlock     ( axi_ethernet_ar_lock   ), // input wire s_axi_arlock
  .s_axi_arcache    ( axi_ethernet_ar_cache  ), // input wire [3 : 0] s_axi_arcache
  .s_axi_arprot     ( axi_ethernet_ar_prot   ), // input wire [2 : 0] s_axi_arprot
  .s_axi_arvalid    ( axi_ethernet_ar_valid  ), // input wire s_axi_arvalid
  .s_axi_arready    ( axi_ethernet_ar_ready  ), // output wire s_axi_arready
  .s_axi_rid        ( axi_ethernet_r_id      ), // output wire [8 : 0] s_axi_rid
  .s_axi_rdata      ( axi_ethernet_r_data    ), // output wire [511 : 0] s_axi_rdata
  .s_axi_rresp      ( axi_ethernet_r_resp    ), // output wire [1 : 0] s_axi_rresp
  .s_axi_rlast      ( axi_ethernet_r_last    ), // output wire s_axi_rlast
  .s_axi_rvalid     ( axi_ethernet_r_valid   ), // output wire s_axi_rvalid
  .s_axi_rready     ( axi_ethernet_r_ready   )  // input wire s_axi_rready
);


sizedn300 #(
    .ID_WIDTH  (8),
    .ADDR_WIDTH  (AxiAddrWidth),
    .US_DATA_WIDTH  (512),
    .DS_DATA_WIDTH  (32)
  ) u_axi2llpp_sizedn(
      .us_araddr               (axi2llpp_ar_addr   ),
      .us_arburst              (axi2llpp_ar_burst  ),
      .us_arcache              (axi2llpp_ar_cache  ),
      .us_arid                 (axi2llpp_ar_id     ),
      .us_arlen                (axi2llpp_ar_len    ),
      .us_arlock               (axi2llpp_ar_lock   ),
      .us_arprot               (axi2llpp_ar_prot   ),
      .us_arready              (axi2llpp_ar_ready  ),
      .us_arsize               (axi2llpp_ar_size   ),
      .us_arvalid              (axi2llpp_ar_valid  ),
      .us_awaddr               (axi2llpp_aw_addr   ),
      .us_awburst              (axi2llpp_aw_burst  ),
      .us_awcache              (axi2llpp_aw_cache  ),
      .us_awid                 (axi2llpp_aw_id     ),
      .us_awlen                (axi2llpp_aw_len    ),
      .us_awlock               (axi2llpp_aw_lock   ),
      .us_awprot               (axi2llpp_aw_prot   ),
      .us_awready              (axi2llpp_aw_ready  ),
      .us_awsize               (axi2llpp_aw_size   ),
      .us_awvalid              (axi2llpp_aw_valid  ),
      .us_bid                  (axi2llpp_b_id      ),
      .us_bready               (axi2llpp_b_ready   ),
      .us_bresp                (axi2llpp_b_resp    ),
      .us_bvalid               (axi2llpp_b_valid   ),
      .us_rdata                (axi2llpp_r_data    ),
      .us_rid                  (axi2llpp_r_id      ),
      .us_rlast                (axi2llpp_r_last    ),
      .us_rready               (axi2llpp_r_ready   ),
      .us_rresp                (axi2llpp_r_resp    ),
      .us_rvalid               (axi2llpp_r_valid   ),
      .us_wdata                (axi2llpp_w_data    ),
      .us_wlast                (axi2llpp_w_last    ),
      .us_wready               (axi2llpp_w_ready   ),
      .us_wstrb                (axi2llpp_w_strb    ),
      .us_wvalid               (axi2llpp_w_valid   ),

      .ds_araddr               (axi2llpp_32_ar_addr   ),
      .ds_arburst              (axi2llpp_32_ar_burst  ),
      .ds_arcache              (axi2llpp_32_ar_cache  ),
      .ds_arlen                (axi2llpp_32_ar_len    ),
      .ds_arlock               (axi2llpp_32_ar_lock   ),
      .ds_arprot               (axi2llpp_32_ar_prot   ),
      .ds_arready              (axi2llpp_32_ar_ready  ),
      .ds_arsize               (axi2llpp_32_ar_size   ),
      .ds_arvalid              (axi2llpp_32_ar_valid  ),
      .ds_awaddr               (axi2llpp_32_aw_addr   ),
      .ds_awburst              (axi2llpp_32_aw_burst  ),
      .ds_awcache              (axi2llpp_32_aw_cache  ),
      .ds_awlen                (axi2llpp_32_aw_len    ),
      .ds_awlock               (axi2llpp_32_aw_lock   ),
      .ds_awprot               (axi2llpp_32_aw_prot   ),
      .ds_awready              (axi2llpp_32_aw_ready  ),
      .ds_awsize               (axi2llpp_32_aw_size   ),
      .ds_awvalid              (axi2llpp_32_aw_valid  ),
      .ds_bready               (axi2llpp_32_b_ready   ),
      .ds_bresp                (axi2llpp_32_b_resp    ),
      .ds_bvalid               (axi2llpp_32_b_valid   ),
      .ds_rdata                (axi2llpp_32_r_data    ),
      .ds_rlast                (axi2llpp_32_r_last    ),
      .ds_rready               (axi2llpp_32_r_ready   ),
      .ds_rresp                (axi2llpp_32_r_resp    ),
      .ds_rvalid               (axi2llpp_32_r_valid   ),
      .ds_wdata                (axi2llpp_32_w_data    ),
      .ds_wlast                (axi2llpp_32_w_last    ),
      .ds_wready               (axi2llpp_32_w_ready   ),
      .ds_wstrb                (axi2llpp_32_w_strb    ),
      .ds_wvalid               (axi2llpp_32_w_valid   ),

      .aclk                    (sys_clk),
      .aresetn                 (ndmreset_n)
);

assign axi2llpp_32_ar_id     = 8'b0;
assign axi2llpp_32_aw_id     = 8'b0;



sizedn300 #(
    .ID_WIDTH  (1),
    .ADDR_WIDTH  (AxiAddrWidth),
    .US_DATA_WIDTH  (64),
    .DS_DATA_WIDTH  (32)
  ) u_llpp_sizedn(
      .us_araddr               (llpp_ar_bits_addr   ),
      .us_arburst              (llpp_ar_bits_burst  ),
      .us_arcache              (llpp_ar_bits_cache  ),
      .us_arid                 (llpp_ar_bits_id     ),
      .us_arlen                (llpp_ar_bits_len    ),
      .us_arlock               (llpp_ar_bits_lock   ),
      .us_arprot               (llpp_ar_bits_prot   ),
      .us_arready              (llpp_ar_ready       ),
      .us_arsize               (llpp_ar_bits_size   ),
      .us_arvalid              (llpp_ar_valid       ),
      .us_awaddr               (llpp_aw_bits_addr   ),
      .us_awburst              (llpp_aw_bits_burst  ),
      .us_awcache              (llpp_aw_bits_cache  ),
      .us_awid                 (llpp_aw_bits_id     ),
      .us_awlen                (llpp_aw_bits_len    ),
      .us_awlock               (llpp_aw_bits_lock   ),
      .us_awprot               (llpp_aw_bits_prot   ),
      .us_awready              (llpp_aw_ready       ),
      .us_awsize               (llpp_aw_bits_size   ),
      .us_awvalid              (llpp_aw_valid       ),
      .us_bid                  (llpp_b_bits_id      ),
      .us_bready               (llpp_b_ready        ),
      .us_bresp                (llpp_b_bits_resp    ),
      .us_bvalid               (llpp_b_valid        ),
      .us_rdata                (llpp_r_bits_data    ),
      .us_rid                  (llpp_r_bits_id      ),
      .us_rlast                (llpp_r_bits_last    ),
      .us_rready               (llpp_r_ready        ),
      .us_rresp                (llpp_r_bits_resp    ),
      .us_rvalid               (llpp_r_valid        ),
      .us_wdata                (llpp_w_bits_data    ),
      .us_wlast                (llpp_w_bits_last    ),
      .us_wready               (llpp_w_ready        ),
      .us_wstrb                (llpp_w_bits_strb    ),
      .us_wvalid               (llpp_w_valid        ),

      .ds_araddr               (llpp_32_ar_bits_addr   ),
      .ds_arburst              (llpp_32_ar_bits_burst  ),
      .ds_arcache              (llpp_32_ar_bits_cache  ),
      .ds_arlen                (llpp_32_ar_bits_len    ),
      .ds_arlock               (llpp_32_ar_bits_lock   ),
      .ds_arprot               (llpp_32_ar_bits_prot   ),
      .ds_arready              (llpp_32_ar_ready       ),
      .ds_arsize               (llpp_32_ar_bits_size   ),
      .ds_arvalid              (llpp_32_ar_valid       ),
      .ds_awaddr               (llpp_32_aw_bits_addr   ),
      .ds_awburst              (llpp_32_aw_bits_burst  ),
      .ds_awcache              (llpp_32_aw_bits_cache  ),
      .ds_awlen                (llpp_32_aw_bits_len    ),
      .ds_awlock               (llpp_32_aw_bits_lock   ),
      .ds_awprot               (llpp_32_aw_bits_prot   ),
      .ds_awready              (llpp_32_aw_ready       ),
      .ds_awsize               (llpp_32_aw_bits_size   ),
      .ds_awvalid              (llpp_32_aw_valid       ),
      .ds_bready               (llpp_32_b_ready        ),
      .ds_bresp                (llpp_32_b_bits_resp    ),
      .ds_bvalid               (llpp_32_b_valid        ),
      .ds_rdata                (llpp_32_r_bits_data    ),
      .ds_rlast                (llpp_32_r_bits_last    ),
      .ds_rready               (llpp_32_r_ready        ),
      .ds_rresp                (llpp_32_r_bits_resp    ),
      .ds_rvalid               (llpp_32_r_valid        ),
      .ds_wdata                (llpp_32_w_bits_data    ),
      .ds_wlast                (llpp_32_w_bits_last    ),
      .ds_wready               (llpp_32_w_ready        ),
      .ds_wstrb                (llpp_32_w_bits_strb    ),
      .ds_wvalid               (llpp_32_w_valid        ),

      .aclk                    (sys_clk),
      .aresetn                 (ndmreset_n)
);

assign llpp_32_aw_bits_id = 1'b0;
assign llpp_32_ar_bits_id = 1'b0;
wire [6:0] nc7_0,nc7_1;



bmc300_llpp i_axi_xbar_llpp(
      .us0_araddr                (llpp_32_ar_bits_addr   ),
      .us0_arburst               (llpp_32_ar_bits_burst  ),
      .us0_arcache               (llpp_32_ar_bits_cache  ),
      .us0_arid                  ({7'b0,llpp_32_ar_bits_id}     ),
      .us0_arlen                 (llpp_32_ar_bits_len    ),
      .us0_arlock                (llpp_32_ar_bits_lock   ),
      .us0_arprot                (llpp_32_ar_bits_prot   ),
      .us0_arready               (llpp_32_ar_ready       ),
      .us0_arsize                (llpp_32_ar_bits_size   ),
      .us0_arvalid               (llpp_32_ar_valid       ),
      .us0_awaddr                (llpp_32_aw_bits_addr   ),
      .us0_awburst               (llpp_32_aw_bits_burst  ),
      .us0_awcache               (llpp_32_aw_bits_cache  ),
      .us0_awid                  ({7'b0,llpp_32_aw_bits_id}     ),
      .us0_awlen                 (llpp_32_aw_bits_len    ),
      .us0_awlock                (llpp_32_aw_bits_lock   ),
      .us0_awprot                (llpp_32_aw_bits_prot   ),
      .us0_awready               (llpp_32_aw_ready       ),
      .us0_awsize                (llpp_32_aw_bits_size   ),
      .us0_awvalid               (llpp_32_aw_valid       ),
      .us0_bid                   ({nc7_0,llpp_32_b_bits_id}      ),
      .us0_bready                (llpp_32_b_ready        ),
      .us0_bresp                 (llpp_32_b_bits_resp    ),
      .us0_bvalid                (llpp_32_b_valid        ),
      .us0_rdata                 (llpp_32_r_bits_data    ),
      .us0_rid                   ({nc7_1,llpp_32_r_bits_id}      ),
      .us0_rlast                 (llpp_32_r_bits_last    ),
      .us0_rready                (llpp_32_r_ready        ),
      .us0_rresp                 (llpp_32_r_bits_resp    ),
      .us0_rvalid                (llpp_32_r_valid        ),
      .us0_wdata                 (llpp_32_w_bits_data    ),
      .us0_wlast                 (llpp_32_w_bits_last    ),
      .us0_wready                (llpp_32_w_ready        ),
      .us0_wstrb                 (llpp_32_w_bits_strb    ),
      .us0_wvalid                (llpp_32_w_valid        ),

      .us1_araddr                (axi2llpp_32_ar_addr   ),
      .us1_arburst               (axi2llpp_32_ar_burst  ),
      .us1_arcache               (axi2llpp_32_ar_cache  ),
      .us1_arid                  (axi2llpp_32_ar_id     ),
      .us1_arlen                 (axi2llpp_32_ar_len    ),
      .us1_arlock                (axi2llpp_32_ar_lock   ),
      .us1_arprot                (axi2llpp_32_ar_prot   ),
      .us1_arready               (axi2llpp_32_ar_ready  ),
      .us1_arsize                (axi2llpp_32_ar_size   ),
      .us1_arvalid               (axi2llpp_32_ar_valid  ),
      .us1_awaddr                (axi2llpp_32_aw_addr   ),
      .us1_awburst               (axi2llpp_32_aw_burst  ),
      .us1_awcache               (axi2llpp_32_aw_cache  ),
      .us1_awid                  (axi2llpp_32_aw_id     ),
      .us1_awlen                 (axi2llpp_32_aw_len    ),
      .us1_awlock                (axi2llpp_32_aw_lock   ),
      .us1_awprot                (axi2llpp_32_aw_prot   ),
      .us1_awready               (axi2llpp_32_aw_ready  ),
      .us1_awsize                (axi2llpp_32_aw_size   ),
      .us1_awvalid               (axi2llpp_32_aw_valid  ),
      .us1_bid                   (axi2llpp_32_b_id      ),
      .us1_bready                (axi2llpp_32_b_ready   ),
      .us1_bresp                 (axi2llpp_32_b_resp    ),
      .us1_bvalid                (axi2llpp_32_b_valid   ),
      .us1_rdata                 (axi2llpp_32_r_data    ),
      .us1_rid                   (axi2llpp_32_r_id      ),
      .us1_rlast                 (axi2llpp_32_r_last    ),
      .us1_rready                (axi2llpp_32_r_ready   ),
      .us1_rresp                 (axi2llpp_32_r_resp    ),
      .us1_rvalid                (axi2llpp_32_r_valid   ),
      .us1_wdata                 (axi2llpp_32_w_data    ),
      .us1_wlast                 (axi2llpp_32_w_last    ),
      .us1_wready                (axi2llpp_32_w_ready   ),
      .us1_wstrb                 (axi2llpp_32_w_strb    ),
      .us1_wvalid                (axi2llpp_32_w_valid   ),

      .ds1_araddr               (llpp2ahb_ar_bits_addr   ),
      .ds1_arburst              (llpp2ahb_ar_bits_burst  ),
      .ds1_arcache              (llpp2ahb_ar_bits_cache  ),
      .ds1_arid                 (llpp2ahb_ar_bits_id     ),
      .ds1_arlen                (llpp2ahb_ar_bits_len    ),
      .ds1_arlock               (llpp2ahb_ar_bits_lock   ),
      .ds1_arprot               (llpp2ahb_ar_bits_prot   ),
      .ds1_arready              (llpp2ahb_ar_ready       ),
      .ds1_arsize               (llpp2ahb_ar_bits_size   ),
      .ds1_arvalid              (llpp2ahb_ar_valid       ),
      .ds1_awaddr               (llpp2ahb_aw_bits_addr   ),
      .ds1_awburst              (llpp2ahb_aw_bits_burst  ),
      .ds1_awcache              (llpp2ahb_aw_bits_cache  ),
      .ds1_awid                 (llpp2ahb_aw_bits_id     ),
      .ds1_awlen                (llpp2ahb_aw_bits_len    ),
      .ds1_awlock               (llpp2ahb_aw_bits_lock   ),
      .ds1_awprot               (llpp2ahb_aw_bits_prot   ),
      .ds1_awready              (llpp2ahb_aw_ready       ),
      .ds1_awsize               (llpp2ahb_aw_bits_size   ),
      .ds1_awvalid              (llpp2ahb_aw_valid       ),
      .ds1_bid                  (llpp2ahb_b_bits_id      ),
      .ds1_bready               (llpp2ahb_b_ready        ),
      .ds1_bresp                (llpp2ahb_b_bits_resp    ),
      .ds1_bvalid               (llpp2ahb_b_valid        ),
      .ds1_rdata                (llpp2ahb_r_bits_data    ),
      .ds1_rid                  (llpp2ahb_r_bits_id      ),
      .ds1_rlast                (llpp2ahb_r_bits_last    ),
      .ds1_rready               (llpp2ahb_r_ready        ),
      .ds1_rresp                (llpp2ahb_r_bits_resp    ),
      .ds1_rvalid               (llpp2ahb_r_valid        ),
      .ds1_wdata                (llpp2ahb_w_bits_data    ),
      .ds1_wlast                (llpp2ahb_w_bits_last    ),
      .ds1_wready               (llpp2ahb_w_ready        ),
      .ds1_wstrb                (llpp2ahb_w_bits_strb    ),
      .ds1_wvalid               (llpp2ahb_w_valid        ),

      .aclk                     (sys_clk),
      .aresetn                  (ndmreset_n)
);


axi2ahb200 #(
    .ID_WIDTH  (12),
    .ADDR_WIDTH  (32),
    .DATA_WIDTH  (32),
    .CMD_FIFO_DEPTH  (8),
    .RDATA_FIFO_DEPTH  (8),
    .BRESP_FIFO_DEPTH  (8),
    .WDATA_FIFO_DEPTH  (8)
  ) u_axi2ahb_llpp (
        .araddr    (llpp2ahb_ar_bits_addr[31:0]    ),
        .arburst   (llpp2ahb_ar_bits_burst   ),
        .arcache   (llpp2ahb_ar_bits_cache   ),
        .arid      (llpp2ahb_ar_bits_id      ),
        .arlen     (llpp2ahb_ar_bits_len     ),
        .arlock    (llpp2ahb_ar_bits_lock    ),
        .arprot    (llpp2ahb_ar_bits_prot    ),
        .arready   (llpp2ahb_ar_ready        ),
        .arsize    (llpp2ahb_ar_bits_size    ),
        .arvalid   (llpp2ahb_ar_valid        ),
        .awaddr    (llpp2ahb_aw_bits_addr[31:0]    ),
        .awburst   (llpp2ahb_aw_bits_burst   ),
        .awcache   (llpp2ahb_aw_bits_cache   ),
        .awid      (llpp2ahb_aw_bits_id      ),
        .awlen     (llpp2ahb_aw_bits_len     ),
        .awlock    (llpp2ahb_aw_bits_lock    ),
        .awprot    (llpp2ahb_aw_bits_prot    ),
        .awready   (llpp2ahb_aw_ready        ),
        .awsize    (llpp2ahb_aw_bits_size    ),
        .awvalid   (llpp2ahb_aw_valid        ),
        .bid       (llpp2ahb_b_bits_id       ),
        .bready    (llpp2ahb_b_ready         ),
        .bresp     (llpp2ahb_b_bits_resp     ),
        .bvalid    (llpp2ahb_b_valid         ),
        .rdata     (llpp2ahb_r_bits_data     ),
        .rid       (llpp2ahb_r_bits_id       ),
        .rlast     (llpp2ahb_r_bits_last     ),
        .rready    (llpp2ahb_r_ready         ),
        .rresp     (llpp2ahb_r_bits_resp     ),
        .rvalid    (llpp2ahb_r_valid         ),
        .wdata     (llpp2ahb_w_bits_data     ),
        .wlast     (llpp2ahb_w_bits_last     ),
        .wready    (llpp2ahb_w_ready         ),
        .wstrb     (llpp2ahb_w_bits_strb     ),
        .wvalid    (llpp2ahb_w_valid         ),
//        .wid       (llpp2ahb_w_bits_id       ),//nc
        .hclk      (sys_clk               ),
        .hresetn   (ndmreset_n            ),
        .aclk      (sys_clk               ),
        .aresetn   (ndmreset_n            ),
        .haddr     (ahbbrg_haddr     ),
        .hburst    (ahbbrg_hburst    ),
        .hprot     (ahbbrg_hprot     ),
        .hrdata    (ahbbrg_hrdata    ),
        .hready    (ahbbrg_hready    ),
        .hresp     (ahbbrg_hresp     ),
        .hsize     (ahbbrg_hsize     ),
        .htrans    (ahbbrg_htrans    ),
        .hwdata    (ahbbrg_hwdata    ),
        .hwrite    (ahbbrg_hwrite    )
);



busdec200_llpp u_busdec200_llpp(
	  .ds1_hsel       (w_apb_hsel),
	  .ds1_hrdata     (w_apb_hrdata),
	  .ds1_hreadyout  (w_apb_hready),
	  .ds1_hresp      (w_apb_hresp),
	  .ds2_hsel       (w_sdio_hsel),
	  .ds2_hrdata     (w_sdio_hrdata),
	  .ds2_hreadyout  (w_sdio_hready),
	  .ds2_hresp      (w_sdio_hresp),
	  .hclk           (sys_clk              ),
	  .hresetn        (ndmreset_n            ),
	  .us_haddr       (ahbbrg_haddr[23:0]),
	  .us_hsel        (1'b1                   ),
	  .us_htrans      (ahbbrg_htrans     ),
	  .us_hrdata      (ahbbrg_hrdata     ),
	  .us_hready      (ahbbrg_hready     ),
	  .us_hreadyout   (ahbbrg_hready     ),
	  .us_hresp       (ahbbrg_hresp      ),
	  .ds_hready      (ahbbrg_hreadyin        )
);

  assign w_apb_haddr  = ahbbrg_haddr;
  assign w_apb_hburst = ahbbrg_hburst;
  assign w_apb_hprot  = ahbbrg_hprot;
  assign w_apb_hsize  = ahbbrg_hsize;
  assign w_apb_htrans = ahbbrg_htrans;
  assign w_apb_hwdata = ahbbrg_hwdata;
  assign w_apb_hwrite = ahbbrg_hwrite;
  assign w_apb_hreadyin = ahbbrg_hreadyin;

  assign w_sdio_haddr  = ahbbrg_haddr;
  assign w_sdio_hburst = ahbbrg_hburst;
  assign w_sdio_hprot  = ahbbrg_hprot;
  assign w_sdio_hsize  = ahbbrg_hsize;
  assign w_sdio_htrans = ahbbrg_htrans;
  assign w_sdio_hwdata = ahbbrg_hwdata;
  assign w_sdio_hwrite = ahbbrg_hwrite;
  assign w_sdio_hreadyin = ahbbrg_hreadyin;

  assign w_sdio_hrdata = 32'b0;
  assign w_sdio_hready = 1'b0;
  assign w_sdio_hresp  = 1'b0;

apbbrg100 u_apb_brg(
	  .ps1_psel      (w_sys_psel  ),
	  .ps1_prdata    (w_sys_prdata),
	  .ps1_pready    (1'b1        ),
	  .ps1_pslverr   (1'b0        ),

	  .ps2_psel      (w_dma_psel  ),
	  .ps2_prdata    (w_dma_prdata),
	  .ps2_pready    (1'b1        ),
	  .ps2_pslverr   (1'b0        ),

	  .ps3_psel      (w_spi_psel  ),
	  .ps3_prdata    (w_spi_prdata),
	  .ps3_pready    (1'b1        ),
	  .ps3_pslverr   (1'b0        ),

	  .ps4_psel      (w_iic_psel  ),
	  .ps4_prdata    (w_iic_prdata),
	  .ps4_pready    (1'b1        ),
	  .ps4_pslverr   (1'b0        ),

	  .ps5_psel      (w_spi_iis_psel  ),
	  .ps5_prdata    (w_spi_iis_prdata),
	  .ps5_pready    (1'b1        ),
	  .ps5_pslverr   (1'b0        ),

	  .ps6_psel      (w_wdt_psel  ),
	  .ps6_prdata    (w_wdt_prdata),
	  .ps6_pready    (1'b1        ),
	  .ps6_pslverr   (1'b0        ),

	  .ps7_psel      (w_timer_psel  ),
	  .ps7_prdata    (w_timer_prdata),
	  .ps7_pready    (1'b1        ),
	  .ps7_pslverr   (1'b0        ),

	  .ps8_psel      (w_rtc_psel  ),
	  .ps8_prdata    (w_rtc_prdata),
	  .ps8_pready    (1'b1        ),
	  .ps8_pslverr   (1'b0        ),

	  .ps9_psel      (w_analog_psel  ),
	  .ps9_prdata    (w_analog_prdata),
	  .ps9_pready    (1'b1        ),
	  .ps9_pslverr   (1'b0        ),

	  .ps10_psel     (w_uart_psel  ),
	  .ps10_prdata   (w_uart_prdata),
	  .ps10_pready   (1'b1        ),
	  .ps10_pslverr  (1'b0        ),

	  .ps11_psel     (w_gpio_psel  ),
	  .ps11_prdata   (w_gpio_prdata),
	  .ps11_pready   (1'b1        ),
	  .ps11_pslverr  (1'b0        ),

	  .hclk          (sys_clk),
	  .hresetn       (ndmreset_n),
	  .hsel          (w_apb_hsel),
	  .hready_in     (w_apb_hreadyin),
	  .htrans        (w_apb_htrans),
	  .haddr         (w_apb_haddr),
	  .hsize         (w_apb_hsize),
	  .hprot         (w_apb_hprot),
	  .hwrite        (w_apb_hwrite),
	  .hwdata        (w_apb_hwdata),
	  .apb2ahb_clken (1'b1),
	  .hrdata        (w_apb_hrdata),
	  .hready        (w_apb_hready),
	  .hresp         (w_apb_hresp),

	  .pclk          (w_sys_pclk),
	  .presetn       (ndmreset_n),
	  .pprot         (w_pprot),
	  .pstrb         (w_pstrb),
	  .paddr         (w_paddr),
	  .penable       (w_penable),
	  .pwrite        (w_pwrite),
	  .pwdata        (w_pwdata)
);

assign w_iic_prdata = 32'b0;
assign w_spi_iis_prdata = 32'b0;
assign w_wdt_prdata = 32'b0;
assign w_rtc_prdata = 32'b0;
assign w_analog_prdata = 32'b0;


assign w_sys_penable = w_penable;
assign w_sys_pwrite  = w_pwrite;
assign w_sys_paddr   = w_paddr;
assign w_sys_pwdata  = w_pwdata;

assign w_dma_penable = w_penable;
assign w_dma_pwrite  = w_pwrite;
assign w_dma_paddr   = w_paddr;
assign w_dma_pwdata  = w_pwdata;

assign w_spi_penable = w_penable;
assign w_spi_pwrite  = w_pwrite;
assign w_spi_paddr   = w_paddr;
assign w_spi_pwdata  = w_pwdata;

assign w_timer_penable = w_penable;
assign w_timer_pwrite  = w_pwrite;
assign w_timer_paddr   = w_paddr;
assign w_timer_pwdata  = w_pwdata;

assign w_uart_penable = w_penable;
assign w_uart_pwrite  = w_pwrite;
assign w_uart_paddr   = w_paddr;
assign w_uart_pwdata  = w_pwdata;

assign w_gpio_penable = w_penable;
assign w_gpio_pwrite  = w_pwrite;
assign w_gpio_paddr   = w_paddr;
assign w_gpio_pwdata  = w_pwdata;





gpio100 u_gpio(
  .gpio_intr      (w_gpio_int),
  .gpio_oe        (w_gpio_oe),
  .gpio_out       (w_gpio_o ),
  .paddr          (w_gpio_paddr[7:0]),
  .penable        (w_gpio_penable),
  .prdata         (w_gpio_prdata),
  .psel           (w_gpio_psel),
  .pwdata         (w_gpio_pwdata),
  .pwrite         (w_gpio_pwrite),
  .pclk           (w_gpio_pclk),
  .presetn        (w_gpio_prst_n),
  .extclk         (w_gpio_extclk),
  .gpio_in        (w_gpio_i )
);



assign w_dma_axim_w_bits_id = 'b0;


logic w_dma_pready;
logic w_dma_pslverr;
logic [15:0] w_dma_ack;
logic [15:0] w_dma_req;
assign w_dma_req = 16'b0;

assign w_dma_axim_aw_bits_id[3] = 'b0;
assign w_dma_axim_ar_bits_id[3] = 'b0;

dmac300 u_dma_top(
        .paddr               ({15'b0,w_dma_paddr}      ),
        .penable             (w_dma_penable    ),
        .prdata              (w_dma_prdata     ),
        .pready              (w_dma_pready     ),//nc
        .psel                (w_dma_psel       ),
        .pslverr             (w_dma_pslverr    ),//nc
        .pwdata              (w_dma_pwdata     ),
        .pwrite              (w_dma_pwrite     ),
        .pclk                (w_dma_pclk       ),
        .presetn             (w_dma_prst_n     ),
        .m0_araddr           (w_dma_axim_ar_bits_addr ),
        .m0_arburst          (w_dma_axim_ar_bits_burst),
        .m0_arcache          (w_dma_axim_ar_bits_cache),
        .m0_arid             (w_dma_axim_ar_bits_id[2:0]   ),
        .m0_arlen            (w_dma_axim_ar_bits_len  ),
        .m0_arlock           (w_dma_axim_ar_bits_lock ),
        .m0_arprot           (w_dma_axim_ar_bits_prot ),
        .m0_arready          (w_dma_axim_ar_ready     ),
        .m0_arsize           (w_dma_axim_ar_bits_size ),
        .m0_arvalid          (w_dma_axim_ar_valid     ),
        .m0_awaddr           (w_dma_axim_aw_bits_addr ),
        .m0_awburst          (w_dma_axim_aw_bits_burst),
        .m0_awcache          (w_dma_axim_aw_bits_cache),
        .m0_awid             (w_dma_axim_aw_bits_id[2:0]   ),
        .m0_awlen            (w_dma_axim_aw_bits_len  ),
        .m0_awlock           (w_dma_axim_aw_bits_lock ),
        .m0_awprot           (w_dma_axim_aw_bits_prot ),
        .m0_awready          (w_dma_axim_aw_ready     ),
        .m0_awsize           (w_dma_axim_aw_bits_size ),
        .m0_awvalid          (w_dma_axim_aw_valid     ),
        .m0_bid              (w_dma_axim_b_bits_id[2:0]),
        .m0_bready           (w_dma_axim_b_ready      ),
        .m0_bresp            (w_dma_axim_b_bits_resp  ),
        .m0_bvalid           (w_dma_axim_b_valid      ),
        .m0_rdata            (w_dma_axim_r_bits_data  ),
        .m0_rid              (w_dma_axim_r_bits_id[2:0]),
        .m0_rlast            (w_dma_axim_r_bits_last  ),
        .m0_rready           (w_dma_axim_r_ready      ),
        .m0_rresp            (w_dma_axim_r_bits_resp  ),
        .m0_rvalid           (w_dma_axim_r_valid      ),
        .m0_wdata            (w_dma_axim_w_bits_data  ),
        .m0_wlast            (w_dma_axim_w_bits_last  ),
        .m0_wready           (w_dma_axim_w_ready      ),
        .m0_wstrb            (w_dma_axim_w_bits_strb  ),
        .m0_wvalid           (w_dma_axim_w_valid      ),
        .aclk                (w_dma_clk),
        .aresetn             (w_dma_rst_n),
        .dma_ack             (w_dma_ack),//TODO
        .dma_req             (w_dma_req),//TODO
        .dma_int             (w_dma_int)
);


assign w_spi_rx_dma_ack = 'b0;
assign w_spi_tx_dma_ack = 'b0;

spi200 u_spi200 (
  .pclk               (w_spi_pclk),
  .presetn            (w_spi_prst_n),
  .paddr              ({15'b0,w_spi_paddr}),
  .penable            (w_spi_penable),
  .prdata             (w_spi_prdata),
  .pready             (w_spi_pready),
  .psel               (w_spi_psel),
  .pwdata             (w_spi_pwdata),
  .pwrite             (w_spi_pwrite),
  .spi_clock          (w_spi_pclk),
  .spi_rstn           (w_spi_prst_n),
  .spi_boot_intr      (w_spi_int),
  .spi_default_mode3  (1'b0),
  .spi_rx_dma_ack     (w_spi_rx_dma_ack),//TODO
  .spi_rx_dma_req     (w_spi_rx_dma_req),//TODO
  .spi_tx_dma_ack     (w_spi_tx_dma_ack),//TODO
  .spi_tx_dma_req     (w_spi_tx_dma_req),//TODO
  .scan_enable        (1'b0),
  .scan_test          (1'b0),
  .spi_clk_in         (w_spi_clk_i   ),
  .spi_clk_oe         (w_spi_clk_oe  ),
  .spi_clk_out        (w_spi_clk_o   ),
  .spi_cs_n_oe        (w_spi_cs_n_oe ),
  .spi_cs_n_out       (w_spi_cs_n_o  ),
  .spi_miso_in        (w_spi_miso_i  ),
  .spi_miso_oe        (w_spi_miso_oe ),
  .spi_miso_out       (w_spi_miso_o  ),
  .spi_mosi_in        (w_spi_mosi_i  ),
  .spi_mosi_oe        (w_spi_mosi_oe ),
  .spi_mosi_out       (w_spi_mosi_o  )
);

/*
pit100 u_timer(
  .extclk    (w_timer_extclk),
  .paddr     (w_timer_paddr[6:2]),
  .pclk      (w_timer_pclk),
  .penable   (w_timer_penable),
  .pit_pause (1'b0),//TODO
  .presetn   (w_timer_prst_n),
  .psel      (w_timer_psel),
  .pwdata    (w_timer_pwdata),
  .pwrite    (w_timer_pwrite),
  .ch0_pwm   (),//nc
  .ch0_pwmoe (),//nc
  .pit_intr  (w_pit_int),//TODO
  .prdata    (w_timer_prdata)
);
*/
assign w_timer_prdata = 32'b0;
assign w_pit_int = 1'b0;

assign w_dma_rx_ack = 1'b0;
assign w_dma_tx_ack = 1'b0;

uart100 u_uart(
  .dma_rx_ack  (w_dma_rx_ack),//TODO
  .dma_tx_ack  (w_dma_tx_ack),//TODO
  .paddr       (w_uart_paddr[5:2]),
  .pclk        (w_uart_pclk),
  .penable     (w_uart_penable),
  .presetn     (w_uart_prst_n),
  .psel        (w_uart_psel),
  .pwdata      (w_uart_pwdata),
  .pwrite      (w_uart_pwrite),
  .prdata      (w_uart_prdata),
  .uart_ctsn   (1'b1),//i_uart_ctsn),
  .uart_dcdn   (1'b1),//i_uart_dcdn),
  .uart_dsrn   (1'b1),//i_uart_dsrn),
  .uart_rin    (1'b1),//i_uart_rin ),
  .uart_sin    (i_uart_rx),
  .dma_rx_req  (w_dma_rx_req),//TODO
  .dma_tx_req  (w_dma_tx_req),//TODO
  .uart_dtrn   (),//o_uart_dtrn),
  .uart_intr   (w_uart_int),
  .uart_out1n  (),//o_uart_out1n),
  .uart_out2n  (),//o_uart_out2n),
  .uart_rtsn   (),//o_uart_rtsn ),
  .uart_sout   (o_uart_tx)
);

sysctrl #(.CORE0_RESET_RELEASE_DEFAULT(!COSIM)) u_sysctrl(
  .i_sys_clk           ( sys_clk        ),
  .i_rst_n             ( ndmreset_n      ),

  .i_xrst_n            (1'b1           ),
  .i_bor_n             (1'b1           ),
  .i_por_n             (1'b1           ),
  .i_wdtrst            (1'b1           ),

  .i_apb_paddr         (w_sys_paddr[11:0] ),
  .i_apb_penable       (w_sys_penable     ),
  .o_apb_prdata        (w_sys_prdata      ),
  .i_apb_psel          (w_sys_psel        ),
  .i_apb_pwdata        (w_sys_pwdata      ),
  .i_apb_pwrite        (w_sys_pwrite      ),

  .i_xtal_clk          (1'b1              ),

  .o_sys_clk           (w_sys_clk         ),
  .o_sys_rst_n         (w_sys_rst_n       ),

  .o_core0_clk         (w_core0_clk       ),
  .o_core1_clk         (w_core1_clk       ),
  .o_core2_clk         (w_core2_clk       ),
  .o_core3_clk         (w_core3_clk       ),
  .o_dpu_clk           (w_dpu_clk         ),
  .o_dma_clk           (w_dma_clk         ),
  .o_uart_clk          (w_uart_clk        ),
  .o_sdio_clk          (w_sdio_clk        ),
  .o_hsm_clk           (w_hsm_clk         ),

  .o_sys_pclk          (w_sys_pclk        ),
  .o_spi_pclk          (w_spi_pclk        ),
  .o_iic_pclk          (w_iic_pclk        ),
  .o_iis_pclk          (w_iis_pclk        ),
  .o_wdt_pclk          (w_wdt_pclk        ),
  .o_wdt_extclk        (w_wdt_extclk      ),
  .o_timer_pclk        (w_timer_pclk      ),
  .o_timer_extclk      (w_timer_extclk    ),
  .o_rtc_pclk          (w_rtc_pclk        ),
  .o_rtc_extclk        (w_rtc_extclk      ),
  .o_analog_pclk       (w_analog_pclk     ),
  .o_dma_pclk          (w_dma_pclk        ),
  .o_uart_pclk         (w_uart_pclk       ),
  .o_gpio_pclk         (w_gpio_pclk       ),
  .o_gpio_extclk       (w_gpio_extclk     ),


  .o_core0_rst_n       (w_core0_rst_n     ),
  .o_core1_rst_n       (w_core1_rst_n     ),
  .o_core2_rst_n       (w_core2_rst_n     ),
  .o_core3_rst_n       (w_core3_rst_n     ),
  .o_dpu_rst_n         (w_dpu_rst_n       ),
  .o_dma_rst_n         (w_dma_rst_n       ),
  .o_uart_rst_n        (w_uart_rst_n      ),
  .o_sdio_rst_n        (w_sdio_rst_n      ),
  .o_hsm_rst_n         (w_hsm_rst_n       ),
  .o_sys_prst_n        (w_sys_prst_n      ),
  .o_spi_prst_n        (w_spi_prst_n      ),
  .o_iic_prst_n        (w_iic_prst_n      ),
  .o_iis_prst_n        (w_iis_prst_n      ),
  .o_wdt_prst_n        (w_wdt_prst_n      ),
  .o_timer_prst_n      (w_timer_prst_n    ),
  .o_timer_extrst_n    (w_timer_extrst_n  ),
  .o_rtc_prst_n        (w_rtc_prst_n      ),
  .o_rtc_extrst_n      (w_rtc_extrst_n    ),
  .o_analog_prst_n     (w_analog_prst_n   ),
  .o_dma_prst_n        (w_dma_prst_n      ),
  .o_uart_prst_n       (w_uart_prst_n     ),
  .o_gpio_prst_n       (w_gpio_prst_n     ),


  .o_vref1p65_vsel_lv  (),
  .o_reg_loadsel_lv    (),
  .o_vrefp25_vsel_lv   (),
  .o_ldo_pd_lv         (),
  .o_reg_ldo_lv        (),
  .o_bor_en_lv         (),
  .o_reg_bor_lv        (),
  .o_clk_chop_lv       (),
  .o_vref12_en_lv      (),
  .o_vref1p65_en_lv    (),
  .o_vrefp25_en_lv     (),
  .o_hqbgp_tempadj_lv  (),
  .o_hqbgp_ctrim_lv    (),
  .o_hqbgp_ftrim_lv    (),

  .o_core0_boot_addr       (w_core0_boot_addr      ),
  .o_core0_itcm_startAddr  (w_core0_itcm_startAddr ),
  .o_core0_itcm_size       (w_core0_itcm_size      ),
  .o_core0_dtcm_startAddr  (w_core0_dtcm_startAddr ),
  .o_core0_dtcm_size       (w_core0_dtcm_size      ),
  .o_core0_llpp_startAddr  (w_core0_llpp_startAddr ),
  .o_core0_llpp_size       (w_core0_llpp_size      ),
  .o_core0_flash_startAddr (w_core0_flash_startAddr),
  .o_core0_flash_size      (w_core0_flash_size     ),
  .o_core1_boot_addr       (w_core1_boot_addr      ),
  .o_core1_itcm_startAddr  (w_core1_itcm_startAddr ),
  .o_core1_itcm_size       (w_core1_itcm_size      ),
  .o_core1_dtcm_startAddr  (w_core1_dtcm_startAddr ),
  .o_core1_dtcm_size       (w_core1_dtcm_size      ),
  .o_core1_llpp_startAddr  (w_core1_llpp_startAddr ),
  .o_core1_llpp_size       (w_core1_llpp_size      ),
  .o_core1_flash_startAddr (w_core1_flash_startAddr),
  .o_core1_flash_size      (w_core1_flash_size     ),
  .o_core2_boot_addr       (w_core2_boot_addr      ),
  .o_core2_itcm_startAddr  (w_core2_itcm_startAddr ),
  .o_core2_itcm_size       (w_core2_itcm_size      ),
  .o_core2_dtcm_startAddr  (w_core2_dtcm_startAddr ),
  .o_core2_dtcm_size       (w_core2_dtcm_size      ),
  .o_core2_llpp_startAddr  (w_core2_llpp_startAddr ),
  .o_core2_llpp_size       (w_core2_llpp_size      ),
  .o_core2_flash_startAddr (w_core2_flash_startAddr),
  .o_core2_flash_size      (w_core2_flash_size     ),
  .o_core3_boot_addr       (w_core3_boot_addr      ),
  .o_core3_itcm_startAddr  (w_core3_itcm_startAddr ),
  .o_core3_itcm_size       (w_core3_itcm_size      ),
  .o_core3_dtcm_startAddr  (w_core3_dtcm_startAddr ),
  .o_core3_dtcm_size       (w_core3_dtcm_size      ),
  .o_core3_llpp_startAddr  (w_core3_llpp_startAddr ),
  .o_core3_llpp_size       (w_core3_llpp_size      ),
  .o_core3_flash_startAddr (w_core3_flash_startAddr),
  .o_core3_flash_size      (w_core3_flash_size     )

);


////////////////////   IO   ////////////////////

endmodule
