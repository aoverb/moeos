#!/bin/bash
. build_libc.sh
. build_user.sh
cd kernel
make
cd ..

# 打包 SYSROOT 为 ustar 格式的 tar
tar --format=ustar -cf isodir/boot/sysroot.tar -C SYSROOT .

# 复制内核
cp kernel/lolios.kernel isodir/boot/lolios.bin

# 创建 GRUB 配置文件
cat > isodir/boot/grub/grub.cfg << EOF
menuentry "LoliOS" {
	multiboot /boot/lolios.bin
    module /boot/sysroot.tar
}
EOF

# 生成 ISO 文件
grub-mkrescue -o lolios.iso isodir
qemu-system-i386 -cdrom lolios.iso \
    -netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
    -device rtl8139,netdev=net0,mac=CA:FE:BA:BE:13:37
