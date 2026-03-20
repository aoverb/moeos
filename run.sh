#!/bin/bash
qemu-system-i386 -device rtl8139,netdev=net0 -netdev user,id=net0 -cdrom moeos.iso
