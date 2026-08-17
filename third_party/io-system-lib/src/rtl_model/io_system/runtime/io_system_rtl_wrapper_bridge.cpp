// SPDX-License-Identifier: Apache-2.0
//
// Keep one translation unit that depends on picker-generated headers so the
// Makefile tracks picker include availability.  The UT implementation itself
// is linked from libUTio_system_rtl_wrapper.so; do not include the generated
// .cpp here, otherwise the process gets two copies of the wrapper symbols.

#include "UT_io_system_rtl_wrapper.hpp"
