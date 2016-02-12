#!/bin/bash

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
QEMU=$(dirname ${SCRIPT_DIR})/qemu/build/x86_64-softmmu/qemu-system-x86_64
BR2=$(dirname ${SCRIPT_DIR})/buildroot
if [ "$RUNNING_IN_NEW_XTERM" != t ] ; then
        RUNNING_IN_NEW_XTERM=t exec xterm -hold -e "$0 $*"
fi

MW_DBG_LEVEL=3
INTC_DBG_LEVEL=1
DMA_DBG_LEVEL=4
MSI_EN=1
INTR_PRD_US=$((1000*1000*10))
#MODE="kern-only"
MODE="disk"
INTC_TYPE="xilinx"
INTC_FLAGS=",intc_flags1=0xffffffff,intc_nirq=2"

DEV_OPTS=",debug=${MW_DBG_LEVEL},intc_debug=${INTC_DBG_LEVEL},dma_debug=${DMA_DBG_LEVEL}"
DEV_OPTS="${DEV_OPTS},msi=${MSI_EN},intr_period_us=${INTR_PRD_US},intc_type=${INTC_TYPE}${INTC_FLAGS}"

NETDEV="-device e1000,netdev=network0"

#DEVICES="-device pci-mwdev${DEV_OPTS} -device pci-mwdev${DEV_OPTS}"
DEVICES="-enable-kvm -device vfio-pci,host=02:00.0"

CPU_SPEC="-cpu host -vga none"

KERNEL_IMG="-kernel ${SCRIPT_DIR}/arch/x86_64/boot/bzImage"
DISK_IMG=${BR2}/output/images/qemu.img.raw
#KERNEL_IMG="-kernel ${BR2}/output/images/bzImage"
#DISK_OPTS="/dev/zero"
#DISK_OPTS="-drive file=${BR2}/output/qemu.img.raw,if=scsi"

if [ $MODE == "kern-only" ]; then
	KERNEL_CMD='console=ttyS0 root=/dev/sda rootwait loglevel=8 dyndbg="file mwgeneric_of.c +p"'
else
	KERNEL_CMD="console=ttyS0 root=/dev/sda1 rw rootwait cma=256M@0-4G"
	DISK_OPTS="-drive file=${DISK_IMG},if=none,format=raw,id=mydisk"
	DEVICES="${DEVICES} -device ich9-ahci,id=ahci -device ide-drive,drive=mydisk,bus=ahci.0"
	#DISK_OPTS="-hda ${BR2}/output/qemu.img.raw"
fi

MISC_OPTS="-localtime -no-reboot -netdev tap,id=network0"
MEM_OPTS="-m 2048"

#rest of the script here...
${QEMU} ${CPU_SPEC} -nographic -s "$@" ${DISK_OPTS} ${MEM_OPTS} ${KERNEL_IMG} -append "${KERNEL_CMD}" ${DEVICES} ${NETDEV} ${MISC_OPTS}

