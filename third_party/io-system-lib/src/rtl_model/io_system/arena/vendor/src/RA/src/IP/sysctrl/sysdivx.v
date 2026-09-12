module sysdivx (
input wire i_clk,
input wire i_rst_n,
input wire [6:0] i_div,
output wire o_clk
);

wire w_pos_rst_n;
reg r_pos_clk;
reg [6:0] r_pos_cnt;
reg [6:0] r_postdiv_possyn,r_postdiv_possyn1;

// pos
reset_syn a_posreset_syn( .o_rst_n(w_pos_rst_n) ,.i_clk (i_clk) ,.i_rst_n(i_rst_n) );

always @(posedge i_clk or negedge w_pos_rst_n)
if(!w_pos_rst_n)
    {r_postdiv_possyn,r_postdiv_possyn1} <= {7'h2,7'h2};
else
    {r_postdiv_possyn,r_postdiv_possyn1} <= {r_postdiv_possyn1, (~|i_div[6:1] ? 7'h2 : i_div)};

always @(posedge i_clk or negedge w_pos_rst_n)
if(!w_pos_rst_n)
    r_pos_cnt <= 7'h1;
else if(r_pos_cnt==r_postdiv_possyn)
    r_pos_cnt <= 7'h1;
else
    r_pos_cnt <= r_pos_cnt + 1'b1;

always @(posedge i_clk or negedge w_pos_rst_n)
if(!w_pos_rst_n)
    r_pos_clk <= 1'b0;
else if(r_pos_cnt==r_postdiv_possyn)
    r_pos_clk <= 1'b0;
else if(r_pos_cnt=={1'b0,r_postdiv_possyn[6:1]})
    r_pos_clk <= 1'b1;
else
    r_pos_clk <= r_pos_clk;


// clk sel
reg r_bpen_syn,r_bpen_syn1;
always @(posedge i_clk or negedge w_pos_rst_n)
if(!w_pos_rst_n)
    {r_bpen_syn,r_bpen_syn1} <= {1'b0,1'b0};
else
    {r_bpen_syn,r_bpen_syn1} <= {r_bpen_syn1, (~|i_div[6:0])};

clock_switch_2 a_div_sel (.i_clk1(i_clk), .i_clk0(r_pos_clk), .i_sel(r_bpen_syn), .i_rst_n(i_rst_n), .o_clkout(o_clk));
endmodule
