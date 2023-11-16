/*
 * (C) 2022, Cornell University
 * All rights reserved.
 */

/* Author: Yunhao Zhang & I-Hsuan Huang
 * Description: memory management unit (MMU)
 * implementation of 2 translation mechanisms: page table and software TLB
 */

#include "egos.h"
#include "disk.h"
#include "servers.h"
#include <string.h>
/* Interface of the paging device, see earth/dev_page.c */
void  paging_init();                 // Function prototype to initialize paging.
int   paging_invalidate_cache(int frame_id); // Invalidate cache for a frame.
int   paging_write(int frame_id, int page_no); // Write a frame to a page.
char* paging_read(int frame_id, int alloc_only); // Read from a frame, possibly allocating only.

/* Allocation and free of physical frames */
#define NFRAMES 256               // Define a constant for the number of frames.
struct frame_mapping {
    int use;     // Is the frame allocated?
    int pid;     // Which process owns the frame?
    int page_no; // Which virtual page is the frame mapped to?
} table[NFRAMES];                 // Array of frame mappings.

int mmu_alloc(int* frame_id, void** cached_addr) { // Allocate a frame.
    for (int i = 0; i < NFRAMES; i++) // Loop through the frames.
        if (!table[i].use) {    // If frame is not in use:
            *frame_id = i;       // Set the frame ID.
            *cached_addr = paging_read(i, 1); // Read the frame, allocating if necessary.
            table[i].use = 1;    // Mark the frame as used.
            return 0;            // Return success.
        }
    FATAL("mmu_alloc: no more available frames"); // If no frames available, fatal error.
}

int mmu_free(int pid) {             // Free frames for a process.
    for (int i = 0; i < NFRAMES; i++) // Loop through frames.
        if (table[i].use && table[i].pid == pid) { // If frame is used by the process:
            paging_invalidate_cache(i); // Invalidate the frame's cache.
            memset(&table[i], 0, sizeof(struct frame_mapping)); // Reset the frame's mapping.
        }
}

/* Software TLB Translation */
int soft_tlb_map(int pid, int page_no, int frame_id) { // Map a virtual page to a frame.
    table[frame_id].pid = pid;       // Set the process ID for the frame.
    table[frame_id].page_no = page_no; // Set the virtual page number for the frame.
}

int soft_tlb_switch(int pid) {       // Switch the TLB context for a process.
    static int curr_vm_pid = -1;     // Static variable for the current VM process ID.
    if (pid == curr_vm_pid) return 0; // If the process is already the current, return.

    // Unmap curr_vm_pid from the user address space
    for (int i = 0; i < NFRAMES; i++) // Loop through frames.
        if (table[i].use && table[i].pid == curr_vm_pid) // If frame belongs to current process:
            paging_write(i, table[i].page_no); // Write the frame to its page.

    // Map pid to the user address space
    for (int i = 0; i < NFRAMES; i++) // Loop through frames.
        if (table[i].use && table[i].pid == pid) // If frame belongs to new process:
            memcpy((void*)(table[i].page_no << 12), paging_read(i, 0), PAGE_SIZE); // Copy data from frame to virtual page.

    curr_vm_pid = pid;               // Update the current VM process ID.
}

/* Page Table Translation
 *
 * The code below creates an identity mapping using RISC-V Sv32;
 * Read section4.3 of RISC-V manual (references/riscv-privileged-v1.10.pdf)
 *
 * mmu_map() and mmu_switch() using page tables is given to students
 * as a course project. After this project, every process should have
 * its own set of page tables. mmu_map() will modify entries in these
 * tables and mmu_switch() will modify satp (page table base register)
 */

#define OS_RWX   0xF       // Define permission flags for the OS.
#define USER_RWX 0x1F      // Define permission flags for the user.
static unsigned int frame_id, *root, *leaf; // Static variables for frame ID and page table pointers.

/* 32 is a number large enough for demo purpose */
static unsigned int* pid_to_pagetable_base[32]; // Array mapping process ID to page table base.

void setup_identity_region(int pid, unsigned int addr, int npages, int flag) {
    int vpn1 = addr >> 22; // Calculate the first virtual page number component.

    if (root[vpn1] & 0x1) {
        // Leaf has been allocated
        leaf = (void*)((root[vpn1] << 2) & 0xFFFFF000); // Get the leaf page table address.
    } else {
        // Leaf has not been allocated
        earth->mmu_alloc(&frame_id, (void**)&leaf); // Allocate a frame for the leaf page table.
        table[frame_id].pid = pid; // Assign the frame to the process.
        memset(leaf, 0, PAGE_SIZE); // Initialize the leaf page table.
        root[vpn1] = ((unsigned int)leaf >> 2) | 0x1; // Set the root entry to point to the leaf page table.
    }

    // Setup the entries in the leaf page table
    int vpn0 = (addr >> 12) & 0x3FF; // Calculate the second virtual page number component.
    for (int i = 0; i < npages; i++) // Loop to set up each page.
        leaf[vpn0 + i] = ((addr + i * PAGE_SIZE) >> 2) | flag; // Map each page.
}

void pagetable_identity_mapping(int pid) {
    // Allocate the root page table and set the page table base (satp)
    earth->mmu_alloc(&frame_id, (void**)&root); // Allocate a frame for the root page table.
    table[frame_id].pid = pid; // Assign the frame to the process.
    memset(root, 0, PAGE_SIZE); // Initialize the root page table.
    pid_to_pagetable_base[pid] = root; // Set the process's page table base.

    // Allocate the leaf page tables
    setup_identity_region(pid, 0x02000000, 16, OS_RWX);   // Map CLINT region.
    setup_identity_region(pid, 0x10013000, 1, OS_RWX);    // Map UART0 region.
    setup_identity_region(pid, 0x10024000, 1, OS_RWX);    // Map SPI1 region.
    setup_identity_region(pid, 0x20400000, 1024, OS_RWX); // Map boot ROM region.
    setup_identity_region(pid, 0x20800000, 1024, OS_RWX); // Map disk image region.
    setup_identity_region(pid, 0x80000000, 1024, OS_RWX); // Map DTIM memory region.

    for (int i = 0; i < 8; i++) // Loop to map ITIM memory (32MB on QEMU).
        setup_identity_region(pid, 0x08000000 + i * 0x400000, 1024, OS_RWX); // Map each 4MB block.
}

int page_table_map(int pid, int page_no, int frame_id) {
    if (pid >= 32) FATAL("page_table_map: pid too large"); // Check for valid process ID.

    // Check if page tables for pid do not exist, build the tables
    if (!pid_to_pagetable_base[pid]) {
        pagetable_identity_mapping(pid); // Create identity mapping for the process.
    }

    // Calculate virtual page number (VPN) components
    int vpn1 = page_no >> 10; // Calculate the first virtual page number component.
    int vpn0 = page_no & 0x3FF; // Calculate the second virtual page number component.

    // Fetch or create root page table
    unsigned int *root = pid_to_pagetable_base[pid]; // Get the root page table for the process.
    unsigned int *leaf;

    if (root[vpn1] & 0x1) {
        leaf = (void*)((root[vpn1] << 2) & 0xFFFFF000); // Get the leaf page table address.
    } else {
        earth->mmu_alloc(&frame_id, (void**)&leaf); // Allocate a frame for the leaf page table.
        table[frame_id].pid = pid; // Assign the frame to the process.
        memset(leaf, 0, PAGE_SIZE); // Initialize the leaf page table.
        root[vpn1] = ((unsigned int)leaf >> 2) | 0x1; // Set the root entry to point to the leaf page table.
    }

    // Map the frame in the leaf page table
    leaf[vpn0] = (frame_id << 2) | USER_RWX; // Map the frame with user permissions.

    return 0;   // Indicating success
}

int page_table_switch(int pid) {
    if (pid >= 32) FATAL("page_table_switch: pid too large");

    /* Check if page tables for pid exist */
    if (!pid_to_pagetable_base[pid]) {
        FATAL("page_table_switch: page tables not initialized for pid");
    }

    unsigned int *root = pid_to_pagetable_base[pid];

    /* Update satp register with the base address of the page table */
    asm("csrw satp, %0" ::"r"(((unsigned int)root >> 12) | (1 << 31)));

    return 0;   // Indicating success
}

/* MMU Initialization */
void mmu_init() {
    /* Initialize the paging device */
    paging_init();

    /* Initialize MMU interface functions */
    earth->mmu_free = mmu_free;
    earth->mmu_alloc = mmu_alloc;

    /* Setup a PMP region for the whole 4GB address space */
    asm("csrw pmpaddr0, %0" : : "r" (0x40000000));
    asm("csrw pmpcfg0, %0" : : "r" (0xF));

    /* Student's code goes here (PMP memory protection). */

    /* Setup PMP TOR region 0x00000000 - 0x20000000 as r/w/x */
    asm("csrw pmpaddr1, %0" : : "r" (0x20000000 >> 2));
    asm("csrw pmpcfg0, %0"  : : "r" (0x1F | (1 << 7))); // TOR, R, W, X

    /* Setup PMP NAPOT region 0x20400000 - 0x20800000 as r/-/x */
    asm("csrw pmpaddr2, %0" : : "r" (((0x20400000 >> 2) & ~0x3F) | 0x1F));
    asm("csrw pmpcfg0, %0"  : : "r" (0x1D | (2 << 7))); // NAPOT, R, X

    /* Setup PMP NAPOT region 0x20800000 - 0x20C00000 as r/-/- */
    asm("csrw pmpaddr3, %0" : : "r" (((0x20800000 >> 2) & ~0x3F) | 0x1F));
    asm("csrw pmpcfg0, %0"  : : "r" (0x1C | (2 << 7))); // NAPOT, R

    /* Setup PMP NAPOT region 0x80000000 - 0x80004000 as r/w/- */
    asm("csrw pmpaddr4, %0" : : "r" (((0x80000000 >> 2) & ~0x3F) | 0x1F));
    asm("csrw pmpcfg0, %0"  : : "r" (0x1E | (2 << 7))); // NAPOT, R, W

    /* Arty board does not support supervisor mode or page tables */
    earth->translation = SOFT_TLB;
    earth->mmu_map = soft_tlb_map;
    earth->mmu_switch = soft_tlb_switch;
    if (earth->platform == ARTY) return;

    /* Choose memory translation mechanism in QEMU */
    CRITICAL("Choose a memory translation mechanism:");
    printf("Enter 0: page tables\r\nEnter 1: software TLB\r\n");

    char buf[2];
    for (buf[0] = 0; buf[0] != '0' && buf[0] != '1'; earth->tty_read(buf, 2));
    earth->translation = (buf[0] == '0') ? PAGE_TABLE : SOFT_TLB;
    INFO("%s translation is chosen", earth->translation == PAGE_TABLE ? "Page table" : "Software");

    if (earth->translation == PAGE_TABLE) {
        /* Setup an identity mapping using page tables */
        pagetable_identity_mapping(0);
        asm("csrw satp, %0" ::"r"(((unsigned int)root >> 12) | (1 << 31)));

        earth->mmu_map = page_table_map;
        earth->mmu_switch = page_table_switch;
    }
}
