#!/bin/bash


#TODO getopt args parsing

DISK=$1
MOUNTPOINT=$2
KERNEL_KO="penisfs.ko"

sudo insmod $KERNEL_KO
sudo mount -t penisfs -o key=key $DISK $MOUNTPOINT