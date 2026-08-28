CC := gcc
AR := ar
CMAKE := cmake
CFLAGS := -Wall -g -O0 -std=gnu99
GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0 2>/dev/null)
LIBVNCSERVER_DIR := third_party/libvncserver
LIBSLIRP_DIR := third_party/libslirp
BUILD_DIR ?= build
OUTPUT_DIR ?= output
LIBVNCSERVER_BUILD_DIR := $(BUILD_DIR)/libvncserver
LIBVNCSERVER_ARCHIVE := $(LIBVNCSERVER_BUILD_DIR)/libvncserver.a
LIBSLIRP_BUILD_DIR := $(BUILD_DIR)/libslirp
LIBSLIRP_ARCHIVE := $(LIBSLIRP_BUILD_DIR)/libslirp.a
BACKEND_MRI := $(BUILD_DIR)/libMyVirtio_backend.mri
LIBVNCSERVER_CFLAGS := -I$(LIBVNCSERVER_DIR)/include -I$(LIBVNCSERVER_BUILD_DIR)/include
LIBSLIRP_CFLAGS := -I$(LIBSLIRP_DIR)/src -I$(LIBSLIRP_BUILD_DIR)/src
LIBVNCSERVER_CMAKE_FLAGS := \
	-S $(LIBVNCSERVER_DIR) \
	-B $(LIBVNCSERVER_BUILD_DIR) \
	-DCMAKE_BUILD_TYPE=Release \
	-DBUILD_SHARED_LIBS=OFF \
	-DLIBVNCSERVER_INSTALL=OFF \
	-DWITH_LIBVNCSERVER=ON \
	-DWITH_LIBVNCCLIENT=OFF \
	-DWITH_THREADS=ON \
	-DWITH_24BPP=ON \
	-DWITH_IPv6=ON \
	-DWITH_ZLIB=OFF \
	-DWITH_LZO=OFF \
	-DWITH_JPEG=OFF \
	-DWITH_PNG=OFF \
	-DWITH_SDL=OFF \
	-DWITH_GTK=OFF \
	-DWITH_QT=OFF \
	-DWITH_LIBSSHTUNNEL=OFF \
	-DWITH_GNUTLS=OFF \
	-DWITH_OPENSSL=OFF \
	-DWITH_SYSTEMD=OFF \
	-DWITH_GCRYPT=OFF \
	-DWITH_FFMPEG=OFF \
	-DWITH_TIGHTVNC_FILETRANSFER=OFF \
	-DWITH_WEBSOCKETS=OFF \
	-DWITH_SASL=OFF \
	-DWITH_XCB=OFF \
	-DWITH_EXAMPLES=OFF \
	-DWITH_TESTS=OFF
VIRTIO_CFLAGS := -Ivirtio

# Backend module switches. Set any of these to n/0/false to omit that
# backend implementation from libMyVirtio_backend.a.
BACKEND_BLK ?= y
BACKEND_NET ?= n
BACKEND_CONSOLE ?= n
BACKEND_GPU ?= n
BACKEND_INPUT ?= n
BACKEND_UI_VNC ?= n

backend_enabled = $(filter 1 y yes true,$(strip $(1)))
BACKEND_DEFS := \
	-DVIRTIO_BACKEND_HAS_BLK=$(if $(call backend_enabled,$(BACKEND_BLK)),1,0) \
	-DVIRTIO_BACKEND_HAS_NET=$(if $(call backend_enabled,$(BACKEND_NET)),1,0) \
	-DVIRTIO_BACKEND_HAS_CONSOLE=$(if $(call backend_enabled,$(BACKEND_CONSOLE)),1,0) \
	-DVIRTIO_BACKEND_HAS_GPU=$(if $(call backend_enabled,$(BACKEND_GPU)),1,0) \
	-DVIRTIO_BACKEND_HAS_INPUT=$(if $(call backend_enabled,$(BACKEND_INPUT)),1,0) \
	-DVIRTIO_BACKEND_HAS_UI_VNC=$(if $(call backend_enabled,$(BACKEND_UI_VNC)),1,0)
BACKEND_CFLAGS := -Ibackend $(BACKEND_DEFS)

VIRTIO_SRCS := virtio/fifo.c virtio/utils.c virtio/virtio.c \
	virtio/virtio_blk.c virtio/virtio_console.c virtio/virtio_mmio.c \
	virtio/virtio_gbus.c \
	virtio/virtio_net.c virtio/virtio_gpu.c virtio/virtio_input.c \
	virtio/virtio_genirq.c \
	virtio/virtio_pci.c \
	virtio/virtio_wrapper.c
VIRTIO_HEADERS := $(wildcard virtio/*.h)
VIRTIO_OBJS := $(patsubst virtio/%.c,$(BUILD_DIR)/virtio/%.o,$(VIRTIO_SRCS))
BACKEND_SRCS := backend/virtio_backend.c backend/virtio_backend_queue.c \
	backend/virtio_backend_ui.c
BACKEND_ARCHIVES :=
ifneq ($(call backend_enabled,$(BACKEND_BLK)),)
BACKEND_SRCS += backend/virtio_backend_blk.c
endif
ifneq ($(call backend_enabled,$(BACKEND_NET)),)
BACKEND_SRCS += backend/virtio_backend_net.c
BACKEND_ARCHIVES += $(LIBSLIRP_ARCHIVE)
endif
ifneq ($(call backend_enabled,$(BACKEND_CONSOLE)),)
BACKEND_SRCS += backend/virtio_backend_console.c
endif
ifneq ($(call backend_enabled,$(BACKEND_GPU)),)
BACKEND_SRCS += backend/virtio_backend_gpu.c
endif
ifneq ($(call backend_enabled,$(BACKEND_INPUT)),)
BACKEND_SRCS += backend/virtio_backend_input.c
endif
ifneq ($(call backend_enabled,$(BACKEND_UI_VNC)),)
BACKEND_SRCS += backend/virtio_backend_ui_vnc.c
BACKEND_ARCHIVES += $(LIBVNCSERVER_ARCHIVE)
endif
BACKEND_ADDLIB_CMDS := $(foreach lib,$(abspath $(BACKEND_ARCHIVES)),echo "ADDLIB $(lib)";)
BACKEND_HEADERS := $(wildcard backend/*.h)
BACKEND_OBJS := $(patsubst backend/%.c,$(BUILD_DIR)/backend/%.backend.o,$(BACKEND_SRCS))
TARGET := $(OUTPUT_DIR)/libMyVirtio.a
BACKEND_TARGET := $(OUTPUT_DIR)/libMyVirtio_backend.a

.PHONY: all clean

all: $(TARGET) $(BACKEND_TARGET)

$(TARGET): $(VIRTIO_OBJS)
	@mkdir -p $(OUTPUT_DIR)
	$(AR) rcs $@ $^
	@cp virtio/virtio_wrapper.h $(OUTPUT_DIR)/

$(LIBVNCSERVER_ARCHIVE):
	$(CMAKE) $(LIBVNCSERVER_CMAKE_FLAGS)
	$(CMAKE) --build $(LIBVNCSERVER_BUILD_DIR) --target vncserver

$(LIBSLIRP_ARCHIVE):
	$(MAKE) -C $(LIBSLIRP_DIR) BUILD_DIR=$(abspath $(LIBSLIRP_BUILD_DIR)) \
		CC="$(CC)" AR="$(AR)" PKG_CONFIG=pkg-config

$(BACKEND_TARGET): $(BACKEND_OBJS) $(BACKEND_ARCHIVES)
	@mkdir -p $(OUTPUT_DIR) $(BUILD_DIR)
	rm -f $@ $(BACKEND_MRI)
	@{ \
		echo "CREATE $@"; \
		for obj in $(BACKEND_OBJS); do echo "ADDMOD $$obj"; done; \
		$(BACKEND_ADDLIB_CMDS) \
		echo "SAVE"; \
		echo "END"; \
	} > $(BACKEND_MRI)
	$(AR) -M < $(BACKEND_MRI)
	$(AR) s $@
	@cp backend/virtio_backend.h $(OUTPUT_DIR)/

$(BUILD_DIR)/virtio/%.o: virtio/%.c $(VIRTIO_HEADERS)
	@mkdir -p $(BUILD_DIR)/virtio
	$(CC) $(CFLAGS) $(VIRTIO_CFLAGS) -c $< -o $@

$(BUILD_DIR)/backend/%.backend.o: backend/%.c $(BACKEND_HEADERS)
	@mkdir -p $(BUILD_DIR)/backend
	$(CC) $(CFLAGS) $(BACKEND_CFLAGS) $(GLIB_CFLAGS) $(LIBSLIRP_CFLAGS) $(LIBVNCSERVER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/backend/virtio_backend_ui_vnc.backend.o: $(LIBVNCSERVER_ARCHIVE)
$(BUILD_DIR)/backend/virtio_backend_net.backend.o: $(LIBSLIRP_ARCHIVE)

clean:
	rm -rf $(BUILD_DIR)
	rm -f *.o *.backend.o virtio/*.o backend/*.backend.o
	rm -f $(TARGET) $(BACKEND_TARGET)
	rm -rf $(OUTPUT_DIR)
