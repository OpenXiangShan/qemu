package ariane_axi_lite_512;

  // ---------------------------------------------------------
  // 配置参数
  // ---------------------------------------------------------
  localparam int AddrWidth = cva6_config_pkg::CVA6ConfigAxiAddrWidth;
  localparam int DataWidth = 512;  // 512位数据
  localparam int StrbWidth = DataWidth / 8;

  // ---------------------------------------------------------
  // 基本类型定义
  // ---------------------------------------------------------
  typedef logic [AddrWidth-1:0] addr_t;
  typedef logic [DataWidth-1:0] data_t;
  typedef logic [StrbWidth-1:0] strb_t;

  // ---------------------------------------------------------
  // 各通道结构定义
  // ---------------------------------------------------------

  // 写地址通道 (AW)
  typedef struct packed {
    addr_t     addr;  // 写地址
    axi_pkg::prot_t   prot;  // 保护属性
  } aw_chan_t;

  // 写数据通道 (W)
  typedef struct packed {
    data_t data;  // 写数据
    strb_t strb;  // 字节使能
  } w_chan_t;

  // 写响应通道 (B)
  typedef struct packed {
    axi_pkg::resp_t  resp; // 响应码
  } b_chan_t;

  // 读地址通道 (AR)
  typedef struct packed {
    addr_t    addr;
    axi_pkg::prot_t  prot;
  } ar_chan_t;

  // 读数据通道 (R)
  typedef struct packed {
    data_t    data;
    axi_pkg::resp_t  resp;
  } r_chan_t;

  // ---------------------------------------------------------
  // 请求/响应总线结构
  // ---------------------------------------------------------

  // Master 发给 Slave 的请求
  typedef struct packed {
    aw_chan_t aw;
    logic     aw_valid;

    w_chan_t  w;
    logic     w_valid;

    logic     b_ready;

    ar_chan_t ar;
    logic     ar_valid;

    logic     r_ready;
  } lite_req_t;

  // Slave 回给 Master 的响应
  typedef struct packed {
    logic     aw_ready;
    logic     ar_ready;
    logic     w_ready;

    logic     b_valid;
    b_chan_t  b;

    logic     r_valid;
    r_chan_t  r;
  } lite_resp_t;

endpackage
