#!/bin/sh
set -x
umount /mnt/wrapfs
lsmod
rmmod wrapfs
insmod wrapfs.ko
lsmod
mount -t u2fs -o ldir=/root/hw2-mdali/hw2/ldir,rdir=/root/hw2-mdali/hw2/rdir none /mnt/wrapfs
