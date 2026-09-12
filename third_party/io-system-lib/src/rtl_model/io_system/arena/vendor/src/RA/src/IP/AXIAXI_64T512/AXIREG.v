module AXIREG #
(
    parameter           W = 16                  //
)
(
    input               RST   ,                 //
    input               CLK   ,                 //
    input               DVI   ,                 //
    input               RDI   ,                 //
    input      [W-1:0]  DTI   ,                 //
    output reg [W-1:0]  DTO   ,                 //
    output reg          DVO   ,                 //
    output reg          RDO                     //
);

//1.0 sigs

//2.0 comp

//3.0 comb
assign WEN = DVI & RDO;
assign REN = DVO & RDI;

//4.0 regs
always@(posedge CLK or posedge RST)
begin
         if (  RST        ) begin DVO <= 1'b0; RDO <= 1'b1; end
    else if ( ~WEN &  REN ) begin DVO <= 1'b0; RDO <= 1'b1; end
    else if (  WEN & ~REN ) begin DVO <= 1'b1; RDO <= 1'b0; end
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


