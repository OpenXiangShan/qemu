


module async_buff (
	w_reset_n,
	r_reset_n,
	w_clk,
	r_clk,
	wr,
	wr_data,
	rd,
	rd_data,
	empty,
	full
);

parameter DATA_WIDTH = 32;
parameter SYNC_STAGE = 2;

input				  w_reset_n;
input  				  r_reset_n;
input  				  w_clk;
input  				  r_clk;
input				  wr;
input	[DATA_WIDTH-1:0]	  wr_data;
input				  rd;
output	[DATA_WIDTH-1:0]	  rd_data;
output 				  empty;
output 				  full;





// 99dc4acd rnd


reg	[DATA_WIDTH-1:0]	  mem[0:1];
reg				  wr_ptr;
reg				  rd_ptr;
wire				  wr_ptr_rclk;
wire				  rd_ptr_wclk;


wire	[DATA_WIDTH-1:0]	  rd_data;

always @(posedge w_clk or negedge w_reset_n) begin
	if (!w_reset_n) begin
		mem[0] <= {DATA_WIDTH{1'b0}};
		mem[1] <= {DATA_WIDTH{1'b0}};
        end
	else if (wr) begin
		mem[wr_ptr] <= wr_data;
        end
end

always @(posedge w_clk or negedge w_reset_n) begin
	if (!w_reset_n)
		wr_ptr <= 1'b0;
	else if (wr)
		wr_ptr <= ~wr_ptr;
end

wire	unused_rd_r_e_p;
wire	unused_rd_f_e_p;
wire	unused_rd_e_p;

sync_l2l #(.SYNC_STAGE (SYNC_STAGE)) rd_ptr_sync (
	.b_reset_n(w_reset_n),
	.b_clk(w_clk),
	.a_signal(rd_ptr),
	.b_signal(rd_ptr_wclk),
	.b_signal_rising_edge_pulse(unused_rd_r_e_p),
	.b_signal_falling_edge_pulse(unused_rd_f_e_p),
	.b_signal_edge_pulse(unused_rd_e_p)
);

assign full = (wr_ptr != rd_ptr_wclk);

assign rd_data  = empty ? /* synopsys infer_mux_override */ mem[~wr_ptr_rclk] : mem[rd_ptr];

always @(posedge r_clk or negedge r_reset_n) begin
	if (!r_reset_n)
		rd_ptr <= 1'b0;
	else if (rd)
		rd_ptr <= ~rd_ptr;
end

wire	unused_wr_r_e_p;
wire	unused_wr_f_e_p;
wire	unused_wr_e_p;

sync_l2l #(.SYNC_STAGE (SYNC_STAGE)) wr_ptr_sync (
	.b_reset_n(r_reset_n),
	.b_clk(r_clk),
	.a_signal(wr_ptr),
	.b_signal(wr_ptr_rclk),
	.b_signal_rising_edge_pulse(unused_wr_r_e_p),
	.b_signal_falling_edge_pulse(unused_wr_f_e_p),
	.b_signal_edge_pulse(unused_wr_e_p)
);

assign empty = (rd_ptr == wr_ptr_rclk);






endmodule
