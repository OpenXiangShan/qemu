module ADRWID_high_width #
(
    parameter   [0:0]   STD =    0                          ,   //是否完全按照AXI窄传输:0=多周期窄传输适配为满传输;1=按标准窄传输适配.
    parameter   [0:0]   WCH =    0                          ,   //写通道设定:0=读通道用;1=写通道用.
    parameter   [1:0]   MOD =    0                          ,   //模式设定:0=128-256  1=256-128  2=256-512  3=512-256
    parameter           IDW =    8                          ,   //ID 位宽设定: IDW+1为 ID的真正位宽.
    parameter           STW = (MOD==1)?4:(MOD==2)?16: 8         //自动计算产生,不要参数传递.主侧使能-1值.
)
(
    //1.0 复位时钟系统
    input               RST             ,                       //复位信号
    input               CLK             ,                       //AXI时钟输入
    //2.0 主侧slave AXI接主设备
    input   [IDW-1:0]     slv_id          ,                       //SLV AXI接口awid
    input   [ 31:0]     slv_addr        ,                       //SLV AXI接口awaddr
    input   [  3:0]     slv_region      ,                       //SLV AXI接口awregion
    input   [  7:0]     slv_len         ,                       //SLV AXI接口awlen
    input   [  2:0]     slv_size        ,                       //SLV AXI接口awsize
    input   [  1:0]     slv_burst       ,                       //SLV AXI接口awburst
    input               slv_lock        ,                       //SLV AXI接口awlock
    input   [  3:0]     slv_cache       ,                       //SLV AXI接口awcache
    input   [  2:0]     slv_prot        ,                       //SLV AXI接口awprot
    input   [  3:0]     slv_qos         ,                       //SLV AXI接口awqos
    input               slv_valid       ,                       //SLV AXI接口awvalid
    output              slv_ready       ,                       //SLV AXI接口awready
    //3.0 从侧master AXI接从设备
    output  [IDW-1:0]     mst_id          ,                       //MST AXI接口awid
    output  [ 31:0]     mst_addr        ,                       //MST AXI接口awaddr
    output  [  3:0]     mst_region      ,                       //MST AXI接口awregion
    output  [  7:0]     mst_len         ,                       //MST AXI接口awlen
    output  [  2:0]     mst_size        ,                       //MST AXI接口awsize
    output  [  1:0]     mst_burst       ,                       //MST AXI接口awburst
    output              mst_lock        ,                       //MST AXI接口awlock
    output  [  3:0]     mst_cache       ,                       //MST AXI接口awcache
    output  [  2:0]     mst_prot        ,                       //MST AXI接口awprot
    output  [  3:0]     mst_qos         ,                       //MST AXI接口awqos
    output              mst_valid       ,                       //MST AXI接口awvalid
    input               mst_ready       ,                       //MST AXI接口awready
    //4.0 cmd_fifo
    input               SLVWEN          ,                       //输入侧数据写有效
    input               SLVRDY          ,                       //输入侧数据写有效
    input               SLVLST          ,                       //输入侧LSTBIT标识
    output              CMDEMP          ,                       //输出控制DATWID模块HLD信号
    output  [  5:0]     CMDDTO                                  //输出控制DATWID模块信号总线
);

//1.0 sigs
    wire                NARROW          ;
    wire                HEDCUT          ;
    wire                ENDCUT          ;
    wire    [  2:0]     CONBUS          ;
    wire                slv_adb         ;
    wire    [  2:0]     new_size        ;
    wire    [  7:0]     new_len         ;
    wire    [  8:0]     tma_len         ;
    wire    [  8:0]     tmb_len         ;
    wire    [ 12:0]     U01DTI          ;
    wire    [ 12:0]     U01DTO          ;

//2.0 comp
AXISPF #(.W  (13 ),.L  (4  )) WA_AXISPF (.RST(RST),.CLK(CLK),.WEN   (U01WEN),.REN   (U01REN),.DTI   (U01DTI),.DTO   (U01DTO),.AMF   (      ),.FUL   (U01FUL),.EMP   (U01EMP));
WIDCON_high_width #(.WCH(WCH),.MOD(MOD)) U02_WIDCON(.RST(RST),.CLK(CLK),.CMDDTI(U01DTO),.CMDEMP(U01EMP),.CMDREN(U01REN),.SLVWEN(SLVWEN),.SLVRDY(SLVRDY),.SLVLST(SLVLST),.CMDDTO(CMDDTO));

//3.0 comb
//3.0.1 AXI接口基本信号简单透传
assign mst_id     = slv_id                              ;       //                                      output  [IDW:0]     mst_id          ,
assign mst_addr   = slv_addr                            ;       //                                      output  [ 31:0]     mst_addr        ,
assign mst_region = slv_region                          ;       //                                      output  [  3:0]     mst_region      ,
assign mst_burst  = slv_burst                           ;       //                                      output  [  1:0]     mst_burst       ,
assign mst_lock   = slv_lock                            ;       //                                      output              mst_lock        ,
assign mst_cache  = slv_cache                           ;       //                                      output  [  3:0]     mst_cache       ,
assign mst_prot   = slv_prot                            ;       //                                      output  [  2:0]     mst_prot        ,
assign mst_qos    = slv_qos                             ;       //                                      output  [  3:0]     mst_qos         ,
assign mst_valid  = slv_valid & ~U01FUL                 ;       //OSD深度,满后暂停命令等待数据完成.     output              mst_valid       ,
assign slv_ready  = mst_ready & ~U01FUL                 ;       //OSD深度,满后暂停命令等待数据完成.     input               mst_ready       ,

//3.0.2 简单内部信号处理
assign U01WEN     = slv_valid & slv_ready               ;       //ready信号已判断空满,此处不必再判断.
assign U01DTI     = {CONBUS,slv_size,slv_addr[6:0]}     ;       //传递给数据位宽变换模块控制用.
assign CONBUS     = {HEDCUT,ENDCUT,NARROW}              ;       //传递给数据位宽变换模块控制用.
assign CMDEMP     =  U01EMP                             ;       //传递给数据位宽变换模块控制用.

//3.0.3 AXI接口长度和宽度信号处理
assign NARROW     = (MOD==0) && (slv_size==3'h4) ? STD  :       //判断是否满位宽传输,标准参数下执行标准窄传输,非标下适配为满位宽,以适配不支持窄传输的SLV模块.
                    (MOD==1) && (slv_size==3'h5) ? STD  :       //判断是否满位宽传输,标准参数下执行标准窄传输,非标下适配为满位宽,以适配不支持窄传输的SLV模块.
                    (MOD==2) && (slv_size==3'h5) ? STD  :       //判断是否满位宽传输,标准参数下执行标准窄传输,非标下适配为满位宽,以适配不支持窄传输的SLV模块.
                    (MOD==3) && (slv_size==3'h6) ? STD  :       //判断是否满位宽传输,标准参数下执行标准窄传输,非标下适配为满位宽,以适配不支持窄传输的SLV模块.
                                                   1'b1 ;       //判断是否满位宽传输

assign mst_size   = ( NARROW   ) ? slv_size             :       //窄传输时位宽透传
                                   new_size             ;       //满传输时位宽变换

assign mst_len    = ( NARROW   ) ? slv_len              :       //窄传输时长度透传
                                   new_len              ;       //满传输时长度变换

assign new_size   = ( MOD==1   ) ? 3'h4                 :       // 32位接口固定size为2^2= 4字节.        output  [  2:0]     mst_size        ,
                    ( MOD==2   ) ? 3'h6                 :       //128位接口固定size为2^4=16字节.        output  [  2:0]     mst_size        ,
                                   3'h5                 ;       // 64位接口固定size为2^3= 8字节.        output  [  2:0]     mst_size        ,
assign slv_adb    = ( MOD[1]   ) ? slv_addr[5]          :       //用于计算新数据长度
                                   slv_addr[4]          ;       //用于计算新数据长度
assign tma_len    = {1'b0,slv_len}+{7'b0,slv_adb}       ;
assign tmb_len    = {1'b0,slv_len}                      ;

generate if ( MOD[0] )
begin
assign new_len ={slv_len[6:0],~slv_adb}                 ;
end
else
begin
assign new_len =(slv_len[0]) ? tma_len[8:1]:tmb_len[8:1];
end
endgenerate

assign HEDCUT = ((new_size==5)&&(|slv_addr[4]))   ? 1'b1:
                ((new_size==6)&&(|slv_addr[5]))   ? 1'b1:
                                                    1'b0;
assign ENDCUT = HEDCUT ^ ~slv_len[0]                    ;


//4.0 regs

//5.0 sims


endmodule


