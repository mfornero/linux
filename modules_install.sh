#!/bin/bash
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
QEMU=$(dirname ${SCRIPT_DIR})/qemu/build/x86_64-softmmu/qemu-system-x86_64
BR2=$(dirname ${SCRIPT_DIR})/buildroot

IMG=${BR2}/output/images/qemu.img.raw
IMG_MOUNT=${BR2}/output/root
LOOP=loop0
LOOPDEV=/dev/${LOOP}
MAPPERDEV=/dev/mapper/${LOOP}p1

# Mount the image
sudo kpartx -avs ${IMG}
sudo mount ${MAPPERDEV} ${IMG_MOUNT}

make modules
sudo make -j8 INSTALL_MOD_PATH=${IMG_MOUNT} modules_install

#unmount the img
sudo umount ${IMG_MOUNT}
sudo kpartx -d ${IMG}
#sudo dmsetup remove ${MAPPERDEV}
#sudo losetup -d ${LOOPDEV}

