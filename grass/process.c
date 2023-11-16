/*
 * (C) 2022, Cornell University
 * All rights reserved.
 */

/* Author: Yunhao Zhang
 * Description: helper functions for managing processes
 */

#include "egos.h"
#include "process.h"
#include "syscall.h"
#include <string.h>
#include <stdatomic.h> // Include for atomic operations

// Function to atomically set the status of a process with a given process ID (pid)
static void proc_set_status(int pid, int status) {
    for (int i = 0; i < MAX_NPROCESS; i++) // Loop through all processes
        if (atomic_load(&proc_set[i].pid) == pid) // Atomically load the pid and check
            atomic_store(&proc_set[i].status, status); // Atomically set the status if the pid matches
}

// Function to set the status of a process with a given process ID (pid)
// static void proc_set_status(int pid, int status) {
//     for (int i = 0; i < MAX_NPROCESS; i++) // Loop through all processes
//         if (proc_set[i].pid == pid) proc_set[i].status = status; // If the process ID matches, set its status
// }

// Set the status of a process to PROC_READY
void proc_set_ready(int pid) { proc_set_status(pid, PROC_READY); }

// Set the status of a process to PROC_RUNNING
void proc_set_running(int pid) { proc_set_status(pid, PROC_RUNNING); }

// Set the status of a process to PROC_RUNNABLE
void proc_set_runnable(int pid) { proc_set_status(pid, PROC_RUNNABLE); }


// Atomically allocate a new process
int proc_alloc() {
    static _Atomic int proc_nprocs = 0; // Atomic counter for the number of processes
    for (int i = 0; i < MAX_NPROCESS; i++) // Loop through all processes
        if (atomic_load(&proc_set[i].status) == PROC_UNUSED) { // Atomically check if the status is unused
            int new_pid = atomic_fetch_add(&proc_nprocs, 1); // Atomically increment and fetch the process count
            atomic_store(&proc_set[i].pid, new_pid); // Atomically set the new process ID
            atomic_store(&proc_set[i].status, PROC_LOADING); // Atomically set the process status to loading
            return new_pid; // Return the new process ID
        }

    FATAL("proc_alloc: reach the limit of %d processes", MAX_NPROCESS); // If no process slot is available, raise a fatal error
}
// Allocate a new process
// int proc_alloc() {
//     static int proc_nprocs = 0; // Static counter for the number of processes
//     for (int i = 0; i < MAX_NPROCESS; i++) // Loop through all processes
//         if (proc_set[i].status == PROC_UNUSED) { // Find an unused process slot
//             proc_set[i].pid = ++proc_nprocs; // Assign a new process ID
//             proc_set[i].status = PROC_LOADING; // Set the process status to loading
//             return proc_nprocs; // Return the new process ID
//         }

//     FATAL("proc_alloc: reach the limit of %d processes", MAX_NPROCESS); // If no process slot is available, raise a fatal error
// }



// Atomically free a process with a given process ID
void proc_free(int pid) {
    if (pid != -1) { // If a specific process ID is provided
        earth->mmu_free(pid); // Free the memory associated with the process
        proc_set_status(pid, PROC_UNUSED); // Atomically set the process status to unused
        return;
    }

    // If no specific process ID is provided, free all user applications
    for (int i = 0; i < MAX_NPROCESS; i++) // Loop through all processes
        if (atomic_load(&proc_set[i].pid) >= GPID_USER_START &&
            atomic_load(&proc_set[i].status) != PROC_UNUSED) { // Atomically check if the process is a user application and not unused
            earth->mmu_free(atomic_load(&proc_set[i].pid)); // Free the memory associated with the process
            atomic_store(&proc_set[i].status, PROC_UNUSED); // Atomically set the process status to unused
        }
}

// Free a process with a given process ID
// void proc_free(int pid) {
//     if (pid != -1) { // If a specific process ID is provided
//         earth->mmu_free(pid); // Free the memory associated with the process
//         proc_set_status(pid, PROC_UNUSED); // Set the process status to unused
//         return;
//     }

//     // If no specific process ID is provided, free all user applications
//     for (int i = 0; i < MAX_NPROCESS; i++) // Loop through all processes
//         if (proc_set[i].pid >= GPID_USER_START &&
//             proc_set[i].status != PROC_UNUSED) { // If the process is a user application and not unused
//             earth->mmu_free(proc_set[i].pid); // Free the memory associated with the process
//             proc_set[i].status = PROC_UNUSED; // Set the process status to unused
//         }
// }
