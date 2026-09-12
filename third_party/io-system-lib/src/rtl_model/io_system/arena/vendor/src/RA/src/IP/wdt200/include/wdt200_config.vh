
`ifdef WDT200_CONFIG_VH
`else // WDT200_CONFIG_VH
`define WDT200_CONFIG_VH

//-------------------------------------------------------
// Extend the WDT timer from 16-bit to 32-bit
//-------------------------------------------------------
//`define WDT200_32BIT_TIMER

//-------------------------------------------------------
// Magic Number of Write Protection (16-bit width)
//-------------------------------------------------------
`define WDT200_WP_NUM	16'h5aa5

//-------------------------------------------------------
// Magic Number of Restart Whdog Timer (16-bit width)
//-------------------------------------------------------
`define WDT200_RESTART_NUM	16'hcafe

`endif // WDT200_CONFIG_VH

