`include "sysctrl_config.vh"

module sysctrl #(
          parameter bit CORE0_RESET_RELEASE_DEFAULT = 1'b1
) (
          i_sys_clk,
          i_rst_n,

          i_xrst_n,
          i_bor_n,
          i_por_n,
          i_wdtrst,

          i_apb_paddr,
          i_apb_penable,
          o_apb_prdata,
          i_apb_psel,
          i_apb_pwdata,
          i_apb_pwrite,

          i_xtal_clk,

          o_sys_clk,
          o_sys_rst_n,

          o_core0_clk,
          o_core1_clk,
          o_core2_clk,
          o_core3_clk,
          o_dpu_clk,
          o_dma_clk,
          o_uart_clk,
          o_sdio_clk,
          o_hsm_clk,

          o_sys_pclk,
          o_spi_pclk,
          o_iic_pclk,
          o_iis_pclk,
          o_wdt_pclk,
          o_wdt_extclk,
          o_timer_pclk,
          o_timer_extclk,
          o_rtc_pclk,
          o_rtc_extclk,
          o_analog_pclk,
          o_dma_pclk,
          o_uart_pclk,
          o_gpio_pclk,
          o_gpio_extclk,



          o_core0_rst_n,
          o_core1_rst_n,
          o_core2_rst_n,
          o_core3_rst_n,
          o_dpu_rst_n,
          o_dma_rst_n,
          o_uart_rst_n,
          o_sdio_rst_n,
          o_hsm_rst_n,
          o_sys_prst_n,
          o_spi_prst_n,
          o_iic_prst_n,
          o_iis_prst_n,
          o_wdt_prst_n,
          o_timer_prst_n,
          o_timer_extrst_n,
          o_rtc_prst_n,
          o_rtc_extrst_n,
          o_analog_prst_n,
          o_dma_prst_n,
          o_uart_prst_n,
          o_gpio_prst_n,


          o_vref1p65_vsel_lv,
          o_reg_loadsel_lv,
          o_vrefp25_vsel_lv,
          o_ldo_pd_lv,
          o_reg_ldo_lv,
          o_bor_en_lv,
          o_reg_bor_lv,
          o_clk_chop_lv,
          o_vref12_en_lv,
          o_vref1p65_en_lv,
          o_vrefp25_en_lv,
          o_hqbgp_tempadj_lv,
          o_hqbgp_ctrim_lv,
          o_hqbgp_ftrim_lv,

          o_core0_boot_addr,
          o_core0_itcm_startAddr,
          o_core0_itcm_size,
          o_core0_dtcm_startAddr,
          o_core0_dtcm_size,
          o_core0_llpp_startAddr,
          o_core0_llpp_size,
          o_core0_flash_startAddr,
          o_core0_flash_size,
          o_core1_boot_addr,
          o_core1_itcm_startAddr,
          o_core1_itcm_size,
          o_core1_dtcm_startAddr,
          o_core1_dtcm_size,
          o_core1_llpp_startAddr,
          o_core1_llpp_size,
          o_core1_flash_startAddr,
          o_core1_flash_size,
          o_core2_boot_addr,
          o_core2_itcm_startAddr,
          o_core2_itcm_size,
          o_core2_dtcm_startAddr,
          o_core2_dtcm_size,
          o_core2_llpp_startAddr,
          o_core2_llpp_size,
          o_core2_flash_startAddr,
          o_core2_flash_size,
          o_core3_boot_addr,
          o_core3_itcm_startAddr,
          o_core3_itcm_size,
          o_core3_dtcm_startAddr,
          o_core3_dtcm_size,
          o_core3_llpp_startAddr,
          o_core3_llpp_size,
          o_core3_flash_startAddr,
          o_core3_flash_size

);

input              i_sys_clk;
input              i_rst_n;

input              i_xrst_n;
input              i_bor_n;
input              i_por_n;
input              i_wdtrst;

input       [11:0] i_apb_paddr;
input              i_apb_penable;
output      [31:0] o_apb_prdata;
input              i_apb_psel;
input       [31:0] i_apb_pwdata;
input              i_apb_pwrite;

input              i_xtal_clk;

output      o_sys_clk;
output      o_sys_rst_n;

output      o_core0_clk;
output      o_core1_clk;
output      o_core2_clk;
output      o_core3_clk;
output      o_dpu_clk;
output      o_dma_clk;
output      o_uart_clk;
output      o_sdio_clk;
output      o_hsm_clk;

output      o_sys_pclk;
output      o_spi_pclk;
output      o_iic_pclk;
output      o_iis_pclk;
output      o_wdt_pclk;
output      o_wdt_extclk;
output      o_timer_pclk;
output      o_timer_extclk;
output      o_rtc_pclk;
output      o_rtc_extclk;
output      o_analog_pclk;
output      o_dma_pclk;
output      o_uart_pclk;
output      o_gpio_pclk;
output      o_gpio_extclk;



output      o_core0_rst_n;
output      o_core1_rst_n;
output      o_core2_rst_n;
output      o_core3_rst_n;
output      o_dpu_rst_n;
output      o_dma_rst_n;
output      o_uart_rst_n;
output      o_sdio_rst_n;
output      o_hsm_rst_n;
output      o_sys_prst_n;
output      o_spi_prst_n;
output      o_iic_prst_n;
output      o_iis_prst_n;
output      o_wdt_prst_n;
output      o_timer_prst_n;
output      o_timer_extrst_n;
output      o_rtc_prst_n;
output      o_rtc_extrst_n;
output      o_analog_prst_n;
output      o_dma_prst_n;
output      o_uart_prst_n;
output      o_gpio_prst_n;

output  [2:0] o_vref1p65_vsel_lv;
output  [1:0] o_reg_loadsel_lv;
output  [2:0] o_vrefp25_vsel_lv;
output        o_ldo_pd_lv;
output  [3:0] o_reg_ldo_lv;
output        o_bor_en_lv;
output  [2:0] o_reg_bor_lv;
output        o_clk_chop_lv;
output        o_vref12_en_lv;
output        o_vref1p65_en_lv;
output        o_vrefp25_en_lv;
output  [3:0] o_hqbgp_tempadj_lv;
output  [3:0] o_hqbgp_ctrim_lv;
output  [5:0] o_hqbgp_ftrim_lv;

output [31:0] o_core0_boot_addr;
output [18:0] o_core0_itcm_startAddr;
output [4:0]  o_core0_itcm_size;
output [18:0] o_core0_dtcm_startAddr;
output [4:0]  o_core0_dtcm_size;
output [19:0] o_core0_llpp_startAddr;
output [4:0]  o_core0_llpp_size;
output [4:0]  o_core0_flash_startAddr;
output [4:0]  o_core0_flash_size;
output [31:0] o_core1_boot_addr;
output [18:0] o_core1_itcm_startAddr;
output [4:0]  o_core1_itcm_size;
output [18:0] o_core1_dtcm_startAddr;
output [4:0]  o_core1_dtcm_size;
output [19:0] o_core1_llpp_startAddr;
output [4:0]  o_core1_llpp_size;
output [4:0]  o_core1_flash_startAddr;
output [4:0]  o_core1_flash_size;
output [31:0] o_core2_boot_addr;
output [18:0] o_core2_itcm_startAddr;
output [4:0]  o_core2_itcm_size;
output [18:0] o_core2_dtcm_startAddr;
output [4:0]  o_core2_dtcm_size;
output [19:0] o_core2_llpp_startAddr;
output [4:0]  o_core2_llpp_size;
output [4:0]  o_core2_flash_startAddr;
output [4:0]  o_core2_flash_size;
output [31:0] o_core3_boot_addr;
output [18:0] o_core3_itcm_startAddr;
output [4:0]  o_core3_itcm_size;
output [18:0] o_core3_dtcm_startAddr;
output [4:0]  o_core3_dtcm_size;
output [19:0] o_core3_llpp_startAddr;
output [4:0]  o_core3_llpp_size;
output [4:0]  o_core3_flash_startAddr;
output [4:0]  o_core3_flash_size;



logic        w_osc1_clk_out;
logic [ 5:0] w_osc1_freq_ctune;
logic [ 5:0] w_osc1_freq_fune;
logic        w_osc1_trim_done;

logic        w_osc2_clk_out;
logic [ 5:0] w_osc2_freq_ctune;
logic [ 5:0] w_osc2_freq_fune;
logic        w_osc2_trim_done;

logic w_sys_clk;
logic w_lspdiv_ckout;
logic w_wdt_div_extckout;
logic w_timer_div_extckout;
logic w_rtc_div_ckout;
logic w_gpio_div_extckout;

logic [10:0] r_rst_cnt;
logic r_ckmux_rst_n;


logic w_apb_write,w_apb_read;

logic w_syspllctl0_en;
logic w_syspllctl1_en;
logic w_syspllctl2_en;
logic w_sysosc1ctl0_en;
logic w_sysosc1ctl1_en;
logic w_sysosc1ctl2_en;
logic w_sysosc2ctl0_en;
logic w_sysosc2ctl1_en;
logic w_sysosc2ctl2_en;
logic w_syspllsts_en;
logic w_sysclkdivsel_en;
logic w_lospcp0_en;
logic w_lospcp1_en;
logic w_clkcr0_en;
logic w_clkcr1_en;
logic w_clkcr2_en;
logic w_clkcr3_en;
logic w_clksrcctl0_en;
logic w_clksrcctl1_en;

logic w_core0_boot_addr_en;
logic w_core0_itcm_ctl_en;
logic w_core0_dtcm_ctl_en;
logic w_core0_llpp_ctl_en;
logic w_core0_flash_ctl_en;
logic w_core0_ctl_en;
logic w_core1_boot_addr_en;
logic w_core1_itcm_ctl_en;
logic w_core1_dtcm_ctl_en;
logic w_core1_llpp_ctl_en;
logic w_core1_flash_ctl_en;
logic w_core1_ctl_en;
logic w_core2_boot_addr_en;
logic w_core2_itcm_ctl_en;
logic w_core2_dtcm_ctl_en;
logic w_core2_llpp_ctl_en;
logic w_core2_flash_ctl_en;
logic w_core2_ctl_en;
logic w_core3_boot_addr_en;
logic w_core3_itcm_ctl_en;
logic w_core3_dtcm_ctl_en;
logic w_core3_llpp_ctl_en;
logic w_core3_flash_ctl_en;
logic w_core3_ctl_en;


logic w_bor_n;
logic w_sys_rst_n;
logic w_rst_n;
logic w_wdtrst_n;
logic r_sys_rst_n;


assign w_apb_read  = ~i_apb_penable & i_apb_psel & ~i_apb_pwrite;



logic [31:0] r_apb_paddr,r_apb_paddr_d;
logic [31:0] r_apb_pwdata,r_apb_pwdata_d;
logic r_apb_write,r_apb_write_d,r_apb_write_dd;
always_ff @(posedge i_sys_clk or negedge i_rst_n) begin
    if (!i_rst_n) begin
        r_apb_paddr   <= 32'b0;
        r_apb_paddr_d <= 32'b0;
    end
    else begin
        r_apb_paddr   <= i_apb_paddr;
        r_apb_paddr_d <= r_apb_paddr;
    end
end

always_ff @(posedge i_sys_clk or negedge i_rst_n) begin
    if (!i_rst_n) begin
        r_apb_pwdata   <= 32'b0;
        r_apb_pwdata_d <= 32'b0;
    end
    else begin
        r_apb_pwdata   <= i_apb_pwdata;
        r_apb_pwdata_d <= r_apb_pwdata;
    end
end

always_ff @(posedge i_sys_clk or negedge i_rst_n) begin
    if (!i_rst_n) begin
        r_apb_write    <= 32'b0;
        r_apb_write_d  <= 32'b0;
        r_apb_write_dd <= 32'b0;
    end
    else begin
        r_apb_write    <= i_apb_penable & i_apb_psel &  i_apb_pwrite;
        r_apb_write_d  <= r_apb_write;
        r_apb_write_dd <= r_apb_write_d;
    end
end

assign w_apb_write = r_apb_write_d & ~r_apb_write_dd;


assign w_clksrcctl0_en   = r_apb_paddr_d[11:0] == `CLKSRCCTL0_OFFSET;
assign w_clksrcctl1_en   = r_apb_paddr_d[11:0] == `CLKSRCCTL1_OFFSET;
assign w_syspllctl0_en   = r_apb_paddr_d[11:0] == `SYSPLLCTL0_OFFSET;
assign w_syspllctl1_en   = r_apb_paddr_d[11:0] == `SYSPLLCTL1_OFFSET;
assign w_syspllctl2_en   = r_apb_paddr_d[11:0] == `SYSPLLCTL2_OFFSET;
assign w_sysosc1ctl0_en  = r_apb_paddr_d[11:0] == `SYSOSC1CTL0_OFFSET;
assign w_sysosc1ctl1_en  = r_apb_paddr_d[11:0] == `SYSOSC1CTL1_OFFSET;
assign w_sysosc1ctl2_en  = r_apb_paddr_d[11:0] == `SYSOSC1CTL2_OFFSET;
assign w_sysosc2ctl0_en  = r_apb_paddr_d[11:0] == `SYSOSC2CTL0_OFFSET;
assign w_sysosc2ctl1_en  = r_apb_paddr_d[11:0] == `SYSOSC2CTL1_OFFSET;
assign w_sysosc2ctl2_en  = r_apb_paddr_d[11:0] == `SYSOSC2CTL2_OFFSET;
assign w_syspllsts_en    = r_apb_paddr_d[11:0] == `SYSPLLSTS_OFFSET;
assign w_sysclkdivsel_en = r_apb_paddr_d[11:0] == `SYSCLKDIVSEL_OFFSET;
assign w_lospcp0_en      = r_apb_paddr_d[11:0] == `LOSPCP0_OFFSET;
assign w_lospcp1_en      = r_apb_paddr_d[11:0] == `LOSPCP1_OFFSET;
assign w_clkcr0_en       = r_apb_paddr_d[11:0] == `CLKCR0_OFFSET;
assign w_clkcr1_en       = r_apb_paddr_d[11:0] == `CLKCR1_OFFSET;
assign w_clkcr2_en       = r_apb_paddr_d[11:0] == `CLKCR2_OFFSET;
assign w_clkcr3_en       = r_apb_paddr_d[11:0] == `CLKCR3_OFFSET;
assign w_softpres0_en    = r_apb_paddr_d[11:0] == `SOFTPRES0_OFFSET;
assign w_softpres1_en    = r_apb_paddr_d[11:0] == `SOFTPRES1_OFFSET;
assign w_softpres2_en    = r_apb_paddr_d[11:0] == `SOFTPRES2_OFFSET;
assign w_softpres3_en    = r_apb_paddr_d[11:0] == `SOFTPRES3_OFFSET;

assign w_pmuctl0_en      = r_apb_paddr_d[11:0] == `PMUCTL0_OFFSET;
assign w_pmuctl1_en      = r_apb_paddr_d[11:0] == `PMUCTL1_OFFSET;

assign w_core0_boot_addr_en = r_apb_paddr_d[11:0] == `CORE0_BOOT_ADDR_OFFSET;
assign w_core0_itcm_ctl_en  = r_apb_paddr_d[11:0] == `CORE0_ITCM_CTL_OFFSET;
assign w_core0_dtcm_ctl_en  = r_apb_paddr_d[11:0] == `CORE0_DTCM_CTL_OFFSET;
assign w_core0_llpp_ctl_en  = r_apb_paddr_d[11:0] == `CORE0_LLPP_CTL_OFFSET;
assign w_core0_flash_ctl_en = r_apb_paddr_d[11:0] == `CORE0_FLASH_CTL_OFFSET;
assign w_core0_ctl_en       = r_apb_paddr_d[11:0] == `CORE0_CTL_OFFSET;
assign w_core1_boot_addr_en = r_apb_paddr_d[11:0] == `CORE1_BOOT_ADDR_OFFSET;
assign w_core1_itcm_ctl_en  = r_apb_paddr_d[11:0] == `CORE1_ITCM_CTL_OFFSET;
assign w_core1_dtcm_ctl_en  = r_apb_paddr_d[11:0] == `CORE1_DTCM_CTL_OFFSET;
assign w_core1_llpp_ctl_en  = r_apb_paddr_d[11:0] == `CORE1_LLPP_CTL_OFFSET;
assign w_core1_flash_ctl_en = r_apb_paddr_d[11:0] == `CORE1_FLASH_CTL_OFFSET;
assign w_core1_ctl_en       = r_apb_paddr_d[11:0] == `CORE1_CTL_OFFSET;
assign w_core2_boot_addr_en = r_apb_paddr_d[11:0] == `CORE2_BOOT_ADDR_OFFSET;
assign w_core2_itcm_ctl_en  = r_apb_paddr_d[11:0] == `CORE2_ITCM_CTL_OFFSET;
assign w_core2_dtcm_ctl_en  = r_apb_paddr_d[11:0] == `CORE2_DTCM_CTL_OFFSET;
assign w_core2_llpp_ctl_en  = r_apb_paddr_d[11:0] == `CORE2_LLPP_CTL_OFFSET;
assign w_core2_flash_ctl_en = r_apb_paddr_d[11:0] == `CORE2_FLASH_CTL_OFFSET;
assign w_core2_ctl_en       = r_apb_paddr_d[11:0] == `CORE2_CTL_OFFSET;
assign w_core3_boot_addr_en = r_apb_paddr_d[11:0] == `CORE3_BOOT_ADDR_OFFSET;
assign w_core3_itcm_ctl_en  = r_apb_paddr_d[11:0] == `CORE3_ITCM_CTL_OFFSET;
assign w_core3_dtcm_ctl_en  = r_apb_paddr_d[11:0] == `CORE3_DTCM_CTL_OFFSET;
assign w_core3_llpp_ctl_en  = r_apb_paddr_d[11:0] == `CORE3_LLPP_CTL_OFFSET;
assign w_core3_flash_ctl_en = r_apb_paddr_d[11:0] == `CORE3_FLASH_CTL_OFFSET;
assign w_core3_ctl_en       = r_apb_paddr_d[11:0] == `CORE3_CTL_OFFSET;





//assign w_rx_mst_wren = w_apb_write & (i_ahb_haddr == AAA) & key;



//-----------------------------------------  CLKSRCCTL0  -----------------------------------------//
//r_oscclk_src_sel[1:0]    - RW-2h

//gen register writes
logic [1:0] r_oscclk_src_sel;

logic w_clksrcctl0_wren;

assign w_clksrcctl0_wren = w_apb_write & w_clksrcctl0_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_oscclk_src_sel  <= 2'b10;
    end
    else if(w_apb_write && w_clksrcctl0_wren) begin
        r_oscclk_src_sel  <= r_apb_pwdata_d[1:0];
    end
end


//-----------------------------------------  CLKSRCCTL1  -----------------------------------------//
//r_sys_clk_sel[2:0]    - RW-0h
//r_sysdiv_clk_sel[10:8]    - RW-0h
//r_xclk_out_sel[19:16]    - RW-0h

//gen register writes
logic [2:0] r_sys_clk_sel;
logic [2:0] r_sysdiv_clk_sel;
logic [3:0] r_xclk_out_sel;

logic w_clksrcctl1_wren;

assign w_clksrcctl1_wren = w_apb_write & w_clksrcctl1_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_sys_clk_sel     <= 3'b0;
        r_sysdiv_clk_sel  <= 4'b0;
        r_xclk_out_sel    <= 4'b0;
    end
    else if(w_apb_write && w_clksrcctl1_wren) begin
        r_sys_clk_sel     <= r_apb_pwdata_d[2:0];
        r_sysdiv_clk_sel  <= r_apb_pwdata_d[10:8];
        r_xclk_out_sel    <= r_apb_pwdata_d[19:16];
    end
end


//-----------------------------------------  SYSPLLCTL0  -----------------------------------------//
//r_pll_enable[0] - RW-0h

//gen register writes
logic r_pll_enable;
logic w_syspllctl0_wren;

assign w_syspllctl0_wren = w_apb_write & w_syspllctl0_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_pll_enable   <= 1'b0;
    end
    else if(w_apb_write && w_syspllctl0_wren) begin
        r_pll_enable   <= r_apb_pwdata_d[0];
    end
end


//-----------------------------------------  SYSPLLCTL1  -----------------------------------------//
//r_pll_div_pre[5:0]   - RW-0h
//r_pll_div_loop[15:8] - RW-0h
//r_pll_div_out[21:16] - RW-0h

//gen register writes
logic [5:0] r_pll_div_pre;
logic [7:0] r_pll_div_loop;
logic [5:0] r_pll_div_out;

logic w_syspllctl1_wren;

assign w_syspllctl1_wren = w_apb_write & w_syspllctl1_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_pll_div_pre   <= 6'h3;
        r_pll_div_loop  <= 8'h19;
        r_pll_div_out   <= 6'h2;
    end
    else if(w_apb_write && w_syspllctl1_wren) begin
        r_pll_div_pre   <= r_apb_pwdata_d[5:0];
        r_pll_div_loop  <= r_apb_pwdata_d[15:8];
        r_pll_div_out   <= r_apb_pwdata_d[21:16];
    end
end


//-----------------------------------------  SYSPLLCTL2  -----------------------------------------//
//r_pll_sw_vco[2:0]    - RW-4h
//r_pll_sw_icp[9:8]    - RW-2h
//r_pll_sw_lp[10]      - RW-1h
//r_pll_sw_vreg[12:11] - RW-2h
//r_pll_sw_lock[16]    - RW-1h
//r_pll_burr_byp[17]   - RW-0h

//gen register writes
logic [2:0] r_pll_sw_vco;
logic [1:0] r_pll_sw_icp;
logic r_pll_sw_lp;
logic [1:0] r_pll_sw_vreg;
logic r_pll_sw_lock;
logic r_pll_burr_byp;

logic w_syspllctl2_wren;

assign w_syspllctl2_wren = w_apb_write & w_syspllctl2_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_pll_sw_vco   <= 3'b100;
        r_pll_sw_icp   <= 2'b01;
        r_pll_sw_lp    <= 1'b1;
        r_pll_sw_vreg  <= 2'b10;
        r_pll_sw_lock  <= 1'b1;
        r_pll_burr_byp <= 1'b0;
    end
    else if(w_apb_write && w_syspllctl2_wren) begin
        r_pll_sw_vco   <= r_apb_pwdata_d[2:0];
        r_pll_sw_icp   <= r_apb_pwdata_d[9:8];
        r_pll_sw_lp    <= r_apb_pwdata_d[10];
        r_pll_sw_vreg  <= r_apb_pwdata_d[12:11];
        r_pll_sw_lock  <= r_apb_pwdata_d[16];
        r_pll_burr_byp <= r_apb_pwdata_d[17];
    end
end



//-----------------------------------------  SYSOSC1CTL0  -----------------------------------------//
//r_osc1_enable[0]    - RW-1h

//gen register writes
logic r_osc1_enable;

logic w_sysosc1ctl0_wren;

assign w_sysosc1ctl0_wren = w_apb_write & w_sysosc1ctl0_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_osc1_enable  <= 1'b1;
    end
    else if(w_apb_write && w_sysosc1ctl0_wren) begin
        r_osc1_enable  <= r_apb_pwdata_d[0];
    end
end



//-----------------------------------------  SYSOSC1CTL1  -----------------------------------------//
//r_osc1_iop_sel[2:0]      - RW-4h
//r_osc1_freq_sel[11:8]    - RW-5h
//r_osc1_freq_ctune[21:16] - RW-20h
//r_osc1_freq_ftune[29:24] - RW-20h

//gen register writes
logic [ 2:0] r_osc1_iop_sel;
logic [ 3:0] r_osc1_freq_sel;
logic [ 5:0] r_osc1_freq_ctune;
logic [ 5:0] r_osc1_freq_ftune;

logic w_sysosc1ctl1_wren;

assign w_sysosc1ctl1_wren = w_apb_write & w_sysosc1ctl1_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_osc1_iop_sel     <= 3'b100;
        r_osc1_freq_sel    <= 4'b0101;
        r_osc1_freq_ctune  <= 6'b100100;
        r_osc1_freq_ftune  <= 6'b101110;
    end
    else if(w_apb_write && w_sysosc1ctl1_wren) begin
        r_osc1_iop_sel     <= r_apb_pwdata_d[2:0];
        r_osc1_freq_sel    <= r_apb_pwdata_d[11:8];
        r_osc1_freq_ctune  <= r_apb_pwdata_d[21:16];
        r_osc1_freq_ftune  <= r_apb_pwdata_d[29:24];
    end
end


//-----------------------------------------  SYSOSC1CTL2  -----------------------------------------//
//r_osc1_clkref[0]      - RW-0h
//r_osc1_trim_en[8]     - RW-0h
//r_osc1_ref_cnt[31:16] - RW-4E2h

//gen register writes
logic        r_osc1_clkref;
logic        r_osc1_trim_en;
logic [15:0] r_osc1_ref_cnt;

logic w_sysosc1ctl2_wren;

assign w_sysosc1ctl2_wren = w_apb_write & w_sysosc1ctl2_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_osc1_clkref   <= 1'b0;
        r_osc1_trim_en  <= 1'b0;
        r_osc1_ref_cnt  <= 16'h04E2;
    end
    else if(w_apb_write && w_sysosc1ctl2_wren) begin
        r_osc1_clkref   <= r_apb_pwdata_d[0];
        r_osc1_trim_en  <= r_apb_pwdata_d[8];
        r_osc1_ref_cnt  <= r_apb_pwdata_d[31:16];
    end
end


//-----------------------------------------  SYSOSC2CTL0  -----------------------------------------//
//r_osc2_enable[0]    - RW-1h

//gen register writes
logic r_osc2_enable;

logic w_sysosc2ctl0_wren;

assign w_sysosc2ctl0_wren = w_apb_write & w_sysosc2ctl0_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_osc2_enable  <= 1'b1;
    end
    else if(w_apb_write && w_sysosc2ctl0_wren) begin
        r_osc2_enable  <= r_apb_pwdata_d[0];
    end
end



//-----------------------------------------  SYSOSC2CTL1  -----------------------------------------//
//r_osc2_iop_sel[2:0]      - RW-4h
//r_osc2_freq_sel[11:8]    - RW-5h
//r_osc2_freq_ctune[21:16] - RW-20h
//r_osc2_freq_ftune[29:24] - RW-20h

//gen register writes
logic [ 2:0] r_osc2_iop_sel;
logic [ 3:0] r_osc2_freq_sel;
logic [ 5:0] r_osc2_freq_ctune;
logic [ 5:0] r_osc2_freq_ftune;

logic w_sysosc2ctl1_wren;

assign w_sysosc2ctl1_wren = w_apb_write & w_sysosc2ctl1_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_osc2_iop_sel     <= 3'b100;
        r_osc2_freq_sel    <= 4'b0101;
        r_osc2_freq_ctune  <= 6'b100100;
        r_osc2_freq_ftune  <= 6'b101110;
    end
    else if(w_apb_write && w_sysosc2ctl1_wren) begin
        r_osc2_iop_sel     <= r_apb_pwdata_d[2:0];
        r_osc2_freq_sel    <= r_apb_pwdata_d[11:8];
        r_osc2_freq_ctune  <= r_apb_pwdata_d[21:16];
        r_osc2_freq_ftune  <= r_apb_pwdata_d[21:16];
    end
end


//-----------------------------------------  SYSOSC2CTL2  -----------------------------------------//
//r_osc2_clkref[0]      - RW-0h
//r_osc2_trim_en[8]     - RW-0h
//r_osc2_ref_cnt[31:16] - RW-4E2h

//gen register writes
logic        r_osc2_clkref;
logic        r_osc2_trim_en;
logic [15:0] r_osc2_ref_cnt;

logic w_sysosc2ctl2_wren;

assign w_sysosc2ctl2_wren = w_apb_write & w_sysosc2ctl2_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_osc2_clkref   <= 1'b0;
        r_osc2_trim_en  <= 1'b0;
        r_osc2_ref_cnt  <= 16'h04E2;
    end
    else if(w_apb_write && w_sysosc2ctl2_wren) begin
        r_osc2_clkref   <= r_apb_pwdata_d[0];
        r_osc2_trim_en  <= r_apb_pwdata_d[8];
        r_osc2_ref_cnt  <= r_apb_pwdata_d[31:16];
    end
end



//-----------------------------------------  SYSPLLSTS  -----------------------------------------//
//w_pll_lock[0]    - R-0h

//gen register writes
logic w_pll_lock;

logic w_syspllsts_wren;

assign w_syspllsts_wren = w_apb_write & w_syspllsts_en;





//-----------------------------------------  SYSCLKDIVSEL  -----------------------------------------//
//r_pll_sysclk_div[6:0]    - RW-2h

//gen register writes
logic [6:0] r_pll_sysclk_div;

logic w_sysclkdivsel_wren;

assign w_sysclkdivsel_wren = w_apb_write & w_sysclkdivsel_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_pll_sysclk_div  <= 7'b10;
    end
    else if(w_apb_write && w_sysclkdivsel_wren) begin
        r_pll_sysclk_div  <= r_apb_pwdata_d[6:0];
    end
end



//-----------------------------------------  LOSPCP0  -----------------------------------------//
//r_lsp_clk_div[3:0]     - RW-4h
//r_wdt_extclk_div[14:8]    - RW-8h
//r_timer_extclk_div[22:16] - RW-8h
//r_rtc_clk_div[30:24] - RW-8h

//gen register writes
logic [3:0] r_lsp_clk_div;
logic [6:0] r_wdt_extclk_div;
logic [6:0] r_timer_extclk_div;
logic [6:0] r_rtc_clk_div;

logic w_lospcp0_wren;

assign w_lospcp0_wren = w_apb_write & w_lospcp0_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_lsp_clk_div  <= 4'h4;
        r_wdt_extclk_div  <= 7'h8;
        r_timer_extclk_div  <= 7'h8;
        r_rtc_clk_div  <= 7'h8;
    end
    else if(w_apb_write && w_lospcp0_wren) begin
        r_lsp_clk_div  <= r_apb_pwdata_d[3:0];
        r_wdt_extclk_div  <= r_apb_pwdata_d[14:8];
        r_timer_extclk_div  <= r_apb_pwdata_d[22:16];
        r_rtc_clk_div  <= r_apb_pwdata_d[30:24];
    end
end



//-----------------------------------------  LOSPCP1  -----------------------------------------//
//r_gpio_extclk_div[6:0]    - RW-8h

//gen register writes
logic [6:0] r_gpio_extclk_div;

logic w_lospcp1_wren;

assign w_lospcp1_wren = w_apb_write & w_lospcp1_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_gpio_extclk_div  <= 7'h8;
    end
    else if(w_apb_write && w_lospcp1_wren) begin
        r_gpio_extclk_div  <= r_apb_pwdata_d[6:0];
    end
end



//-----------------------------------------  CLKCR0  -----------------------------------------//
//r_core0_clk_en[0]    - RW-1h
//r_core1_clk_en[1]    - RW-0h
//r_core2_clk_en[0]    - RW-0h
//r_core3_clk_en[1]    - RW-0h

//gen register writes
logic r_core0_clk_en;
logic r_core1_clk_en;
logic r_core2_clk_en;
logic r_core3_clk_en;

logic w_clkcr0_wren;

assign w_clkcr0_wren = w_apb_write & w_clkcr0_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core0_clk_en   <= 1'b1;
        r_core1_clk_en   <= 1'b0;
        r_core2_clk_en   <= 1'b0;
        r_core3_clk_en   <= 1'b0;
    end
    else if(w_apb_write && w_clkcr0_wren) begin
        r_core0_clk_en   <= r_apb_pwdata_d[0];
        r_core1_clk_en   <= r_apb_pwdata_d[1];
        r_core2_clk_en   <= r_apb_pwdata_d[2];
        r_core3_clk_en   <= r_apb_pwdata_d[3];
    end
end


//-----------------------------------------  CLKCR1  -----------------------------------------//
//r_dma_clk_en[0]    - RW-0h
//r_dpu_clk_en[1]    - RW-0h
//r_hsm_clk_en[2]    - RW-1h

//gen register writes
logic r_dma_clk_en;
logic r_dpu_clk_en;
logic r_hsm_clk_en;

logic w_clkcr1_wren;

assign w_clkcr1_wren = w_apb_write & w_clkcr1_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_dma_clk_en   <= 1'b0;
        r_dpu_clk_en   <= 1'b0;
        r_hsm_clk_en   <= 1'b1;
    end
    else if(w_apb_write && w_clkcr1_wren) begin
        r_dma_clk_en   <= r_apb_pwdata_d[0];
        r_dpu_clk_en   <= r_apb_pwdata_d[1];
        r_hsm_clk_en   <= r_apb_pwdata_d[2];
    end
end


//-----------------------------------------  CLKCR2  -----------------------------------------//
//r_uart_clk_en[0]   - RW-0h
//r_sdio_clk_en[1]   - RW-0h

//gen register writes
logic r_uart_clk_en;
logic r_sdio_clk_en;

logic w_clkcr2_wren;

assign w_clkcr2_wren = w_apb_write & w_clkcr2_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_uart_clk_en  <= 1'b0;
        r_sdio_clk_en  <= 1'b0;
    end
    else if(w_apb_write && w_clkcr2_wren) begin
        r_uart_clk_en  <= r_apb_pwdata_d[0];
        r_sdio_clk_en  <= r_apb_pwdata_d[1];
    end
end

//-----------------------------------------  CLKCR3  -----------------------------------------//
//r_spi_pclk_en[0]       - RW-0h
//r_iic_pclk_en[1]       - RW-0h
//r_iis_pclk_en[2]       - RW-0h
//r_wdt_pclk_en[3]       - RW-0h
//r_wdt_extclk_en[4]     - RW-0h
//r_timer_pclk_en[5]     - RW-0h
//r_timer_extclk_en[6]   - RW-0h
//r_rtc_pclk_en[7]       - RW-0h
//r_rtc_extclk_en[8]        - RW-0h
//r_analog_pclk_en[9]        - RW-0h
//r_dma_pclk_en[10]        - RW-0h
//r_uart_pclk_en[11]        - RW-0h
//r_gpio_pclk_en[12]        - RW-0h
//r_gpio_extclk_en[13]        - RW-0h

//gen register writes
logic r_spi_pclk_en;
logic r_iic_pclk_en;
logic r_iis_pclk_en;
logic r_wdt_pclk_en;
logic r_wdt_extclk_en;
logic r_timer_pclk_en;
logic r_timer_extclk_en;
logic r_rtc_pclk_en;
logic r_rtc_extclk_en;
logic r_analog_pclk_en;
logic r_dma_pclk_en;
logic r_uart_pclk_en;
logic r_gpio_pclk_en;
logic r_gpio_extclk_en;


logic w_clkcr3_wren;

assign w_clkcr3_wren = w_apb_write & w_clkcr3_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_spi_pclk_en     <= 1'b0;
        r_iic_pclk_en     <= 1'b0;
        r_iis_pclk_en     <= 1'b0;
        r_wdt_pclk_en     <= 1'b1;
        r_wdt_extclk_en   <= 1'b1;
        r_timer_pclk_en   <= 1'b0;
        r_timer_extclk_en <= 1'b0;
        r_rtc_pclk_en     <= 1'b0;
        r_rtc_extclk_en   <= 1'b0;
        r_analog_pclk_en  <= 1'b0;
        r_dma_pclk_en     <= 1'b0;
        r_uart_pclk_en    <= 1'b0;
        r_gpio_pclk_en    <= 1'b0;
        r_gpio_extclk_en  <= 1'b0;
    end
    else if(w_apb_write && w_clkcr3_wren) begin
        r_spi_pclk_en     <= r_apb_pwdata_d[0];
        r_iic_pclk_en     <= r_apb_pwdata_d[1];
        r_iis_pclk_en     <= r_apb_pwdata_d[2];
        r_wdt_pclk_en     <= r_apb_pwdata_d[3];
        r_wdt_extclk_en   <= r_apb_pwdata_d[4];
        r_timer_pclk_en   <= r_apb_pwdata_d[5];
        r_timer_extclk_en <= r_apb_pwdata_d[6];
        r_rtc_pclk_en     <= r_apb_pwdata_d[7];
        r_rtc_extclk_en   <= r_apb_pwdata_d[8];
        r_analog_pclk_en  <= r_apb_pwdata_d[9];
        r_dma_pclk_en     <= r_apb_pwdata_d[10];
        r_uart_pclk_en    <= r_apb_pwdata_d[11];
        r_gpio_pclk_en    <= r_apb_pwdata_d[12];
        r_gpio_extclk_en  <= r_apb_pwdata_d[13];
    end
end



//-----------------------------------------  SOFTPRES0  -----------------------------------------//
//r_core0_soft_reset[0]    - RW-1h
//r_core1_soft_reset[1]    - RW-0h
//r_core2_soft_reset[0]    - RW-0h
//r_core3_soft_reset[1]    - RW-0h

//gen register writes
logic r_core0_soft_reset;
logic r_core1_soft_reset;
logic r_core2_soft_reset;
logic r_core3_soft_reset;

logic w_softpres0_wren;

assign w_softpres0_wren = w_apb_write & w_softpres0_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core0_soft_reset   <= CORE0_RESET_RELEASE_DEFAULT;
        r_core1_soft_reset   <= 1'b0;
        r_core2_soft_reset   <= 1'b0;
        r_core3_soft_reset   <= 1'b0;
    end
    else if(w_apb_write && w_softpres0_wren) begin
        r_core0_soft_reset   <= r_apb_pwdata_d[0];
        r_core1_soft_reset   <= r_apb_pwdata_d[0];
        r_core2_soft_reset   <= r_apb_pwdata_d[0];
        r_core3_soft_reset   <= r_apb_pwdata_d[0];
    end
end


//-----------------------------------------  SOFTPRES1  -----------------------------------------//
//r_dma_soft_reset[0]    - RW-0h
//r_dpu_soft_reset[1]    - RW-0h
//r_hsm_soft_reset[2]    - RW-0h

//gen register writes
logic r_dma_soft_reset;
logic r_dpu_soft_reset;
logic r_hsm_soft_reset;

logic w_softpres1_wren;

assign w_softpres1_wren = w_apb_write & w_softpres1_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_dma_soft_reset   <= 1'b0;
        r_dpu_soft_reset   <= 1'b0;
        r_hsm_soft_reset   <= 1'b1;
    end
    else if(w_apb_write && w_softpres1_wren) begin
        r_dma_soft_reset   <= r_apb_pwdata_d[0];
        r_dpu_soft_reset   <= r_apb_pwdata_d[1];
        r_hsm_soft_reset   <= r_apb_pwdata_d[1];
    end
end


//-----------------------------------------  SOFTPRES2  -----------------------------------------//
//r_uart_soft_reset[0]   - RW-0h
//r_sdio_soft_reset[1]   - RW-0h

//gen register writes
logic r_uart_soft_reset;
logic r_sdio_soft_reset;

logic w_softpres2_wren;

assign w_softpres2_wren = w_apb_write & w_softpres2_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_uart_soft_reset  <= 1'b0;
        r_sdio_soft_reset  <= 1'b0;
    end
    else if(w_apb_write && w_softpres2_wren) begin
        r_uart_soft_reset  <= r_apb_pwdata_d[0];
        r_sdio_soft_reset  <= r_apb_pwdata_d[1];
    end
end


//-----------------------------------------  SOFTPRES3  -----------------------------------------//
//r_spi_soft_preset[0]       - RW-0h
//r_iic_soft_preset[1]       - RW-0h
//r_iis_soft_preset[2]       - RW-0h
//r_wdt_soft_preset[3]       - RW-0h
//r_timer_soft_preset[5]     - RW-0h
//r_timer_soft_extreset[6]   - RW-0h
//r_rtc_soft_preset[7]       - RW-0h
//r_rtc_soft_extreset[8]     - RW-0h
//r_analog_soft_preset[9]    - RW-0h
//r_dma_soft_preset[10]      - RW-0h
//r_uart_soft_preset[11]     - RW-0h
//r_gpio_soft_preset[12]     - RW-0h


//gen register writes
logic r_spi_soft_preset;
logic r_iic_soft_preset;
logic r_iis_soft_preset;
logic r_wdt_soft_preset;
logic r_timer_soft_preset;
logic r_timer_soft_extreset;
logic r_rtc_soft_preset;
logic r_rtc_soft_extreset;
logic r_analog_soft_preset;
logic r_dma_soft_preset;
logic r_gpio_soft_preset;
logic r_uart_soft_preset;

logic w_softpres3_wren;

assign w_softpres3_wren = w_apb_write & w_softpres3_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_spi_soft_preset     <= 1'b0;
        r_iic_soft_preset     <= 1'b0;
        r_iis_soft_preset     <= 1'b0;
        r_wdt_soft_preset     <= 1'b1;
        r_timer_soft_preset   <= 1'b0;
        r_timer_soft_extreset <= 1'b0;
        r_rtc_soft_preset     <= 1'b0;
        r_rtc_soft_extreset   <= 1'b0;
        r_analog_soft_preset  <= 1'b0;
        r_dma_soft_preset     <= 1'b0;
        r_gpio_soft_preset    <= 1'b0;
        r_uart_soft_preset    <= 1'b0;
    end
    else if(w_apb_write && w_softpres3_wren) begin
        r_spi_soft_preset     <= r_apb_pwdata_d[0];
        r_iic_soft_preset     <= r_apb_pwdata_d[1];
        r_iis_soft_preset     <= r_apb_pwdata_d[2];
        r_wdt_soft_preset     <= r_apb_pwdata_d[3];
        r_timer_soft_preset   <= r_apb_pwdata_d[5];
        r_timer_soft_extreset <= r_apb_pwdata_d[6];
        r_rtc_soft_preset     <= r_apb_pwdata_d[7];
        r_rtc_soft_extreset   <= r_apb_pwdata_d[8];
        r_analog_soft_preset  <= r_apb_pwdata_d[9];
        r_dma_soft_preset     <= r_apb_pwdata_d[10];
        r_uart_soft_preset    <= r_apb_pwdata_d[11];
        r_gpio_soft_preset    <= r_apb_pwdata_d[12];
    end
end


//-----------------------------------------  PMUCTL0  -----------------------------------------//
//r_vref1p65_vsel_lv[2:0] - RW-0h
//r_reg_loadsel_lv[4:3]   - RW-0h
//r_vrefp25_vsel_lv[7:5]  - RW-0h
//r_ldo_pd_lv[8]          - RW-0h
//r_reg_ldo_lv[12:9]      - RW-0h
//r_bor_en_lv[16]         - RW-0h
//r_reg_bor_lv[19:17]     - RW-0h

//gen register writes
logic  [2:0] r_vref1p65_vsel_lv;
logic  [1:0] r_reg_loadsel_lv;
logic  [2:0] r_vrefp25_vsel_lv;
//LDO_CORE
logic        r_ldo_pd_lv;
logic  [3:0] r_reg_ldo_lv;
//BOR
logic        r_bor_en_lv;
logic  [2:0] r_reg_bor_lv;


logic w_pmuctl0_wren;

assign w_pmuctl0_wren = w_apb_write & w_pmuctl0_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_vref1p65_vsel_lv  <= 1'b0;
        r_reg_loadsel_lv    <= 1'b0;
        r_vrefp25_vsel_lv   <= 1'b0;
        r_ldo_pd_lv         <= 1'b0;
        r_reg_ldo_lv        <= 1'b0;
        r_bor_en_lv         <= 1'b0;
        r_reg_bor_lv        <= 1'b0;
    end
    else if(w_apb_write && w_pmuctl0_wren) begin
        r_vref1p65_vsel_lv  <= r_apb_pwdata_d[2:0];
        r_reg_loadsel_lv    <= r_apb_pwdata_d[4:3];
        r_vrefp25_vsel_lv   <= r_apb_pwdata_d[7:5];
        r_ldo_pd_lv         <= r_apb_pwdata_d[8];
        r_reg_ldo_lv        <= r_apb_pwdata_d[12:9];
        r_bor_en_lv         <= r_apb_pwdata_d[16];
        r_reg_bor_lv        <= r_apb_pwdata_d[19:17];
    end
end




//-----------------------------------------  PMUCTL1  -----------------------------------------//
//r_clk_chop_lv[0]          - RW-0h
//r_vref12_en_lv[1]         - RW-0h
//r_vref1p65_en_lv[2]       - RW-0h
//r_vrefp25_en_lv[3]        - RW-0h
//r_hqbgp_tempadj_lv[11:8]  - RW-0h
//r_hqbgp_ctrim_lv[19:16]   - RW-0h
//r_hqbgp_ftrim_lv[29:24]   - RW-0h

//gen register writes
logic        r_clk_chop_lv;
logic        r_vref12_en_lv;
logic        r_vref1p65_en_lv;
logic        r_vrefp25_en_lv;

logic  [3:0] r_hqbgp_tempadj_lv;
logic  [3:0] r_hqbgp_ctrim_lv;
logic  [5:0] r_hqbgp_ftrim_lv;


logic w_pmuctl1_wren;

assign w_pmuctl1_wren = w_apb_write & w_pmuctl1_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_clk_chop_lv       <= 1'b0;
        r_vref12_en_lv      <= 1'b0;
        r_vref1p65_en_lv    <= 1'b0;
        r_vrefp25_en_lv     <= 1'b0;
        r_hqbgp_tempadj_lv  <= 1'b0;
        r_hqbgp_ctrim_lv    <= 1'b0;
        r_hqbgp_ftrim_lv    <= 1'b0;
    end
    else if(w_apb_write && w_pmuctl1_wren) begin
        r_clk_chop_lv       <= r_apb_pwdata_d[0];
        r_vref12_en_lv      <= r_apb_pwdata_d[1];
        r_vref1p65_en_lv    <= r_apb_pwdata_d[2];
        r_vrefp25_en_lv     <= r_apb_pwdata_d[3];
        r_hqbgp_tempadj_lv  <= r_apb_pwdata_d[11:8];
        r_hqbgp_ctrim_lv    <= r_apb_pwdata_d[19:16];
        r_hqbgp_ftrim_lv    <= r_apb_pwdata_d[29:24];
    end
end


//-----------------------------------------  CORE0_BOOT_ADDR  -----------------------------------------//
//r_core0_boot_addr[31:0]    - RW-10000000h

//gen register writes
logic [31:0] r_core0_boot_addr;

logic w_core0_boot_addr_wren;

assign w_core0_boot_addr_wren = w_apb_write & w_core0_boot_addr_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core0_boot_addr  <= 32'h00400000;
    end
    else if(w_apb_write && w_core0_boot_addr_wren) begin
        r_core0_boot_addr  <= r_apb_pwdata_d[31:0];
    end
end

//-----------------------------------------  CORE0_ITCM_CTL  -----------------------------------------//
//r_core0_itcm_startAddr[18:0] - RW-00038h
//r_core0_itcm_size[28:24]     - RW-7h

//gen register writes
logic [18:0] r_core0_itcm_startAddr;
logic [4:0]  r_core0_itcm_size;


logic w_core0_itcm_ctl_wren;

assign w_core0_itcm_ctl_wren = w_apb_write & w_core0_itcm_ctl_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core0_itcm_startAddr  <= 19'h00200;
        r_core0_itcm_size  <= 5'b00111;
    end
    else if(w_apb_write && w_core0_itcm_ctl_wren) begin
        r_core0_itcm_startAddr  <= r_apb_pwdata_d[18:0];
        r_core0_itcm_size  <= r_apb_pwdata_d[28:24];
    end
end



//-----------------------------------------  CORE0_DTCM_CTL  -----------------------------------------//
//r_core0_dtcm_startAddr[18:0] - RW-00040h
//r_core0_dtcm_size[28:24]     - RW-7h

//gen register writes
logic [18:0] r_core0_dtcm_startAddr;
logic [4:0]  r_core0_dtcm_size;


logic w_core0_dtcm_ctl_wren;

assign w_core0_dtcm_ctl_wren = w_apb_write & w_core0_dtcm_ctl_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core0_dtcm_startAddr  <= 19'h00400;
        r_core0_dtcm_size  <= 5'b00111;
    end
    else if(w_apb_write && w_core0_dtcm_ctl_wren) begin
        r_core0_dtcm_startAddr  <= r_apb_pwdata_d[18:0];
        r_core0_dtcm_size  <= r_apb_pwdata_d[28:24];
    end
end



//-----------------------------------------  CORE0_LLPP_CTL  -----------------------------------------//
//r_core0_llpp_startAddr[19:0] - RW-B0000h
//r_core0_llpp_size[28:24]     - RW-11h

//gen register writes
logic [19:0] r_core0_llpp_startAddr;
logic [4:0]  r_core0_llpp_size;


logic w_core0_llpp_ctl_wren;

assign w_core0_llpp_ctl_wren = w_apb_write & w_core0_llpp_ctl_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core0_llpp_startAddr  <= 20'hB0000;
        r_core0_llpp_size  <= 5'b10001;
    end
    else if(w_apb_write && w_core0_dtcm_ctl_wren) begin
        r_core0_llpp_startAddr  <= r_apb_pwdata_d[19:0];
        r_core0_llpp_size  <= r_apb_pwdata_d[28:24];
    end
end



//-----------------------------------------  CORE0_FLASH_CTL  -----------------------------------------//
//r_core0_llpp_startAddr[4:0] - RW-04h
//r_core0_llpp_size[28:24]     - RW-12h

//gen register writes
logic [4:0] r_core0_flash_startAddr;
logic [4:0] r_core0_flash_size;


logic w_core0_flash_ctl_wren;

assign w_core0_flash_ctl_wren = w_apb_write & w_core0_flash_ctl_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core0_flash_startAddr  <= 5'h04;
        r_core0_flash_size  <= 5'b10010;
    end
    else if(w_apb_write && w_core0_flash_ctl_wren) begin
        r_core0_flash_startAddr  <= r_apb_pwdata_d[4:0];
        r_core0_flash_size  <= r_apb_pwdata_d[28:24];
    end
end


//-----------------------------------------  CORE1_BOOT_ADDR  -----------------------------------------//
//r_core1_boot_addr[31:0]    - RW-10000000h

//gen register writes
logic [31:0] r_core1_boot_addr;

logic w_core1_boot_addr_wren;

assign w_core1_boot_addr_wren = w_apb_write & w_core1_boot_addr_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core1_boot_addr  <= 32'h00400000;
    end
    else if(w_apb_write && w_core1_boot_addr_wren) begin
        r_core1_boot_addr  <= r_apb_pwdata_d[31:0];
    end
end

//-----------------------------------------  CORE1_ITCM_CTL  -----------------------------------------//
//r_core1_itcm_startAddr[18:0] - RW-00038h
//r_core1_itcm_size[28:24]     - RW-7h

//gen register writes
logic [18:0] r_core1_itcm_startAddr;
logic [4:0]  r_core1_itcm_size;


logic w_core1_itcm_ctl_wren;

assign w_core1_itcm_ctl_wren = w_apb_write & w_core1_itcm_ctl_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core1_itcm_startAddr  <= 19'h00200;
        r_core1_itcm_size  <= 5'b00111;
    end
    else if(w_apb_write && w_core1_itcm_ctl_wren) begin
        r_core1_itcm_startAddr  <= r_apb_pwdata_d[18:0];
        r_core1_itcm_size  <= r_apb_pwdata_d[28:24];
    end
end



//-----------------------------------------  CORE1_DTCM_CTL  -----------------------------------------//
//r_core1_dtcm_startAddr[18:0] - RW-00040h
//r_core1_dtcm_size[28:24]     - RW-7h

//gen register writes
logic [18:0] r_core1_dtcm_startAddr;
logic [4:0]  r_core1_dtcm_size;


logic w_core1_dtcm_ctl_wren;

assign w_core1_dtcm_ctl_wren = w_apb_write & w_core1_dtcm_ctl_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core1_dtcm_startAddr  <= 19'h00400;
        r_core1_dtcm_size  <= 5'b00111;
    end
    else if(w_apb_write && w_core1_dtcm_ctl_wren) begin
        r_core1_dtcm_startAddr  <= r_apb_pwdata_d[18:0];
        r_core1_dtcm_size  <= r_apb_pwdata_d[28:24];
    end
end



//-----------------------------------------  CORE1_LLPP_CTL  -----------------------------------------//
//r_core1_llpp_startAddr[19:0] - RW-B0000h
//r_core1_llpp_size[28:24]     - RW-11h

//gen register writes
logic [19:0] r_core1_llpp_startAddr;
logic [4:0]  r_core1_llpp_size;


logic w_core1_llpp_ctl_wren;

assign w_core1_llpp_ctl_wren = w_apb_write & w_core1_llpp_ctl_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core1_llpp_startAddr  <= 20'hB0000;
        r_core1_llpp_size  <= 5'b10001;
    end
    else if(w_apb_write && w_core1_dtcm_ctl_wren) begin
        r_core1_llpp_startAddr  <= r_apb_pwdata_d[19:0];
        r_core1_llpp_size  <= r_apb_pwdata_d[28:24];
    end
end



//-----------------------------------------  CORE1_FLASH_CTL  -----------------------------------------//
//r_core1_llpp_startAddr[4:0] - RW-04h
//r_core1_llpp_size[28:24]     - RW-12h

//gen register writes
logic [4:0] r_core1_flash_startAddr;
logic [4:0] r_core1_flash_size;


logic w_core1_flash_ctl_wren;

assign w_core1_flash_ctl_wren = w_apb_write & w_core1_flash_ctl_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core1_flash_startAddr  <= 5'h04;
        r_core1_flash_size  <= 5'b10010;
    end
    else if(w_apb_write && w_core1_flash_ctl_wren) begin
        r_core1_flash_startAddr  <= r_apb_pwdata_d[4:0];
        r_core1_flash_size  <= r_apb_pwdata_d[28:24];
    end
end



//-----------------------------------------  CORE2_BOOT_ADDR  -----------------------------------------//
//r_core2_boot_addr[31:0]    - RW-10000000h

//gen register writes
logic [31:0] r_core2_boot_addr;

logic w_core2_boot_addr_wren;

assign w_core2_boot_addr_wren = w_apb_write & w_core2_boot_addr_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core2_boot_addr  <= 32'h10000000;
    end
    else if(w_apb_write && w_core2_boot_addr_wren) begin
        r_core2_boot_addr  <= r_apb_pwdata_d[31:0];
    end
end

//-----------------------------------------  CORE2_ITCM_CTL  -----------------------------------------//
//r_core2_itcm_startAddr[18:0] - RW-00038h
//r_core2_itcm_size[28:24]     - RW-7h

//gen register writes
logic [18:0] r_core2_itcm_startAddr;
logic [4:0]  r_core2_itcm_size;


logic w_core2_itcm_ctl_wren;

assign w_core2_itcm_ctl_wren = w_apb_write & w_core2_itcm_ctl_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core2_itcm_startAddr  <= 19'h00038;
        r_core2_itcm_size  <= 5'b00111;
    end
    else if(w_apb_write && w_core2_itcm_ctl_wren) begin
        r_core2_itcm_startAddr  <= r_apb_pwdata_d[18:0];
        r_core2_itcm_size  <= r_apb_pwdata_d[28:24];
    end
end



//-----------------------------------------  CORE2_DTCM_CTL  -----------------------------------------//
//r_core2_dtcm_startAddr[18:0] - RW-00040h
//r_core2_dtcm_size[28:24]     - RW-7h

//gen register writes
logic [18:0] r_core2_dtcm_startAddr;
logic [4:0]  r_core2_dtcm_size;


logic w_core2_dtcm_ctl_wren;

assign w_core2_dtcm_ctl_wren = w_apb_write & w_core2_dtcm_ctl_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core2_dtcm_startAddr  <= 19'h00040;
        r_core2_dtcm_size  <= 5'b00111;
    end
    else if(w_apb_write && w_core2_dtcm_ctl_wren) begin
        r_core2_dtcm_startAddr  <= r_apb_pwdata_d[18:0];
        r_core2_dtcm_size  <= r_apb_pwdata_d[28:24];
    end
end



//-----------------------------------------  CORE2_LLPP_CTL  -----------------------------------------//
//r_core2_llpp_startAddr[19:0] - RW-B0000h
//r_core2_llpp_size[28:24]     - RW-11h

//gen register writes
logic [19:0] r_core2_llpp_startAddr;
logic [4:0]  r_core2_llpp_size;


logic w_core2_llpp_ctl_wren;

assign w_core2_llpp_ctl_wren = w_apb_write & w_core2_llpp_ctl_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core2_llpp_startAddr  <= 20'hB0000;
        r_core2_llpp_size  <= 5'b10001;
    end
    else if(w_apb_write && w_core2_llpp_ctl_wren) begin
        r_core2_llpp_startAddr  <= r_apb_pwdata_d[19:0];
        r_core2_llpp_size  <= r_apb_pwdata_d[28:24];
    end
end



//-----------------------------------------  CORE2_FLASH_CTL  -----------------------------------------//
//r_core2_llpp_startAddr[4:0] - RW-04h
//r_core2_llpp_size[28:24]     - RW-12h

//gen register writes
logic [4:0] r_core2_flash_startAddr;
logic [4:0] r_core2_flash_size;


logic w_core2_flash_ctl_wren;

assign w_core2_flash_ctl_wren = w_apb_write & w_core2_flash_ctl_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core2_flash_startAddr  <= 5'h04;
        r_core2_flash_size  <= 5'b10010;
    end
    else if(w_apb_write && w_core2_flash_ctl_wren) begin
        r_core2_flash_startAddr  <= r_apb_pwdata_d[4:0];
        r_core2_flash_size  <= r_apb_pwdata_d[28:24];
    end
end



//-----------------------------------------  CORE3_BOOT_ADDR  -----------------------------------------//
//r_core3_boot_addr[31:0]    - RW-10000000h

//gen register writes
logic [31:0] r_core3_boot_addr;

logic w_core3_boot_addr_wren;

assign w_core3_boot_addr_wren = w_apb_write & w_core3_boot_addr_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core3_boot_addr  <= 32'h10000000;
    end
    else if(w_apb_write && w_core3_boot_addr_wren) begin
        r_core3_boot_addr  <= r_apb_pwdata_d[31:0];
    end
end

//-----------------------------------------  CORE3_ITCM_CTL  -----------------------------------------//
//r_core3_itcm_startAddr[18:0] - RW-00038h
//r_core3_itcm_size[28:24]     - RW-7h

//gen register writes
logic [18:0] r_core3_itcm_startAddr;
logic [4:0]  r_core3_itcm_size;


logic w_core3_itcm_ctl_wren;

assign w_core3_itcm_ctl_wren = w_apb_write & w_core3_itcm_ctl_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core3_itcm_startAddr  <= 19'h00038;
        r_core3_itcm_size  <= 5'b00111;
    end
    else if(w_apb_write && w_core3_itcm_ctl_wren) begin
        r_core3_itcm_startAddr  <= r_apb_pwdata_d[18:0];
        r_core3_itcm_size  <= r_apb_pwdata_d[28:24];
    end
end



//-----------------------------------------  CORE3_DTCM_CTL  -----------------------------------------//
//r_core3_dtcm_startAddr[18:0] - RW-00040h
//r_core3_dtcm_size[28:24]     - RW-7h

//gen register writes
logic [18:0] r_core3_dtcm_startAddr;
logic [4:0]  r_core3_dtcm_size;


logic w_core3_dtcm_ctl_wren;

assign w_core3_dtcm_ctl_wren = w_apb_write & w_core3_dtcm_ctl_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core3_dtcm_startAddr  <= 19'h00040;
        r_core3_dtcm_size  <= 5'b00111;
    end
    else if(w_apb_write && w_core3_dtcm_ctl_wren) begin
        r_core3_dtcm_startAddr  <= r_apb_pwdata_d[18:0];
        r_core3_dtcm_size  <= r_apb_pwdata_d[28:24];
    end
end



//-----------------------------------------  CORE3_LLPP_CTL  -----------------------------------------//
//r_core3_llpp_startAddr[19:0] - RW-B0000h
//r_core3_llpp_size[28:24]     - RW-11h

//gen register writes
logic [19:0] r_core3_llpp_startAddr;
logic [4:0]  r_core3_llpp_size;


logic w_core3_llpp_ctl_wren;

assign w_core3_llpp_ctl_wren = w_apb_write & w_core3_llpp_ctl_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core3_llpp_startAddr  <= 20'hB0000;
        r_core3_llpp_size  <= 5'b10001;
    end
    else if(w_apb_write && w_core3_llpp_ctl_wren) begin
        r_core3_llpp_startAddr  <= r_apb_pwdata_d[19:0];
        r_core3_llpp_size  <= r_apb_pwdata_d[28:24];
    end
end



//-----------------------------------------  CORE3_FLASH_CTL  -----------------------------------------//
//r_core3_llpp_startAddr[4:0] - RW-04h
//r_core3_llpp_size[28:24]     - RW-12h

//gen register writes
logic [4:0] r_core3_flash_startAddr;
logic [4:0] r_core3_flash_size;


logic w_core3_flash_ctl_wren;

assign w_core3_flash_ctl_wren = w_apb_write & w_core3_flash_ctl_en;

always @(posedge i_sys_clk or negedge i_rst_n)
begin
    if(!i_rst_n) begin
        r_core3_flash_startAddr  <= 5'h04;
        r_core3_flash_size  <= 5'b10010;
    end
    else if(w_apb_write && w_core3_flash_ctl_wren) begin
        r_core3_flash_startAddr  <= r_apb_pwdata_d[4:0];
        r_core3_flash_size  <= r_apb_pwdata_d[28:24];
    end
end



logic [31:0] r_apb_prdata_t;
logic [31:0] r_apb_prdata;

always @(posedge w_lspdiv_ckout or negedge o_sys_prst_n) begin
    if(!o_sys_prst_n)
        r_apb_prdata <= 32'b0;
    else if(w_apb_read)
        r_apb_prdata <= r_apb_prdata_t;
end

assign o_apb_prdata = r_apb_prdata;


always @(*) begin
    r_apb_prdata_t = 64'b0;
    case(i_apb_paddr[11:0])
    `SYSPLLCTL0_OFFSET: begin
        r_apb_prdata_t = {31'b0, r_pll_enable};
    end
    `SYSPLLCTL1_OFFSET: begin
        r_apb_prdata_t = {8'b0, 2'b0, r_pll_div_out, r_pll_div_loop, 2'b0, r_pll_div_pre};
    end
    `SYSPLLCTL2_OFFSET: begin
        r_apb_prdata_t = {14'b0, r_pll_burr_byp,  3'b0, r_osc1_enable,
                    r_pll_sw_vreg, r_pll_sw_lp, r_pll_sw_icp, 5'b0, r_pll_sw_vco};
    end
    `SYSOSC1CTL0_OFFSET: begin
        r_apb_prdata_t = {16'b0, 15'b0,r_osc1_enable}; 
    end
    `SYSOSC1CTL1_OFFSET: begin
        r_apb_prdata_t = {2'b0, 5'b0, r_osc1_freq_ftune, 2'b0, r_osc1_freq_ctune, 
                    4'b0, r_osc1_freq_sel, 5'b0, r_osc1_iop_sel};
    end
    `SYSOSC1CTL2_OFFSET: begin
        r_apb_prdata_t = {r_osc1_ref_cnt, 7'b0, r_osc1_trim_en, 7'b0, r_osc1_clkref};
    end
    `SYSOSC2CTL0_OFFSET: begin
        r_apb_prdata_t = {16'b0, 15'b0,r_osc2_enable}; 
    end
    `SYSOSC2CTL1_OFFSET: begin
        r_apb_prdata_t = {2'b0, 5'b0, r_osc2_freq_ftune, 2'b0, r_osc2_freq_ctune, 
                    4'b0, r_osc2_freq_sel, 5'b0, r_osc2_iop_sel};
    end
    `SYSOSC2CTL2_OFFSET: begin
        r_apb_prdata_t = {r_osc2_ref_cnt, 7'b0, r_osc2_trim_en, 7'b0, r_osc2_clkref};
    end
    `CLKSRCCTL0_OFFSET: begin
        r_apb_prdata_t = {16'b0, 14'b0, r_oscclk_src_sel};
    end
    `CLKSRCCTL1_OFFSET: begin
        r_apb_prdata_t = {8'b0, 4'b0, r_xclk_out_sel, 5'b0, r_sysdiv_clk_sel, 
                    5'b0, r_sys_clk_sel};
    end
    `SYSPLLSTS_OFFSET: begin
        r_apb_prdata_t = {16'b0, 15'b0, w_pll_lock};
    end
    `SYSCLKDIVSEL_OFFSET: begin
        r_apb_prdata_t = {16'b0, 1'b0, r_pll_sysclk_div};
    end
    `LOSPCP0_OFFSET: begin
        r_apb_prdata_t = {1'b0, r_rtc_clk_div, 1'b0, r_timer_extclk_div, 1'b0, r_wdt_extclk_div, 4'b0, r_lsp_clk_div};
    end
    `LOSPCP1_OFFSET: begin
        r_apb_prdata_t = {24'b0, 1'b0, r_gpio_extclk_div};
    end
    `CLKCR0_OFFSET: begin
        r_apb_prdata_t = {16'b0, 14'b0, r_core3_clk_en, r_core2_clk_en,
                    r_core1_clk_en, r_core0_clk_en};
    end
    `CLKCR1_OFFSET: begin
        r_apb_prdata_t = {16'b0, 13'b0, r_dpu_clk_en, r_dma_clk_en, r_hsm_clk_en};
    end
    `CLKCR2_OFFSET: begin
        r_apb_prdata_t = {16'b0, 14'b0,  r_uart_clk_en, r_sdio_clk_en};
    end
    `CLKCR3_OFFSET: begin
        r_apb_prdata_t = {16'b0, 2'b0, r_gpio_extclk_en, r_gpio_pclk_en, r_uart_pclk_en, 
                    r_dma_pclk_en, r_analog_pclk_en, r_rtc_extclk_en, 
                    r_rtc_pclk_en, r_timer_extclk_en,r_timer_pclk_en, 
                    r_wdt_extclk_en, r_wdt_pclk_en, r_iis_pclk_en, r_iic_pclk_en, r_spi_pclk_en};
    end 
    `SOFTPRES0_OFFSET: begin
        r_apb_prdata_t = {16'b0, 12'b0, r_core3_soft_reset, r_core2_soft_reset,
                    r_core1_soft_reset, r_core0_soft_reset};
    end
    `SOFTPRES1_OFFSET: begin
        r_apb_prdata_t = {16'b0, 14'b0, r_dpu_soft_reset, r_dma_soft_reset};
    end
    `SOFTPRES2_OFFSET: begin
        r_apb_prdata_t = {16'b0, 14'b0, r_sdio_soft_reset,  r_uart_soft_reset};
    end
    `SOFTPRES3_OFFSET: begin
        r_apb_prdata_t = {16'b0, 3'b0, r_gpio_soft_preset, r_uart_soft_preset, 
                    r_dma_soft_preset, r_analog_soft_preset,r_rtc_soft_extreset, 
                    r_rtc_soft_preset, r_timer_soft_extreset, r_timer_soft_preset, 1'b0,
                    r_wdt_soft_preset, r_iis_soft_preset, r_iic_soft_preset, r_spi_soft_preset};
    end
    `PMUCTL0_OFFSET: begin
        r_apb_prdata_t = {12'b0, r_reg_bor_lv, r_bor_en_lv, 3'b0, 
                    r_reg_ldo_lv, r_ldo_pd_lv,r_vrefp25_vsel_lv, 
                    r_reg_loadsel_lv, r_vref1p65_vsel_lv};
    end
    `PMUCTL1_OFFSET: begin
        r_apb_prdata_t = {2'b0, r_hqbgp_ftrim_lv, 4'b0, r_hqbgp_ctrim_lv, 
                    4'b0, r_hqbgp_tempadj_lv, 4'b0, r_vrefp25_en_lv, 
                    r_vref1p65_en_lv, r_vref12_en_lv, r_clk_chop_lv};
    end
    `CORE0_BOOT_ADDR_OFFSET: begin
        r_apb_prdata_t = {r_core0_boot_addr};
    end
    `CORE0_ITCM_CTL_OFFSET: begin
        r_apb_prdata_t = {3'b0, r_core0_itcm_size, 5'b0, r_core0_itcm_startAddr};
    end
    `CORE0_DTCM_CTL_OFFSET: begin
        r_apb_prdata_t = {3'b0, r_core0_dtcm_size, 5'b0, r_core0_dtcm_startAddr};
    end
    `CORE0_LLPP_CTL_OFFSET: begin
        r_apb_prdata_t = {3'b0, r_core0_llpp_size, 4'b0, r_core0_llpp_startAddr};
    end
    `CORE0_FLASH_CTL_OFFSET: begin
        r_apb_prdata_t = {3'b0, r_core0_flash_size, 19'b0, r_core0_flash_startAddr};
    end
    `CORE1_BOOT_ADDR_OFFSET: begin
        r_apb_prdata_t = {r_core1_boot_addr};
    end
    `CORE1_ITCM_CTL_OFFSET: begin
        r_apb_prdata_t = {3'b0, r_core1_itcm_size, 5'b0, r_core1_itcm_startAddr};
    end
    `CORE1_DTCM_CTL_OFFSET: begin
        r_apb_prdata_t = {3'b0, r_core1_dtcm_size, 5'b0, r_core1_dtcm_startAddr};
    end
    `CORE1_LLPP_CTL_OFFSET: begin
        r_apb_prdata_t = {3'b0, r_core1_llpp_size, 4'b0, r_core1_llpp_startAddr};
    end
    `CORE1_FLASH_CTL_OFFSET: begin
        r_apb_prdata_t = {3'b0, r_core1_flash_size, 19'b0, r_core1_flash_startAddr};
    end
    `CORE2_BOOT_ADDR_OFFSET: begin
        r_apb_prdata_t = {r_core2_boot_addr};
    end
    `CORE2_ITCM_CTL_OFFSET: begin
        r_apb_prdata_t = {3'b0, r_core2_itcm_size, 5'b0, r_core2_itcm_startAddr};
    end
    `CORE2_DTCM_CTL_OFFSET: begin
        r_apb_prdata_t = {3'b0, r_core2_dtcm_size, 5'b0, r_core2_dtcm_startAddr};
    end
    `CORE2_LLPP_CTL_OFFSET: begin
        r_apb_prdata_t = {3'b0, r_core2_llpp_size, 4'b0, r_core2_llpp_startAddr};
    end
    `CORE2_FLASH_CTL_OFFSET: begin
        r_apb_prdata_t = {3'b0, r_core2_flash_size, 19'b0, r_core2_flash_startAddr};
    end
    `CORE3_BOOT_ADDR_OFFSET: begin
        r_apb_prdata_t = {r_core3_boot_addr};
    end
    `CORE3_ITCM_CTL_OFFSET: begin
        r_apb_prdata_t = {3'b0, r_core3_itcm_size, 5'b0, r_core3_itcm_startAddr};
    end
    `CORE3_DTCM_CTL_OFFSET: begin
        r_apb_prdata_t = {3'b0, r_core3_dtcm_size, 5'b0, r_core3_dtcm_startAddr};
    end
    `CORE3_LLPP_CTL_OFFSET: begin
        r_apb_prdata_t = {3'b0, r_core3_llpp_size, 4'b0, r_core3_llpp_startAddr};
    end
    `CORE3_FLASH_CTL_OFFSET: begin
        r_apb_prdata_t = {3'b0, r_core3_flash_size, 19'b0, r_core3_flash_startAddr};
    end
//    : begin
//        r_apb_prdata_t = {16'b0, gtpr_gt, gtpr_psc};
//    end
    default: r_apb_prdata_t  = {32'b0};
    endcase
end

//r_core1_itcm_startAddr[18:0] - RW-00038h
//r_core1_itcm_size[28:24]     - RW-7h


logic w_pll_osc_clk;
logic w_pll_clk_in;

clock_switch  #(
   .NUM_OF_SYN0 (2),
   .NUM_OF_SYN1 (4)
) u_pll_ckin_switch_0(
  .i_clk0   (w_osc2_clk_out), 
  .i_clk1   (i_xtal_clk), 
  .i_sel    (r_oscclk_src_sel[0]), 
  .i_rst_n  (r_ckmux_rst_n), 
  .o_clkout (w_pll_osc_clk)
);

clock_switch  #(
   .NUM_OF_SYN0 (2),
   .NUM_OF_SYN1 (4)
) u_pll_ckin_switch_1(
  .i_clk0   (w_pll_osc_clk), 
  .i_clk1   (w_osc1_clk_out), 
  .i_sel    (r_oscclk_src_sel[1]), 
  .i_rst_n  (r_ckmux_rst_n), 
  .o_clkout (w_pll_clk_in)
);


assign w_pll_clk_out = 1'b0;

assign w_osc1_clk_out = 1'b0;
assign w_osc2_clk_out = 1'b0;


logic w_sysdiv_ck0;
logic w_sysdiv_ck1;
logic w_sysdiv_ckin;


clock_switch  #(
   .NUM_OF_SYN0 (2),
   .NUM_OF_SYN1 (4)
) u_sys_div_clk_switch_0(
  .i_clk0   (w_osc1_clk_out),
  .i_clk1   (w_osc2_clk_out),
  .i_sel    (r_sysdiv_clk_sel[0]),
  .i_rst_n  (r_ckmux_rst_n),
  .o_clkout (w_sysdiv_ck0)
);

clock_switch  #(
   .NUM_OF_SYN0 (2),
   .NUM_OF_SYN1 (4)
) u_sys_div_clk_switch_1(
  .i_clk0   (w_sysdiv_ck0),
  .i_clk1   (i_xtal_clk),
  .i_sel    (r_sysdiv_clk_sel[1]),
  .i_rst_n  (r_ckmux_rst_n),
  .o_clkout (w_sysdiv_ck1)
);

clock_switch  #(
   .NUM_OF_SYN0 (2),
   .NUM_OF_SYN1 (4)
) u_sys_div_clk_switch_2(
  .i_clk0   (w_sysdiv_ck1),
  .i_clk1   (w_pll_clk_out),
  .i_sel    (r_sysdiv_clk_sel[2]),
  .i_rst_n  (r_ckmux_rst_n),
  .o_clkout (w_sysdiv_ckin)
);


sysdivx u_sysdivx(
  .i_clk    (w_sysdiv_ckin),
  .i_rst_n  (r_ckmux_rst_n),
  .i_div    (r_pll_sysclk_div),
  .o_clk    (w_sysdiv_ckout)
);


assign w_sys_clk = i_sys_clk;



assign w_lspdiv_ckout = w_sys_clk;
assign w_wdt_div_extckout = w_sys_clk;
assign w_timer_div_extckout = w_sys_clk;
assign w_rtc_div_ckout = w_sys_clk;
assign w_gpio_div_extckout = w_sys_clk;


/*
sysdivx u_lsp_divx(
  .i_clk    (w_sys_clk),
  .i_rst_n  (r_sys_rst_n),
  .i_div    ({3'b0,r_lsp_clk_div}),
  .o_clk    (w_lspdiv_ckout)
);

sysdivx u_wdt_divx(
  .i_clk    (w_sys_clk),
  .i_rst_n  (r_sys_rst_n),
  .i_div    (r_wdt_extclk_div),
  .o_clk    (w_wdt_div_extckout)
);

sysdivx u_timer_divx(
  .i_clk    (w_sys_clk),
  .i_rst_n  (r_sys_rst_n),
  .i_div    (r_timer_extclk_div),
  .o_clk    (w_timer_div_extckout)
);

sysdivx u_rtc_divx(
  .i_clk    (w_sys_clk),
  .i_rst_n  (r_sys_rst_n),
  .i_div    (r_rtc_clk_div),
  .o_clk    (w_rtc_div_ckout)
);

sysdivx u_gpio_divx(
  .i_clk    (w_sys_clk),
  .i_rst_n  (r_sys_rst_n),
  .i_div    (r_gpio_extclk_div),
  .o_clk    (w_gpio_div_extckout)
);
*/

ck_gate u_core0_clk_gate  ( .i_clock(w_sys_clk), .i_clock_en(r_core0_clk_en), .o_clock(o_core0_clk) );
ck_gate u_core1_clk_gate  ( .i_clock(w_sys_clk), .i_clock_en(r_core1_clk_en), .o_clock(o_core1_clk) );
ck_gate u_core2_clk_gate  ( .i_clock(w_sys_clk), .i_clock_en(r_core2_clk_en), .o_clock(o_core2_clk) );
ck_gate u_core3_clk_gate  ( .i_clock(w_sys_clk), .i_clock_en(r_core3_clk_en), .o_clock(o_core3_clk) );
ck_gate u_dpu_clk_gate    ( .i_clock(w_sys_clk), .i_clock_en(r_dpu_clk_en  ), .o_clock(o_dpu_clk  ) );
ck_gate u_dma_clk_gate    ( .i_clock(w_sys_clk), .i_clock_en(r_dma_clk_en  ), .o_clock(o_dma_clk  ) );
ck_gate u_uart_clk_gate   ( .i_clock(w_sys_clk), .i_clock_en(r_uart_clk_en ), .o_clock(o_uart_clk ) );
ck_gate u_sdio_clk_gate   ( .i_clock(w_sys_clk), .i_clock_en(r_sdio_clk_en ), .o_clock(o_sdio_clk ) );
ck_gate u_hsm_clk_gate    ( .i_clock(w_sys_clk), .i_clock_en(r_hsm_clk_en  ), .o_clock(o_hsm_clk  ) );

logic w_spi_pclk_en_sync,w_iic_pclk_en_sync,w_iis_pclk_en_sync,w_wdt_pclk_en_sync,w_wdt_extclk_en_sync;
logic w_timer_pclk_en_sync,w_timer_extclk_en_sync,w_rtc_pclk_en_sync,w_rtc_extclk_en_sync;
logic w_analog_pclk_en_sync,w_dma_pclk_en_sync, w_gpio_extclk_en_sync, w_gpio_pclk_en_sync, w_uart_pclk_en_sync;
logic w_lsp_rst_n;

reset_syn u_sys_rst_syn(
  .o_rst_n  (w_lsp_rst_n),
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (r_sys_rst_n)
);


signal_sync u_spi_pclk_en_sync (
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (w_lsp_rst_n),
  .i_signal (r_spi_pclk_en),
  .o_signal (w_spi_pclk_en_sync)
);

signal_sync u_iic_pclk_en_sync (
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (w_lsp_rst_n),
  .i_signal (r_iic_pclk_en),
  .o_signal (w_iic_pclk_en_sync)
);

signal_sync u_iis_pclk_en_sync (
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (w_lsp_rst_n),
  .i_signal (r_iis_pclk_en),
  .o_signal (w_iis_pclk_en_sync)
);

signal_sync u_wdt_pclk_en_sync (
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (w_lsp_rst_n),
  .i_signal (r_wdt_pclk_en),
  .o_signal (w_wdt_pclk_en_sync)
);

signal_sync u_wdt_extclk_en_sync (
  .i_clk    (w_wdt_div_extckout),
  .i_rst_n  (w_lsp_rst_n),
  .i_signal (r_wdt_extclk_en),
  .o_signal (w_wdt_extclk_en_sync)
);

signal_sync u_timer_pclk_en_sync (
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (w_lsp_rst_n),
  .i_signal (r_timer_pclk_en),
  .o_signal (w_timer_pclk_en_sync)
);

signal_sync u_timer_extclk_en_sync (
  .i_clk    (w_timer_div_extckout),
  .i_rst_n  (w_lsp_rst_n),
  .i_signal (r_timer_extclk_en),
  .o_signal (w_timer_extclk_en_sync)
);

signal_sync u_rtc_pclk_en_sync (
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (w_lsp_rst_n),
  .i_signal (r_rtc_pclk_en),
  .o_signal (w_rtc_pclk_en_sync)
);

signal_sync u_rtc_extclk_en_sync (
  .i_clk    (w_rtc_div_ckout),
  .i_rst_n  (w_lsp_rst_n),
  .i_signal (r_rtc_extclk_en),
  .o_signal (w_rtc_extclk_en_sync)
);

signal_sync u_analog_pclk_en_sync (
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (w_lsp_rst_n),
  .i_signal (r_analog_pclk_en),
  .o_signal (w_analog_pclk_en_sync)
);

signal_sync u_dma_pclk_en_sync (
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (w_lsp_rst_n),
  .i_signal (r_dma_pclk_en),
  .o_signal (w_dma_pclk_en_sync)
);

signal_sync u_uart_pclk_en_sync (
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (w_lsp_rst_n),
  .i_signal (r_uart_pclk_en),
  .o_signal (w_uart_pclk_en_sync)
);

signal_sync u_gpio_pclk_en_sync (
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (w_lsp_rst_n),
  .i_signal (r_gpio_pclk_en),
  .o_signal (w_gpio_pclk_en_sync)
);

signal_sync u_gpio_extclk_en_sync (
  .i_clk    (w_gpio_div_extckout),
  .i_rst_n  (w_lsp_rst_n),
  .i_signal (r_gpio_extclk_en),
  .o_signal (w_gpio_extclk_en_sync)
);




assign o_sys_pclk = w_lspdiv_ckout;

ck_gate u_spi_pclk_gate      ( .i_clock(w_lspdiv_ckout      ), .i_clock_en(w_spi_pclk_en_sync      ), .o_clock(o_spi_pclk    ) );
ck_gate u_iic_pclk_gate      ( .i_clock(w_lspdiv_ckout      ), .i_clock_en(w_iic_pclk_en_sync      ), .o_clock(o_iic_pclk    ) );
ck_gate u_iis_pclk_gate      ( .i_clock(w_lspdiv_ckout      ), .i_clock_en(w_iis_pclk_en_sync      ), .o_clock(o_iis_pclk    ) );
ck_gate u_wdt_pclk_gate      ( .i_clock(w_lspdiv_ckout      ), .i_clock_en(w_wdt_pclk_en_sync      ), .o_clock(o_wdt_pclk    ) );
ck_gate u_wdt_extclk_gate    ( .i_clock(w_wdt_div_extckout  ), .i_clock_en(w_wdt_extclk_en_sync    ), .o_clock(o_wdt_extclk  ) );
ck_gate u_timer_pclk_gate    ( .i_clock(w_lspdiv_ckout      ), .i_clock_en(w_timer_pclk_en_sync    ), .o_clock(o_timer_pclk  ) );
ck_gate u_timer_extclk_gate  ( .i_clock(w_timer_div_extckout), .i_clock_en(w_timer_extclk_en_sync  ), .o_clock(o_timer_extclk) );
ck_gate u_rtc_pclk_gate      ( .i_clock(w_lspdiv_ckout      ), .i_clock_en(w_rtc_pclk_en_sync      ), .o_clock(o_rtc_pclk    ) );
ck_gate u_rtc_extclk_gate    ( .i_clock(w_rtc_div_ckout     ), .i_clock_en(w_rtc_extclk_en_sync    ), .o_clock(o_rtc_extclk  ) );
ck_gate u_analog_pclk_gate   ( .i_clock(w_lspdiv_ckout      ), .i_clock_en(w_analog_pclk_en_sync   ), .o_clock(o_analog_pclk ) );
ck_gate u_dma_pclk_gate      ( .i_clock(w_lspdiv_ckout      ), .i_clock_en(w_dma_pclk_en_sync      ), .o_clock(o_dma_pclk    ) );
ck_gate u_uart_pclk_gate     ( .i_clock(w_lspdiv_ckout      ), .i_clock_en(w_uart_pclk_en_sync     ), .o_clock(o_uart_pclk   ) );
ck_gate u_gpio_pclk_gate     ( .i_clock(w_lspdiv_ckout      ), .i_clock_en(w_gpio_pclk_en_sync     ), .o_clock(o_gpio_pclk   ) );
ck_gate u_gpio_extclk_gate   ( .i_clock(w_gpio_div_extckout ), .i_clock_en(w_gpio_extclk_en_sync   ), .o_clock(o_gpio_extclk ) );




assign o_sys_clk = w_sys_clk;
logic w_sysctrl_rstn;
logic w_clk_rst_n;

ck_and u_pmu_rst  (.i_a(i_bor_n), .i_b(i_por_n), .out(w_pmu_rstn));
ck_and u_sysctrl_rst  (.i_a(i_por_n), .i_b(i_xrst_n), .out(w_sysctrl_rstn));


filter_rst u_sys_rst_flr(
  .clk     (w_osc1_clk_out),
  .i_rst_n (w_sysctrl_rstn),
  .i_din   (1'b1),
  .i_cfg   (1'b0),
  .o_dout  (w_clk_rst_n)
);



always @(posedge w_osc1_clk_out or negedge w_clk_rst_n)
begin
    if(!w_clk_rst_n) begin
        r_rst_cnt       <= 11'b0;
    end
    else if(~r_rst_cnt[10]) begin
        r_rst_cnt       <= r_rst_cnt + 11'b1;
    end
end

always @(posedge w_osc1_clk_out or negedge w_clk_rst_n)
begin
    if(!w_clk_rst_n) begin
        r_ckmux_rst_n       <= 1'b0;
    end
    else if(r_rst_cnt[9]&r_rst_cnt[8]&(~r_ckmux_rst_n)) begin
        r_ckmux_rst_n       <= 1'b1;
    end
end


filter_rst u_bor_flr(
  .clk     (w_sys_clk),
  .i_rst_n (r_rst_cnt[10]),
  .i_din   (i_bor_n),
  .i_cfg   (1'b0),
  .o_dout  (w_bor_n)
);

ck_and u_sys_rst0  (.i_a(w_bor_n), .i_b(r_rst_cnt[10]), .out(w_rst_n));
ck_inv u_wdtrst_inv   (.in(i_wdtrst), .out(w_wdtrst_n));
ck_and u_sys_rst1  (.i_a(w_rst_n), .i_b(w_wdtrst_n), .out(w_sys_rst_n));


assign r_sys_rst_n = i_rst_n;


assign o_sys_rst_n = r_sys_rst_n;


assign o_vref1p65_vsel_lv = r_vref1p65_vsel_lv;
assign o_reg_loadsel_lv   = r_reg_loadsel_lv;
assign o_vrefp25_vsel_lv  = r_vrefp25_vsel_lv;
assign o_ldo_pd_lv        = r_ldo_pd_lv;
assign o_reg_ldo_lv       = r_reg_ldo_lv;
assign o_bor_en_lv        = r_bor_en_lv;
assign o_reg_bor_lv       = r_reg_bor_lv;
assign o_clk_chop_lv      = r_clk_chop_lv;
assign o_vref12_en_lv     = r_vref12_en_lv;
assign o_vref1p65_en_lv   = r_vref1p65_en_lv;
assign o_vrefp25_en_lv    = r_vrefp25_en_lv;
assign o_hqbgp_tempadj_lv = r_hqbgp_tempadj_lv;
assign o_hqbgp_ctrim_lv   = r_hqbgp_ctrim_lv;
assign o_hqbgp_ftrim_lv   = r_hqbgp_ftrim_lv;


ck_and u_core0_rst    (.i_a(r_sys_rst_n), .i_b(r_core0_soft_reset  ), .out(o_core0_rst_n ));
ck_and u_core1_rst    (.i_a(r_sys_rst_n), .i_b(r_core1_soft_reset  ), .out(o_core1_rst_n ));
ck_and u_core2_rst    (.i_a(r_sys_rst_n), .i_b(r_core2_soft_reset  ), .out(o_core2_rst_n ));
ck_and u_core3_rst    (.i_a(r_sys_rst_n), .i_b(r_core3_soft_reset  ), .out(o_core3_rst_n ));
ck_and u_dpu_rst      (.i_a(r_sys_rst_n), .i_b(r_dpu_soft_reset    ), .out(o_dpu_rst_n   ));
ck_and u_dma_rst      (.i_a(r_sys_rst_n), .i_b(r_dma_soft_reset    ), .out(o_dma_rst_n   ));
ck_and u_uart_rst     (.i_a(r_sys_rst_n), .i_b(r_uart_soft_reset   ), .out(o_uart_rst_n  ));
ck_and u_sdio_rst     (.i_a(r_sys_rst_n), .i_b(r_sdio_soft_reset   ), .out(o_sdio_rst_n  ));
ck_and u_hsm_rst      (.i_a(r_sys_rst_n), .i_b(r_hsm_soft_reset    ), .out(o_hsm_rst_n   ));

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



ck_and u_spi_prst     (.i_a(r_sys_rst_n), .i_b(r_spi_soft_preset     ), .out(w_spi_prst_n    ));
ck_and u_iic_prst     (.i_a(r_sys_rst_n), .i_b(r_iic_soft_preset     ), .out(w_iic_prst_n    ));
ck_and u_iis_prst     (.i_a(r_sys_rst_n), .i_b(r_iis_soft_preset     ), .out(w_iis_prst_n    ));
ck_and u_wdt_prst     (.i_a(r_sys_rst_n), .i_b(r_wdt_soft_preset     ), .out(w_wdt_prst_n    ));
ck_and u_timer_prst   (.i_a(r_sys_rst_n), .i_b(r_timer_soft_preset   ), .out(w_timer_prst_n  ));
ck_and u_timer_extrst (.i_a(r_sys_rst_n), .i_b(r_timer_soft_extreset ), .out(w_timer_extrst_n));
ck_and u_rtc_prst     (.i_a(r_sys_rst_n), .i_b(r_rtc_soft_preset     ), .out(w_rtc_prst_n    ));
ck_and u_rtc_extrst   (.i_a(r_sys_rst_n), .i_b(r_rtc_soft_extreset   ), .out(w_rtc_extrst_n  ));
ck_and u_analog_prst  (.i_a(r_sys_rst_n), .i_b(r_analog_soft_preset  ), .out(w_analog_prst_n ));
ck_and u_dma_prst     (.i_a(r_sys_rst_n), .i_b(r_dma_soft_preset     ), .out(w_dma_prst_n    ));
ck_and u_uart_prst    (.i_a(r_sys_rst_n), .i_b(r_uart_soft_preset    ), .out(w_uart_prst_n   ));
ck_and u_gpio_prst    (.i_a(r_sys_rst_n), .i_b(r_gpio_soft_preset    ), .out(w_gpio_prst_n   ));

reset_syn u_sys_prst_syn(
  .o_rst_n  (o_sys_prst_n),
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (r_sys_rst_n)
);


reset_syn u_spi_prst_syn(
  .o_rst_n  (o_spi_prst_n),
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (w_spi_prst_n)
);

reset_syn u_iic_prst_syn(
  .o_rst_n  (o_iic_prst_n),
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (w_iic_prst_n)
);

reset_syn u_iis_prst_syn(
  .o_rst_n  (o_iis_prst_n),
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (w_iis_prst_n)
);

reset_syn u_wdt_prst_syn(
  .o_rst_n  (o_wdt_prst_n),
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (w_wdt_prst_n)
);

reset_syn u_timer_prst_syn(
  .o_rst_n  (o_timer_prst_n),
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (w_timer_prst_n)
);

reset_syn u_rtc_prst_syn(
  .o_rst_n  (o_rtc_prst_n),
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (w_rtc_prst_n)
);

reset_syn u_timer_extrst_syn(
  .o_rst_n  (o_timer_extrst_n),
  .i_clk    (w_timer_div_extckout),
  .i_rst_n  (w_timer_extrst_n)
);

reset_syn u_rtc_extrst_syn(
  .o_rst_n  (o_rtc_extrst_n),
  .i_clk    (w_rtc_div_ckout),
  .i_rst_n  (w_rtc_extrst_n)
);

reset_syn u_analog_prst_syn(
  .o_rst_n  (o_analog_prst_n),
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (w_analog_prst_n)
);

reset_syn u_dma_prst_syn(
  .o_rst_n  (o_dma_prst_n),
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (w_dma_prst_n)
);

reset_syn u_uart_prst_syn(
  .o_rst_n  (o_uart_prst_n),
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (w_uart_prst_n)
);

reset_syn u_gpio_prst_syn(
  .o_rst_n  (o_gpio_prst_n),
  .i_clk    (w_lspdiv_ckout),
  .i_rst_n  (w_gpio_prst_n)
);



assign o_core0_boot_addr       = r_core0_boot_addr;
assign o_core0_itcm_startAddr  = r_core0_itcm_startAddr;
assign o_core0_itcm_size       = r_core0_itcm_size;
assign o_core0_dtcm_startAddr  = r_core0_dtcm_startAddr;
assign o_core0_dtcm_size       = r_core0_dtcm_size;
assign o_core0_llpp_startAddr  = r_core0_llpp_startAddr;
assign o_core0_llpp_size       = r_core0_llpp_size;
assign o_core0_flash_startAddr = r_core0_flash_startAddr;
assign o_core0_flash_size      = r_core0_flash_size;
assign o_core1_boot_addr       = r_core1_boot_addr;
assign o_core1_itcm_startAddr  = r_core1_itcm_startAddr;
assign o_core1_itcm_size       = r_core1_itcm_size;
assign o_core1_dtcm_startAddr  = r_core1_dtcm_startAddr;
assign o_core1_dtcm_size       = r_core1_dtcm_size;
assign o_core1_llpp_startAddr  = r_core1_llpp_startAddr;
assign o_core1_llpp_size       = r_core1_llpp_size;
assign o_core1_flash_startAddr = r_core1_flash_startAddr;
assign o_core1_flash_size      = r_core1_flash_size;
assign o_core2_boot_addr       = r_core2_boot_addr;
assign o_core2_itcm_startAddr  = r_core2_itcm_startAddr;
assign o_core2_itcm_size       = r_core2_itcm_size;
assign o_core2_dtcm_startAddr  = r_core2_dtcm_startAddr;
assign o_core2_dtcm_size       = r_core2_dtcm_size;
assign o_core2_llpp_startAddr  = r_core2_llpp_startAddr;
assign o_core2_llpp_size       = r_core2_llpp_size;
assign o_core2_flash_startAddr = r_core2_flash_startAddr;
assign o_core2_flash_size      = r_core2_flash_size;
assign o_core3_boot_addr       = r_core3_boot_addr;
assign o_core3_itcm_startAddr  = r_core3_itcm_startAddr;
assign o_core3_itcm_size       = r_core3_itcm_size;
assign o_core3_dtcm_startAddr  = r_core3_dtcm_startAddr;
assign o_core3_dtcm_size       = r_core3_dtcm_size;
assign o_core3_llpp_startAddr  = r_core3_llpp_startAddr;
assign o_core3_llpp_size       = r_core3_llpp_size;
assign o_core3_flash_startAddr = r_core3_flash_startAddr;
assign o_core3_flash_size      = r_core3_flash_size;




endmodule

