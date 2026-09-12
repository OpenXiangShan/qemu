module OSLLMT #
(
    parameter           OSL = 0                 //
)
(
    input               RST   ,                 //
    input               CLK   ,                 //
    input               WEN   ,                 //有效的命令下发标识
    input               REN   ,                 //有效的响应回馈标识
    input               LST   ,                 //读有效中的LST标识
    output              FUL                     //hold下一个命令信号
);

//1.0 sigs
    reg        [OSL:0]  DCNT  ;                 //

//2.0 comp

//3.0 comb
assign FUL    =   DCNT[OSL]   ;
assign WENENB = WEN           ;
assign RENENB = REN & LST     ;

//4.0 regs
always@(posedge CLK or posedge RST)
begin
         if ( RST              )  DCNT <= 'b0     ;
    else if ( WENENB & ~RENENB )  DCNT <= DCNT + 1;
    else if (~WENENB &  RENENB )  DCNT <= DCNT - 1;
end

//5.0 sims


endmodule
