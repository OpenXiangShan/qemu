`ifdef GPIO100_CONFIG_VH
`else
`define GPIO100_CONFIG_VH

//-------------------------------------------------
// GPIO Channel Number
//-------------------------------------------------
// Available value: 1~32
`define GPIO100_GPIO_NUM 16

//-------------------------------------------------
// GPIO Pull Option
//-------------------------------------------------
//`define GPIO100_PULL_SUPPORT

//-------------------------------------------------
// GPIO Interrupt Option
//-------------------------------------------------
`define GPIO100_INTR_SUPPORT

//-------------------------------------------------
// GPIO Debounce Option
//-------------------------------------------------
`define GPIO100_DEBOUNCE_SUPPORT

`endif // GPIO100_CONFIG_VH

