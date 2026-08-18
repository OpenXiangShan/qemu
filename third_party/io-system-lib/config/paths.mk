# External tool and repository paths for io-system-lib.
#
# Edit this file when moving the project to a different machine, or override
# any of the variables from the command line or environment.

IO_SYSTEM_PARENT_DIR := $(notdir $(patsubst %/,%,$(dir $(IO_SYSTEM_ROOT))))
ifeq ($(IO_SYSTEM_PARENT_DIR),third_party)
WORKSPACE_ROOT ?= $(abspath $(IO_SYSTEM_ROOT)/../../..)
else
WORKSPACE_ROOT ?= $(abspath $(IO_SYSTEM_ROOT)/..)
endif
MY_VIRTIO_LIB_DIR ?= $(abspath $(WORKSPACE_ROOT)/my-virtio-lib)
BOSC_IOMMU_DIR ?= $(abspath $(WORKSPACE_ROOT)/bosc-iommu-v2)
UNITYCHIP_ROOT ?= $(abspath $(BOSC_IOMMU_DIR)/third_party/unitychip)

VCS_HOME ?= /nfs/tools/synopsys/vcs/Q-2020.03-SP2
VCS_LIBDIR ?= $(VCS_HOME)/linux64/lib
VCS_INCLUDE ?= $(VCS_HOME)/include

PICKER ?= $(UNITYCHIP_ROOT)/bin/picker
PICKER_TEMPLATE ?= $(UNITYCHIP_ROOT)/share/picker/template
PICKER_XSPCOMM_INCLUDE ?= $(UNITYCHIP_ROOT)/share/picker/include
PICKER_XSPCOMM_LIB ?= $(UNITYCHIP_ROOT)/share/picker/lib/libxspcomm.so

IOMMU_RTL_PICKER_OUT ?= $(abspath $(BOSC_IOMMU_DIR)/picker/picker_out_iommu)
