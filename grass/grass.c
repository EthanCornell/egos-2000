/*
 * (C) 2022, Cornell University
 * All rights reserved.
 */

/* Author: Yunhao Zhang
 * Description: grass layer initialization
 * Initialize timer and the process control block; 
 * Spawn the first kernel process, GPID_PROCESS (pid=1).
 */

// Include headers for the operating system, process management, and system calls.
#include "egos.h"
#include "process.h"
#include "syscall.h"

// Define pointers to grass and earth structures, initializing them with predefined stack top addresses.
struct grass *grass = (void*)APPS_STACK_TOP;
struct earth *earth = (void*)GRASS_STACK_TOP;

// Function to read a block from the disk.
// 'block_no' specifies the block number to read, 'dst' is the destination buffer.
// It calls the 'disk_read' function of the 'earth' structure, offsetting the block number by SYS_PROC_EXEC_START.
static int sys_proc_read(int block_no, char* dst) {
    return earth->disk_read(SYS_PROC_EXEC_START + block_no, 1, dst);
}

// Main function of the program.
int main() {
    // Log entry into the grass layer of the operating system.
    CRITICAL("Enter the grass layer");

    // Initialize the grass interface functions for process management and system calls.
    grass->proc_alloc = proc_alloc;
    grass->proc_free = proc_free;
    grass->proc_set_ready = proc_set_ready;

    grass->sys_exit = sys_exit;
    grass->sys_send = sys_send;
    grass->sys_recv = sys_recv;

    // Register functions to handle interrupts and exceptions in the earth layer.
    earth->intr_register(intr_entry);
    earth->excp_register(excp_entry);
    
    // Load the first kernel process, identified by GPID_PROCESS.
    // It logs the information, loads the process using 'elf_load', and sets it to running.
    INFO("Load kernel process #%d: sys_proc", GPID_PROCESS);
    elf_load(GPID_PROCESS, sys_proc_read, 0, 0);
    proc_set_running(proc_alloc());
    earth->mmu_switch(GPID_PROCESS);

    // Setup for jumping to the entry point of process GPID_PROCESS.
    // It moves the APPS_ARG value to register 'a0' and then jumps to the address stored in APPS_ENTRY.
    asm("mv a0, %0" ::"r"(APPS_ARG));
    asm("jr %0" :: "r" (APPS_ENTRY));
}

