// SPDX-License-Identifier: Apache-2.0
// Byte-addressed scoreboard independent of AXI width-converter implementation.
`timescale 1ns/1ps
module width_bfm #(
    parameter int S_BYTES=64, M_BYTES=8, ID_BITS=8,
    parameter bit UP=0,
    parameter type s_req_t=logic, s_resp_t=logic,
    parameter type m_req_t=logic, m_resp_t=logic
)(input logic clk, output logic rstn, output s_req_t s_req,
  input s_resp_t s_resp, input m_req_t m_req, output m_resp_t m_resp,
  output bit done);
    localparam longint unsigned BASE = UP ? 'h50000000 : 'h80000000;
    localparam int MEM_BYTES=65536, MAX_SIZE=$clog2(S_BYTES);
    typedef struct packed {
        longint unsigned addr;
        int unsigned id, size, count, beat;
        bit error;
    } burst_t;
    burst_t awq[$], arq[$], expect_r[$], expect_b[$];
    int unsigned bq[$];
    bit berror[$];
    byte unsigned memory[MEM_BYTES], reference[MEM_BYTES];
    int unsigned rng=1, cycles=0, writes=0, reads=0, memory_beats=0;
    int unsigned max_read_pending=0, id_counter=0;
    int active_read=-1;
    bit pause_memory=0, pause_reads=0, random_stalls=1, hold_responses=0;
    bit stalled_s_r=0, stalled_s_b=0, stalled_m_aw=0, stalled_m_ar=0, stalled_m_w=0;
    s_resp_t previous_s;
    m_req_t previous_m;

    function automatic int unsigned rand32();
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return rng;
    endfunction
    function automatic bit ready_random();
        return !random_stalls || ((rand32() & 3) != 0);
    endfunction
    function automatic longint unsigned byte_start(input burst_t b);
        longint unsigned step;
        step=64'd1 << b.size;
        return b.beat==0 ? b.addr : (b.addr / step + b.beat) * step;
    endfunction
    function automatic longint unsigned byte_end(input burst_t b);
        longint unsigned step;
        step=64'd1 << b.size;
        return (b.addr / step + b.beat + 1) * step;
    endfunction
    function automatic bit memory_error(input longint unsigned addr);
        return addr < BASE || addr >= BASE+MEM_BYTES ||
               (addr >= BASE+'he000 && addr < BASE+'hf000);
    endfunction
    function automatic burst_t descriptor(input longint unsigned addr,
        input int size, count, id, input bit error=0);
        burst_t b;
        b='0; b.addr=addr; b.size=size; b.count=count; b.id=id; b.error=error;
        return b;
    endfunction
    function automatic int new_id();
        id_counter++;
        return ((id_counter*37) ^ (UP ? 'ha5a0 : 'ha0)) & ((1<<ID_BITS)-1);
    endfunction

    // Memory model: accepts multiple addresses, keeps same-ID reads ordered,
    // may select another ID between beats, and independently stalls channels.
    always @(negedge clk) begin
        if (!rstn) begin
            m_resp='0;
        end else begin
            m_resp.aw_ready=!pause_memory && awq.size()<16 && bq.size()<16 && ready_random();
            m_resp.ar_ready=!pause_memory && arq.size()<16 && ready_random();
            m_resp.w_ready=!pause_memory && awq.size()!=0 && bq.size()<16 && ready_random();
            if (!m_resp.b_valid && bq.size()!=0 && ready_random()) begin
                m_resp.b='0; m_resp.b.id=bq[0];
                m_resp.b.resp=berror[0] ? 2'b10 : 2'b00;
                m_resp.b_valid=1;
            end
            if (!pause_reads && !m_resp.r_valid && arq.size()!=0 && ready_random()) begin
                automatic burst_t b;
                active_read=rand32()%arq.size();
                // Never bypass an earlier request with the same AXI ID.
                for (int j=0;j<active_read;j++) begin
                    if(arq[j].id==arq[active_read].id) active_read=j;
                end
                b=arq[active_read];
                m_resp.r='0; m_resp.r.id=b.id;
                m_resp.r.resp=memory_error(byte_start(b)) ? 2'b10 : 2'b00;
                m_resp.r.last=b.beat+1==b.count;
                if(m_resp.r.resp==0) begin
                    for(longint unsigned a=byte_start(b);a<byte_end(b);a++)
                        m_resp.r.data[8*(a%M_BYTES)+:8]=memory[a-BASE];
                end
                m_resp.r_valid=1;
            end
        end
    end

    always @(posedge clk) begin
        if (!rstn) begin
            awq.delete(); arq.delete(); bq.delete(); berror.delete();
            expect_r.delete(); expect_b.delete(); active_read=-1;
            stalled_s_r=0; stalled_s_b=0;
            stalled_m_aw=0; stalled_m_ar=0; stalled_m_w=0;
        end else begin
            cycles++;
            if(cycles>2000000) $fatal(1,"WIDTH_TIMEOUT UP=%0d pending R=%0d B=%0d",UP,expect_r.size(),expect_b.size());
            if($isunknown({s_resp.aw_ready,s_resp.w_ready,s_resp.ar_ready,s_resp.r_valid,s_resp.b_valid,
                           m_req.aw_valid,m_req.w_valid,m_req.ar_valid,m_req.r_ready,m_req.b_ready}))
                $fatal(1,"WIDTH_X_CONTROL UP=%0d",UP);
            if(stalled_s_r && (!s_resp.r_valid || s_resp.r!==previous_s.r))
                $fatal(1,"WIDTH_R_UNSTABLE UP=%0d",UP);
            if(stalled_s_b && (!s_resp.b_valid || s_resp.b!==previous_s.b))
                $fatal(1,"WIDTH_B_UNSTABLE UP=%0d",UP);
            if(stalled_m_aw && (!m_req.aw_valid || m_req.aw!==previous_m.aw))
                $fatal(1,"WIDTH_AW_UNSTABLE UP=%0d",UP);
            if(stalled_m_ar && (!m_req.ar_valid || m_req.ar!==previous_m.ar))
                $fatal(1,"WIDTH_AR_UNSTABLE UP=%0d",UP);
            if(stalled_m_w && (!m_req.w_valid || m_req.w!==previous_m.w))
                $fatal(1,"WIDTH_W_UNSTABLE UP=%0d",UP);
            stalled_s_r=s_resp.r_valid&&!s_req.r_ready; previous_s=s_resp;
            stalled_s_b=s_resp.b_valid&&!s_req.b_ready;
            stalled_m_aw=m_req.aw_valid&&!m_resp.aw_ready;
            stalled_m_ar=m_req.ar_valid&&!m_resp.ar_ready;
            stalled_m_w=m_req.w_valid&&!m_resp.w_ready; previous_m=m_req;

            if(s_resp.r_valid && s_req.r_ready) begin
                automatic int index=-1;
                automatic burst_t b;
                for(int j=0;j<expect_r.size();j++)
                    if(index<0 && expect_r[j].id==s_resp.r.id) index=j;
                if(index<0) $fatal(1,"WIDTH_R_UNKNOWN_ID UP=%0d ID=%0h",UP,s_resp.r.id);
                b=expect_r[index];
                if(s_resp.r.resp!==(b.error ? 2'b10 : 2'b00) || s_resp.r.last!==(b.beat+1==b.count))
                    $fatal(1,"WIDTH_R_STATUS UP=%0d ID=%0h beat=%0d/%0d resp=%0d last=%0d",UP,b.id,b.beat,b.count,s_resp.r.resp,s_resp.r.last);
                if(!b.error) begin
                    for(longint unsigned a=byte_start(b);a<byte_end(b);a++)
                        if(s_resp.r.data[8*(a%S_BYTES)+:8]!==reference[a-BASE])
                            $fatal(1,"WIDTH_R_DATA UP=%0d addr=%0h beat=%0d got=%0h expected=%0h",UP,a,b.beat,s_resp.r.data[8*(a%S_BYTES)+:8],reference[a-BASE]);
                end
                if(b.beat+1==b.count) begin expect_r.delete(index); reads++; end
                else begin b.beat++; expect_r[index]=b; end
            end
            if(s_resp.b_valid && s_req.b_ready) begin
                automatic int index=-1;
                for(int j=0;j<expect_b.size();j++)
                    if(index<0 && expect_b[j].id==s_resp.b.id) index=j;
                if(index<0) $fatal(1,"WIDTH_B_UNKNOWN_ID UP=%0d ID=%0h",UP,s_resp.b.id);
                if(s_resp.b.resp!==(expect_b[index].error ? 2'b10 : 2'b00))
                    $fatal(1,"WIDTH_B_STATUS UP=%0d ID=%0h resp=%0d",UP,s_resp.b.id,s_resp.b.resp);
                expect_b.delete(index); writes++;
            end
            if(m_req.aw_valid && m_resp.aw_ready) begin
                if(m_req.aw.size>$clog2(M_BYTES) || m_req.aw.burst!=1 ||
                   (m_req.aw.addr/4096)!=((m_req.aw.addr/(1<<m_req.aw.size)*(1<<m_req.aw.size)+(int'(m_req.aw.len)+1)*(1<<m_req.aw.size)-1)/4096))
                    $fatal(1,"WIDTH_BAD_DOWNSTREAM_AW UP=%0d",UP);
                awq.push_back(descriptor(m_req.aw.addr,m_req.aw.size,int'(m_req.aw.len)+1,m_req.aw.id));
            end
            if(m_req.ar_valid && m_resp.ar_ready) begin
                if(m_req.ar.size>$clog2(M_BYTES) || m_req.ar.burst!=1)
                    $fatal(1,"WIDTH_BAD_DOWNSTREAM_AR UP=%0d",UP);
                arq.push_back(descriptor(m_req.ar.addr,m_req.ar.size,int'(m_req.ar.len)+1,m_req.ar.id));
                if(arq.size()>max_read_pending) max_read_pending=arq.size();
            end
            if(m_req.w_valid && m_resp.w_ready) begin
                automatic burst_t b=awq[0];
                if(m_req.w.last!==(b.beat+1==b.count)) $fatal(1,"WIDTH_DOWNSTREAM_WLAST UP=%0d",UP);
                for(int lane=0;lane<M_BYTES;lane++) begin
                    automatic longint unsigned a=byte_start(b)/M_BYTES*M_BYTES+lane;
                    if(m_req.w.strb[lane]) begin
                        if(a<byte_start(b) || a>=byte_end(b)) $fatal(1,"WIDTH_BAD_STROBE UP=%0d addr=%0h",UP,a);
                        if(!memory_error(a)) memory[a-BASE]=m_req.w.data[8*lane+:8];
                    end
                end
                b.error |= memory_error(byte_start(b)); memory_beats++;
                if(b.beat+1==b.count) begin
                    bq.push_back(b.id); berror.push_back(b.error); awq.delete(0);
                end else begin b.beat++; awq[0]=b; end
            end
            if(m_resp.r_valid && m_req.r_ready) begin
                automatic burst_t b=arq[active_read];
                memory_beats++;
                if(b.beat+1==b.count) arq.delete(active_read);
                else begin b.beat++; arq[active_read]=b; end
                m_resp.r_valid<=0;
            end
            if(m_resp.b_valid && m_req.b_ready) begin
                bq.delete(0); berror.delete(0); m_resp.b_valid<=0;
            end
        end
    end

    // READY is changed only at falling edges and independently of VALID.
    always @(negedge clk) if(rstn) begin
        s_req.r_ready=!hold_responses && ready_random(); s_req.b_ready=!hold_responses && ready_random();
    end

    task automatic reset_model();
        @(negedge clk); rstn=0; s_req='0; pause_memory=0; pause_reads=0; hold_responses=0;
        repeat(8) @(posedge clk);
        @(negedge clk); rstn=1;
        repeat(4) @(posedge clk);
    endtask
    task automatic idle(input int n);
        repeat(n) @(posedge clk);
    endtask
    task automatic check_memory();
        for(int j=0;j<MEM_BYTES;j++)
            if(memory[j]!==reference[j])
                $fatal(1,"WIDTH_RAM_MISMATCH UP=%0d addr=%0h got=%0h expected=%0h",UP,BASE+j,memory[j],reference[j]);
    endtask
    task automatic drain();
        while(expect_r.size()!=0 || expect_b.size()!=0 || awq.size()!=0 || arq.size()!=0 || bq.size()!=0)
            @(negedge clk);
        idle(8); check_memory();
    endtask
    task automatic send_aw(input burst_t b, input int burst=1, lock=0, atop=0);
        repeat(rand32()%4) @(posedge clk);
        @(negedge clk);
        s_req.aw='0; s_req.aw.addr=b.addr; s_req.aw.len=b.count-1;
        s_req.aw.size=b.size; s_req.aw.id=b.id; s_req.aw.burst=burst;
        s_req.aw.lock=lock; s_req.aw.atop=atop;
        // RA DDR uses modifiable cached AXI accesses; config uses MMIO attrs.
        s_req.aw.cache=UP ? 0 : 4'b0010;
        s_req.aw_valid=1;
        do @(posedge clk); while(!s_resp.aw_ready);
        @(negedge clk); s_req.aw_valid=0;
    endtask
    task automatic send_w(input burst_t descriptor_in, input int mask_mode=0);
        burst_t b=descriptor_in;
        for(int beat=0;beat<b.count;beat++) begin
            b.beat=beat;
            repeat(rand32()%3) @(posedge clk);
            @(negedge clk); s_req.w='0;
            if(b.size<=MAX_SIZE) begin
                for(longint unsigned a=byte_start(b);a<byte_end(b);a++) begin
                    automatic int lane=a%S_BYTES;
                    s_req.w.data[8*lane+:8]=byte'(a^(a>>8)^b.id^(beat*13));
                    s_req.w.strb[lane]=mask_mode==1 ? 0 : (mask_mode==2 ? 1 : ready_random());
                    if(!b.error && s_req.w.strb[lane]) reference[a-BASE]=s_req.w.data[8*lane+:8];
                end
            end
            s_req.w.last=beat+1==b.count; s_req.w_valid=1;
            do @(posedge clk); while(!s_resp.w_ready);
            @(negedge clk); s_req.w_valid=0;
        end
    endtask
    task automatic write_burst(input longint unsigned addr, input int size, count,
        input int order=0, mask_mode=0, input bit error=0,
        input int burst=1, lock=0, atop=0, forced_id=-1);
        burst_t b=descriptor(addr,size,count,forced_id<0?new_id():forced_id,error);
        expect_b.push_back(b);
        fork
            begin if(order==1) idle(12); send_aw(b,burst,lock,atop); end
            begin if(order==2) idle(12); send_w(b,mask_mode); end
        join
    endtask
    task automatic read_burst(input longint unsigned addr, input int size, count,
        input bit error=0, input int burst=1, lock=0, forced_id=-1);
        burst_t b=descriptor(addr,size,count,forced_id<0?new_id():forced_id,error);
        expect_r.push_back(b);
        repeat(rand32()%3) @(posedge clk);
        @(negedge clk); s_req.ar='0;
        s_req.ar.addr=addr; s_req.ar.size=size; s_req.ar.len=count-1;
        s_req.ar.id=b.id; s_req.ar.burst=burst; s_req.ar.lock=lock;
        s_req.ar.cache=UP ? 0 : 4'b0010;
        s_req.ar_valid=1;
        do @(posedge clk); while(!s_resp.ar_ready);
        @(negedge clk); s_req.ar_valid=0;
    endtask

    initial begin : scenario
        int seed, ignored;
        seed=4660; ignored=$value$plusargs("seed=%d",seed);
        rng=seed^(UP?'h12345678:'h87654321);
        done=0; rstn=0; s_req='0; m_resp='0;
        for(int j=0;j<MEM_BYTES;j++) begin memory[j]=byte'(j^(j>>8)^'h5a); reference[j]=memory[j]; end
        reset_model();
        for(int size=0;size<=MAX_SIZE;size++) begin
            for(int lane=0;lane<64;lane++) begin
                write_burst(BASE+'h1000+lane,size,1+lane%7,lane%3,lane%3);
                drain();
                read_burst(BASE+'h1000+lane,size,1+lane%7);
                drain();
            end
        end
        // 128 B, 2 KiB, legal maximum LEN255 and full-page 4 KiB transfer.
        write_burst(BASE+'h2000,MAX_SIZE,128/S_BYTES,1,2); drain();
        read_burst(BASE+'h2000,MAX_SIZE,128/S_BYTES); drain();
        write_burst(BASE+'h3000,MAX_SIZE,2048/S_BYTES,2); drain();
        read_burst(BASE+'h3000,MAX_SIZE,2048/S_BYTES); drain();
        write_burst(BASE+'h4000,3,256); drain(); read_burst(BASE+'h4000,3,256); drain();
        if(!UP) begin
            write_burst(BASE+'h5000,6,64,1); drain(); read_burst(BASE+'h5000,6,64); drain();
            write_burst(BASE+'h6003,6,32); drain(); read_burst(BASE+'h6003,6,32); drain();
        end else begin
            write_burst(BASE+'h5000,3,256); write_burst(BASE+'h5800,3,256);
            drain(); read_burst(BASE+'h5000,3,256); read_burst(BASE+'h5800,3,256); drain();
        end
        // Fill every configured conversion context before permitting replies.
        pause_reads=1;
        for(int j=0;j<(UP?4:16);j++)
            read_burst(BASE+'h7000+j*128,MAX_SIZE,8,0,1,0,j);
        idle(32);
        if(arq.size()!=(UP?4:16))
            $fatal(1,"WIDTH_READ_CAPACITY UP=%0d count=%0d",UP,arq.size());
        pause_reads=0; drain();
        // Multiple active IDs and same-ID ordering, with concurrent directions.
        fork
            begin
                for(int j=0;j<8;j++) read_burst(BASE+'h7000+j*256,MAX_SIZE,4,0,1,0,j%4+1);
            end
            begin
                for(int j=0;j<8;j++) write_burst(BASE+'h9000+j*256,MAX_SIZE,4,j%3,0,0,1,0,0,j%4+1);
            end
        join
        drain();
        // E6 cache-line reads: boundary-start WRAP has INCR byte order.
        // Cover every legal WRAP length and each supported beat width.
        if (!UP) begin
            for (int size=0; size<=MAX_SIZE; size++) begin
                for (int count=2; count<=16; count*=2) begin
                    read_burst(BASE+'ha000,size,count,0,2); drain();
                end
            end
            // Preserve ordering when WRAP and INCR share an ID and while
            // unrelated IDs can be serviced under randomized backpressure.
            read_burst(BASE+'ha020,3,4,0,2,0,7);
            read_burst(BASE+'ha040,3,4,0,1,0,7);
            read_burst(BASE+'ha080,3,4,0,2,0,8);
            drain();
        end
        // Guard rejects unsupported burst/size/lock/ATOP and crossing page.
        begin
            automatic int old_beats=memory_beats;
            for(int burst=0;burst<4;burst++) if(burst!=1) begin
                write_burst(BASE+'ha000,3,4,0,0,1,burst); drain();
                read_burst(BASE+'ha000+((!UP && burst==2)?8:0),3,4,1,burst); drain();
            end
            // Malformed WRAP lengths, size misalignment, and exclusives
            // remain rejected even on the DDR path.
            read_burst(BASE+'ha000,3,1,1,2); drain();
            read_burst(BASE+'ha000,3,3,1,2); drain();
            read_burst(BASE+'ha001,3,4,1,2); drain();
            read_burst(BASE+'ha000,3,4,1,2,1); drain();
            write_burst(BASE+'haff8,3,2,1,0,1); drain();
            read_burst(BASE+'haff8,3,2,1); drain();
            write_burst(BASE+'hb000,MAX_SIZE+1,2,0,0,1); drain();
            read_burst(BASE+'hb000,MAX_SIZE+1,2,1); drain();
            write_burst(BASE+'hb000,3,1,0,0,1,1,1); drain();
            read_burst(BASE+'hb000,3,1,1,1,1); drain();
            write_burst(64'hfffffffffffffffc,3,2,0,0,1); drain();
            read_burst(64'hfffffffffffffffc,3,2,1); drain();
            if(old_beats!=memory_beats) $fatal(1,"WIDTH_REJECTED_SIDE_EFFECT UP=%0d",UP);
        end
        write_burst(BASE+MEM_BYTES,3,8,0,0,1); drain();
        read_burst(BASE+MEM_BYTES,3,8,1); drain();
        write_burst(BASE+'he000,MAX_SIZE,8,0,0,1); drain();
        read_burst(BASE+'he000,MAX_SIZE,8,1); drain();
        // Random transactions remain in one page and outside the fault page.
        for(int n=0;n<1000;n++) begin
            automatic int size=rand32()%(MAX_SIZE+1);
            automatic int max_count=4096/(1<<size);
            automatic int count=1+rand32()%(max_count<256?max_count:256);
            automatic int room=4096-count*(1<<size);
            automatic longint unsigned addr=BASE+'hc000+(room==0?0:rand32()%(room+1));
            // Round down to size boundary then optionally make first beat partial.
            addr=addr/(1<<size)*(1<<size)+(rand32()%(1<<size));
            write_burst(addr,size,count,n%3,n%5==0?1:0); drain();
            read_burst(addr,size,count); drain();
        end
        // Reset while an accepted read is blocked on downstream memory.
        pause_memory=1;
        read_burst(BASE+'hd000,3,1);
        idle(8); reset_model();
        idle(12);
        read_burst(BASE+'hd000,3,1); drain();
        // Reset a held response and an incomplete write with AW already accepted.
        hold_responses=1;
        read_burst(BASE+'hd020,3,1);
        wait(s_resp.r_valid); idle(4); reset_model();
        pause_memory=1;
        begin
            automatic burst_t b=descriptor(BASE+'hd040,3,4,new_id());
            expect_b.push_back(b); send_aw(b);
            @(negedge clk); s_req.w='0; s_req.w_valid=1;
            idle(8); reset_model();
        end
        write_burst(BASE+'hd040,3,4); drain(); read_burst(BASE+'hd040,3,4); drain();
        // Valid AtomicStore and AtomicLoad are rejected, with both B and R
        // supplied for a load-returning atomic. Neither may reach memory.
        begin
            automatic int old_beats=memory_beats;
            automatic int id=new_id();
            write_burst(BASE+'hb000,3,1,0,0,1,1,0,'h10); drain();
            expect_r.push_back(descriptor(BASE+'hb000,3,1,id,1));
            write_burst(BASE+'hb000,3,1,0,0,1,1,0,'h20,id); drain();
            if(old_beats!=memory_beats) $fatal(1,"WIDTH_ATOMIC_SIDE_EFFECT UP=%0d",UP);
        end
        $display("WIDTH_PATH_PASS up=%0d seed=%0d writes=%0d reads=%0d cycles=%0d memory_beats=%0d max_read_pending=%0d",UP,seed,writes,reads,cycles,memory_beats,max_read_pending);
        done=1;
    end
endmodule

module width_tb;
    import io_system_rtl_arena_pkg::*;
    logic clk=0;
    always #5 clk=~clk;
    logic [1:0] rstn;
    bit [1:0] done;
    a512i8_req_t s0, checked0;
    a512i8_resp_t sr0, checked_r0;
    a64i8_req_t m0;
    a64i8_resp_t mr0;
    width_bfm #(.S_BYTES(64),.M_BYTES(8),.ID_BITS(8),.UP(0),
        .s_req_t(a512i8_req_t),.s_resp_t(a512i8_resp_t),
        .m_req_t(a64i8_req_t),.m_resp_t(a64i8_resp_t)) bfm0 (
        .clk(clk),.rstn(rstn[0]),.s_req(s0),.s_resp(sr0),
        .m_req(m0),.m_resp(mr0),.done(done[0]));
    io_system_rtl_axi_guard #(.ID_WIDTH(8),.MAX_SIZE(6),.MAX_TRANS(16),
        .ALLOW_ALIGNED_READ_WRAP(1'b1),
        .CHECK_WINDOW(0),.BASE(64'h50000000),.SIZE(64'h10000),
        .aw_t(a512i8_aw_chan_t),.w_t(a512i8_w_chan_t),.b_t(a512i8_b_chan_t),
        .ar_t(a512i8_ar_chan_t),.r_t(a512i8_r_chan_t),
        .req_t(a512i8_req_t),.resp_t(a512i8_resp_t)) guard0 (
        .clk(clk),.rstn(rstn[0]),.req_i(s0),.resp_o(sr0),
        .req_o(checked0),.resp_i(checked_r0));
    axi_dw_converter #(.AxiMaxReads(16),.AxiSlvPortDataWidth(512),
        .AxiMstPortDataWidth(64),.AxiAddrWidth(64),.AxiIdWidth(8),
        .aw_chan_t(a512i8_aw_chan_t),.ar_chan_t(a512i8_ar_chan_t),.b_chan_t(a512i8_b_chan_t),
        .slv_w_chan_t(a512i8_w_chan_t),.mst_w_chan_t(a64i8_w_chan_t),
        .slv_r_chan_t(a512i8_r_chan_t),.mst_r_chan_t(a64i8_r_chan_t),
        .axi_slv_req_t(a512i8_req_t),.axi_slv_resp_t(a512i8_resp_t),
        .axi_mst_req_t(a64i8_req_t),.axi_mst_resp_t(a64i8_resp_t)) converter0 (
        .clk_i(clk),.rst_ni(rstn[0]),.slv_req_i(checked0),.slv_resp_o(checked_r0),
        .mst_req_o(m0),.mst_resp_i(mr0));
    a64i16_req_t s1, checked1;
    a64i16_resp_t sr1, checked_r1;
    a512i4_req_t m1;
    a512i4_resp_t mr1;
    width_bfm #(.S_BYTES(8),.M_BYTES(64),.ID_BITS(16),.UP(1),
        .s_req_t(a64i16_req_t),.s_resp_t(a64i16_resp_t),
        .m_req_t(a512i4_req_t),.m_resp_t(a512i4_resp_t)) bfm1 (
        .clk(clk),.rstn(rstn[1]),.s_req(s1),.s_resp(sr1),
        .m_req(m1),.m_resp(mr1),.done(done[1]));
    io_system_rtl_axi_guard #(.ID_WIDTH(16),.MAX_SIZE(3),.MAX_TRANS(4),
        .CHECK_WINDOW(1),.BASE(64'h50000000),.SIZE(64'h10000),
        .aw_t(a64i16_aw_chan_t),.w_t(a64i16_w_chan_t),.b_t(a64i16_b_chan_t),
        .ar_t(a64i16_ar_chan_t),.r_t(a64i16_r_chan_t),
        .req_t(a64i16_req_t),.resp_t(a64i16_resp_t)) guard1 (
        .clk(clk),.rstn(rstn[1]),.req_i(s1),.resp_o(sr1),
        .req_o(checked1),.resp_i(checked_r1));
    a64i4_req_t remap;
    a64i4_resp_t remap_r;
    axi_id_remap #(.AxiSlvPortIdWidth(16),.AxiSlvPortMaxUniqIds(4),
        .AxiMaxTxnsPerId(1),.AxiMstPortIdWidth(4),
        .slv_req_t(a64i16_req_t),.slv_resp_t(a64i16_resp_t),
        .mst_req_t(a64i4_req_t),.mst_resp_t(a64i4_resp_t)) ids (
        .clk_i(clk),.rst_ni(rstn[1]),.slv_req_i(checked1),.slv_resp_o(checked_r1),
        .mst_req_o(remap),.mst_resp_i(remap_r));
    axi_dw_converter #(.AxiMaxReads(4),.AxiSlvPortDataWidth(64),
        .AxiMstPortDataWidth(512),.AxiAddrWidth(64),.AxiIdWidth(4),
        .aw_chan_t(a64i4_aw_chan_t),.ar_chan_t(a64i4_ar_chan_t),.b_chan_t(a64i4_b_chan_t),
        .slv_w_chan_t(a64i4_w_chan_t),.mst_w_chan_t(a512i4_w_chan_t),
        .slv_r_chan_t(a64i4_r_chan_t),.mst_r_chan_t(a512i4_r_chan_t),
        .axi_slv_req_t(a64i4_req_t),.axi_slv_resp_t(a64i4_resp_t),
        .axi_mst_req_t(a512i4_req_t),.axi_mst_resp_t(a512i4_resp_t)) converter1 (
        .clk_i(clk),.rst_ni(rstn[1]),.slv_req_i(remap),.slv_resp_o(remap_r),
        .mst_req_o(m1),.mst_resp_i(mr1));
    initial begin
        wait(&done);
        $display("AXI_WIDTH_RTL_PASS");
        $finish;
    end
    initial begin
        #25000000;
        $fatal(1,"WIDTH_GLOBAL_TIMEOUT");
    end
endmodule
