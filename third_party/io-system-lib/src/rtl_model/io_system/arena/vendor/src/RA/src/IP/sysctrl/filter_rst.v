module filter_rst (
input  wire clk,
input  wire i_rst_n,
input  wire i_din,
input  wire i_cfg,
output reg o_dout
);

reg r_din_syn,r_din_syn_syn,r_din_syn2;
reg [5: 0] r_din_mux;
logic w_rst_n;
reset_syn a_prst_syn( .o_rst_n(w_rst_n) ,.i_clk (clk), .i_rst_n(i_rst_n) );

always @(posedge clk or negedge w_rst_n)
if(!w_rst_n) begin r_din_syn <= 1'b0; r_din_syn_syn <= 1'b0; end
else begin r_din_syn <= i_din; r_din_syn_syn <= r_din_syn; end

always @(posedge clk or negedge w_rst_n)
if(!w_rst_n) r_din_mux <= 6'b0;
else if(r_din_syn_syn^o_dout) r_din_mux <= r_din_mux + 6'b1;
else r_din_mux <= 6'b0;

always @(posedge clk or negedge w_rst_n)
if(!w_rst_n) o_dout <= 1'b0;
else if(r_din_mux==(i_cfg ? 6'h3F : 6'h1F)||(&r_din_mux)) o_dout <= r_din_syn;
endmodule