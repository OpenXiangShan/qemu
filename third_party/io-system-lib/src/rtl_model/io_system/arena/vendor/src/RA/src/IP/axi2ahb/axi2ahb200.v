module axi2ahb200 (
	  araddr,
	  arburst,
	  arcache,
	  arid,
	  arlen,
	  arlock,
	  arprot,
	  arready,
	  arsize,
	  arvalid,
	  awaddr,
	  awburst,
	  awcache,
	  awid,
	  awlen,
	  awlock,
	  awprot,
	  awready,
	  awsize,
	  awvalid,
	  bid,
	  bready,
	  bresp,
	  bvalid,
	  rdata,
	  rid,
	  rlast,
	  rready,
	  rresp,
	  rvalid,
	  wdata,
	  wlast,
	  wready,
	  wstrb,
	  wvalid,
	  hclk,
	  hresetn,
	  aclk,
	  aresetn,
	  haddr,
	  hburst,
	  hprot,
	  hrdata,
	  hready,
	  hresp,
	  hsize,
	  htrans,
	  hwdata,
	  hwrite
);

parameter ID_WIDTH = 4;
parameter ADDR_WIDTH = 32;
parameter DATA_WIDTH = 32;
parameter CMD_FIFO_DEPTH = 4;
parameter RDATA_FIFO_DEPTH = 4;
parameter BRESP_FIFO_DEPTH = 4;
parameter WDATA_FIFO_DEPTH = 4;

localparam WSTRB_WIDTH = DATA_WIDTH/8;
localparam CMD_WIDTH   = 2+2+8+2+3+ID_WIDTH+ADDR_WIDTH;
localparam RDATA_WIDTH = 1+1+ID_WIDTH+DATA_WIDTH;
localparam WDATA_WIDTH = WSTRB_WIDTH+DATA_WIDTH;
localparam BRESP_WIDTH = 1+ID_WIDTH;
localparam PRODUCT_ID = 32'h00042002;

input           [ADDR_WIDTH-1:0] araddr;
input                      [1:0] arburst;
input                      [3:0] arcache;
input             [ID_WIDTH-1:0] arid;
input                      [7:0] arlen;
input                            arlock;
input                      [2:0] arprot;
output                           arready;
input                      [2:0] arsize;
input                            arvalid;
input           [ADDR_WIDTH-1:0] awaddr;
input                      [1:0] awburst;
input                      [3:0] awcache;
input             [ID_WIDTH-1:0] awid;
input                      [7:0] awlen;
input                            awlock;
input                      [2:0] awprot;
output                           awready;
input                      [2:0] awsize;
input                            awvalid;
output            [ID_WIDTH-1:0] bid;
input                            bready;
output                     [1:0] bresp;
output                           bvalid;
output          [DATA_WIDTH-1:0] rdata;
output            [ID_WIDTH-1:0] rid;
output                           rlast;
input                            rready;
output                     [1:0] rresp;
output                           rvalid;
input           [DATA_WIDTH-1:0] wdata;
input                            wlast;
output                           wready;
input       [(DATA_WIDTH/8)-1:0] wstrb;
input                            wvalid;
input                            hclk;
input                            hresetn;
input                            aclk;
input                            aresetn;
output        [(ADDR_WIDTH-1):0] haddr;
output                     [2:0] hburst;
output                     [3:0] hprot;
input         [(DATA_WIDTH-1):0] hrdata;
input                            hready;
input                            hresp;
output                     [2:0] hsize;
output                     [1:0] htrans;
output        [(DATA_WIDTH-1):0] hwdata;
output                           hwrite;

wire                             s23;
wire             [CMD_WIDTH-1:0] s24;
wire                             s25;
wire                             s26;
wire             [CMD_WIDTH-1:0] s27;
wire                             s28;
wire           [WDATA_WIDTH-1:0] s29;
wire                             s30;
wire                             s31;
wire                             brespq_almost_full;
wire                             s32;
wire                             brespq_full;
wire           [BRESP_WIDTH-1:0] s33;
wire           [BRESP_WIDTH-1:0] brespq_wdata;
wire                             brespq_wr;
wire                             rcmdq_rd;
wire           [RDATA_WIDTH-1:0] rdq_wdata;
wire                             rdq_wr;
wire                             wcmdq_rd;
wire                             wdq_rd;
wire                             s34;
wire                             s35;
wire                             rcmdq_empty;
wire                             s36;
wire             [CMD_WIDTH-1:0] rcmdq_rdata;
wire                             s37;
wire                             rdq_almost_full;
wire                             s38;
wire                             rdq_full;
wire           [RDATA_WIDTH-1:0] s39;
wire                             s40;
wire                             s41;
wire                             wcmdq_empty;
wire                             s42;
wire             [CMD_WIDTH-1:0] wcmdq_rdata;
wire                             s43;
wire                             s44;
wire                             wdq_empty;
wire                             s45;
wire           [WDATA_WIDTH-1:0] wdq_rdata;


assign s24[ADDR_WIDTH-1:0]                                  = araddr[ADDR_WIDTH-1:0];
assign s24[ID_WIDTH+ADDR_WIDTH-1  : ADDR_WIDTH]             = arid[ID_WIDTH-1:0];
assign s24[ID_WIDTH+ADDR_WIDTH+2  : ID_WIDTH+ADDR_WIDTH]    = arsize[2:0];
assign s24[ID_WIDTH+ADDR_WIDTH+4  : ID_WIDTH+ADDR_WIDTH+3]  = arcache[1:0];
assign s24[ID_WIDTH+ADDR_WIDTH+12 : ID_WIDTH+ADDR_WIDTH+5]  = arlen[7:0];
assign s24[ID_WIDTH+ADDR_WIDTH+14 : ID_WIDTH+ADDR_WIDTH+13] = arburst[1:0];
assign s24[ID_WIDTH+ADDR_WIDTH+16 : ID_WIDTH+ADDR_WIDTH+15] = {arprot[2],arprot[0]};
assign arready = ~s36;
assign s25 = arvalid & ~s36;
assign s27[ADDR_WIDTH-1:0]                                  = awaddr[ADDR_WIDTH-1:0];
assign s27[ID_WIDTH+ADDR_WIDTH-1  : ADDR_WIDTH]             = awid[ID_WIDTH-1:0];
assign s27[ID_WIDTH+ADDR_WIDTH+2  : ID_WIDTH+ADDR_WIDTH]    = awsize[2:0];
assign s27[ID_WIDTH+ADDR_WIDTH+4  : ID_WIDTH+ADDR_WIDTH+3]  = awcache[1:0];
assign s27[ID_WIDTH+ADDR_WIDTH+12 : ID_WIDTH+ADDR_WIDTH+5]  = awlen[7:0];
assign s27[ID_WIDTH+ADDR_WIDTH+14 : ID_WIDTH+ADDR_WIDTH+13] = awburst[1:0];
assign s27[ID_WIDTH+ADDR_WIDTH+16 : ID_WIDTH+ADDR_WIDTH+15] = {awprot[2],awprot[0]};
assign awready = ~s42;
assign s28 = awvalid & ~s42;
assign s29[DATA_WIDTH-1:0] = wdata[DATA_WIDTH-1:0];
assign s29[WSTRB_WIDTH+DATA_WIDTH-1:DATA_WIDTH] = wstrb[WSTRB_WIDTH-1:0];
assign s30 = wvalid & ~s45;
assign wready = ~s45;
assign rvalid = ~s38;
assign s26 = rready & ~s38;
assign rdata[DATA_WIDTH-1:0] = s39[DATA_WIDTH-1:0];
assign rlast = s39[DATA_WIDTH];
assign rresp = {s39[DATA_WIDTH+1],1'b0};
assign rid = s39[ID_WIDTH+DATA_WIDTH+1:DATA_WIDTH+2];
assign bvalid = ~s32;
assign s23 = bready & ~s32;
assign bresp = {s33[ID_WIDTH],1'b0};
assign bid = s33[ID_WIDTH-1:0];

fifo2ahb #(
	.ADDR_WIDTH      (ADDR_WIDTH      ),
	.DATA_WIDTH      (DATA_WIDTH      ),
	.ID_WIDTH        (ID_WIDTH        )
) fifo2ahb (
	.haddr             (haddr             ),
	.htrans            (htrans            ),
	.hready            (hready            ),
	.hresp             (hresp             ),
	.hwrite            (hwrite            ),
	.hsize             (hsize             ),
	.hburst            (hburst            ),
	.hprot             (hprot             ),
	.hwdata            (hwdata            ),
	.hrdata            (hrdata            ),
	.rcmdq_rdata       (rcmdq_rdata       ),
	.rcmdq_empty       (rcmdq_empty       ),
	.rcmdq_rd          (rcmdq_rd          ),
	.wcmdq_rdata       (wcmdq_rdata       ),
	.wcmdq_empty       (wcmdq_empty       ),
	.wcmdq_rd          (wcmdq_rd          ),
	.wdq_rdata         (wdq_rdata         ),
	.wdq_empty         (wdq_empty         ),
	.wdq_rd            (wdq_rd            ),
	.rdq_wdata         (rdq_wdata         ),
	.rdq_wr            (rdq_wr            ),
	.rdq_almost_full   (rdq_almost_full   ),
	.rdq_full          (rdq_full          ),
	.brespq_wdata      (brespq_wdata      ),
	.brespq_wr         (brespq_wr         ),
	.brespq_almost_full(brespq_almost_full),
	.brespq_full       (brespq_full       ),
	.clk               (hclk              ),
	.resetn            (hresetn           )
);

async_fifo_afe #(
	.DATA_WIDTH      (CMD_WIDTH       ),
	.FIFO_DEPTH      (CMD_FIFO_DEPTH  )
) rcmdq_fifo (
	.w_reset_n   (aresetn           ),
	.r_reset_n   (hresetn           ),
	.w_clk       (aclk              ),
	.r_clk       (hclk              ),
	.wr          (s25          ),
	.wr_data     (s24       ),
	.rd          (rcmdq_rd          ),
	.rd_data     (rcmdq_rdata       ),
	.empty       (rcmdq_empty       ),
	.full        (s36        ),
	.almost_empty(s34),
	.almost_full (s35 )
);

async_fifo_afe #(
	.DATA_WIDTH      (CMD_WIDTH       ),
	.FIFO_DEPTH      (CMD_FIFO_DEPTH  )
) wcmdq_fifo (
	.w_reset_n   (aresetn           ),
	.r_reset_n   (hresetn           ),
	.w_clk       (aclk              ),
	.r_clk       (hclk              ),
	.wr          (s28          ),
	.wr_data     (s27       ),
	.rd          (wcmdq_rd          ),
	.rd_data     (wcmdq_rdata       ),
	.empty       (wcmdq_empty       ),
	.full        (s42        ),
	.almost_empty(s40),
	.almost_full (s41 )
);

async_fifo_afe #(
	.DATA_WIDTH      (WDATA_WIDTH     ),
	.FIFO_DEPTH      (WDATA_FIFO_DEPTH)
) wdq_fifo (
	.w_reset_n   (aresetn         ),
	.r_reset_n   (hresetn         ),
	.w_clk       (aclk            ),
	.r_clk       (hclk            ),
	.wr          (s30          ),
	.wr_data     (s29       ),
	.rd          (wdq_rd          ),
	.rd_data     (wdq_rdata       ),
	.empty       (wdq_empty       ),
	.full        (s45        ),
	.almost_empty(s43),
	.almost_full (s44 )
);

async_fifo_afe #(
	.ALMOST_FULL_THRESHOLD(2               ),
	.DATA_WIDTH      (RDATA_WIDTH     ),
	.FIFO_DEPTH      (RDATA_FIFO_DEPTH)
) rdq_fifo (
	.w_reset_n   (hresetn         ),
	.r_reset_n   (aresetn         ),
	.w_clk       (hclk            ),
	.r_clk       (aclk            ),
	.wr          (rdq_wr          ),
	.wr_data     (rdq_wdata       ),
	.rd          (s26          ),
	.rd_data     (s39       ),
	.empty       (s38       ),
	.full        (rdq_full        ),
	.almost_empty(s37),
	.almost_full (rdq_almost_full )
);

async_fifo_afe #(
	.ALMOST_FULL_THRESHOLD(2               ),
	.DATA_WIDTH      (BRESP_WIDTH     ),
	.FIFO_DEPTH      (BRESP_FIFO_DEPTH)
) brespq_fifo (
	.w_reset_n   (hresetn            ),
	.r_reset_n   (aresetn            ),
	.w_clk       (hclk               ),
	.r_clk       (aclk               ),
	.wr          (brespq_wr          ),
	.wr_data     (brespq_wdata       ),
	.rd          (s23          ),
	.rd_data     (s33       ),
	.empty       (s32       ),
	.full        (brespq_full        ),
	.almost_empty(s31),
	.almost_full (brespq_almost_full )
);

endmodule
