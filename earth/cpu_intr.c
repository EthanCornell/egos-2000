/*
 * (C) 2022, Cornell University
 * All rights reserved.
 */

/* Author: Yunhao Zhang
 * Description: RISC-V interrupt and exception handling
 */

#include "egos.h"  

/* Declaration of two static function pointers for handling interrupts and exceptions. 
   These are initialized to NULL, meaning they point to no function initially. */
static void (*intr_handler)(int);
static void (*excp_handler)(int);

/* Function to register an interrupt handler. 
   It assigns the provided function pointer to the static 'intr_handler'. */
int intr_register(void (*_handler)(int)) { intr_handler = _handler; }

/* Function to register an exception handler. 
   It assigns the provided function pointer to the static 'excp_handler'. */
int excp_register(void (*_handler)(int)) { excp_handler = _handler; }

void trap_entry_vm();  // Declaration of a wrapper function 'trap_entry_vm', defined elsewhere (in earth.S).

/* Declaration of the 'trap_entry' function with specific attributes.
   It is marked as an interrupt handler for the 'machine' level and aligned to 128 bytes. */
void trap_entry()  __attribute__((interrupt ("machine"), aligned(128)));

void trap_entry() {
    int mcause;  // Variable to store the cause of the trap.

    /* Inline assembly to read the 'mcause' register value into 'mcause' variable. */
    asm("csrr %0, mcause" : "=r"(mcause));

    /* Extracts the interrupt ID from 'mcause'. The lower 10 bits are the ID. */
    int id = mcause & 0x3FF;

    /* Checks the highest bit of 'mcause' to determine if it's an interrupt or an exception. */
    if (mcause & (1 << 31))
        /* If it's an interrupt and a handler is registered, call it; otherwise, trigger a fatal error. */
        (intr_handler)? intr_handler(id) : FATAL("trap_entry: interrupt handler not registered");
    else
        /* If it's an exception and a handler is registered, call it; otherwise, trigger a fatal error. */
        (excp_handler)? excp_handler(id) : FATAL("trap_entry: exception handler not registered");
}

void intr_init() {
    /* Registering the interrupt and exception handler registration functions with the 'earth' structure. */
    earth->intr_register = intr_register;
    earth->excp_register = excp_register;

    /* Depending on the translation mode, set the 'mtvec' register to the appropriate trap handler function. */
    if (earth->translation == PAGE_TABLE) {
        /* If using page table translation, set 'mtvec' to 'trap_entry_vm'. */
        asm("csrw mtvec, %0" ::"r"(trap_entry_vm));
        INFO("Use direct mode and put the address of trap_entry_vm() to mtvec");
    } else {
        /* Otherwise, set 'mtvec' to 'trap_entry'. */
        asm("csrw mtvec, %0" ::"r"(trap_entry));
        INFO("Use direct mode and put the address of trap_entry() to mtvec");
    }

    /* Enable machine-mode timer and software interrupts. */
    int mstatus, mie;  // Variables to store 'mie' (Machine Interrupt Enable) and 'mstatus' (Machine Status) register values.

    /* Read the current 'mie' value, set bits for enabling timer and software interrupts, and write back. */
    asm("csrr %0, mie" : "=r"(mie));
    asm("csrw mie, %0" ::"r"(mie | 0x88));
    /* Read the current 'mstatus' value, set bits for enabling timer and software interrupts, and write back. */
    asm("csrr %0, mstatus" : "=r"(mstatus));
    asm("csrw mstatus, %0" ::"r"(mstatus | 0x88));
}

