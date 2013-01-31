#!/bin/bash
ROOTFS_DIR=/Working/rootfs
ROOTFS_STAGING=${ROOTFS_DIR}/_rootfs

if [ x${1} = x ]; then
	BUILDARCH=arm
else
	BUILDARCH=$1
fi

case $BUILDARCH in
	arm)
		CC_TOOL=arm-none-linux-gnueabi-	
		KERN=zImage
		DEFCONFIG=xilinx_zynq_adt_defconfig
	;;
	i386)
		CC_TOOL=i686-pc-linux-gnu-
		KERN=bzImage
		DEFCONFIG=i386_adt_defconfig
	;;
	*)
	echo "Unknown architecture!"
	exit
	;;
esac

# Setup the .config file
make ARCH=${BUILDARCH} ${DEFCONFIG}
# Build and install the kernel and modules
make -j4 ARCH=${BUILDARCH} CROSS_COMPILE=${CC_TOOL} ${KERN} modules 
#make ARCH=${BUILDARCH} CROSS_COMPILE=${CC_TOOL} INSTALL_MOD_PATH=${ROOTFS_STAGING} modules_install
# Remove the symlinks
#for D in `ls ${ROOTFS_STAGING}/lib/modules/`; do
#	rm ${ROOTFS_STAGING}/lib/modules/${D}/source
#	rm ${ROOTFS_STAGING}/lib/modules/${D}/build
#done

#if [ $BUILDARCH = arm ]; then
#	# Build the device tree
#	./scripts/dtc/dtc -I dts -O dtb -o ${ROOTFS_DIR}/misc/devicetree.dtb arch/arm/boot/dts/zynq-zed-adt.dts
#fi
