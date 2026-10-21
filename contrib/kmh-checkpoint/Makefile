# SPDX-License-Identifier: GPL-2.0-or-later
CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Werror
CPPFLAGS ?=
LDLIBS ?= -lzstd
TOOL := kmh-zstd-unpack

.PHONY: all bin clean
all: $(TOOL)

$(TOOL): kmh-zstd-unpack.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDLIBS) -o $@

# Usage:
#   make bin zstd=/absolute/path/checkpoint.zst
# The result is a sparse raw RAM image beside the input: checkpoint.bin.
bin: $(TOOL)
	@test -n "$(zstd)" || (echo 'usage: make bin zstd=/path/checkpoint.zst' >&2; exit 2)
	@out='$(basename $(zstd)).bin'; \
	  ./$(TOOL) "$(zstd)" "$$out"

clean:
	rm -f $(TOOL)
