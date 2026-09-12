module AXISRF #
(
    parameter           W = 16,                 //
    parameter           L = 0 ,                 //
    parameter           A = 0                   //
)
(
    input               RST   ,                 //
    input               CLK   ,                 //
    input               WEN   ,                 //
    input               REN   ,                 //
    input      [W-1:0]  DTI   ,                 //
    output reg [W-1:0]  DTO   ,                 //
    output              AMF   ,                 //
    output reg          FUL   ,                 //
    output reg          EMP                     //
);

//1.0 sigs

//2.0 comp

//3.0 comb
assign AMF = FUL;

//4.0 regs
always@(posedge CLK or posedge RST)
begin
         if (  RST        ) begin FUL <= 1'b0; EMP <= 1'b1; end
    else if ( ~WEN &  REN ) begin FUL <= 1'b0; EMP <= 1'b1; end
    else if (  WEN & ~REN ) begin FUL <= 1'b1; EMP <= 1'b0; end
end

always@(posedge CLK or posedge RST)
begin
    if ( RST )
        DTO <= 'b0;
    else if ( WEN )
        DTO <= DTI;
end

//5.0 sims

endmodule


