module axi2ahb #
(
    parameter           IDW =  8,
    parameter           DTW =  64,

    parameter           STW = (DTW/8-1)
)
(
    input               arstn           ,
    input               aclk            ,
    //2.0 slave AXI
    input   [IDW-1:0]     slv_axi_awid    ,
    input   [ 31:0]     slv_axi_awaddr  ,
    input   [  3:0]     slv_axi_awregion,
    input   [  7:0]     slv_axi_awlen   ,
    input   [  2:0]     slv_axi_awsize  ,
    input   [  1:0]     slv_axi_awburst ,
    input               slv_axi_awlock  ,
    input   [  3:0]     slv_axi_awcache ,
    input   [  2:0]     slv_axi_awprot  ,
    input   [  3:0]     slv_axi_awqos   ,
    input               slv_axi_awvalid ,
    output              slv_axi_awready ,
    input   [DTW-1:0]     slv_axi_wdata   ,
    input   [STW:0]     slv_axi_wstrb   ,
    input               slv_axi_wlast   ,
    input               slv_axi_wvalid  ,
    output              slv_axi_wready  ,
    output  [IDW-1-1:0]     slv_axi_bid     ,
    output  [  1:0]     slv_axi_bresp   ,
    output              slv_axi_bvalid  ,
    input               slv_axi_bready  ,
    input   [IDW-1:0]     slv_axi_arid    ,
    input   [ 31:0]     slv_axi_araddr  ,
    input   [  3:0]     slv_axi_arregion,
    input   [  7:0]     slv_axi_arlen   ,
    input   [  2:0]     slv_axi_arsize  ,
    input   [  1:0]     slv_axi_arburst ,
    input               slv_axi_arlock  ,
    input   [  3:0]     slv_axi_arcache ,
    input   [  2:0]     slv_axi_arprot  ,
    input   [  3:0]     slv_axi_arqos   ,
    input               slv_axi_arvalid ,
    output              slv_axi_arready ,
    output  [IDW-1:0]     slv_axi_rid     ,
    output  [DTW-1:0]     slv_axi_rdata   ,
    output  [  1:0]     slv_axi_rresp   ,
    output              slv_axi_rlast   ,
    output              slv_axi_rvalid  ,
    input               slv_axi_rready  ,
    //3.0 master AHB
    output              mst_ahb_sel     ,                       //MST AHBæ?¥å?£hsel
    output  [ 31:0]     mst_ahb_wdata   ,                       //MST AHBæ?¥å?£hwdata
    output  [ 31:0]     mst_ahb_addr    ,                       //MST AHBæ?¥å?£haddr
    output  [  3:0]     mst_ahb_prot    ,                       //MST AHBæ?¥å?£hprot
    output  [  2:0]     mst_ahb_size    ,                       //MST AHBæ?¥å?£hsize
    output  [  2:0]     mst_ahb_burst   ,                       //MST AHBæ?¥å?£hburst
    output  [  1:0]     mst_ahb_trans   ,                       //MST AHBæ?¥å?£htrans
    output              mst_ahb_write   ,                       //MST AHBæ?¥å?£hwrite
    output              mst_ahb_mastlock,                       //MST AHBæ?¥å?£hmastlock
    output              mst_ahb_ready   ,                       //MST AHBæ?¥å?£hready
    input               mst_ahb_readyout,                       //MST AHBæ?¥å?£hreadyout
    input               mst_ahb_resp    ,                       //MST AHBæ?¥å?£hresp
    input   [ 31:0]     mst_ahb_rdata                           //MST AHBæ?¥å?£hrdata
);

//1.0 sigs
    parameter           N   = IDW-1                           ;   //è?ªå?¨è®¡ç®?äº§ç"?,ä¸?è¦?å??æ?°ä¼ é?'.
    parameter           D   = (DTW==128)?127:(DTW==64)?63:31;   //è?ªå?¨è®¡ç®?äº§ç"?,ä¸?è¦?å??æ?°ä¼ é?'.
    parameter           S   = (DTW==128)? 15:(DTW==64)? 7: 3;   //è?ªå?¨è®¡ç®?äº§ç"?,ä¸?è¦?å??æ?°ä¼ é?'.
    parameter           IBS = 2*(IDW-1) + DTW-1 + STW + 131       ;   //è?ªå?¨è®¡ç®?äº§ç"?,ä¸?è¦?å??æ?°ä¼ é?'.
    parameter           OBS = 2*(IDW-1) + DTW-1 +        12       ;   //è?ªå?¨è®¡ç®?äº§ç"?,ä¸?è¦?å??æ?°ä¼ é?'.
    parameter           WA  = N+62                          ;
    parameter           WD  = D+S+3                         ;
    parameter           WR  = N+3                           ;
    parameter           RA  = N+62                          ;
    parameter           RD  = N+D+5                         ;

    wire       [ 71:0]  CMDDTI;
    wire       [ 31:0]  RB_DTR;




//2.0 comp
axisit #
(
    .IDW                ( IDW                   ),      //parameter           IDW =    8                          ,   //ID ä½?å®½è®¾å®?: IDW+1ä¸º IDç??ç??æ­£ä½?å®½.
    .DTW                ( DTW                   )       //parameter           DTW =  128                          ,   //DATä½?å®½è®¾å®?: DTW+1ä¸º DATç??ç??æ­£ä½?å®½.
)
u01_axisit
(
    .RST_N              ( RST_N                 ),      //input               RST_N           ,                       //å¼?æ­¥å¤?ä½?ä¿¡å?·
    .CLK                ( CLK                   ),      //input               CLK             ,                       //AXIAHBå?±ç"¨æ?¶é'?è¾"å?¥
    .slv_axi_awid       ( slv_axi_awid          ),      //input   [IDW:0]     slv_axi_awid    ,                       //SLV AXIæ?¥å?£awid
    .slv_axi_awaddr     ( slv_axi_awaddr        ),      //input   [ 31:0]     slv_axi_awaddr  ,                       //SLV AXIæ?¥å?£awaddr
    .slv_axi_awregion   ( slv_axi_awregion      ),      //input   [  3:0]     slv_axi_awregion,                       //SLV AXIæ?¥å?£awregion
    .slv_axi_awlen      ( slv_axi_awlen         ),      //input   [  7:0]     slv_axi_awlen   ,                       //SLV AXIæ?¥å?£awlen
    .slv_axi_awsize     ( slv_axi_awsize        ),      //input   [  2:0]     slv_axi_awsize  ,                       //SLV AXIæ?¥å?£awsize
    .slv_axi_awburst    ( slv_axi_awburst       ),      //input   [  1:0]     slv_axi_awburst ,                       //SLV AXIæ?¥å?£awburst
    .slv_axi_awlock     ( slv_axi_awlock        ),      //input               slv_axi_awlock  ,                       //SLV AXIæ?¥å?£awlock
    .slv_axi_awcache    ( slv_axi_awcache       ),      //input   [  3:0]     slv_axi_awcache ,                       //SLV AXIæ?¥å?£awcache
    .slv_axi_awprot     ( slv_axi_awprot        ),      //input   [  2:0]     slv_axi_awprot  ,                       //SLV AXIæ?¥å?£awprot
    .slv_axi_awqos      ( slv_axi_awqos         ),      //input   [  3:0]     slv_axi_awqos   ,                       //SLV AXIæ?¥å?£awqos
    .slv_axi_awvalid    ( slv_axi_awvalid       ),      //input               slv_axi_awvalid ,                       //SLV AXIæ?¥å?£awvalid
    .slv_axi_awready    ( slv_axi_awready       ),      //output              slv_axi_awready ,                       //SLV AXIæ?¥å?£awready
    .slv_axi_wdata      ( slv_axi_wdata         ),      //input   [DTW:0]     slv_axi_wdata   ,                       //SLV AXIæ?¥å?£wdata
    .slv_axi_wstrb      ( slv_axi_wstrb         ),      //input   [STW:0]     slv_axi_wstrb   ,                       //SLV AXIæ?¥å?£wstrb
    .slv_axi_wlast      ( slv_axi_wlast         ),      //input               slv_axi_wlast   ,                       //SLV AXIæ?¥å?£wlast
    .slv_axi_wvalid     ( slv_axi_wvalid        ),      //input               slv_axi_wvalid  ,                       //SLV AXIæ?¥å?£wvalid
    .slv_axi_wready     ( slv_axi_wready        ),      //output              slv_axi_wready  ,                       //SLV AXIæ?¥å?£wready
    .slv_axi_bid        ( slv_axi_bid           ),      //output  [IDW:0]     slv_axi_bid     ,                       //SLV AXIæ?¥å?£bid
    .slv_axi_bresp      ( slv_axi_bresp         ),      //output  [  1:0]     slv_axi_bresp   ,                       //SLV AXIæ?¥å?£bresp
    .slv_axi_bvalid     ( slv_axi_bvalid        ),      //output              slv_axi_bvalid  ,                       //SLV AXIæ?¥å?£bvalid
    .slv_axi_bready     ( slv_axi_bready        ),      //input               slv_axi_bready  ,                       //SLV AXIæ?¥å?£bready
    .slv_axi_arid       ( slv_axi_arid          ),      //input   [IDW:0]     slv_axi_arid    ,                       //SLV AXIæ?¥å?£arid
    .slv_axi_araddr     ( slv_axi_araddr        ),      //input   [ 31:0]     slv_axi_araddr  ,                       //SLV AXIæ?¥å?£araddr
    .slv_axi_arregion   ( slv_axi_arregion      ),      //input   [  3:0]     slv_axi_arregion,                       //SLV AXIæ?¥å?£arregion
    .slv_axi_arlen      ( slv_axi_arlen         ),      //input   [  7:0]     slv_axi_arlen   ,                       //SLV AXIæ?¥å?£arlen
    .slv_axi_arsize     ( slv_axi_arsize        ),      //input   [  2:0]     slv_axi_arsize  ,                       //SLV AXIæ?¥å?£arsize
    .slv_axi_arburst    ( slv_axi_arburst       ),      //input   [  1:0]     slv_axi_arburst ,                       //SLV AXIæ?¥å?£arburst
    .slv_axi_arlock     ( slv_axi_arlock        ),      //input               slv_axi_arlock  ,                       //SLV AXIæ?¥å?£arlock
    .slv_axi_arcache    ( slv_axi_arcache       ),      //input   [  3:0]     slv_axi_arcache ,                       //SLV AXIæ?¥å?£arcache
    .slv_axi_arprot     ( slv_axi_arprot        ),      //input   [  2:0]     slv_axi_arprot  ,                       //SLV AXIæ?¥å?£arprot
    .slv_axi_arqos      ( slv_axi_arqos         ),      //input   [  3:0]     slv_axi_arqos   ,                       //SLV AXIæ?¥å?£arqos
    .slv_axi_arvalid    ( slv_axi_arvalid       ),      //input               slv_axi_arvalid ,                       //SLV AXIæ?¥å?£arvalid
    .slv_axi_arready    ( slv_axi_arready       ),      //output              slv_axi_arready ,                       //SLV AXIæ?¥å?£arready
    .slv_axi_rid        ( slv_axi_rid           ),      //output  [IDW:0]     slv_axi_rid     ,                       //SLV AXIæ?¥å?£rid
    .slv_axi_rdata      ( slv_axi_rdata         ),      //output  [DTW:0]     slv_axi_rdata   ,                       //SLV AXIæ?¥å?£rdata
    .slv_axi_rresp      ( slv_axi_rresp         ),      //output  [  1:0]     slv_axi_rresp   ,                       //SLV AXIæ?¥å?£rresp
    .slv_axi_rlast      ( slv_axi_rlast         ),      //output              slv_axi_rlast   ,                       //SLV AXIæ?¥å?£rlast
    .slv_axi_rvalid     ( slv_axi_rvalid        ),      //output              slv_axi_rvalid  ,                       //SLV AXIæ?¥å?£rvalid
    .slv_axi_rready     ( slv_axi_rready        ),      //input               slv_axi_rready  ,                       //SLV AXIæ?¥å?£rready
    .CMDDVI             ( CMDDVI                ),      //output              CMDDVI          ,                       //
    .CMDFUL             ( CMDFUL                ),      //input               CMDFUL          ,                       //
    .CMDDTI             ( CMDDTI                ),      //output  [ 71:0]     CMDDTI          ,                       //
    .RB_DVI             ( RB_DVI                ),      //input               RB_DVI          ,                       //
    .RB_DTR             ( RB_DTR                )       //input   [ 31:0]     RB_DTI                                  //
);

ahbcom u01_ahbcom
(
    .RST_N              ( RST_N                 ),      //input               RST_N           ,                       //å¼?æ­¥å¤?ä½?ä¿¡å?·
    .CLK                ( CLK                   ),      //input               CLK             ,                       //AXIAHBå?±ç"¨æ?¶é'?è¾"å?¥
    .CMDDVI             ( CMDDVI                ),      //input               CMDDVI          ,                       //
    .CMDFUL             ( CMDFUL                ),      //output              CMDFUL          ,                       //
    .CMDDTI             ( CMDDTI                ),      //input      [ 71:0]  CMDDTI          ,                       //
    .RB_DVI             ( RB_DVI                ),      //output              RB_DVI          ,                       //
    .RB_DTR             ( RB_DTR                ),      //output     [ 31:0]  RB_DTR          ,                       //
    .AHBSEL             ( mst_ahb_sel           ),      //output              mst_ahb_sel     ,                       //MST AHBæ?¥å?£hsel
    .AHBDTW             ( mst_ahb_wdata         ),      //output  [ 31:0]     mst_ahb_wdata   ,                       //MST AHBæ?¥å?£hwdata
    .AHBADR             ( mst_ahb_addr          ),      //output  [ 31:0]     mst_ahb_addr    ,                       //MST AHBæ?¥å?£haddr
    .AHBPOT             ( mst_ahb_prot          ),      //output  [  3:0]     mst_ahb_prot    ,                       //MST AHBæ?¥å?£hprot
    .AHBSIZ             ( mst_ahb_size          ),      //output  [  2:0]     mst_ahb_size    ,                       //MST AHBæ?¥å?£hsize
    .AHBINC             ( mst_ahb_burst         ),      //output  [  2:0]     mst_ahb_burst   ,                       //MST AHBæ?¥å?£hburst
    .AHBSEQ             ( mst_ahb_trans         ),      //output  [  1:0]     mst_ahb_trans   ,                       //MST AHBæ?¥å?£htrans
    .AHBWEN             ( mst_ahb_write         ),      //output              mst_ahb_write   ,                       //MST AHBæ?¥å?£hwrite
    .AHBLCK             ( mst_ahb_mastlock      ),      //output              mst_ahb_mastlock,                       //MST AHBæ?¥å?£hmastlock
    .AHBRDY             ( mst_ahb_ready         ),      //output              mst_ahb_ready   ,                       //MST AHBæ?¥å?£hready
    .AHBRDO             ( mst_ahb_readyout      ),      //input               mst_ahb_readyout,                       //MST AHBæ?¥å?£hreadyout
    .AHBERR             ( mst_ahb_resp          ),      //input               mst_ahb_resp    ,                       //MST AHBæ?¥å?£hresp
    .AHBDTR             ( mst_ahb_rdata         )       //input   [ 31:0]     mst_ahb_rdata                           //MST AHBæ?¥å?£hrdata
);
//3.0 comb
assign RST_N            = arstn           ;
assign CLK              = aclk            ;

//4.0 regs

//5.0 sims

endmodule



