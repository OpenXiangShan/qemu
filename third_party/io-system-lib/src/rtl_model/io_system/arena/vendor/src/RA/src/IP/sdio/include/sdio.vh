


`define     SDIO_REG_BASE           32'h4001_8000

`define     SDIO_AHB_ADDR_WIDTH     10

`define     SDIO_PWR_OFFSET         `SDIO_AHB_ADDR_WIDTH'h0
`define     SDIO_CLK_OFFSET         `SDIO_AHB_ADDR_WIDTH'h4
`define     SDIO_ARG_OFFSET         `SDIO_AHB_ADDR_WIDTH'h8
`define     SDIO_CMD_OFFSET         `SDIO_AHB_ADDR_WIDTH'hc
`define     SDIO_RSP_CMD_OFFSET     `SDIO_AHB_ADDR_WIDTH'h10
`define     SDIO_RSP_0_OFFSET       `SDIO_AHB_ADDR_WIDTH'h14
`define     SDIO_RSP_1_OFFSET       `SDIO_AHB_ADDR_WIDTH'h18
`define     SDIO_RSP_2_OFFSET       `SDIO_AHB_ADDR_WIDTH'h1c
`define     SDIO_RSP_3_OFFSET       `SDIO_AHB_ADDR_WIDTH'h20
`define     SDIO_DTIMER_OFFSET      `SDIO_AHB_ADDR_WIDTH'h24
`define     SDIO_DLEN_OFFSET        `SDIO_AHB_ADDR_WIDTH'h28
`define     SDIO_DCTRL_OFFSET       `SDIO_AHB_ADDR_WIDTH'h2c
`define     SDIO_DCNT_OFFSET        `SDIO_AHB_ADDR_WIDTH'h30
`define     SDIO_STA_OFFSET         `SDIO_AHB_ADDR_WIDTH'h34
`define     SDIO_ICR_OFFSET         `SDIO_AHB_ADDR_WIDTH'h38
`define     SDIO_MASK_OFFSET        `SDIO_AHB_ADDR_WIDTH'h3c
`define     SDIO_FIFOCNT_OFFSET     `SDIO_AHB_ADDR_WIDTH'h48
`define     SDIO_FIFO_OFFSET        `SDIO_AHB_ADDR_WIDTH'h80

`define     SDIO_PWR_CTRL           1:0

`define     SDIO_CLK_HWFC_EN        14
`define     SDIO_CLK_NEGEDGE        13
`define     SDIO_CLK_WIDBUS         12:11
`define     SDIO_CLK_BYPASS         10
`define     SDIO_CLK_PWRSAV         9
`define     SDIO_CLK_CLKEN          8
`define     SDIO_CLK_CLKDIV         25:16

`define     SDIO_ARG_CMDARG         31:0

`define     SDIO_CMD_ATACMD         14
`define     SDIO_CMD_NIEN           13
`define     SDIO_CMD_ENCMDC         12
`define     SDIO_CMD_SDIOSUSPEND    11
`define     SDIO_CMD_CPSMEN         10
`define     SDIO_CMD_WAITPEND       9
`define     SDIO_CMD_WAITINT        8
`define     SDIO_CMD_WAITRESP       7:6
`define     SDIO_CMD_CMDINDEX       5:0

`define     SDIO_RSP_CMD            5:0

`define     SDIO_RSP_CARDSTA        31:0

`define     SDIO_DTIMER             31:0

`define     SDIO_DLEN               24:0

`define     SDIO_DCTRL_SDIOEN       11
`define     SDIO_DCTRL_RWMOD        10
`define     SDIO_DCTRL_RWSTOP       9
`define     SDIO_DCTRL_RWSTART      8
`define     SDIO_DCTRL_DBLKSIZE     7:4
`define     SDIO_DCTRL_DMAEN        3
`define     SDIO_DCTRL_DTMOD        2
`define     SDIO_DCTRL_DTDIR        1
`define     SDIO_DCTRL_DTEN         0

`define     SDIO_DCNT               24:0

`define     SDIO_STA_CEATAEND       23
`define     SDIO_STA_SDIOIT         22
`define     SDIO_STA_RXDAVL         21
`define     SDIO_STA_TXDAVL         20
`define     SDIO_STA_RXFIFOE        19
`define     SDIO_STA_TXFIFOE        18
`define     SDIO_STA_RXFIFOF        17
`define     SDIO_STA_TXFIFOF        16
`define     SDIO_STA_RXFIFOHF       15
`define     SDIO_STA_TXFIFOHE       14
`define     SDIO_STA_RXACT          13
`define     SDIO_STA_TXACT          12
`define     SDIO_STA_CMDACT         11
`define     SDIO_STA_DBCKEND        10
`define     SDIO_STA_STBITERR       9
`define     SDIO_STA_DATAEND        8
`define     SDIO_STA_CMDSENT        7
`define     SDIO_STA_CMDREND        6
`define     SDIO_STA_RXOVERR        5
`define     SDIO_STA_TXUNDERR       4
`define     SDIO_STA_DTIMEOUT       3
`define     SDIO_STA_CTIMEOUT       2
`define     SDIO_STA_DCRCFAIL       1
`define     SDIO_STA_CCRCFAIL       0

`define     SDIO_ICR_CEATAENDC      23
`define     SDIO_ICR_SDIOITC        22
`define     SDIO_ICR_DBCKENDC       10
`define     SDIO_ICR_STBITERRC      9
`define     SDIO_ICR_DATAENDC       8
`define     SDIO_ICR_CMDSENTC       7
`define     SDIO_ICR_CMDRENDC       6
`define     SDIO_ICR_RXOVERRC       5
`define     SDIO_ICR_TXUNDERRC      4
`define     SDIO_ICR_DTIMEOUTC      3
`define     SDIO_ICR_CTIMEOUTC      2
`define     SDIO_ICR_DCRCFAILC      1
`define     SDIO_ICR_CCRCFAILC      0

`define     SDIO_MASK_CEATAENDIE    23
`define     SDIO_MASK_SDIOITIE      22
`define     SDIO_MASK_RXDAVLIE      21
`define     SDIO_MASK_TXDAVLIE      20
`define     SDIO_MASK_RXFIFOEIE     19
`define     SDIO_MASK_TXFIFOEIE     18
`define     SDIO_MASK_RXFIFOFIE     17
`define     SDIO_MASK_TXFIFOFIE     16
`define     SDIO_MASK_RXFIFOHFIE    15
`define     SDIO_MASK_TXFIFOHEIE    14
`define     SDIO_MASK_RXACTIE       13
`define     SDIO_MASK_TXACTIE       12
`define     SDIO_MASK_CMDACTIE      11
`define     SDIO_MASK_DBCKENDIE     10
`define     SDIO_MASK_STBITERRIE    9
`define     SDIO_MASK_DATAENDIE     8
`define     SDIO_MASK_CMDSENTIE     7
`define     SDIO_MASK_CMDRENDIE     6
`define     SDIO_MASK_RXOVERRIE     5
`define     SDIO_MASK_TXUNDERRIE    4
`define     SDIO_MASK_DTIMEOUTIE    3
`define     SDIO_MASK_CTIMEOUTIE    2
`define     SDIO_MASK_DCRCFAILIE    1
`define     SDIO_MASK_CCRCFAILIE    0

`define     SDIO_FIFOCNT            23:0

`define     SDIO_FIFO               31:0

