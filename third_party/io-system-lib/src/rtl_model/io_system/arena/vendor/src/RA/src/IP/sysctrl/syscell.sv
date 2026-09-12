module clock_mux2(
input wire D0,
input wire D1,
input wire S,
output wire X
);

/*
CKMUX2M2T u_mux(
.A(D0),
.B(D1),
.S(S),
.Z(X)
);*/

assign X = S ? D1 : D0;

endmodule



module ck_gate (
input wire i_clock,
input wire i_clock_en,
output wire o_clock
);

//LAGCESM16T u_gate    ( .CK(i_clock), .E(i_clock_en  ), .SE(1'b0), .GCK(o_clock  ) );

assign o_clock = i_clock;
endmodule


module ck_and (
input wire i_a,
input wire i_b,
output wire out
);

//CKAN2M2T u_and  (.A(i_a), .B(i_b), .Z(out));

assign out = i_a & i_b;
endmodule


module ck_nand (
input wire i_a,
input wire i_b,
output wire out
);

//CKND2M2T u_nand  (.A(i_a), .B(i_b), .Z(out));

assign out = ~(i_a & i_b);
endmodule


module ck_inv (
input wire in,
output wire out
);

//CKINVM16T u_inv  (.A(in), .Z(out));

assign out = ~in;
endmodule



module gck (
input wire clk_in,
input wire clk_en,
input wire test_en,
output wire clk_out
);

//LAGCESM16T u_gate    ( .CK(clk_in), .E(clk_en  ), .SE(test_en), .GCK(clk_out  ) );

assign clk_out = clk_in;
endmodule




