// SPDX-License-Identifier: Apache-2.0
// Directed check of the production E6 wrapper, including its outer FIFOs.
`timescale 1ns/1ps
module e6_width_tb;
  import io_system_rtl_arena_pkg::*;
  logic clk=0, rstn=0;
  always #5 clk=~clk;
  a64i4_req_t s_req;
  a64i4_resp_t s_resp;
  a512i4_req_t m_req;
  a512i4_resp_t m_resp;
  io_system_rtl_e6_width #(.IDW(4), .STD(1), .OSL(8), .MaxReads(16)) dut (
    .aclk(clk),
    .aresetn(rstn),
    .slv_axi_awid(s_req.aw.id),
    .slv_axi_awaddr(s_req.aw.addr),
    .slv_axi_awregion(s_req.aw.region),
    .slv_axi_awlen(s_req.aw.len),
    .slv_axi_awsize(s_req.aw.size),
    .slv_axi_awburst(s_req.aw.burst),
    .slv_axi_awlock(s_req.aw.lock),
    .slv_axi_awcache(s_req.aw.cache),
    .slv_axi_awprot(s_req.aw.prot),
    .slv_axi_awqos(s_req.aw.qos),
    .slv_axi_awvalid(s_req.aw_valid),
    .slv_axi_awready(s_resp.aw_ready),
    .slv_axi_wdata(s_req.w.data),
    .slv_axi_wstrb(s_req.w.strb),
    .slv_axi_wlast(s_req.w.last),
    .slv_axi_wvalid(s_req.w_valid),
    .slv_axi_wready(s_resp.w_ready),
    .slv_axi_bid(s_resp.b.id),
    .slv_axi_bresp(s_resp.b.resp),
    .slv_axi_bvalid(s_resp.b_valid),
    .slv_axi_bready(s_req.b_ready),
    .slv_axi_arid(s_req.ar.id),
    .slv_axi_araddr(s_req.ar.addr),
    .slv_axi_arregion(s_req.ar.region),
    .slv_axi_arlen(s_req.ar.len),
    .slv_axi_arsize(s_req.ar.size),
    .slv_axi_arburst(s_req.ar.burst),
    .slv_axi_arlock(s_req.ar.lock),
    .slv_axi_arcache(s_req.ar.cache),
    .slv_axi_arprot(s_req.ar.prot),
    .slv_axi_arqos(s_req.ar.qos),
    .slv_axi_arvalid(s_req.ar_valid),
    .slv_axi_arready(s_resp.ar_ready),
    .slv_axi_rid(s_resp.r.id),
    .slv_axi_rdata(s_resp.r.data),
    .slv_axi_rresp(s_resp.r.resp),
    .slv_axi_rlast(s_resp.r.last),
    .slv_axi_rvalid(s_resp.r_valid),
    .slv_axi_rready(s_req.r_ready),
    .mst_axi_awid(m_req.aw.id),
    .mst_axi_awaddr(m_req.aw.addr),
    .mst_axi_awregion(m_req.aw.region),
    .mst_axi_awlen(m_req.aw.len),
    .mst_axi_awsize(m_req.aw.size),
    .mst_axi_awburst(m_req.aw.burst),
    .mst_axi_awlock(m_req.aw.lock),
    .mst_axi_awcache(m_req.aw.cache),
    .mst_axi_awprot(m_req.aw.prot),
    .mst_axi_awqos(m_req.aw.qos),
    .mst_axi_awvalid(m_req.aw_valid),
    .mst_axi_awready(m_resp.aw_ready),
    .mst_axi_wdata(m_req.w.data),
    .mst_axi_wstrb(m_req.w.strb),
    .mst_axi_wlast(m_req.w.last),
    .mst_axi_wvalid(m_req.w_valid),
    .mst_axi_wready(m_resp.w_ready),
    .mst_axi_bid(m_resp.b.id),
    .mst_axi_bresp(m_resp.b.resp),
    .mst_axi_bvalid(m_resp.b_valid),
    .mst_axi_bready(m_req.b_ready),
    .mst_axi_arid(m_req.ar.id),
    .mst_axi_araddr(m_req.ar.addr),
    .mst_axi_arregion(m_req.ar.region),
    .mst_axi_arlen(m_req.ar.len),
    .mst_axi_arsize(m_req.ar.size),
    .mst_axi_arburst(m_req.ar.burst),
    .mst_axi_arlock(m_req.ar.lock),
    .mst_axi_arcache(m_req.ar.cache),
    .mst_axi_arprot(m_req.ar.prot),
    .mst_axi_arqos(m_req.ar.qos),
    .mst_axi_arvalid(m_req.ar_valid),
    .mst_axi_arready(m_resp.ar_ready),
    .mst_axi_rid(m_resp.r.id),
    .mst_axi_rdata(m_resp.r.data),
    .mst_axi_rresp(m_resp.r.resp),
    .mst_axi_rlast(m_resp.r.last),
    .mst_axi_rvalid(m_resp.r_valid),
    .mst_axi_rready(m_req.r_ready)
  );
  typedef struct packed {
    logic [63:0] addr;
    logic [7:0] len;
    logic [2:0] size;
    logic [1:0] burst;
  } desc_t;
  desc_t exp_q[16][$], wide_q[16][$];
  int rbeat[16], wbeat[16];
  logic [1:0] expected_resp[16];
  int wide_accepted=0;
  int accepted=0, received=0, pending=0, max_pending=0, ticks=0;
  bit stall=0;
  byte unsigned memory[65536], expected_memory[65536];
  desc_t write_q[$], swrite_q[$];
  int wrbeat=0, swrbeat=0, writes=0, bresponses=0;
  logic [3:0] write_ids[$];
  function automatic int byte_address(desc_t d, int beat);
    return int'(d.addr) + beat*(1 << d.size);
  endfunction
  always @(negedge clk) begin
    if(rstn) begin
      ticks=ticks+1;
      s_req.r_ready = !stall || ticks%5!=0;
      s_req.b_ready = ticks%4!=0;
      m_resp.ar_ready = !stall || ticks%3!=0;
      m_resp.aw_ready = ticks%3!=1;
      m_resp.w_ready = write_q.size()!=0 && ticks%4!=2;
    end
  end
  always @(posedge clk) if(rstn) begin
    if(s_req.ar_valid && s_resp.ar_ready) begin
      exp_q[s_req.ar.id].push_back('{s_req.ar.addr,s_req.ar.len,s_req.ar.size,s_req.ar.burst});
      accepted++;
    end
    if(m_req.ar_valid && m_resp.ar_ready) begin
      if(m_req.ar.burst!=1) $fatal(1,"WRAP was not normalized before converter");
      wide_q[m_req.ar.id].push_back('{m_req.ar.addr,m_req.ar.len,m_req.ar.size,m_req.ar.burst});
      pending++;wide_accepted++;
      if(pending>max_pending) max_pending=pending;
    end
    if(m_resp.r_valid && m_req.r_ready && m_resp.r.last) pending--;
    if(s_resp.r_valid && s_req.r_ready) begin
      automatic int id=int'(s_resp.r.id);
      automatic desc_t d;
      automatic int addr;
      if(exp_q[id].size()==0) $fatal(1,"unsolicited RID=%0d",id);
      d=exp_q[id][0]; addr=byte_address(d,rbeat[id]);
      if(s_resp.r.resp!=expected_resp[id] || s_resp.r.last!=(rbeat[id]==d.len))
        $fatal(1,"RID=%0d bad RESP/LAST beat=%0d",id,rbeat[id]);
      if(expected_resp[id]==0) for(int k=0;k<(1<<d.size);k++)
        if(s_resp.r.data[8*((addr+k)%8)+:8]!==expected_memory[(addr+k)&65535])
          $fatal(1,"RID=%0d beat=%0d byte=%h actual=%h expected=%h",id,rbeat[id],addr+k,
            s_resp.r.data[8*((addr+k)%8)+:8],expected_memory[(addr+k)&65535]);
      rbeat[id]++;
      if(s_resp.r.last) begin d=exp_q[id].pop_front();rbeat[id]=0;received++;end
    end
    if(s_req.aw_valid && s_resp.aw_ready)
      swrite_q.push_back('{s_req.aw.addr,s_req.aw.len,s_req.aw.size,s_req.aw.burst});
    if(s_req.w_valid && s_resp.w_ready) begin
      automatic int addr;
      if(swrite_q.size()==0) $fatal(1,"test expected AW before input W");
      addr=byte_address(swrite_q[0],swrbeat)&~7;
      for(int k=0;k<8;k++) if(s_req.w.strb[k]) expected_memory[(addr+k)&65535]=s_req.w.data[8*k+:8];
      swrbeat++;
      if(s_req.w.last) begin automatic desc_t d=swrite_q.pop_front();swrbeat=0;end
    end
    if(m_req.aw_valid && m_resp.aw_ready) begin
      write_q.push_back('{m_req.aw.addr,m_req.aw.len,m_req.aw.size,m_req.aw.burst});
      write_ids.push_back(m_req.aw.id);
    end
    if(m_req.w_valid && m_resp.w_ready) begin
      automatic int addr=byte_address(write_q[0],wrbeat)&~63;
      if(m_req.w.last!=(wrbeat==write_q[0].len)) $fatal(1,"bad WLAST");
      for(int k=0;k<64;k++) if(m_req.w.strb[k]) memory[(addr+k)&65535]=m_req.w.data[8*k+:8];
      wrbeat++;
      if(m_req.w.last) begin automatic desc_t d=write_q.pop_front();wrbeat=0;writes++;end
    end
    if(s_resp.b_valid && s_req.b_ready) begin
      if(s_resp.b.resp!=0 || s_resp.b.id!=4'd7) $fatal(1,"bad B response");
      bresponses++;
    end
  end
  task automatic ar(input int id,input logic[63:0] addr,input int len=0,
                    input int size=3,input bit wrap=0,input bit modifiable=0,
                    input bit rejected=0);
    @(negedge clk); #1;
    expected_resp[id]=rejected?2:0;
    s_req.ar='0;s_req.ar.id=4'(id);s_req.ar.addr=addr;s_req.ar.len=8'(len);
    s_req.ar.size=3'(size);s_req.ar.burst=wrap?2:1;s_req.ar.cache=modifiable?4'b0010:0;
    s_req.ar_valid=1;
    do @(posedge clk); while(!s_resp.ar_ready);
    @(negedge clk);#1;s_req.ar_valid=0;
  endtask
  task automatic return_beat(input int id, input logic [1:0] resp=0);
    desc_t d;
    int addr;
    wait(wide_q[id].size()!=0);
    d=wide_q[id][0];addr=byte_address(d,wbeat[id]);
    @(negedge clk);#1;
    m_resp.r='0;m_resp.r.resp=resp;m_resp.r.id=4'(id);m_resp.r.last=(wbeat[id]==d.len);
    for(int k=0;k<64;k++) m_resp.r.data[8*k+:8]=memory[((addr&~63)+k)&65535];
    m_resp.r_valid=1;
    do @(posedge clk); while(!m_req.r_ready);
    wbeat[id]++;
    if(m_resp.r.last) begin d=wide_q[id].pop_front();wbeat[id]=0;end
    @(negedge clk);#1;m_resp.r_valid=0;
  endtask
  task automatic drain_reads;
    wait(received==accepted);
    repeat(4) @(posedge clk);
  endtask
  task automatic write_sparse;
    @(negedge clk);#1;
    s_req.aw='0;s_req.aw.id=7;s_req.aw.addr=64'h80004018;s_req.aw.len=3;
    s_req.aw.size=3;s_req.aw.burst=1;s_req.aw_valid=1;
    do @(posedge clk);while(!s_resp.aw_ready);
    @(negedge clk);#1;s_req.aw_valid=0;
    for(int b=0;b<4;b++) begin
      s_req.w.data=64'h123456789abc0000+64'(b);
      s_req.w.strb=b==2?0:8'had;s_req.w.last=b==3;s_req.w_valid=1;
      do @(posedge clk);while(!s_resp.w_ready);
      @(negedge clk);#1;s_req.w_valid=0;
    end
    wait(writes==1);
    @(negedge clk);#1;m_resp.b.id=write_ids.pop_front();m_resp.b.resp=0;m_resp.b_valid=1;
    do @(posedge clk);while(!m_req.b_ready);
    @(negedge clk);#1;m_resp.b_valid=0;
    wait(bresponses==1);
    for(int k=0;k<65536;k++) if(memory[k]!==expected_memory[k]) $fatal(1,"write byte mismatch %h",k);
    $display("E6_WIDTH_CASE_PASS sparse_zero_strobe_write");
  endtask
  initial begin
    s_req='0;m_resp='0;
    for(int k=0;k<65536;k++) begin memory[k]=8'((k*37)^(k>>8));expected_memory[k]=memory[k];end
    for(int id=0;id<16;id++) begin rbeat[id]=0;wbeat[id]=0;expected_resp[id]=0;end
    repeat(4) @(negedge clk);rstn=1;
    ar(1,64'h80001008);ar(2,64'h80002028);
    return_beat(2);return_beat(1);drain_reads();
    $display("E6_WIDTH_CASE_PASS reversed_ids");
    stall=1;
    ar(1,64'h80001008,1);ar(2,64'h80002028,1);
    return_beat(1);return_beat(2);return_beat(1);return_beat(2);drain_reads();
    $display("E6_WIDTH_CASE_PASS interleaved_stalled");
    for(int id=0;id<16;id++) ar(id,64'h80003000+64'(id*72));
    wait(pending==16);
    for(int id=15;id>=0;id--) return_beat(id);
    drain_reads();if(max_pending<16) $fatal(1,"did not reach 16 outstanding");
    $display("E6_WIDTH_CASE_PASS outstanding_16 max_pending=%0d",max_pending);
    ar(3,64'h80001038,2);ar(3,64'h80002010,0);
    repeat(12) @(posedge clk);
    if(wide_q[3].size()!=1) $fatal(1,"same-ID context overwritten");
    repeat(3) return_beat(3);return_beat(3);drain_reads();
    $display("E6_WIDTH_CASE_PASS same_id_queued_boundary");
    ar(4,64'h80005000,7,3,1);
    repeat(8) return_beat(4);drain_reads();
    $display("E6_WIDTH_CASE_PASS aligned_wrap");
    for(int sz=0;sz<4;sz++) begin ar(5,64'h80006118,3,sz);repeat(4) return_beat(5);drain_reads();end
    $display("E6_WIDTH_CASE_PASS subword_reads");
    ar(6,64'h80007000,15,3,0,1);
    wait(wide_q[6].size()!=0);
    begin automatic int n=int'(wide_q[6][0].len)+1;repeat(n) return_beat(6);end
    drain_reads();$display("E6_WIDTH_CASE_PASS modifiable_burst");
    begin
      automatic int before_count=wide_accepted;
      ar(9,64'h80002008,3,3,1,0,1);drain_reads();
      if(wide_accepted!=before_count) $fatal(1,"rejected WRAP reached downstream");
    end
    $display("E6_WIDTH_CASE_PASS unsupported_wrap_error");
    ar(8,64'h80001008);expected_resp[8]=2;
    return_beat(8,2);drain_reads();
    $display("E6_WIDTH_CASE_PASS downstream_error");
    write_sparse();
    ar(1,64'h80001008);ar(2,64'h80002028);wait(pending==2);
    @(negedge clk);#1;rstn=0;s_req='0;m_resp='0;
    for(int id=0;id<16;id++) begin
      exp_q[id].delete();wide_q[id].delete();rbeat[id]=0;wbeat[id]=0;expected_resp[id]=0;
    end
    pending=0;accepted=received;
    repeat(4) @(negedge clk);#1;rstn=1;
    ar(1,64'h80009030);return_beat(1);drain_reads();
    $display("E6_WIDTH_CASE_PASS reset_with_pending_id_reuse");
    $display("E6_WIDTH_PASS transactions=%0d max_pending=%0d",received,max_pending);
    $finish;
  end
  initial begin #300000; $fatal(1,"E6 width test stalled");end
endmodule
