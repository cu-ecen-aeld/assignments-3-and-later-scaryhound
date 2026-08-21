#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

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

    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
fi
echo "Adding the img in OUTDIR"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}

echo "Adding the Image in outdir"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

mkdir -p ${OUTDIR}/rootfs
cd ${OUTDIR}/rootfs
mkdir -p bin dev etc home lib lib64 sbin proc sys tmp var usr
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log


cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone https://github.com/mirror/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    make distclean
    make defconfig
else
    cd busybox
fi

make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH={ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

echo "Library dependencies"
cd "${OUTDIR}/rootfs"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)

echo "Copying library dependencies to rootfs..."
cp -a "${SYSROOT}/lib/ld-linux-aarch64.so.1" "${OUTDIR}/rootfs/lib/"
cp -a "${SYSROOT}/lib64/libm.so.6" "${OUTDIR}/rootfs/lib64/"
cp -a "${SYSROOT}/lib64/libresolv.so.2" "${OUTDIR}/rootfs/lib64/"
cp -a "${SYSROOT}/lib64/libc.so.6" "${OUTDIR}/rootfs/lib64/"

echo "Making device nodes..."
sudo mknod -m 666 "${OUTDIR}/rootfs/dev/null" c 1 3
sudo mknod -m 600 "${OUTDIR}/rootfs/dev/console" c 5 1


echo "Cleaning and Building writer utility"
cd "${FINDER_APP_DIR}" 
make clean
make CROSS_COMPILE=${CROSS_COMPILE}

echo "Copying scripts to rootfs /home directory..."
cp writer "${OUTDIR}/rootfs/home/"
cp finder.sh "${OUTDIR}/rootfs/home/"
cp finder-test.sh "${OUTDIR}/rootfs/home/"
cp autorun-qemu.sh "${OUTDIR}/rootfs/home/"
cp -r conf/ "${OUTDIR}/rootfs/home/"


sed -i 's/\.\.\/conf\/assignment\.txt/conf\/assignment\.txt/g' "${OUTDIR}/rootfs/home/finder-test.sh"


echo "Changing ownership of rootfs to root..."
cd "${OUTDIR}/rootfs"
sudo chown -R root:root *



echo "Creating initramfs.cpio.gz..."

cd "${OUTDIR}/rootfs"
find . | cpio -H newc -ov --owner root:root > "${OUTDIR}/initramfs.cpio"

cd "${OUTDIR}"
gzip -f initramfs.cpio
