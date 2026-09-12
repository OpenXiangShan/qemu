/*
+-----------------------------------------------------------------------------------------------------------------------------+
|模块说明:                                                                                                                    |
|    1. AXIAXI模块为128/256/512位AXI总线转128/256/512位AXI总线,位宽转换比例为1:2或2:1,通过MOD参数配置,                        |
|       一共支持4种转换模式:                                                                                                  |
|           1. 128位转256位.                                                                                                  |
|           2. 256位转128位.                                                                                                  |
|           3. 256位转512位.                                                                                                  |
|           4. 512位转256位.                                                                                                  |
|       通过以上四种模式可以实现32/64/128位不同位宽的总线系统的互联通信;                                                      |
|            +----------+--------+--------+                                                                                   |
|            |   MOD    |  主    |   从   |                                                                                   |
|            +----------+--------+--------+                                                                                   |
|            |    0     |  128   |   256  |                                                                                   |
|            +----------+--------+--------+                                                                                   |
|            |    1     |  256   |   128  |                                                                                   |
|            +----------+--------+--------+                                                                                   |
|            |    2     |  256   |   512  |                                                                                   |
|            +----------+--------+--------+                                                                                   |
|            |    3     |  512   |   256  |                                                                                   |
|            +----------+--------+--------+                                                                                   |
|    2. 主要规格:                                                                                                             |
|       AXIAXI的规格比较特殊,由于开发过程中需求的不断变化,为适应项目的实际需求,其规格最终不是一个                             |
|       完全标准的AXI位宽转换模块,而且在兼容标准AXI-4协议的位宽转换机制的基础上,还适配了前期项目                              |
|       非标准转换的转换需求.                                                                                                 |
|                                                                                                                             |
|       1. 标准AXI-4位宽转换机制: 支持AXI-4窄传输的机制,若本次收到的命令是窄传输命令,则总线不改变                             |
|                                 其命令模式,仍然以窄传输的方式继续传递;                                                      |
|       2. 特殊AXI-4位宽转换机制: 在1:2的位宽转换方向，凡是入口是满传输的命令，不将其转换成窄传输                             |
|                                 的命令模式，而是按照满传输的机制扩展位宽。这样就可以兼容不支持                              |
|                                 窄传输的SLAVE设备的正常工作。                                                               |
|       3. 是否支持第二种特殊模式可以由参数STD来控制,缺省为支持.目前来看要保持该缺省值(0)。                                   |
|                                                                                                                             |
|                                                                                                                             |
|    3. 端口说明:                                                                                                             |
|       module AXIAXI #                                                                                                       |
|       (                                                                                                                     |
|           parameter   [1:0]   MOD =    0                          ,   //模式设定:0=128-256  1=256-128  2=256-512  3=512-256 |
|           parameter           IDW =    7                          ,   //ID 位宽设定: IDW+1为 ID的真正位宽.                  |
|           parameter   [0:0]   STD =    0                          ,   //是否完全按照AXI窄传输:                              |
|                                                                       //0=多周期窄传输适配为满传输;1=按标准窄传输适配.      |
|           parameter           LVL =    4                          ,   //BUF深度设定: 2**LVL为缓存的真正深度.                |
|           parameter           OSL =    0                          ,   //Outstanding: 2**OSL为真正深度.为安全,缺省为零.      |
|                                                                                                                             |
|           parameter           DTW = (MOD==3)? 512:(MOD==0)? 128:256   ,   //自动计算产生,不要参数传递.主侧位宽-1值.         |
|           parameter           STW = (MOD==3)?  64:(MOD==0)?  16: 32   ,   //自动计算产生,不要参数传递.主侧使能-1值.         |
|           parameter           DOW = (MOD==2)? 512:(MOD==1)? 128:256   ,   //自动计算产生,不要参数传递.从侧位宽-1值.         |
|           parameter           SOW = (MOD==2)?  64:(MOD==1)?  16: 32       //自动计算产生,不要参数传递.从侧使能-1值.         |
|       )                                                                                                                     |
|       (                                                                                                                     |
|           //1.0 复位时钟系统                                                                                                |
|           input               aresetn         ,                       //AXI asyn. reset. active low.                        |
|           input               aclk            ,                       //AXI clock input. rising edge work                   |
|           //2.0 主侧slave AXI接主设备                                                                                       |
|           input   [IDW:0]     slv_axi_awid    ,                       //SLV AXI接口awid                                     |
|           input   [ 31:0]     slv_axi_awaddr  ,                       //SLV AXI接口awaddr                                   |
|           input   [  3:0]     slv_axi_awregion,                       //SLV AXI接口awregion                                 |
|                .....   .....                                                                                                |
|                .....   .....                                                                                                |
|           //3.0 从侧master AXI接从设备                                                                                      |
|           output  [IDW:0]     mst_axi_awid    ,                       //MST AXI接口awid                                     |
|           output  [ 31:0]     mst_axi_awaddr  ,                       //MST AXI接口awaddr                                   |
|           output  [  3:0]     mst_axi_awregion,                       //MST AXI接口awregion                                 |
|                .....   .....                                                                                                |
|                .....   .....                                                                                                |
|       );                                                                                                                    |
+-----------------------------------------------------------------------------------------------------------------------------+
*/
module AXIAXI_high_width #
(
    parameter   [1:0]   MOD =    0                          ,   //模式设定:0=128-256  1=256-128  2=256-512  3=512-256
    parameter           IDW =    8                          ,   //ID 位宽设定.
    parameter   [0:0]   STD =    0                          ,   //是否完全按照AXI窄传输:0=多周期窄传输适配为满传输;1=按标准窄传输适配.
    parameter           LVL =    4                          ,   //BUF深度设定: 2**LVL为缓存的真正深度.
    parameter           OSL =    0                          ,   //Outstanding: 2**OSL为真正深度.为安全,缺省为零.|

    parameter           DTW = (MOD==3)?512:(MOD==0)?128:256   ,   //自动计算产生,不要参数传递.主侧位宽-1值.
    parameter           STW = (MOD==3)? 64:(MOD==0)? 16: 32   ,   //自动计算产生,不要参数传递.主侧使能-1值.
    parameter           DOW = (MOD==2)?512:(MOD==1)?128:256   ,   //自动计算产生,不要参数传递.从侧位宽-1值.
    parameter           SOW = (MOD==2)? 64:(MOD==1)? 16: 32       //自动计算产生,不要参数传递.从侧使能-1值.
)
(
    //1.0 复位时钟系统
    input               aresetn         ,                       //AXI asyn. reset. active low.
    input               aclk            ,                       //AXI clock input. rising edge work
    //2.0 主侧slave AXI接主设备
    input   [IDW-1:0]     slv_axi_awid    ,                       //SLV AXI接口awid
    input   [ 31:0]     slv_axi_awaddr  ,                       //SLV AXI接口awaddr
    input   [  3:0]     slv_axi_awregion,                       //SLV AXI接口awregion
    input   [  7:0]     slv_axi_awlen   ,                       //SLV AXI接口awlen
    input   [  2:0]     slv_axi_awsize  ,                       //SLV AXI接口awsize
    input   [  1:0]     slv_axi_awburst ,                       //SLV AXI接口awburst
    input               slv_axi_awlock  ,                       //SLV AXI接口awlock
    input   [  3:0]     slv_axi_awcache ,                       //SLV AXI接口awcache
    input   [  2:0]     slv_axi_awprot  ,                       //SLV AXI接口awprot
    input   [  3:0]     slv_axi_awqos   ,                       //SLV AXI接口awqos
    input               slv_axi_awvalid ,                       //SLV AXI接口awvalid
    output              slv_axi_awready ,                       //SLV AXI接口awready
    input   [DTW-1:0]     slv_axi_wdata   ,                       //SLV AXI接口wdata
    input   [STW-1:0]     slv_axi_wstrb   ,                       //SLV AXI接口wstrb
    input               slv_axi_wlast   ,                       //SLV AXI接口wlast
    input               slv_axi_wvalid  ,                       //SLV AXI接口wvalid
    output              slv_axi_wready  ,                       //SLV AXI接口wready
    output  [IDW-1:0]     slv_axi_bid     ,                       //SLV AXI接口bid
    output  [  1:0]     slv_axi_bresp   ,                       //SLV AXI接口bresp
    output              slv_axi_bvalid  ,                       //SLV AXI接口bvalid
    input               slv_axi_bready  ,                       //SLV AXI接口bready
    input   [IDW-1:0]     slv_axi_arid    ,                       //SLV AXI接口arid
    input   [ 31:0]     slv_axi_araddr  ,                       //SLV AXI接口araddr
    input   [  3:0]     slv_axi_arregion,                       //SLV AXI接口arregion
    input   [  7:0]     slv_axi_arlen   ,                       //SLV AXI接口arlen
    input   [  2:0]     slv_axi_arsize  ,                       //SLV AXI接口arsize
    input   [  1:0]     slv_axi_arburst ,                       //SLV AXI接口arburst
    input               slv_axi_arlock  ,                       //SLV AXI接口arlock
    input   [  3:0]     slv_axi_arcache ,                       //SLV AXI接口arcache
    input   [  2:0]     slv_axi_arprot  ,                       //SLV AXI接口arprot
    input   [  3:0]     slv_axi_arqos   ,                       //SLV AXI接口arqos
    input               slv_axi_arvalid ,                       //SLV AXI接口arvalid
    output              slv_axi_arready ,                       //SLV AXI接口arready
    output  [IDW-1:0]     slv_axi_rid     ,                       //SLV AXI接口rid
    output  [DTW-1:0]     slv_axi_rdata   ,                       //SLV AXI接口rdata
    output  [  1:0]     slv_axi_rresp   ,                       //SLV AXI接口rresp
    output              slv_axi_rlast   ,                       //SLV AXI接口rlast
    output              slv_axi_rvalid  ,                       //SLV AXI接口rvalid
    input               slv_axi_rready  ,                       //SLV AXI接口rready
    //3.0 从侧master AXI接从设备
    output  [IDW-1:0]     mst_axi_awid    ,                       //MST AXI接口awid
    output  [ 31:0]     mst_axi_awaddr  ,                       //MST AXI接口awaddr
    output  [  3:0]     mst_axi_awregion,                       //MST AXI接口awregion
    output  [  7:0]     mst_axi_awlen   ,                       //MST AXI接口awlen
    output  [  2:0]     mst_axi_awsize  ,                       //MST AXI接口awsize
    output  [  1:0]     mst_axi_awburst ,                       //MST AXI接口awburst
    output              mst_axi_awlock  ,                       //MST AXI接口awlock
    output  [  3:0]     mst_axi_awcache ,                       //MST AXI接口awcache
    output  [  2:0]     mst_axi_awprot  ,                       //MST AXI接口awprot
    output  [  3:0]     mst_axi_awqos   ,                       //MST AXI接口awqos
    output              mst_axi_awvalid ,                       //MST AXI接口awvalid
    input               mst_axi_awready ,                       //MST AXI接口awready
    output  [DOW:0]     mst_axi_wdata   ,                       //MST AXI接口wdata
    output  [SOW:0]     mst_axi_wstrb   ,                       //MST AXI接口wstrb
    output              mst_axi_wlast   ,                       //MST AXI接口wlast
    output              mst_axi_wvalid  ,                       //MST AXI接口wvalid
    input               mst_axi_wready  ,                       //MST AXI接口wready
    input   [IDW-1:0]     mst_axi_bid     ,                       //MST AXI接口bid
    input   [  1:0]     mst_axi_bresp   ,                       //MST AXI接口bresp
    input               mst_axi_bvalid  ,                       //MST AXI接口bvalid
    output              mst_axi_bready  ,                       //MST AXI接口bready
    output  [IDW-1:0]     mst_axi_arid    ,                       //MST AXI接口arid
    output  [ 31:0]     mst_axi_araddr  ,                       //MST AXI接口araddr
    output  [  3:0]     mst_axi_arregion,                       //MST AXI接口arregion
    output  [  7:0]     mst_axi_arlen   ,                       //MST AXI接口arlen
    output  [  2:0]     mst_axi_arsize  ,                       //MST AXI接口arsize
    output  [  1:0]     mst_axi_arburst ,                       //MST AXI接口arburst
    output              mst_axi_arlock  ,                       //MST AXI接口arlock
    output  [  3:0]     mst_axi_arcache ,                       //MST AXI接口arcache
    output  [  2:0]     mst_axi_arprot  ,                       //MST AXI接口arprot
    output  [  3:0]     mst_axi_arqos   ,                       //MST AXI接口arqos
    output              mst_axi_arvalid ,                       //MST AXI接口arvalid
    input               mst_axi_arready ,                       //MST AXI接口arready
    input   [IDW-1:0]     mst_axi_rid     ,                       //MST AXI接口rid
    input   [DOW:0]     mst_axi_rdata   ,                       //MST AXI接口rdata
    input   [  1:0]     mst_axi_rresp   ,                       //MST AXI接口rresp
    input               mst_axi_rlast   ,                       //MST AXI接口rlast
    input               mst_axi_rvalid  ,                       //MST AXI接口rvalid
    output              mst_axi_rready                          //MST AXI接口rready
);

//1.0 sigs
    wire    [IDW-1:0]     s00_axi_awid    ;                       //MST AXI接口awid
    wire    [ 31:0]     s00_axi_awaddr  ;                       //MST AXI接口awaddr
    wire    [  3:0]     s00_axi_awregion;                       //MST AXI接口awregion
    wire    [  7:0]     s00_axi_awlen   ;                       //MST AXI接口awlen
    wire    [  2:0]     s00_axi_awsize  ;                       //MST AXI接口awsize
    wire    [  1:0]     s00_axi_awburst ;                       //MST AXI接口awburst
    wire                s00_axi_awlock  ;                       //MST AXI接口awlock
    wire    [  3:0]     s00_axi_awcache ;                       //MST AXI接口awcache
    wire    [  2:0]     s00_axi_awprot  ;                       //MST AXI接口awprot
    wire    [  3:0]     s00_axi_awqos   ;                       //MST AXI接口awqos
    wire                s00_axi_awvalid ;                       //MST AXI接口awvalid
    wire                s00_axi_awready ;                       //MST AXI接口awready
    wire    [DTW-1:0]     s00_axi_wdata   ;                       //MST AXI接口wdata
    wire    [STW-1:0]     s00_axi_wstrb   ;                       //MST AXI接口wstrb
    wire                s00_axi_wlast   ;                       //MST AXI接口wlast
    wire                s00_axi_wvalid  ;                       //MST AXI接口wvalid
    wire                s00_axi_wready  ;                       //MST AXI接口wready
    wire    [IDW-1:0]     s00_axi_bid     ;                       //MST AXI接口bid
    wire    [  1:0]     s00_axi_bresp   ;                       //MST AXI接口bresp
    wire                s00_axi_bvalid  ;                       //MST AXI接口bvalid
    wire                s00_axi_bready  ;                       //MST AXI接口bready
    wire    [IDW-1:0]     s00_axi_arid    ;                       //MST AXI接口arid
    wire    [ 31:0]     s00_axi_araddr  ;                       //MST AXI接口araddr
    wire    [  3:0]     s00_axi_arregion;                       //MST AXI接口arregion
    wire    [  7:0]     s00_axi_arlen   ;                       //MST AXI接口arlen
    wire    [  2:0]     s00_axi_arsize  ;                       //MST AXI接口arsize
    wire    [  1:0]     s00_axi_arburst ;                       //MST AXI接口arburst
    wire                s00_axi_arlock  ;                       //MST AXI接口arlock
    wire    [  3:0]     s00_axi_arcache ;                       //MST AXI接口arcache
    wire    [  2:0]     s00_axi_arprot  ;                       //MST AXI接口arprot
    wire    [  3:0]     s00_axi_arqos   ;                       //MST AXI接口arqos
    wire                s00_axi_arvalid ;                       //MST AXI接口arvalid
    wire                s00_axi_arready ;                       //MST AXI接口arready
    wire    [IDW-1:0]     s00_axi_rid     ;                       //MST AXI接口rid
    wire    [DTW-1:0]     s00_axi_rdata   ;                       //MST AXI接口rdata
    wire    [  1:0]     s00_axi_rresp   ;                       //MST AXI接口rresp
    wire                s00_axi_rlast   ;                       //MST AXI接口rlast
    wire                s00_axi_rvalid  ;                       //MST AXI接口rvalid
    wire                s00_axi_rready  ;                       //MST AXI接口rready

    wire    [IDW-1:0]     m00_axi_awid    ;                       //MST AXI接口awid
    wire    [ 31:0]     m00_axi_awaddr  ;                       //MST AXI接口awaddr
    wire    [  3:0]     m00_axi_awregion;                       //MST AXI接口awregion
    wire    [  7:0]     m00_axi_awlen   ;                       //MST AXI接口awlen
    wire    [  2:0]     m00_axi_awsize  ;                       //MST AXI接口awsize
    wire    [  1:0]     m00_axi_awburst ;                       //MST AXI接口awburst
    wire                m00_axi_awlock  ;                       //MST AXI接口awlock
    wire    [  3:0]     m00_axi_awcache ;                       //MST AXI接口awcache
    wire    [  2:0]     m00_axi_awprot  ;                       //MST AXI接口awprot
    wire    [  3:0]     m00_axi_awqos   ;                       //MST AXI接口awqos
    wire                m00_axi_awvalid ;                       //MST AXI接口awvalid
    wire                m00_axi_awready ;                       //MST AXI接口awready
    wire    [DOW:0]     m00_axi_wdata   ;                       //MST AXI接口wdata
    wire    [SOW:0]     m00_axi_wstrb   ;                       //MST AXI接口wstrb
    wire                m00_axi_wlast   ;                       //MST AXI接口wlast
    wire                m00_axi_wvalid  ;                       //MST AXI接口wvalid
    wire                m00_axi_wready  ;                       //MST AXI接口wready
    wire    [IDW-1:0]     m00_axi_bid     ;                       //MST AXI接口bid
    wire    [  1:0]     m00_axi_bresp   ;                       //MST AXI接口bresp
    wire                m00_axi_bvalid  ;                       //MST AXI接口bvalid
    wire                m00_axi_bready  ;                       //MST AXI接口bready
    wire    [IDW-1:0]     m00_axi_arid    ;                       //MST AXI接口arid
    wire    [ 31:0]     m00_axi_araddr  ;                       //MST AXI接口araddr
    wire    [  3:0]     m00_axi_arregion;                       //MST AXI接口arregion
    wire    [  7:0]     m00_axi_arlen   ;                       //MST AXI接口arlen
    wire    [  2:0]     m00_axi_arsize  ;                       //MST AXI接口arsize
    wire    [  1:0]     m00_axi_arburst ;                       //MST AXI接口arburst
    wire                m00_axi_arlock  ;                       //MST AXI接口arlock
    wire    [  3:0]     m00_axi_arcache ;                       //MST AXI接口arcache
    wire    [  2:0]     m00_axi_arprot  ;                       //MST AXI接口arprot
    wire    [  3:0]     m00_axi_arqos   ;                       //MST AXI接口arqos
    wire                m00_axi_arvalid ;                       //MST AXI接口arvalid
    wire                m00_axi_arready ;                       //MST AXI接口arready
    wire    [IDW-1:0]     m00_axi_rid     ;                       //MST AXI接口rid
    wire    [DOW:0]     m00_axi_rdata   ;                       //MST AXI接口rdata
    wire    [  1:0]     m00_axi_rresp   ;                       //MST AXI接口rresp
    wire                m00_axi_rlast   ;                       //MST AXI接口rlast
    wire                m00_axi_rvalid  ;                       //MST AXI接口rvalid
    wire                m00_axi_rready  ;                       //MST AXI接口rready
//2.0 comp
AXIBRF #                                                        //
(
    .IDW                ( IDW               ),                  //parameter           IDW =    7                          ,   //ID 位宽设定: IDW+1为 ID的真正位宽.
    .DTW                ( DTW               ),                  //parameter           DTW =   63                          ,   //DAT位宽设定: DTW+1为 DAT的真正位宽.
    .LVL                ( LVL               )                   //parameter           LVL =    2                          ,   //BUF深度设定: 2**LVL为缓存的真正深度.
)
U01_AXIBUF
(
    .aresetn            ( aresetn           ),                  //input               aresetn         ,                       //异步复位信号,低有效
    .aclk               ( aclk              ),                  //input               aclk            ,                       //AXIAHB共用时钟输入
    .slv_axi_awid       ( slv_axi_awid      ),                  //input   [IDW:0]     slv_axi_awid    ,                       //SLV AXI接口awid
    .slv_axi_awaddr     ( slv_axi_awaddr    ),                  //input   [ 31:0]     slv_axi_awaddr  ,                       //SLV AXI接口awaddr
    .slv_axi_awregion   ( slv_axi_awregion  ),                  //input   [  3:0]     slv_axi_awregion,                       //SLV AXI接口awregion
    .slv_axi_awlen      ( slv_axi_awlen     ),                  //input   [  7:0]     slv_axi_awlen   ,                       //SLV AXI接口awlen
    .slv_axi_awsize     ( slv_axi_awsize    ),                  //input   [  2:0]     slv_axi_awsize  ,                       //SLV AXI接口awsize
    .slv_axi_awburst    ( slv_axi_awburst   ),                  //input   [  1:0]     slv_axi_awburst ,                       //SLV AXI接口awburst
    .slv_axi_awlock     ( slv_axi_awlock    ),                  //input               slv_axi_awlock  ,                       //SLV AXI接口awlock
    .slv_axi_awcache    ( slv_axi_awcache   ),                  //input   [  3:0]     slv_axi_awcache ,                       //SLV AXI接口awcache
    .slv_axi_awprot     ( slv_axi_awprot    ),                  //input   [  2:0]     slv_axi_awprot  ,                       //SLV AXI接口awprot
    .slv_axi_awqos      ( slv_axi_awqos     ),                  //input   [  3:0]     slv_axi_awqos   ,                       //SLV AXI接口awqos
    .slv_axi_awvalid    ( slv_axi_awvalid   ),                  //input               slv_axi_awvalid ,                       //SLV AXI接口awvalid
    .slv_axi_awready    ( slv_axi_awready   ),                  //output              slv_axi_awready ,                       //SLV AXI接口awready
    .slv_axi_wdata      ( slv_axi_wdata     ),                  //input   [DTW:0]     slv_axi_wdata   ,                       //SLV AXI接口wdata
    .slv_axi_wstrb      ( slv_axi_wstrb     ),                  //input   [STW:0]     slv_axi_wstrb   ,                       //SLV AXI接口wstrb
    .slv_axi_wlast      ( slv_axi_wlast     ),                  //input               slv_axi_wlast   ,                       //SLV AXI接口wlast
    .slv_axi_wvalid     ( slv_axi_wvalid    ),                  //input               slv_axi_wvalid  ,                       //SLV AXI接口wvalid
    .slv_axi_wready     ( slv_axi_wready    ),                  //output              slv_axi_wready  ,                       //SLV AXI接口wready
    .slv_axi_bid        ( slv_axi_bid       ),                  //output  [IDW:0]     slv_axi_bid     ,                       //SLV AXI接口bid
    .slv_axi_bresp      ( slv_axi_bresp     ),                  //output  [  1:0]     slv_axi_bresp   ,                       //SLV AXI接口bresp
    .slv_axi_bvalid     ( slv_axi_bvalid    ),                  //output              slv_axi_bvalid  ,                       //SLV AXI接口bvalid
    .slv_axi_bready     ( slv_axi_bready    ),                  //input               slv_axi_bready  ,                       //SLV AXI接口bready
    .slv_axi_arid       ( slv_axi_arid      ),                  //input   [IDW:0]     slv_axi_arid    ,                       //SLV AXI接口arid
    .slv_axi_araddr     ( slv_axi_araddr    ),                  //input   [ 31:0]     slv_axi_araddr  ,                       //SLV AXI接口araddr
    .slv_axi_arregion   ( slv_axi_arregion  ),                  //input   [  3:0]     slv_axi_arregion,                       //SLV AXI接口arregion
    .slv_axi_arlen      ( slv_axi_arlen     ),                  //input   [  7:0]     slv_axi_arlen   ,                       //SLV AXI接口arlen
    .slv_axi_arsize     ( slv_axi_arsize    ),                  //input   [  2:0]     slv_axi_arsize  ,                       //SLV AXI接口arsize
    .slv_axi_arburst    ( slv_axi_arburst   ),                  //input   [  1:0]     slv_axi_arburst ,                       //SLV AXI接口arburst
    .slv_axi_arlock     ( slv_axi_arlock    ),                  //input               slv_axi_arlock  ,                       //SLV AXI接口arlock
    .slv_axi_arcache    ( slv_axi_arcache   ),                  //input   [  3:0]     slv_axi_arcache ,                       //SLV AXI接口arcache
    .slv_axi_arprot     ( slv_axi_arprot    ),                  //input   [  2:0]     slv_axi_arprot  ,                       //SLV AXI接口arprot
    .slv_axi_arqos      ( slv_axi_arqos     ),                  //input   [  3:0]     slv_axi_arqos   ,                       //SLV AXI接口arqos
    .slv_axi_arvalid    ( slv_axi_arvalid   ),                  //input               slv_axi_arvalid ,                       //SLV AXI接口arvalid
    .slv_axi_arready    ( slv_axi_arready   ),                  //output              slv_axi_arready ,                       //SLV AXI接口arready
    .slv_axi_rid        ( slv_axi_rid       ),                  //output  [IDW:0]     slv_axi_rid     ,                       //SLV AXI接口rid
    .slv_axi_rdata      ( slv_axi_rdata     ),                  //output  [DTW:0]     slv_axi_rdata   ,                       //SLV AXI接口rdata
    .slv_axi_rresp      ( slv_axi_rresp     ),                  //output  [  1:0]     slv_axi_rresp   ,                       //SLV AXI接口rresp
    .slv_axi_rlast      ( slv_axi_rlast     ),                  //output              slv_axi_rlast   ,                       //SLV AXI接口rlast
    .slv_axi_rvalid     ( slv_axi_rvalid    ),                  //output              slv_axi_rvalid  ,                       //SLV AXI接口rvalid
    .slv_axi_rready     ( slv_axi_rready    ),                  //input               slv_axi_rready  ,                       //SLV AXI接口rready
    .mst_axi_awid       ( s00_axi_awid      ),                  //output  [IDW:0]     mst_axi_awid    ,                       //MST AXI接口awid
    .mst_axi_awaddr     ( s00_axi_awaddr    ),                  //output  [ 31:0]     mst_axi_awaddr  ,                       //MST AXI接口awaddr
    .mst_axi_awregion   ( s00_axi_awregion  ),                  //output  [  3:0]     mst_axi_awregion,                       //MST AXI接口awregion
    .mst_axi_awlen      ( s00_axi_awlen     ),                  //output  [  7:0]     mst_axi_awlen   ,                       //MST AXI接口awlen
    .mst_axi_awsize     ( s00_axi_awsize    ),                  //output  [  2:0]     mst_axi_awsize  ,                       //MST AXI接口awsize
    .mst_axi_awburst    ( s00_axi_awburst   ),                  //output  [  1:0]     mst_axi_awburst ,                       //MST AXI接口awburst
    .mst_axi_awlock     ( s00_axi_awlock    ),                  //output              mst_axi_awlock  ,                       //MST AXI接口awlock
    .mst_axi_awcache    ( s00_axi_awcache   ),                  //output  [  3:0]     mst_axi_awcache ,                       //MST AXI接口awcache
    .mst_axi_awprot     ( s00_axi_awprot    ),                  //output  [  2:0]     mst_axi_awprot  ,                       //MST AXI接口awprot
    .mst_axi_awqos      ( s00_axi_awqos     ),                  //output  [  3:0]     mst_axi_awqos   ,                       //MST AXI接口awqos
    .mst_axi_awvalid    ( s00_axi_awvalid   ),                  //output              mst_axi_awvalid ,                       //MST AXI接口awvalid
    .mst_axi_awready    ( s00_axi_awready   ),                  //input               mst_axi_awready ,                       //MST AXI接口awready
    .mst_axi_wdata      ( s00_axi_wdata     ),                  //output  [DTW:0]     mst_axi_wdata   ,                       //MST AXI接口wdata
    .mst_axi_wstrb      ( s00_axi_wstrb     ),                  //output  [STW:0]     mst_axi_wstrb   ,                       //MST AXI接口wstrb
    .mst_axi_wlast      ( s00_axi_wlast     ),                  //output              mst_axi_wlast   ,                       //MST AXI接口wlast
    .mst_axi_wvalid     ( s00_axi_wvalid    ),                  //output              mst_axi_wvalid  ,                       //MST AXI接口wvalid
    .mst_axi_wready     ( s00_axi_wready    ),                  //input               mst_axi_wready  ,                       //MST AXI接口wready
    .mst_axi_bid        ( s00_axi_bid       ),                  //input   [IDW:0]     mst_axi_bid     ,                       //MST AXI接口bid
    .mst_axi_bresp      ( s00_axi_bresp     ),                  //input   [  1:0]     mst_axi_bresp   ,                       //MST AXI接口bresp
    .mst_axi_bvalid     ( s00_axi_bvalid    ),                  //input               mst_axi_bvalid  ,                       //MST AXI接口bvalid
    .mst_axi_bready     ( s00_axi_bready    ),                  //output              mst_axi_bready  ,                       //MST AXI接口bready
    .mst_axi_arid       ( s00_axi_arid      ),                  //output  [IDW:0]     mst_axi_arid    ,                       //MST AXI接口arid
    .mst_axi_araddr     ( s00_axi_araddr    ),                  //output  [ 31:0]     mst_axi_araddr  ,                       //MST AXI接口araddr
    .mst_axi_arregion   ( s00_axi_arregion  ),                  //output  [  3:0]     mst_axi_arregion,                       //MST AXI接口arregion
    .mst_axi_arlen      ( s00_axi_arlen     ),                  //output  [  7:0]     mst_axi_arlen   ,                       //MST AXI接口arlen
    .mst_axi_arsize     ( s00_axi_arsize    ),                  //output  [  2:0]     mst_axi_arsize  ,                       //MST AXI接口arsize
    .mst_axi_arburst    ( s00_axi_arburst   ),                  //output  [  1:0]     mst_axi_arburst ,                       //MST AXI接口arburst
    .mst_axi_arlock     ( s00_axi_arlock    ),                  //output              mst_axi_arlock  ,                       //MST AXI接口arlock
    .mst_axi_arcache    ( s00_axi_arcache   ),                  //output  [  3:0]     mst_axi_arcache ,                       //MST AXI接口arcache
    .mst_axi_arprot     ( s00_axi_arprot    ),                  //output  [  2:0]     mst_axi_arprot  ,                       //MST AXI接口arprot
    .mst_axi_arqos      ( s00_axi_arqos     ),                  //output  [  3:0]     mst_axi_arqos   ,                       //MST AXI接口arqos
    .mst_axi_arvalid    ( s00_axi_arvalid   ),                  //output              mst_axi_arvalid ,                       //MST AXI接口arvalid
    .mst_axi_arready    ( s00_axi_arready   ),                  //input               mst_axi_arready ,                       //MST AXI接口arready
    .mst_axi_rid        ( s00_axi_rid       ),                  //input   [IDW:0]     mst_axi_rid     ,                       //MST AXI接口rid
    .mst_axi_rdata      ( s00_axi_rdata     ),                  //input   [DTW:0]     mst_axi_rdata   ,                       //MST AXI接口rdata
    .mst_axi_rresp      ( s00_axi_rresp     ),                  //input   [  1:0]     mst_axi_rresp   ,                       //MST AXI接口rresp
    .mst_axi_rlast      ( s00_axi_rlast     ),                  //input               mst_axi_rlast   ,                       //MST AXI接口rlast
    .mst_axi_rvalid     ( s00_axi_rvalid    ),                  //input               mst_axi_rvalid  ,                       //MST AXI接口rvalid
    .mst_axi_rready     ( s00_axi_rready    )                   //output              mst_axi_rready                          //MST AXI接口rready
);

AXIAXO_high_width #
(
    .MOD                ( MOD               ),                  //parameter   [1:0]   MOD =    0                          ,   //模式设定:0=32-64  1=64-32  2=64-128  3=128-64
    .IDW                ( IDW               ),                  //parameter           IDW =    7                          ,   //ID 位宽设定: IDW+1为 ID的真正位宽.
    .STD                ( STD               )                   //parameter   [0:0]   STD =    0                          ,   //是否完全按照AXI窄传输:0=多周期窄传输适配为满传输;1=按标准窄传输适配.
)
U01_AXIAXO
(
    .aresetn            ( aresetn           ),                  //input               aresetn         ,                       //AXI asyn. reset. active low.
    .aclk               ( aclk              ),                  //input               aclk            ,                       //AXI clock input. rising edge work
    .slv_axi_awid       ( s00_axi_awid      ),                  //input   [IDW:0]     slv_axi_awid    ,                       //SLV AXI接口awid
    .slv_axi_awaddr     ( s00_axi_awaddr    ),                  //input   [ 31:0]     slv_axi_awaddr  ,                       //SLV AXI接口awaddr
    .slv_axi_awregion   ( s00_axi_awregion  ),                  //input   [  3:0]     slv_axi_awregion,                       //SLV AXI接口awregion
    .slv_axi_awlen      ( s00_axi_awlen     ),                  //input   [  7:0]     slv_axi_awlen   ,                       //SLV AXI接口awlen
    .slv_axi_awsize     ( s00_axi_awsize    ),                  //input   [  2:0]     slv_axi_awsize  ,                       //SLV AXI接口awsize
    .slv_axi_awburst    ( s00_axi_awburst   ),                  //input   [  1:0]     slv_axi_awburst ,                       //SLV AXI接口awburst
    .slv_axi_awlock     ( s00_axi_awlock    ),                  //input               slv_axi_awlock  ,                       //SLV AXI接口awlock
    .slv_axi_awcache    ( s00_axi_awcache   ),                  //input   [  3:0]     slv_axi_awcache ,                       //SLV AXI接口awcache
    .slv_axi_awprot     ( s00_axi_awprot    ),                  //input   [  2:0]     slv_axi_awprot  ,                       //SLV AXI接口awprot
    .slv_axi_awqos      ( s00_axi_awqos     ),                  //input   [  3:0]     slv_axi_awqos   ,                       //SLV AXI接口awqos
    .slv_axi_awvalid    ( s00_axi_awvalid   ),                  //input               slv_axi_awvalid ,                       //SLV AXI接口awvalid
    .slv_axi_awready    ( s00_axi_awready   ),                  //output              slv_axi_awready ,                       //SLV AXI接口awready
    .slv_axi_wdata      ( s00_axi_wdata     ),                  //input   [DTW:0]     slv_axi_wdata   ,                       //SLV AXI接口wdata
    .slv_axi_wstrb      ( s00_axi_wstrb     ),                  //input   [STW:0]     slv_axi_wstrb   ,                       //SLV AXI接口wstrb
    .slv_axi_wlast      ( s00_axi_wlast     ),                  //input               slv_axi_wlast   ,                       //SLV AXI接口wlast
    .slv_axi_wvalid     ( s00_axi_wvalid    ),                  //input               slv_axi_wvalid  ,                       //SLV AXI接口wvalid
    .slv_axi_wready     ( s00_axi_wready    ),                  //output              slv_axi_wready  ,                       //SLV AXI接口wready
    .slv_axi_bid        ( s00_axi_bid       ),                  //output  [IDW:0]     slv_axi_bid     ,                       //SLV AXI接口bid
    .slv_axi_bresp      ( s00_axi_bresp     ),                  //output  [  1:0]     slv_axi_bresp   ,                       //SLV AXI接口bresp
    .slv_axi_bvalid     ( s00_axi_bvalid    ),                  //output              slv_axi_bvalid  ,                       //SLV AXI接口bvalid
    .slv_axi_bready     ( s00_axi_bready    ),                  //input               slv_axi_bready  ,                       //SLV AXI接口bready
    .slv_axi_arid       ( s00_axi_arid      ),                  //input   [IDW:0]     slv_axi_arid    ,                       //SLV AXI接口arid
    .slv_axi_araddr     ( s00_axi_araddr    ),                  //input   [ 31:0]     slv_axi_araddr  ,                       //SLV AXI接口araddr
    .slv_axi_arregion   ( s00_axi_arregion  ),                  //input   [  3:0]     slv_axi_arregion,                       //SLV AXI接口arregion
    .slv_axi_arlen      ( s00_axi_arlen     ),                  //input   [  7:0]     slv_axi_arlen   ,                       //SLV AXI接口arlen
    .slv_axi_arsize     ( s00_axi_arsize    ),                  //input   [  2:0]     slv_axi_arsize  ,                       //SLV AXI接口arsize
    .slv_axi_arburst    ( s00_axi_arburst   ),                  //input   [  1:0]     slv_axi_arburst ,                       //SLV AXI接口arburst
    .slv_axi_arlock     ( s00_axi_arlock    ),                  //input               slv_axi_arlock  ,                       //SLV AXI接口arlock
    .slv_axi_arcache    ( s00_axi_arcache   ),                  //input   [  3:0]     slv_axi_arcache ,                       //SLV AXI接口arcache
    .slv_axi_arprot     ( s00_axi_arprot    ),                  //input   [  2:0]     slv_axi_arprot  ,                       //SLV AXI接口arprot
    .slv_axi_arqos      ( s00_axi_arqos     ),                  //input   [  3:0]     slv_axi_arqos   ,                       //SLV AXI接口arqos
    .slv_axi_arvalid    ( s00_axi_arvalid   ),                  //input               slv_axi_arvalid ,                       //SLV AXI接口arvalid
    .slv_axi_arready    ( s00_axi_arready   ),                  //output              slv_axi_arready ,                       //SLV AXI接口arready
    .slv_axi_rid        ( s00_axi_rid       ),                  //output  [IDW:0]     slv_axi_rid     ,                       //SLV AXI接口rid
    .slv_axi_rdata      ( s00_axi_rdata     ),                  //output  [DTW:0]     slv_axi_rdata   ,                       //SLV AXI接口rdata
    .slv_axi_rresp      ( s00_axi_rresp     ),                  //output  [  1:0]     slv_axi_rresp   ,                       //SLV AXI接口rresp
    .slv_axi_rlast      ( s00_axi_rlast     ),                  //output              slv_axi_rlast   ,                       //SLV AXI接口rlast
    .slv_axi_rvalid     ( s00_axi_rvalid    ),                  //output              slv_axi_rvalid  ,                       //SLV AXI接口rvalid
    .slv_axi_rready     ( s00_axi_rready    ),                  //input               slv_axi_rready  ,                       //SLV AXI接口rready
    .mst_axi_awid       ( m00_axi_awid      ),                  //output  [IDW:0]     mst_axi_awid    ,                       //MST AXI接口awid
    .mst_axi_awaddr     ( m00_axi_awaddr    ),                  //output  [ 31:0]     mst_axi_awaddr  ,                       //MST AXI接口awaddr
    .mst_axi_awregion   ( m00_axi_awregion  ),                  //output  [  3:0]     mst_axi_awregion,                       //MST AXI接口awregion
    .mst_axi_awlen      ( m00_axi_awlen     ),                  //output  [  7:0]     mst_axi_awlen   ,                       //MST AXI接口awlen
    .mst_axi_awsize     ( m00_axi_awsize    ),                  //output  [  2:0]     mst_axi_awsize  ,                       //MST AXI接口awsize
    .mst_axi_awburst    ( m00_axi_awburst   ),                  //output  [  1:0]     mst_axi_awburst ,                       //MST AXI接口awburst
    .mst_axi_awlock     ( m00_axi_awlock    ),                  //output              mst_axi_awlock  ,                       //MST AXI接口awlock
    .mst_axi_awcache    ( m00_axi_awcache   ),                  //output  [  3:0]     mst_axi_awcache ,                       //MST AXI接口awcache
    .mst_axi_awprot     ( m00_axi_awprot    ),                  //output  [  2:0]     mst_axi_awprot  ,                       //MST AXI接口awprot
    .mst_axi_awqos      ( m00_axi_awqos     ),                  //output  [  3:0]     mst_axi_awqos   ,                       //MST AXI接口awqos
    .mst_axi_awvalid    ( m00_axi_awvalid   ),                  //output              mst_axi_awvalid ,                       //MST AXI接口awvalid
    .mst_axi_awready    ( m00_axi_awready   ),                  //input               mst_axi_awready ,                       //MST AXI接口awready
    .mst_axi_wdata      ( m00_axi_wdata     ),                  //output  [DOW:0]     mst_axi_wdata   ,                       //MST AXI接口wdata
    .mst_axi_wstrb      ( m00_axi_wstrb     ),                  //output  [SOW:0]     mst_axi_wstrb   ,                       //MST AXI接口wstrb
    .mst_axi_wlast      ( m00_axi_wlast     ),                  //output              mst_axi_wlast   ,                       //MST AXI接口wlast
    .mst_axi_wvalid     ( m00_axi_wvalid    ),                  //output              mst_axi_wvalid  ,                       //MST AXI接口wvalid
    .mst_axi_wready     ( m00_axi_wready    ),                  //input               mst_axi_wready  ,                       //MST AXI接口wready
    .mst_axi_bid        ( m00_axi_bid       ),                  //input   [IDW:0]     mst_axi_bid     ,                       //MST AXI接口bid
    .mst_axi_bresp      ( m00_axi_bresp     ),                  //input   [  1:0]     mst_axi_bresp   ,                       //MST AXI接口bresp
    .mst_axi_bvalid     ( m00_axi_bvalid    ),                  //input               mst_axi_bvalid  ,                       //MST AXI接口bvalid
    .mst_axi_bready     ( m00_axi_bready    ),                  //output              mst_axi_bready  ,                       //MST AXI接口bready
    .mst_axi_arid       ( m00_axi_arid      ),                  //output  [IDW:0]     mst_axi_arid    ,                       //MST AXI接口arid
    .mst_axi_araddr     ( m00_axi_araddr    ),                  //output  [ 31:0]     mst_axi_araddr  ,                       //MST AXI接口araddr
    .mst_axi_arregion   ( m00_axi_arregion  ),                  //output  [  3:0]     mst_axi_arregion,                       //MST AXI接口arregion
    .mst_axi_arlen      ( m00_axi_arlen     ),                  //output  [  7:0]     mst_axi_arlen   ,                       //MST AXI接口arlen
    .mst_axi_arsize     ( m00_axi_arsize    ),                  //output  [  2:0]     mst_axi_arsize  ,                       //MST AXI接口arsize
    .mst_axi_arburst    ( m00_axi_arburst   ),                  //output  [  1:0]     mst_axi_arburst ,                       //MST AXI接口arburst
    .mst_axi_arlock     ( m00_axi_arlock    ),                  //output              mst_axi_arlock  ,                       //MST AXI接口arlock
    .mst_axi_arcache    ( m00_axi_arcache   ),                  //output  [  3:0]     mst_axi_arcache ,                       //MST AXI接口arcache
    .mst_axi_arprot     ( m00_axi_arprot    ),                  //output  [  2:0]     mst_axi_arprot  ,                       //MST AXI接口arprot
    .mst_axi_arqos      ( m00_axi_arqos     ),                  //output  [  3:0]     mst_axi_arqos   ,                       //MST AXI接口arqos
    .mst_axi_arvalid    ( m00_axi_arvalid   ),                  //output              mst_axi_arvalid ,                       //MST AXI接口arvalid
    .mst_axi_arready    ( m00_axi_arready   ),                  //input               mst_axi_arready ,                       //MST AXI接口arready
    .mst_axi_rid        ( m00_axi_rid       ),                  //input   [IDW:0]     mst_axi_rid     ,                       //MST AXI接口rid
    .mst_axi_rdata      ( m00_axi_rdata     ),                  //input   [DOW:0]     mst_axi_rdata   ,                       //MST AXI接口rdata
    .mst_axi_rresp      ( m00_axi_rresp     ),                  //input   [  1:0]     mst_axi_rresp   ,                       //MST AXI接口rresp
    .mst_axi_rlast      ( m00_axi_rlast     ),                  //input               mst_axi_rlast   ,                       //MST AXI接口rlast
    .mst_axi_rvalid     ( m00_axi_rvalid    ),                  //input               mst_axi_rvalid  ,                       //MST AXI接口rvalid
    .mst_axi_rready     ( m00_axi_rready    )                   //output              mst_axi_rready                          //MST AXI接口rready
);

AXIBUF #                                                        //将AXIBUF从slv移到mst接口更为有利,因DATWID在无指令时不接收数据.但目前(src093)使用一定有指令,故也不影响,只是改了更有利.
(
    .IDW                ( IDW               ),                  //parameter           IDW =    7                          ,   //ID 位宽设定: IDW+1为 ID的真正位宽.
    .DTW                ( DOW               ),                  //parameter           DTW =   63                          ,   //DAT位宽设定: DTW+1为 DAT的真正位宽.
    .LVL                ( LVL               ),                  //parameter           LVL =    2                          ,   //BUF深度设定: 2**LVL为缓存的真正深度.
    .OSL                ( OSL               )                   //parameter           OSL =    0                          ,   //Outstanding: 2**OSL为真正深度. 为安全起见,缺省值设为零.
)
U03_AXIBUF
(
    .aresetn            ( aresetn           ),                  //input               aresetn         ,                       //异步复位信号,低有效
    .aclk               ( aclk              ),                  //input               aclk            ,                       //AXIAHB共用时钟输入
    .slv_axi_awid       ( m00_axi_awid      ),                  //input   [IDW:0]     slv_axi_awid    ,                       //SLV AXI接口awid
    .slv_axi_awaddr     ( m00_axi_awaddr    ),                  //input   [ 31:0]     slv_axi_awaddr  ,                       //SLV AXI接口awaddr
    .slv_axi_awregion   ( m00_axi_awregion  ),                  //input   [  3:0]     slv_axi_awregion,                       //SLV AXI接口awregion
    .slv_axi_awlen      ( m00_axi_awlen     ),                  //input   [  7:0]     slv_axi_awlen   ,                       //SLV AXI接口awlen
    .slv_axi_awsize     ( m00_axi_awsize    ),                  //input   [  2:0]     slv_axi_awsize  ,                       //SLV AXI接口awsize
    .slv_axi_awburst    ( m00_axi_awburst   ),                  //input   [  1:0]     slv_axi_awburst ,                       //SLV AXI接口awburst
    .slv_axi_awlock     ( m00_axi_awlock    ),                  //input               slv_axi_awlock  ,                       //SLV AXI接口awlock
    .slv_axi_awcache    ( m00_axi_awcache   ),                  //input   [  3:0]     slv_axi_awcache ,                       //SLV AXI接口awcache
    .slv_axi_awprot     ( m00_axi_awprot    ),                  //input   [  2:0]     slv_axi_awprot  ,                       //SLV AXI接口awprot
    .slv_axi_awqos      ( m00_axi_awqos     ),                  //input   [  3:0]     slv_axi_awqos   ,                       //SLV AXI接口awqos
    .slv_axi_awvalid    ( m00_axi_awvalid   ),                  //input               slv_axi_awvalid ,                       //SLV AXI接口awvalid
    .slv_axi_awready    ( m00_axi_awready   ),                  //output              slv_axi_awready ,                       //SLV AXI接口awready
    .slv_axi_wdata      ( m00_axi_wdata     ),                  //input   [DTW:0]     slv_axi_wdata   ,                       //SLV AXI接口wdata
    .slv_axi_wstrb      ( m00_axi_wstrb     ),                  //input   [STW:0]     slv_axi_wstrb   ,                       //SLV AXI接口wstrb
    .slv_axi_wlast      ( m00_axi_wlast     ),                  //input               slv_axi_wlast   ,                       //SLV AXI接口wlast
    .slv_axi_wvalid     ( m00_axi_wvalid    ),                  //input               slv_axi_wvalid  ,                       //SLV AXI接口wvalid
    .slv_axi_wready     ( m00_axi_wready    ),                  //output              slv_axi_wready  ,                       //SLV AXI接口wready
    .slv_axi_bid        ( m00_axi_bid       ),                  //output  [IDW:0]     slv_axi_bid     ,                       //SLV AXI接口bid
    .slv_axi_bresp      ( m00_axi_bresp     ),                  //output  [  1:0]     slv_axi_bresp   ,                       //SLV AXI接口bresp
    .slv_axi_bvalid     ( m00_axi_bvalid    ),                  //output              slv_axi_bvalid  ,                       //SLV AXI接口bvalid
    .slv_axi_bready     ( m00_axi_bready    ),                  //input               slv_axi_bready  ,                       //SLV AXI接口bready
    .slv_axi_arid       ( m00_axi_arid      ),                  //input   [IDW:0]     slv_axi_arid    ,                       //SLV AXI接口arid
    .slv_axi_araddr     ( m00_axi_araddr    ),                  //input   [ 31:0]     slv_axi_araddr  ,                       //SLV AXI接口araddr
    .slv_axi_arregion   ( m00_axi_arregion  ),                  //input   [  3:0]     slv_axi_arregion,                       //SLV AXI接口arregion
    .slv_axi_arlen      ( m00_axi_arlen     ),                  //input   [  7:0]     slv_axi_arlen   ,                       //SLV AXI接口arlen
    .slv_axi_arsize     ( m00_axi_arsize    ),                  //input   [  2:0]     slv_axi_arsize  ,                       //SLV AXI接口arsize
    .slv_axi_arburst    ( m00_axi_arburst   ),                  //input   [  1:0]     slv_axi_arburst ,                       //SLV AXI接口arburst
    .slv_axi_arlock     ( m00_axi_arlock    ),                  //input               slv_axi_arlock  ,                       //SLV AXI接口arlock
    .slv_axi_arcache    ( m00_axi_arcache   ),                  //input   [  3:0]     slv_axi_arcache ,                       //SLV AXI接口arcache
    .slv_axi_arprot     ( m00_axi_arprot    ),                  //input   [  2:0]     slv_axi_arprot  ,                       //SLV AXI接口arprot
    .slv_axi_arqos      ( m00_axi_arqos     ),                  //input   [  3:0]     slv_axi_arqos   ,                       //SLV AXI接口arqos
    .slv_axi_arvalid    ( m00_axi_arvalid   ),                  //input               slv_axi_arvalid ,                       //SLV AXI接口arvalid
    .slv_axi_arready    ( m00_axi_arready   ),                  //output              slv_axi_arready ,                       //SLV AXI接口arready
    .slv_axi_rid        ( m00_axi_rid       ),                  //output  [IDW:0]     slv_axi_rid     ,                       //SLV AXI接口rid
    .slv_axi_rdata      ( m00_axi_rdata     ),                  //output  [DTW:0]     slv_axi_rdata   ,                       //SLV AXI接口rdata
    .slv_axi_rresp      ( m00_axi_rresp     ),                  //output  [  1:0]     slv_axi_rresp   ,                       //SLV AXI接口rresp
    .slv_axi_rlast      ( m00_axi_rlast     ),                  //output              slv_axi_rlast   ,                       //SLV AXI接口rlast
    .slv_axi_rvalid     ( m00_axi_rvalid    ),                  //output              slv_axi_rvalid  ,                       //SLV AXI接口rvalid
    .slv_axi_rready     ( m00_axi_rready    ),                  //input               slv_axi_rready  ,                       //SLV AXI接口rready
    .mst_axi_awid       ( mst_axi_awid      ),                  //output  [IDW:0]     mst_axi_awid    ,                       //MST AXI接口awid
    .mst_axi_awaddr     ( mst_axi_awaddr    ),                  //output  [ 31:0]     mst_axi_awaddr  ,                       //MST AXI接口awaddr
    .mst_axi_awregion   ( mst_axi_awregion  ),                  //output  [  3:0]     mst_axi_awregion,                       //MST AXI接口awregion
    .mst_axi_awlen      ( mst_axi_awlen     ),                  //output  [  7:0]     mst_axi_awlen   ,                       //MST AXI接口awlen
    .mst_axi_awsize     ( mst_axi_awsize    ),                  //output  [  2:0]     mst_axi_awsize  ,                       //MST AXI接口awsize
    .mst_axi_awburst    ( mst_axi_awburst   ),                  //output  [  1:0]     mst_axi_awburst ,                       //MST AXI接口awburst
    .mst_axi_awlock     ( mst_axi_awlock    ),                  //output              mst_axi_awlock  ,                       //MST AXI接口awlock
    .mst_axi_awcache    ( mst_axi_awcache   ),                  //output  [  3:0]     mst_axi_awcache ,                       //MST AXI接口awcache
    .mst_axi_awprot     ( mst_axi_awprot    ),                  //output  [  2:0]     mst_axi_awprot  ,                       //MST AXI接口awprot
    .mst_axi_awqos      ( mst_axi_awqos     ),                  //output  [  3:0]     mst_axi_awqos   ,                       //MST AXI接口awqos
    .mst_axi_awvalid    ( mst_axi_awvalid   ),                  //output              mst_axi_awvalid ,                       //MST AXI接口awvalid
    .mst_axi_awready    ( mst_axi_awready   ),                  //input               mst_axi_awready ,                       //MST AXI接口awready
    .mst_axi_wdata      ( mst_axi_wdata     ),                  //output  [DTW:0]     mst_axi_wdata   ,                       //MST AXI接口wdata
    .mst_axi_wstrb      ( mst_axi_wstrb     ),                  //output  [STW:0]     mst_axi_wstrb   ,                       //MST AXI接口wstrb
    .mst_axi_wlast      ( mst_axi_wlast     ),                  //output              mst_axi_wlast   ,                       //MST AXI接口wlast
    .mst_axi_wvalid     ( mst_axi_wvalid    ),                  //output              mst_axi_wvalid  ,                       //MST AXI接口wvalid
    .mst_axi_wready     ( mst_axi_wready    ),                  //input               mst_axi_wready  ,                       //MST AXI接口wready
    .mst_axi_bid        ( mst_axi_bid       ),                  //input   [IDW:0]     mst_axi_bid     ,                       //MST AXI接口bid
    .mst_axi_bresp      ( mst_axi_bresp     ),                  //input   [  1:0]     mst_axi_bresp   ,                       //MST AXI接口bresp
    .mst_axi_bvalid     ( mst_axi_bvalid    ),                  //input               mst_axi_bvalid  ,                       //MST AXI接口bvalid
    .mst_axi_bready     ( mst_axi_bready    ),                  //output              mst_axi_bready  ,                       //MST AXI接口bready
    .mst_axi_arid       ( mst_axi_arid      ),                  //output  [IDW:0]     mst_axi_arid    ,                       //MST AXI接口arid
    .mst_axi_araddr     ( mst_axi_araddr    ),                  //output  [ 31:0]     mst_axi_araddr  ,                       //MST AXI接口araddr
    .mst_axi_arregion   ( mst_axi_arregion  ),                  //output  [  3:0]     mst_axi_arregion,                       //MST AXI接口arregion
    .mst_axi_arlen      ( mst_axi_arlen     ),                  //output  [  7:0]     mst_axi_arlen   ,                       //MST AXI接口arlen
    .mst_axi_arsize     ( mst_axi_arsize    ),                  //output  [  2:0]     mst_axi_arsize  ,                       //MST AXI接口arsize
    .mst_axi_arburst    ( mst_axi_arburst   ),                  //output  [  1:0]     mst_axi_arburst ,                       //MST AXI接口arburst
    .mst_axi_arlock     ( mst_axi_arlock    ),                  //output              mst_axi_arlock  ,                       //MST AXI接口arlock
    .mst_axi_arcache    ( mst_axi_arcache   ),                  //output  [  3:0]     mst_axi_arcache ,                       //MST AXI接口arcache
    .mst_axi_arprot     ( mst_axi_arprot    ),                  //output  [  2:0]     mst_axi_arprot  ,                       //MST AXI接口arprot
    .mst_axi_arqos      ( mst_axi_arqos     ),                  //output  [  3:0]     mst_axi_arqos   ,                       //MST AXI接口arqos
    .mst_axi_arvalid    ( mst_axi_arvalid   ),                  //output              mst_axi_arvalid ,                       //MST AXI接口arvalid
    .mst_axi_arready    ( mst_axi_arready   ),                  //input               mst_axi_arready ,                       //MST AXI接口arready
    .mst_axi_rid        ( mst_axi_rid       ),                  //input   [IDW:0]     mst_axi_rid     ,                       //MST AXI接口rid
    .mst_axi_rdata      ( mst_axi_rdata     ),                  //input   [DTW:0]     mst_axi_rdata   ,                       //MST AXI接口rdata
    .mst_axi_rresp      ( mst_axi_rresp     ),                  //input   [  1:0]     mst_axi_rresp   ,                       //MST AXI接口rresp
    .mst_axi_rlast      ( mst_axi_rlast     ),                  //input               mst_axi_rlast   ,                       //MST AXI接口rlast
    .mst_axi_rvalid     ( mst_axi_rvalid    ),                  //input               mst_axi_rvalid  ,                       //MST AXI接口rvalid
    .mst_axi_rready     ( mst_axi_rready    )                   //output              mst_axi_rready                          //MST AXI接口rready
);

//3.0 comb

//4.0 regs

//5.0 sims


endmodule


