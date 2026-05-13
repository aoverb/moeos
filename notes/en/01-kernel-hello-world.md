## Homemade OS (1): Kernel Hello World

```
Note: This article is incomplete. I didn't initially plan to document the entire process, so I didn't write this chapter at the beginning... I'll fill it in later. Please bear with me.
```

Full reference: https://osdev.wiki/wiki/Multiboot1_Bare_Bones

I'll be honest: AI helped me write most of the code for the Bare Bones stage. Why? 1) There isn't much code here, and most of it will be replaced later anyway. 2) I had no prior kernel programming experience — I didn't want to be too hard on myself and get stuck in errors and debugging right from the start, dealing with too much frustration. 3) I wanted to "see something" as quickly as possible — feedback is important!

### Development Environment

WSL2 + VSCode

#### Preparation: Cross-Compiler

To compile our 32-bit OS in the WSL2 environment, you need to build a corresponding cross-compilation toolchain:

https://github.com/lordmilko/i686-elf-tools

Just follow the project instructions. Installing too high a version of Python might cause issues; Python 3.10 is recommended.

#### Framebuffer Output

The OSDev Wiki uses text mode output, but I switched to framebuffer mode:

```assembly
/* boot.s */
.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set VIDEOMODE, 1<<2        /* New: request video mode flag */
.set FLAGS,    ALIGN | MEMINFO | VIDEOMODE
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)
```

Then in the kernel code, we can draw characters like this:

```cpp
/* 2. Define an extremely simple 8x8 pixel font 
   For demonstration, only define bitmaps for 'H', 'e', 'l', 'o', 'W', 'r', 'd'.
   A real OS would include font8x8_basic.h with all ASCII characters.
   1 = draw pixel, 0 = background.
*/
uint8_t font_H[] = {0xC3, 0xC3, 0xC3, 0xFF, 0xC3, 0xC3, 0xC3, 0x00};
uint8_t font_e[] = {0x00, 0x3C, 0x42, 0x7E, 0x40, 0x3C, 0x00, 0x00};
uint8_t font_l[] = {0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xFE, 0x00, 0x00};
uint8_t font_o[] = {0x00, 0x3C, 0x42, 0x42, 0x42, 0x3C, 0x00, 0x00};
uint8_t font_W[] = {0xC3, 0xC3, 0xC3, 0xDZb, 0xFF, 0xC3, 0x00, 0x00}; // Simplified
uint8_t font_r[] = {0x00, 0xAE, 0x66, 0x60, 0x60, 0xF0, 0x00, 0x00}; // Simplified
uint8_t font_d[] = {0x06, 0x06, 0x3E, 0x46, 0x46, 0x3E, 0x00, 0x00};

// Global variables for screen info
uint32_t* fb_addr;
uint32_t fb_pitch;
uint32_t fb_bpp;

/* 3. Plot Pixel function 
   Core formula: address = base + (y * bytes_per_row) + (x * bytes_per_pixel)
*/
void put_pixel(int x, int y, uint32_t color) {
    // Assume 32-bit color (4 bytes)
    uint32_t offset = y * (fb_pitch / 4) + x;
    fb_addr[offset] = color;
}

/* 4. Draw character function */
void draw_char(int x, int y, uint8_t* font_char, uint32_t color) {
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            // Check if the corresponding bit in the bitmap is 1
            // 0x80 is 10000000, used to test the highest bit
            if ((font_char[row] & (0x80 >> col))) {
                put_pixel(x + col, y + row, color);
            }
        }
    }
}
```

### Result

![image-20260127201726165](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%882%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8A%EF%BC%89/image-20260127201726165.png)

With that, we've written our first kernel-level Hello World program! In the next section, we'll discuss how to transform our fragile kernel skeleton into a robust Meaty Skeleton.
