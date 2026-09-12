module TcmSram #(
  parameter Depth = 2048,
  parameter Width = 39,
  parameter setIdx = $clog2(Depth)
)(
    input wire clock,
    input wire reset_ni,
    input wire cen,
    input wire wen,
    input wire [setIdx-1:0] addr,
    input wire [Width-1:0] data,
    output wire [Width-1:0] rdata
);

  reg [39-1:0] ram [Depth-1:0];
  reg [39-1:0] rdata_q;
  always @(posedge clock)begin
      if(cen) begin
          if(wen)
              ram[addr] <= data;
      end 
  end

  always @(posedge clock or negedge reset_ni)begin
      if(!reset_ni)begin
          rdata_q <= 'b0;
      end else begin
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
  end
  assign rdata = rdata_q;

endmodule