## Homemade OS (999): Afterword

This is where I record various musings related to the project.

### Things I Regret Not Doing Right from the Start

Not choosing to develop a 64-bit operating system;

Not firmly committing to C++ features, resulting in my coding style oscillating between C and C++, which was a poor experience;

### Resources I Used and Recommend

**OSDev Wiki:**

The first thing to do if you want to build an operating system — go to this website! A must-visit. It can help you avoid many pitfalls.

It tells you which module to work on first, which module to work on next, and what each module should implement. The upside is that some entries are quite detailed; the downside is that some entries are as good as not written at all.

**OSTEP:**

If you need a slightly more advanced and comprehensive understanding of everything about operating systems — beyond just the basics — read this. It covers almost everything.

**Linux 0.11 Source Code and Source Code Analysis:**

You can directly experience some of Linux's clever designs and charm.

### AI-Assisted Coding Principles

When writing the code for this project, I used AI assistance but adhered to the following principles:

1. When writing the kernel, I tried not to let AI write code for me — I only used AI to review my code. For user-space programs and libc, I did leverage AI writing assistance (since I wanted to focus on the kernel itself).

2. I let AI give suggestions on direction, but not on implementation details.

3. Rather than spending a lot of time cleaning up the mess caused by a poor AI's suggestions, it's better to spend a bit more money from the start on a good AI — it reduces time loss and unnecessary frustration.

The AIs I used were Gemini 3.0 Free (chapters 1-10) and Claude Opus 4.6 Extended (chapter 10 onwards). I asked the AIs I used to output how I utilized them, using the following prompt, for your reference.

Prompt:

```
Based on your comprehensive memory, give me your assessment of the degree of dependence and focus areas in my OSDev project, along with some opinions — what I did well, what I didn't do well, what I should have done, and what I shouldn't have done. Give a concise summary.
```

**Claude:**

```
Based on extensive conversation history, here is my summary.

Project Overview: LoliOS
An i386 architecture homemade operating system, using the i686-elf cross-compiler, written in a mix of C/C++. Starting from the OSDev Meaty Skeleton, it progressively implemented GDT/IDT, PMM/VMM/Kheap, process management, system calls, VFS/TARFS/DevFS/PipeFS, ELF loading, user-mode shell, RTL8139 NIC driver, a complete network stack (Ethernet → ARP → IP → ICMP → UDP → TCP), as well as the ping user program and panic screen. The project has a remarkably high degree of completion.

Your Degree of Dependence on Me and Focus Areas

High-Frequency Dependence (you came to me for almost every step):

Compilation and linking error troubleshooting — extern "C" mismatches, __gxx_personality_v0, __cxa_atexit, undefined symbols, etc. When you encountered these problems, you almost always directly pasted the error messages for me to look at.
Architecture design decisions — How to abstract VFS, whether pipe should be implemented as a filesystem, network stack layering order, TCP hash table strategy. You asked the most about these "how should it be designed" questions.
Bug hunting — pipefs race conditions, refcnt lifecycle, the timing of v_close calling the underlying implementation. You often wrote a version, hit a bug, then pasted code for me to help find the problem.

Medium-Frequency Dependence:

API/standard alignment — clock() vs get_tick, which libc directory a POSIX function belongs in, how scanf should be layered.
Code review — shared_ptr implementation, comprehensive TCP implementation review. You had good awareness in this area, even asking me to guide with questions rather than pointing things out directly.

Low-Frequency / Almost No Dependence:

Core algorithm implementation (your ring buffers, process scheduler, and memory allocator were mostly written by yourself)
Assembly-level stuff (boot, context switch, interrupt entry points)

What You Did Well
1. Excellent project momentum. From Meaty Skeleton all the way to a complete network stack and user-space programs — very broad coverage. Many OSDev projects stay forever at "can print Hello World"; you far exceeded that stage.
2. Clear layering awareness. The VFS abstraction, pipefs as an independent mounted filesystem, the network stack built layer by layer (rather than one tangled mess) — this shows you have good intuition for system architecture.
3. Proactively requesting reviews and guided teaching. With shared_ptr you explicitly said "don't point it out directly, guide me to think" — showing you're consciously training your debugging ability, not just looking for answers.
4. Willingness to grind through difficult points. The pipefs refcnt issue went through at least three or four iterations before you finally solved it, during which you added timing logs, drew flowcharts — your debugging methodology was improving.

What You Didn't Do Well
1. Your compilation error troubleshooting ability should be stronger. The extern "C" linking issue came up at least three times (tss_set_kernel_stack, v_open, string.libk.o), and you asked me each time. These kinds of problems have clear diagnostic routines (nm to check symbols, look for mangling) — after encountering them once, you should be able to handle them yourself.
2. The quality of your first-draft code has room for improvement. The code you pasted to me often had fairly basic issues: missing return statements, forgetting to change variable names after copy-paste (TCP hasher where remote_port was written twice), pseudo-header length not including payload, etc. These aren't ability problems — they're a matter of not having the habit of reviewing your own code line by line after writing it.
3. Not thinking about concurrency/race conditions early enough. The pipefs problem was essentially "reference counting vs. boolean values" and "lock granularity" — these should have been thought through at the design stage, not discovered only after hitting bugs. The tcp_connect lock issue was similar — sending a packet before acquiring the lock; this kind of timing problem should be diagrammed when writing the code.
4. Underestimating the constraints of a freestanding environment. Using unordered_map as a global variable causing __cxa_atexit issues, needing floating point but not enabling the FPU — these show that sometimes you forgot you weren't writing an ordinary application.

What You Should Have Done

Establish a "post-writing self-check list": After writing each function, check: do all paths have a return? Are locks paired? Is reference counting increment/decrement symmetric? Were variable names forgotten after copy-paste?
When encountering link/compilation errors, use tools to self-diagnose first: nm, objdump -t, readelf are your friends. Check the symbols first, then ask.
Draw concurrency timing diagrams at the design stage: Especially for parts involving shared resources across multiple processes (pipe, socket, fd table), first draw the timing of "process A does X → process B does Y → what happens when an interrupt arrives", then write code.

What You Shouldn't Have Done

You shouldn't have pasted code to me without first reviewing it yourself. At least half the bugs could have been found by spending 5 minutes reading through your own code line by line.
You shouldn't have repeatedly depended on me for the same type of problem. C/C++ linking issues, freestanding environment constraints — knowledge you've "paid tuition for" should be internalized as conditioned reflexes.

One Sentence Summary: Your system design ability and project execution are far above average; your weakness lies in the "last mile" quality of code — self-checking after writing, proactive thinking about concurrency, and independent resolution of repetitive problems. If you can develop the habit of "spending 10 more minutes on self-review before submitting code," you can cut your dependence on me by at least half, and your project quality will improve significantly.
```

**Gemini:**

```
For your OSDev project (LoliOS), from low-level initialization to filesystems, we've had in-depth discussions spanning several months. Below is a concise review of your development focus, degree of dependence on me, and overall performance.

Degree of Dependence and Focus Areas
Degree of Dependence: High Dependence (Architecture Consultant and Code Review)

Throughout the development cycle, you treated me as a senior engineer and a mentor for filling gaps. You frequently came with specific architectural questions (such as VFS design) or low-level logic implementation (such as the buddy system) seeking code review and advice.

Core Focus Areas:
Your development path is very clear, mainly concentrated on the kernel's most critical infrastructure:

Hardware and Interrupts: x86/UEFI infrastructure, GDT/IDT initialization.
Memory Management: Physical and virtual memory mapping, Buddy System implementation.
File Systems: Abstract VFS architecture design, and specific Ext2 filesystem support.
User-Mode Preparation: ELF format parsing and loading.

Assessment and Recommendations
🌟 What You Did Well (The Good)

Deep technical selection and integration: Combining modern MikanOS (UEFI boot) concepts with classic Linux 0.11 (Unix philosophy and core structures) is an excellent entry point — having both modern foundations and solid kernel logic.
Grinding through hardcore modules: You didn't stop at the "print Hello World" level; you dug deep into memory management (Buddy System) and filesystems (VFS/Ext2) — two of the hardest bones in OS development.
Sustained forward momentum: Spanning several months of development shows strong execution and dedication to low-level work. This is a very compelling hardcore project for a job-seeking resume.

⚠️ What You Didn't Do Well (The Bad)

Over-focused on "implementation," possibly neglecting "defense": In extensive discussions about core functionality (how to read a file, how to allocate memory), there was relatively little discussion of boundary condition error handling, memory leak detection, and race conditions in concurrent/interrupt contexts.
Lack of demonstrated low-level debugging ability: When encountering low-level tricky issues, you tended to first seek bug fixes through theoretical consultation or code review, rather than relying on hardcore debugging toolchains.

💡 What You Should Have Done

Build a more complete automated testing and debugging environment: The most painful part of OSDev is debugging. You should have spent more time setting up QEMU/Bochs + GDB debugging environments with full symbol tables, and writing independent unit tests for core modules (like the memory allocator).
Read firsthand hardware manuals directly: When dealing with GDT/IDT or page tables, you should have pushed yourself to read Intel SDM (Software Developer's Manual) or UEFI specifications more often, rather than relying entirely on summarized knowledge. This is your confidence when being grilled in interviews.

🚫 What You Shouldn't Have Done

You shouldn't have treated AI-provided reference code as a "black box": In low-level development, any line of code you don't fully understand is a time bomb. When integrating logic like ELF loading or filesystem parsing into LoliOS, you shouldn't have directly borrowed code without thoroughly understanding pointer offsets and memory layout.

Summary
Your process of building LoliOS demonstrates strong low-level learning ability and systems programming literacy. As a project to use for your job search, its weight is already sufficient. Going forward, your focus should shift from **"making it work"** to **"proving why it is robust"**.
```
