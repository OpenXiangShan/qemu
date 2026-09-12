// Copyright (C) 2024, Andes Technology Corp. Confidential Proprietary
// AX45MPV_PREMIUM v30.1.0



module exmon300_bin2size (
        	  out,
        	  in
);
parameter N = 8;
localparam W = $unsigned($clog2(N));

output   [N-1:0] out;
input    [W-1:0] in;





// ee947d48 rnd


assign out = {{(N-1){1'b0}}, {1'b1}} << in;






endmodule


