## Homemade OS (0): A Preface of Beginnings and Endings

This preface should have been written first, but in reality it was the last one to be written — hence the name "A Preface of Beginnings and Endings."

As you read through this series of articles, you'll see my thoughts and reflections throughout the process of building this operating system. Ideally, I want to showcase the good parts — watching a behemoth come to life from nothing, with tangible feedback at every step. But reality isn't always ideal. There are certainly not-so-good parts too — moments of hesitation, struggle, and reluctant compromises...

**It's worth noting that this series of articles can hardly be called a tutorial.** Strictly speaking, these are just records of my thoughts and study notes — before this, I didn't have a single line of kernel programming experience. The purpose of documenting all of this is simple: I want to share the joy of developing an operating system with you. Of course, joy doesn't mean it's easy or quick. I believe the joy lies in the process itself — the process of learning, of challenging yourself. AI is incredibly powerful nowadays, and I'm confident that someone could replicate everything I've done over these past months in just one day (and probably do it better than me!). But if you can find even just one useful insight while reading these articles, and recall it later in your own development journey — that would be wonderful. You would have given my documentation a deeper meaning.

If you'd like to know what I did, what I didn't do, and what I plan to do, please read the final "Afterword" article.

If you have any questions, please raise them in the Issues section. I just ask that you don't ask anything too difficult, or I might not be able to answer!

Note: This project was originally called "lolios" before being renamed to "moeos," so you'll see many references to "lolios" throughout the later articles.

------

### Roadmap

![img](../assets/%E8%87%AA%E5%88%B6%E6%93%8D%E4%BD%9C%E7%B3%BB%E7%BB%9F%EF%BC%880%EF%BC%89%EF%BC%9A%E5%BC%80%E5%A7%8B%E4%B8%8E%E7%BB%93%E6%9D%9F%E7%9A%84%E5%BA%8F%E8%A8%80/9998225831198834-f67cb2ca-e5ed-4f7c-818d-0b4b0a0d8b31-image_task_01KMAB58HRPMTEH4AJ0RTCTKXD.jpg)

A picture is worth a thousand words.

Above is the Roadmap I created using Nano Banana Pro for this tutorial. I hope it gives you an intuitive view of what we'll be implementing next.

If you don't like looking at pictures, here's my explanation:

At the start, we'll follow the OSDev Wiki's Bare Bones tutorial to create a kernel-level "Hello World." Think of this as the training ground of a starter village — it gives you a taste of "combat" and makes kernel programming feel less like an unattainable castle in the sky. Later, for the project's long-term stability, we'll refactor Bare Bones into Meaty Skeleton, which has a more standardized project structure. We'll also introduce Makefiles for build management, so you don't have to rebuild all source code every time. For the even longer term, it also prepares you for multi-platform support — when you later want to run your OS on more than just i386 CPUs, you can simply create a new subfolder and write the appropriate adaptation code...[^1]

With Meaty Skeleton in place, we can try to cut the "umbilical cord" to Multiboot and start setting up our own GDT and IDT, leaving the starter village for independent development. The Interrupt Handler will be your good helper.

After leaving the starter village, we enter the realm of memory management — memory is vast, but managing it well is no easy task. Before managing memory, we'll first discuss how to follow convention and migrate the kernel to the Higher Half. Then we'll use the clever Buddy System algorithm to manage physical memory. To give processes hosted on our system the illusion of exclusive memory space (and to keep them from interfering with each other), we'll use Recursive Paging to manage virtualized memory at page granularity. Finally, to allocate memory at a finer granularity, we'll implement a kernel heap manager using a coalescing linked list with the First Fit algorithm. With that, we'll have tamed memory at the kernel level.

With memory ready, we'll discuss the virtualization of another hardware resource: the CPU. We'll give processes the illusion of exclusive CPU access by having them take turns, and discuss some basic scheduling strategies to ensure reasonable fairness. We'll also cover how to save and restore each process's context, which involves switching kernel stacks and registers. With the foundation of kernel process creation, we'll further explore how to create user-mode processes — how to use a "trampoline" to go from Ring 0 (kernel mode) "back" to Ring 3 (user mode), and how to use special interrupts (system calls) to temporarily trap into kernel mode and invoke kernel code. Finally, we'll discuss how to separate user-mode programs from kernel code, packaging them as simple instruction stream files.

With basic process infrastructure in place, we'll move into the domain of file operations. We'll discuss how to create a Virtual File System (VFS) abstraction layer to hide the implementation details of all the file systems we'll adapt later, how to extract common characteristics of file systems and encapsulate them into interfaces. Then, on top of VFS, we'll implement a very simple and practical file system: TARFS, and discuss how to package it into our image using GRUB Module, loading it into memory at boot time so VFS can mount it.

With a basic file system available, we'll circle back to complete the process runtime infrastructure: implementing user-mode `malloc` and `free` for dynamic heap memory allocation, then "upgrading" user-mode program packaging to ELF format so our OS can properly load it, and finally implementing PipeFS on top of VFS for inter-process communication via pipes.

Now we have a fairly complete Hobby OS, but we won't stop here. We'll attempt to implement a TCP/IP stack: from the PCI bus and RTL8139 NIC driver, through ARP and ICMP, to the famous IP protocol, and finally a simple implementation of TCP and UDP (spoiler alert: due to time constraints, we'll keep things simple — no congestion control or retransmission; sending isn't even send-and-wait, it's a "brute force" direct send). Using this infrastructure, we'll implement Telnet access to our OS.

Back on the local front, we'll implement another form of inter-process communication: signals. This is a very practical mechanism — we can use interrupt signals to terminate stuck programs (no need to reboot the entire OS), and use KILL signals to terminate other processes or our own. Meanwhile, we won't settle for a read-only file system; we'll discuss how to adapt the Ext2 file system — obviously no easy task. Then we'll implement ProcFS and develop `ps`, `netstat`, and `kill` based on it.

At the end of the story, we'll finally try porting some software. Due to our earlier neglect of POSIX standards, we'll run into quite a few difficulties here. But ultimately, we'll port Kilo — the tiny text editor — to our system. And finally, we'll port DOOM to our system and proudly declare — It runs DOOM! With that, our OS journey will come to a pause.

So, turn the page, and let's begin.

[^1]: In fact, "thinking ahead" is very important. Looking back at the entire project from where I stand now, I've certainly suffered from many pitfalls caused by haste and short-sightedness (you'll see this as you read later articles)! Of course, this isn't meant to trap you in perfectionism — iterate in place, but don't expect your future self to clean up your mess. As they often say: don't trust every `// TODO` a programmer writes.
