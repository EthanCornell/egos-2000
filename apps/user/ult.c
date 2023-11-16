/*
 * (C) 2023, Cornell University
 * All rights reserved.
 */

/* Author: Robbert van Renesse
 * Description: course project, user-level threading
 * Students implement a threading package and semaphore;
 * And then spawn multiple threads as either producer or consumer.
 */

#include "app.h"
#include <setjmp.h>
#include <stdlib.h>
#include <stdbool.h>

/** These two functions are defined in grass/context.S **/
void ctx_start(void** old_sp, void* new_sp);
void ctx_switch(void** old_sp, void* new_sp);

/** Forward declarations for thread functions **/
void thread_exit();

/** Multi-threading functions **/
//uses setjmp and longjmp for context switching. When setjmp is called, it saves the current environment (including the instruction pointer and stack pointer) in jmp_buf. 
//Later, longjmp is used to restore this environment, effectively switching the context.
#define MAX_THREADS 10

// Thread structure
struct thread {
    void (*function)(void*); // Pointer to the thread function
    void *arg;               // Argument to be passed to the thread function
    bool is_active;          // Flag to indicate whether the thread is active
    jmp_buf env;             // Environment buffer to store the thread context for context switching
};

struct thread threads[MAX_THREADS]; // Array of threads
int current_thread_idx = -1;        // Index of the currently running thread, -1 indicates no thread is currently active

// Initialize thread management
void thread_init() {
    for (int i = 0; i < MAX_THREADS; ++i) {
        threads[i].is_active = false; // Initialize all threads as inactive
    }
}

// Entry point for threads
void ctx_entry() {
    if (current_thread_idx >= 0 && threads[current_thread_idx].is_active) {
        // If there is an active thread, execute its function
        threads[current_thread_idx].function(threads[current_thread_idx].arg);
        thread_exit(); // Exit the thread after executing its function
    }
}

// Create a new thread
void thread_create(void (*f)(void *), void *arg) {
    for (int i = 0; i < MAX_THREADS; ++i) {
        if (!threads[i].is_active) {
            // Find an inactive thread slot and set up the thread
            threads[i].function = f;
            threads[i].arg = arg;
            threads[i].is_active = true;

            if (setjmp(threads[i].env) == 0) {
                // Save the current context (like a bookmark) and return to continue execution
                return;
            } else {
                // This part is executed when longjmp is called, starting the thread function
                threads[i].function(threads[i].arg);
                thread_exit(); // Exit the thread after the function completes
            }
        }
    }
}

// Yield execution to another thread
void thread_yield() {
    int current_thread = current_thread_idx;
    int next_thread = (current_thread + 1) % MAX_THREADS; // Determine the next thread to run
    current_thread_idx = next_thread; // Update the index of the current thread

    if (threads[current_thread].is_active) {
        if (setjmp(threads[current_thread].env) == 0) {
            // Save the current thread's context and switch to the next thread
            longjmp(threads[next_thread].env, 1);
        }
    } else {
        // If the current thread is not active, just switch to the next thread
        longjmp(threads[next_thread].env, 1);
    }
}

// Exit the current thread
void thread_exit() {
    if (current_thread_idx >= 0) {
        // Mark the current thread as inactive
        threads[current_thread_idx].is_active = false;
        // Loop to find the next active thread to switch to
        for (int i = (current_thread_idx + 1) % MAX_THREADS; i != current_thread_idx; i = (i + 1) % MAX_THREADS) {
            if (threads[i].is_active) {
                // Switch to the next active thread
                current_thread_idx = i;
                longjmp(threads[i].env, 1); // Jump to the next thread's saved context
            }
        }
    }
}



// /** Multi-threading functions **/
// struct thread {
//     /* Student's code goes here. */
// };

// void thread_init(){
//     /* Student's code goes here */
// }

// void ctx_entry(void){
//     /* Student's code goes here. */
// }

// void thread_create(void (*f)(void *), void *arg, unsigned int stack_size){
//     /* Student's code goes here. */
// }

// void thread_yield(){
//     /* Student's code goes here. */
// }

// void thread_exit(){
//     /* Student's code goes here. */
// }

/** Semaphore functions **/

#define MAX_WAITING_THREADS 10

struct sema {
    int count;
    int waiting_threads[MAX_WAITING_THREADS];
    int num_waiting;
};

void sema_init(struct sema *sema, unsigned int count) {
    sema->count = count;
    sema->num_waiting = 0;
    for (int i = 0; i < MAX_WAITING_THREADS; i++) {
        sema->waiting_threads[i] = -1; // Initialize to an invalid thread index
    }
}

void sema_inc(struct sema *sema) {
    sema->count++;
    if (sema->num_waiting > 0) {
        // Since we don't have thread_mark_ready, we'll simply
        // remove the thread from the waiting list. This thread
        // will check the semaphore status again when it gets CPU time.
        for (int i = 0; i < sema->num_waiting - 1; i++) {
            sema->waiting_threads[i] = sema->waiting_threads[i + 1];
        }
        sema->num_waiting--;

        // Optionally, you might yield here to give other threads a chance
        // to run immediately, but this depends on your scheduler's behavior
        thread_yield();

    }
}

void sema_dec(struct sema *sema) {
    while (true) {
        if (sema->count > 0) {
            sema->count--;
            break;
        }

        if (sema->num_waiting < MAX_WAITING_THREADS) {
            sema->waiting_threads[sema->num_waiting++] = current_thread_idx;
            thread_yield(); // Yield the thread to allow others to run
        } else {
            // Waiting list is full, so continuously yield until there's space
            while (sema->num_waiting >= MAX_WAITING_THREADS) {
                thread_yield();
            }
        }
    }
}


// struct sema {
//     /* Student's code goes here. */
// };

// void sema_init(struct sema *sema, unsigned int count){
//     /* Student's code goes here. */
// }

// void sema_inc(struct sema *sema){
//     /* Student's code goes here. */
// }

// void sema_dec(struct sema *sema){
//     /* Student's code goes here. */
// }

// int sema_release(struct sema *sema){
//     /* Student's code goes here. */
// }

/** Producer and consumer functions **/

#define NSLOTS	3

static char *slots[NSLOTS];
static unsigned int in, out;
static struct sema s_empty, s_full;

static void producer(void *arg){
    for (;;) {
        // first make sure there's an empty slot.
        // then add an entry to the queue
        // lastly, signal consumers

        sema_dec(&s_empty);
        slots[in++] = arg;
        if (in == NSLOTS) in = 0;
        sema_inc(&s_full);
    }
}

static void consumer(void *arg){
    for (int i = 0; i < 5; i++) {
        // first make sure there's something in the buffer
        // then grab an entry to the queue
        // lastly, signal producers

        sema_dec(&s_full);
        void *x = slots[out++];
        if (out == NSLOTS) out = 0;
        printf("%s: got '%s'\n", arg, x);
        sema_inc(&s_empty);
    }
}

int main() {
    INFO("User-level threading is not implemented.");

    /*
    thread_init();
    sema_init(&s_full, 0);
    sema_init(&s_empty, NSLOTS);

    thread_create(consumer, "consumer 1", 16 * 1024);
    thread_create(consumer, "consumer 2", 16 * 1024);
    thread_create(consumer, "consumer 3", 16 * 1024);
    thread_create(consumer, "consumer 4", 16 * 1024);
    thread_create(producer, "producer 2", 16 * 1024);
    thread_create(producer, "producer 3", 16 * 1024);
    producer("producer 1");
    thread_exit();
    */

    return 0;
}

