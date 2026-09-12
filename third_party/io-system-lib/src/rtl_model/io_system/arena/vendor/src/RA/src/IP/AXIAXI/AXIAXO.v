module AXIAXO #
(
    parameter   [1:0]   MOD =    0                          ,   //模式设定:0=32-64  1=64-32  2=64-128  3=128-64
    parameter           IDW =    8                          ,   //ID 位宽设定.
    parameter   [0:0]   STD =    0                          ,   //是否完全按照AXI窄传输:0=多周期窄传输适配为满传输;1=按标准窄传输适配.

    parameter           DTW = (MOD==3)?128:(MOD==0)?32:66   ,   //自动计算产生,不要参数传递.主侧位宽.
    parameter           STW = (MOD==3)? 16:(MOD==0)? 4: 8   ,   //自动计算产生,不要参数传递.主侧使能.
    parameter           DOW = (MOD==2)?128:(MOD==1)?32:64   ,   //自动计算产生,不要参数传递.从侧位宽.
    parameter           SOW = (MOD==2)? 16:(MOD==1)? 4: 8   ,   //自动计算产生,不要参数传递.从侧使能.
    parameter           WDW = DTW-1 + STW-1 + 2                 ,   //自动计算产生,不要参数传递.
    parameter           RDW = DOW-1 + IDW-1 + 4                     //自动计算产生,不要参数传递.
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
    output  [DOW-1:0]     mst_axi_wdata   ,                       //MST AXI接口wdata
    output  [SOW-1:0]     mst_axi_wstrb   ,                       //MST AXI接口wstrb
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
    input   [DOW-1:0]     mst_axi_rdata   ,                       //MST AXI接口rdata
    input   [  1:0]     mst_axi_rresp   ,                       //MST AXI接口rresp
    input               mst_axi_rlast   ,                       //MST AXI接口rlast
    input               mst_axi_rvalid  ,                       //MST AXI接口rvalid
    output              mst_axi_rready                          //MST AXI接口rready
);

//1.0 sigs
    wire    [  5:0]     U01CMD          ;
    wire    [  5:0]     U03CMD          ;

//2.0 comp
ADRWID #
(
    .STD                ( STD                   ),      //parameter   [0:0]   STD =     0     ,                       //是否完全按照AXI窄传输:0=多周期窄传输适配为满传输;1=按标准窄传输适配.
    .WCH                (  1                    ),      //parameter   [0:0]   WCH =     0     ,                       //写通道设定:0=读通道用;1=写通道用.
    .MOD                ( MOD                   ),      //parameter   [1:0]   MOD =     0     ,                       //模式设定:0=32-64  1=64-32  2=64-128  3=128-64
    .IDW                ( IDW                   )       //parameter           IDW =     6     ,                       //ID位宽设定: IDW+1为ID的真正位宽.
)
U01_ADRWID
(
    //1.0 复位时钟系统
    .RST                ( RST                   ),      //input               RST             ,                       //复位信号
    .CLK                ( CLK                   ),      //input               CLK             ,                       //AXI时钟输入
    //2.0 主侧slave AXI接主设备
    .slv_id             ( slv_axi_awid          ),      //input   [  3:0]     slv_id          ,                       //SLV AXI接口awid
    .slv_addr           ( slv_axi_awaddr        ),      //input   [ 31:0]     slv_addr        ,                       //SLV AXI接口awaddr
    .slv_region         ( slv_axi_awregion      ),      //input   [  3:0]     slv_region      ,                       //SLV AXI接口awregion
    .slv_len            ( slv_axi_awlen         ),      //input   [  7:0]     slv_len         ,                       //SLV AXI接口awlen
    .slv_size           ( slv_axi_awsize        ),      //input   [  2:0]     slv_size        ,                       //SLV AXI接口awsize
    .slv_burst          ( slv_axi_awburst       ),      //input   [  1:0]     slv_burst       ,                       //SLV AXI接口awburst
    .slv_lock           ( slv_axi_awlock        ),      //input               slv_lock        ,                       //SLV AXI接口awlock
    .slv_cache          ( slv_axi_awcache       ),      //input   [  3:0]     slv_cache       ,                       //SLV AXI接口awcache
    .slv_prot           ( slv_axi_awprot        ),      //input   [  2:0]     slv_prot        ,                       //SLV AXI接口awprot
    .slv_qos            ( slv_axi_awqos         ),      //input   [  3:0]     slv_qos         ,                       //SLV AXI接口awqos
    .slv_valid          ( slv_axi_awvalid       ),      //input               slv_valid       ,                       //SLV AXI接口awvalid
    .slv_ready          ( slv_axi_awready       ),      //output              slv_ready       ,                       //SLV AXI接口awready
    //3.0 从侧master AXI接从设备
    .mst_id             ( mst_axi_awid          ),      //output  [  3:0]     mst_id          ,                       //MST AXI接口awid
    .mst_addr           ( mst_axi_awaddr        ),      //output  [ 31:0]     mst_addr        ,                       //MST AXI接口awaddr
    .mst_region         ( mst_axi_awregion      ),      //output  [  3:0]     mst_region      ,                       //MST AXI接口awregion
    .mst_len            ( mst_axi_awlen         ),      //output  [  7:0]     mst_len         ,                       //MST AXI接口awlen
    .mst_size           ( mst_axi_awsize        ),      //output  [  2:0]     mst_size        ,                       //MST AXI接口awsize
    .mst_burst          ( mst_axi_awburst       ),      //output  [  1:0]     mst_burst       ,                       //MST AXI接口awburst
    .mst_lock           ( mst_axi_awlock        ),      //output              mst_lock        ,                       //MST AXI接口awlock
    .mst_cache          ( mst_axi_awcache       ),      //output  [  3:0]     mst_cache       ,                       //MST AXI接口awcache
    .mst_prot           ( mst_axi_awprot        ),      //output  [  2:0]     mst_prot        ,                       //MST AXI接口awprot
    .mst_qos            ( mst_axi_awqos         ),      //output  [  3:0]     mst_qos         ,                       //MST AXI接口awqos
    .mst_valid          ( mst_axi_awvalid       ),      //output              mst_valid       ,                       //MST AXI接口awvalid
    .mst_ready          ( mst_axi_awready       ),      //input               mst_ready       ,                       //MST AXI接口awready
    //4.0 cmd_fifo
    .SLVWEN             ( slv_axi_wvalid        ),      //input               SLVWEN          ,                       //输入侧数据写有效
    .SLVRDY             ( slv_axi_wready        ),      //input               SLVRDY          ,                       //输入侧数据写有效
    .SLVLST             ( slv_axi_wlast         ),      //input               SLVLST          ,                       //输入侧LSTBIT标识
    .CMDEMP             ( U01EMP                ),      //output              CMDEMP          ,                       //输出控制DATWID模块HLD信号
    .CMDDTO             ( U01CMD                )       //output  [  5:0]     CMDDTO                                  //输出控制DATWID模块信号总线
);

DATWID #
(
    .WCH                (  1                    ),      //parameter   [0:0]   WCH =     0     ,                       //写通道设定:0=读通道用;1=写通道用.
    .MOD                ( MOD                   )       //parameter   [1:0]   MOD =     0     ,                       //模式设定:0=32-64  1=64-32  2=64-128  3=128-64
)
U02_DATWID
(
    .RST                ( RST                   ),      //input               RST             ,                       //复位信号
    .CLK                ( CLK                   ),      //input               CLK             ,                       //AXI时钟输入
    .DTI                ( slv_axi_wdata         ),      //input   [DTW:0]     DTI             ,                       //SLV AXI接口wdata
    .STI                ( slv_axi_wstrb         ),      //input   [STW:0]     STI             ,                       //SLV AXI接口wstrb,读通道中无用但必须接全高.
    .LSI                ( slv_axi_wlast         ),      //input               LSI             ,                       //SLV AXI接口wlast
    .HVI                ( slv_axi_wvalid        ),      //input               DVI             ,                       //SLV AXI接口wvalid
    .RDO                ( slv_axi_wready        ),      //output              RDO             ,                       //SLV AXI接口wready
    .DTO                ( mst_axi_wdata         ),      //output  [DOW:0]     DTO             ,                       //MST AXI接口wdata
    .STO                ( mst_axi_wstrb         ),      //output  [SOW:0]     STO             ,                       //MST AXI接口wstrb
    .LSO                ( mst_axi_wlast         ),      //output              LSO             ,                       //MST AXI接口wlast
    .DVO                ( mst_axi_wvalid        ),      //output              DVO             ,                       //MST AXI接口wvalid
    .RDI                ( mst_axi_wready        ),      //input               RDI             ,                       //MST AXI接口wready
    .HLD                ( U01EMP                ),      //input               HLD             ,                       //
    .CMD                ( U01CMD                )       //input   [  5:0]     CMD                                     //WIDCON输出控制DATWID模块信号总线
);

ADRWID #
(
    .STD                ( STD                   ),      //parameter   [0:0]   STD =     0     ,                       //是否完全按照AXI窄传输:0=多周期窄传输适配为满传输;1=按标准窄传输适配.
    .WCH                (  0                    ),      //parameter   [0:0]   WCH =     0     ,                       //写通道设定:0=读通道用;1=写通道用.
    .MOD                ( MOD                   ),      //parameter   [1:0]   MOD =     0     ,                       //模式设定:0=32-64  1=64-32  2=64-128  3=128-64
    .IDW                ( IDW                   )       //parameter           IDW =     6     ,                       //ID位宽设定: IDW+1为ID的真正位宽.
)
U03_ADRWID
(
    //1.0 复位时钟系统
    .RST                ( RST                   ),      //input               RST             ,                       //复位信号
    .CLK                ( CLK                   ),      //input               CLK             ,                       //AXI时钟输入
    //2.0 主侧slave AXI接主设备
    .slv_id             ( slv_axi_arid          ),      //input   [  3:0]     slv_id          ,                       //SLV AXI接口awid
    .slv_addr           ( slv_axi_araddr        ),      //input   [ 31:0]     slv_addr        ,                       //SLV AXI接口awaddr
    .slv_region         ( slv_axi_arregion      ),      //input   [  3:0]     slv_region      ,                       //SLV AXI接口awregion
    .slv_len            ( slv_axi_arlen         ),      //input   [  7:0]     slv_len         ,                       //SLV AXI接口awlen
    .slv_size           ( slv_axi_arsize        ),      //input   [  2:0]     slv_size        ,                       //SLV AXI接口awsize
    .slv_burst          ( slv_axi_arburst       ),      //input   [  1:0]     slv_burst       ,                       //SLV AXI接口awburst
    .slv_lock           ( slv_axi_arlock        ),      //input               slv_lock        ,                       //SLV AXI接口awlock
    .slv_cache          ( slv_axi_arcache       ),      //input   [  3:0]     slv_cache       ,                       //SLV AXI接口awcache
    .slv_prot           ( slv_axi_arprot        ),      //input   [  2:0]     slv_prot        ,                       //SLV AXI接口awprot
    .slv_qos            ( slv_axi_arqos         ),      //input   [  3:0]     slv_qos         ,                       //SLV AXI接口awqos
    .slv_valid          ( slv_axi_arvalid       ),      //input               slv_valid       ,                       //SLV AXI接口awvalid
    .slv_ready          ( slv_axi_arready       ),      //output              slv_ready       ,                       //SLV AXI接口awready
    //3.0 从侧master AXI接从设备
    .mst_id             ( mst_axi_arid          ),      //output  [  3:0]     mst_id          ,                       //MST AXI接口awid
    .mst_addr           ( mst_axi_araddr        ),      //output  [ 31:0]     mst_addr        ,                       //MST AXI接口awaddr
    .mst_region         ( mst_axi_arregion      ),      //output  [  3:0]     mst_region      ,                       //MST AXI接口awregion
    .mst_len            ( mst_axi_arlen         ),      //output  [  7:0]     mst_len         ,                       //MST AXI接口awlen
    .mst_size           ( mst_axi_arsize        ),      //output  [  2:0]     mst_size        ,                       //MST AXI接口awsize
    .mst_burst          ( mst_axi_arburst       ),      //output  [  1:0]     mst_burst       ,                       //MST AXI接口awburst
    .mst_lock           ( mst_axi_arlock        ),      //output              mst_lock        ,                       //MST AXI接口awlock
    .mst_cache          ( mst_axi_arcache       ),      //output  [  3:0]     mst_cache       ,                       //MST AXI接口awcache
    .mst_prot           ( mst_axi_arprot        ),      //output  [  2:0]     mst_prot        ,                       //MST AXI接口awprot
    .mst_qos            ( mst_axi_arqos         ),      //output  [  3:0]     mst_qos         ,                       //MST AXI接口awqos
    .mst_valid          ( mst_axi_arvalid       ),      //output              mst_valid       ,                       //MST AXI接口awvalid
    .mst_ready          ( mst_axi_arready       ),      //input               mst_ready       ,                       //MST AXI接口awready
    //4.0 cmd_fifo
    .SLVWEN             ( mst_axi_rvalid        ),      //input               SLVWEN          ,                       //输入侧数据写有效
    .SLVRDY             ( mst_axi_rready        ),      //input               SLVRDY          ,                       //输入侧数据写有效
    .SLVLST             ( mst_axi_rlast         ),      //input               SLVLST          ,                       //输入侧LSTBIT标识
    .CMDEMP             ( U03EMP                ),      //output              CMDEMP          ,                       //输出控制DATWID模块HLD信号
    .CMDDTO             ( U03CMD                )       //output  [  5:0]     CMDDTO                                  //输出控制DATWID模块信号总线
);

DATWID #
(
    .WCH                (  0                    ),      //parameter   [0:0]   WCH =     0     ,                       //写通道设定:0=读通道用;1=写通道用.
    .MOD                ( MOD                   )       //parameter   [1:0]   MOD=      0     ,                       //模式设定:0=32-64  1=64-32  2=64-128  3=128-64
)
U04_DATWID
(
    .RST                ( RST                   ),      //input               RST             ,                       //复位信号
    .CLK                ( CLK                   ),      //input               CLK             ,                       //AXI时钟输入
    .DTI                ( mst_axi_rdata         ),      //input   [DTW:0]     DTI             ,                       //SLV AXI接口wdata
    .STI                ( {(SOW){1'b0}}       ),      //input   [STW:0]     STI             ,                       //SLV AXI接口wstrb,读通道中无用但必须接全高.
    .LSI                ( mst_axi_rlast         ),      //input               LSI             ,                       //SLV AXI接口wlast
    .HVI                ( mst_axi_rvalid        ),      //input               DVI             ,                       //SLV AXI接口wvalid
    .RDO                ( mst_axi_rready        ),      //output              RDO             ,                       //SLV AXI接口wready
    .DTO                ( slv_axi_rdata         ),      //output  [DOW:0]     DTO             ,                       //MST AXI接口wdata
    .STO                (                       ),      //output  [SOW:0]     STO             ,                       //MST AXI接口wstrb
    .LSO                ( slv_axi_rlast         ),      //output              LSO             ,                       //MST AXI接口wlast
    .DVO                ( slv_axi_rvalid        ),      //output              DVO             ,                       //MST AXI接口wvalid
    .RDI                ( slv_axi_rready        ),      //input               RDI             ,                       //MST AXI接口wready
    .HLD                ( U03EMP                ),      //input               HLD             ,                       //
    .CMD                ( U03CMD                )       //input   [  5:0]     CMD                                     //WIDCON输出控制DATWID模块信号总线
);


//3.0 comb
assign RST              = ~aresetn        ;
assign CLK              =  aclk           ;

assign slv_axi_bid      =  mst_axi_bid    ;
assign slv_axi_bresp    =  mst_axi_bresp  ;
assign slv_axi_bvalid   =  mst_axi_bvalid ;
assign mst_axi_bready   =  slv_axi_bready ;

assign slv_axi_rid      =  mst_axi_rid    ;
assign slv_axi_rresp    =  mst_axi_rresp  ;

//4.0 regs

//5.0 sims


endmodule


