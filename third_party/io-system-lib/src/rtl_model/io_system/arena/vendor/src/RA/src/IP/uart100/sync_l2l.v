// Copyright (C) 2024, Andes Technology Corp. Confidential Proprietary
// AX45MPV_PREMIUM v30.1.0


module sync_l2l (
b_reset_n,
b_clk,
a_signal,
b_signal,
b_signal_rising_edge_pulse,
b_signal_falling_edge_pulse,
b_signal_edge_pulse
);

parameter SYNC_STAGE = 2;
parameter RESET_VALUE = 1'b0;

input  b_reset_n;
input  b_clk;
input  a_signal;
output b_signal;
output b_signal_rising_edge_pulse;
output b_signal_falling_edge_pulse;
output b_signal_edge_pulse;





// 0348a427 rnd


reg [SYNC_STAGE:0]    a_signal_sync;

wire   b_signal;
wire   b_signal_rising_edge_pulse;
wire   b_signal_falling_edge_pulse;
wire   b_signal_edge_pulse;
wire   b_signal_d1;

always @(negedge b_reset_n or posedge b_clk)
begin
	if (!b_reset_n)
                a_signal_sync <= {(SYNC_STAGE+1){RESET_VALUE}};
        else
                a_signal_sync <= {a_signal_sync[SYNC_STAGE-1:0], a_signal};
end

assign b_signal         = a_signal_sync[(SYNC_STAGE-1)];
assign b_signal_d1      = a_signal_sync[(SYNC_STAGE  )];

assign b_signal_rising_edge_pulse = b_signal && !b_signal_d1;
assign b_signal_falling_edge_pulse = !(b_signal || !b_signal_d1);
assign b_signal_edge_pulse = b_signal ^ b_signal_d1;






endmodule
