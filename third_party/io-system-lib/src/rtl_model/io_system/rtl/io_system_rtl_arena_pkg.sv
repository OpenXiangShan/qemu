// SPDX-License-Identifier: Apache-2.0
`ifdef IO_SYSTEM_RTL_ARENA
`include "axi/typedef.svh"
package io_system_rtl_arena_pkg;
    typedef logic [63:0] addr_t;
    typedef logic [63:0] data64_t;
    typedef logic [511:0] data512_t;
    typedef logic [7:0] strb64_t;
    typedef logic [63:0] strb512_t;
    typedef logic [3:0] id4_t;
    typedef logic [7:0] id8_t;
    typedef logic [9:0] id10_t;
    typedef logic [15:0] id16_t;
    typedef logic user_t;
    `AXI_TYPEDEF_ALL(a64i16, addr_t, id16_t, data64_t, strb64_t, user_t)
    `AXI_TYPEDEF_ALL(a64i10, addr_t, id10_t, data64_t, strb64_t, user_t)
    `AXI_TYPEDEF_ALL(a64i8, addr_t, id8_t, data64_t, strb64_t, user_t)
    `AXI_TYPEDEF_ALL(a64i4, addr_t, id4_t, data64_t, strb64_t, user_t)
    `AXI_TYPEDEF_ALL(a512i8, addr_t, id8_t, data512_t, strb512_t, user_t)
    `AXI_TYPEDEF_ALL(a512i4, addr_t, id4_t, data512_t, strb512_t, user_t)
endpackage
`endif
