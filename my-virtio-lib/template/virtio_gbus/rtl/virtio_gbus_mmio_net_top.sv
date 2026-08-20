`timescale 1ns/1ps

module virtio_gbus_mmio_net_top (
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

  input  logic [31:0] tosysbus_addr,
  output logic [31:0] tosysbus_data,
  input  logic        todut_en,
  input  logic [31:0] todut_addr,
  input  logic [31:0] todut_data
);

  localparam logic [31:0] VIRTIO_MMIO_MAGIC_VALUE = 32'h7472_6976;
  localparam logic [31:0] VIRTIO_MMIO_VERSION     = 32'h0000_0001;
  localparam logic [31:0] VIRTIO_MMIO_DEVICE_ID   = 32'h0000_0001;
  localparam logic [31:0] VIRTIO_MMIO_VENDOR_ID   = 32'h5253_5658;
  localparam logic [31:0] VIRTIO_GBUS_MAGIC       = 32'h7375_6267;
  localparam logic [31:0] VIRTIO_GBUS_VERSION     = 32'h0000_0001;

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
  localparam logic [11:0] REG_CONFIG_GENERATION   = 12'h0fc;
  localparam logic [11:0] REG_CONFIG              = 12'h100;

  localparam logic [31:0] GBUS_MAGIC              = 32'h0000;
  localparam logic [31:0] GBUS_VERSION            = 32'h0004;
  localparam logic [31:0] GBUS_STATUS             = 32'h0010;
  localparam logic [31:0] GBUS_DRIVER_FEATURES_0  = 32'h0014;
  localparam logic [31:0] GBUS_DRIVER_FEATURES_1  = 32'h0018;
  localparam logic [31:0] GBUS_GUEST_PAGE_SIZE    = 32'h001c;
  localparam logic [31:0] GBUS_RESET_SEQ          = 32'h0020;
  localparam logic [31:0] GBUS_NET_MAC_LOW        = 32'h0060;
  localparam logic [31:0] GBUS_NET_MAC_HIGH       = 32'h0064;
  localparam logic [31:0] GBUS_NET_STATUS         = 32'h0068;
  localparam logic [31:0] GBUS_NET_MAX_QUEUE_PAIRS = 32'h006c;
  localparam logic [31:0] GBUS_QUEUE_BASE         = 32'h0100;
  localparam logic [31:0] GBUS_QUEUE_STRIDE       = 32'h0020;
  localparam logic [31:0] GBUS_HOST_IRQ_SET       = 32'h0200;

  localparam int unsigned NUM_QUEUES              = 3;
  localparam logic [31:0] QUEUE_NUM_MAX           = 32'd256;
  localparam logic [31:0] INT_VRING               = 32'h0000_0001;
  localparam logic [31:0] INT_CONFIG              = 32'h0000_0002;
  localparam logic [47:0] NET_MAC_DEFAULT         = 48'h5634_1200_5452;
  localparam logic [15:0] NET_STATUS_LINK_UP      = 16'h0001;
  localparam logic [15:0] NET_MAX_QUEUE_PAIRS     = 16'h0001;

  localparam int unsigned VIRTIO_NET_F_MAC        = 5;
  localparam int unsigned VIRTIO_NET_F_CTRL_VQ    = 17;
  localparam int unsigned VIRTIO_NET_F_MQ         = 22;
  localparam int unsigned VIRTIO_RING_F_EVENT_IDX = 29;

  localparam logic [31:0] DEVICE_FEATURES_0 =
    (32'h1 << VIRTIO_NET_F_MAC) |
    (32'h1 << VIRTIO_NET_F_CTRL_VQ) |
    (32'h1 << VIRTIO_NET_F_MQ) |
    (32'h1 << VIRTIO_RING_F_EVENT_IDX);

  logic [31:0] device_features_sel_reg;
  logic [31:0] driver_features_sel_reg;
  logic [31:0] driver_features_0_reg;
  logic [31:0] driver_features_1_reg;
  logic [31:0] guest_page_size_reg;
  logic [31:0] queue_sel_reg;
  logic [31:0] interrupt_status_reg;
  logic [31:0] status_reg;
  logic [31:0] reset_seq_reg;
  logic [47:0] mac_reg;
  logic [15:0] net_status_reg;
  logic [15:0] max_queue_pairs_reg;

  logic [31:0] queue_num_reg        [NUM_QUEUES];
  logic [31:0] queue_align_reg      [NUM_QUEUES];
  logic [31:0] queue_pfn_reg        [NUM_QUEUES];
  logic [31:0] queue_ready_reg      [NUM_QUEUES];
  logic [31:0] queue_notify_seq_reg [NUM_QUEUES];

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

  function automatic logic [7:0] net_config_byte(input logic [7:0] offset);
    begin
      net_config_byte = 8'h00;
      unique case (offset)
        8'd0: net_config_byte = mac_reg[7:0];
        8'd1: net_config_byte = mac_reg[15:8];
        8'd2: net_config_byte = mac_reg[23:16];
        8'd3: net_config_byte = mac_reg[31:24];
        8'd4: net_config_byte = mac_reg[39:32];
        8'd5: net_config_byte = mac_reg[47:40];
        8'd6: net_config_byte = net_status_reg[7:0];
        8'd7: net_config_byte = net_status_reg[15:8];
        8'd8: net_config_byte = max_queue_pairs_reg[7:0];
        8'd9: net_config_byte = max_queue_pairs_reg[15:8];
        default: net_config_byte = 8'h00;
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
        REG_VERSION:             mmio_word = VIRTIO_MMIO_VERSION;
        REG_DEVICE_ID:           mmio_word = VIRTIO_MMIO_DEVICE_ID;
        REG_VENDOR_ID:           mmio_word = VIRTIO_MMIO_VENDOR_ID;
        REG_DEVICE_FEATURES:     mmio_word = (device_features_sel_reg == 32'd0) ?
                                             DEVICE_FEATURES_0 : 32'h0000_0000;
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
        mmio_byte = net_config_byte(byte_addr[7:0] - REG_CONFIG[7:0]);
      end else begin
        mmio_byte = 8'(mmio_word({byte_addr[11:2], 2'b00}) >>
                       ({29'h0, byte_addr[1:0]} << 3));
      end
    end
  endfunction

  function automatic logic [31:0] gbus_word(input logic [31:0] csr_addr);
    logic in_queue_window;
    logic [1:0] q;
    logic [31:0] qoff;
    begin
      gbus_word = 32'h0000_0000;
      in_queue_window = (csr_addr >= GBUS_QUEUE_BASE) &&
                        (csr_addr < (GBUS_QUEUE_BASE +
                                     NUM_QUEUES * GBUS_QUEUE_STRIDE));
      if (in_queue_window) begin
        q = 2'((csr_addr - GBUS_QUEUE_BASE) / GBUS_QUEUE_STRIDE);
        qoff = (csr_addr - GBUS_QUEUE_BASE) % GBUS_QUEUE_STRIDE;
        unique case (qoff)
          32'h00: gbus_word = queue_num_reg[q];
          32'h04: gbus_word = queue_align_reg[q];
          32'h08: gbus_word = queue_pfn_reg[q];
          32'h0c: gbus_word = queue_ready_reg[q];
          32'h10: gbus_word = queue_notify_seq_reg[q];
          default: gbus_word = 32'h0000_0000;
        endcase
      end else begin
        unique case (csr_addr)
          GBUS_MAGIC:              gbus_word = VIRTIO_GBUS_MAGIC;
          GBUS_VERSION:            gbus_word = VIRTIO_GBUS_VERSION;
          GBUS_STATUS:             gbus_word = status_reg;
          GBUS_DRIVER_FEATURES_0:  gbus_word = driver_features_0_reg;
          GBUS_DRIVER_FEATURES_1:  gbus_word = driver_features_1_reg;
          GBUS_GUEST_PAGE_SIZE:    gbus_word = guest_page_size_reg;
          GBUS_RESET_SEQ:          gbus_word = reset_seq_reg;
          GBUS_NET_MAC_LOW:        gbus_word = mac_reg[31:0];
          GBUS_NET_MAC_HIGH:       gbus_word = {16'h0000, mac_reg[47:32]};
          GBUS_NET_STATUS:         gbus_word = {16'h0000, net_status_reg};
          GBUS_NET_MAX_QUEUE_PAIRS: gbus_word = {16'h0000, max_queue_pairs_reg};
          default:                 gbus_word = 32'h0000_0000;
        endcase
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

  assign tosysbus_data = gbus_word(tosysbus_addr);

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
      end
    end
  endtask

  always_ff @(posedge clock or posedge reset) begin
    if (reset) begin
      clear_driver_state();
      guest_page_size_reg <= 32'h0000_0000;
      reset_seq_reg <= 32'h0000_0000;
      mac_reg <= NET_MAC_DEFAULT;
      net_status_reg <= NET_STATUS_LINK_UP;
      max_queue_pairs_reg <= NET_MAX_QUEUE_PAIRS;
    end else begin
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
            end else if (driver_features_sel_reg == 32'd1) begin
              driver_features_1_reg <= new_word;
            end
          end
          REG_DRIVER_FEATURES_SEL: driver_features_sel_reg <= new_word;
          REG_GUEST_PAGE_SIZE: guest_page_size_reg <= new_word;
          REG_QUEUE_SEL: queue_sel_reg <= new_word;
          REG_QUEUE_NUM: begin
            if (queue_sel_reg < NUM_QUEUES) begin
              queue_num_reg[q] <= new_word;
            end
          end
          REG_QUEUE_ALIGN: begin
            if (queue_sel_reg < NUM_QUEUES) begin
              queue_align_reg[q] <= new_word;
            end
          end
          REG_QUEUE_PFN: begin
            if (queue_sel_reg < NUM_QUEUES) begin
              queue_pfn_reg[q] <= new_word;
              queue_ready_reg[q] <= {31'h0, (new_word != 32'h0000_0000)};
            end
          end
          REG_QUEUE_READY: begin
            if (queue_sel_reg < NUM_QUEUES) begin
              queue_ready_reg[q] <= {31'h0, new_word[0]};
            end
          end
          REG_QUEUE_NOTIFY: begin
            if (new_word < NUM_QUEUES) begin
              queue_notify_seq_reg[new_word[$clog2(NUM_QUEUES)-1:0]] <=
                queue_notify_seq_reg[new_word[$clog2(NUM_QUEUES)-1:0]] + 32'd1;
            end
          end
          REG_INTERRUPT_ACK: interrupt_status_reg <= interrupt_status_reg & ~new_word;
          REG_STATUS: begin
            if (new_word == 32'h0000_0000) begin
              clear_driver_state();
              reset_seq_reg <= reset_seq_reg + 32'd1;
            end else begin
              status_reg <= new_word;
            end
          end
          default: begin
          end
        endcase
      end

      if (todut_en) begin
        unique case (todut_addr)
          GBUS_NET_MAC_LOW: mac_reg[31:0] <= todut_data;
          GBUS_NET_MAC_HIGH: mac_reg[47:32] <= todut_data[15:0];
          GBUS_NET_STATUS: net_status_reg <= todut_data[15:0];
          GBUS_NET_MAX_QUEUE_PAIRS: max_queue_pairs_reg <= todut_data[15:0];
          GBUS_HOST_IRQ_SET: interrupt_status_reg <=
                                interrupt_status_reg |
                                (todut_data & (INT_VRING | INT_CONFIG));
          default: begin
          end
        endcase
      end
    end
  end

endmodule
