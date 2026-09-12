module DataSram #(
  parameter nSets = 256,
  parameter encDataBits = 72,
  parameter setIdx = $clog2(nSets)
)(
    input wire clock,
    input wire cen,
    input wire wen,
    input wire [setIdx-1:0] addr,
    input wire [encDataBits-1:0] data,
    output wire [encDataBits-1:0] rdata
);

  reg [encDataBits-1:0] ram [nSets-1:0];
  reg [encDataBits-1:0] rdata_q;
  always @(posedge clock)begin
      if(cen) begin
          if(wen)
              ram[addr] <= data;
      end 
  end

  always @(posedge clock)begin
      if(cen)begin
          if(!wen)begin
              rdata_q <= ram[addr];
          end else begin
              rdata_q <= rdata_q;
          end
      end else begin
          rdata_q <= rdata_q;
      end
  end
  assign rdata = rdata_q;

endmodule