#!/bin/bash
. build_libc.sh
. build_user.sh
cd kernel
make
cd ..
# 复制内核
cp kernel/lolios.kernel isodir/boot/lolios.bin
cp user/hello_world.bin isodir/boot/hello_world.bin

# 创建 GRUB 配置文件
cat > isodir/boot/grub/grub.cfg << EOF
menuentry "LoliOS" {
	multiboot /boot/lolios.bin
    module /boot/hello_world.bin
}
EOF

# 生成 ISO 文件
grub-mkrescue -o lolios.iso isodir
qemu-system-i386 -cdrom lolios.iso
