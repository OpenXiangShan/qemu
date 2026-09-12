module ahbcom
(
    input               RST_N             ,                       //CLKå??æ­¥å??å??ç??å¤?ä½?ä¿¡å?·,é«?æ??æ??!
    input               CLK             ,                       //AHBæ?»çº¿æ?¶é'?

    input               CMDDVI          ,                       //
    output reg          CMDFUL          ,                       //
    input      [ 71:0]  CMDDTI          ,                       //
    output              RB_DVI          ,                       //
    output     [ 31:0]  RB_DTR          ,                       //

    output              AHBSEL          ,                       //MST AHBæ?¥å?£hsel
    output     [ 31:0]  AHBDTW          ,                       //MST AHBæ?¥å?£hwdata
    output     [ 31:0]  AHBADR          ,                       //MST AHBæ?¥å?£haddr
    output     [  3:0]  AHBPOT          ,                       //MST AHBæ?¥å?£hprot
    output     [  2:0]  AHBSIZ          ,                       //MST AHBæ?¥å?£hsize
    output     [  2:0]  AHBINC          ,                       //MST AHBæ?¥å?£hburst
    output     [  1:0]  AHBSEQ          ,                       //MST AHBæ?¥å?£htrans
    output              AHBWEN          ,                       //MST AHBæ?¥å?£hwrite
    output              AHBLCK          ,                       //MST AHBæ?¥å?£hmastlock
    output              AHBRDY          ,                       //MST AHBæ?¥å?£hready
    input               AHBRDO          ,                       //MST AHBæ?¥å?£hreadyout
    input               AHBERR          ,                       //MST AHBæ?¥å?£hresp
    input      [ 31:0]  AHBDTR                                  //MST AHBæ?¥å?£hrdata
);

//1.0 sigs
    reg        [ 71:0]  DTILCK;
    reg        [  2:0]  NEWSIZ;
    reg        [  1:0]  LOWADR;
    reg                 HLDFLG;
    reg                 AHBBSY;
    wire       [ 31:0]  CMDDTW;
    wire       [ 31:0]  CMDADR;
    wire       [  2:0]  CM_SIZ;
    wire       [  3:0]  CMDSTB;
    wire                WENMOD;

//2.0 comp

//3.0 comb
assign {WENMOD,CM_SIZ,CMDSTB,CMDDTW,CMDADR } = DTILCK   ;       //
assign CMDVLD = CMDFUL & ~HLDFLG                        ;       //
assign RB_DVI = AHBBSY & AHBRDY & CMDFUL                ;       //
assign RB_DTR = AHBDTR                                  ;       //
assign AHBSEL = 1'b1                                    ;       //
assign AHBDTW = CMDDTW                                  ;       //
assign AHBADR ={CMDADR[31:2],LOWADR}                    ;       //
assign AHBPOT = 4'b0                                    ;       //
assign AHBSIZ = NEWSIZ                                  ;       //
assign AHBINC = 3'b0                                    ;       //3'b0=single;
assign AHBSEQ ={CMDVLD,1'b0}                            ;       //
assign AHBWEN = WENMOD                                  ;       //
assign AHBLCK = 1'b0                                    ;       //
assign AHBRDY =(CMDVLD | AHBBSY ) & AHBRDO              ;       //

always@( * )
begin
    case ( {CMDADR[1:0],CM_SIZ[1:0],CMDSTB} )
        8'h21 : begin LOWADR = 2'h0       ; NEWSIZ = 3'h0  ; end
        8'h22 : begin LOWADR = 2'h1       ; NEWSIZ = 3'h0  ; end
        8'h24 : begin LOWADR = 2'h2       ; NEWSIZ = 3'h0  ; end
        8'h28 : begin LOWADR = 2'h3       ; NEWSIZ = 3'h0  ; end
        8'h23 : begin LOWADR = 2'h0       ; NEWSIZ = 3'h1  ; end
        8'h2c : begin LOWADR = 2'h2       ; NEWSIZ = 3'h1  ; end
      default : begin LOWADR = CMDADR[1:0]; NEWSIZ = CM_SIZ; end
    endcase
end

//4.0 regs
always@(posedge CLK or negedge RST_N)
begin
    if ( !RST_N )
        DTILCK <=  'b0;
    else if ( CMDDVI )
        DTILCK <= CMDDTI;
end

always@(posedge CLK or negedge RST_N)
begin
    if ( !RST_N )
        HLDFLG <= 1'b0;
    else if ( CMDVLD & AHBRDY)
        HLDFLG <= 1'b1;
    else if ( RB_DVI )
        HLDFLG <= 1'b0;
end

always@(posedge CLK or negedge RST_N)
begin
    if ( !RST_N )
        CMDFUL <= 1'b0;
    else if ( CMDDVI )
        CMDFUL <= 1'b1;
    else if ( RB_DVI )
        CMDFUL <= 1'b0;
end

always@(posedge CLK or negedge RST_N)
begin
    if ( !RST_N )
        AHBBSY <= 1'b0 ;
    else if ( CMDVLD )
        AHBBSY <= 1'b1 ;
    else if ( AHBRDY )
        AHBBSY <= 1'b0 ;
end

//5.0 sims


endmodule



