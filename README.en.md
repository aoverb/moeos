<p align="center">
  <img src="./screenshots/logo-small.jpg" width="400" />
</p>

<h1 align="center">MoeOS</h1>

<p align="center">
  <b>M</b>inimal <b>O</b>pen <b>E</b>xperimental <b>O</b>perating <b>S</b>ystem
</p>

<p align="center">
  A 32-bit hobby operating system built from scratch in C++.
</p>

---

MoeOS is a hobby operating system project built from scratch[^1] in C++, aiming to deeply understand the core principles of operating systems and put them into practice one by one.

### Implemented Features

**Kernel Infrastructure**
- Higher-Half Kernel
- Physical Memory Manager (PMM), Virtual Memory Manager (VMM), Kernel Heap Allocator
- MLFQ (Multi-Level Feedback Queue) based process scheduler
- ELF executable parsing and loading
- Signals, Pipes
- Basic system call interface

**Drivers**
- PS/2 keyboard driver
- PIT timer driver
- RTL8139 network card driver
- ATA block device driver
- Graphics framebuffer

**File Systems**
- VFS (Virtual File System) abstraction layer
- Ext2 file system read/write support
- TARFS, DevFS, ProcFS, SockFS

**Networking**
- Basic TCP/IP protocol stack
- Telnet protocol support

**User Space**
- User-mode processes and basic Libc
- Console device files, VT100 terminal state machine
- Kilo text editor port
- ...And it runs DOOM!

<p align="center">
  <img src="./screenshots/doom.gif" width="480" />
</p>

### Development Records

The entire development process has been fully recorded through screencasts and Markdown notes. Related resources are being organized:

- [Blog](http://moe.cm/) — Technical notes and development logs[^2]
- [GitHub Repository](https://github.com/aoverb/lolios/tree/main/notes) — Original development notes[^2]
- [Bilibili Channel](https://space.bilibili.com/1506917) — Development recordings and technical explainer videos[^3]

### Screenshots

#### Boot Screen

<p align="center">
  <img src="./screenshots/boot.jpg" width="720" />
</p>

#### Pipe & File Operations Demo

<p align="center">
  <img src="./screenshots/echo-pipe-ll-kilo-cat.gif" width="720" />
</p>

#### Telnet, Process List, Killing Processes with Signals

<p align="center">
  <img src="./screenshots/telnet-ps-signal-kill.gif" width="720" />
</p>

#### ProcFS & PS Demo

<p align="center">
  <img src="./screenshots/ps-procfs.gif" width="720" />
</p>

#### netstat Demo

<p align="center">
  <img src="./screenshots/netstat.gif" width="720" />
</p>

### Build & Run

#### Prerequisites

- `i686-elf` Cross-compiler toolchain (`i686-elf-gcc`, `i686-elf-g++`, `i686-elf-as`)
- GNU Make
- GRUB tools (`grub-mkrescue`) and its dependency `xorriso`
- QEMU (`qemu-system-i386`)

To install non-cross-compiler dependencies on Ubuntu/Debian:

```bash
sudo apt install qemu-system-x86 grub-pc-bin xorriso make
```

The cross-compiler needs to be built manually. For detailed steps, see [OSDev Wiki: GCC Cross-Compiler](https://wiki.osdev.org/GCC_Cross-Compiler).

#### Build

```bash
git clone https://github.com/aoverb/moeos.git
cd moeos
./build.sh
```

`build.sh` will execute the following steps in order:

1. Build Libc (install headers and static library to `SYSROOT/`)
2. Build user-space programs (install to `SYSROOT/usr/bin/`)
3. Build the kernel (generates `kernel/moeos.kernel`)
4. Package `SYSROOT` into a ustar-format `sysroot.tar`, loaded as a GRUB module
5. Generate a bootable ISO image via `grub-mkrescue`

#### Run

After building, `build.sh` will automatically start the system via QEMU. To run manually:

```bash
qemu-system-i386 -cdrom moeos.iso -drive file=disk.img,format=raw \
    -netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
    -device rtl8139,netdev=net0,mac=CA:FE:BA:BE:13:37
```

#### Network Configuration (Optional)

MoeOS uses a TAP network interface to communicate with the host. To enable networking, create a TAP device first:

```bash
sudo ip tuntap add dev tap0 mode tap user $(whoami)
sudo ip addr add 10.0.1.0/24 dev tap0
sudo ip link set tap0 up
```

#### Disk Image (Optional)

The Ext2 file system requires a disk image. Create one with:

```bash
dd if=/dev/zero of=disk.img bs=1M count=32
mkfs.ext2 disk.img
```

If persistent storage is not needed, remove the `-drive` option from the QEMU command.

### ⚠️ Important Notes

MoeOS is an experimental project built for learning purposes. All hardware interactions are developed and tested in the QEMU emulated environment and **have not been verified on any real hardware**. Do not run this system on a physical machine, as it may cause hardware damage or data loss.

### Third-Party Software

This project makes use of the following third-party software:

- **[GNU GRUB](https://www.gnu.org/software/grub/)** — GRand Unified Bootloader, used as an external tool.
  Licensed under the [GNU General Public License v3.0 or later](https://www.gnu.org/licenses/gpl-3.0.html).
  Copyright © Free Software Foundation, Inc.

- **[doomgeneric](https://github.com/ozkl/doomgeneric)** — An easily portable version of DOOM, based on the original id Software source release.
  Licensed under the [GNU General Public License v2.0](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
  Original game © id Software. Port by ozkl.

- **[QR-Code-generator](https://github.com/nayuki/QR-Code-generator)** — High-quality QR Code generator library by Project Nayuki.
  Licensed under the [MIT License](https://opensource.org/licenses/MIT).
  Copyright © Project Nayuki.

- **[Kilo](https://github.com/antirez/kilo)** — A small text editor in less than 1000 LOC by Salvatore Sanfilippo (antirez).
  Licensed under the [BSD 2-Clause License](https://opensource.org/licenses/BSD-2-Clause).
  Copyright © 2016 Salvatore Sanfilippo.

---

[^1]: "From scratch" in a certain sense — the bootloader (GRUB) is treated as existing infrastructure; the project focuses on the kernel and everything built above it.
[^2]: Still being organized.
[^3]: Screencasts of the development process and milestone technical reviews will be posted gradually, covering everything from kernel booting and driver development to network stack and file system implementation.
