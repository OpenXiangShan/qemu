// Copyright (C) 2024, Andes Technology Corp. Confidential Proprietary
// AX45MPV_PREMIUM v30.1.0


module exmon300_arb_fp (
        	  valid,
        	  ready,
        	  grant
);
parameter N = 8;
input		[N-1:0]	valid;
output		[N-1:0]	ready;
output		[N-1:0]	grant;





// 2d6200a8 rnd



assign	ready[0] = 1'b1;

generate
genvar i;
for (i=1; i<N; i=i+1) begin : gen_ready
        assign ready[i] = ~|valid[i-1:0];
end
endgenerate



assign grant = valid & ready;






endmodule

