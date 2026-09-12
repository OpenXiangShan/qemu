// Copyright (C) 2024, Andes Technology Corp. Confidential Proprietary
// AX45MPV_PREMIUM v30.1.0


module sizeup300(
	aclk,
	aresetn,

	us_arvalid,
	us_arready,
	ds_arvalid,
	ds_arready,

	us_awvalid,
	us_awready,
	ds_awvalid,
	ds_awready,

	us_rid,
	us_rvalid,
	us_rdata,
	us_rready,
	ds_rlast,
	ds_rvalid,
	ds_rdata,
	ds_rready,

	us_wstrb,
	us_wlast,
	us_wvalid,
	us_wdata,
	us_wready,

	ds_wstrb,
	ds_wvalid,
	ds_wdata,
	ds_wready,

	us_bid,
	us_bvalid,
	us_bready,

	ds_bvalid,
	ds_bready,

	us_arid,
	us_araddr,
	us_arburst,
	us_arlen,
	us_arsize,

	us_awid,
	us_awaddr,
	us_awburst,
	us_awlen,
	us_awsize,

	ds_wlast,
	us_rlast,
	us_rresp,
	ds_rresp
);

parameter ID_WIDTH      = 4;
parameter US_DATA_WIDTH = 32;
parameter DS_DATA_WIDTH = 128;
parameter US_DATA_SIZE = $clog2(US_DATA_WIDTH) - 3;
parameter DS_DATA_SIZE = $clog2(DS_DATA_WIDTH) - 3;
parameter DATA_RATIO = DS_DATA_WIDTH/US_DATA_WIDTH;
parameter DATA_BUFFERING = 0;

localparam ADDR_WIDTH	= DS_DATA_SIZE;
localparam ADDR_MSB	= ADDR_WIDTH - 1;

localparam RDATA_FIFO_WIDTH = ID_WIDTH + ADDR_WIDTH + 4 + 3 + 2;
localparam WDATA_FIFO_WIDTH = ADDR_WIDTH + 4 + 3 + 2;
localparam WRESP_FIFO_WIDTH = ID_WIDTH;
localparam WDATA_BUFFER_WIDTH = US_DATA_WIDTH + (US_DATA_WIDTH/8) + 1;
localparam RDATA_BUFFER_WIDTH = DS_DATA_WIDTH + 2 + 1;

localparam FIXED = 2'b00;
localparam INCR  = 2'b01;
localparam WRAP  = 2'b10;

localparam PRODUCT_ID 	= 32'h000a3002;

input                            aclk;
input                            aresetn;

input                   	us_arvalid;
input	[ID_WIDTH-1:0] 		us_arid;
input	[ADDR_MSB:0]	 	us_araddr;
input	[3:0]	 		us_arlen;
input	[2:0]	 		us_arsize;
input	[1:0]	 		us_arburst;
output                  	us_arready;

input                   	us_awvalid;
input	[ID_WIDTH-1:0] 		us_awid;
input	[ADDR_MSB:0]	 	us_awaddr;
input	[3:0]	 		us_awlen;
input	[2:0]	 		us_awsize;
input	[1:0]	 		us_awburst;
output                  	us_awready;

output                   	ds_arvalid;
input                  	 	ds_arready;
output                   	ds_awvalid;
input                  	 	ds_awready;

output	[ID_WIDTH-1:0] 		us_rid;
output                    	us_rvalid;
output  [US_DATA_WIDTH-1:0] 	us_rdata;
input                     	us_rready;

input                    	ds_rlast;
input                    	ds_rvalid;
input	[DS_DATA_WIDTH-1:0] 	ds_rdata;
output				ds_rready;

input  	[(US_DATA_WIDTH/8)-1:0]	us_wstrb;
input                   	us_wlast;
input                   	us_wvalid;
input  	[US_DATA_WIDTH-1:0] 	us_wdata;
output                    	us_wready;

output [(DS_DATA_WIDTH/8)-1:0] 	ds_wstrb;
output                   	ds_wvalid;
output  [DS_DATA_WIDTH-1:0] 	ds_wdata;
input                    	ds_wready;

output				ds_wlast;
output				us_rlast;
output	[1:0]			us_rresp;
input	[1:0]			ds_rresp;

output	[ID_WIDTH-1:0] 		us_bid;
output            		us_bvalid;
input        			us_bready;

input                  		ds_bvalid;
output                  	ds_bready;





// 80c8d12f rnd



wire	[ID_WIDTH-1:0] 		us_bid;
wire	          		us_bvalid;

wire	[ID_WIDTH-1:0] 		us_rid_a1;
wire                   		us_rvalid_a1;
wire  	[US_DATA_WIDTH-1:0] 	us_rdata_a1;

wire       	            	ds_wvalid_a1;
wire 	[(DS_DATA_WIDTH/8)-1:0] ds_wstrb_a1;
wire  	[DS_DATA_WIDTH-1:0] 	ds_wdata_a1;

wire 	[RDATA_FIFO_WIDTH-1:0]	rcmd_fifo_rdata;
wire 	[RDATA_FIFO_WIDTH-1:0]	rcmd_fifo_wdata;
wire				rcmd_fifo_rd;
wire				rcmd_fifo_wr;
wire				rcmd_fifo_empty;
wire				rcmd_fifo_full;

wire 	[WDATA_FIFO_WIDTH-1:0]	wcmd_fifo_rdata;
wire 	[WDATA_FIFO_WIDTH-1:0]	wcmd_fifo_wdata;
wire				wcmd_fifo_rd;
wire				wcmd_fifo_wr;
wire				wcmd_fifo_empty;

wire 	[WRESP_FIFO_WIDTH-1:0]	wid_fifo_rdata;
wire 	[WRESP_FIFO_WIDTH-1:0]	wid_fifo_wdata;
wire				wid_fifo_rd;
wire				wid_fifo_wr;
wire				wid_fifo_empty;
wire				wid_fifo_full;

wire	[ID_WIDTH-1:0] 		rdata_id;
wire	[ADDR_MSB:0]	 	rdata_addr;
wire	[3:0]	 		rdata_len;
wire	[2:0]	 		rdata_size;
wire	[1:0]	 		rdata_burst;

wire	[ADDR_MSB:0]	 	wdata_addr;
wire	[3:0]	 		wdata_len;
wire	[2:0]	 		wdata_size;
wire	[1:0]	 		wdata_burst;

wire			 	ds_mux_rvalid;
wire	                    	ds_mux_rlast;
wire	[DS_DATA_WIDTH-1:0] 	ds_mux_rdata;

wire				us_mux_wvalid;
wire	[(US_DATA_WIDTH/8)-1:0]	us_mux_wstrb;
wire                  		us_mux_wlast;
wire  	[US_DATA_WIDTH-1:0] 	us_mux_wdata;

wire    [ADDR_MSB:0] 		rdata_wrap_mask;
wire    [ADDR_MSB:0] 		rdata_incr_addr;
wire    [ADDR_MSB:0] 		wdata_wrap_mask;
wire    [ADDR_MSB:0] 		wdata_incr_addr;

reg				rdata_addr_sel_a1;
reg				rdata_addr_sel;
wire	[ADDR_MSB:0]		rdata_addr_mux;
reg	[ADDR_MSB:0]		next_rdata_addr_a1;
reg	[ADDR_MSB:0]		next_rdata_addr;
reg	[ADDR_MSB:0]		rdata_update_addr_a1;
reg	[ADDR_MSB:0]		rdata_update_addr;

reg				wdata_addr_sel_a1;
reg				wdata_addr_sel;
wire	[ADDR_MSB:0]		wdata_addr_mux;
reg	[ADDR_MSB:0]		next_wdata_addr_a1;
reg	[ADDR_MSB:0]		next_wdata_addr;
reg	[ADDR_MSB:0]		wdata_update_size_a1;
reg	[ADDR_MSB:0]		wdata_update_size;

assign us_arready	= ds_arready && (!rcmd_fifo_full);
assign ds_arvalid	= us_arvalid && (!rcmd_fifo_full);

assign us_awready	= ds_awready && (!wid_fifo_full);
assign ds_awvalid	= us_awvalid && (!wid_fifo_full);

assign us_rvalid_a1	= ds_mux_rvalid && (!rcmd_fifo_empty);
assign us_rid_a1	= rdata_id;
assign rcmd_fifo_wdata 	= {us_arid, us_araddr, us_arlen, us_arsize, us_arburst};
assign rcmd_fifo_wr 	= ds_arvalid && ds_arready;

assign rdata_id		= rcmd_fifo_rdata[RDATA_FIFO_WIDTH-1:ADDR_WIDTH+9];
assign rdata_addr	= rcmd_fifo_rdata[ADDR_WIDTH+9-1:9];
assign rdata_len	= rcmd_fifo_rdata[8:5];
assign rdata_size	= rcmd_fifo_rdata[4:2];
assign rdata_burst	= rcmd_fifo_rdata[1:0];

assign rdata_wrap_mask 	= (rdata_burst == WRAP) ? ~rdata_len[3:0] << {1'b0, rdata_size} : {(ADDR_WIDTH){1'b0}};
assign rdata_incr_addr	= rdata_addr_mux + rdata_update_addr_a1;
assign rdata_addr_mux 	= rdata_addr_sel ? rdata_addr : next_rdata_addr;

assign ds_wvalid_a1	= us_mux_wvalid && (!wcmd_fifo_empty);
assign wcmd_fifo_wdata 	= {us_awaddr, us_awlen, us_awsize, us_awburst};
assign wcmd_fifo_wr 	= ds_awvalid && ds_awready;

assign wdata_addr	= wcmd_fifo_rdata[ADDR_WIDTH+9-1:9];
assign wdata_len	= wcmd_fifo_rdata[8:5];
assign wdata_size	= wcmd_fifo_rdata[4:2];
assign wdata_burst	= wcmd_fifo_rdata[1:0];

assign wdata_wrap_mask 	= (wdata_burst == WRAP) ? ~wdata_len[3:0] << {1'b0, wdata_size} : {(ADDR_WIDTH){1'b0}};
assign wdata_incr_addr	= wdata_addr_mux + wdata_update_size_a1;
assign wdata_addr_mux 	= wdata_addr_sel ? wdata_addr : next_wdata_addr;

assign ds_bready	= us_bready && (!wid_fifo_empty);
assign us_bvalid	= ds_bvalid && (!wid_fifo_empty);
assign us_bid		= wid_fifo_rdata;

assign wid_fifo_wdata 	= us_awid;
assign wid_fifo_wr 	= ds_awvalid && ds_awready;
assign wid_fifo_rd	= (!wid_fifo_empty) && ds_bready && ds_bvalid;


always @(posedge aclk or negedge aresetn) begin
	if (!aresetn) begin
		rdata_addr_sel 	<= 1'b1;
	end
	else begin
		rdata_addr_sel 	<= rdata_addr_sel_a1;
	end
end

always @(*) begin
	if (rdata_addr_sel) begin
		if (rdata_burst == FIXED) begin
			rdata_update_addr_a1	= {(ADDR_WIDTH){1'b0}};
		end
		else begin
			rdata_update_addr_a1	= (4'h1 << {1'h0, rdata_size});
		end
	end
	else begin
		rdata_update_addr_a1	= rdata_update_addr;
	end
end

always @(posedge aclk or negedge aresetn) begin
	if (!aresetn) begin
		next_rdata_addr		<= {(ADDR_WIDTH){1'b0}};
		rdata_update_addr	<= {(ADDR_WIDTH){1'b0}};
	end
	else begin
		next_rdata_addr		<= next_rdata_addr_a1;
		rdata_update_addr	<= rdata_update_addr_a1;
	end
end

genvar gnv_i;

assign	us_rdata_a1 = ds_mux_rdata >> (rdata_addr_mux[DS_DATA_SIZE-1:US_DATA_SIZE]*US_DATA_WIDTH);

always @(posedge aclk or negedge aresetn) begin
	if (!aresetn) begin
		wdata_addr_sel <= 1'b0;
	end
	else begin
		wdata_addr_sel <= wdata_addr_sel_a1;
	end
end

always @(*) begin
	if (wdata_addr_sel) begin
		if (wdata_burst == FIXED) begin
			wdata_update_size_a1	= {(ADDR_WIDTH){1'b0}};
		end
		else begin
			wdata_update_size_a1	= (4'h1 << {1'h0, wdata_size});
		end
	end
	else begin
		wdata_update_size_a1	= wdata_update_size;
	end
end


always @(posedge aclk or negedge aresetn) begin
	if (!aresetn) begin
		next_wdata_addr	<= {(ADDR_WIDTH){1'b0}};
		wdata_update_size	<= {(ADDR_WIDTH){1'b0}};
	end
	else begin
		next_wdata_addr	<= next_wdata_addr_a1;
		wdata_update_size	<= wdata_update_size_a1;
	end
end

generate
for (gnv_i = 0; gnv_i < DATA_RATIO; gnv_i = gnv_i + 1) begin: wdata_mux
	assign ds_wdata_a1[US_DATA_WIDTH*(gnv_i+1)-1:US_DATA_WIDTH*gnv_i] = us_mux_wdata;
end
endgenerate

assign	ds_wstrb_a1 = us_mux_wstrb << (wdata_addr_mux[DS_DATA_SIZE-1:US_DATA_SIZE]*($unsigned(US_DATA_WIDTH)>>3));

generate
if (DATA_BUFFERING[0] == 1'b1) begin :data_buffering_signal
	reg 	[RDATA_BUFFER_WIDTH-1:0]rdata_buffer;
	reg 	[RDATA_BUFFER_WIDTH-1:0]rdata_buffer_a1;
	reg				rdata_buffer_valid;
	reg				rdata_buffer_valid_a1;

	reg 	[WDATA_BUFFER_WIDTH-1:0]wdata_buffer;
	reg 	[WDATA_BUFFER_WIDTH-1:0]wdata_buffer_a1;
	reg				wdata_buffer_valid;
	reg				wdata_buffer_valid_a1;

	reg	[ID_WIDTH-1:0] 		us_rid_r;
	reg                    		us_rvalid_r;
	reg  	[US_DATA_WIDTH-1:0] 	us_rdata_r;
	reg	[1:0]			us_rresp_r;
	reg				us_rlast_r;

	reg	[(DS_DATA_WIDTH/8)-1:0] ds_wstrb_r;
	reg      	             	ds_wvalid_r;
	reg  	[DS_DATA_WIDTH-1:0] 	ds_wdata_r;
	reg				ds_wlast_r;

	wire				update_us_rdata;
	wire				update_ds_wdata;
	wire 				ds_rdata_valid;
	wire				us_wdata_valid;
	wire	[1:0] 			ds_mux_rresp;

	assign us_rid = us_rid_r;
	assign us_rvalid = us_rvalid_r;
	assign us_rdata = us_rdata_r;
	assign us_rresp = us_rresp_r;
	assign us_rlast = us_rlast_r;

	assign ds_wstrb = ds_wstrb_r;
	assign ds_wvalid = ds_wvalid_r;
	assign ds_wdata = ds_wdata_r;
	assign ds_wlast = ds_wlast_r;

	assign rcmd_fifo_rd	= update_us_rdata && ds_mux_rvalid && ds_mux_rlast;
	assign wcmd_fifo_rd	= update_ds_wdata && us_mux_wvalid && us_mux_wlast;

	assign update_us_rdata 	= ((!us_rvalid_r) || us_rready) && (!rcmd_fifo_empty);
	assign update_ds_wdata 	= ((!ds_wvalid_r) || ds_wready) && (!wcmd_fifo_empty);
	assign ds_rready	= !rdata_buffer_valid;
	assign us_wready	= !wdata_buffer_valid;

	assign ds_rdata_valid	= ds_rvalid && ds_rready;
	assign ds_mux_rvalid	= rdata_buffer_valid ? 1'b1	 				: ds_rvalid;
	assign ds_mux_rdata	= rdata_buffer_valid ? rdata_buffer[RDATA_BUFFER_WIDTH-1:3] 	: ds_rdata;
	assign ds_mux_rresp	= rdata_buffer_valid ? rdata_buffer[2:1] 			: ds_rresp;
	assign ds_mux_rlast	= rdata_buffer_valid ? rdata_buffer[0] 				: ds_rlast;

	always @(*) begin
		if (ds_mux_rlast && ds_mux_rvalid && update_us_rdata) begin
			rdata_addr_sel_a1 = 1'b1;
		end
		else if (ds_mux_rvalid && update_us_rdata) begin
			rdata_addr_sel_a1 = 1'b0;
		end
		else begin
			rdata_addr_sel_a1 = rdata_addr_sel;
		end
	end

	always @(*) begin
		if (update_us_rdata && ds_mux_rvalid) begin
			next_rdata_addr_a1 = (rdata_addr_mux &  rdata_wrap_mask)
		                          | (rdata_incr_addr & ~rdata_wrap_mask);
		end
		else begin
			next_rdata_addr_a1 = next_rdata_addr;
		end
	end

	always @(*) begin
		if (ds_rdata_valid && (!update_us_rdata)) begin
			rdata_buffer_valid_a1 = 1'b1;
		end
		else if (update_us_rdata) begin
			rdata_buffer_valid_a1 = 1'b0;
		end
		else begin
			rdata_buffer_valid_a1 = rdata_buffer_valid;
		end
	end

	always @(*) begin
		if (ds_rdata_valid) begin
			rdata_buffer_a1	= {ds_rdata, ds_rresp, ds_rlast};
		end
		else begin
			rdata_buffer_a1 = rdata_buffer;
		end
	end

	always @(posedge aclk or negedge aresetn) begin
		if (!aresetn) begin
			rdata_buffer_valid 	<= 1'b0;
			rdata_buffer		<= {(RDATA_BUFFER_WIDTH){1'b0}};
		end
		else begin
			rdata_buffer_valid 	<= rdata_buffer_valid_a1;
			rdata_buffer		<= rdata_buffer_a1;
		end
	end

	assign us_wdata_valid	= us_wvalid && us_wready;
	assign us_mux_wvalid	= wdata_buffer_valid ? 1'b1						   	: us_wvalid;
	assign us_mux_wdata	= wdata_buffer_valid ? wdata_buffer[WDATA_BUFFER_WIDTH-1:(US_DATA_WIDTH/8)+1] 	: us_wdata;
	assign us_mux_wstrb	= wdata_buffer_valid ? wdata_buffer[(US_DATA_WIDTH/8):1] 			: us_wstrb;
	assign us_mux_wlast	= wdata_buffer_valid ? wdata_buffer[0] 					   	: us_wlast;

	always @(*) begin
		if (wcmd_fifo_empty) begin
			wdata_addr_sel_a1 = 1'b1;
		end
		else if (us_mux_wlast && us_mux_wvalid && update_ds_wdata) begin
			wdata_addr_sel_a1 = 1'b1;
		end
		else if (us_mux_wvalid && update_ds_wdata) begin
			wdata_addr_sel_a1 = 1'b0;
		end
		else begin
			wdata_addr_sel_a1 = wdata_addr_sel;
		end
	end

	always @(*) begin
		if (update_ds_wdata && us_mux_wvalid) begin
			next_wdata_addr_a1 = (wdata_addr_mux &  wdata_wrap_mask)
	                                  | (wdata_incr_addr & ~wdata_wrap_mask);
		end
		else begin
			next_wdata_addr_a1 = next_wdata_addr;
		end
	end

	always @(*) begin
		if (us_wdata_valid && (!update_ds_wdata)) begin
			wdata_buffer_valid_a1 = 1'b1;
		end
		else if (update_ds_wdata) begin
			wdata_buffer_valid_a1 = 1'b0;
		end
		else begin
			wdata_buffer_valid_a1 = wdata_buffer_valid;
		end
	end

	always @(*) begin
		if (us_wdata_valid) begin
			wdata_buffer_a1 = {us_wdata, us_wstrb, us_wlast};
		end
		else begin
			wdata_buffer_a1 = wdata_buffer;
		end
	end

	always @(posedge aclk or negedge aresetn) begin
		if (!aresetn) begin
			wdata_buffer_valid 	<= 1'b0;
			wdata_buffer		<= {(WDATA_BUFFER_WIDTH){1'b0}};
		end
		else begin
			wdata_buffer_valid 	<= wdata_buffer_valid_a1;
			wdata_buffer		<= wdata_buffer_a1;
		end
	end

	always @(posedge aclk or negedge aresetn) begin
		if (!aresetn) begin
			us_rvalid_r	<= 1'b0;
		end
		else begin
			if (update_us_rdata) begin
				us_rvalid_r	<= us_rvalid_a1;
			end
			else if (us_rready) begin
				us_rvalid_r	<= 1'b0;
			end
		end
	end
	always @(posedge aclk or negedge aresetn) begin
		if (!aresetn) begin
			us_rid_r		<= {(ID_WIDTH){1'b0}};
			us_rdata_r	<= {(US_DATA_WIDTH){1'b0}};
			us_rresp_r	<= 2'h0;
			us_rlast_r	<= 1'b0;
		end
		else begin
			if (update_us_rdata) begin
				us_rid_r		<= us_rid_a1;
				us_rdata_r	<= us_rdata_a1;
				us_rresp_r	<= ds_mux_rresp;
				us_rlast_r	<= ds_mux_rlast;
			end
		end
	end

	always @(posedge aclk or negedge aresetn) begin
		if (!aresetn) begin
			ds_wvalid_r	<= 1'b0;
		end
		else begin
			if (update_ds_wdata) begin
				ds_wvalid_r	<= ds_wvalid_a1;
			end
			else if (ds_wready) begin
				ds_wvalid_r	<= 1'b0;
			end
		end
	end
	always @(posedge aclk or negedge aresetn) begin
		if (!aresetn) begin
			ds_wstrb_r	<= {((DS_DATA_WIDTH/8)){1'b0}};
			ds_wdata_r	<= {(DS_DATA_WIDTH){1'b0}};
			ds_wlast_r	<= 1'b0;
		end
		else begin
			if (update_ds_wdata) begin
				ds_wstrb_r	<= ds_wstrb_a1;
				ds_wdata_r	<= ds_wdata_a1;
				ds_wlast_r	<= us_mux_wlast;
			end
		end
	end
end
else begin :data_nonbuffering_signal
	assign rcmd_fifo_rd	= (!rcmd_fifo_empty) && ds_rready && ds_mux_rvalid && ds_mux_rlast;
	assign wcmd_fifo_rd	= (!wcmd_fifo_empty )&& us_wready && us_mux_wvalid && us_mux_wlast;

	assign ds_rready	= us_rready && (!rcmd_fifo_empty);
	assign us_wready	= ds_wready && (!wcmd_fifo_empty);

	assign ds_mux_rvalid	= ds_rvalid;
	assign ds_mux_rdata	= ds_rdata;
	assign ds_mux_rlast	= ds_rlast;

	assign us_mux_wvalid	= us_wvalid;
	assign us_mux_wdata	= us_wdata;
	assign us_mux_wstrb	= us_wstrb;
	assign us_mux_wlast	= us_wlast;

	assign us_rid		= us_rid_a1;
	assign us_rvalid	= us_rvalid_a1;
	assign us_rdata		= us_rdata_a1;
	assign us_rresp		= ds_rresp;
	assign us_rlast		= ds_rlast;

	assign ds_wstrb		= ds_wstrb_a1;
	assign ds_wvalid	= ds_wvalid_a1;
	assign ds_wdata		= ds_wdata_a1;
	assign ds_wlast		= us_wlast;

	always @(*) begin
		if (ds_mux_rlast && ds_mux_rvalid && ds_rready) begin
			rdata_addr_sel_a1 = 1'b1;
		end
		else if (ds_mux_rvalid && ds_rready) begin
			rdata_addr_sel_a1 = 1'b0;
		end
		else begin
			rdata_addr_sel_a1 = rdata_addr_sel;
		end
	end

	always @(*) begin
		if (us_rready && ds_mux_rvalid) begin
			next_rdata_addr_a1 = (rdata_addr_mux &  rdata_wrap_mask)
		                          | (rdata_incr_addr & ~rdata_wrap_mask);
		end
		else begin
			next_rdata_addr_a1 = next_rdata_addr;
		end
	end

	always @(*) begin
		if (wcmd_fifo_empty) begin
			wdata_addr_sel_a1 = 1'b1;
		end
		else if (us_mux_wlast && us_mux_wvalid && us_wready) begin
			wdata_addr_sel_a1 = 1'b1;
		end
		else if (us_mux_wvalid && us_wready) begin
			wdata_addr_sel_a1 = 1'b0;
		end
		else begin
			wdata_addr_sel_a1 = wdata_addr_sel;
		end
	end

	always @(*) begin
		if (ds_wready && us_mux_wvalid) begin
			next_wdata_addr_a1 = (wdata_addr_mux &  wdata_wrap_mask)
	                                  | (wdata_incr_addr & ~wdata_wrap_mask);
		end
		else begin
			next_wdata_addr_a1 = next_wdata_addr;
		end
	end

end
endgenerate

sync_fifo_afe #(.DATA_WIDTH(RDATA_FIFO_WIDTH),.FIFO_DEPTH(2)) rcmd_fifo
(
.reset_n(aresetn),
.clk(aclk),
.wr(rcmd_fifo_wr),
.wr_data(rcmd_fifo_wdata),
.rd(rcmd_fifo_rd),
.rd_data(rcmd_fifo_rdata),
.almost_empty(),
.almost_full(),
.empty(rcmd_fifo_empty),
.full(rcmd_fifo_full)
);

sync_fifo_afe #(.DATA_WIDTH(WDATA_FIFO_WIDTH),.FIFO_DEPTH(2)) wcmd_fifo
(
.reset_n(aresetn),
.clk(aclk),
.wr(wcmd_fifo_wr),
.wr_data(wcmd_fifo_wdata),
.rd(wcmd_fifo_rd),
.rd_data(wcmd_fifo_rdata),
.almost_empty(),
.almost_full(),
.empty(wcmd_fifo_empty),
.full()
);

sync_fifo_afe #(.DATA_WIDTH(WRESP_FIFO_WIDTH),.FIFO_DEPTH(2)) wid_fifo
(
.reset_n(aresetn),
.clk(aclk),
.wr(wid_fifo_wr),
.wr_data(wid_fifo_wdata),
.rd(wid_fifo_rd),
.rd_data(wid_fifo_rdata),
.almost_empty(),
.almost_full(),
.empty(wid_fifo_empty),
.full(wid_fifo_full)
);






endmodule
