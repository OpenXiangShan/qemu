`timescale 1ns/1ps

module virtio_gbus_mmio_blk_top (
  input  logic        clock,
  input  logic        reset,

  input  logic [11:0] addr,
  input  logic        write,
  input  logic [31:0] wdata,
  input  logic [2:0]  size,
  output logic [31:0] rdata,
  output logic        ready,
  input  logic        valid,
  /* verilator lint_off SYMRSVDWORD */
  output logic        interrupt,
  /* verilator lint_on SYMRSVDWORD */

  output logic [31:0] tosysbus_addr,
  output logic [31:0] tosysbus_data,
  input  logic        todut_en,
  input  logic [31:0] todut_addr,
  input  logic [31:0] todut_data
);

  localparam logic [31:0] VIRTIO_MMIO_MAGIC_VALUE = 32'h7472_6976;
  localparam logic [31:0] VIRTIO_MMIO_VERSION_LEGACY = 32'h0000_0001;
  localparam logic [31:0] VIRTIO_MMIO_VERSION_MODERN = 32'h0000_0002;
  localparam logic [31:0] VIRTIO_MMIO_DEVICE_ID   = 32'h0000_0002;
  localparam logic [31:0] VIRTIO_MMIO_VENDOR_ID   = 32'h5253_5658;

  localparam logic [11:0] REG_MAGIC_VALUE         = 12'h000;
  localparam logic [11:0] REG_VERSION             = 12'h004;
  localparam logic [11:0] REG_DEVICE_ID           = 12'h008;
  localparam logic [11:0] REG_VENDOR_ID           = 12'h00c;
  localparam logic [11:0] REG_DEVICE_FEATURES     = 12'h010;
  localparam logic [11:0] REG_DEVICE_FEATURES_SEL = 12'h014;
  localparam logic [11:0] REG_DRIVER_FEATURES     = 12'h020;
  localparam logic [11:0] REG_DRIVER_FEATURES_SEL = 12'h024;
  localparam logic [11:0] REG_GUEST_PAGE_SIZE     = 12'h028;
  localparam logic [11:0] REG_QUEUE_SEL           = 12'h030;
  localparam logic [11:0] REG_QUEUE_NUM_MAX       = 12'h034;
  localparam logic [11:0] REG_QUEUE_NUM           = 12'h038;
  localparam logic [11:0] REG_QUEUE_ALIGN         = 12'h03c;
  localparam logic [11:0] REG_QUEUE_PFN           = 12'h040;
  localparam logic [11:0] REG_QUEUE_READY         = 12'h044;
  localparam logic [11:0] REG_QUEUE_NOTIFY        = 12'h050;
  localparam logic [11:0] REG_INTERRUPT_STATUS    = 12'h060;
  localparam logic [11:0] REG_INTERRUPT_ACK       = 12'h064;
  localparam logic [11:0] REG_STATUS              = 12'h070;
  localparam logic [11:0] REG_QUEUE_DESC_LOW      = 12'h080;
  localparam logic [11:0] REG_QUEUE_DESC_HIGH     = 12'h084;
  localparam logic [11:0] REG_QUEUE_AVAIL_LOW     = 12'h090;
  localparam logic [11:0] REG_QUEUE_AVAIL_HIGH    = 12'h094;
  localparam logic [11:0] REG_QUEUE_USED_LOW      = 12'h0a0;
  localparam logic [11:0] REG_QUEUE_USED_HIGH     = 12'h0a4;
  localparam logic [11:0] REG_CONFIG_GENERATION   = 12'h0fc;
  localparam logic [11:0] REG_CONFIG              = 12'h100;

  localparam logic [31:0] GBUS_MAGIC              = 32'h0000;
  localparam logic [31:0] GBUS_MAGIC_VALUE        = 32'h7375_6267;
  localparam logic [31:0] GBUS_STATUS             = 32'h0010;
  localparam logic [31:0] GBUS_DRIVER_FEATURES_0  = 32'h0014;
  localparam logic [31:0] GBUS_DRIVER_FEATURES_1  = 32'h0018;
  localparam logic [31:0] GBUS_GUEST_PAGE_SIZE    = 32'h001c;
  localparam logic [31:0] GBUS_RESET_SEQ          = 32'h0020;
  localparam logic [31:0] GBUS_TRANSPORT_FEATURES_0 = 32'h0024;
  localparam logic [31:0] GBUS_TRANSPORT_FEATURES_1 = 32'h0028;
  localparam logic [31:0] GBUS_BLK_CAPACITY_LOW   = 32'h0040;
  localparam logic [31:0] GBUS_BLK_CAPACITY_HIGH  = 32'h0044;
  localparam logic [31:0] GBUS_BLK_SEG_MAX        = 32'h0048;
  localparam logic [31:0] GBUS_BLK_SIZE           = 32'h004c;
  localparam logic [31:0] GBUS_QUEUE_BASE         = 32'h0100;
  localparam logic [31:0] GBUS_QUEUE_STRIDE       = 32'h0040;
  localparam logic [31:0] GBUS_QUEUE_NUM          = 32'h0000;
  localparam logic [31:0] GBUS_QUEUE_ALIGN        = 32'h0004;
  localparam logic [31:0] GBUS_QUEUE_PFN          = 32'h0008;
  localparam logic [31:0] GBUS_QUEUE_READY        = 32'h000c;
  localparam logic [31:0] GBUS_QUEUE_NOTIFY_SEQ   = 32'h0010;
  localparam logic [31:0] GBUS_QUEUE_DESC_LOW     = 32'h0014;
  localparam logic [31:0] GBUS_QUEUE_DESC_HIGH    = 32'h0018;
  localparam logic [31:0] GBUS_QUEUE_AVAIL_LOW    = 32'h001c;
  localparam logic [31:0] GBUS_QUEUE_AVAIL_HIGH   = 32'h0020;
  localparam logic [31:0] GBUS_QUEUE_USED_LOW     = 32'h0024;
  localparam logic [31:0] GBUS_QUEUE_USED_HIGH    = 32'h0028;
  localparam logic [31:0] GBUS_HOST_IRQ_SET       = 32'h0200;

  localparam int unsigned NUM_QUEUES              = 3;
  localparam logic [31:0] QUEUE_NUM_MAX           = 32'd128;
  localparam logic [31:0] INT_VRING               = 32'h0000_0001;
  localparam logic [31:0] INT_CONFIG              = 32'h0000_0002;
  localparam logic [31:0] BLK_SEG_MAX_DEFAULT     = 32'd126;
  localparam logic [31:0] BLK_SIZE_DEFAULT        = 32'd512;

  localparam int unsigned VIRTIO_BLK_F_SEG_MAX    = 2;
  localparam int unsigned VIRTIO_BLK_F_BLK_SIZE   = 6;
  localparam int unsigned VIRTIO_BLK_F_FLUSH      = 9;
  localparam int unsigned VIRTIO_RING_F_EVENT_IDX = 29;
  localparam int unsigned VIRTIO_F_VERSION_1_SEL1 = 0;

  localparam logic [31:0] DEVICE_FEATURES_0 =
    (32'h1 << VIRTIO_BLK_F_SEG_MAX) |
    (32'h1 << VIRTIO_BLK_F_BLK_SIZE) |
    (32'h1 << VIRTIO_BLK_F_FLUSH) |
    (32'h1 << VIRTIO_RING_F_EVENT_IDX);

  logic [31:0] device_features_sel_reg;
  logic [31:0] driver_features_sel_reg;
  logic [31:0] driver_features_0_reg;
  logic [31:0] driver_features_1_reg;
  logic [31:0] transport_features_0_reg;
  logic [31:0] transport_features_1_reg;
  logic [31:0] guest_page_size_reg;
  logic [31:0] queue_sel_reg;
  logic [31:0] interrupt_status_reg;
  logic [31:0] status_reg;
  logic [31:0] reset_seq_reg;
  logic [63:0] capacity_reg;
  logic [31:0] seg_max_reg;
  logic [31:0] blk_size_reg;

  logic [31:0] queue_num_reg        [NUM_QUEUES];
  logic [31:0] queue_align_reg      [NUM_QUEUES];
  logic [31:0] queue_pfn_reg        [NUM_QUEUES];
  logic [31:0] queue_ready_reg      [NUM_QUEUES];
  logic [31:0] queue_notify_seq_reg [NUM_QUEUES];
  logic [63:0] queue_desc_reg       [NUM_QUEUES];
  logic [63:0] queue_avail_reg      [NUM_QUEUES];
  logic [63:0] queue_used_reg       [NUM_QUEUES];

  logic        publish_pending_valid_reg;
  logic [31:0] publish_pending_addr_reg;
  logic [31:0] publish_pending_data_reg;

  assign ready = 1'b1;
  assign interrupt = interrupt_status_reg != 32'h0000_0000;

  function automatic logic [31:0] clamp_size(input logic [2:0] size_i);
    begin
      unique case (size_i)
        3'd1: clamp_size = 32'd1;
        3'd2: clamp_size = 32'd2;
        3'd4: clamp_size = 32'd4;
        default: clamp_size = 32'd4;
      endcase
    end
  endfunction

  function automatic logic [7:0] blk_config_byte(input logic [7:0] offset);
    begin
      blk_config_byte = 8'h00;
      unique case (offset)
        8'd0:  blk_config_byte = capacity_reg[7:0];
        8'd1:  blk_config_byte = capacity_reg[15:8];
        8'd2:  blk_config_byte = capacity_reg[23:16];
        8'd3:  blk_config_byte = capacity_reg[31:24];
        8'd4:  blk_config_byte = capacity_reg[39:32];
        8'd5:  blk_config_byte = capacity_reg[47:40];
        8'd6:  blk_config_byte = capacity_reg[55:48];
        8'd7:  blk_config_byte = capacity_reg[63:56];
        8'd12: blk_config_byte = seg_max_reg[7:0];
        8'd13: blk_config_byte = seg_max_reg[15:8];
        8'd14: blk_config_byte = seg_max_reg[23:16];
        8'd15: blk_config_byte = seg_max_reg[31:24];
        8'd20: blk_config_byte = blk_size_reg[7:0];
        8'd21: blk_config_byte = blk_size_reg[15:8];
        8'd22: blk_config_byte = blk_size_reg[23:16];
        8'd23: blk_config_byte = blk_size_reg[31:24];
        default: blk_config_byte = 8'h00;
      endcase
    end
  endfunction

  function automatic logic [31:0] mmio_word(input logic [11:0] reg_addr);
    logic queue_valid;
    logic [1:0] q;
    begin
      q = queue_sel_reg[1:0];
      queue_valid = queue_sel_reg < NUM_QUEUES;
      mmio_word = 32'h0000_0000;
      unique case (reg_addr)
        REG_MAGIC_VALUE:         mmio_word = VIRTIO_MMIO_MAGIC_VALUE;
        REG_VERSION:             mmio_word = transport_features_1_reg[VIRTIO_F_VERSION_1_SEL1] ?
                                             VIRTIO_MMIO_VERSION_MODERN :
                                             VIRTIO_MMIO_VERSION_LEGACY;
        REG_DEVICE_ID:           mmio_word = VIRTIO_MMIO_DEVICE_ID;
        REG_VENDOR_ID:           mmio_word = VIRTIO_MMIO_VENDOR_ID;
        REG_DEVICE_FEATURES:     mmio_word = (device_features_sel_reg == 32'd0) ?
                                             (DEVICE_FEATURES_0 |
                                              transport_features_0_reg) :
                                             (device_features_sel_reg == 32'd1) ?
                                             transport_features_1_reg :
                                             32'h0000_0000;
        REG_DEVICE_FEATURES_SEL: mmio_word = device_features_sel_reg;
        REG_DRIVER_FEATURES:     mmio_word = (driver_features_sel_reg == 32'd0) ?
                                             driver_features_0_reg :
                                             (driver_features_sel_reg == 32'd1) ?
                                             driver_features_1_reg : 32'h0000_0000;
        REG_DRIVER_FEATURES_SEL: mmio_word = driver_features_sel_reg;
        REG_GUEST_PAGE_SIZE:     mmio_word = guest_page_size_reg;
        REG_QUEUE_SEL:           mmio_word = queue_sel_reg;
        REG_QUEUE_NUM_MAX:       mmio_word = queue_valid ? QUEUE_NUM_MAX : 32'h0;
        REG_QUEUE_NUM:           mmio_word = queue_valid ? queue_num_reg[q] : 32'h0;
        REG_QUEUE_ALIGN:         mmio_word = queue_valid ? queue_align_reg[q] : 32'h0;
        REG_QUEUE_PFN:           mmio_word = queue_valid ? queue_pfn_reg[q] : 32'h0;
        REG_QUEUE_READY:         mmio_word = queue_valid ? queue_ready_reg[q] : 32'h0;
        REG_QUEUE_DESC_LOW:      mmio_word = queue_valid ? queue_desc_reg[q][31:0] : 32'h0;
        REG_QUEUE_DESC_HIGH:     mmio_word = queue_valid ? queue_desc_reg[q][63:32] : 32'h0;
        REG_QUEUE_AVAIL_LOW:     mmio_word = queue_valid ? queue_avail_reg[q][31:0] : 32'h0;
        REG_QUEUE_AVAIL_HIGH:    mmio_word = queue_valid ? queue_avail_reg[q][63:32] : 32'h0;
        REG_QUEUE_USED_LOW:      mmio_word = queue_valid ? queue_used_reg[q][31:0] : 32'h0;
        REG_QUEUE_USED_HIGH:     mmio_word = queue_valid ? queue_used_reg[q][63:32] : 32'h0;
        REG_INTERRUPT_STATUS:    mmio_word = interrupt_status_reg;
        REG_STATUS:              mmio_word = status_reg;
        REG_CONFIG_GENERATION:   mmio_word = 32'h0000_0000;
        default:                 mmio_word = 32'h0000_0000;
      endcase
    end
  endfunction

  function automatic logic [7:0] mmio_byte(input logic [11:0] byte_addr);
    begin
      if (byte_addr >= REG_CONFIG) begin
        mmio_byte = blk_config_byte(byte_addr[7:0] - REG_CONFIG[7:0]);
      end else begin
        mmio_byte = 8'(mmio_word({byte_addr[11:2], 2'b00}) >>
                       ({29'h0, byte_addr[1:0]} << 3));
      end
    end
  endfunction

  always_comb begin
    rdata = 32'h0000_0000;
    for (int i = 0; i < 4; i++) begin
      if (32'(i) < clamp_size(size)) begin
        rdata[i*8 +: 8] = mmio_byte(addr + 12'(i));
      end
    end
  end

  task automatic publish_sysbus(
    input logic [31:0] csr_addr,
    input logic [31:0] csr_data
  );
    begin
      tosysbus_addr <= csr_addr;
      tosysbus_data <= csr_data;
    end
  endtask

  task automatic publish_sysbus_later(
    input logic [31:0] csr_addr,
    input logic [31:0] csr_data
  );
    begin
      publish_pending_valid_reg <= 1'b1;
      publish_pending_addr_reg <= csr_addr;
      publish_pending_data_reg <= csr_data;
    end
  endtask

  task automatic clear_driver_state;
    begin
      device_features_sel_reg <= 32'h0000_0000;
      driver_features_sel_reg <= 32'h0000_0000;
      driver_features_0_reg   <= 32'h0000_0000;
      driver_features_1_reg   <= 32'h0000_0000;
      // Preserve GUEST_PAGE_SIZE: Linux legacy virtio-mmio writes it before reset.
      queue_sel_reg           <= 32'h0000_0000;
      interrupt_status_reg    <= 32'h0000_0000;
      status_reg              <= 32'h0000_0000;
      for (int i = 0; i < NUM_QUEUES; i++) begin
        queue_num_reg[i]        <= 32'h0000_0000;
        queue_align_reg[i]      <= 32'h0000_0000;
        queue_pfn_reg[i]        <= 32'h0000_0000;
        queue_ready_reg[i]      <= 32'h0000_0000;
        queue_notify_seq_reg[i] <= 32'h0000_0000;
        queue_desc_reg[i]       <= 64'h0000_0000_0000_0000;
        queue_avail_reg[i]      <= 64'h0000_0000_0000_0000;
        queue_used_reg[i]       <= 64'h0000_0000_0000_0000;
      end
    end
  endtask

  always_ff @(posedge clock or posedge reset) begin
    if (reset) begin
      clear_driver_state();
      transport_features_0_reg <= 32'h0000_0000;
      transport_features_1_reg <= 32'h0000_0000;
      guest_page_size_reg <= 32'h0000_0000;
      reset_seq_reg <= 32'h0000_0000;
      capacity_reg  <= 64'h0000_0000_0000_0000;
      seg_max_reg   <= BLK_SEG_MAX_DEFAULT;
      blk_size_reg  <= BLK_SIZE_DEFAULT;
      tosysbus_addr <= GBUS_MAGIC;
      tosysbus_data <= GBUS_MAGIC_VALUE;
      publish_pending_valid_reg <= 1'b0;
      publish_pending_addr_reg <= 32'h0000_0000;
      publish_pending_data_reg <= 32'h0000_0000;
    end else begin
      tosysbus_addr <= GBUS_MAGIC;
      tosysbus_data <= GBUS_MAGIC_VALUE;
      if (publish_pending_valid_reg) begin
        publish_sysbus(publish_pending_addr_reg,
                       publish_pending_data_reg);
        publish_pending_valid_reg <= 1'b0;
      end

      if (valid && write && addr[11:0] < REG_CONFIG) begin
        logic [11:0] reg_addr;
        logic [31:0] old_word;
        logic [31:0] new_word;
        logic [1:0] q;
        int unsigned byte_index;

        reg_addr = {addr[11:2], 2'b00};
        old_word = mmio_word(reg_addr);
        new_word = old_word;
        for (int i = 0; i < 4; i++) begin
          if (32'(i) < clamp_size(size)) begin
            byte_index = {30'h0, addr[1:0]} + i;
            if (byte_index < 4) begin
              new_word[byte_index*8 +: 8] = wdata[i*8 +: 8];
            end
          end
        end
        q = queue_sel_reg[1:0];

        unique case (reg_addr)
          REG_DEVICE_FEATURES_SEL: device_features_sel_reg <= new_word;
          REG_DRIVER_FEATURES: begin
            if (driver_features_sel_reg == 32'd0) begin
              driver_features_0_reg <= new_word;
              publish_sysbus(GBUS_DRIVER_FEATURES_0, new_word);
            end else if (driver_features_sel_reg == 32'd1) begin
              driver_features_1_reg <= new_word;
              publish_sysbus(GBUS_DRIVER_FEATURES_1, new_word);
            end
          end
          REG_DRIVER_FEATURES_SEL: driver_features_sel_reg <= new_word;
          REG_GUEST_PAGE_SIZE: begin
            guest_page_size_reg <= new_word;
            publish_sysbus(GBUS_GUEST_PAGE_SIZE, new_word);
          end
          REG_QUEUE_SEL: queue_sel_reg <= new_word;
          REG_QUEUE_NUM: begin
            if (queue_sel_reg < NUM_QUEUES) begin
              queue_num_reg[q] <= new_word;
              publish_sysbus(GBUS_QUEUE_BASE +
                             queue_sel_reg * GBUS_QUEUE_STRIDE +
                             GBUS_QUEUE_NUM,
                             new_word);
            end
          end
          REG_QUEUE_ALIGN: begin
            if (queue_sel_reg < NUM_QUEUES) begin
              queue_align_reg[q] <= new_word;
              publish_sysbus(GBUS_QUEUE_BASE +
                             queue_sel_reg * GBUS_QUEUE_STRIDE +
                             GBUS_QUEUE_ALIGN,
                             new_word);
            end
          end
          REG_QUEUE_PFN: begin
            if (queue_sel_reg < NUM_QUEUES) begin
              queue_pfn_reg[q] <= new_word;
              queue_ready_reg[q] <= {31'h0, (new_word != 32'h0000_0000)};
              publish_sysbus(GBUS_QUEUE_BASE +
                             queue_sel_reg * GBUS_QUEUE_STRIDE +
                             GBUS_QUEUE_PFN,
                             new_word);
              publish_sysbus_later(
                GBUS_QUEUE_BASE + queue_sel_reg * GBUS_QUEUE_STRIDE +
                GBUS_QUEUE_READY,
                {31'h0, (new_word != 32'h0000_0000)});
            end
          end
          REG_QUEUE_READY: begin
            if (queue_sel_reg < NUM_QUEUES) begin
              queue_ready_reg[q] <= {31'h0, new_word[0]};
              publish_sysbus(GBUS_QUEUE_BASE +
                             queue_sel_reg * GBUS_QUEUE_STRIDE +
                             GBUS_QUEUE_READY,
                             {31'h0, new_word[0]});
            end
          end
          REG_QUEUE_DESC_LOW: begin
            if (queue_sel_reg < NUM_QUEUES) begin
              queue_desc_reg[q][31:0] <= new_word;
              publish_sysbus(GBUS_QUEUE_BASE +
                             queue_sel_reg * GBUS_QUEUE_STRIDE +
                             GBUS_QUEUE_DESC_LOW,
                             new_word);
            end
          end
          REG_QUEUE_DESC_HIGH: begin
            if (queue_sel_reg < NUM_QUEUES) begin
              queue_desc_reg[q][63:32] <= new_word;
              publish_sysbus(GBUS_QUEUE_BASE +
                             queue_sel_reg * GBUS_QUEUE_STRIDE +
                             GBUS_QUEUE_DESC_HIGH,
                             new_word);
            end
          end
          REG_QUEUE_AVAIL_LOW: begin
            if (queue_sel_reg < NUM_QUEUES) begin
              queue_avail_reg[q][31:0] <= new_word;
              publish_sysbus(GBUS_QUEUE_BASE +
                             queue_sel_reg * GBUS_QUEUE_STRIDE +
                             GBUS_QUEUE_AVAIL_LOW,
                             new_word);
            end
          end
          REG_QUEUE_AVAIL_HIGH: begin
            if (queue_sel_reg < NUM_QUEUES) begin
              queue_avail_reg[q][63:32] <= new_word;
              publish_sysbus(GBUS_QUEUE_BASE +
                             queue_sel_reg * GBUS_QUEUE_STRIDE +
                             GBUS_QUEUE_AVAIL_HIGH,
                             new_word);
            end
          end
          REG_QUEUE_USED_LOW: begin
            if (queue_sel_reg < NUM_QUEUES) begin
              queue_used_reg[q][31:0] <= new_word;
              publish_sysbus(GBUS_QUEUE_BASE +
                             queue_sel_reg * GBUS_QUEUE_STRIDE +
                             GBUS_QUEUE_USED_LOW,
                             new_word);
            end
          end
          REG_QUEUE_USED_HIGH: begin
            if (queue_sel_reg < NUM_QUEUES) begin
              queue_used_reg[q][63:32] <= new_word;
              publish_sysbus(GBUS_QUEUE_BASE +
                             queue_sel_reg * GBUS_QUEUE_STRIDE +
                             GBUS_QUEUE_USED_HIGH,
                             new_word);
            end
          end
          REG_QUEUE_NOTIFY: begin
            if (new_word < NUM_QUEUES) begin
              queue_notify_seq_reg[new_word[$clog2(NUM_QUEUES)-1:0]] <=
                queue_notify_seq_reg[new_word[$clog2(NUM_QUEUES)-1:0]] + 32'd1;
              publish_sysbus(
                GBUS_QUEUE_BASE + new_word * GBUS_QUEUE_STRIDE +
                GBUS_QUEUE_NOTIFY_SEQ,
                queue_notify_seq_reg[
                  new_word[$clog2(NUM_QUEUES)-1:0]] + 32'd1);
            end
          end
          REG_INTERRUPT_ACK: interrupt_status_reg <= interrupt_status_reg & ~new_word;
          REG_STATUS: begin
            if (new_word == 32'h0000_0000) begin
              clear_driver_state();
              reset_seq_reg <= reset_seq_reg + 32'd1;
              publish_sysbus(GBUS_STATUS, 32'h0000_0000);
              publish_sysbus_later(GBUS_RESET_SEQ,
                                   reset_seq_reg + 32'd1);
            end else begin
              status_reg <= new_word;
              publish_sysbus(GBUS_STATUS, new_word);
            end
          end
          default: begin
          end
        endcase
      end

      if (todut_en) begin
        unique case (todut_addr)
          GBUS_TRANSPORT_FEATURES_0:
            transport_features_0_reg <= todut_data;
          GBUS_TRANSPORT_FEATURES_1:
            transport_features_1_reg <= todut_data;
          GBUS_BLK_CAPACITY_LOW:  capacity_reg[31:0]  <= todut_data;
          GBUS_BLK_CAPACITY_HIGH: capacity_reg[63:32] <= todut_data;
          GBUS_BLK_SEG_MAX:       seg_max_reg <= todut_data;
          GBUS_BLK_SIZE:          blk_size_reg <= todut_data;
          GBUS_HOST_IRQ_SET:      interrupt_status_reg <=
                                    interrupt_status_reg |
                                    (todut_data & (INT_VRING | INT_CONFIG));
          default: begin
          end
        endcase
      end
    end
  end

endmodule
