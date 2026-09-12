module WIDCON_64T512
(
    input               RST             ,                       //复位信号
    input               CLK             ,                       //AXI时钟输入
    input   [ 12:0]     CMDDTI          ,                       //控制信息FIFO接口
    input               CMDEMP          ,                       //控制信息FIFO接口
    output              CMDREN          ,                       //控制信息FIFO接口
    input               SLVWEN          ,                       //输入侧数据写有效
    input               SLVRDY          ,                       //输入侧数据写有效
    input               SLVLST          ,                       //输入侧LSTBIT标识
    output  [  7:0]     CMDDTO                                  //输出控制DATWID模块信号总线
);

//1.0 sigs
    reg                 WRKING          ;
    reg     [  7:0]     ACNT            ;
    wire    [  1:0]     CUTBIT          ;
    wire    [  6:0]     SLVADR          ;
    wire    [  6:0]     SIZNUM          ;
    wire    [  2:0]     CMDSIZ          ;
    wire    [  6:0]     ADROUT          ;
    wire    [  6:0]     FULNUM          ;
    wire                HEDFLG          ;
    wire                ENDFLG          ;
    wire                CMDNRM          ;
    wire    [2:0]       CMDSET          ;

//2.0 comp

//3.0 comb
assign {CUTBIT,CMDNRM,CMDSIZ,SLVADR} = CMDDTI           ;
assign CMDDTO =  {CUTBIT,HEDFLG,ENDFLG,CMDNRM,CMDSET}   ;
assign CMDREN =  SLVWEN & SLVRDY & SLVLST               ;
assign ADROUT =  SLVADR + ACNT                          ;
assign HEDFLG =  ~(|ACNT) & SLVWEN & ~WRKING            ;
assign ENDFLG =  SLVLST                                 ;
assign FULSIZ =  ~CMDNRM                                ;

assign FULNUM =                    7'h08                ;     //以本转换上最小总线位宽为判断基准.
assign CMDSET =                    ADROUT[5:3]          ;     //以本转换上最小总线位宽为判断基准.
assign SIZNUM = ( FULSIZ      ) ?  FULNUM               :     //以本转换上最小总线位宽为判断基准.
                ( CMDSIZ == 0 ) ?  7'h01                :
                ( CMDSIZ == 1 ) ?  7'h02                :
                ( CMDSIZ == 2 ) ?  7'h04                :
                ( CMDSIZ == 3 ) ?  7'h08                :
                ( CMDSIZ == 4 ) ?  7'h10                :
                ( CMDSIZ == 5 ) ?  7'h20                :
                ( CMDSIZ == 6 ) ?  7'h40                :
                                   7'h40                ;
//4.0 regs
always@(posedge CLK or posedge RST)
begin
    if ( RST )
        ACNT <= 8'b0  ;
    else if ( CMDREN )
        ACNT <= 8'b0  ;
    else if ( SLVWEN & SLVRDY )
        ACNT <= ACNT + SIZNUM;
end

always@(posedge CLK or posedge RST)
begin
    if ( RST )
        WRKING <= 1'b0;
    else if ( ENDFLG )
        WRKING <= 1'b0;
    else if ( HEDFLG & SLVRDY )
        WRKING <= 1'b1;
end

//5.0 sims


endmodule


