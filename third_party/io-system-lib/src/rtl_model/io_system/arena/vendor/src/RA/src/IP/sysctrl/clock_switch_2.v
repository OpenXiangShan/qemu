module clock_switch_2(i_clk0, i_clk1, i_sel, i_rst_n, o_clkout);
input i_clk0, i_clk1, i_sel, i_rst_n;
output o_clkout;

reg r_clk0_sel0, r_clk0_sel1, r_clk1_sel0, r_clk1_sel1;
wire w_clk0, w_clk1;
wire w_clk0_rst_n, w_clk1_rst_n;

reset_syn a_clk0_reset_syn(.o_rst_n(w_clk0_rst_n), .i_clk(i_clk0), .i_rst_n(i_rst_n));
reset_syn a_clk1_reset_syn(.o_rst_n(w_clk1_rst_n), .i_clk(i_clk1), .i_rst_n(i_rst_n));

always@(posedge i_clk0 or negedge w_clk0_rst_n)
if(!w_clk0_rst_n)
    r_clk0_sel0 <= 1'b0;
else
    r_clk0_sel0 <= ~i_sel&~r_clk1_sel1;

always@(negedge i_clk0 or negedge w_clk0_rst_n)
if(!w_clk0_rst_n)
    r_clk0_sel1 <= 1'b0;
else
    r_clk0_sel1 <= r_clk0_sel0;

always@(posedge i_clk1 or negedge w_clk1_rst_n)
if(!w_clk1_rst_n)
    r_clk1_sel0 <= 1'b0;
else
    r_clk1_sel0 <= i_sel&~r_clk0_sel1;

always@(negedge i_clk1 or negedge w_clk1_rst_n)
if(!w_clk1_rst_n)
    r_clk1_sel1 <= 1'b0;
else
    r_clk1_sel1 <= r_clk1_sel0;

ck_nand clk0nand  (.i_a(i_clk0), .i_b(r_clk0_sel1), .out(w_clk0));
ck_nand clk1nand  (.i_a(i_clk1), .i_b(r_clk1_sel1), .out(w_clk1));
ck_nand clkoutnand (.i_a(w_clk0), .i_b(w_clk1), .out(o_clkout));
endmodule
