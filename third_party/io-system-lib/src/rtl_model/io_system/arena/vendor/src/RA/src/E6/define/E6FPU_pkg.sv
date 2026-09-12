package E6FPU_pkg;
 
 
 
 
 
 
 
 
 

 
 
 
 
 
 
 

     
     
     
    function automatic integer ceil_div (input longint dividend, input longint divisor);
        automatic longint remainder;

         
        `ifndef VERILATOR
        if (dividend < 0) begin
            $fatal(1, "Dividend %0d is not a natural number!", dividend);
        end

        if (divisor < 0) begin
            $fatal(1, "Divisor %0d is not a natural number!", divisor);
        end

        if (divisor == 0) begin
            $fatal(1, "Division by zero!");
        end
        `endif
         

        remainder = dividend;
        for (ceil_div = 0; remainder > 0; ceil_div++) begin
            remainder = remainder - divisor;
        end
    endfunction

     
     
     
     
     
     
     
     
     
    function automatic integer unsigned idx_width (input integer unsigned num_idx);
        return (num_idx > 32'd1) ? unsigned'($clog2(num_idx)) : 32'd1;
    endfunction

 
 
 
 
 
 
 
 
 
 
 
 


    
   localparam C_RM                  = 3;
   localparam C_RM_NEAREST          = 3'h0;
   localparam C_RM_TRUNC            = 3'h1;
   localparam C_RM_PLUSINF          = 3'h2;
   localparam C_RM_MINUSINF         = 3'h3;
   localparam C_PC                  = 6;  
   localparam C_FS                  = 2;  
   localparam C_IUNC                = 2;  
   localparam Iteration_unit_num_S  = 2'b10;

    
   localparam C_OP_FP64             = 64;
   localparam C_MANT_FP64           = 52;
   localparam C_EXP_FP64            = 11;
   localparam C_BIAS_FP64           = 1023;
   localparam C_BIAS_AONE_FP64      = 11'h400;
   localparam C_HALF_BIAS_FP64      = 511;
   localparam C_EXP_ZERO_FP64       = 11'h000;
   localparam C_EXP_ONE_FP64        = 13'h001;  
   localparam C_EXP_INF_FP64        = 11'h7FF;
   localparam C_MANT_ZERO_FP64      = 52'h0;
   localparam C_MANT_NAN_FP64       = 52'h8_0000_0000_0000;
   localparam C_PZERO_FP64          = 64'h0000_0000_0000_0000;
   localparam C_MZERO_FP64          = 64'h8000_0000_0000_0000;
   localparam C_QNAN_FP64           = 64'h7FF8_0000_0000_0000;

    
   localparam C_OP_FP32             = 32;
   localparam C_MANT_FP32           = 23;
   localparam C_EXP_FP32            = 8;
   localparam C_BIAS_FP32           = 127;
   localparam C_BIAS_AONE_FP32      = 8'h80;
   localparam C_HALF_BIAS_FP32      = 63;
   localparam C_EXP_ZERO_FP32       = 8'h00;
   localparam C_EXP_INF_FP32        = 8'hFF;
   localparam C_MANT_ZERO_FP32      = 23'h0;
   localparam C_PZERO_FP32          = 32'h0000_0000;
   localparam C_MZERO_FP32          = 32'h8000_0000;
   localparam C_QNAN_FP32           = 32'h7FC0_0000;

    
   localparam C_OP_FP16             = 16;
   localparam C_MANT_FP16           = 10;
   localparam C_EXP_FP16            = 5;
   localparam C_BIAS_FP16           = 15;
   localparam C_BIAS_AONE_FP16      = 5'h10;
   localparam C_HALF_BIAS_FP16      = 7;
   localparam C_EXP_ZERO_FP16       = 5'h00;
   localparam C_EXP_INF_FP16        = 5'h1F;
   localparam C_MANT_ZERO_FP16      = 10'h0;
   localparam C_PZERO_FP16          = 16'h0000;
   localparam C_MZERO_FP16          = 16'h8000;
   localparam C_QNAN_FP16           = 16'h7E00;

    
   localparam C_OP_FP16ALT           = 16;
   localparam C_MANT_FP16ALT         = 7;
   localparam C_EXP_FP16ALT          = 8;
   localparam C_BIAS_FP16ALT         = 127;
   localparam C_BIAS_AONE_FP16ALT    = 8'h80;
   localparam C_HALF_BIAS_FP16ALT    = 63;
   localparam C_EXP_ZERO_FP16ALT     = 8'h00;
   localparam C_EXP_INF_FP16ALT      = 8'hFF;
   localparam C_MANT_ZERO_FP16ALT    = 7'h0;
   localparam C_QNAN_FP16ALT         = 16'h7FC0;

 
 
 
 
 
 
 
 
 
 
 
 

 


   
   
   
   
   
   
   
   
   
   
   

   
  typedef struct packed {
    int unsigned exp_bits;
    int unsigned man_bits;
  } fp_encoding_t;

  localparam int unsigned NUM_FP_FORMATS = 5;  
  localparam int unsigned FP_FORMAT_BITS = $clog2(NUM_FP_FORMATS);

   
  typedef enum logic [FP_FORMAT_BITS-1:0] {
    FP32    = 'd0,
    FP64    = 'd1,
    FP16    = 'd2,
    FP8     = 'd3,
    FP16ALT = 'd4
     
  } fp_format_e;

   
  localparam fp_encoding_t [0:NUM_FP_FORMATS-1] FP_ENCODINGS  = '{
    '{8,  23},  
    '{11, 52},  
    '{5,  10},  
    '{5,  2},   
    '{8,  7}    
     
  };

  typedef logic [0:NUM_FP_FORMATS-1]       fmt_logic_t;     
  typedef logic [0:NUM_FP_FORMATS-1][31:0] fmt_unsigned_t;  

  localparam fmt_logic_t CPK_FORMATS = 5'b11000;  

   
   
   
   
   
   
   
   
   
   

  localparam int unsigned NUM_INT_FORMATS = 4;  
  localparam int unsigned INT_FORMAT_BITS = $clog2(NUM_INT_FORMATS);

   
  typedef enum logic [INT_FORMAT_BITS-1:0] {
    INT8,
    INT16,
    INT32,
    INT64
     
  } int_format_e;

   
  function automatic int unsigned int_width(int_format_e ifmt);
    unique case (ifmt)
      INT8:  return 8;
      INT16: return 16;
      INT32: return 32;
      INT64: return 64;
      default: begin
         
        $fatal(1, "Invalid INT format supplied");
         
         
         
        return INT8;
      end
    endcase
  endfunction

  typedef logic [0:NUM_INT_FORMATS-1] ifmt_logic_t;  

   
   
   
  localparam int unsigned NUM_OPGROUPS = 4;

   
  typedef enum logic [1:0] {
    ADDMUL, DIVSQRT, NONCOMP, CONV
  } opgroup_e;

  localparam int unsigned OP_BITS = 4;

  typedef enum logic [OP_BITS-1:0] {
    FMADD, FNMSUB, ADD, MUL,      
    DIV, SQRT,                    
    SGNJ, MINMAX, CMP, CLASSIFY,  
    F2F, F2I, I2F, CPKAB, CPKCD,  
    ADDS                          
  } operation_e;

   
   
   
  typedef enum logic[1:0] {
    PULP,     
    TH32,     
    THMULTI   
  } divsqrt_unit_t;

   
   
   
   
  typedef enum logic [2:0] {
    RNE = 3'b000,
    RTZ = 3'b001,
    RDN = 3'b010,
    RUP = 3'b011,
    RMM = 3'b100,
    ROD = 3'b101,   
    DYN = 3'b111
  } roundmode_e;

   
  typedef struct packed {
    logic NV;  
    logic DZ;  
    logic OF;  
    logic UF;  
    logic NX;  
  } status_t;

   
  typedef struct packed {
    logic is_normal;      
    logic is_subnormal;   
    logic is_zero;        
    logic is_inf;         
    logic is_nan;         
    logic is_signalling;  
    logic is_quiet;       
    logic is_boxed;       
  } fp_info_t;

   
  typedef enum logic [9:0] {
    NEGINF     = 10'b00_0000_0001,
    NEGNORM    = 10'b00_0000_0010,
    NEGSUBNORM = 10'b00_0000_0100,
    NEGZERO    = 10'b00_0000_1000,
    POSZERO    = 10'b00_0001_0000,
    POSSUBNORM = 10'b00_0010_0000,
    POSNORM    = 10'b00_0100_0000,
    POSINF     = 10'b00_1000_0000,
    SNAN       = 10'b01_0000_0000,
    QNAN       = 10'b10_0000_0000
  } classmask_e;

   
   
   
   
  typedef enum logic [1:0] {
    BEFORE,      
    AFTER,       
    INSIDE,      
    DISTRIBUTED  
  } pipe_config_t;

   
  typedef enum logic [1:0] {
    DISABLED,  
    PARALLEL,  
    MERGED     
  } unit_type_t;

   
  typedef unit_type_t [0:NUM_FP_FORMATS-1] fmt_unit_types_t;

   
  typedef fmt_unit_types_t [0:NUM_OPGROUPS-1] opgrp_fmt_unit_types_t;
   
  typedef fmt_unsigned_t [0:NUM_OPGROUPS-1] opgrp_fmt_unsigned_t;

   
  typedef struct packed {
    int unsigned Width;
    logic        EnableVectors;
    logic        EnableNanBox;
    fmt_logic_t  FpFmtMask;
    ifmt_logic_t IntFmtMask;
  } fpu_features_t;

  localparam fpu_features_t RV64D = '{
    Width:         64,
    EnableVectors: 1'b0,
    EnableNanBox:  1'b1,
    FpFmtMask:     5'b11000,
    IntFmtMask:    4'b0011
  };

  localparam fpu_features_t RV32D = '{
    Width:         64,
    EnableVectors: 1'b1,
    EnableNanBox:  1'b1,
    FpFmtMask:     5'b11000,
    IntFmtMask:    4'b0010
  };

  localparam fpu_features_t RV32F = '{
    Width:         32,
    EnableVectors: 1'b0,
    EnableNanBox:  1'b1,
    FpFmtMask:     5'b10000,
    IntFmtMask:    4'b0010
  };

  localparam fpu_features_t RV64D_Xsflt = '{
    Width:         64,
    EnableVectors: 1'b0,
    EnableNanBox:  1'b1,
    FpFmtMask:     5'b11111,
    IntFmtMask:    4'b1111
  };

  localparam fpu_features_t RV32F_Xsflt = '{
    Width:         32,
    EnableVectors: 1'b1,
    EnableNanBox:  1'b1,
    FpFmtMask:     5'b10111,
    IntFmtMask:    4'b1110
  };

  localparam fpu_features_t RV32F_Xf16alt_Xfvec = '{
    Width:         32,
    EnableVectors: 1'b1,
    EnableNanBox:  1'b1,
    FpFmtMask:     5'b10001,
    IntFmtMask:    4'b0110
  };

  localparam fpu_features_t DQS_RV32D = '{
    Width:         64,
    EnableVectors: 1'b0,
    EnableNanBox:  1'b1,
    FpFmtMask:     5'b11000,
    IntFmtMask:    4'b0010
  };

   
  typedef struct packed {
    opgrp_fmt_unsigned_t   PipeRegs;
    opgrp_fmt_unit_types_t UnitTypes;
    pipe_config_t          PipeConfig;
  } fpu_implementation_t;

  localparam fpu_implementation_t DEFAULT_NOREGS = '{
    PipeRegs:   '{default: 0},
    UnitTypes:  '{'{default: PARALLEL},  
                  '{default: MERGED},    
                  '{default: PARALLEL},  
                  '{default: MERGED}},   
    PipeConfig: BEFORE
  };

  localparam fpu_implementation_t DEFAULT_SNITCH = '{
    PipeRegs:   '{default: 1},
    UnitTypes:  '{'{default: PARALLEL},  
                  '{default: DISABLED},  
                  '{default: PARALLEL},  
                  '{default: MERGED}},   
    PipeConfig: BEFORE
  };

  localparam fpu_implementation_t DEFAULT_4Reg = '{
    PipeRegs:   '{default: 4},
    UnitTypes:  '{'{default: MERGED},  
                  '{default: MERGED},  
                  '{default: PARALLEL},  
                  '{default: MERGED}},   
    PipeConfig: DISTRIBUTED
  };

  localparam fpu_implementation_t ADDMUL_4Reg = '{
    PipeRegs:   '{default: 3},
    UnitTypes:  '{'{default: MERGED},  
                  '{default: DISABLED},  
                  '{default: DISABLED},  
                  '{default: DISABLED}},   
    PipeConfig: DISTRIBUTED
  };

  localparam fpu_implementation_t DIVSQRT_4Reg = '{
    PipeRegs:   '{default: 2},
    UnitTypes:  '{'{default: DISABLED},  
                  '{default: MERGED},  
                  '{default: DISABLED},  
                  '{default: DISABLED}},   
    PipeConfig: DISTRIBUTED
  };

  localparam fpu_implementation_t NONCOMP_4Reg = '{
    PipeRegs:   '{default: 2},
    UnitTypes:  '{'{default: DISABLED},  
                  '{default: DISABLED},  
                  '{default: PARALLEL},  
                  '{default: DISABLED}},   
    PipeConfig: DISTRIBUTED
  };

  localparam fpu_implementation_t CONV_4Reg = '{
    PipeRegs:   '{default: 3},
    UnitTypes:  '{'{default: DISABLED},  
                  '{default: DISABLED},  
                  '{default: DISABLED},  
                  '{default: MERGED}},   
    PipeConfig: DISTRIBUTED
  };

   
   
   
  localparam logic DONT_CARE = 1'b1;  

   
   
   
  function automatic int minimum(int a, int b);
    return (a < b) ? a : b;
  endfunction

  function automatic int maximum(int a, int b);
    return (a > b) ? a : b;
  endfunction

   
   
   
   
  function automatic int unsigned fp_width(fp_format_e fmt);
    return FP_ENCODINGS[fmt].exp_bits + FP_ENCODINGS[fmt].man_bits + 1;
  endfunction

   
  function automatic int unsigned max_fp_width(fmt_logic_t cfg);
    automatic int unsigned res = 0;
    for (int unsigned i = 0; i < NUM_FP_FORMATS; i++)
      if (cfg[i])
        res = unsigned'(maximum(res, fp_width(fp_format_e'(i))));
    return res;
  endfunction

   
  function automatic int unsigned min_fp_width(fmt_logic_t cfg);
    automatic int unsigned res = max_fp_width(cfg);
    for (int unsigned i = 0; i < NUM_FP_FORMATS; i++)
      if (cfg[i])
        res = unsigned'(minimum(res, fp_width(fp_format_e'(i))));
    return res;
  endfunction

   
  function automatic int unsigned exp_bits(fp_format_e fmt);
    return FP_ENCODINGS[fmt].exp_bits;
  endfunction

   
  function automatic int unsigned man_bits(fp_format_e fmt);
    return FP_ENCODINGS[fmt].man_bits;
  endfunction

   
  function automatic int unsigned bias(fp_format_e fmt);
    return unsigned'(2**(FP_ENCODINGS[fmt].exp_bits-1)-1);  
  endfunction

  function automatic fp_encoding_t super_format(fmt_logic_t cfg);
    automatic fp_encoding_t res;
    res = '0;
    for (int unsigned fmt = 0; fmt < NUM_FP_FORMATS; fmt++)
      if (cfg[fmt]) begin  
        res.exp_bits = unsigned'(maximum(res.exp_bits, exp_bits(fp_format_e'(fmt))));
        res.man_bits = unsigned'(maximum(res.man_bits, man_bits(fp_format_e'(fmt))));
      end
    return res;
  endfunction

   
   
   
   
  function automatic int unsigned max_int_width(ifmt_logic_t cfg);
    automatic int unsigned res = 0;
    for (int ifmt = 0; ifmt < NUM_INT_FORMATS; ifmt++) begin
      if (cfg[ifmt]) res = maximum(res, int_width(int_format_e'(ifmt)));
    end
    return res;
  endfunction

   
   
   
   
  function automatic opgroup_e get_opgroup(operation_e op);
    unique case (op)
      FMADD, FNMSUB, ADD, ADDS, MUL: return ADDMUL;
      DIV, SQRT:                     return DIVSQRT;
      SGNJ, MINMAX, CMP, CLASSIFY:   return NONCOMP;
      F2F, F2I, I2F, CPKAB, CPKCD:   return CONV;
      default:                       return NONCOMP;
    endcase
  endfunction

   
  function automatic int unsigned num_operands(opgroup_e grp);
    unique case (grp)
      ADDMUL:  return 3;
      DIVSQRT: return 2;
      NONCOMP: return 2;
      CONV:    return 3;  
      default: return 0;
    endcase
  endfunction

   
  function automatic int unsigned num_lanes(int unsigned width, fp_format_e fmt, logic vec);
    return vec ? width / fp_width(fmt) : 1;  
  endfunction

   
  function automatic int unsigned max_num_lanes(int unsigned width, fmt_logic_t cfg, logic vec);
    return vec ? width / min_fp_width(cfg) : 1;  
  endfunction

     
  function automatic int unsigned num_divsqrt_lanes(int unsigned width, fmt_logic_t cfg, logic vec, divsqrt_unit_t DivSqrtSel);
    automatic fmt_logic_t cfg_tmp;
    cfg_tmp = (DivSqrtSel == THMULTI) ? cfg & 5'b11101 : cfg;
    return vec ? width / min_fp_width(cfg_tmp) : 1;  
  endfunction

   
  function automatic fmt_logic_t get_lane_formats(int unsigned width,
                                                  fmt_logic_t cfg,
                                                  int unsigned lane_no);
    automatic fmt_logic_t res;
    for (int unsigned fmt = 0; fmt < NUM_FP_FORMATS; fmt++)
       
      res[fmt] = cfg[fmt] & (width / fp_width(fp_format_e'(fmt)) > lane_no);
    return res;
  endfunction

   
  function automatic ifmt_logic_t get_lane_int_formats(int unsigned width,
                                                       fmt_logic_t cfg,
                                                       ifmt_logic_t icfg,
                                                       int unsigned lane_no);
    automatic ifmt_logic_t res;
    automatic fmt_logic_t lanefmts;
    res = '0;
    lanefmts = get_lane_formats(width, cfg, lane_no);

    for (int unsigned ifmt = 0; ifmt < NUM_INT_FORMATS; ifmt++)
      for (int unsigned fmt = 0; fmt < NUM_FP_FORMATS; fmt++)
         
        if ((fp_width(fp_format_e'(fmt)) == int_width(int_format_e'(ifmt))))
          res[ifmt] |= icfg[ifmt] && lanefmts[fmt];
    return res;
  endfunction

   
  function automatic fmt_logic_t get_conv_lane_formats(int unsigned width,
                                                       fmt_logic_t cfg,
                                                       int unsigned lane_no);
    automatic fmt_logic_t res;
    for (int unsigned fmt = 0; fmt < NUM_FP_FORMATS; fmt++)
       
      res[fmt] = cfg[fmt] && ((width / fp_width(fp_format_e'(fmt)) > lane_no) ||
                             (CPK_FORMATS[fmt] && (lane_no < 2)));
    return res;
  endfunction

   
  function automatic ifmt_logic_t get_conv_lane_int_formats(int unsigned width,
                                                            fmt_logic_t cfg,
                                                            ifmt_logic_t icfg,
                                                            int unsigned lane_no);
    automatic ifmt_logic_t res;
    automatic fmt_logic_t lanefmts;
    res = '0;
    lanefmts = get_conv_lane_formats(width, cfg, lane_no);

    for (int unsigned ifmt = 0; ifmt < NUM_INT_FORMATS; ifmt++)
      for (int unsigned fmt = 0; fmt < NUM_FP_FORMATS; fmt++)
         
        res[ifmt] |= icfg[ifmt] && lanefmts[fmt] &&
                     (fp_width(fp_format_e'(fmt)) == int_width(int_format_e'(ifmt)));
    return res;
  endfunction

   
  function automatic logic any_enabled_multi(fmt_unit_types_t types, fmt_logic_t cfg);
    for (int unsigned i = 0; i < NUM_FP_FORMATS; i++)
      if (cfg[i] && types[i] == MERGED)
        return 1'b1;
      return 1'b0;
  endfunction

   
  function automatic logic is_first_enabled_multi(fp_format_e fmt,
                                                  fmt_unit_types_t types,
                                                  fmt_logic_t cfg);
    for (int unsigned i = 0; i < NUM_FP_FORMATS; i++) begin
      if (cfg[i] && types[i] == MERGED) return (fp_format_e'(i) == fmt);
    end
    return 1'b0;
  endfunction

   
  function automatic fp_format_e get_first_enabled_multi(fmt_unit_types_t types, fmt_logic_t cfg);
    for (int unsigned i = 0; i < NUM_FP_FORMATS; i++)
      if (cfg[i] && types[i] == MERGED)
        return fp_format_e'(i);
      return fp_format_e'(0);
  endfunction

   
  function automatic int unsigned get_num_regs_multi(fmt_unsigned_t regs,
                                                     fmt_unit_types_t types,
                                                     fmt_logic_t cfg);
    automatic int unsigned res = 0;
    for (int unsigned i = 0; i < NUM_FP_FORMATS; i++) begin
      if (cfg[i] && types[i] == MERGED) res = maximum(res, regs[i]);
    end
    return res;
  endfunction

endpackage
