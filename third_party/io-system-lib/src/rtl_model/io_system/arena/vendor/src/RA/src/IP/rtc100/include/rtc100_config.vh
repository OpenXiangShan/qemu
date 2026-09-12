`ifdef RTC100_CONFIG_VH
`else
`define RTC100_CONFIG_VH

/* --------------------------------------------------------------------	*/
/* Define the day counter bit width (value: 1 ~ 15)			*/
/* One week  : 3 bits							*/
/* One month : 5 bits							*/
/* One year  : 9 bits							*/
/* -------------------------------------------------------------------- */
`define RTC100_DAY_BITS 5

/* -------------------------------------------------------------------- */
/* Half second support                                                  */
/* -------------------------------------------------------------------- */
`define RTC100_HALF_SECOND_SUPPORT

`endif // RTC100_CONFIG_VH
