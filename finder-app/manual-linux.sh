#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u
set -x

OUTDIR=/tmp/aeld
ROOTFS=${OUTDIR}/rootfs
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
SYSROOT=`aarch64-none-linux-gnu-gcc -print-sysroot`

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
    make -j11 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- mrproper
    make -j11 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- defconfig
    make -j11 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- all
    make -j11 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- modules
    make -j11 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- dtbs
fi

echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/Image


echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${ROOTFS}" ]
then
	echo "Deleting rootfs directory at ${ROOTFS} and starting over"
    sudo rm  -rf ${ROOTFS}
fi

# TODO: Create necessary base directories
mkdir -p ${ROOTFS}
cd ${ROOTFS}
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]; then
    git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
else
    cd busybox
fi

# TODO: Make and install busybox
make -j11 distclean
make -j11 defconfig
# Hacky workaround: disabling tc.c as it causes error building in linux 6.8 and above
# https://github.com/mirror/busybox/issues/91
sed -e "s/.*CONFIG_TC=y/CONFIG_TC=n/" -i .config
make -j11 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make -j11 CONFIG_PREFIX=${ROOTFS} CROSS_COMPILE=${CROSS_COMPILE} install

echo "Library dependencies"
${CROSS_COMPILE}readelf -a busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a busybox | grep "Shared library"

# TODO: Add library dependencies to rootfs
cp ${SYSROOT}/lib/ld-linux-aarch64.so.1 ${ROOTFS}/lib/
cp ${SYSROOT}/lib64/libm.so.6           ${ROOTFS}/lib64/
cp ${SYSROOT}/lib64/libresolv.so.2      ${ROOTFS}/lib64/
cp ${SYSROOT}/lib64/libc.so.6           ${ROOTFS}/lib64/

# TODO: Make device nodes
sudo mknod -m 666 ${ROOTFS}/dev/console c 5 1
sudo mknod -m 666 ${ROOTFS}/dev/null    c 1 3

# TODO: Clean and build the writer utility
cd ${FINDER_APP_DIR}
make clean
make CROSS_COMPILE=${CROSS_COMPILE}

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
mkdir -p ${ROOTFS}/home/conf
cp -r writer finder.sh finder-test.sh autorun-qemu.sh ${ROOTFS}/home
cp -r conf/assignment.txt conf/username.txt ${ROOTFS}/home/conf

# TODO: Chown the root directory
sudo chown -R root:root ${ROOTFS}

# TODO: Create initramfs.cpio.gz
cd ${ROOTFS}
find . | cpio -H newc -ov --owner root:root | gzip -n > ${OUTDIR}/initramfs.cpio.gz
