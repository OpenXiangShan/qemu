riscv_ss = ss.source_set()
if get_option('io_system').enabled()
  riscv_ss.add(io_system_backend)
endif
riscv_ss.add(files('boot.c'))
riscv_ss.add(when: 'CONFIG_RISCV_NUMA', if_true: files('numa.c'))
riscv_ss.add(files('riscv_hart.c'))
riscv_ss.add(when: 'CONFIG_OPENTITAN', if_true: files('opentitan.c'))
riscv_ss.add(when: 'CONFIG_RISCV_VIRT', if_true: files('virt.c'))
riscv_ss.add(when: 'CONFIG_SHAKTI_C', if_true: files('shakti_c.c'))
riscv_ss.add(when: 'CONFIG_SIFIVE_E', if_true: files('sifive_e.c'))
riscv_ss.add(when: 'CONFIG_SIFIVE_U', if_true: files('sifive_u.c'))
riscv_ss.add(when: 'CONFIG_SPIKE', if_true: files('spike.c'))
riscv_ss.add(when: 'CONFIG_MICROCHIP_PFSOC', if_true: files('microchip_pfsoc.c'))
riscv_ss.add(when: 'CONFIG_ACPI', if_true: files('virt-acpi-build.c', 'xiangshan_kmh_acpi.c', 'kmh_bosc_acpi.c'))
riscv_ss.add(when: 'CONFIG_RISCV_IOMMU', if_true: files(
	'riscv-iommu.c', 'riscv-iommu-pci.c', 'riscv-iommu-sys.c', 'riscv-iommu-hpm.c'))
riscv_ss.add(when: 'CONFIG_MICROBLAZE_V', if_true: files('microblaze-v-generic.c'))
riscv_ss.add(when: 'CONFIG_KMH_BOSC_SOC', if_true: files('kmh_bosc_soc.c'))
riscv_ss.add(when: 'CONFIG_XIANGSHAN_KUNMINGHU', if_true: files('xiangshan_kmh.c'))
riscv_ss.add(when: 'CONFIG_QEMU_TO_IOSYSTEM',
             if_true: files('qemu_to_iosystem.c'))
riscv_ss.add(when: 'CONFIG_XIANGSHAN_KUNMINGHU_COSIM',
             if_true: files('xiangshan_kmh_cosim.c'))
riscv_ss.add(when: 'CONFIG_XIANGSHAN_NANHUV5', if_true: files('xiangshan_nhv5.c'))
hw_arch += {'riscv': riscv_ss}
