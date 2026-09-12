// Copyright (C) 2024, Andes Technology Corp. Confidential Proprietary
// AX45MPV_PREMIUM v30.1.0



module exmon300_bin2mask (
        	  out,
        	  in
);
parameter N = 8;
localparam W = $unsigned($clog2(N));

output   [N-1:0] out;
input    [W-1:0] in;





// 0523a012 rnd

wire     [N-2:0] nds_unused_wire;

assign {nds_unused_wire, out} = {(N){1'b1}} << in;






endmodule


