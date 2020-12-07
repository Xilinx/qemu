====================
iomem-cache
====================

About
-----------------

The iomem-cache in QEMU is intended to operate in front of RAM memory
(DDR) modelled on the SystemC side when co-simulating QEMU and SystemC.
The cache lines in the cache are presented as small areas of RAM into
QEMU's memory subsystem resulting in an improved execution speed compared
to when executing without the cache from the same co-simulated SystemC
memory. The cache can be configured to act as an "invisible" cache
caching all accesses (the default that maximises execution speed) or to
respect the cacheable attribute of memory transactions (respecting
noncacheable accesses).

.. _____________________          ____________________
   |                    | remote |                    |
   | QEMU  ___________  |  port  |     _____  SystemC |
   |      [iomem-cache]-|--------|--->[ DDR ]         |
   ---------------------          --------------------


Configuration
--------------------------

The iomem-cache contain the following device properties:

- line-size (default 1024 bytes)
- cache-size (default 32 MiB)
- cache-all (default 1)

  - '0' will only cache cacheable memory (looking at memory attributes)
    and also enable a write buffer for noncacheable accesses

  - '1' handle all target memory as cacheable

|

Below is a configuration example of the properties in a Xilinx QEMU
hardware device tree [1] (the default values are chosen if left
out/unconfigured).

::

	iomem_cache: iomem_cache@0 {
		compatible = "iomem-cache";
		reg = <0x0 0x0 0x0 0x80000000 0x1>;
		mr = <&cci_mr>;
		line-size = <1024>;
		cache-size = <0x2000000>;
		cache-all = <0>;
	}


Scenarios / Use cases
---------------------

- QEMU Execution from (DDR) memories placed on the SystemC side

- QEMU co-simulation with AXI masters in SystemC with coherent
  noncacheable memory modelled on the SystemC side

- Basic traffic analysing on the traffic towards DDR when configured with
  ``cache-all = <0>``, the traffic will be a mix of cache line sized
  accesses and uncached (possibly) burst accesses

Limitations
-----------

- Emulation speed will not be as fast as when placing the RAM memory
  inside QEMU. For faster emulation speed a larger cache size and larger
  cache line sizes should be selected.


Command lines
-------------

Below is a sample QEMU command line using Xilinx's Versal machine and a
HW DTB file instantiating an iomem-cache (the HW DTB file can be found at
Xilinx hardware qemu-devicetrees.git [1]).

::

	$ qemu-system-aarch64 \
	-M arm-generic-fdt \
	-hw-dtb dts/LATEST/SINGLE_ARCH/board-versal-ps-cosim-virt-cache.dtb \
	-serial null -serial null -serial stdio -display none \
	-device loader,file=u-boot.elf \
	-device loader,file=bl31.elf,cpu-num=0 \
	-device loader,addr=0x40000000,file=Image \
	-device loader,addr=0x2000000,file=system.dtb \
	-device loader,addr=0xFD1A0300,data=0x8000000e,data-len=4 \
	-machine-path /tmp/qemu

The ``versal_demo`` in systemctlm-cosim-demo.git [2] places the DDR on the
SystemC side when compiled with ``DDR_IN_SYSTEMC=y`` and can be used with
the QEMU comand line above for testing the iomem-cache. Below is an
example command line launching the demo:

::

	$ ./versal_demo unix:/tmp/qemu/qemu-rport-_amba@0_cosim@0 10000


References
----------

[1] https://github.com/Xilinx/qemu-devicetrees.git

[2] https://github.com/Xilinx/systemctlm-cosim-demo.git
