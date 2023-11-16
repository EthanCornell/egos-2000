/*
 * (C) 2022, Cornell University
 * All rights reserved.
 */

/* Author: Yunhao Zhang
 * Description: the system call interface to user applications
 */

#include "egos.h"
#include "syscall.h"
#include <string.h>

static struct syscall *sc = (struct syscall*)SYSCALL_ARG;  
/* Declares a static pointer 'sc' to a 'struct syscall', and initializes it to the address specified by 'SYSCALL_ARG'.
   This structure is likely used for passing arguments to system calls. */

static void sys_invoke() {
    /* Function to trigger a system call. It uses a memory-mapped I/O technique instead of the standard 'ecall' instruction. */

    *((int*)0x2000000) = 1; // Writes the value 1 to the memory address 0x2000000, likely signaling the system call handler.
    while (sc->type != SYS_UNUSED); // Busy-waits until the syscall's type is set to SYS_UNUSED, indicating completion.
}

int sys_send(int receiver, char* msg, int size) {
    /* Function to send a message via a system call. */

    if (size > SYSCALL_MSG_LEN) return -1; // If the message size exceeds the maximum, returns -1 indicating an error.

    sc->type = SYS_SEND; // Sets the system call type to SYS_SEND.
    sc->msg.receiver = receiver; // Sets the message receiver.
    memcpy(sc->msg.content, msg, size); // Copies the message content to the syscall structure.
    sys_invoke(); // Invokes the system call.
    return sc->retval;  // Returns the return value from the syscall.  
}

int sys_recv(int* sender, char* buf, int size) {
    /* Function to receive a message via a system call. */

    if (size > SYSCALL_MSG_LEN) return -1; // If the buffer size exceeds the maximum, returns -1 indicating an error.

    sc->type = SYS_RECV; // Sets the system call type to SYS_RECV.
    sys_invoke(); // Invokes the system call.
    memcpy(buf, sc->msg.content, size); // Copies the received message into the buffer.
    if (sender) *sender = sc->msg.sender; // If a sender pointer is provided, sets it to the sender of the message.
    return sc->retval; // Returns the return value from the syscall.
}

void sys_exit(int status) {
    /* Function to handle process exit via a system call. */

    struct proc_request req; // Creates a process request structure.
    req.type = PROC_EXIT; // Sets the request type to PROC_EXIT.
    sys_send(GPID_PROCESS, (void*)&req, sizeof(req)); // Sends the exit request to the process manager.
}
