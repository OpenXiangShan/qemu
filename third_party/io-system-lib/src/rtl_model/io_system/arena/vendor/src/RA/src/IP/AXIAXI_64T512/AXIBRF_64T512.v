/*
原AXIBRG,现为AXIBUF模块.2个AXIBUF反向并联即可实现两个AXI总线经过BUFFER的桥接.主要优化timing,方便后端实现.
*/
module AXIBRF_64T512 #
(
    parameter           IDW =    8                          ,   //ID 位宽设定: IDW为 ID的真正位宽.
    parameter           DTW =   64                          ,   //DAT位宽设定: DTW为 DAT的真正位宽.
    parameter           LVL =    2                          ,   //BUF深度设定: 2**LVL为缓存的真正深度.

    parameter           STW =  DTW/8                            //自动计算产生,不要参数传递.
)
(
    //1.0 复位时钟系统
    input               aresetn         ,                       //AXI asyn. reset. active low.
    input               aclk            ,                       //AXI clock input. rising edge work
    //2.0 主侧slave AXI接主设备
    input   [IDW-1:0]   slv_axi_awid    ,                       //SLV AXI接口awid
    input   [ 63:0]     slv_axi_awaddr  ,                       //SLV AXI接口awaddr
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
    input   [DTW-1:0]   slv_axi_wdata   ,                       //SLV AXI接口wdata
    input   [STW-1:0]   slv_axi_wstrb   ,                       //SLV AXI接口wstrb
    input               slv_axi_wlast   ,                       //SLV AXI接口wlast
    input               slv_axi_wvalid  ,                       //SLV AXI接口wvalid
    output              slv_axi_wready  ,                       //SLV AXI接口wready
    output  [IDW-1:0]   slv_axi_bid     ,                       //SLV AXI接口bid
    output  [  1:0]     slv_axi_bresp   ,                       //SLV AXI接口bresp
    output              slv_axi_bvalid  ,                       //SLV AXI接口bvalid
    input               slv_axi_bready  ,                       //SLV AXI接口bready
    input   [IDW-1:0]   slv_axi_arid    ,                       //SLV AXI接口arid
    input   [ 63:0]     slv_axi_araddr  ,                       //SLV AXI接口araddr
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
    output  [IDW-1:0]   slv_axi_rid     ,                       //SLV AXI接口rid
    output  [DTW-1:0]   slv_axi_rdata   ,                       //SLV AXI接口rdata
    output  [  1:0]     slv_axi_rresp   ,                       //SLV AXI接口rresp
    output              slv_axi_rlast   ,                       //SLV AXI接口rlast
    output              slv_axi_rvalid  ,                       //SLV AXI接口rvalid
    input               slv_axi_rready  ,                       //SLV AXI接口rready
    //3.0 从侧master AXI接从设备
    output  [IDW-1:0]   mst_axi_awid    ,                       //MST AXI接口awid
    output  [ 63:0]     mst_axi_awaddr  ,                       //MST AXI接口awaddr
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
    output  [DTW-1:0]   mst_axi_wdata   ,                       //MST AXI接口wdata
    output  [STW-1:0]   mst_axi_wstrb   ,                       //MST AXI接口wstrb
    output              mst_axi_wlast   ,                       //MST AXI接口wlast
    output              mst_axi_wvalid  ,                       //MST AXI接口wvalid
    input               mst_axi_wready  ,                       //MST AXI接口wready
    input   [IDW-1:0]   mst_axi_bid     ,                       //MST AXI接口bid
    input   [  1:0]     mst_axi_bresp   ,                       //MST AXI接口bresp
    input               mst_axi_bvalid  ,                       //MST AXI接口bvalid
    output              mst_axi_bready  ,                       //MST AXI接口bready
    output  [IDW-1:0]   mst_axi_arid    ,                       //MST AXI接口arid
    output  [ 63:0]     mst_axi_araddr  ,                       //MST AXI接口araddr
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
    input   [IDW-1:0]   mst_axi_rid     ,                       //MST AXI接口rid
    input   [DTW-1:0]   mst_axi_rdata   ,                       //MST AXI接口rdata
    input   [  1:0]     mst_axi_rresp   ,                       //MST AXI接口rresp
    input               mst_axi_rlast   ,                       //MST AXI接口rlast
    input               mst_axi_rvalid  ,                       //MST AXI接口rvalid
    output              mst_axi_rready                          //MST AXI接口rready
);

//1.0 sigs
    parameter           IBS = 2*(IDW-1) + (DTW-1) + (STW-1) + 131 + 64       ;   //自动计算产生,不要参数传递.
    parameter           OBS = 2*(IDW-1) + (DTW-1) +        12       ;   //自动计算产生,不要参数传递.
    parameter           N   = (IDW-1)                           ;   //自动计算产生,不要参数传递.
    parameter           D   = ((DTW-1)==127)?127:((DTW-1)==63)?63:31;   //自动计算产生,不要参数传递.
    parameter           S   = ((DTW-1)==127)? 15:((DTW-1)==63)? 7: 3;   //自动计算产生,不要参数传递.
    parameter           WA  = N+62+32                          ;
    parameter           WD  = D+S+3                         ;
    parameter           WR  = N+3                           ;
    parameter           RA  = N+62+32                          ;
    parameter           RD  = N+D+5                         ;
    wire      [ IBS:0]  SLVIBS                              ;   //AXI总线IN  BUS信号
    wire      [ IBS:0]  MSTIBS                              ;   //AXI总线IN  BUS信号
    wire      [ OBS:0]  SLVOBS                              ;   //AXI总线OUT BUS信号
    wire      [ OBS:0]  MSTOBS                              ;   //AXI总线OUT BUS信号
    wire      [WA-1:0]  WA_DTI                              ;
    wire      [RA-1:0]  RA_DTI                              ;
    wire      [WD-1:0]  WD_DTI                              ;
    wire      [RD-1:0]  RD_DTI                              ;
    wire      [WR-1:0]  WR_DTI                              ;
    wire      [WA-1:0]  WA_DTO                              ;
    wire      [RA-1:0]  RA_DTO                              ;
    wire      [WD-1:0]  WD_DTO                              ;
    wire      [RD-1:0]  RD_DTO                              ;
    wire      [WR-1:0]  WR_DTO                              ;

//2.0 comp
AXISRF #(.W (WA),.L (LVL),.A (1)) WA_AXISPF (.RST ( RST ),.CLK ( CLK ),.WEN (WA_WEN & ~WA_FUL),.REN ( WA_REN & ~WA_EMP),.DTI ( WA_DTI ),.DTO ( WA_DTO ),.AMF ( ),.FUL ( WA_FUL ),.EMP ( WA_EMP ) );
AXISRF #(.W (RA),.L (LVL),.A (1)) RA_AXISPF (.RST ( RST ),.CLK ( CLK ),.WEN (RA_WEN & ~RA_FUL),.REN ( RA_REN & ~RA_EMP),.DTI ( RA_DTI ),.DTO ( RA_DTO ),.AMF ( ),.FUL ( RA_FUL ),.EMP ( RA_EMP ) );
AXISRF #(.W (WD),.L (LVL),.A (1)) WD_AXISPF (.RST ( RST ),.CLK ( CLK ),.WEN (WD_WEN & ~WD_FUL),.REN ( WD_REN & ~WD_EMP),.DTI ( WD_DTI ),.DTO ( WD_DTO ),.AMF ( ),.FUL ( WD_FUL ),.EMP ( WD_EMP ) );
AXISRF #(.W (RD),.L (LVL),.A (1)) RD_AXISPF (.RST ( RST ),.CLK ( CLK ),.WEN (RD_WEN & ~RD_FUL),.REN ( RD_REN & ~RD_EMP),.DTI ( RD_DTI ),.DTO ( RD_DTO ),.AMF ( ),.FUL ( RD_FUL ),.EMP ( RD_EMP ) );
AXISRF #(.W (WR),.L (LVL),.A (1)) WR_AXISPF (.RST ( RST ),.CLK ( CLK ),.WEN (WR_WEN & ~WR_FUL),.REN ( WR_REN & ~WR_EMP),.DTI ( WR_DTI ),.DTO ( WR_DTO ),.AMF ( ),.FUL ( WR_FUL ),.EMP ( WR_EMP ) );

//3.0 comb
assign RST    =~aresetn;
assign CLK    = aclk   ;

assign SLVIBS = {slv_axi_rready,slv_axi_arvalid,slv_axi_arqos,slv_axi_arprot,slv_axi_arcache,slv_axi_arlock,slv_axi_arburst,slv_axi_arsize,slv_axi_arlen,slv_axi_arregion,slv_axi_araddr,slv_axi_arid,slv_axi_bready,slv_axi_wvalid,slv_axi_wlast,slv_axi_wstrb,slv_axi_wdata,slv_axi_awvalid,slv_axi_awqos,slv_axi_awprot,slv_axi_awcache,slv_axi_awlock,slv_axi_awburst,slv_axi_awsize,slv_axi_awlen,slv_axi_awregion,slv_axi_awaddr,slv_axi_awid}          ;
assign          {mst_axi_rready,mst_axi_arvalid,mst_axi_arqos,mst_axi_arprot,mst_axi_arcache,mst_axi_arlock,mst_axi_arburst,mst_axi_arsize,mst_axi_arlen,mst_axi_arregion,mst_axi_araddr,mst_axi_arid,mst_axi_bready,mst_axi_wvalid,mst_axi_wlast,mst_axi_wstrb,mst_axi_wdata,mst_axi_awvalid,mst_axi_awqos,mst_axi_awprot,mst_axi_awcache,mst_axi_awlock,mst_axi_awburst,mst_axi_awsize,mst_axi_awlen,mst_axi_awregion,mst_axi_awaddr,mst_axi_awid}  = MSTIBS;
assign          {slv_axi_rvalid,slv_axi_rlast,slv_axi_rresp,slv_axi_rdata,slv_axi_rid,slv_axi_arready,slv_axi_bvalid,slv_axi_bresp,slv_axi_bid,slv_axi_wready,slv_axi_awready                                                                                                                                                                                                                                                                      }  = SLVOBS;
assign MSTOBS = {mst_axi_rvalid,mst_axi_rlast,mst_axi_rresp,mst_axi_rdata,mst_axi_rid,mst_axi_arready,mst_axi_bvalid,mst_axi_bresp,mst_axi_bid,mst_axi_wready,mst_axi_awready                                                                                                                                                                                                                                                                      }          ;

assign SLVOBS = {~RD_EMP,RD_DTO,~RA_FUL,~WR_EMP,WR_DTO,~WD_FUL,~WA_FUL       }         ;
assign          { RD_WEN,RD_DTI, RA_REN, WR_WEN,WR_DTI, WD_REN, WA_REN       } = MSTOBS;

assign          { RD_REN, RA_WEN,RA_DTI, WR_REN, WD_WEN,WD_DTI, WA_WEN,WA_DTI} = SLVIBS;
assign MSTIBS = {~RD_FUL,~RA_EMP,RA_DTO,~WR_FUL,~WD_EMP,WD_DTO,~WA_EMP,WA_DTO}         ;

//4.0 regs

//5.0 sims


endmodule
