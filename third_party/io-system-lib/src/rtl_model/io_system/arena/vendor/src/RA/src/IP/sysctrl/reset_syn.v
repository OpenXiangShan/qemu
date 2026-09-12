module reset_syn(o_rst_n, i_clk, i_rst_n);
output o_rst_n;
input i_clk, i_rst_n;
reg o_rst_n, r_ff1;

always@(posedge i_clk or negedge i_rst_n)
    if(!i_rst_n) begin
        r_ff1 <= 1'b0;
        o_rst_n <= 1'b0;
    end
    else begin
        r_ff1 <= 1'b1;
        o_rst_n <= r_ff1;
    end
endmodule

module reset_negsyn(o_rst_n, i_clk, i_rst_n);
output o_rst_n;
input i_clk, i_rst_n;
reg o_rst_n, r_ff1;

always@(posedge i_clk or negedge i_rst_n)
    if(!i_rst_n) begin
        r_ff1 <= 1'b0;
    end
    else begin
        r_ff1 <= 1'b1;
    end

always@(negedge i_clk or negedge i_rst_n)
    if(!i_rst_n) begin
        o_rst_n <= 1'b0;
    end
    else begin
        o_rst_n <= r_ff1;
    end
endmodule