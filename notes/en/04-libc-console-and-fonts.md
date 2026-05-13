## Homemade OS (4): Enhancing libc — Console Logic and More Output Fonts

In the previous article, we refactored the kernel, created a rudimentary libc, and successfully linked them together.

Now let's enhance this libc. We'll start with something "what you see is what you get." The ultimate goal for today is to use this libc to output a Cirno ASCII art.

### Font Enhancement: From 8×8 to 8×16

The current 8×8 font is a bit underwhelming and only supports a few simple characters. Let's have AI generate a larger, more complete font library and modify our terminal output logic slightly. Specifically, when we detect a newline character being output, we reset the output column and increment the output row. (You might argue it should be `\r\n` rather than `\n`? I think keeping it simple like Unix is better.)

![image-20260129091317020](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%884%EF%BC%89%EF%BC%9Alibc%E5%AE%8C%E5%96%84%E2%80%94%E2%80%94%E6%8E%A7%E5%88%B6%E5%8F%B0%E9%80%BB%E8%BE%91%E5%AE%8C%E5%96%84%E6%9B%B4%E5%A4%9A%E7%9A%84%E8%BE%93%E5%87%BA%E5%AD%97%E4%BD%93/image-20260129091317020.png)

With simple output, we can now display enough characters.

![image-20260129100250339](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%884%EF%BC%89%EF%BC%9Alibc%E5%AE%8C%E5%96%84%E2%80%94%E2%80%94%E6%8E%A7%E5%88%B6%E5%8F%B0%E9%80%BB%E8%BE%91%E5%AE%8C%E5%96%84%E6%9B%B4%E5%A4%9A%E7%9A%84%E8%BE%93%E5%87%BA%E5%AD%97%E4%BD%93/image-20260129100250339.png)

Our console can now correctly display ASCII art. Though I must admit, I've wasted too much time on trivial matters... Please don't follow my example!

### Auto-Adaptive Multi-Line Output

The output problem is solved for now, but we notice we still can't automatically wrap lines. (You can see in the image above that the characters on the last line are all crowded together.) Let's fix this.

First, we need to know the current screen resolution, then divide the entire screen by our font size to determine the display boundaries.

![image-20260129100523211](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%884%EF%BC%89%EF%BC%9Alibc%E5%AE%8C%E5%96%84%E2%80%94%E2%80%94%E6%8E%A7%E5%88%B6%E5%8F%B0%E9%80%BB%E8%BE%91%E5%AE%8C%E5%96%84%E6%9B%B4%E5%A4%9A%E7%9A%84%E8%BE%93%E5%87%BA%E5%AD%97%E4%BD%93/image-20260129100523211.png)

![image-20260129100945318](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%884%EF%BC%89%EF%BC%9Alibc%E5%AE%8C%E5%96%84%E2%80%94%E2%80%94%E6%8E%A7%E5%88%B6%E5%8F%B0%E9%80%BB%E8%BE%91%E5%AE%8C%E5%96%84%E6%9B%B4%E5%A4%9A%E7%9A%84%E8%BE%93%E5%87%BA%E5%AD%97%E4%BD%93/image-20260129100945318.png)

(Only now did I realize I had swapped rows and columns earlier... how embarrassing!)

![image-20260129101039505](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%884%EF%BC%89%EF%BC%9Alibc%E5%AE%8C%E5%96%84%E2%80%94%E2%80%94%E6%8E%A7%E5%88%B6%E5%8F%B0%E9%80%BB%E8%BE%91%E5%AE%8C%E5%96%84%E6%9B%B4%E5%A4%9A%E7%9A%84%E8%BE%93%E5%87%BA%E5%AD%97%E4%BD%93/image-20260129101039505.png)

Now we can output multiple lines. Of course, this is (somewhat) non-standard in terms of output format, but it's good enough for now. We can optimize it later.

### Scrolling

![image-20260129101304024](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%884%EF%BC%89%EF%BC%9Alibc%E5%AE%8C%E5%96%84%E2%80%94%E2%80%94%E6%8E%A7%E5%88%B6%E5%8F%B0%E9%80%BB%E8%BE%91%E5%AE%8C%E5%96%84%E6%9B%B4%E5%A4%9A%E7%9A%84%E8%BE%93%E5%87%BA%E5%AD%97%E4%BD%93/image-20260129101304024.png)

If we output a few more lines, we find that the bottom of the screen fills up and subsequent information can't be displayed. In a standard terminal, scrolling should be supported.

![image-20260129115740028](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%884%EF%BC%89%EF%BC%9Alibc%E5%AE%8C%E5%96%84%E2%80%94%E2%80%94%E6%8E%A7%E5%88%B6%E5%8F%B0%E9%80%BB%E8%BE%91%E5%AE%8C%E5%96%84%E6%9B%B4%E5%A4%9A%E7%9A%84%E8%BE%93%E5%87%BA%E5%AD%97%E4%BD%93/image-20260129115740028.png)

![image-20260129115913411](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%884%EF%BC%89%EF%BC%9Alibc%E5%AE%8C%E5%96%84%E2%80%94%E2%80%94%E6%8E%A7%E5%88%B6%E5%8F%B0%E9%80%BB%E8%BE%91%E5%AE%8C%E5%96%84%E6%9B%B4%E5%A4%9A%E7%9A%84%E8%BE%93%E5%87%BA%E5%AD%97%E4%BD%93/image-20260129115913411.png)

To implement scrolling, we first need to implement `memcpy`, then use it to copy each row's content to the previous row when the line count exceeds the maximum:

![image-20260129120011351](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%884%EF%BC%89%EF%BC%9Alibc%E5%AE%8C%E5%96%84%E2%80%94%E2%80%94%E6%8E%A7%E5%88%B6%E5%8F%B0%E9%80%BB%E8%BE%91%E5%AE%8C%E5%96%84%E6%9B%B4%E5%A4%9A%E7%9A%84%E8%BE%93%E5%87%BA%E5%AD%97%E4%BD%93/image-20260129120011351.png)

![image-20260129120024036](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%884%EF%BC%89%EF%BC%9Alibc%E5%AE%8C%E5%96%84%E2%80%94%E2%80%94%E6%8E%A7%E5%88%B6%E5%8F%B0%E9%80%BB%E8%BE%91%E5%AE%8C%E5%96%84%E6%9B%B4%E5%A4%9A%E7%9A%84%E8%BE%93%E5%87%BA%E5%AD%97%E4%BD%93/image-20260129120024036.png)

Now our system can scroll!

### Formatted Printf

Our console output is looking pretty good now — except our `printf` function isn't like a real `printf`: it doesn't support formatted output!

![image-20260129121157428](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%884%EF%BC%89%EF%BC%9Alibc%E5%AE%8C%E5%96%84%E2%80%94%E2%80%94%E6%8E%A7%E5%88%B6%E5%8F%B0%E9%80%BB%E8%BE%91%E5%AE%8C%E5%96%84%E6%9B%B4%E5%A4%9A%E7%9A%84%E8%BE%93%E5%87%BA%E5%AD%97%E4%BD%93/image-20260129121157428.png)

Yes, we were just throwing everything at `print` for processing.

We need to write a state machine that replaces `%x` format specifiers in the first argument with the corresponding subsequent arguments.

![image-20260129130040890](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%884%EF%BC%89%EF%BC%9Alibc%E5%AE%8C%E5%96%84%E2%80%94%E2%80%94%E6%8E%A7%E5%88%B6%E5%8F%B0%E9%80%BB%E8%BE%91%E5%AE%8C%E5%96%84%E6%9B%B4%E5%A4%9A%E7%9A%84%E8%BE%93%E5%87%BA%E5%AD%97%E4%BD%93/image-20260129130040890.png)

Variadic arguments are somewhat unfamiliar territory for me. I had to ask AI about their usage. If you're also unfamiliar with them, feel free to ask AI about it too.

#### %d

Integer output is actually quite clever. In a regular algorithm problem, I'd put the integer into a stringstream and read it back out. But here we don't have many functions available, so I used recursion instead.

![image-20260129130442643](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%884%EF%BC%89%EF%BC%9Alibc%E5%AE%8C%E5%96%84%E2%80%94%E2%80%94%E6%8E%A7%E5%88%B6%E5%8F%B0%E9%80%BB%E8%BE%91%E5%AE%8C%E5%96%84%E6%9B%B4%E5%A4%9A%E7%9A%84%E8%BE%93%E5%87%BA%E5%AD%97%E4%BD%93/image-20260129130442643.png)

#### %x, %X

![image-20260129132403304](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%884%EF%BC%89%EF%BC%9Alibc%E5%AE%8C%E5%96%84%E2%80%94%E2%80%94%E6%8E%A7%E5%88%B6%E5%8F%B0%E9%80%BB%E8%BE%91%E5%AE%8C%E5%96%84%E6%9B%B4%E5%A4%9A%E7%9A%84%E8%BE%93%E5%87%BA%E5%AD%97%E4%BD%93/image-20260129132403304.png)

The logic is similar to decimal — just change the base to 16. Same for binary.

![image-20260129132528631](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%884%EF%BC%89%EF%BC%9Alibc%E5%AE%8C%E5%96%84%E2%80%94%E2%80%94%E6%8E%A7%E5%88%B6%E5%8F%B0%E9%80%BB%E8%BE%91%E5%AE%8C%E5%96%84%E6%9B%B4%E5%A4%9A%E7%9A%84%E8%BE%93%E5%87%BA%E5%AD%97%E4%BD%93/image-20260129132528631.png)

#### %s

![image-20260129132744535](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%884%EF%BC%89%EF%BC%9Alibc%E5%AE%8C%E5%96%84%E2%80%94%E2%80%94%E6%8E%A7%E5%88%B6%E5%8F%B0%E9%80%BB%E8%BE%91%E5%AE%8C%E5%96%84%E6%9B%B4%E5%A4%9A%E7%9A%84%E8%BE%93%E5%87%BA%E5%AD%97%E4%BD%93/image-20260129132744535.png)

![image-20260129132828134](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%884%EF%BC%89%EF%BC%9Alibc%E5%AE%8C%E5%96%84%E2%80%94%E2%80%94%E6%8E%A7%E5%88%B6%E5%8F%B0%E9%80%BB%E8%BE%91%E5%AE%8C%E5%96%84%E6%9B%B4%E5%A4%9A%E7%9A%84%E8%BE%93%E5%87%BA%E5%AD%97%E4%BD%93/image-20260129132828134.png)

---

So far, we've improved the console display, including expanding the number of displayable characters, supporting multi-line output and scrolling, and providing a formatted `printf` function.

In the next section, we'll look at GDT and IDT.
