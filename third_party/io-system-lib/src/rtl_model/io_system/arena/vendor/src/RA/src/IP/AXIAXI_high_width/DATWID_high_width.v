module DATWID_high_width #
(
    parameter   [0:0]   WCH =    0                          ,   //写通道设定:0=读通道用;1=写通道用.
    parameter   [1:0]   MOD =    0                          ,   //模式设定:0=32-64  1=64-32  2=64-128  3=128-64

    parameter   [1:0]   MED = (WCH) ? MOD:{MOD[1],~MOD[0]}   ,   //自动计算产生,不要参数传递.
    parameter           DTW = (MED==3)?512:(MED==0)?128:256  ,   //自动计算产生,不要参数传递.主侧位宽-1值.
    parameter           STW = (MED==3)? 64:(MED==0)? 16: 32  ,   //自动计算产生,不要参数传递.主侧使能-1值.
    parameter           DOW = (MED==2)?512:(MED==1)?128:256  ,   //自动计算产生,不要参数传递.从侧位宽-1值.
    parameter           SOW = (MED==2)? 64:(MED==1)? 16: 32      //自动计算产生,不要参数传递.从侧使能-1值.
)
(
    //1.0 复位时钟系统
    input               RST             ,                       //复位信号
    input               CLK             ,                       //AXI时钟输入
    input               HLD             ,                       //OSD缓存空不接收数据.没收到命令,不知道数据该咋处理,故不接收.
    //2.0 主侧slave AXI接主设备
    input   [DTW-1:0]     DTI             ,                       //SLV AXI接口wdata
    input   [STW-1:0]     STI             ,                       //SLV AXI接口wstrb,读通道中无用但必须接全高.
    input               LSI             ,                       //SLV AXI接口wlast
    input               HVI             ,                       //SLV AXI接口wvalid
    output              RDO             ,                       //SLV AXI接口wready
    //3.0 从侧master AXI接从设备
    output  [DOW-1:0]     DTO             ,                       //MST AXI接口wdata
    output  [SOW-1:0]     STO             ,                       //MST AXI接口wstrb
    output              LSO             ,                       //MST AXI接口wlast
    output              DVO             ,                       //MST AXI接口wvalid
    input               RDI             ,                       //MST AXI接口wready
    //4.0 cmd_fifo
    input   [  5:0]     CMD                                     //WIDCON输出控制DATWID模块信号总线
);

//1.0 sigs
    parameter   [0:0]   MOD1T2 =  WCH ^  MOD[0];                //写通道设定:0=读通道用;1=写通道用.
    parameter   [0:0]   MODW02 =  WCH & ~MOD[0];                //NOM模式MOD_0_2_W_1T2:`include "W02COM.vh"
    parameter   [0:0]   MODR13 = ~WCH &  MOD[0];                //NOM模式MOD_1_3_R_1T2:`include "R13COM.vh"
    parameter   [0:0]   MODR02 = ~WCH & ~MOD[0];                //NOM模式MOD_0_2_R_2T1:`include "R02COM.vh"
    parameter   [0:0]   MODW13 =  WCH &  MOD[0];                //NOM模式MOD_1_3_W_2T1:`include "W13COM.vh"
    reg     [DTW-1:0]     DTIB01 =0       ;                       //
    reg     [STW-1:0]     STIB01 =0       ;                       //
    reg                 INS    =0       ;                       //
    wire    [  1:0]     CUT             ;                       //
    wire                HED             ;                       //头部字节标识.
    wire                END             ;                       //尾部字节标识.
    wire                NRM             ;                       //'1'为标准AXI窄传输模式;'0'为满位宽整合方式.
    wire                SET             ;                       //'1'选择高位,'0'选择低位.由对应地址位控制.
    wire                DVI             ;                       //
    wire    [DOW-1:0]     NRMDTO          ;                       //
    wire    [SOW-1:0]     NRMSTO          ;                       //
    wire    [DOW-1:0]     NOMDTO          ;                       //
    wire    [SOW-1:0]     NOMSTO          ;                       //
    wire                NRMLSO          ;
    wire                NRMDVO          ;
    wire                NRMRDO          ;
    wire                NOMLSO          ;
    wire                NOMDVO          ;
    wire                NOMRDO          ;

//2.0 comp

//3.0 comb
//3.0.1公共部分
assign {CUT,HED,END,NRM,SET} = CMD                                        ;

assign DTO        = ( NRM ) ? NRMDTO : NOMDTO                             ;
assign STO        = ( NRM ) ? NRMSTO : NOMSTO                             ;
assign LSO        = ( NRM ) ? NRMLSO : NOMLSO                             ;
assign DVO        = ( NRM ) ? NRMDVO : NOMDVO                             ;
assign RDH        = ( NRM ) ? NRMRDO : NOMRDO                             ;

assign DVI        =   HVI   & ~HLD                                        ;
assign RDO        =   RDH   & ~HLD                                        ;

//3.0.1 NRM模式
generate if ( MOD1T2 )    //1T2
begin
    assign NRMDTO = { DTI,DTI }                                           ;
    assign NRMSTO = ( SET ) ? {STI,{(STW-1+1){1'b0}}}: {{(STW-1+1){1'b0}},STI};
    assign NRMLSO =   LSI                                                 ;
    assign NRMDVO =   DVI                                                 ;
    assign NRMRDO =   RDI                                                 ;
end
else                            //2T1
begin
    assign NRMDTO = ( SET ) ? DTI[DTW-1:(DOW)] : DTI[DOW-1:00]              ;
    assign NRMSTO = ( SET ) ? STI[STW-1:(SOW)] : STI[SOW-1:00]              ;
    assign NRMLSO =   LSI                                                 ;
    assign NRMDVO =   DVI                                                 ;
    assign NRMRDO =   RDI                                                 ;
end
endgenerate

generate if (  MODW02 | MODR13 ) begin

    assign NOMDTO = ( SET ) ? { DTI,DTIB01 } : {            DTI,DTI }     ;
    assign NOMSTO = ( SET ) ? { STI,STIB01 } : {{(DTW-1+1){1'b0}},STI }     ;
    assign NOMLSO =   LSI                                                 ;
    assign NOMDVO =   DVI   & ( SET | LSI  )                              ;
    assign NOMRDO =   RDI                                                 ;

end else if (  MODR02 ) begin

    assign NOMDTO = ( INS ) ? DTI[DTW-1:(DOW)] : DTI[DOW-1:00]              ;         //
    assign NOMSTO = ( INS ) ? STI[STW-1:(SOW)] : STI[SOW-1:00]              ;         //
    assign NOMLSO =   LSI   &  (     INS | (END & CUT[0]         ))       ;         //R2T1去掉头尾多余的无效数据.头尾是否多余可计算得到
    assign NOMDVO =   DVI   & ~(HED&~INS&CUT[1]) & ~(END& INS&CUT[0])     ;         //R2T1去掉头尾多余的无效数据.头尾是否多余可计算得到
    assign NOMRDO =   RDI   &        INS                                  ;         //??

end else if (  MODW13 ) begin

    assign NOMDTO = ( INS ) ? DTI[DTW-1:(DOW)] : DTI[DOW-1:00]              ;         //
    assign NOMSTO = ( INS ) ? STI[STW-1:(SOW)] : STI[SOW-1:00]              ;         //
    assign NOMLSO =   LSI   &  (     INS        )                         ;         //
    assign NOMDVO =   DVI   & ~(HED&~INS& SET)                            ;         //W2T1去掉头部多余的无效数据,尾部是否多余无法先计算得到故保留.
    assign NOMRDO =   RDI   &        INS                                  ;         //

end endgenerate

//4.0 regs
generate if (  MODW02 | MODR13 ) begin
    always@(posedge CLK or posedge RST)
    begin
        if ( RST )
        begin
            DTIB01 <= 'b0;
            STIB01 <= 'b0;
        end
        else if ( DVI & RDO & LSI )
        begin
            DTIB01 <= 'b0;
            STIB01 <= 'b0;
        end
        else if ( DVI & RDO )
        begin
            DTIB01 <= DTI;
            STIB01 <= STI;
        end
    end
end else if (  MODR02 | MODW13 ) begin
    always@(posedge CLK or posedge RST)
    begin
        if ( RST )
            INS <= 1'b0;
        else if ( DVI & RDO )
            INS <= 1'b0;
        else if ( DVI & RDI )
            INS <= 1'b1;
    end
end endgenerate

//5.0 sims

endmodule


