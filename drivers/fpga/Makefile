#
# Makefile for the fpga framework and fpga manager drivers.
#

# Core FPGA Manager Framework
obj-$(CONFIG_FPGA)			+= fpga-mgr.o

# Overlay Drivers
obj-$(CONFIG_FPGA_OVERLAY) 			+= fpga-overlay.o

# FPGA Manager Drivers
obj-$(CONFIG_FPGA_MGR_SOCFPGA)		+= socfpga.o
obj-$(CONFIG_FPGA_MGR_ZYNQ_FPGA)	+= zynq-fpga.o
obj-$(CONFIG_FPGA_MGR_ZYNQMP_FPGA)	+= zynqmp-fpga.o

obj-y				+= bridge/
# FPGA Bridge Drivers
obj-$(CONFIG_FPGA_BRIDGE)		+= fpga-bridge.o
obj-$(CONFIG_XILINX_PR_DECOUPLER)	+= xilinx-pr-decoupler.o

# High Level Interfaces
obj-$(CONFIG_FPGA_REGION)		+= fpga-region.o
