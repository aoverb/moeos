## Homemade OS (3): From Bare Bones to Meaty Skeleton (Part 2)

In the previous section, we reorganized the file architecture, refactored some code, and introduced makefiles for building. Now we can output "Hello World" with a much cleaner architecture.

```cpp
extern "C" void kernel_main(multiboot_info_t* mbi) {
    // 1. Initialize hardware
    terminal_initialize(mbi);

    // 2. Define colors
    uint32_t white = 0x00FFFFFF;
    uint32_t green = 0x0000FF00;

    // 3. Business logic: draw characters
    // The code now reads more like natural language
    terminal_draw_char(100, 100, get_font_bitmap('H'), green);
    terminal_draw_char(108, 100, get_font_bitmap('e'), white);
    terminal_draw_char(116, 100, get_font_bitmap('l'), white);
    terminal_draw_char(124, 100, get_font_bitmap('l'), white);
    terminal_draw_char(132, 100, get_font_bitmap('o'), white);
    
    terminal_draw_char(148, 100, get_font_bitmap('W'), green);
    terminal_draw_char(156, 100, get_font_bitmap('o'), white);
    terminal_draw_char(164, 100, get_font_bitmap('r'), white);
    terminal_draw_char(172, 100, get_font_bitmap('l'), white);
    terminal_draw_char(180, 100, get_font_bitmap('d'), white);
}
```

But wait... looking at our `kernel.cpp` code, something still seems off. Do we really need to draw characters one by one every time we want to output debug information? Clearly, we need a more convenient function.

When writing user-space programs, we use a single `printf` to output all text! But we don't have `printf` yet because we don't have our own standard library. So we need to implement it ourselves.

### Libk

![image-20260128121433934](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128121433934.png)

We create a new `libc` directory and define several console output functions in it:

![image-20260128121526643](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128121526643.png)

![image-20260128121542953](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128121542953.png)

Notice we've used a new function `terminal_write` to output a specific character. We haven't defined it yet, so we need to declare and implement it in `tty.h` and `tty.c` respectively:

![image-20260128121735266](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128121735266.png)

Since we're using pixel output mode, the handling should be more complex than this. Let's stub it out for now and come back later to improve our console output logic.

After this, we need to update our Makefile to generate `libk.a` and have the kernel's makefile find and link against it. Let's write the libc Makefile first.

#### Makefile

![image-20260128194646792](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128194646792.png)

This time I tried writing the makefile myself. It turns out to be quite intuitive — no need to rely on AI to generate it.

Just follow a top-down approach: "What's our ultimate goal? `libk.a`. What does `libk.a` need? It needs various `.o` files. What command do we use? We use the `ar` command to combine them..."

![image-20260128194850029](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128194850029.png)

Finally, we can generate our `.a` static library file in the `libc` directory.

But there's still a problem with our OS: we're using `-I` to point to source directories for header files, which doesn't conform to Unix conventions.

### Sysroot

Meaty Skeleton introduces the concept of **Sysroot**. We create a folder called `sysroot` with this internal structure:

- `sysroot/usr/include`
- `sysroot/usr/lib`

**Why do this?** To enable the possibility of **self-hosting**. We "install" the kernel and libc headers into this sysroot. When the compiler compiles subsequent code, it uses the `--sysroot=sysroot` flag. This way, the compiler looks for headers in `sysroot/usr/include`, just like developing on a real Linux system.

#### Getting Rid of `-Iinclude`

First, we stop using the `-I` parameter in makefiles and switch to `--sysroot=$(SYSROOT)`:

![image-20260128200920658](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128200920658.png)

Of course, we need someone to pass `SYSROOT` in. Let's create a new `build_libc.sh` at the top level:

![image-20260128201354346](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128201354346.png)

Let's try running it.

![image-20260128201403941](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128201403941.png)

OK, it seems we need to copy the headers to `sysroot/usr/include` first.

![image-20260128201631212](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128201631212.png)

![image-20260128201642975](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128201642975.png)

Added a new operation: `install-headers`.

![image-20260128203330372](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128203330372.png)

Then we need to specify a system directory.

![image-20260128203355177](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128203355177.png)

Now we can find the header files built under `sysroot`.

#### install-libc

Next, we need to add an `install` command to copy the built `libk.a` to `sysroot/usr/lib` for later kernel linking.

![image-20260128204318205](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128204318205.png)

![image-20260128204433207](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128204433207.png)

#### Kernel Build & Install

Now let's merge the execution of `build_libc.sh` into `build.sh`, and also switch kernel includes to sysroot:

![image-20260128204634755](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128204634755.png)

Let's try running `build.sh`.

![image-20260128205608211](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128205608211.png)

We can see there's a linking issue — we haven't added `libk.a` to the linker script yet!

![image-20260128205930836](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128205930836.png)

![image-20260128205938609](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128205938609.png)

Now it builds correctly.

![image-20260128205955780](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128205955780.png)

Hey wait... Why is it vertical?! Looks like our `printf` function needs more work.

![image-20260128210506228](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128210506228.png)

After a small logic adjustment and adding a space character to the font, it can now correctly output "Hello World."

![image-20260128210626753](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%883%EF%BC%89%EF%BC%9A%E4%BB%8EBare%20bone%E5%88%B0Meaty%20skeleton%EF%BC%88%E4%B8%8B%EF%BC%89/image-20260128210626753.png)

Of course, we can still modify the output content (though it's quite limited...).

---

Today, we've finally implemented `libk` in a proper way, along with a sysroot-based build system. But we've discovered that the characters we can output are quite limited due to insufficient font support! Next, we'll introduce proper fonts to output more characters and improve our console output logic.
