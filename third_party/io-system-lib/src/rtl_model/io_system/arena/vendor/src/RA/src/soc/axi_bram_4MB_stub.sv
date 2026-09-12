
module axi_bram_4MB(s_axi_aclk, s_axi_aresetn, s_axi_awid, 
  s_axi_awaddr, s_axi_awlen, s_axi_awsize, s_axi_awburst, s_axi_awlock, s_axi_awcache, 
  s_axi_awprot, s_axi_awvalid, s_axi_awready, s_axi_wdata, s_axi_wstrb, s_axi_wlast, 
  s_axi_wvalid, s_axi_wready, s_axi_bid, s_axi_bresp, s_axi_bvalid, s_axi_bready, s_axi_arid, 
  s_axi_araddr, s_axi_arlen, s_axi_arsize, s_axi_arburst, s_axi_arlock, s_axi_arcache, 
  s_axi_arprot, s_axi_arvalid, s_axi_arready, s_axi_rid, s_axi_rdata, s_axi_rresp, s_axi_rlast, 
  s_axi_rvalid, s_axi_rready);

  input s_axi_aclk /* synthesis syn_isclock = 1 */;
  input s_axi_aresetn;
  input [7:0]s_axi_awid;
  input [63:0]s_axi_awaddr;
  input [7:0]s_axi_awlen;
  input [2:0]s_axi_awsize;
  input [1:0]s_axi_awburst;
  input s_axi_awlock;
  input [3:0]s_axi_awcache;
  input [2:0]s_axi_awprot;
  input s_axi_awvalid;
  output s_axi_awready;
  input [511:0]s_axi_wdata;
  input [63:0]s_axi_wstrb;
  input s_axi_wlast;
  input s_axi_wvalid;
  output s_axi_wready;
  output [7:0]s_axi_bid;
  output [1:0]s_axi_bresp;
  output s_axi_bvalid;
  input s_axi_bready;
  input [7:0]s_axi_arid;
  input [63:0]s_axi_araddr;
  input [7:0]s_axi_arlen;
  input [2:0]s_axi_arsize;
  input [1:0]s_axi_arburst;
  input s_axi_arlock;
  input [3:0]s_axi_arcache;
  input [2:0]s_axi_arprot;
  input s_axi_arvalid;
  output s_axi_arready;
  output [7:0]s_axi_rid;
  output [511:0]s_axi_rdata;
  output [1:0]s_axi_rresp;
  output s_axi_rlast;
  output s_axi_rvalid;
  input s_axi_rready;

  ram_subsystem #(
      //   .RAM_BASE    (32*1024*1024),//0x1000_0000=256*1024*1024 /8byte
        .RAM_BASE    (0),//0x1000_0000=256*1024*1024 /8byte
        .WDATA_WIDTH (512),
        .ADDR_WIDTH  (64),
        //.RAM_DEPTH   (2*8*1024),//0x10000=64*1024 /8byte
        .RAM_DEPTH   (4*16*1024),//0x10000=64*1024 /8byte
      //   .RAM_DEPTH   (4*1024*1024),//0x10000=4*1024*1024 /8byte
        .ID_WIDTH    (8)
  ) u_axim_mem_2MB  (
	  .aclk    (s_axi_aclk   ),
	  .aresetn (s_axi_aresetn),
	  .araddr  (s_axi_araddr),
	  .arburst (s_axi_arburst),
	  .arid    ({10'b0,s_axi_arid}),
	  .arlen   (s_axi_arlen),
	  .arlock  (s_axi_arlock),
	  .arready (s_axi_arready),
	  .arsize  (s_axi_arsize),
	  .arvalid (s_axi_arvalid),
	  .awaddr  (s_axi_awaddr),
	  .awburst (s_axi_awburst),
	  .awid    ({10'b0,s_axi_awid}),
	  .awlen   (s_axi_awlen),
	  .awlock  (s_axi_awlock),
	  .awready (s_axi_awready),
	  .awsize  (s_axi_awsize),
	  .awvalid (s_axi_awvalid),
	  .bid     ({nc10_b,s_axi_bid}),
	  .bready  (s_axi_bready),
	  .bresp   (s_axi_bresp),
	  .bvalid  (s_axi_bvalid),
	  .rdata   (s_axi_rdata),
	  .rid     ({nc10_r,s_axi_rid}),
	  .rlast   (s_axi_rlast),
	  .rready  (s_axi_rready),
	  .rresp   (s_axi_rresp),
	  .rvalid  (s_axi_rvalid),
	  .wdata   (s_axi_wdata),
	  .wready  (s_axi_wready),
	  .wstrb   (s_axi_wstrb),
	  .wvalid  (s_axi_wvalid),
	  .arcache (s_axi_arcache),
	  .arprot  (s_axi_arprot),
	  .awcache (s_axi_awcache),
	  .awprot  (s_axi_awprot),
	  .wlast   (s_axi_wlast)
    );

endmodule
