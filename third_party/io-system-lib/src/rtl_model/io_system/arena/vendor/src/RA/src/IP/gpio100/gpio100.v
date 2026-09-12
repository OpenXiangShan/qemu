

`include "gpio100_config.vh"
`include "gpio100_const.vh"


module gpio100 (
`ifdef GPIO100_PULL_SUPPORT
	  gpio_pulldown,
	  gpio_pullup,
`endif
`ifdef GPIO100_INTR_SUPPORT
	  gpio_intr,
`endif
	  gpio_oe,
	  gpio_out,
	  paddr,
	  penable,
	  prdata,
	  psel,
	  pwdata,
	  pwrite,
	  pclk,
	  presetn,
	  extclk,
	  gpio_in
);

`ifdef GPIO100_PULL_SUPPORT
output      [(`GPIO100_GPIO_NUM - 1):0] gpio_pulldown;
output      [(`GPIO100_GPIO_NUM - 1):0] gpio_pullup;
`endif
`ifdef GPIO100_INTR_SUPPORT
output                                     gpio_intr;
`endif
output      [(`GPIO100_GPIO_NUM - 1):0] gpio_oe;
output      [(`GPIO100_GPIO_NUM - 1):0] gpio_out;
input                                [7:0] paddr;
input                                      penable;
output                              [31:0] prdata;
input                                      psel;
input                               [31:0] pwdata;
input                                      pwrite;
input                                      pclk;
input                                      presetn;
input                                      extclk;
input       [(`GPIO100_GPIO_NUM - 1):0] gpio_in;





// d187bdc7 rnd


`ifdef GPIO100_INTR_SUPPORT
wire        [(`GPIO100_GPIO_NUM - 1):0] gpio_intr_en;
wire        [(`GPIO100_GPIO_NUM - 1):0] gpio_intr_mode_b0;
wire        [(`GPIO100_GPIO_NUM - 1):0] gpio_intr_mode_b1;
wire        [(`GPIO100_GPIO_NUM - 1):0] gpio_intr_mode_b2;
wire        [(`GPIO100_GPIO_NUM - 1):0] gpio_ch_intr_trg;
`endif
`ifdef GPIO100_DEBOUNCE_SUPPORT
wire                                       gpio_db_clk;
wire        [(`GPIO100_GPIO_NUM - 1):0] gpio_db_en;
wire                                 [7:0] gpio_db_scale;
`endif
wire        [(`GPIO100_GPIO_NUM - 1):0] gpio_in_en;
wire        [(`GPIO100_GPIO_NUM - 1):0] gpio_din;


gpio100_apbslv gpio100_apbslv (
	.pclk             (pclk             ),
	.presetn          (presetn          ),
	.psel             (psel             ),
	.penable          (penable          ),
	.pwrite           (pwrite           ),
	.paddr            (paddr            ),
	.pwdata           (pwdata           ),
	.prdata           (prdata           ),
`ifdef GPIO100_PULL_SUPPORT
	.gpio_pullup      (gpio_pullup      ),
	.gpio_pulldown    (gpio_pulldown    ),
`endif
`ifdef GPIO100_INTR_SUPPORT
	.gpio_ch_intr_trg (gpio_ch_intr_trg ),
	.gpio_intr_en     (gpio_intr_en     ),
	.gpio_intr_mode_b0(gpio_intr_mode_b0),
	.gpio_intr_mode_b1(gpio_intr_mode_b1),
	.gpio_intr_mode_b2(gpio_intr_mode_b2),
	.gpio_intr        (gpio_intr        ),
`endif
`ifdef GPIO100_DEBOUNCE_SUPPORT
	.gpio_db_en       (gpio_db_en       ),
	.gpio_db_scale    (gpio_db_scale    ),
	.gpio_db_clk      (gpio_db_clk      ),
`endif
	.gpio_din         (gpio_din         ),
	.gpio_out         (gpio_out         ),
	.gpio_oe          (gpio_oe          ),
	.gpio_in_en       (gpio_in_en       )
);

gpio100_gpio gpio100_gpio (
	.pclk             (pclk             ),
	.presetn          (presetn          ),
	.extclk           (extclk           ),
`ifdef GPIO100_INTR_SUPPORT
	.gpio_intr_en     (gpio_intr_en     ),
	.gpio_intr_mode_b0(gpio_intr_mode_b0),
	.gpio_intr_mode_b1(gpio_intr_mode_b1),
	.gpio_intr_mode_b2(gpio_intr_mode_b2),
	.gpio_ch_intr_trg (gpio_ch_intr_trg ),
`endif
`ifdef GPIO100_DEBOUNCE_SUPPORT
	.gpio_db_en       (gpio_db_en       ),
	.gpio_db_scale    (gpio_db_scale    ),
	.gpio_db_clk      (gpio_db_clk      ),
`endif
	.gpio_in          (gpio_in          ),
	.gpio_din         (gpio_din         ),
	.gpio_in_en       (gpio_in_en       )
);






endmodule
