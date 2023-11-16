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
#define MAX_THREADS 10

// Thread structure
struct thread {
    void (*function)(void*);
    void *arg;
    bool is_active;
    jmp_buf env; // Environment buffer to store the thread context
};

struct thread threads[MAX_THREADS];
int current_thread_idx = -1;

// Initialize thread management
void thread_init() {
    for (int i = 0; i < MAX_THREADS; ++i) {
        threads[i].is_active = false;
    }
}

// Entry point for threads
void ctx_entry() {
    if (current_thread_idx >= 0 && threads[current_thread_idx].is_active) {
        threads[current_thread_idx].function(threads[current_thread_idx].arg);
        thread_exit(); // Exit the thread after function execution
    }
}

// Create a new thread
void thread_create(void (*f)(void *), void *arg) {
    for (int i = 0; i < MAX_THREADS; ++i) {
        if (!threads[i].is_active) {
            threads[i].function = f;
            threads[i].arg = arg;
            threads[i].is_active = true;

            if (setjmp(threads[i].env) == 0) {
                // Save the thread context and return
                return;
            } else {
                // When longjmp is called, start the thread function
                threads[i].function(threads[i].arg);
                thread_exit();
            }
        }
    }
}

// Yield execution to another thread
void thread_yield() {
    int current_thread = current_thread_idx;
    int next_thread = (current_thread + 1) % MAX_THREADS;
    current_thread_idx = next_thread;

    if (threads[current_thread].is_active) {
        if (setjmp(threads[current_thread].env) == 0) {
            longjmp(threads[next_thread].env, 1);
        }
    } else {
        longjmp(threads[next_thread].env, 1);
    }
}

// Exit the current thread
void thread_exit() {
    if (current_thread_idx >= 0) {
        threads[current_thread_idx].is_active = false;
        // Find the next active thread and switch to it
        for (int i = (current_thread_idx + 1) % MAX_THREADS; i != current_thread_idx; i = (i + 1) % MAX_THREADS) {
            if (threads[i].is_active) {
                current_thread_idx = i;
                longjmp(threads[i].env, 1);
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

struct sema {
    /* Student's code goes here. */
};

void sema_init(struct sema *sema, unsigned int count){
    /* Student's code goes here. */
}

void sema_inc(struct sema *sema){
    /* Student's code goes here. */
}

void sema_dec(struct sema *sema){
    /* Student's code goes here. */
}

int sema_release(struct sema *sema){
    /* Student's code goes here. */
}

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

