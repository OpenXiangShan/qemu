module WIDCON_high_width #
(
    parameter   [0:0]   WCH =    0                          ,   //写通道设定:0=读通道用;1=写通道用.
    parameter   [1:0]   MOD =    0                          ,   //模式设定:0=32-64  1=64-32  2=64-128  3=128-64
    parameter           STW = (MOD==1)?3:(MOD==2)?15: 7     ,   //自动计算产生,不要参数传递.主侧使能-1值.
    parameter           HFW = (MOD==1)?2:(MOD==2)? 8: 4         //自动计算产生,不要参数传递.主侧使能-1值.
)
(
    input               RST             ,                       //复位信号
    input               CLK             ,                       //AXI时钟输入
    input   [ 12:0]     CMDDTI          ,                       //控制信息FIFO接口
    input               CMDEMP          ,                       //控制信息FIFO接口
    output              CMDREN          ,                       //控制信息FIFO接口
    input               SLVWEN          ,                       //输入侧数据写有效
    input               SLVRDY          ,                       //输入侧数据写有效
    input               SLVLST          ,                       //输入侧LSTBIT标识
    output  [  5:0]     CMDDTO                                  //输出控制DATWID模块信号总线
);

//1.0 sigs
    reg                 WRKING          ;
    reg     [  6:0]     ACNT            ;
    wire    [  1:0]     CUTBIT          ;
    wire    [  6:0]     SLVADR          ;
    wire    [  6:0]     SIZNUM          ;
    wire    [  2:0]     CMDSIZ          ;
    wire    [  6:0]     ADROUT          ;
    wire    [  4:0]     FULNUM          ;
    wire                HEDFLG          ;
    wire                ENDFLG          ;
    wire                CMDNRM          ;
    wire                CMDSET          ;

//2.0 comp

//3.0 comb
assign {CUTBIT,CMDNRM,CMDSIZ,SLVADR} = CMDDTI           ;
assign CMDDTO =  {CUTBIT,HEDFLG,ENDFLG,CMDNRM,CMDSET}   ;
assign CMDREN =  SLVWEN & SLVRDY & SLVLST               ;
assign ADROUT =  SLVADR + ACNT                          ;
assign HEDFLG =  ~(|ACNT) & SLVWEN & ~WRKING            ;
assign ENDFLG =  SLVLST                                 ;
assign FULSIZ =  ~CMDNRM                                ;

assign FULNUM = ( MOD[1]      ) ?  5'h08                :     //以本转换上最小总线位宽为判断基准.
                                   5'h04                ;     //以本转换上最小总线位宽为判断基准.
assign CMDSET = ( MOD[1]      ) ?  ADROUT[5]            :     //以本转换上最小总线位宽为判断基准.
                                   ADROUT[4]            ;     //以本转换上最小总线位宽为判断基准.
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
        ACNT <= 7'b0  ;
    else if ( CMDREN )
        ACNT <= 7'b0  ;
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


