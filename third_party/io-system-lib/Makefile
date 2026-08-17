CC ?= gcc
CXX ?= g++
AR ?= ar
CFLAGS ?= -O2 -g -Wall -Wextra -std=gnu11
CXXFLAGS ?= -O2 -g -Wall -Wextra -std=c++17 -fPIC
QEMU_CFLAGS ?=
QEMU_CFLAGS_LIST ?=
IO_SYSTEM_QEMU_BACKEND ?= 0
IO_SYSTEM_RTL_SYSTEM ?= 0
IO_SYSTEM_IOMMU_RTL ?= 0
BUILD_DIR ?= build
OUTPUT_DIR ?= output
IO_SYSTEM_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BUILD_PATHS_FILE ?= $(IO_SYSTEM_ROOT)/config/paths.mk
include $(BUILD_PATHS_FILE)

IOMMU_REFMODEL_DIR := src/cmodel/iommu_refmodel
IOMMU_REFMODEL_LIB_SRCS := $(sort $(wildcard $(IOMMU_REFMODEL_DIR)/libiommu/src/*.c))
IOMMU_REFMODEL_SRCS := \
	$(IOMMU_REFMODEL_DIR)/src/iommu_refmodel_api.c \
	$(IOMMU_REFMODEL_LIB_SRCS)
IOMMU_REFMODEL_HEADERS := \
	$(wildcard $(IOMMU_REFMODEL_DIR)/include/*.h) \
	$(wildcard $(IOMMU_REFMODEL_DIR)/libiommu/include/*.h)
IOMMU_RTL_STAGE_DIR := $(OUTPUT_DIR)/iommu-rtl
IOMMU_RTL_STAGE_STAMP := $(IOMMU_RTL_STAGE_DIR)/.iommu-rtl-api.ready.stamp
IOMMU_RTL_API_SO_SOURCE := $(IOMMU_RTL_API_DIR)/lib/libiommu_api.so
IOMMU_RTL_PICKER_LIBS := \
	$(IOMMU_RTL_PICKER_OUT)/libUTiommu_wrap.so \
	$(IOMMU_RTL_PICKER_OUT)/libDPIiommu_wrap.so \
	$(IOMMU_RTL_PICKER_OUT)/libxspcomm.so
MY_VIRTIO_SOURCE_BUILD_DIR := $(abspath $(MY_VIRTIO_LIB_DIR)/build)
MY_VIRTIO_SOURCE_OUTPUT_DIR := $(abspath $(MY_VIRTIO_LIB_DIR)/output)
MY_VIRTIO_OUTPUT_DIR := $(abspath $(BUILD_DIR)/my-virtio-lib/output)
MY_VIRTIO_STAGE_STAMP := $(MY_VIRTIO_OUTPUT_DIR)/.stage.ready.stamp
RTL_SYSTEM_RTL_DIR := src/rtl_model/io_system/rtl
RTL_SYSTEM_RTL_FILE := $(RTL_SYSTEM_RTL_DIR)/io_system_rtl_wrapper.sv
RTL_SYSTEM_FILELIST_SOURCE := $(RTL_SYSTEM_RTL_DIR)/io_system_rtl_filelist.f
RTL_SYSTEM_FILELIST := $(BUILD_DIR)/rtl-system-picker/io_system_rtl_filelist.f
RTL_SYSTEM_RTL_SRCS := \
	src/rtl_model/io_system/rtl/aplic/vendor/axi_pkg.sv \
	src/rtl_model/io_system/rtl/aplic/vendor/ariane_axi_pkg.sv \
	src/rtl_model/io_system/rtl/aplic/vendor/reg_intf_pkg.sv \
	src/rtl_model/io_system/rtl/aplic/common/axi4_lite_write_master.sv \
	src/rtl_model/io_system/rtl/aplic/scalable/aplic_regmap.sv \
	src/rtl_model/io_system/rtl/aplic/scalable/aplic_domain_gateway.sv \
	src/rtl_model/io_system/rtl/aplic/scalable/aplic_domain_notifier.sv \
	src/rtl_model/io_system/rtl/aplic/scalable/aplic_domain_regctl.sv \
	src/rtl_model/io_system/rtl/aplic/scalable/aplic_domain_top.sv \
	$(RTL_SYSTEM_RTL_DIR)/rtl_mem2mem_dmac.sv \
	$(RTL_SYSTEM_RTL_DIR)/virtio_gbus_mmio_blk_top.sv \
	$(RTL_SYSTEM_RTL_DIR)/io_system_rtl_aplic_msi.sv \
	$(RTL_SYSTEM_RTL_DIR)/io_system_rtl_gbus_shadow.sv \
	$(RTL_SYSTEM_RTL_DIR)/io_system_rtl_mmio_mux.sv \
	$(RTL_SYSTEM_RTL_DIR)/io_system_rtl_wrapper.sv
RTL_SYSTEM_PICKER_OUT ?= $(OUTPUT_DIR)/rtl-system-picker
RTL_SYSTEM_PICKER_STAMP := $(RTL_SYSTEM_PICKER_OUT)/.picker-vcs.ready.stamp
RTL_SYSTEM_API_SO := $(OUTPUT_DIR)/libio_rtl_system_api.so
ifeq ($(IO_SYSTEM_RTL_SYSTEM),1)
RTL_SYSTEM_TEST_ENV = LD_LIBRARY_PATH="$(abspath $(RTL_SYSTEM_PICKER_OUT)):$(VCS_LIBDIR):$$LD_LIBRARY_PATH" LD_PRELOAD="$(abspath $(RTL_SYSTEM_PICKER_OUT))/libDPIio_system_rtl_wrapper.so:$(abspath $(RTL_SYSTEM_PICKER_OUT))/libUTio_system_rtl_wrapper.so"
else
RTL_SYSTEM_TEST_ENV =
endif

INCLUDES := -Iinclude -Isrc/core -Isrc/scheduler -Isrc/cmodel -Isrc/iommu \
	-Isrc/cmodel/include \
	-I$(IOMMU_REFMODEL_DIR)/include -I$(IOMMU_REFMODEL_DIR)/libiommu/include \
	-Isrc/rtl_model/io_system/include \
	-I$(MY_VIRTIO_OUTPUT_DIR) -I$(MY_VIRTIO_LIB_DIR)/virtio -I$(MY_VIRTIO_LIB_DIR)/backend
CFLAGS += -DIO_SYSTEM_QEMU_BACKEND=$(IO_SYSTEM_QEMU_BACKEND)
QEMU_CFLAGS += $(subst |, ,$(QEMU_CFLAGS_LIST))
CORE_SRCS := \
	src/core/io_system.c \
	src/core/io_system_backend.c
IOMMU_SRCS := \
	src/iommu/io_iommu.c \
	src/iommu/io_iommu_api_backend.c \
	src/iommu/io_iommu_cmodel.c \
	src/iommu/io_iommu_rtl.c
SCHEDULER_SRCS := \
	src/scheduler/io_scheduler.c
CMODEL_DEVICE_SRCS := \
	src/cmodel/io_cmodel_devices.c \
	src/cmodel/devices/aplic/io_aplic.c \
	src/cmodel/devices/dmac/io_dwc_dmac.c \
	src/cmodel/devices/virtio/io_cmodel_virtio_blk.c
MANIFEST_SRCS := \
	src/manifest/io_manifest.c
MODEL_SRCS := \
	src/cmodel/io_backend_cmodel.c \
	src/cmodel/bus/io_cmodel_mmio.c \
	src/rtl_model/io_system/io_backend_rtl_system.c \
	src/test_model/rtl_template/io_backend_rtl_template.c
ifeq ($(IO_SYSTEM_QEMU_BACKEND),1)
MODEL_SRCS += \
	src/cmodel/adapters/qemu/io_dwc_pcie_qemu.c
endif
RTL_SYSTEM_SRCS := \
	src/rtl_model/io_system/runtime/io_rtl_system_api.cpp \
	src/rtl_model/io_system/runtime/io_rtl_system_sim.cpp
RTL_SYSTEM_GEN_SRCS := \
	$(RTL_SYSTEM_PICKER_OUT)/UT_io_system_rtl_wrapper.cpp
RTL_SYSTEM_GEN_OBJS := \
	$(BUILD_DIR)/rtl-system-picker/UT_io_system_rtl_wrapper.o
TEST_SRCS := \
	tests/io_scheduler_smoke.c \
	tests/aplic_smoke.c \
	tests/dwc_dmac_smoke.c \
	tests/dispatch_smoke.c \
	tests/my_virtio_blk_smoke.c \
	tests/manifest_smoke.c \
	tests/dma_api_smoke.c \
	tests/iommu_cmodel_smoke.c \
	tests/iommu_external_smoke.c \
	tests/rtl_template_smoke.c
ifeq ($(IO_SYSTEM_RTL_SYSTEM),1)
TEST_SRCS += tests/rtl_system_smoke.c
endif

CORE_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(CORE_SRCS))
IOMMU_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(IOMMU_SRCS))
IOMMU_REFMODEL_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(IOMMU_REFMODEL_SRCS))
CMODEL_DEVICE_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(CMODEL_DEVICE_SRCS))
MANIFEST_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(MANIFEST_SRCS))
SCHEDULER_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SCHEDULER_SRCS))
LIBIO_SYSTEM_OBJS := $(CORE_OBJS) $(SCHEDULER_OBJS) $(IOMMU_OBJS) \
	$(IOMMU_REFMODEL_OBJS) $(CMODEL_DEVICE_OBJS) $(MANIFEST_OBJS)
MODEL_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(MODEL_SRCS))
RTL_SYSTEM_OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(RTL_SYSTEM_SRCS))
TEST_BINS := $(patsubst tests/%.c,$(BUILD_DIR)/tests/%,$(TEST_SRCS))
IOMMU_STUB_API_SO := $(BUILD_DIR)/tests/libiommu_stub_api.so

TARGET := $(OUTPUT_DIR)/libio_system.a
MODEL_TARGET := $(OUTPUT_DIR)/libio_system_backend.a
ifeq ($(IO_SYSTEM_RTL_SYSTEM),1)
RTL_SYSTEM_TARGETS := $(MY_VIRTIO_STAGE_STAMP) $(RTL_SYSTEM_API_SO)
else
RTL_SYSTEM_TARGETS :=
endif
ifeq ($(IO_SYSTEM_IOMMU_RTL),1)
IOMMU_RTL_TARGETS := $(IOMMU_RTL_STAGE_STAMP)
else
IOMMU_RTL_TARGETS :=
endif

.PHONY: all clean test iommu-rtl-api

all: $(TARGET) $(MODEL_TARGET) $(RTL_SYSTEM_TARGETS) $(IOMMU_RTL_TARGETS)

iommu-rtl-api: $(IOMMU_RTL_STAGE_STAMP)

$(TARGET): $(LIBIO_SYSTEM_OBJS) $(MY_VIRTIO_STAGE_STAMP)
	@mkdir -p $(OUTPUT_DIR)
	@rm -f $@
	$(AR) rcs $@ $(LIBIO_SYSTEM_OBJS)

$(MODEL_TARGET): $(MODEL_OBJS) $(MY_VIRTIO_STAGE_STAMP)
	@mkdir -p $(OUTPUT_DIR)
	@rm -f $@ $@.mri
	@{ \
		echo "create $@"; \
		for obj in $(MODEL_OBJS); do echo "addmod $$obj"; done; \
		echo "addlib $(abspath $(MY_VIRTIO_OUTPUT_DIR))/libMyVirtio.a"; \
		echo "addlib $(abspath $(MY_VIRTIO_OUTPUT_DIR))/libMyVirtio_backend.a"; \
		echo "save"; \
		echo "end"; \
	} > $@.mri
	$(AR) -M < $@.mri

$(BUILD_DIR)/%.o: %.c $(wildcard include/*.h) $(wildcard src/core/*.h) \
		$(wildcard src/cmodel/*.h) $(wildcard src/iommu/*.h) \
		$(wildcard src/cmodel/include/*.h) \
		$(IOMMU_REFMODEL_HEADERS) \
		$(MY_VIRTIO_STAGE_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(QEMU_CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/src/cmodel/iommu_refmodel/%.o: CFLAGS += \
	-Wno-unused-parameter -Wno-implicit-fallthrough -Wno-uninitialized

$(IOMMU_RTL_STAGE_STAMP): $(IOMMU_RTL_API_SO_SOURCE) \
		$(IOMMU_RTL_PICKER_LIBS) $(BUILD_PATHS_FILE) Makefile
	@mkdir -p $(IOMMU_RTL_STAGE_DIR)
	cp -f $(IOMMU_RTL_API_SO_SOURCE) $(IOMMU_RTL_STAGE_DIR)/
	cp -f $(IOMMU_RTL_PICKER_LIBS) $(IOMMU_RTL_STAGE_DIR)/
	@if ls $(IOMMU_RTL_PICKER_OUT)/*.so.daidir >/dev/null 2>&1; then \
		cp -a $(IOMMU_RTL_PICKER_OUT)/*.so.daidir $(IOMMU_RTL_STAGE_DIR)/; \
	fi
	@touch $@

$(RTL_SYSTEM_FILELIST): $(RTL_SYSTEM_FILELIST_SOURCE) Makefile
	@mkdir -p $(dir $@)
	cp -f $< $@

$(RTL_SYSTEM_PICKER_STAMP): $(RTL_SYSTEM_FILELIST) $(RTL_SYSTEM_RTL_SRCS) \
		src/rtl_model/io_system/scripts/build-picker-vcs.sh
	bash src/rtl_model/io_system/scripts/build-picker-vcs.sh \
		$(RTL_SYSTEM_FILELIST) \
		$(RTL_SYSTEM_PICKER_OUT) \
		$(PICKER) \
		$(PICKER_TEMPLATE) \
		$(PICKER_XSPCOMM_LIB)

$(RTL_SYSTEM_PICKER_OUT)/UT_io_system_rtl_wrapper.cpp: $(RTL_SYSTEM_PICKER_STAMP)
	@:

$(BUILD_DIR)/%.o: %.cpp $(RTL_SYSTEM_PICKER_STAMP) \
		$(wildcard src/rtl_model/io_system/include/*.h) \
		$(wildcard src/rtl_model/io_system/runtime/*.hpp)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -I$(RTL_SYSTEM_PICKER_OUT) -I$(PICKER_XSPCOMM_INCLUDE) -I$(VCS_INCLUDE) -c $< -o $@

$(BUILD_DIR)/rtl-system-picker/%.o: $(RTL_SYSTEM_PICKER_OUT)/%.cpp $(RTL_SYSTEM_PICKER_STAMP) Makefile
	@mkdir -p $(dir $@)
	$(CXX) -O3 -g -Wall -Wextra -std=gnu++20 -fcoroutines -fPIC -DUSE_VCS \
		-I$(RTL_SYSTEM_PICKER_OUT) \
		-I$(RTL_SYSTEM_PICKER_OUT)/build \
		-I$(PICKER_XSPCOMM_INCLUDE) \
		-I$(VCS_INCLUDE) \
		-c $< -o $@

$(MY_VIRTIO_STAGE_STAMP): $(MY_VIRTIO_LIB_DIR)/Makefile \
		$(wildcard $(MY_VIRTIO_LIB_DIR)/virtio/*) \
		$(wildcard $(MY_VIRTIO_LIB_DIR)/backend/*) \
		$(BUILD_PATHS_FILE)
	$(MAKE) -C $(MY_VIRTIO_LIB_DIR) \
		IO_SYSTEM_DIR=$(IO_SYSTEM_ROOT) \
		BUILD_DIR=$(MY_VIRTIO_SOURCE_BUILD_DIR) \
		OUTPUT_DIR=$(MY_VIRTIO_SOURCE_OUTPUT_DIR) \
		BACKEND_BLK=y BACKEND_NET=n BACKEND_CONSOLE=n BACKEND_GPU=n BACKEND_INPUT=n BACKEND_UI_VNC=n \
		all
	@mkdir -p $(MY_VIRTIO_OUTPUT_DIR)
	cp -f $(MY_VIRTIO_SOURCE_OUTPUT_DIR)/libMyVirtio.a $(MY_VIRTIO_OUTPUT_DIR)/
	cp -f $(MY_VIRTIO_SOURCE_OUTPUT_DIR)/libMyVirtio_backend.a $(MY_VIRTIO_OUTPUT_DIR)/
	cp -f $(MY_VIRTIO_SOURCE_OUTPUT_DIR)/virtio_backend.h $(MY_VIRTIO_OUTPUT_DIR)/
	cp -f $(MY_VIRTIO_SOURCE_OUTPUT_DIR)/virtio_wrapper.h $(MY_VIRTIO_OUTPUT_DIR)/
	@touch $@

$(RTL_SYSTEM_API_SO): $(RTL_SYSTEM_OBJS) $(RTL_SYSTEM_GEN_OBJS) $(RTL_SYSTEM_PICKER_STAMP) $(MY_VIRTIO_STAGE_STAMP) Makefile
	@mkdir -p $(OUTPUT_DIR)
	$(CXX) -shared -o $@ $(RTL_SYSTEM_OBJS) $(RTL_SYSTEM_GEN_OBJS) \
		$(abspath $(MY_VIRTIO_OUTPUT_DIR))/libMyVirtio.a \
		$(abspath $(MY_VIRTIO_OUTPUT_DIR))/libMyVirtio_backend.a \
		-L$(RTL_SYSTEM_PICKER_OUT) \
		-Wl,-rpath,'$$ORIGIN' \
		-Wl,-rpath,'$$ORIGIN/rtl-system-picker' \
		-lUTio_system_rtl_wrapper \
		-lDPIio_system_rtl_wrapper \
		-lxspcomm \
		-lm -lc -lpthread -lnuma -ldl

$(BUILD_DIR)/tests/%: tests/%.c $(TARGET) $(MODEL_TARGET)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(TARGET) $(MODEL_TARGET) -ldl -o $@

$(IOMMU_STUB_API_SO): tests/iommu_stub_api.c src/iommu/io_iommu_api_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fPIC -shared -Isrc/iommu $< -o $@

$(BUILD_DIR)/tests/iommu_external_smoke: tests/iommu_external_smoke.c \
		$(TARGET) $(MODEL_TARGET) $(IOMMU_STUB_API_SO)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) \
		-DIO_IOMMU_STUB_API_SO=\"$(abspath $(IOMMU_STUB_API_SO))\" \
		$< $(TARGET) $(MODEL_TARGET) -ldl -o $@

test: all $(TEST_BINS)
	@for test_bin in $(TEST_BINS); do \
		$(RTL_SYSTEM_TEST_ENV) \
		IO_RTL_SYSTEM_API_SO="$(abspath $(RTL_SYSTEM_API_SO))" \
		IO_RTL_SYSTEM_VCS_LIBDIR="$(VCS_LIBDIR)" \
		"$$test_bin"; \
	done

clean:
	rm -rf $(BUILD_DIR) $(OUTPUT_DIR)
