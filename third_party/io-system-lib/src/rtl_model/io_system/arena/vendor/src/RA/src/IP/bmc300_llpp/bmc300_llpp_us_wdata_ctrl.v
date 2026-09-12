
`include "bmc300_llpp_config.vh"
`include "bmc300_llpp_const.vh"

module bmc300_llpp_us_wdata_ctrl (
	  self_id,
`ifdef BMC300_LLPP_SLV0_SUPPORT
	  slv0_wmid,
	  slv0_wready,
	  slv0_connect,
`endif
`ifdef BMC300_LLPP_SLV1_SUPPORT
	  slv1_wmid,
	  slv1_wready,
	  slv1_connect,
`endif
`ifdef BMC300_LLPP_SLV2_SUPPORT
	  slv2_wmid,
	  slv2_wready,
	  slv2_connect,
`endif
`ifdef BMC300_LLPP_SLV3_SUPPORT
	  slv3_wmid,
	  slv3_wready,
	  slv3_connect,
`endif
`ifdef BMC300_LLPP_SLV4_SUPPORT
	  slv4_wmid,
	  slv4_wready,
	  slv4_connect,
`endif
`ifdef BMC300_LLPP_SLV5_SUPPORT
	  slv5_wmid,
	  slv5_wready,
	  slv5_connect,
`endif
`ifdef BMC300_LLPP_SLV6_SUPPORT
	  slv6_wmid,
	  slv6_wready,
	  slv6_connect,
`endif
`ifdef BMC300_LLPP_SLV7_SUPPORT
	  slv7_wmid,
	  slv7_wready,
	  slv7_connect,
`endif
`ifdef BMC300_LLPP_SLV8_SUPPORT
	  slv8_wmid,
	  slv8_wready,
	  slv8_connect,
`endif
`ifdef BMC300_LLPP_SLV9_SUPPORT
	  slv9_wmid,
	  slv9_wready,
	  slv9_connect,
`endif
`ifdef BMC300_LLPP_SLV10_SUPPORT
	  slv10_wmid,
	  slv10_wready,
	  slv10_connect,
`endif
`ifdef BMC300_LLPP_SLV11_SUPPORT
	  slv11_wmid,
	  slv11_wready,
	  slv11_connect,
`endif
`ifdef BMC300_LLPP_SLV12_SUPPORT
	  slv12_wmid,
	  slv12_wready,
	  slv12_connect,
`endif
`ifdef BMC300_LLPP_SLV13_SUPPORT
	  slv13_wmid,
	  slv13_wready,
	  slv13_connect,
`endif
`ifdef BMC300_LLPP_SLV14_SUPPORT
	  slv14_wmid,
	  slv14_wready,
	  slv14_connect,
`endif
`ifdef BMC300_LLPP_SLV15_SUPPORT
	  slv15_wmid,
	  slv15_wready,
	  slv15_connect,
`endif
`ifdef BMC300_LLPP_SLV16_SUPPORT
	  slv16_wmid,
	  slv16_wready,
	  slv16_connect,
`endif
`ifdef BMC300_LLPP_SLV17_SUPPORT
	  slv17_wmid,
	  slv17_wready,
	  slv17_connect,
`endif
`ifdef BMC300_LLPP_SLV18_SUPPORT
	  slv18_wmid,
	  slv18_wready,
	  slv18_connect,
`endif
`ifdef BMC300_LLPP_SLV19_SUPPORT
	  slv19_wmid,
	  slv19_wready,
	  slv19_connect,
`endif
`ifdef BMC300_LLPP_SLV20_SUPPORT
	  slv20_wmid,
	  slv20_wready,
	  slv20_connect,
`endif
`ifdef BMC300_LLPP_SLV21_SUPPORT
	  slv21_wmid,
	  slv21_wready,
	  slv21_connect,
`endif
`ifdef BMC300_LLPP_SLV22_SUPPORT
	  slv22_wmid,
	  slv22_wready,
	  slv22_connect,
`endif
`ifdef BMC300_LLPP_SLV23_SUPPORT
	  slv23_wmid,
	  slv23_wready,
	  slv23_connect,
`endif
`ifdef BMC300_LLPP_SLV24_SUPPORT
	  slv24_wmid,
	  slv24_wready,
	  slv24_connect,
`endif
`ifdef BMC300_LLPP_SLV25_SUPPORT
	  slv25_wmid,
	  slv25_wready,
	  slv25_connect,
`endif
`ifdef BMC300_LLPP_SLV26_SUPPORT
	  slv26_wmid,
	  slv26_wready,
	  slv26_connect,
`endif
`ifdef BMC300_LLPP_SLV27_SUPPORT
	  slv27_wmid,
	  slv27_wready,
	  slv27_connect,
`endif
`ifdef BMC300_LLPP_SLV28_SUPPORT
	  slv28_wmid,
	  slv28_wready,
	  slv28_connect,
`endif
`ifdef BMC300_LLPP_SLV29_SUPPORT
	  slv29_wmid,
	  slv29_wready,
	  slv29_connect,
`endif
`ifdef BMC300_LLPP_SLV30_SUPPORT
	  slv30_wmid,
	  slv30_wready,
	  slv30_connect,
`endif
`ifdef BMC300_LLPP_SLV31_SUPPORT
	  slv31_wmid,
	  slv31_wready,
	  slv31_connect,
`endif
	  us_wdata,
	  us_wlast,
	  us_wstrb,
	  us_wvalid,
	  us_wready,
	  aw_outstanding_id,
	  aw_addr_outstanding_en,
	  aw_outstanding_ready,
	  wd_outstanding_idle,
	  resp_outstanding_en,
	  outstanding_resp_ready,
	  dec_err_wready,
	  dec_err_wvalid,
	  dec_err_wlast,
	  mst_wsid,
	  mst_wvalid,
	  mst_wdata,
	  mst_wlast,
	  mst_wstrb,
	  aclk,
	  aresetn
);
parameter DATA_WIDTH = 64;
parameter OUTSTAND_ID_WIDTH = 5;
parameter OUTSTANDING_DEPTH = 2;

localparam DATA_MSB = DATA_WIDTH - 1;
localparam OUTSTAND_ID_MSB = OUTSTAND_ID_WIDTH - 1;
localparam WD_FIFO_WIDTH = DATA_WIDTH + DATA_WIDTH/8 +1;
localparam WD_FIFO_DEPTH = 8;

input [3:0] 	    self_id;

`ifdef BMC300_LLPP_SLV0_SUPPORT
input [3:0]        slv0_wmid;
input              slv0_wready;
input 	 	slv0_connect;
`endif
`ifdef BMC300_LLPP_SLV1_SUPPORT
input [3:0]        slv1_wmid;
input              slv1_wready;
input 	 	slv1_connect;
`endif
`ifdef BMC300_LLPP_SLV2_SUPPORT
input [3:0]        slv2_wmid;
input              slv2_wready;
input 	 	slv2_connect;
`endif
`ifdef BMC300_LLPP_SLV3_SUPPORT
input [3:0]        slv3_wmid;
input              slv3_wready;
input 	 	slv3_connect;
`endif
`ifdef BMC300_LLPP_SLV4_SUPPORT
input [3:0]        slv4_wmid;
input              slv4_wready;
input 	 	slv4_connect;
`endif
`ifdef BMC300_LLPP_SLV5_SUPPORT
input [3:0]        slv5_wmid;
input              slv5_wready;
input 	 	slv5_connect;
`endif
`ifdef BMC300_LLPP_SLV6_SUPPORT
input [3:0]        slv6_wmid;
input              slv6_wready;
input 	 	slv6_connect;
`endif
`ifdef BMC300_LLPP_SLV7_SUPPORT
input [3:0]        slv7_wmid;
input              slv7_wready;
input 	 	slv7_connect;
`endif
`ifdef BMC300_LLPP_SLV8_SUPPORT
input [3:0]        slv8_wmid;
input              slv8_wready;
input 	 	slv8_connect;
`endif
`ifdef BMC300_LLPP_SLV9_SUPPORT
input [3:0]        slv9_wmid;
input              slv9_wready;
input 	 	slv9_connect;
`endif
`ifdef BMC300_LLPP_SLV10_SUPPORT
input [3:0]        slv10_wmid;
input              slv10_wready;
input 	 	slv10_connect;
`endif
`ifdef BMC300_LLPP_SLV11_SUPPORT
input [3:0]        slv11_wmid;
input              slv11_wready;
input 	 	slv11_connect;
`endif
`ifdef BMC300_LLPP_SLV12_SUPPORT
input [3:0]        slv12_wmid;
input              slv12_wready;
input 	 	slv12_connect;
`endif
`ifdef BMC300_LLPP_SLV13_SUPPORT
input [3:0]        slv13_wmid;
input              slv13_wready;
input 	 	slv13_connect;
`endif
`ifdef BMC300_LLPP_SLV14_SUPPORT
input [3:0]        slv14_wmid;
input              slv14_wready;
input 	 	slv14_connect;
`endif
`ifdef BMC300_LLPP_SLV15_SUPPORT
input [3:0]        slv15_wmid;
input              slv15_wready;
input 	 	slv15_connect;
`endif
`ifdef BMC300_LLPP_SLV16_SUPPORT
input [3:0]        slv16_wmid;
input              slv16_wready;
input 	 	slv16_connect;
`endif
`ifdef BMC300_LLPP_SLV17_SUPPORT
input [3:0]        slv17_wmid;
input              slv17_wready;
input 	 	slv17_connect;
`endif
`ifdef BMC300_LLPP_SLV18_SUPPORT
input [3:0]        slv18_wmid;
input              slv18_wready;
input 	 	slv18_connect;
`endif
`ifdef BMC300_LLPP_SLV19_SUPPORT
input [3:0]        slv19_wmid;
input              slv19_wready;
input 	 	slv19_connect;
`endif
`ifdef BMC300_LLPP_SLV20_SUPPORT
input [3:0]        slv20_wmid;
input              slv20_wready;
input 	 	slv20_connect;
`endif
`ifdef BMC300_LLPP_SLV21_SUPPORT
input [3:0]        slv21_wmid;
input              slv21_wready;
input 	 	slv21_connect;
`endif
`ifdef BMC300_LLPP_SLV22_SUPPORT
input [3:0]        slv22_wmid;
input              slv22_wready;
input 	 	slv22_connect;
`endif
`ifdef BMC300_LLPP_SLV23_SUPPORT
input [3:0]        slv23_wmid;
input              slv23_wready;
input 	 	slv23_connect;
`endif
`ifdef BMC300_LLPP_SLV24_SUPPORT
input [3:0]        slv24_wmid;
input              slv24_wready;
input 	 	slv24_connect;
`endif
`ifdef BMC300_LLPP_SLV25_SUPPORT
input [3:0]        slv25_wmid;
input              slv25_wready;
input 	 	slv25_connect;
`endif
`ifdef BMC300_LLPP_SLV26_SUPPORT
input [3:0]        slv26_wmid;
input              slv26_wready;
input 	 	slv26_connect;
`endif
`ifdef BMC300_LLPP_SLV27_SUPPORT
input [3:0]        slv27_wmid;
input              slv27_wready;
input 	 	slv27_connect;
`endif
`ifdef BMC300_LLPP_SLV28_SUPPORT
input [3:0]        slv28_wmid;
input              slv28_wready;
input 	 	slv28_connect;
`endif
`ifdef BMC300_LLPP_SLV29_SUPPORT
input [3:0]        slv29_wmid;
input              slv29_wready;
input 	 	slv29_connect;
`endif
`ifdef BMC300_LLPP_SLV30_SUPPORT
input [3:0]        slv30_wmid;
input              slv30_wready;
input 	 	slv30_connect;
`endif
`ifdef BMC300_LLPP_SLV31_SUPPORT
input [3:0]        slv31_wmid;
input              slv31_wready;
input 	 	slv31_connect;
`endif
input [DATA_MSB:0] us_wdata;
input 	           us_wlast;
input [(DATA_WIDTH/8)-1:0] us_wstrb;
input              us_wvalid;
output             us_wready;

input [OUTSTAND_ID_MSB:0] aw_outstanding_id;
input	  	   aw_addr_outstanding_en;
output		   aw_outstanding_ready;
output		   wd_outstanding_idle;

output		   resp_outstanding_en;
input		   outstanding_resp_ready;
input		   dec_err_wready;
output		   dec_err_wvalid;
output		   dec_err_wlast;
output [4:0]       mst_wsid;
output		   	    mst_wvalid;
output [DATA_MSB:0] 	    mst_wdata;
output 	                    mst_wlast;
output [(DATA_WIDTH/8)-1:0] mst_wstrb;

input aclk;
input aresetn;





// b31515dd rnd


wire 			 s0;
wire [31:0] 		 s1;
wire [OUTSTAND_ID_MSB:0] s2;
wire [OUTSTAND_ID_MSB:0] s3;
wire                     s4;
wire			 s5;
wire                     s6;
wire			 s7;

wire [WD_FIFO_WIDTH-1:0] s8;
wire [WD_FIFO_WIDTH-1:0] s9;
wire                     s10;
wire			 s11;
wire                     s12;
wire			 s13;

assign aw_outstanding_ready = ~s5;
assign wd_outstanding_idle  = s4;
assign s6 = ~s10 & mst_wlast & outstanding_resp_ready & ~s4 & s0;
assign s7 = aw_addr_outstanding_en;
assign s2 = aw_outstanding_id;
assign resp_outstanding_en = s6;
assign mst_wsid = s3[4:0];


`ifdef BMC300_LLPP_SLV0_SUPPORT
	assign s1[0] = (slv0_wmid==self_id) & slv0_wready & slv0_connect;
`else
	assign s1[0] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV1_SUPPORT
	assign s1[1] = (slv1_wmid==self_id) & slv1_wready & slv1_connect;
`else
	assign s1[1] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV2_SUPPORT
	assign s1[2] = (slv2_wmid==self_id) & slv2_wready & slv2_connect;
`else
	assign s1[2] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV3_SUPPORT
	assign s1[3] = (slv3_wmid==self_id) & slv3_wready & slv3_connect;
`else
	assign s1[3] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV4_SUPPORT
	assign s1[4] = (slv4_wmid==self_id) & slv4_wready & slv4_connect;
`else
	assign s1[4] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV5_SUPPORT
	assign s1[5] = (slv5_wmid==self_id) & slv5_wready & slv5_connect;
`else
	assign s1[5] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV6_SUPPORT
	assign s1[6] = (slv6_wmid==self_id) & slv6_wready & slv6_connect;
`else
	assign s1[6] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV7_SUPPORT
	assign s1[7] = (slv7_wmid==self_id) & slv7_wready & slv7_connect;
`else
	assign s1[7] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV8_SUPPORT
	assign s1[8] = (slv8_wmid==self_id) & slv8_wready & slv8_connect;
`else
	assign s1[8] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV9_SUPPORT
	assign s1[9] = (slv9_wmid==self_id) & slv9_wready & slv9_connect;
`else
	assign s1[9] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV10_SUPPORT
	assign s1[10] = (slv10_wmid==self_id) & slv10_wready & slv10_connect;
`else
	assign s1[10] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV11_SUPPORT
	assign s1[11] = (slv11_wmid==self_id) & slv11_wready & slv11_connect;
`else
	assign s1[11] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV12_SUPPORT
	assign s1[12] = (slv12_wmid==self_id) & slv12_wready & slv12_connect;
`else
	assign s1[12] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV13_SUPPORT
	assign s1[13] = (slv13_wmid==self_id) & slv13_wready & slv13_connect;
`else
	assign s1[13] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV14_SUPPORT
	assign s1[14] = (slv14_wmid==self_id) & slv14_wready & slv14_connect;
`else
	assign s1[14] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV15_SUPPORT
	assign s1[15] = (slv15_wmid==self_id) & slv15_wready & slv15_connect;
`else
	assign s1[15] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV16_SUPPORT
	assign s1[16] = (slv16_wmid==self_id) & slv16_wready & slv16_connect;
`else
	assign s1[16] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV17_SUPPORT
	assign s1[17] = (slv17_wmid==self_id) & slv17_wready & slv17_connect;
`else
	assign s1[17] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV18_SUPPORT
	assign s1[18] = (slv18_wmid==self_id) & slv18_wready & slv18_connect;
`else
	assign s1[18] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV19_SUPPORT
	assign s1[19] = (slv19_wmid==self_id) & slv19_wready & slv19_connect;
`else
	assign s1[19] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV20_SUPPORT
	assign s1[20] = (slv20_wmid==self_id) & slv20_wready & slv20_connect;
`else
	assign s1[20] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV21_SUPPORT
	assign s1[21] = (slv21_wmid==self_id) & slv21_wready & slv21_connect;
`else
	assign s1[21] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV22_SUPPORT
	assign s1[22] = (slv22_wmid==self_id) & slv22_wready & slv22_connect;
`else
	assign s1[22] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV23_SUPPORT
	assign s1[23] = (slv23_wmid==self_id) & slv23_wready & slv23_connect;
`else
	assign s1[23] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV24_SUPPORT
	assign s1[24] = (slv24_wmid==self_id) & slv24_wready & slv24_connect;
`else
	assign s1[24] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV25_SUPPORT
	assign s1[25] = (slv25_wmid==self_id) & slv25_wready & slv25_connect;
`else
	assign s1[25] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV26_SUPPORT
	assign s1[26] = (slv26_wmid==self_id) & slv26_wready & slv26_connect;
`else
	assign s1[26] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV27_SUPPORT
	assign s1[27] = (slv27_wmid==self_id) & slv27_wready & slv27_connect;
`else
	assign s1[27] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV28_SUPPORT
	assign s1[28] = (slv28_wmid==self_id) & slv28_wready & slv28_connect;
`else
	assign s1[28] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV29_SUPPORT
	assign s1[29] = (slv29_wmid==self_id) & slv29_wready & slv29_connect;
`else
	assign s1[29] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV30_SUPPORT
	assign s1[30] = (slv30_wmid==self_id) & slv30_wready & slv30_connect;
`else
	assign s1[30] = 1'b0;
`endif
`ifdef BMC300_LLPP_SLV31_SUPPORT
	assign s1[31] = (slv31_wmid==self_id) & slv31_wready & slv31_connect;
`else
	assign s1[31] = 1'b0;
`endif
assign us_wready = ~s11;
assign dec_err_wvalid = ~s10 & s4;
assign dec_err_wlast = mst_wlast;
assign s0 = s1[mst_wsid];

sync_fifo_afe #(
                .DATA_WIDTH (OUTSTAND_ID_WIDTH),
                .FIFO_DEPTH (OUTSTANDING_DEPTH)
) wsid_fifo (
	.reset_n     (aresetn               ),
	.clk         (aclk                  ),
	.wr          (s7          ),
	.wr_data     (s2       ),
	.rd          (s6          ),
	.rd_data     (s3       ),
	.almost_empty(                      ),
	.almost_full (                      ),
	.empty       (s4       ),
	.full        (s5        )
);

sync_fifo_afe #(
                .DATA_WIDTH(WD_FIFO_WIDTH),
                .FIFO_DEPTH(WD_FIFO_DEPTH)
) wdata_fifo (
	.reset_n     (aresetn               ),
	.clk         (aclk                  ),
	.wr          (s13         ),
	.wr_data     (s8      ),
	.rd          (s12         ),
	.rd_data     (s9      ),
	.almost_empty(                      ),
	.almost_full (                      ),
	.empty       (s10      ),
	.full        (s11       )
);

assign s8 = {us_wlast,us_wstrb, us_wdata};
assign s13    = us_wvalid & ~s11;
assign s12    = ~s10 & (dec_err_wready | (s0 & (~mst_wlast | outstanding_resp_ready) & ~s4));
assign mst_wlast        = s9[WD_FIFO_WIDTH-1];
assign mst_wstrb        = s9[DATA_WIDTH+DATA_WIDTH/8-1:DATA_WIDTH];
assign mst_wdata        = s9[DATA_WIDTH-1:0];
assign mst_wvalid       = ~s10 & ~(mst_wlast & ~outstanding_resp_ready) & ~s4;






endmodule
