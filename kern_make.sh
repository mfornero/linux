#!/bin/bash
ROOTFS_DIR=/Working/rootfs
ROOTFS_STAGING=${ROOTFS_DIR}/_rootfs

# Setup the .config file
make ARCH=arm xilinx_zynq_adt_defconfig
# Build and install the kernel and modules
make -j4 ARCH=arm CROSS_COMPILE=arm-none-linux-gnueabi- zImage modules 
make ARCH=arm CROSS_COMPILE=arm-none-linux-gnueabi INSTALL_MOD_PATH=${ROOTFS_STAGING} modules_install
# Remove the symlinks
for D in `ls ${ROOTFS_STAGING}/lib/modules/`; do
	rm ${ROOTFS_STAGING}/lib/modules/${D}/source
	rm ${ROOTFS_STAGING}/lib/modules/${D}/build
done
# Build the device tree
./scripts/dtc/dtc -I dts -O dtb -o ${ROOTFS_DIR}/misc/devicetree.dtb arch/arm/boot/dts/zynq-zed-adt.dts
