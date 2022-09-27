# core qdev-related obj files, also used by *-user and unit tests
hwcore_files = files(
  'bus.c',
  'hotplug.c',
  'qdev-properties.c',
  'qdev.c',
  'reset.c',
  'resettable.c',
  'vmstate-if.c',
  # irq.c needed for qdev GPIO handling:
  'irq.c',
  'clock.c',
  'qdev-clock.c',
)

common_ss.add(files('cpu-common.c'))
common_ss.add(when: 'CONFIG_FITLOADER', if_true: files('loader-fit.c'))
common_ss.add(when: 'CONFIG_GENERIC_LOADER', if_true: files('generic-loader.c'))
common_ss.add(when: ['CONFIG_GUEST_LOADER', fdt], if_true: files('guest-loader.c'))
common_ss.add(when: 'CONFIG_OR_IRQ', if_true: files('or-irq.c'))
common_ss.add(when: 'CONFIG_PLATFORM_BUS', if_true: files('platform-bus.c'))
common_ss.add(when: 'CONFIG_PTIMER', if_true: files('ptimer.c'))
common_ss.add(when: 'CONFIG_REGISTER', if_true: files('register.c'))
common_ss.add(when: 'CONFIG_SPLIT_IRQ', if_true: files('split-irq.c'))
common_ss.add(when: 'CONFIG_XILINX_AXI', if_true: files('stream.c'))

common_ss.add(when: 'CONFIG_XILINX_AXI', if_true: files(
  'dma-ctrl.c',
  'fdt_generic_devices_xilinx_axi.c'))

common_ss.add(when: 'CONFIG_XILINX_SPIPS', if_true: files('fdt_generic_devices_xilinx_spips.c'))
common_ss.add(when: 'CONFIG_XLNX_ZYNQMP', if_true: files('fdt_generic_devices_zynqmp.c'))
common_ss.add(when: 'CONFIG_CADENCE', if_true: files('fdt_generic_devices_cadence.c'))
common_ss.add(when: 'CONFIG_SERIAL', if_true: files('fdt_generic_devices_serial.c'))
common_ss.add(when: 'CONFIG_PFLASH_CFI01', if_true: files('fdt_generic_devices_flash.c'))
common_ss.add(when: 'CONFIG_SSI_M25P80', if_true: files('fdt_generic_devices_ssi_m25p80.c'))
common_ss.add(when: 'CONFIG_SI57X', if_true: files('fdt_generic_devices_si57x.c'))
common_ss.add(when: 'CONFIG_SI5341', if_true: files('fdt_generic_devices_si5341.c'))
common_ss.add(when: 'CONFIG_USB_EHCI_SYSBUS', if_true: files('fdt_generic_devices_usb_ehci_sysbus.c'))

softmmu_ss.add(files(
  'fdt_generic.c',
  'fdt_generic_devices.c',
  'fdt_generic_util.c',
  'fdt_generic_user_creatable.c',
  ))

specific_ss.add(when: 'CONFIG_REMOTE_PORT', if_true: files(
  'remote-port-proto.c',
  'remote-port.c',
  'remote-port-memory-master.c',
  'remote-port-memory-slave.c',
  'remote-port-gpio.c',
  'remote-port-stream.c',
  'remote-port-net.c',
  'remote-port-ats.c',
))

specific_ss.add(when: 'CONFIG_REMOTE_PORT_PCI', if_true: files(
  'remote-port-pci-adaptor.c',
  'remote-port-pci-device.c',
  'remote-port-pcie-root-port.c',
))

specific_ss.add(when: 'CONFIG_SOFTMMU', if_true: files(
  'cpu-exec-gpio.c',
  'remote-port-qdev.c'
))

softmmu_ss.add(files(
  'cpu-sysemu.c',
  'fw-path-provider.c',
  'loader.c',
  'machine-hmp-cmds.c',
  'machine.c',
  'nmi.c',
  'null-machine.c',
  'qdev-fw.c',
  'qdev-properties-system.c',
  'sysbus.c',
  'vm-change-state-handler.c',
  'clock-vmstate.c',
))

specific_ss.add(when: 'CONFIG_SOFTMMU', if_true: files(
  'machine-qmp-cmds.c',
  'numa.c',
))
