module AXISPF #
(
    parameter           W = 16,                 //
    parameter           L = 3 ,                 //
    parameter           A = 1                   //
)
(
    input               RST   ,                 //
    input               CLK   ,                 //
    input               WEN   ,                 //
    input               REN   ,                 //
    input      [W-1:0]  DTI   ,                 //
    output     [W-1:0]  DTO   ,                 //
    output reg          AMF   ,                 //
    output              FUL   ,                 //
    output reg          EMP                     //
);

//1.0 sigs
    reg        [W-1:0]  MEM[2**L-1:0];
    reg        [ 31:0]  WCNT  ;
    reg        [ 31:0]  RCNT  ;
    reg        [  L:0]  DCNT  ;
    reg                 ERR   ;

//2.0 comp

//3.0 comb
assign FUL    =   DCNT[L]        ;

assign WENENB = WEN;    // & ~FUL;
assign RENENB = REN;    // & ~EMP;

assign DTO    = MEM[RCNT[L-1:0]] ;

//4.0 regs
always@(posedge CLK or posedge RST)
begin
    if ( RST )
        WCNT <= 'b0;
    else if ( WENENB )
        WCNT <= WCNT + 1;
end

always@(posedge CLK or posedge RST)
begin
    if ( RST )
        RCNT <= 'b0;
    else if ( RENENB )
        RCNT <= RCNT + 1;
end

always@(posedge CLK or posedge RST)
begin
         if (  RST              ) DCNT <= 'b0     ;
    else if (  WENENB & ~RENENB ) DCNT <= DCNT + 1;
    else if ( ~WENENB &  RENENB ) DCNT <= DCNT - 1;
end

always@(posedge CLK or posedge RST)
begin
    if (  RST )
        EMP  <= 1'b1    ;
    else if  (  WENENB & ~RENENB )
        EMP  <= 1'b0    ;
    else if (( ~WENENB &  RENENB ) && ( DCNT == 1 ))
        EMP  <= 1'b1    ;
end

always@(posedge CLK or posedge RST)
begin
    if (  RST )
        AMF  <= 1'b0    ;
    else if  ( DCNT >= (2**L-A) )
        AMF  <= 1'b1    ;
    else
        AMF  <= 1'b0    ;
end

always@(posedge CLK or posedge RST)
begin
    if ( RST )
        ERR <= 1'b0;
    else if ( DCNT > 2**L )
        ERR <= 1'b1;
end

always@(posedge CLK)
begin
    if ( WENENB )
        MEM[WCNT[L-1:0]] <= DTI;
end

//5.0 sims
//5.1 MEMA init
// synthesis translate_off
integer i;

initial
begin
    for(i = 0; i < 2**L; i= i+1)
    begin
        MEM[i] <= 'b0;
    end
end

always@(posedge ERR)
begin
    $display("%m %t ERROR: AXISPF ERRLCK is HIGH!", $time);
    $stop;
end


// synthesis translate_on

endmodule


