#!/bin/bash
export SYSROOT=`pwd`/SYSROOT
echo $SYSROOT
mkdir -p $SYSROOT/usr/bin
cd user
make all install-bin
cd ..