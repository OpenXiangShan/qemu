module axi_bram_512MB(s_axi_aclk, s_axi_aresetn, s_axi_awid,
  s_axi_awaddr, s_axi_awlen, s_axi_awsize, s_axi_awburst, s_axi_awlock, s_axi_awcache,
  s_axi_awprot, s_axi_awvalid, s_axi_awready, s_axi_wdata, s_axi_wstrb, s_axi_wlast,
  s_axi_wvalid, s_axi_wready, s_axi_bid, s_axi_bresp, s_axi_bvalid, s_axi_bready, s_axi_arid,
  s_axi_araddr, s_axi_arlen, s_axi_arsize, s_axi_arburst, s_axi_arlock, s_axi_arcache,
  s_axi_arprot, s_axi_arvalid, s_axi_arready, s_axi_rid, s_axi_rdata, s_axi_rresp, s_axi_rlast,
  s_axi_rvalid, s_axi_rready);

  input s_axi_aclk /* synthesis syn_isclock = 1 */;
  input s_axi_aresetn;
  input [9:0]s_axi_awid;
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
  output [9:0]s_axi_bid;
  output [1:0]s_axi_bresp;
  output s_axi_bvalid;
  input s_axi_bready;
  input [9:0]s_axi_arid;
  input [63:0]s_axi_araddr;
  input [7:0]s_axi_arlen;
  input [2:0]s_axi_arsize;
  input [1:0]s_axi_arburst;
  input s_axi_arlock;
  input [3:0]s_axi_arcache;
  input [2:0]s_axi_arprot;
  input s_axi_arvalid;
  output s_axi_arready;
  output [9:0]s_axi_rid;
  output [511:0]s_axi_rdata;
  output [1:0]s_axi_rresp;
  output s_axi_rlast;
  output s_axi_rvalid;
  input s_axi_rready;

localparam integer RESPONSE_LATENCY = 32;

wire [17:0] mem_bid_ext;
wire [17:0] mem_rid_ext;
wire [1:0] mem_bresp;
wire [1:0] mem_rresp;
wire [511:0] mem_rdata;
wire mem_rlast;
wire mem_bvalid;
wire mem_bready;
wire mem_rvalid;
wire mem_rready;

reg [RESPONSE_LATENCY-1:0] b_valid_pipe;
reg [9:0] b_id_pipe [0:RESPONSE_LATENCY-1];
reg [1:0] b_resp_pipe [0:RESPONSE_LATENCY-1];

reg [RESPONSE_LATENCY-1:0] r_valid_pipe;
reg [9:0] r_id_pipe [0:RESPONSE_LATENCY-1];
reg [511:0] r_data_pipe [0:RESPONSE_LATENCY-1];
reg [1:0] r_resp_pipe [0:RESPONSE_LATENCY-1];
reg r_last_pipe [0:RESPONSE_LATENCY-1];

integer bi;
integer ri;

assign mem_bready = !b_valid_pipe[RESPONSE_LATENCY-1] || s_axi_bready;
assign mem_rready = !r_valid_pipe[RESPONSE_LATENCY-1] || s_axi_rready;

assign s_axi_bvalid = b_valid_pipe[RESPONSE_LATENCY-1];
assign s_axi_bid = b_id_pipe[RESPONSE_LATENCY-1];
assign s_axi_bresp = b_resp_pipe[RESPONSE_LATENCY-1];

assign s_axi_rvalid = r_valid_pipe[RESPONSE_LATENCY-1];
assign s_axi_rid = r_id_pipe[RESPONSE_LATENCY-1];
assign s_axi_rdata = r_data_pipe[RESPONSE_LATENCY-1];
assign s_axi_rresp = r_resp_pipe[RESPONSE_LATENCY-1];
assign s_axi_rlast = r_last_pipe[RESPONSE_LATENCY-1];

ram_subsystem #(
      .RAM_BASE    (0),
      .WDATA_WIDTH (512),
      .ADDR_WIDTH  (64),
      .RAM_DEPTH   (512*16*1024),
      .ID_WIDTH    (8)
  )u_axim_mem_512MB (
      .aclk    (s_axi_aclk),
      .aresetn (s_axi_aresetn),
      .araddr  (s_axi_araddr),
      .arburst (s_axi_arburst),
      .arid    ({10'b0, s_axi_arid}),
      .arlen   (s_axi_arlen),
      .arlock  (s_axi_arlock),
      .arready (s_axi_arready),
      .arsize  (s_axi_arsize),
      .arvalid (s_axi_arvalid),
      .awaddr  (s_axi_awaddr),
      .awburst (s_axi_awburst),
      .awid    ({10'b0, s_axi_awid}),
      .awlen   (s_axi_awlen),
      .awlock  (s_axi_awlock),
      .awready (s_axi_awready),
      .awsize  (s_axi_awsize),
      .awvalid (s_axi_awvalid),
      .bid     (mem_bid_ext),
      .bready  (mem_bready),
      .bresp   (mem_bresp),
      .bvalid  (mem_bvalid),
      .rdata   (mem_rdata),
      .rid     (mem_rid_ext),
      .rlast   (mem_rlast),
      .rready  (mem_rready),
      .rresp   (mem_rresp),
      .rvalid  (mem_rvalid),
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

always @(posedge s_axi_aclk or negedge s_axi_aresetn) begin
  if (!s_axi_aresetn) begin
    for (bi = 0; bi < RESPONSE_LATENCY; bi = bi + 1) begin
      b_valid_pipe[bi] <= 1'b0;
      b_id_pipe[bi] <= 10'b0;
      b_resp_pipe[bi] <= 2'b0;
    end
  end else if (mem_bready) begin
    for (bi = RESPONSE_LATENCY-1; bi > 0; bi = bi - 1) begin
      b_valid_pipe[bi] <= b_valid_pipe[bi-1];
      b_id_pipe[bi] <= b_id_pipe[bi-1];
      b_resp_pipe[bi] <= b_resp_pipe[bi-1];
    end
    b_valid_pipe[0] <= mem_bvalid;
    b_id_pipe[0] <= mem_bid_ext[9:0];
    b_resp_pipe[0] <= mem_bresp;
  end
end

always @(posedge s_axi_aclk or negedge s_axi_aresetn) begin
  if (!s_axi_aresetn) begin
    for (ri = 0; ri < RESPONSE_LATENCY; ri = ri + 1) begin
      r_valid_pipe[ri] <= 1'b0;
      r_id_pipe[ri] <= 10'b0;
      r_data_pipe[ri] <= 512'b0;
      r_resp_pipe[ri] <= 2'b0;
      r_last_pipe[ri] <= 1'b0;
    end
  end else if (mem_rready) begin
    for (ri = RESPONSE_LATENCY-1; ri > 0; ri = ri - 1) begin
      r_valid_pipe[ri] <= r_valid_pipe[ri-1];
      r_id_pipe[ri] <= r_id_pipe[ri-1];
      r_data_pipe[ri] <= r_data_pipe[ri-1];
      r_resp_pipe[ri] <= r_resp_pipe[ri-1];
      r_last_pipe[ri] <= r_last_pipe[ri-1];
    end
    r_valid_pipe[0] <= mem_rvalid;
    r_id_pipe[0] <= mem_rid_ext[9:0];
    r_data_pipe[0] <= mem_rdata;
    r_resp_pipe[0] <= mem_rresp;
    r_last_pipe[0] <= mem_rlast;
  end
end

endmodule
