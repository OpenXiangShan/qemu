

`define     AHB_ADDR_WIDTH          10
`define     AHB_DATA_WIDTH          32
`define     AHB_SIZE_WIDTH          3
`define     AHB_TRANS_WIDTH         2
`define     AHB_BURST_WIDTH         3
`define     AHB_RSPS_WIDTH          1

`define     AHB_BURST_SINGLE        3'b000
`define     AHB_BURST_INCR          3'b001
`define     AHB_BURST_WRAP4         3'b010
`define     AHB_BURST_INCR4         3'b011
`define     AHB_BURST_WRAP8         3'b100
`define     AHB_BURST_INCR8         3'b101
`define     AHB_BURST_WRAP16        3'b110
`define     AHB_BURST_INCR16        3'b111

`define     AHB_TRANS_IDLE          2'b00
`define     AHB_TRANS_BUSY          2'b01
`define     AHB_TRANS_NOSEQ         2'b10
`define     AHB_TRANS_SEQ           2'b11

`define     AHB_SIZE_BYTE           3'b000
`define     AHB_SIZE_HALF           3'b001
`define     AHB_SIZE_WORD           3'b010

`define     AHB_RSPS_OKAY           2'b00
`define     AHB_RSPS_ERROR          2'b01
`define     AHB_RSPS_RETRY          2'b10
`define     AHB_RSPS_SPLIT          2'b11

