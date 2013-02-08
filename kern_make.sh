#!/bin/bash
PARBUILD=-j8

if [ x${1} = x ]; then
	BUILDARCH=arm
else
	BUILDARCH=$1
fi

if [ x${2} = x ]; then
	LOCAL_VER=
else
	LOCAL_VER=$2
fi

case $BUILDARCH in
	arm)
		CC_TOOL=arm-none-linux-gnueabi-	
		KERN=zImage
		DEFCONFIG=xilinx_zynq_adt_defconfig
	;;
	x86_64)
		CC_TOOL=x86_64-corei7-linux-gnu-
		KERN=bzImage
		DEFCONFIG=x86_64_adt_defconfig
	;;
	*)
	echo "Unknown architecture!"
	exit
	;;
esac

# Setup the .config file
make ARCH=${BUILDARCH} ${DEFCONFIG}

if [ x${LOCAL_VER} != x ]; then
	echo "Setting local version to ${LOCAL_VER}"
	./scripts/config --set-str CONFIG_LOCALVERSION ${LOCAL_VER}
fi

# Build and install the kernel and modules
make ${PARBUILD} ARCH=${BUILDARCH} CROSS_COMPILE=${CC_TOOL} ${KERN} modules 
