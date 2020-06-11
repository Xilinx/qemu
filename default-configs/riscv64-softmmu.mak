# Default configuration for riscv64-softmmu

# Uncomment the following lines to disable these optional devices:
#
#CONFIG_PCI_DEVICES=n

# Boards:
#
CONFIG_SPIKE=y
CONFIG_SIFIVE_E=y
CONFIG_SIFIVE_U=y
CONFIG_RISCV_VIRT=y

# Xilinx
CONFIG_SSI=y
CONFIG_I2C=y
CONFIG_XILINX_AXI=y
CONFIG_XILINX_SPI=y
CONFIG_XILINX_SPIPS=y
CONFIG_PTIMER=y
CONFIG_CADENCE=y
CONFIG_SI57X=y
CONFIG_REMOTE_PORT=y
