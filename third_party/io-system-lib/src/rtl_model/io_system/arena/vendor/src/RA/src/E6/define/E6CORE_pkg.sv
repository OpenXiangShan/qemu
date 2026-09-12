 
 
 
 
 
`define RV_INST_ACCESS_MASK5 'hffffffff
`define RV_DATA_ACCESS_ENABLE4 1'h0
`define RV_INST_ACCESS_ENABLE3 1'h0
`define RV_INST_ACCESS_ENABLE0 1'h0
`define RV_INST_ACCESS_MASK3 'hffffffff
`define RV_DATA_ACCESS_ENABLE5 1'h0
`define RV_DATA_ACCESS_MASK5 'hffffffff
`define RV_DATA_ACCESS_ADDR3 'h00000000
`define RV_INST_ACCESS_ENABLE7 1'h0
`define RV_DATA_ACCESS_ADDR6 'h00000000
`define RV_INST_ACCESS_MASK7 'hffffffff
`define RV_INST_ACCESS_ENABLE6 1'h0
`define RV_INST_ACCESS_ENABLE5 1'h0
`define RV_DATA_ACCESS_ADDR4 'h00000000
`define RV_DATA_ACCESS_ADDR7 'h00000000
`define RV_DATA_ACCESS_MASK3 'hffffffff
`define RV_INST_ACCESS_MASK4 'hffffffff
`define RV_DATA_ACCESS_ADDR1 'h00000000
`define RV_INST_ACCESS_ADDR4 'h00000000
`define RV_INST_ACCESS_ADDR3 'h00000000
`define RV_DATA_ACCESS_ENABLE1 1'h0
`define RV_DATA_ACCESS_ADDR0 'h00000000
`define RV_DATA_ACCESS_MASK0 'hffffffff
`define RV_DATA_ACCESS_MASK6 'hffffffff
`define RV_INST_ACCESS_ADDR7 'h00000000
`define RV_INST_ACCESS_MASK0 'hffffffff
`define RV_DATA_ACCESS_ADDR5 'h00000000
`define RV_DATA_ACCESS_ADDR2 'h00000000
`define RV_DATA_ACCESS_MASK4 'hffffffff
`define RV_DATA_ACCESS_MASK1 'hffffffff
`define RV_INST_ACCESS_ADDR0 'h00000000
`define RV_INST_ACCESS_ADDR2 'h00000000
`define RV_DATA_ACCESS_ENABLE0 1'h0
`define RV_DATA_ACCESS_ENABLE2 1'h0
`define RV_DATA_ACCESS_ENABLE7 1'h0
`define RV_INST_ACCESS_ENABLE4 1'h0
`define RV_DATA_ACCESS_MASK7 'hffffffff
`define RV_INST_ACCESS_ADDR5 'h00000000
`define RV_DATA_ACCESS_MASK2 'hffffffff
`define RV_INST_ACCESS_ENABLE1 1'h0
`define RV_INST_ACCESS_MASK6 'hffffffff
`define RV_DATA_ACCESS_ENABLE3 1'h0
`define RV_INST_ACCESS_ADDR6 'h00000000
`define RV_INST_ACCESS_MASK2 'hffffffff
`define RV_INST_ACCESS_ENABLE2 1'h0
`define RV_DATA_ACCESS_ENABLE6 1'h0
`define RV_INST_ACCESS_ADDR1 'h00000000
`define RV_INST_ACCESS_MASK1 'hffffffff
`define RV_DEC_INSTBUF_DEPTH 4
`define RV_DMA_BUF_DEPTH 4
`define RV_LSU_NUM_NBLOAD 8
`define RV_LSU_STBUF_DEPTH 8
`define RV_LSU_NUM_NBLOAD_WIDTH 3
`define RV_IFU_BUS_TAG 3
`define RV_LSU_BUS_TAG 5
`define RV_SB_BUS_TAG 1
`define RV_DMA_BUS_TAG 1
`define RV_DCCM_WIDTH_BITS 2
`define RV_DCCM_RESERVED 'h1000
`define RV_DCCM_SIZE 32
`define RV_DCCM_DATA_WIDTH 32
`define RV_DCCM_SIZE_32 
`define RV_DCCM_NUM_BANKS_8 
`define RV_DCCM_FDATA_WIDTH 39
`define RV_DCCM_BYTE_WIDTH 4
`define RV_DCCM_DATA_CELL ram_1024x39
`define RV_DCCM_ENABLE 1
`define RV_DCCM_BITS 15
`define RV_DCCM_ECC_WIDTH 7
`define RV_DCCM_ROWS 1024
`define RV_DCCM_BANK_BITS 3
`define RV_DCCM_NUM_BANKS 8
`define RV_DCCM_INDEX_BITS 10
`define RV_LSU_SB_BITS 15
`define RV_RESET_VEC 'h80000000
`define RV_RET_STACK_SIZE 4
`define RV_XLEN 32
`define RV_TARGET default_pd
`define RV_BTB_BTAG_FOLD 1
`define RV_BTB_INDEX3_HI 15
`define RV_BTB_INDEX1_LO 4
`define RV_BTB_ADDR_HI 7
`define RV_BTB_ADDR_LO 4
`define RV_BTB_INDEX1_HI 7
`define RV_BTB_INDEX2_HI 11
`define RV_BTB_INDEX2_LO 8
`define RV_BTB_ARRAY_DEPTH 16
`define RV_BTB_BTAG_SIZE 7
`define RV_BTB_SIZE 128
`define RV_BTB_INDEX3_LO 12
`define RV_ICCM_NUM_BANKS 8
`define RV_ICCM_ENABLE 1
`define RV_ICCM_BITS 16
`define RV_ICCM_BANK_BITS 3
`define RV_ICCM_ROWS 2048
`define RV_ICCM_OFFSET 10'he000000

 
 
 
 
 
 
 
 

 
`define RV_ICCM_REGION 4'h0
`define RV_ICCM_SADR 32'h00400000
`define RV_ICCM_EADR 32'h0040ffff
`define RV_DCCM_REGION 4'h0
`define RV_DCCM_OFFSET 28'h80000
`define RV_DCCM_SADR 32'h00800000
`define RV_DCCM_EADR 32'h0080ffff

`define RV_ICCM_RESERVED 'h1000
`define RV_ICCM_DATA_CELL ram_2048x39
`define RV_ICCM_INDEX_BITS 11
`define RV_ICCM_NUM_BANKS_8 
`define RV_ICCM_SIZE 64
`define RV_ICCM_SIZE_64 
`define RV_ICACHE_SIZE 32
`define RV_ICACHE_ECC 1
`define RV_ICACHE_TAG_HIGH 13
`define RV_ICACHE_IC_ROWS 512
`define RV_ICACHE_TADDR_HIGH 6
`define RV_ICACHE_TAG_LOW 6
`define RV_ICACHE_TAG_CELL ram_128x25
`define RV_ICACHE_IC_DEPTH 9
`define RV_ICACHE_IC_INDEX 9
`define RV_ICACHE_ENABLE 1
`define RV_ICACHE_DATA_CELL ram_512x39
`define RV_ICACHE_TAG_DEPTH 128
`define RV_UNUSED_REGION7 'h70000000
`define RV_UNUSED_REGION4 'h40000000
`define RV_UNUSED_REGION9 'h90000000
`define RV_SERIALIO 'hd0580000
`define RV_UNUSED_REGION2 'h20000000
`define RV_EXTERNAL_PROG 'hb0000000
`define RV_UNUSED_REGION1 'h10000000
`define RV_UNUSED_REGION0 'h00000000
`define RV_UNUSED_REGION3 'h30000000
`define RV_UNUSED_REGION5 'h50000000
`define RV_EXTERNAL_DATA_1 'h00000000
`define RV_DEBUG_SB_MEM 'hb0580000
`define RV_UNUSED_REGION6 'h60000000
`define RV_EXTERNAL_DATA 'hc0580000
`define RV_NMI_VEC 'h11110000
`define RV_BHT_HASH_STRING {ghr[3:3] ^ ghr[3+1],hashin[6:4]^ghr[3-1:0]}
`define RV_BHT_ADDR_HI 7
`define RV_BHT_GHR_RANGE 4:0
`define RV_BHT_GHR_SIZE 5
`define RV_BHT_GHR_PAD2 fghr[4:3],2'b0
`define RV_BHT_SIZE 128
`define RV_BHT_ADDR_LO 4
`define RV_BHT_ARRAY_DEPTH 16
`define RV_BHT_GHR_PAD fghr[4],3'b0

`define RV_NUMIREGS 32
`define RV_PIC_MEIPL_MASK 'hf
`define RV_PIC_MEIP_COUNT 4
`define RV_PIC_BITS 15
`define RV_PIC_REGION 4'hf
`define RV_PIC_INT_WORDS 1
`define RV_PIC_MEIE_COUNT 8
`define RV_PIC_MPICCFG_MASK 'h1
`define RV_PIC_TOTAL_INT_PLUS1 9
`define RV_PIC_MEIGWCTRL_MASK 'h3
`define RV_PIC_MEIP_OFFSET 'h1000
`define RV_PIC_BASE_ADDR 32'hf00c0000
`define RV_PIC_MEIPT_MASK 'h0
`define RV_PIC_MEIGWCTRL_OFFSET 'h4000
`define RV_PIC_MEIPL_OFFSET 'h0000
`define RV_PIC_MEIPT_COUNT 8
`define RV_PIC_TOTAL_INT 8
`define RV_PIC_MEIGWCLR_COUNT 8
`define RV_PIC_MPICCFG_COUNT 1
`define RV_PIC_SIZE 32
`define RV_PIC_MEIGWCTRL_COUNT 8
`define RV_PIC_MEIE_OFFSET 'h2000
`define RV_PIC_MEIPL_COUNT 8
`define RV_PIC_OFFSET 10'hc0000
`define RV_PIC_MEIP_MASK 'h0
`define RV_PIC_MEIPT_OFFSET 'h3004
`define RV_PIC_MEIGWCLR_MASK 'h0
`define RV_PIC_MEIE_MASK 'h1
`define RV_PIC_MPICCFG_OFFSET 'h3000
`define RV_PIC_MEIGWCLR_OFFSET 'h5000
`define CLOCK_PERIOD 100
`define CPU_TOP `RV_TOP.veer
`define TOP tb_top
`define RV_BUILD_AXI4 1
`define RV_TOP `TOP.rvtop
`define DATAWIDTH 64
`define RV_STERR_ROLLBACK 0
`define RV_EXT_ADDRWIDTH 32
`define RV_EXT_DATAWIDTH 64
`define SDVT_AHB 1
`define RV_LDERR_ROLLBACK 1
 
`define TEC_RV_ICG clockhdr
`define REGWIDTH 32
`define E5_DTCM_SADR 32'h00800000
`define E5_DTCM_EADR 32'h0080FFFF
 
 
 
 
 
package E6CORE_pkg;

`define D_extension 
 
 
 
`define RESET_PC     0
`define ROB_DEEP     16
`define TAG_WIDTH    4
`define FP_ROB_DEEP  4
`define VLEN         64
`define VSTART_WIDTH 32
`define VXSAT_WIDTH  1
`define VXRM_WIDTH   2
`define VCSR_WIDTH   3
`define VL_WIDTH     32
`define VTYPE_WIDTH  8
`define VLENB_WIDTH  32

`define PMPNumRegions 32 
 
 

typedef struct packed { 
  logic valid ; 
  logic sin ;
  logic cos ;
}cordic_pkt_t;

typedef struct packed {
                      logic valid                  ; 
                    
                      logic vpu                    ;
                      logic vs1                    ;
                      logic vs2                    ;
                      logic vs3                    ;
                      logic rd                     ;
                      logic fd                     ;
                      logic vd                     ;
                      logic vm                     ;
                      logic widen                  ;
                      logic narrow                 ;
                      logic vset                   ;
                      logic varith                 ;
                      logic vload                  ;
                      logic vstore                 ;
                      logic vsetvli                ;
                      logic vsetivli               ;
                      logic vsetvl                 ;
                      logic unit_stride            ;
                      logic strided                ;
                      logic indexed                ;
                      logic OPIVV                  ;
                      logic OPFVV                  ;
                      logic OPMVV                  ;
                      logic OPIVI                  ;
                      logic OPIVX                  ;
                      logic OPFVF                  ;
                      logic OPMVX                  ;
                      logic vadd                   ;
                      logic vsub                   ;
                      logic vrsub                  ;
                      logic vminu                  ;
                      logic vmin                   ;
                      logic vmaxu                  ;
                      logic vmax                   ;
                      logic vand                   ;
                      logic vor                    ;
                      logic vxor                   ;
                      logic vrgather               ;
                      logic vslideup               ;
                      logic vrgatherei16           ;
                      logic vslidedown             ;
                      logic vadc                   ;
                      logic vmadc                  ;
                      logic vsbc                   ;
                      logic vmsbc                  ;
                      logic vmerge                 ;
                      logic vmv                    ;
                      logic vmseq                  ;
                      logic vmsne                  ;
                      logic vmsltu                 ;
                      logic vmslt                  ;
                      logic vmsleu                 ;
                      logic vmsle                  ;
                      logic vmsgtu                 ;
                      logic vmsgt                  ;
                      logic vsaddu                 ;
                      logic vsadd                  ;
                      logic vssubu                 ;
                      logic vssub                  ;
                      logic vsll                   ;
                      logic vsmul                  ;
                      logic vmv1r                  ;
                      logic vmv2r                  ;
                      logic vmv4r                  ;
                      logic vmv8r                  ;
                      logic vsrl                   ;
                      logic vsra                   ;
                      logic vssrl                  ;
                      logic vssra                  ;
                      logic vnsrl                  ;
                      logic vnsra                  ;
                      logic vnclipu                ;
                      logic vnclip                 ;
                      logic vwredsumu              ;
                      logic vwredsum               ;
                      logic vredsum                ;
                      logic vredand                ;
                      logic vredor                 ;
                      logic vredxor                ;
                      logic vredminu               ;
                      logic vredmin                ;
                      logic vredmaxu               ;
                      logic vredmax                ;
                      logic vaaddu                 ;
                      logic vaadd                  ;
                      logic vasubu                 ;
                      logic vasub                  ;
                      logic vslide1up              ;
                      logic vslide1down            ;
                      logic VWXUNARY0              ;
                      logic VRXUNARY0              ;
                      logic VXUNARY0               ;
                      logic VMUNARY0               ;
                      logic vcompress              ;
                      logic vmandnot               ;
                      logic vmand                  ;
                      logic vmor                   ;
                      logic vmxor                  ;
                      logic vmornot                ;
                      logic vmnand                 ;
                      logic vmnor                  ;
                      logic vmxnor                 ;
                      logic vdivu                  ;
                      logic vdiv                   ;
                      logic vremu                  ;
                      logic vrem                   ;
                      logic vmulhu                 ;
                      logic vmul                   ;
                      logic vmulhsu                ;
                      logic vmulh                  ;
                      logic vmadd                  ;
                      logic vnmsub                 ;
                      logic vmacc                  ;
                      logic vnmsac                 ;
                      logic vwaddu                 ;
                      logic vwadd                  ;
                      logic vwsubu                 ;
                      logic vwsub                  ;
                      logic vwaddu_w               ;
                      logic vwadd_w                ;
                      logic vwsubu_w               ;
                      logic vwsub_w                ;
                      logic vwmulu                 ;
                      logic vwmulsu                ;
                      logic vwmul                  ;
                      logic vwmaccu                ;
                      logic vwmacc                 ;
                      logic vwmaccus               ;
                      logic vwmaccsu               ;
                      logic vfadd                  ;
                      logic vfredusum              ;
                      logic vfsub                  ;
                      logic vfredosum              ;
                      logic vfmin                  ;
                      logic vfredmin               ;
                      logic vfmax                  ;
                      logic vfredmax               ;
                      logic vfsgnj                 ;
                      logic vfsgnjn                ;
                      logic vfsgnjx                ;
                      logic vfslide1up             ;
                      logic vfslide1down           ;
                      logic VWFUNARY0              ;
                      logic VRFUNARY0              ;
                      logic VFUNARY0               ;
                      logic VFUNARY1               ;
                      logic vfmerge                ;
                      logic vfmv                   ;
                      logic vmfeq                  ;
                      logic vmfle                  ;
                      logic vmflt                  ;
                      logic vmfne                  ;
                      logic vmfgt                  ;
                      logic vmfge                  ;
                      logic vfdiv                  ;
                      logic vfrdiv                 ;
                      logic vfmul                  ;
                      logic vfrsub                 ;
                      logic vfmadd                 ;
                      logic vfnmadd                ;
                      logic vfmsub                 ;
                      logic vfnmsub                ;
                      logic vfmacc                 ;
                      logic vfnmacc                ;
                      logic vfmsac                 ;
                      logic vfnmsac                ;
                      logic vfwadd                 ;
                      logic vfwredusum             ;
                      logic vfwsub                 ;
                      logic vfwredosum             ;
                      logic vfwadd_w               ;
                      logic vfwsub_w               ;
                      logic vfwmul                 ;
                      logic vfwmacc                ;
                      logic vfwnmacc               ;
                      logic vfwmsac                ;
                      logic vfwnmsac               ;
                      logic vmv_s_x                ;
                      logic vmv_x_s                ;
                      logic vpopc                  ;
                      logic vfirst                 ;
                      logic vzext_vf8              ;
                      logic vsext_vf8              ;
                      logic vzext_vf4              ;
                      logic vsext_vf4              ;
                      logic vzext_vf2              ;
                      logic vsext_vf2              ;
                      logic vfmv_s_f               ;
                      logic vfmv_f_s               ;
                      logic vfcvt_xu_f_v           ;
                      logic vfcvt_x_f_v            ;
                      logic vfcvt_f_xu_v           ;
                      logic vfcvt_f_x_v            ;
                      logic vfcvt_rtz_xu_f_v       ;
                      logic vfcvt_rtz_x_f_v        ;
                      logic vfwcvt_xu_f_v          ;
                      logic vfwcvt_x_f_v           ;
                      logic vfwcvt_f_xu_v          ;
                      logic vfwcvt_f_x_v           ;
                      logic vfwcvt_f_f_v           ;
                      logic vfwcvt_rtz_xu_f_v      ;
                      logic vfwcvt_rtz_x_f_v       ;
                      logic vfncvt_xu_f_w          ;
                      logic vfncvt_x_f_w           ;
                      logic vfncvt_f_xu_w          ;
                      logic vfncvt_f_x_w           ;
                      logic vfncvt_f_f_w           ;
                      logic vfncvt_rod_f_f_w       ;
                      logic vfncvt_rtz_xu_f_w      ;
                      logic vfncvt_rtz_x_f_w       ;
                      logic vfsqrt_v               ;
                      logic vfrsqrt7_v             ;
                      logic vfrec7_v               ;
                      logic vfclass_v              ;
                      logic vmsbf                  ;
                      logic vmsof                  ;
                      logic vmsif                  ;
                      logic viota                  ;
                      logic vid                    ;
                   
                      } vpu_pkt_t;

typedef struct packed {
                        logic valid;  
                         
                         
                        logic fpu_addmul  ;
                        logic fpu_divsqrt ;
                        logic fpu_noncomp ;
                        logic fpu_conv    ;
                        logic fs1 ;
                        logic fs2 ;
                        logic fs3 ;
                        logic rs1 ;
                        logic rs2 ;
                        logic fcsr ;
                        logic fd ;
                        logic rd ;
                        logic f_extension ;
                        logic f_int64 ;
                        logic fp_fmadd ;
                        logic fp_fmsub ;
                        logic fp_fnmsub ;
                        logic fp_fnmadd ;
                        logic fp_add ;
                        logic fp_fmul ;
                        logic fp_fdiv ;
                        logic fp_fsqrt ;
                        logic fp_fsgnj ;
                        logic fp_fsgnjn ;
                        logic fp_fsgnjx ;
                        logic fp_min ;
                        logic fp_max ;
                        logic fp_feq ;
                        logic fp_flt ;
                        logic fp_fle ;
                        logic fp_fclass ;
                        logic fp_fcvt_f2i ;
                        logic fp_sign ;
                        logic fp_fmv ;
                        logic fp_fcvt_i2f ;
                        logic fp16_extension;
                        logic fp_fcvt_s_h;
                        logic fp_fcvt_h_s;
                        logic fp_fcvt_d_h;
                        logic fp_fcvt_h_d;
                         

                         
                        logic d_extension ;
                        logic fp_fcvt_s_d ;
                        logic fp_fcvt_d_s ;   
                         
                      } fpu_pkt_t;

 
typedef enum logic [4:0] {
                            ROB_EXP_INSTR_MIS = 5'b0_0000, 
                            ROB_EXP_INSTR_ACCESS_FAULT = 5'b0_0001, 
                            ROB_EXP_ILLEGAL_INSTR = 5'b0_0010, 
                            ROB_EXP_BREAKPOINT = 5'b0_0011, 
                            ROB_EXP_LOAD_MIS = 5'b0_0100, 
                            ROB_EXP_LOAD_ACCESS_FAULT = 5'b0_0101, 
                            ROB_EXP_STORE_AMO_MIS = 5'b0_0110, 
                            ROB_EXP_STORE_AMO_ACCESS_FAULT = 5'b0_0111, 
                            ROB_EXP_ECALL_U = 5'b0_1000, 
                            ROB_EXP_ECALL_S = 5'b0_1001, 
                            ROB_EXP_ECALL_M = 5'b0_1011, 
                            ROB_EXP_NORMAL = 5'b1_0000  
} rob_entry_exception_t;

 
typedef enum logic [1:0] {
  PMP_MODE_OFF   = 2'b00,
  PMP_MODE_TOR   = 2'b01,
  PMP_MODE_NA4   = 2'b10,
  PMP_MODE_NAPOT = 2'b11
} pmp_cfg_mode_e;

typedef struct packed {
  logic          lock;
  pmp_cfg_mode_e mode;
  logic          exec;
  logic          write;
  logic          read;
} pmp_cfg_t;

typedef struct packed {
  logic          writethrough;
  logic          writeallocate;
  logic          readallocate;
  logic          cacheable;
} pma_cfg_t;

 
typedef enum logic [4:0] {
                            ROB_INT_S_SOFTWARE = 5'b0_0001, 
                            ROB_INT_M_SOFTWARE = 5'b0_0011, 
                            ROB_INT_S_TIMER = 5'b0_0101, 
                            ROB_INT_M_TIMER = 5'b0_0111, 
                            ROB_INT_S_EXTERNAL = 5'b0_1001, 
                            ROB_INT_M_EXTERNAL = 5'b0_1011, 
                            ROB_INT_COUNTER_OVER = 5'b0_1101 
} rob_entry_interrupt_t;

typedef enum logic [1:0] {
                           ROB_STATE_EMPTY = 2'b00,
                           ROB_STATE_WAIT  = 2'b01,
                           ROB_STATE_VALID = 2'b10
} rob_entry_state_t;

 
 
typedef enum logic [3:0] {
                           ROB_TYPE_IDLE     = 4'b0000, 
                           ROB_TYPE_RD       = 4'b0001,
                           ROB_TYPE_JUMP     = 4'b0010,
                           ROB_TYPE_CSRWRITE = 4'b0011,   
                           ROB_TYPE_LOAD     = 4'b0100,
                           ROB_TYPE_STORE    = 4'b0101,
                           ROB_TYPE_BRANCH   = 4'b0110,
                           ROB_TYPE_FD       = 4'b0111,
                           ROB_TYPE_F_LOAD   = 4'b1000,
                           ROB_TYPE_VSET     = 4'b1001,
                           ROB_TYPE_VD       = 4'b1010,
                           ROB_TYPE_V_LOAD   = 4'b1011
} rob_entry_type_t;

typedef enum logic [3:0] {
                           ROB_SYS_TYPE_IDLE     = 4'b0000, 
                           ROB_SYS_TYPE_EBREAK   = 4'b0001,
                           ROB_SYS_TYPE_ECALL    = 4'b0010,
                           ROB_SYS_TYPE_MRET     = 4'b0011,
                           ROB_SYS_TYPE_SRET     = 4'b0100,
                           ROB_SYS_TYPE_FENCE    = 4'b0101,
                           ROB_SYS_TYPE_FENCEI   = 4'b0110,
                           ROB_SYS_TYPE_DRET     = 4'b0111,
                           ROB_SYS_TYPE_WFI      = 4'b1000
} rob_entry_system_type_t;

 
typedef struct packed {
                       logic[4:0]  exception;
                       logic[3:0]  instr_type;
                       logic[4:0]  addr;
                       logic[31:0] result;
                       logic[31:0] pc;
                     
} rob_pkt_t;



 
typedef struct packed {
                       logic [2:0] trace_rv_i_valid_ip;
                       logic [95:0] trace_rv_i_insn_ip;
                       logic [(64*3)-1:0] trace_rv_i_address_ip;
                       logic [2:0] trace_rv_i_exception_ip;
                       logic [4:0] trace_rv_i_ecause_ip;
                       logic [2:0] trace_rv_i_interrupt_ip;
                       
                       logic [63:0] trace_rv_i_tval_ip;
                       } trace_pkt_t;


typedef enum logic [3:0] {
                          NULL     = 4'b0000,
                          MUL      = 4'b0001,
                          LOAD     = 4'b0010,
                          STORE    = 4'b0011,
                          ALU      = 4'b0100,
                          CSRREAD  = 4'b0101,
                          CSRWRITE = 4'b0110,
                          CSRRW    = 4'b0111,
                          EBREAK   = 4'b1000,
                          ECALL    = 4'b1001,
                          FENCE    = 4'b1010,
                          FENCEI   = 4'b1011,
                          MRET     = 4'b1100,
                          CONDBR   = 4'b1101,
                          JAL      = 4'b1110
                          } inst_t;

typedef struct packed {
`ifdef RV_ICACHE_ECC
                       
                       logic [27:0] ecc;
`else
                       logic [7:0] parity;
`endif
                       } icache_err_pkt_t;

typedef struct packed {
                       logic valid;
                       logic wb;
                       logic [`RV_LSU_NUM_NBLOAD_WIDTH-1:0] tag;
                       logic [4:0] rd;
                       } load_cam_pkt_t;

typedef struct packed {
                       logic pc0_call;
                       logic pc0_ret;
                       logic pc0_pc4;
                       logic pc1_call;
                       logic pc1_ret;
                       logic pc1_pc4;
                       } rets_pkt_t;
typedef struct packed {
                       logic valid;
                       logic [11:0] toffset;
                       logic [1:0] hist;
                       logic br_error;
                       logic br_start_error;
                       logic [`RV_BTB_ADDR_HI:`RV_BTB_ADDR_LO] index;
                       logic [1:0] bank;
                       
                       logic [63:1] prett;   
                       logic [`RV_BHT_GHR_RANGE] fghr;
`ifdef RV_BTB_48
                       logic [1:0] way;
`else
                       logic way;
`endif
                       logic ret;
                       logic [`RV_BTB_BTAG_SIZE-1:0] btag;
                       } br_pkt_t;

typedef struct packed {
                       logic valid;
                       logic [1:0] hist;
                       logic br_error;
                       logic br_start_error;
                       logic [`RV_BTB_ADDR_HI:`RV_BTB_ADDR_LO] index;
                       logic [1:0] bank;
                       logic [`RV_BHT_GHR_RANGE] fghr;
`ifdef RV_BTB_48
                       logic [1:0] way;
`else
                       logic way;
`endif
                       logic middle;
                       } br_tlu_pkt_t;

typedef struct packed {
                       logic misp;
                       logic ataken;
                       logic boffset;
                       logic pc4;
                       logic [1:0] hist;
                       logic [11:0] toffset;
                       logic [`RV_BTB_ADDR_HI:`RV_BTB_ADDR_LO] index;
                       logic [1:0] bank;
                       logic valid;
                       logic br_error;
                       logic br_start_error;
                       logic [31:1] prett;
                       logic pcall;
                       logic pret;
                       logic pja;
                       logic [`RV_BTB_BTAG_SIZE-1:0] btag;
                       logic [`RV_BHT_GHR_RANGE] fghr;
`ifdef RV_BTB_48
                       logic [1:0] way;
`else
                       logic way;
`endif
                       } predict_pkt_t;

typedef struct packed {
                       logic legal;
                       logic icaf;
                       logic icaf_second;
                       logic perr;
                       logic sbecc;
                       logic fence_i;
                       logic [3:0] i0trigger;
                       logic [3:0] i1trigger;
                       inst_t pmu_i0_itype;         
                       inst_t pmu_i1_itype;         
                       logic pmu_i0_br_unpred;      
                       logic pmu_i1_br_unpred;      
                       logic pmu_divide;
                       logic pmu_lsu_misaligned;
                       } trap_pkt_t;

typedef struct packed {
                       logic [4:0] i0rd;
                       logic i0mul;
                       logic i0load;
                       logic i0store;
                       logic i0div;
                       logic i0v;
                       logic i0valid;
                       logic i0secondary;
                       logic [1:0] i0rs1bype2;
                       logic [1:0] i0rs2bype2;
                       logic [3:0] i0rs1bype3;
                       logic [3:0] i0rs2bype3;
                       logic [4:0] i1rd;
                       logic i1mul;
                       logic i1load;
                       logic i1store;
                       logic i1v;
                       logic i1valid;
                       logic csrwen;
                       logic csrwonly;
                       logic [11:0] csrwaddr;
                       logic i1secondary;
                       logic [1:0] i1rs1bype2;
                       logic [1:0] i1rs2bype2;
                       logic [6:0] i1rs1bype3;
                       logic [6:0] i1rs2bype3;
                       } dest_pkt_t;

typedef struct packed {
                       logic mul;
                       logic load;
                       logic sec;
                       logic alu;
                       } class_pkt_t;

typedef struct packed {
                       logic [4:0] rs1;
                       logic [4:0] rs2;
                       logic [4:0] rd;
                       } reg_pkt_t;


typedef struct packed {
                       logic valid;
                       logic land;
                       logic lor;
                       logic lxor;
                       logic sll;
                       logic srl;
                       logic sra;
                       logic beq;
                       logic bne;
                       logic blt;
                       logic bge;
                       logic add;
                       logic sub;
                       logic slt;
                       logic unsign;
                       logic jal;
                       logic predict_t;
                       logic predict_nt;
                       logic csr_write;
                       logic csr_imm;
                       logic pc4;
                       logic pc2;

                     
                         
                        logic b_andn                     ;
                        logic b_clmul                    ;
                        logic b_clmulh                   ;
                        logic b_clmulr                   ;
                        logic b_clz                      ;
                         
                        logic b_cpop                     ;
                         
                        logic b_ctz                      ;
                         
                        logic b_max                      ;
                        logic b_maxu                     ;
                        logic b_min                      ;
                        logic b_minu                     ;
                        logic b_orc_b                    ;
                        logic b_orn                      ;
                        logic b_rev8                     ;
                        logic b_rol                      ;
                         
                        logic b_ror                      ;
                        logic b_rori                     ;
                         
                         
                        logic b_bclr                     ;
                        logic b_bclri                    ;
                        logic b_bext                     ;
                        logic b_bexti                    ;
                        logic b_binv                     ;
                        logic b_binvi                    ;
                        logic b_bset                     ;
                        logic b_bseti                    ;
                        logic b_sext_b                   ;
                        logic b_sext_h                   ;
                        logic b_sh1add                   ;
                         
                        logic b_sh2add                   ;
                         
                        logic b_sh3add                   ;
                         
                         
                        logic b_xnor                     ;
                        logic b_zext_h                   ;

                        logic b_pack                     ;
                        logic b_packh                    ;
                         
                        logic b_rev_b                    ;
                        logic b_zip                      ;
                        logic b_unzip                    ;
                        logic b_xperm_n                  ;
                        logic b_xperm_b                  ; 
                   
                       } alu_pkt_t;

typedef struct packed {
                       logic amoValid;
                       logic [4:0] amoFunc;
                       logic vectorValid;
                       
                       
                       
                       
                       logic unit_stride;
                       logic stride;
                       logic indexed;
                       logic fd;
                       logic by;
                       logic half;
                       logic word;
                       logic dword;   
                       logic load;
                       logic store;
                       logic unsign;
                       logic dma;     
                       logic store_data_bypass_c1;
                       logic load_ldst_bypass_c1;
                       logic store_data_bypass_c2;
                       logic store_data_bypass_i0_e2_c2;
                       logic [1:0] store_data_bypass_e4_c1;
                       logic [1:0] store_data_bypass_e4_c2;
                       logic [1:0] store_data_bypass_e4_c3;
                       logic valid;
                       } lsu_pkt_t;

typedef struct packed {
                      logic exc_valid;
                      logic single_ecc_error;
                      logic inst_type;    
                      logic inst_pipe;    
                      logic dma_valid;
                      logic exc_type;     
                      logic [31:0] addr;
                      } lsu_error_pkt_t;

typedef struct packed {
                     
                     
                     
                     
                     
                     
                        logic cbo_clean ;
                        logic cbo_flush  ;
                        logic cbo_inval  ;
                        logic cbo_zero   ;
                         
                         
                         
                     
                     
                        logic a_extension                ;
                     

                     
                         
                        logic bit_extension              ;
                        logic b_shadd_uw                 ;
                        logic b_add_uw                   ;
                        logic b_slli_uw                  ;
                        logic b_andn                     ;
                        logic b_clmul                    ;
                        logic b_clmulh                   ;
                        logic b_clmulr                   ;
                        logic b_clz                      ;
                         
                        logic b_cpop                     ;
                         
                        logic b_ctz                      ;
                         
                        logic b_max                      ;
                        logic b_maxu                     ;
                        logic b_min                      ;
                        logic b_minu                     ;
                        logic b_orc_b                    ;
                        logic b_orn                      ;
                        logic b_rev8                     ;
                        logic b_rol                      ;
                         
                        logic b_ror                      ;
                        logic b_rori                     ;
                         
                         
                        logic b_bclr                     ;
                        logic b_bclri                    ;
                        logic b_bext                     ;
                        logic b_bexti                    ;
                        logic b_binv                     ;
                        logic b_binvi                    ;
                        logic b_bset                     ;
                        logic b_bseti                    ;
                        logic b_sext_b                   ;
                        logic b_sext_h                   ;
                        logic b_sh1add                   ;
                         
                        logic b_sh2add                   ;
                         
                        logic b_sh3add                   ;
                         
                         
                        logic b_xnor                     ;
                        logic b_zext_h                   ;

                        logic b_pack                     ;
                        logic b_packh                    ;
                         
                        logic b_rev_b                    ;
                        logic b_zip                      ;
                        logic b_unzip                    ;
                        logic b_xperm_n                  ;
                        logic b_xperm_b                  ; 
                   
                   
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                       
                   
                   
                       
                      logic fpu ;
                      logic fpu_addmul  ;
                      logic fpu_divsqrt ;
                      logic fpu_noncomp ;
                      logic fpu_conv    ;
                      logic fs1 ;
                      logic fs2 ;
                      logic fs3 ;
                      logic fd ;
                      logic fcsr;
                      logic f_extension ;
                      logic f_int64 ;
                      logic fp_fmadd ;
                      logic fp_fmsub ;
                      logic fp_fnmsub ;
                      logic fp_fnmadd ;
                      logic fp_add ;
                      logic fp_fmul ;
                      logic fp_fdiv ;
                      logic fp_fsqrt ;
                      logic fp_fsgnj ;
                      logic fp_fsgnjn ;
                      logic fp_fsgnjx ;
                      logic fp_min ;
                      logic fp_max ;
                      logic fp_feq ;
                      logic fp_flt ;
                      logic fp_fle ;
                      logic fp_fclass ;
                      logic fp_fcvt_f2i ;
                      logic fp_sign ;
                      logic fp_fmv ;
                      logic fp_fcvt_i2f ;
                      logic fp16_extension;
                      logic fp_fcvt_s_h;
                      logic fp_fcvt_h_s;
                      logic fp_fcvt_d_h;
                      logic fp_fcvt_h_d;
                       

                       
                      logic d_extension ;
                      logic fp_fcvt_s_d ;
                      logic fp_fcvt_d_s ;

                       
                   

                       logic alu;
                       logic rs1;
                       logic rs2;
                       logic imm12;
                       logic rd;
                       logic shimm5;
                       logic imm20;
                       logic pc;
                       logic load;
                       logic store;
                       logic lsu;
                       logic add;
                       logic sub;
                       logic land;
                       logic lor;
                       logic lxor;
                       logic sll;
                       logic sra;
                       logic srl;
                       logic slt;
                       logic unsign;
                       logic condbr;
                       logic beq;
                       logic bne;
                       logic bge;
                       logic blt;
                       logic jal;
                       logic by;
                       logic half;
                       logic word;
                       logic dword;
                       logic csr_read;
                       logic csr_clr;
                       logic csr_set;
                       logic csr_write;
                       logic csr_imm;
                       logic presync;
                       logic postsync;
                       logic ebreak;
                       logic ecall;
                       logic mret;
                       logic sret;
                       logic dret;
                       logic wfi;
                       logic mul;
                       logic rs1_sign;
                       logic rs2_sign;
                       logic low;
                       logic div;
                       logic rem;
                       logic fence;
                       logic fence_i;
                       logic sfence_vma;
                       logic pm_alu;
                       logic legal;
                       } dec_pkt_t;


typedef struct packed {
                       logic valid;
                       logic rs1_sign;
                       logic rs2_sign;
                       logic low;
                       logic load_mul_rs1_bypass_e1;
                       logic load_mul_rs2_bypass_e1;
                       } mul_pkt_t;

typedef struct packed {
                       logic valid;
                       logic unsign;
                       logic rem;
                       } div_pkt_t;


typedef struct packed {
                        logic        select;
                        logic        match;
                        logic        store;
                        logic        load;
                        logic        execute;
                        logic        m;
                        logic [31:0] tdata2;
            } trigger_pkt_t;


typedef struct packed {
`ifdef RV_ICACHE_ECC
                        logic [41:0]  icache_wrdata;  
`else
                        logic [33:0]  icache_wrdata;  
`endif
                        logic [18:2]  icache_dicawics;
                        logic         icache_rd_valid;
                        logic         icache_wr_valid;
            } cache_debug_pkt_t;


endpackage
