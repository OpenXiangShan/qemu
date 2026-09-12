module ram_model_bwe (
	  clk,
	  cs,
	  bwe,
	  addr,
	  din,
	  dout
);

parameter ADDR_WIDTH		= 5;
parameter DATA_BYTE		= 4;
parameter IN_DELAY		= 0;
parameter OUT_DELAY		= 0;
parameter [0:0]	HOLD_DOUT	= 1'b1;
parameter ENABLE		= "yes";
parameter BIT_PER_BYTE		= 8;

localparam DATA_WIDTH		= DATA_BYTE * BIT_PER_BYTE;
localparam ARRAY_SIZE		= 1 << ADDR_WIDTH;

input				clk;
input				cs;
input [(DATA_BYTE-1):0]		bwe;
input [(ADDR_WIDTH-1):0]	addr;
input [(DATA_WIDTH-1):0]	din;
output [(DATA_WIDTH-1):0]	dout;


// synthesis translate_off
reg [(DATA_WIDTH-1):0]		ram[0:(ARRAY_SIZE-1)];	/* sparse */
// synthesis translate_on
reg [(ADDR_WIDTH-1):0]		addr_d1;
reg [(DATA_WIDTH-1):0]		tmp;
reg				drive_dout;

always @(posedge clk) begin: gen_mem
	integer		i;
	if (cs) begin
		if (|bwe) begin
			// synthesis translate_off
			tmp = ram[addr];
			// synthesis translate_on
			for (i = 0; i < DATA_BYTE; i = i + 1)
				if (bwe[i])
					tmp[(i*BIT_PER_BYTE)+:BIT_PER_BYTE] = din[(i*BIT_PER_BYTE)+:BIT_PER_BYTE];
			// synthesis translate_off
			ram[addr] <= tmp;
			// synthesis translate_on
		end
	end
end

always @(posedge clk)
	if (cs) begin
		if (~|bwe) begin
			addr_d1 <= addr;
			drive_dout <= 1'b1;
		end
		else begin
			drive_dout <= 1'b0;
		end
	end
	else begin
		if (HOLD_DOUT == 1'b0)
			drive_dout <= 1'b0;
	end

// synthesis translate_off
assign  dout = ram[addr_d1];

// synthesis translate_on

endmodule
