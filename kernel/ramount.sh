#!/bin/bash

DIRECTORY="/mnt/penis"
KERNEL_KO="penisfs.ko"

if [ -d "$DIRECTORY" ]; then
  echo "$DIRECTORY does exist. Can't umount"
  exit 1
fi

sudo umount /mnt/penis
sudo rmmod $KERNEL_KO
