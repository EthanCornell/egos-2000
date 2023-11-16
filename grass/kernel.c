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

#define EXCP_ID_ECALL_U    8
#define EXCP_ID_ECALL_M    11

void excp_entry(int id) {
    /* Student's code goes here (system call and memory exception). */

    if (id == EXCP_ID_ECALL_U) {
        // If id is for system call, handle the system call and return
        proc_syscall();
        return;
    } else if (id == EXCP_ID_ECALL_M && curr_pid >= GPID_USER_START) {
        // Kill the process if curr_pid is a user application
        // Handle unauthorized memory access or other exceptions here
        INFO("process %d killed due to exception", curr_pid);
        asm("csrw mepc, %0" ::"r"(0x800500C));
        return;
    }
    FATAL("excp_entry: kernel got exception %d", id);
}


#define INTR_ID_SOFT       3
#define INTR_ID_TIMER      7

static void proc_yield();
static void proc_syscall();
static void (*kernel_entry)();

int proc_curr_idx;
struct process proc_set[MAX_NPROCESS];

void intr_entry(int id) {
    if (id == INTR_ID_TIMER && curr_pid < GPID_SHELL) {
        /* Do not interrupt kernel processes since IO can be stateful */
        earth->timer_reset();
        return;
    }

    if (earth->tty_recv_intr() && curr_pid >= GPID_USER_START) {
        /* User process killed by ctrl+c interrupt */
        INFO("process %d killed by interrupt", curr_pid);
        asm("csrw mepc, %0" ::"r"(0x800500C));
        return;
    }

    if (id == INTR_ID_SOFT)
        kernel_entry = proc_syscall;
    else if (id == INTR_ID_TIMER)
        kernel_entry = proc_yield;
    else
        FATAL("intr_entry: got unknown interrupt %d", id);

    /* Switch to the kernel stack */
    ctx_start(&proc_set[proc_curr_idx].sp, (void*)GRASS_STACK_TOP);
}

void ctx_entry() {
    /* Now on the kernel stack */
    int mepc, tmp;
    asm("csrr %0, mepc" : "=r"(mepc));
    proc_set[proc_curr_idx].mepc = (void*) mepc;

    /* kernel_entry() is either proc_yield() or proc_syscall() */
    kernel_entry();

    /* Switch back to the user application stack */
    mepc = (int)proc_set[proc_curr_idx].mepc;
    asm("csrw mepc, %0" ::"r"(mepc));
    ctx_switch((void**)&tmp, proc_set[proc_curr_idx].sp);
}

static void proc_yield() {
    /* Find the next runnable process */
    int next_idx = -1;
    for (int i = 1; i <= MAX_NPROCESS; i++) {
        int s = proc_set[(proc_curr_idx + i) % MAX_NPROCESS].status;
        if (s == PROC_READY || s == PROC_RUNNING || s == PROC_RUNNABLE) {
            next_idx = (proc_curr_idx + i) % MAX_NPROCESS;
            break;
        }
    }

    if (next_idx == -1) FATAL("proc_yield: no runnable process");
    if (curr_status == PROC_RUNNING) proc_set_runnable(curr_pid);

    /* Switch to the next runnable process and reset timer */
    proc_curr_idx = next_idx;
    earth->mmu_switch(curr_pid);
    earth->timer_reset();

    /* Student's code goes here (switch privilege level). */

    /* Modify mstatus.MPP to enter machine or user mode during mret
     * depending on whether curr_pid is a grass server or a user app
     */
    int mstatus;
    asm("csrr %0, mstatus" : "=r"(mstatus));
    if (curr_pid >= GPID_USER_START) {
        // Set MPP to user mode (00)
        mstatus &= ~(0x3 << 11);
    } else {
        // Set MPP to machine mode (11)
        mstatus |= (0x3 << 11);
    }
    asm("csrw mstatus, %0" :: "r"(mstatus));

    /* Student's code ends here. */

    /* Call the entry point for newly created process */
    if (curr_status == PROC_READY) {
        proc_set_running(curr_pid);
        /* Prepare argc and argv */
        asm("mv a0, %0" ::"r"(APPS_ARG));
        asm("mv a1, %0" ::"r"(APPS_ARG + 4));
        /* Enter application code entry using mret */
        asm("csrw mepc, %0" ::"r"(APPS_ENTRY));
        asm("mret");
    }

    proc_set_running(curr_pid);
}

static void proc_send(struct syscall *sc) {
    sc->msg.sender = curr_pid;
    int receiver = sc->msg.receiver;

    for (int i = 0; i < MAX_NPROCESS; i++)
        if (proc_set[i].pid == receiver) {
            /* Find the receiver */
            if (proc_set[i].status != PROC_WAIT_TO_RECV) {
                curr_status = PROC_WAIT_TO_SEND;
                proc_set[proc_curr_idx].receiver_pid = receiver;
            } else {
                /* Copy message from sender to kernel stack */
                struct sys_msg tmp;
                earth->mmu_switch(curr_pid);
                memcpy(&tmp, &sc->msg, sizeof(tmp));

                /* Copy message from kernel stack to receiver */
                earth->mmu_switch(receiver);
                memcpy(&sc->msg, &tmp, sizeof(tmp));

                /* Set receiver process as runnable */
                proc_set_runnable(receiver);
            }
            proc_yield();
            return;
        }

    sc->retval = -1;
}


static void proc_recv(struct syscall *sc) {
    // Validate the syscall structure to ensure it is not null.
    if (sc == NULL) {
        FATAL("proc_recv: Invalid syscall structure");
        return;
    }

    int sender = -1;
    for (int i = 0; i < MAX_NPROCESS; i++) {
        if (proc_set[i].status == PROC_WAIT_TO_SEND &&
            proc_set[i].receiver_pid == curr_pid) {
            sender = proc_set[i].pid;
            break;
        }
    }

    if (sender == -1) {
        // No sender found, set current process status to waiting to receive
        curr_status = PROC_WAIT_TO_RECV;
    } else {
        /* Copy message from sender to kernel stack */
        struct sys_msg tmp;
        earth->mmu_switch(sender);
        // Consider adding error checking after mmu_switch if applicable

        // Ensure that memory copying is done safely
        if (&sc->msg == NULL) {
            FATAL("proc_recv: Message structure is invalid");
            return;
        }
        memcpy(&tmp, &sc->msg, sizeof(tmp));

        /* Copy message from kernel stack to receiver */
        earth->mmu_switch(curr_pid);
        // Again, consider error checking after mmu_switch if necessary
        memcpy(&sc->msg, &tmp, sizeof(tmp));

        /* Set sender process as runnable */
        proc_set_runnable(sender);
    }

    proc_yield();
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
    // Validate the SYSCALL_ARG to ensure it is not null or invalid.
    if (SYSCALL_ARG == NULL) {
        FATAL("proc_syscall: SYSCALL_ARG is null");
        return;
    }

    struct syscall *sc = (struct syscall*)SYSCALL_ARG;

    // Check if the syscall structure is valid
    if (sc == NULL) {
        FATAL("proc_syscall: Invalid syscall structure");
        return;
    }

    int type = sc->type;

    // Initialize the return value and reset the syscall type
    sc->retval = 0;
    sc->type = SYS_UNUSED;
    *((int*)0x2000000) = 0;

    switch (type) {
    case SYS_RECV:
        proc_recv(sc);
        break;
    case SYS_SEND:
        proc_send(sc);
        break;
    default:
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
