## 自制操作系统（1）：内核Hello world

```
注：这篇文章并不完整，因为我一开始没打算记录制作的全过程，所以一开始并没有写这一章...后面会把它补充完整，请见谅。
```

全程参考：https://osdev.wiki/wiki/Multiboot1_Bare_Bones。

毫不避讳地说，Bare bone这里AI帮我写了绝大部分的代码，因为：1、这部分代码不多，而且绝大部分在后面会被替代；2、我没有进行过内核编程，我不想太过苛求自己，让自己一开始就困在各种报错和调试中，为自己带来太多的挫败感；3、我想先让自己能尽快“看到点什么”——反馈是很重要的！

### 开发环境

WSL2+VSCode

#### 准备工作：交叉编译器

为了在WSL2环境编译出我们的32位操作系统，你需要构建对应的交叉编译工具链：

https://github.com/lordmilko/i686-elf-tools

按项目说明操作即可。装太高版本的Python好像会有问题，推荐安装Python3.10。

#### 帧缓冲输出

OSDEV WIKI里面用的是文字模式输出，我改成了帧缓冲模式：

```assembly
/* boot.s */
.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set VIDEOMODE, 1<<2        /* 新增：请求视频模式标志位 */
.set FLAGS,    ALIGN | MEMINFO | VIDEOMODE
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)
```

然后在内核代码中，我们可以这样来绘制字符：

```cpp
/* 2. 定义一个极其简单的 8x8 像素字体 
   为了演示，这里只定义 'H', 'e', 'l', 'o', 'W', 'r', 'd' 的位图。
   实际 OS 会引入 font8x8_basic.h 包含所有 ASCII 字符。
   1 表示画点，0 表示背景。
*/
uint8_t font_H[] = {0xC3, 0xC3, 0xC3, 0xFF, 0xC3, 0xC3, 0xC3, 0x00};
uint8_t font_e[] = {0x00, 0x3C, 0x42, 0x7E, 0x40, 0x3C, 0x00, 0x00};
uint8_t font_l[] = {0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xFE, 0x00, 0x00};
uint8_t font_o[] = {0x00, 0x3C, 0x42, 0x42, 0x42, 0x3C, 0x00, 0x00};
uint8_t font_W[] = {0xC3, 0xC3, 0xC3, 0xDZb, 0xFF, 0xC3, 0x00, 0x00}; // Simplified
uint8_t font_r[] = {0x00, 0xAE, 0x66, 0x60, 0x60, 0xF0, 0x00, 0x00}; // Simplified
uint8_t font_d[] = {0x06, 0x06, 0x3E, 0x46, 0x46, 0x3E, 0x00, 0x00};

// 全局变量用于存储屏幕信息
uint32_t* fb_addr;
uint32_t fb_pitch;
uint32_t fb_bpp;

/* 3. 画点函数 (Plot Pixel) 
   核心公式：地址 = 基址 + (y * 每行字节数) + (x * 每个像素字节数)
*/
void put_pixel(int x, int y, uint32_t color) {
    // 假设是 32 位颜色 (4 bytes)
    uint32_t offset = y * (fb_pitch / 4) + x;
    fb_addr[offset] = color;
}

/* 4. 绘制字符函数 */
void draw_char(int x, int y, uint8_t* font_char, uint32_t color) {
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            // 检查位图中对应的位是否为 1
            // 0x80 是 10000000，用来测试最高位
            if ((font_char[row] & (0x80 >> col))) {
                put_pixel(x + col, y + row, color);
            }
        }
    }
```
### 效果
![image-20260127201726165](./assets/自制操作系统（2）：从Bare bone到Meaty skeleton（上）/image-20260127201726165.png)

至此，我们写出了第一个内核级的Hello world程序！下一节，我们将讨论怎么把我们现在一碰就散的内核骨架变成一个有血有肉的Meaty skeleton。
