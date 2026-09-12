// Copyright (C) 2024, Andes Technology Corp. Confidential Proprietary
// AX45MPV_PREMIUM v30.1.0


module exmon300 (
	  aclk,
	  aresetn,

	  us_awid,
	  us_awaddr,
	  us_awlock,
	  us_awburst,
	  us_awlen,
	  us_awcache,
	  us_awprot,
	  us_awsize,
	  us_awqos,
	  us_awregion,
	  us_awvalid,
	  us_awready,

	  us_wdata,
	  us_wstrb,
	  us_wlast,
	  us_wvalid,
	  us_wready,

	  us_bresp,
	  us_bid,
	  us_bvalid,
	  us_bready,

	  us_arid,
	  us_araddr,
	  us_arlock,
	  us_arburst,
	  us_arlen,
	  us_arcache,
	  us_arprot,
	  us_arsize,
	  us_arqos,
	  us_arregion,
	  us_arvalid,
	  us_arready,

	  us_rid,
	  us_rresp,
	  us_rdata,
	  us_rlast,
	  us_rvalid,
	  us_rready,


	  ds_awid,
	  ds_awaddr,
	  ds_awlock,
	  ds_awburst,
	  ds_awlen,
	  ds_awcache,
	  ds_awprot,
	  ds_awsize,
	  ds_awqos,
	  ds_awregion,
	  ds_awvalid,
	  ds_awready,

	  ds_wdata,
	  ds_wstrb,
	  ds_wlast,
	  ds_wvalid,
	  ds_wready,

	  ds_bresp,
	  ds_bid,
	  ds_bvalid,
	  ds_bready,

	  ds_arid,
	  ds_araddr,
	  ds_arlock,
	  ds_arburst,
	  ds_arlen,
	  ds_arcache,
	  ds_arprot,
	  ds_arsize,
	  ds_arqos,
	  ds_arregion,
	  ds_arvalid,
	  ds_arready,

	  ds_rid,
	  ds_rresp,
	  ds_rdata,
	  ds_rlast,
	  ds_rvalid,
	  ds_rready
);

parameter ID_WIDTH = 16;
parameter ADDR_WIDTH = 64;
parameter DATA_WIDTH = 256;
parameter NUM_EX_SEQ = 32;

localparam PRODUCT_ID = 32'h00133022;
localparam PENDING_CNT = ID_WIDTH;
localparam SIZE_WIDTH  = 3;
localparam CACHE_WIDTH = 4;
localparam QOS_WIDTH = 4;
localparam REGION_WIDTH = 4;
localparam PROT_WIDTH  = 3;
localparam LEN_WIDTH   = 8;
localparam BURST_WIDTH = 2;
localparam FIFO_WIDTH = ID_WIDTH + ADDR_WIDTH + SIZE_WIDTH + BURST_WIDTH + LEN_WIDTH + CACHE_WIDTH + PROT_WIDTH + QOS_WIDTH + REGION_WIDTH + 1;
localparam FIFO_WD_WIDTH = DATA_WIDTH + (DATA_WIDTH/8) + 1;

localparam IDLE		= 0;
localparam SEND_RW_LOCK	= 1;
localparam SEND_AR_LOCK	= 2;
localparam WAIT_AW_SEND = 3;
localparam SEND_AW_LOCK	= 4;
localparam WAIT_RW_PENDING = 5;
localparam WAIT_AR_PENDING = 6;
localparam WAIT_AW_PENDING = 7;
localparam WAIT_RW_RESP = 8;
localparam WAIT_AR_RESP = 9;
localparam WAIT_AW_RESP = 10;
localparam SEND_WDATA   = 11;
localparam WAIT_AW_ARREADY = 12;
localparam WAIT_AR_AWREADY = 13;
localparam STATES	= 14;

input                             aclk;
input                             aresetn;
input            [(ID_WIDTH-1):0] us_awid;
input            [ADDR_WIDTH-1:0] us_awaddr;
input                             us_awlock;
input                       [1:0] us_awburst;
input                       [7:0] us_awlen;
input	        [CACHE_WIDTH-1:0] us_awcache;
input	 	 [PROT_WIDTH-1:0] us_awprot;
input                       [2:0] us_awsize;
input		  [QOS_WIDTH-1:0] us_awqos;
input	       [REGION_WIDTH-1:0] us_awregion;
input                             us_awvalid;
output                            us_awready;

input	  	 [DATA_WIDTH-1:0] us_wdata;
input        [(DATA_WIDTH/8)-1:0] us_wstrb;
input                             us_wlast;
input                             us_wvalid;
output	  			  us_wready;

output                      [1:0] us_bresp;
output           [(ID_WIDTH-1):0] us_bid;
output                            us_bvalid;
input                             us_bready;


input            [(ID_WIDTH-1):0] us_arid;
input            [ADDR_WIDTH-1:0] us_araddr;
input                             us_arlock;
input                       [1:0] us_arburst;
input                       [7:0] us_arlen;
input	        [CACHE_WIDTH-1:0] us_arcache;
input	         [PROT_WIDTH-1:0] us_arprot;
input                       [2:0] us_arsize;
input		  [QOS_WIDTH-1:0] us_arqos;
input	       [REGION_WIDTH-1:0] us_arregion;
input                             us_arvalid;
output				  us_arready;

output           [(ID_WIDTH-1):0] us_rid;
output                      [1:0] us_rresp;
output	  	 [DATA_WIDTH-1:0] us_rdata;
output	  			  us_rlast;
output	  			  us_rvalid;
input                             us_rready;

output	  	 [(ID_WIDTH-1):0] ds_awid;
output		 [ADDR_WIDTH-1:0] ds_awaddr;
output	  		 	  ds_awlock;
output	  		    [1:0] ds_awburst;
output	  		    [7:0] ds_awlen;
output	        [CACHE_WIDTH-1:0] ds_awcache;
output	  	 [PROT_WIDTH-1:0] ds_awprot;
output	  		    [2:0] ds_awsize;
output		  [QOS_WIDTH-1:0] ds_awqos;
output	       [REGION_WIDTH-1:0] ds_awregion;
output	  			  ds_awvalid;
input                             ds_awready;

output	  	 [DATA_WIDTH-1:0] ds_wdata;
output       [(DATA_WIDTH/8)-1:0] ds_wstrb;
output	  			  ds_wlast;
output	  			  ds_wvalid;
input				  ds_wready;

input                       [1:0] ds_bresp;
input            [(ID_WIDTH-1):0] ds_bid;
input                             ds_bvalid;
output                            ds_bready;

output	  	 [(ID_WIDTH-1):0] ds_arid;
output	  	 [ADDR_WIDTH-1:0] ds_araddr;
output	  			  ds_arlock;
output	  		    [1:0] ds_arburst;
output	  		    [7:0] ds_arlen;
output	        [CACHE_WIDTH-1:0] ds_arcache;
output	  	 [PROT_WIDTH-1:0] ds_arprot;
output	  		    [2:0] ds_arsize;
output		  [QOS_WIDTH-1:0] ds_arqos;
output	       [REGION_WIDTH-1:0] ds_arregion;
output	  			  ds_arvalid;
input                             ds_arready;

input	 	 [(ID_WIDTH-1):0] ds_rid;
input                       [1:0] ds_rresp;
input	  	 [DATA_WIDTH-1:0] ds_rdata;
input                             ds_rlast;
input                             ds_rvalid;
output                            ds_rready;





// d0870df6 rnd


wire 		 [NUM_EX_SEQ-1:0] ent_valid;
wire  		 [NUM_EX_SEQ-1:0] s0;
wire  		 [NUM_EX_SEQ-1:0] s1;
wire			          s2;
wire  [(NUM_EX_SEQ*ID_WIDTH)-1:0] ent_id;
wire  		 [NUM_EX_SEQ-1:0] dec_err;
wire 		 [NUM_EX_SEQ-1:0] match_exclusive;
wire		 [NUM_EX_SEQ-1:0] s3;
wire		 [NUM_EX_SEQ-1:0] s4;
wire		 [NUM_EX_SEQ-1:0] s5;
wire		 [NUM_EX_SEQ-1:0] s6;
wire		 	          s7;
wire			    [1:0] s8;
wire			          s9;
wire	         [NUM_EX_SEQ-1:0] nds_unused_ready;

wire				  nds_unused_aw_ex_awlock;
wire               [ID_WIDTH-1:0] s10;
wire 		 [ADDR_WIDTH-1:0] s11;
wire 		 [SIZE_WIDTH-1:0] s12;
wire 	        [BURST_WIDTH-1:0] s13;
wire 		  [LEN_WIDTH-1:0] s14;
wire 	        [CACHE_WIDTH-1:0] s15;
wire 	 	 [PROT_WIDTH-1:0] s16;
wire 	 	  [QOS_WIDTH-1:0] s17;
wire 	       [REGION_WIDTH-1:0] s18;

wire				  nds_unused_ar_ex_arlock;
wire               [ID_WIDTH-1:0] s19;
wire 		 [ADDR_WIDTH-1:0] s20;
wire 		 [SIZE_WIDTH-1:0] s21;
wire 	        [BURST_WIDTH-1:0] s22;
wire 		  [LEN_WIDTH-1:0] s23;
wire 	        [CACHE_WIDTH-1:0] s24;
wire 	 	 [PROT_WIDTH-1:0] s25;
wire 	 	  [QOS_WIDTH-1:0] s26;
wire 	       [REGION_WIDTH-1:0] s27;

wire                              s28;
wire                              s29;
wire				  s30;
wire				  s31;
wire				  s32;
wire				  s33;
wire				  s34;
wire				  s35;
wire				  s36;
wire				  s37;
wire				  s38;
wire				  s39;
reg				  s40;
reg				  s41;

wire		 [FIFO_WIDTH-1:0] s42;
wire		 		  s43;
reg		 [FIFO_WIDTH-1:0] s44;
wire		 [FIFO_WIDTH-1:0] s45;
wire		 		  s46;
reg		 [FIFO_WIDTH-1:0] s47;
wire		 		  s48;
wire		 		  s49;

wire				  s50;
wire				  s51;
wire				  nds_unused_aw_d_pending_full;
wire				  s52;
wire				  s53;
wire				  s54;
wire				  s55;
wire				  s56;
wire				  s57;

wire 				  s58;
wire 				  s59;
wire 				  s60;
wire				  s61;
wire				  s62;
wire				  s63;
wire				  s64;
wire 				  s65;

reg		     [STATES-1:0] s66;
reg        	     [STATES-1:0] s67;
reg        	                  s68;
wire				  s69;
wire                              s70;
wire                              s71;
wire                              s72;
wire                              s73;
wire				  s74;
wire				  nds_unused_fsm_wait_aw_send;
wire				  s75;
wire				  s76;
wire				  s77;
wire				  s78;

wire             [FIFO_WIDTH-1:0] s79;
wire             [FIFO_WIDTH-1:0] s80;
wire             [(ID_WIDTH-1):0] s81;
wire             [ADDR_WIDTH-1:0] s82;
wire                              s83;
wire                        [1:0] s84;
wire                        [7:0] s85;
wire            [CACHE_WIDTH-1:0] s86;
wire             [PROT_WIDTH-1:0] s87;
wire                        [2:0] s88;
wire              [QOS_WIDTH-1:0] s89;
wire           [REGION_WIDTH-1:0] s90;
wire                              s91;
wire                              s92;

wire             [FIFO_WIDTH-1:0] s93;
wire             [FIFO_WIDTH-1:0] s94;
wire	  	 [(ID_WIDTH-1):0] s95;
wire	  	 [ADDR_WIDTH-1:0] s96;
wire	  			  s97;
wire	  		    [1:0] s98;
wire	  		    [7:0] s99;
wire	        [CACHE_WIDTH-1:0] s100;
wire	  	 [PROT_WIDTH-1:0] s101;
wire	  		    [2:0] s102;
wire		  [QOS_WIDTH-1:0] s103;
wire	       [REGION_WIDTH-1:0] s104;
wire	  			  s105;
wire                              s106;

reg              [FIFO_WIDTH-1:0] s107;
wire                              s108;
reg                               s109;
wire                              s110;
reg              [FIFO_WIDTH-1:0] s111;
wire                              s112;
reg                               s113;
wire                              s114;


assign s50 = us_awvalid & us_awready & ~us_awlock;
assign s51 = ds_wvalid & ds_wready & ds_wlast & ~s73;
exmon300_pending_cnt #(
	.WIDTH	(PENDING_CNT		)
	) u_aw_d_pending_cnt (
	.clk            (aclk				),
	.reset_n        (aresetn			),
	.increase       (s50		),
	.decrease       (s51		),
	.full           (nds_unused_aw_d_pending_full	),
	.zero           (s52		)
);

assign s53 = us_awvalid & us_awready & ~us_awlock;
assign s54 = us_bvalid & us_bready & ~s74;
exmon300_pending_cnt #(
	.WIDTH	(PENDING_CNT		)
	) u_aw_r_pending_cnt (
	.clk            (aclk				),
	.reset_n        (aresetn			),
	.increase       (s53		),
	.decrease       (s54		),
	.full           (s55		),
	.zero           (s56		)
);

assign s49 = us_awvalid & us_awready & us_awlock;
assign s45 = {us_awlock, us_awid, us_awaddr, us_awsize, us_awburst, us_awlen, us_awcache, us_awprot, us_awqos, us_awregion};
assign s46 = us_awvalid & us_awready;

assign {nds_unused_aw_ex_awlock,
	s10,
	s11,
	s12,
	s13,
	s14,
	s15,
	s16,
	s17,
	s18
	}  = s47;

always @(posedge aclk or negedge aresetn) begin
        if (!aresetn)
		s47 <= {(FIFO_WIDTH){1'b0}};
	else if (s46)
		s47 <= s45;
end

always @(posedge aclk or negedge aresetn) begin
        if (!aresetn)
		s41 <= 1'b0;
	else
		s41 <= us_awvalid & us_awready & ~us_awlock;
end

assign s61 = us_arvalid & us_arready & ~us_arlock;
assign s62 = ds_rvalid & us_rready & ds_rlast & ~(s28);
exmon300_pending_cnt #(
	.WIDTH	(PENDING_CNT		)
	) u_ar_pending_cnt (
	.clk            (aclk				),
	.reset_n        (aresetn			),
	.increase       (s61			),
	.decrease       (s62			),
	.full           (s63		),
	.zero           (s57		)
);

assign s48 = us_arvalid & us_arready & us_arlock;
assign s42 = {us_arlock, us_arid, us_araddr, us_arsize, us_arburst, us_arlen, us_arcache, us_arprot, us_arqos, us_arregion};
assign s43 = us_arvalid & us_arready;

assign {nds_unused_ar_ex_arlock,
	s19,
	s20,
	s21,
	s22,
	s23,
	s24,
	s25,
	s26,
	s27
	}  = s44;


always @(posedge aclk or negedge aresetn) begin
        if (!aresetn)
		s44 <= {(FIFO_WIDTH){1'b0}};
	else if (s43)
		s44 <= s42;
end

assign s69		= s66[IDLE];
assign s70	= s66[SEND_RW_LOCK];
assign s71	= s66[SEND_AR_LOCK];
assign s72	= s66[SEND_AW_LOCK];
assign s74 = s66[WAIT_AW_RESP];
assign nds_unused_fsm_wait_aw_send = s66[WAIT_AW_SEND];
assign s73   = s66[SEND_WDATA];
assign s75 = s66[WAIT_AR_RESP];
assign s76 = s66[WAIT_RW_RESP];
assign s77 = s66[WAIT_AW_ARREADY];
assign s78 = s66[WAIT_AR_AWREADY];

assign s28	 = s75 | s76;
assign s29 = s57 & s56;
assign s33	 = s29 & ~us_awvalid;
assign s34     = s29 & ~us_arvalid;

assign s30  = s48  & s49 &  s29;
assign s31  = s48  & s33;
assign s32  = s49 & s34;
assign s35  = s48  & s49 & ~s29;
assign s36  = s48  & ~s33;
assign s38  = s49 & ~s34;
assign s37 = s48  & (us_awvalid & ~us_awready);
assign s39 = s49 & (us_arvalid & ~us_arready);

assign s60 = ds_rvalid & us_rready & ds_rlast;
assign s58 = ds_bvalid & us_bready;
assign s59    = ds_wready & ds_wvalid & ds_wlast;

always @* begin
	s67 = {STATES{1'b0}};
	case (1'b1)
	s66[IDLE]: begin
		if (s30) begin
			s67[SEND_RW_LOCK] = 1'b1;
			s68 = 1'b1;
		end
		else if (s31) begin
			s67[SEND_AR_LOCK] = 1'b1;
			s68 = 1'b1;
		end
		else if (s32) begin
			s67[WAIT_AW_SEND] = 1'b1;
			s68 = 1'b1;
		end
		else if (s35) begin
			s67[WAIT_RW_PENDING] = 1'b1;
			s68 = 1'b1;
		end
		else if (s37) begin
			s67[WAIT_AR_AWREADY] = 1'b1;
			s68 = 1'b1;
		end
		else if (s39) begin
			s67[WAIT_AW_ARREADY] = 1'b1;
			s68 = 1'b1;
		end
		else if (s36) begin
			s67[WAIT_AR_PENDING] = 1'b1;
			s68 = 1'b1;
		end
		else if (s38) begin
			s67[WAIT_AW_PENDING] = 1'b1;
			s68 = 1'b1;
		end
		else begin
			s68 = 1'b0;
		end
	end
	s66[WAIT_AW_SEND]: begin
		s67[SEND_AW_LOCK] = 1'b1;
		s68 = 1'b1;
	end
	s66[WAIT_AR_AWREADY]: begin
		if (us_awready & us_awlock) begin
			s67[WAIT_RW_PENDING] = 1'b1;
			s68 = 1'b1;
		end
		else if (us_awready & ~us_awlock) begin
			s67[WAIT_AR_PENDING] = 1'b1;
			s68 = 1'b1;
		end
		else begin
			s68 = 1'b0;
		end
	end
	s66[WAIT_AW_ARREADY]: begin
		if (us_arready & us_arlock) begin
			s67[WAIT_RW_PENDING] = 1'b1;
			s68 = 1'b1;
		end
		else if (us_arready & ~us_arlock) begin
			s67[WAIT_AW_PENDING] = 1'b1;
			s68 = 1'b1;
		end
		else begin
			s68 = 1'b0;
		end
	end
	s66[WAIT_RW_PENDING]: begin
		if (s29) begin
			s67[SEND_RW_LOCK] = 1'b1;
			s68 = 1'b1;
		end
		else begin
			s68 = 1'b0;
		end
	end
	s66[WAIT_AR_PENDING]: begin
		if (s29) begin
			s67[SEND_AR_LOCK] = 1'b1;
			s68 = 1'b1;
		end
		else begin
			s68 = 1'b0;
		end
	end
	s66[WAIT_AW_PENDING]: begin
		if (s29) begin
			s67[SEND_AW_LOCK] = 1'b1;
			s68 = 1'b1;
		end
		else begin
			s68 = 1'b0;
		end
	end
	s66[SEND_RW_LOCK]: begin
		s67[WAIT_RW_RESP] = 1'b1;
		s68 = 1'b1;
	end
	s66[SEND_AR_LOCK]: begin
		s67[WAIT_AR_RESP] = 1'b1;
		s68 = 1'b1;
	end
	s66[SEND_AW_LOCK]: begin
		s67[SEND_WDATA] = 1'b1;
		s68 = 1'b1;
	end
	s66[SEND_WDATA]: begin
		if (s59) begin
			s67[WAIT_AW_RESP] = 1'b1;
			s68 = 1'b1;
		end
		else begin
			s68 = 1'b0;
		end
	end
	s66[WAIT_RW_RESP]: begin
		if (s60) begin
			s67[SEND_AW_LOCK] = 1'b1;
			s68 = 1'b1;
		end
		else begin
			s68 = 1'b0;
		end
	end
	s66[WAIT_AR_RESP]: begin
		if (s60) begin
			s67[IDLE] = 1'b1;
			s68 = 1'b1;
		end
		else begin
			s68 = 1'b0;
		end
	end
	s66[WAIT_AW_RESP]: begin
		if (s58) begin
			s67[IDLE] = 1'b1;
			s68 = 1'b1;
		end
		else begin
			s68 = 1'b0;
		end
	end
	default: begin
		s67 = {STATES{1'b0}};
		s68 = 1'b0;
	end
	endcase
end

always @(posedge aclk or negedge aresetn) begin
	if (!aresetn)
		s66 <= {{(STATES-1){1'b0}}, 1'b1};
	else if (s68)
		s66 <= s67;
end

assign s64 = s71 | s70;
assign s65 = s72;

assign s105 = us_arvalid & (s69 | s77) & ~us_arlock & ~s63
		  | s64
		  ;
assign s91 = us_awvalid & (s69 | s78) & ~us_awlock & ~s55
		  | s65
		  ;
assign us_arready = s106 & (s69 | s77) & ~s63;
assign us_awready = s92 & (s69 | s78) & ~s55;

assign	s89	= s65 ? s17	 : us_awqos	;
assign	s90	= s65 ? s18 : us_awregion	;
assign	s81		= s65 ? s10	 : us_awid	;
assign	s82	= s65 ? s11	 : us_awaddr	;
assign	s84	= s65 ? s13	 : us_awburst	;
assign	s85	= s65 ? s14	 : us_awlen	;
assign	s86	= s65 ? s15	 : us_awcache	;
assign	s87	= s65 ? s16	 : us_awprot	;
assign	s88	= s65 ? s12	 : us_awsize	;
assign	s83	= 1'b0;

assign	ds_wdata	= us_wdata;
assign	ds_wlast	= us_wlast;
assign	ds_wvalid	= us_wvalid & (~s52 | s73);
assign	us_wready	= ds_wready & (~s52 | s73);

assign	s103	= s64 ? s26	 :  us_arqos	;
assign	s104	= s64 ? s27 :  us_arregion ;
assign	s95		= s64 ? s19	 :  us_arid	;
assign	s96	= s64 ? s20   :  us_araddr	;
assign	s98	= s64 ? s22  :  us_arburst  ;
assign	s99	= s64 ? s23	 :  us_arlen	;
assign	s100	= s64 ? s24  :  us_arcache  ;
assign	s101	= s64 ? s25   :  us_arprot	;
assign	s102	= s64 ? s21   :  us_arsize	;
assign	s97	= 1'b0;

assign  us_rid		= ds_rid;
assign	us_rdata	= ds_rdata;
assign	us_rlast	= ds_rlast;
assign	us_rvalid	= ds_rvalid;
assign	ds_rready	= us_rready;

assign  ds_bready	= us_bready;
assign  us_bvalid	= ds_bvalid;
assign  us_bid		= ds_bid;

assign s2         = ds_rvalid & us_rready & ds_rlast & ~ds_rresp[1] & s28 & ~(|s23);
assign s7 = (|s3);
assign s9    = ds_bvalid & us_bready & s40 & s74;

generate
genvar i;
for (i=0; i<NUM_EX_SEQ; i=i+1) begin : gen_ent
	exmon300_ent #(
		.FIFO_WIDTH      (FIFO_WIDTH		                ),
		.SIZE_WIDTH      (SIZE_WIDTH		                ),
		.ADDR_WIDTH	 (ADDR_WIDTH				),
		.ID_WIDTH	 (ID_WIDTH		                ),
		.BURST_WIDTH     (BURST_WIDTH		                ),
		.CACHE_WIDTH	 (CACHE_WIDTH		                ),
		.QOS_WIDTH	 (QOS_WIDTH		                ),
		.REGION_WIDTH	 (REGION_WIDTH		                ),
		.PROT_WIDTH      (PROT_WIDTH		                ),
		.LEN_WIDTH       (LEN_WIDTH		                )
		) u_monitor_ent		(
                .aclk			(aclk                                   ),
		.reset_n		(aresetn	                        ),
		.norm_clr		(s41				),
		.exok_clr		(s9				),
		.dec_err		(dec_err[i]				),
		.set			(s0[i]				),
		.exclusive_rdata	(s44				),
		.exclusive_wdata	(s47				),
		.match_exclusive	(match_exclusive[i]			),
		.ent_id			(ent_id[i*ID_WIDTH+:ID_WIDTH]		),
		.ent_valid		(ent_valid[i]				)
	);
	assign s6[i] = (s19 == ent_id[i*ID_WIDTH+:ID_WIDTH]);
	assign s3[i]   = s6[i] & ent_valid[i];
	assign dec_err[i] = ds_rvalid & us_rready & ds_rlast & ds_rresp[1] & s28 & s6[i];
	assign s1[i] = ( s3[i])
			  | (~s7 & s5[i] & (&ent_valid))
			  | (~s7 & s4[i])
			  ;
	assign s0[i] = s2 & s1[i];
end
endgenerate

exmon300_arb_fp #(
	.N	(NUM_EX_SEQ	)
	) u_arb_ent (
	.valid	(~ent_valid	),
	.ready	(nds_unused_ready),
	.grant	(s4	)
);

exmon300_lfsr #(
	.N	(NUM_EX_SEQ	)
	) u_arb_lfsr (
	.resetn	(aresetn	),
	.clk	(aclk		),
	.update	(s48	),
	.grant	(s5	)
);

always @(posedge aclk or negedge aresetn) begin
	if (!aresetn)
		s40 <= 1'b0;
	else
		s40 <= (|match_exclusive);
end

assign s8 = {1'b0,  s40};
assign us_rresp = (s28 & ~ds_rresp[1]) ?  2'b01 : ds_rresp;
assign us_bresp = ((s73 | s74) & ~ds_bresp[1])  ? s8 : ds_bresp;
assign ds_wstrb = (s73 & ~s40) ? ({(DATA_WIDTH/8){1'b0}}) : (us_wstrb);

assign s79 = {s83, s81, s82, s88, s84, s85, s86, s87, s89, s90};
assign {ds_awlock, ds_awid, ds_awaddr, ds_awsize, ds_awburst, ds_awlen, ds_awcache, ds_awprot, ds_awqos, ds_awregion} = s80;

assign ds_awvalid = s109;
assign s92 = (~s109 | ds_awready);
assign s80  = s107;

assign s110 = (s109 & ~ds_awready) | s91;
always @(posedge aclk or negedge aresetn)begin
    if(!aresetn)begin
        s109 <= 1'b0;
    end
    else begin
        s109 <= s110;
    end
end

assign s108 = s91 & s92;
always @(posedge aclk or negedge aresetn)begin
    if(!aresetn)begin
       s107 <= {(FIFO_WIDTH){1'b0}};
    end
    else if(s108) begin
       s107 <= s79;
    end
end

assign s93 = {s97, s95, s96, s102, s98, s99, s100, s101, s103, s104};
assign {ds_arlock, ds_arid, ds_araddr, ds_arsize, ds_arburst, ds_arlen, ds_arcache, ds_arprot, ds_arqos, ds_arregion} = s94;

assign ds_arvalid = s113;
assign s106 = (~s113 | ds_arready);
assign s94  = s111;

assign s114 = (s113 & ~ds_arready) | s105;
always @(posedge aclk or negedge aresetn)begin
    if(!aresetn)begin
        s113 <= 1'b0;
    end
    else begin
        s113 <= s114;
    end
end

assign s112 = s105 & s106;
always @(posedge aclk or negedge aresetn)begin
    if(!aresetn)begin
       s111 <= {(FIFO_WIDTH){1'b0}};
    end
    else if(s112) begin
       s111 <= s93;
    end
end






endmodule

