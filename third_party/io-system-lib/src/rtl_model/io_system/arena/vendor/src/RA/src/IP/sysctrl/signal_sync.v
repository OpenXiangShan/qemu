
module signal_sync (
input wire i_clk,
input wire i_rst_n,
input wire i_signal,
output wire o_signal
);

reg [1:0] r_signal_syn;

always @(posedge i_clk or negedge i_rst_n)
if(!i_rst_n)
    r_signal_syn <= 2'b0;
else
    r_signal_syn <= {r_signal_syn[0],i_signal};


assign o_signal= r_signal_syn[1];

endmodule
