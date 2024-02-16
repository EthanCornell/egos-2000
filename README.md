## Vision

This project's vision is to help **every** college student read **all** the code of an operating system.

With only **2000** lines of code, egos-2000 implements every component of an operating system for education. 
It can run on RISC-V boards and the QEMU software emulator.

![Fail to load an image of egos-2000.](references/screenshots/egos-2000.jpg)

```shell
# The cloc utility is used to count the lines of code (LOC).
# The command below counts the LOC of everything excluding text documents.
> cloc egos-2000 --exclude-ext=md,txt
...
github.com/AlDanial/cloc v 1.94  T=0.05 s (949.3 files/s, 62349.4 lines/s)
-------------------------------------------------------------------------------
Language                     files          blank        comment           code
-------------------------------------------------------------------------------
C                               37            508            657           1577
C/C++ Header                    10             69            105            283
Assembly                         3             10             27             76
make                             1             14              3             64
-------------------------------------------------------------------------------
SUM:                            51            601            792           2000 (exactly 2000!)
-------------------------------------------------------------------------------
```

## Earth and Grass Operating System

The **egos** part of egos-2000 is named after its three-layer architecture.

* The **earth layer** implements hardware-specific abstractions.
    * tty and disk device interfaces
    * timer, exception and memory management interfaces
* The **grass layer** implements hardware-independent abstractions.
    * process control block and system call interfaces
* The **application layer** implements file system, shell and user commands.

The definitions of `struct earth` and `struct grass` in [this header file](library/egos.h) specify the layer interfaces.

Developed for [CS4411 at Cornell](https://www.cs.cornell.edu/courses/cs4411/2022fa/schedule/),
egos-2000 also has a [special version](https://github.com/yhzhang0128/egos-2000/tree/ece4750) running on the Verilog processor from [ECE4750 at Cornell](https://github.com/cornell-ece4750).
The goal is to make OS education more connected with computer architecture.


## Main Change from Original Edition
In this edition of egos-2000, I have introduced several significant changes and improvements compared to the original edition. Some of the main changes include:

## 1. CPU Memory Management Unit (MMU) Development

In the `cpu_mmu.c` file, significant developments were made to enhance the Memory Management Unit (MMU) for our operating system. This work included the implementation of critical functions such as `setup_identity_region`, `pagetable_identity_mapping`, `page_table_map`, and MMU initialization.

### Highlights:

- **Setup Identity Region Function:** Implemented memory identity mapping using bitwise operations and managed virtual-to-physical address mappings, improving system stability and efficiency.

- **Page Table Identity Mapping Function:** Developed identity mappings for various memory regions, including system-specific components, resulting in more efficient memory utilization.

- **Page Table Map Function:** Created a function to link virtual pages to physical frames with precise page-to-frame mappings, reducing the risk of critical system errors.

- **MMU Initialization Function** Successfully initialized the Memory Management Unit (MMU), managing physical memory protection (PMP) and memory translation mechanisms, contributing to enhanced system security and performance.

These enhancements have led to a more robust MMU setup, improving system stability, security, and performance, ultimately reducing the likelihood of critical system errors.

## 2. Operating System Kernel Development

In the `kernel.c` file, significant enhancements and optimizations were made to the operating system kernel, focusing on improving process scheduling, fault handling, and system calls to increase overall efficiency and reliability.

### Highlights:

- **Advanced Programming Techniques:** Leveraged advanced low-level C programming skills, including pointer manipulation, bit operations, and memory-mapped I/O, to optimize hardware interaction.

- **Exception and Interrupt Handling:** Implemented robust exception and interrupt handling mechanisms, effectively managing various system exceptions and interrupts through functions like `excp_entry` and `intr_entry`.

- **Process Scheduling and Context Switching:** Developed and refined process scheduling logic and context switching methods in `proc_yield` and `ctx_entry`, resulting in enhanced system efficiency.

- **Inter-Process Communication (IPC):** Streamlined inter-process communication (IPC) mechanisms with the development of `proc_send` and `proc_recv`, facilitating efficient communication between processes.

- **Hardware-Software Interface Management:** Managed the hardware-software interface by handling system register management, incorporating comprehensive error checking, and implementing debugging procedures.

These enhancements and optimizations have led to improvements in system performance and reliability, contributing to a more robust and efficient operating system.

## 3. File System Optimization Using Zero-Copy Techniques

In the `file.c` file, extensive optimizations were applied to the file system, with a focus on zero-copy techniques to improve overall system performance and responsiveness.

### Key Points for Zero-Copy Optimization:

- **Direct Write to Disk:** Implemented direct I/O strategies to write data directly from user-space memory buffers to the disk, reducing intermediate copies.

- **Minimize Internal Copying:** Avoided unnecessary data copying within functions, especially when dealing with indirect blocks.

- **Efficient Buffer Management:** Managed buffers efficiently for reading blocks, such as indirect blocks, to reduce memory operation overhead.

### Proposed Optimizations:

- **Direct Disk Writes:** Utilized direct I/O techniques for writing blocks to the disk, reducing unnecessary copying and improving efficiency.

- **Optimized Indirect Block Handling:** Minimized the number of read and write operations for indirect blocks, improving I/O efficiency.

- **Inode Updates:** Grouped inode updates to minimize write operations, enhancing file system performance.

- **Error Handling and Recovery:** Implemented robust error handling, ensuring file system consistency and recovery in case of failures.

### Results:

- **Performance Enhancement:** These optimizations significantly enhanced file system performance, reducing latency for write operations, and improving overall system responsiveness.

These optimizations collectively resulted in enhanced file system performance, reduced latency for write operations, and improved overall system responsiveness.

## 4. Cache Management and Concurrency Control

In the `dev_page.c` file, the work was undertaken to enhance cache management and ensure thread safety in a multi-threaded environment, resulting in improved system performance, efficiency, and reliability.

### Cache Management:

- **Enhanced Cache Eviction Policy:** Optimized cache eviction policies to improve system efficiency, reducing unnecessary disk I/O.

- **Efficient Cache Slot Finding:** Developed an efficient cache slot finding algorithm (Writeback-Aware Cache) within the `paging_write` function, improving cache management.

- **Streamlined Paging Operations:** Streamlined the `paging_read` function, optimizing cache slot search and implementing lazy loading to minimize disk I/O.

- **Memory Operation Efficiency:** Utilized `memcpy` judiciously for memory operations, reducing system overhead and enhancing performance.

### Concurrency Control:

- **Thread Safety Implementation:** Implemented robust concurrency control mechanisms using pthread mutexes, safeguarding critical sections in cache operations.

- **Thorough Testing:** Conducted comprehensive testing to ensure smooth integration and prevent deadlocks, significantly enhancing system reliability and stability.

These actions collectively resulted in a improvement in cache management efficiency, reduced disk I/O, enhanced system performance, and ensured thread safety and data integrity in multi-threaded scenarios.

## 5. Thread Management and Synchronization

In the `ult.c` file, substantial work was undertaken to develop a Thread Management System, context switching mechanism, and advanced synchronization techniques for efficient multi-threading in a C-based project.

### Thread Management:

- **Thread Control Block:** Developed a Thread Control Block (TCB) to efficiently manage thread-related information.

- **Critical Thread Functions:** Created and implemented critical thread management functions, including thread creation, yielding, and termination.

- **Context Switching Mechanism:** Implemented a Context Switching Mechanism using `setjmp` and `longjmp` functions to ensure seamless context switching and effective thread state management for cooperative multitasking.

- **Improved Performance:** Enhanced thread management, resulting in improved system performance and responsiveness.

### Advanced Thread Management and Synchronization:

- **Resilient System:** Built a resilient system for thread life-cycle management, including creation, context switching, and termination.

- **Semaphore-Based Solutions:** Developed semaphore-based solutions for handling producer-consumer problems, ensuring safe multi-threaded operations and optimized resource use.

- **Dynamic Memory Management:** Constructed a comprehensive thread management framework with capabilities like `thread_create`, `thread_yield`, and `thread_exit`, integrating dynamic memory management for thread stacks.

- **Semaphore Operations:** Implemented a `sema` struct for semaphore operations, crafting functions such as `sema_init`, `sema_inc`, and `sema_dec` for regulated access to shared resources in a multi-threaded environment.

- **Producer-Consumer Handling:** Orchestrated producer-consumer processes using semaphores to efficiently handle shared buffers, ensuring synchronized operations between producers and consumers.

- **Performance Enhancement:** Achieved a significant enhancement in the multi-threading mechanism, resulting in more effective context switching and thread administration.

- **Resource Distribution:** Facilitated improved resource distribution among threads, leading to enhanced application performance and stability in multi-threaded contexts.

This work provided profound expertise and hands-on experience in low-level thread management and inter-thread communication within C programming, ultimately contributing to a more robust and efficient multi-threaded system.


### Usages and Documentation

For compiling and running egos-2000, please read [this document](references/USAGES.md).
The [RISC-V instruction set manual](references/riscv-privileged-v1.10.pdf) and [SiFive FE310 processor manual](references/sifive-fe310-v19p04.pdf) introduce the privileged ISA and memory map.
[This document](references/README.md) introduces the teaching plans, software architecture and development history.

For any questions, please contact [Yunhao Zhang](https://dolobyte.net/).

## Acknowledgements

Many thanks to Meta for a [Meta fellowship](https://research.facebook.com/fellows/zhang-yunhao/).
Many thanks to [Robbert van Renesse](https://www.cs.cornell.edu/home/rvr/), [Lorenzo Alvisi](https://www.cs.cornell.edu/lorenzo/), [Shan Lu](https://people.cs.uchicago.edu/~shanlu/), [Hakim Weatherspoon](https://www.cs.cornell.edu/~hweather/) and [Christopher Batten](https://www.csl.cornell.edu/~cbatten/) for their support.
Many thanks to all the CS5411/4411 students at Cornell over the years for helping improve this course.
Many thanks to [Cheng Tan](https://naizhengtan.github.io/) for providing valuable feedback and using egos-2000 in his [CS6640 at Northeastern](https://naizhengtan.github.io/23fall/).
Many thanks to Brandon Fusi for [porting to the Allwinner's D1 chip](https://github.com/cheofusi/egos-2000-d1) using Sipeed's Lichee RV64 compute module.

## Reference

- [Writeback-Aware Caching](https://www.pdl.cmu.edu/PDL-FTP/Storage/2020.apocs.writeback.pdf)
- [Brief Announcement: Writeback-Aware Caching](https://www.cs.cmu.edu/~beckmann/publications/papers/2019.spaa.ba.writeback.pdf)
