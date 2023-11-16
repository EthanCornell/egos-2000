/*
 * (C) 2022, Cornell University
 * All rights reserved.
 */

/* Author: Yunhao Zhang
 * Description: Kernel ≈ 3 handlers
 *     proc_yield() handles timer interrupt for process scheduling
 *     excp_entry() handles faults such as unauthorized memory access
 *     proc_syscall() handles system calls for inter-process communication
 */


#include "egos.h"
#include "process.h"
#include "syscall.h"
#include <string.h>

#define EXCP_ID_ECALL_U    8  // Defines an exception ID for user mode system calls.
#define EXCP_ID_ECALL_M    11 // Defines an exception ID for machine mode system calls.

void excp_entry(int id) {
    /* Exception handling entry point function. */

    if (id == EXCP_ID_ECALL_U) {
        // Handle user mode system call exception.
        proc_syscall(); // Calls a function to process the system call.
        return;
    } else if (id == EXCP_ID_ECALL_M && curr_pid >= GPID_USER_START) {
        // Handle machine mode system call exception for user processes.
        // Logs information and performs cleanup if necessary.
        INFO("process %d killed due to exception", curr_pid);
        asm("csrw mepc, %0" ::"r"(0x800500C)); // Sets the Machine Exception Program Counter (mepc) to a specific address.
        return;
    }
    // If the exception is not handled, logs a fatal error.
    FATAL("excp_entry: kernel got exception %d", id);
}


#define INTR_ID_SOFT       3  // Defines an interrupt ID for software interrupts.
#define INTR_ID_TIMER      7  // Defines an interrupt ID for timer interrupts.

static void proc_yield();           // Forward declaration of a function to yield the processor.
static void proc_syscall();         // Forward declaration of a function to handle system calls.
static void (*kernel_entry)();      // Declaration of a function pointer for kernel entry.

int proc_curr_idx;                  // Variable to store the current process index.
struct process proc_set[MAX_NPROCESS]; // Array to hold process control blocks.

void intr_entry(int id) {
    /* Interrupt handling entry point function. */

    if (id == INTR_ID_TIMER && curr_pid < GPID_SHELL) {
        // If the interrupt is a timer interrupt and the current process is a kernel process,
        // reset the timer and return without handling.
        earth->timer_reset();
        return;
    }

    if (earth->tty_recv_intr() && curr_pid >= GPID_USER_START) {
        // If a terminal interrupt is received and the current process is a user process,
        // log the information and set mepc.
        INFO("process %d killed by interrupt", curr_pid);
        asm("csrw mepc, %0" ::"r"(0x800500C));
        return;
    }

    // Depending on the interrupt ID, set the appropriate kernel entry function.
    if (id == INTR_ID_SOFT)
        kernel_entry = proc_syscall; // For software interrupts, handle system calls.
    else if (id == INTR_ID_TIMER)
        kernel_entry = proc_yield;   // For timer interrupts, yield the processor.
    else
        // If the interrupt ID is unknown, log a fatal error.
        FATAL("intr_entry: got unknown interrupt %d", id);

    // Switch to the kernel stack for further processing.
    ctx_start(&proc_set[proc_curr_idx].sp, (void*)GRASS_STACK_TOP);
}

void ctx_entry() {
    /* Entering kernel context from user application or another kernel function. */

    int mepc, tmp;
    asm("csrr %0, mepc" : "=r"(mepc)); // Read the Machine Exception Program Counter (mepc) register into variable 'mepc'.
    proc_set[proc_curr_idx].mepc = (void*) mepc; // Store the mepc value in the current process's structure.

    /* Call the appropriate kernel function, which is either proc_yield() or proc_syscall(). */
    kernel_entry();

    /* Switching back to the user application stack. */
    mepc = (int)proc_set[proc_curr_idx].mepc; // Retrieve the mepc value from the current process's structure.
    asm("csrw mepc, %0" ::"r"(mepc)); // Write the mepc value back to the mepc register.
    ctx_switch((void**)&tmp, proc_set[proc_curr_idx].sp); // Perform context switch to the user stack.
}


static void proc_yield() {
    /* Find the next runnable process. */
    int next_idx = -1;
    for (int i = 1; i <= MAX_NPROCESS; i++) {
        int s = proc_set[(proc_curr_idx + i) % MAX_NPROCESS].status;
        if (s == PROC_READY || s == PROC_RUNNING || s == PROC_RUNNABLE) {
            next_idx = (proc_curr_idx + i) % MAX_NPROCESS;
            break;
        }
    }

    if (next_idx == -1) FATAL("proc_yield: no runnable process"); // Fatal error if no runnable process is found.
    if (curr_status == PROC_RUNNING) proc_set_runnable(curr_pid); // Set the current process status to runnable if it's running.

    /* Switch to the next runnable process and reset the timer. */
    proc_curr_idx = next_idx;
    earth->mmu_switch(curr_pid); // Switch the Memory Management Unit (MMU) context to the current process.
    earth->timer_reset(); // Reset the system timer.

    /* Modify mstatus.MPP to enter machine or user mode during mret. */
    int mstatus;
    asm("csrr %0, mstatus" : "=r"(mstatus));
    if (curr_pid >= GPID_USER_START) {
        mstatus &= ~(0x3 << 11); // Set MPP to user mode (00).
    } else {
        mstatus |= (0x3 << 11); // Set MPP to machine mode (11).
    }
    asm("csrw mstatus, %0" :: "r"(mstatus)); // Write the updated mstatus value back to the register.

    /* Prepare for entering application code if the process is ready. */
    if (curr_status == PROC_READY) {
        proc_set_running(curr_pid); // Update the process status to running.
        /* Setup arguments for the application (argc and argv). */
        asm("mv a0, %0" ::"r"(APPS_ARG));
        asm("mv a1, %0" ::"r"(APPS_ARG + 4));
        /* Set mepc to the application entry point and use mret to enter the application. */
        asm("csrw mepc, %0" ::"r"(APPS_ENTRY));
        asm("mret");
    }

    proc_set_running(curr_pid); // Update the process status to running.
}


static void proc_send(struct syscall *sc) {
    /* Implementing a message sending syscall. */
    sc->msg.sender = curr_pid; // Set the sender of the message.
    int receiver = sc->msg.receiver;

    for (int i = 0; i < MAX_NPROCESS; i++) {
        if (proc_set[i].pid == receiver) {
            /* Find the process that should receive the message. */
            if (proc_set[i].status != PROC_WAIT_TO_RECV) {
                curr_status = PROC_WAIT_TO_SEND; // If receiver is not waiting to receive, set current process to waiting to send.
                proc_set[proc_curr_idx].receiver_pid = receiver; // Record the receiver PID in the current process structure.
            } else {
                /* If receiver is ready to receive, perform message passing. */
                struct sys_msg tmp;
                earth->mmu_switch(curr_pid); // Switch MMU context to the current process.
                memcpy(&tmp, &sc->msg, sizeof(tmp)); // Copy the message from the sender.

                earth->mmu_switch(receiver); // Switch MMU context to the receiver.
                memcpy(&sc->msg, &tmp, sizeof(tmp)); // Copy the message to the receiver.

                proc_set_runnable(receiver); // Set the receiver process status to runnable.
            }
            proc_yield(); // Yield processor to handle scheduling.
            return;
        }
    }

    sc->retval = -1; // Set return value to -1 if receiver not found.
}



static void proc_recv(struct syscall *sc) {
    /* This function handles the receiving part of inter-process communication. */

    // Check if the syscall structure pointer is NULL, indicating an invalid argument.
    if (sc == NULL) {
        FATAL("proc_recv: Invalid syscall structure"); // Log a fatal error if the syscall structure is invalid.
        return; // Exit the function early as it can't proceed without a valid syscall structure.
    }

    int sender = -1; // Initialize the sender variable to -1, indicating no sender has been found yet.
    for (int i = 0; i < MAX_NPROCESS; i++) {
        /* Loop through all processes to find a potential sender. */
        if (proc_set[i].status == PROC_WAIT_TO_SEND &&
            proc_set[i].receiver_pid == curr_pid) {
            // If a process is waiting to send a message to the current process, it's identified as the sender.
            sender = proc_set[i].pid; // Store the sender's process ID.
            break; // Break the loop as the sender has been found.
        }
    }

    if (sender == -1) {
        // If no sender is found, set the current process's status to waiting to receive.
        curr_status = PROC_WAIT_TO_RECV;
    } else {
        /* If a sender is found, perform the message receiving operations. */
        struct sys_msg tmp; // Temporary structure to hold the message.
        earth->mmu_switch(sender); // Switch the MMU context to the sender process.
        // Additional error checking after mmu_switch could be added here.

        // Check if the message pointer in the syscall structure is valid.
        if (&sc->msg == NULL) {
            FATAL("proc_recv: Message structure is invalid"); // Log a fatal error if the message structure is invalid.
            return; // Exit the function as it can't proceed with an invalid message structure.
        }
        memcpy(&tmp, &sc->msg, sizeof(tmp)); // Copy the message from the syscall structure to the temporary structure.

        /* Switch the MMU context back to the current (receiver) process and copy the message. */
        earth->mmu_switch(curr_pid); // Switch the MMU context to the current process.
        // Additional error checking after mmu_switch could be added here as well.
        memcpy(&sc->msg, &tmp, sizeof(tmp)); // Copy the message from the temporary structure to the syscall structure.

        /* Mark the sender process as runnable so it can continue execution. */
        proc_set_runnable(sender);
    }

    proc_yield(); // Yield the CPU to allow other processes to run.
}




// static void proc_recv(struct syscall *sc) {
//     int sender = -1;
//     for (int i = 0; i < MAX_NPROCESS; i++)
//         if (proc_set[i].status == PROC_WAIT_TO_SEND &&
//             proc_set[i].receiver_pid == curr_pid)
//             sender = proc_set[i].pid;

//     if (sender == -1) {
//         curr_status = PROC_WAIT_TO_RECV;
//     } else {
//         /* Copy message from sender to kernel stack */
//         struct sys_msg tmp;
//         earth->mmu_switch(sender);
//         memcpy(&tmp, &sc->msg, sizeof(tmp));

//         /* Copy message from kernel stack to receiver */
//         earth->mmu_switch(curr_pid);
//         memcpy(&sc->msg, &tmp, sizeof(tmp));

//         /* Set sender process as runnable */
//         proc_set_runnable(sender);
//     }

//     proc_yield();
// }


static void proc_syscall() {
    /* This function is designed to handle system calls. */

    // Check if SYSCALL_ARG is NULL, which indicates a problem as it should be pointing to a syscall structure.
    if (SYSCALL_ARG == NULL) {
        FATAL("proc_syscall: SYSCALL_ARG is null"); // Log a fatal error if SYSCALL_ARG is null.
        return; // Exit the function since it can't proceed without a valid SYSCALL_ARG.
    }

    struct syscall *sc = (struct syscall*)SYSCALL_ARG; // Cast SYSCALL_ARG to a pointer of type 'struct syscall'.

    // Check if the syscall structure pointer is NULL after casting, which should not happen.
    if (sc == NULL) {
        FATAL("proc_syscall: Invalid syscall structure"); // Log a fatal error if the syscall structure pointer is invalid.
        return; // Exit the function since it can't proceed without a valid syscall structure.
    }

    int type = sc->type; // Retrieve the type of the syscall from the syscall structure.

    // Initialize the return value of the syscall to 0 and reset the syscall type to SYS_UNUSED.
    sc->retval = 0;
    sc->type = SYS_UNUSED;
    *((int*)0x2000000) = 0; // Reset a memory-mapped flag (possibly indicating syscall completion) to 0.

    // Switch statement to handle different types of syscalls.
    switch (type) {
    case SYS_RECV:
        proc_recv(sc); // Handle a receive syscall.
        break;
    case SYS_SEND:
        proc_send(sc); // Handle a send syscall.
        break;
    default:
        // Log a fatal error if an unknown syscall type is encountered.
        FATAL("proc_syscall: got unknown syscall type=%d", type);
    }
}



// static void proc_syscall() {
//     struct syscall *sc = (struct syscall*)SYSCALL_ARG;

//     int type = sc->type;
//     sc->retval = 0;
//     sc->type = SYS_UNUSED;
//     *((int*)0x2000000) = 0;

//     switch (type) {
//     case SYS_RECV:
//         proc_recv(sc);
//         break;
//     case SYS_SEND:
//         proc_send(sc);
//         break;
//     default:
//         FATAL("proc_syscall: got unknown syscall type=%d", type);
//     }
// }
