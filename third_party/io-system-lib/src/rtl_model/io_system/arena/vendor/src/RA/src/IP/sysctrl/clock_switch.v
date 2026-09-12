module clock_switch(i_clk0, i_clk1, i_sel, i_rst_n, o_clkout);
parameter NUM_OF_SYN0 = 2;
parameter NUM_OF_SYN1 = 4;
input i_clk0, i_clk1, i_sel, i_rst_n;
output o_clkout;
reg r_clk0_sel0, r_clk1_sel0;
reg [NUM_OF_SYN0 - 1: 0] r_clk0_sel;
reg [NUM_OF_SYN1 - 1: 0] r_clk1_sel;
wire w_sel0, w_sel1, w_clk0, w_clk1;
wire w_clk0_rst_n, w_clk1_rst_n;

reset_syn a_clk0_reset_syn(.o_rst_n(w_clk0_rst_n), .i_clk(i_clk0), .i_rst_n(i_rst_n));
reset_syn a_clk1_reset_syn(.o_rst_n(w_clk1_rst_n), .i_clk(i_clk1), .i_rst_n(i_rst_n));

always@(posedge i_clk0 or negedge w_clk0_rst_n)
if(!w_clk0_rst_n)
    r_clk0_sel0 <= 1'b0;
else
    r_clk0_sel0 <= ~i_sel;

always@(negedge i_clk0 or negedge w_clk0_rst_n)
if(!w_clk0_rst_n) begin
    r_clk0_sel <= 'b0;
end else begin
    r_clk0_sel[0] <= r_clk0_sel0;
    r_clk0_sel[NUM_OF_SYN0-1: 1] <= r_clk0_sel[NUM_OF_SYN0-2: 0];
end

always@(posedge i_clk1 or negedge w_clk1_rst_n)
if(!w_clk1_rst_n)
    r_clk1_sel0 <= 1'b0;
else
    r_clk1_sel0 <= i_sel;

always@(negedge i_clk1 or negedge w_clk1_rst_n)
if(!w_clk1_rst_n)
    r_clk1_sel <= 'b0;
else begin
    r_clk1_sel[0] <= r_clk1_sel0;
    r_clk1_sel[NUM_OF_SYN1-1: 1] <= r_clk1_sel[NUM_OF_SYN1-2: 0];
end

ck_and sel0and  (.i_a(r_clk0_sel[0]), .i_b(r_clk0_sel[NUM_OF_SYN0-1]), .out(w_sel0));
ck_and sel1and  (.i_a(r_clk1_sel[0]), .i_b(r_clk1_sel[NUM_OF_SYN1-1]), .out(w_sel1));

ck_nand clk0nand (.i_a(i_clk0), .i_b(w_sel0), .out(w_clk0));
ck_nand clk1nand (.i_a(i_clk1), .i_b(w_sel1), .out(w_clk1));
ck_nand clkoutnand (.i_a(w_clk0), .i_b(w_clk1), .out(o_clkout));

endmodule
