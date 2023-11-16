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

#define STACK_SIZE 16 * 1024 // Example stack size, adjust as necessary
//uses setjmp and longjmp for context switching. When setjmp is called, it saves the current environment (including the instruction pointer and stack pointer) in jmp_buf. 
//Later, longjmp is used to restore this environment, effectively switching the context.
#define MAX_THREADS 10

/** These two functions are defined in grass/context.S **/
void ctx_start(void** old_sp, void* new_sp);
void ctx_switch(void** old_sp, void* new_sp);

//Implemented the C functions ctx_start and ctx_switch based on assembly code in grass/context.S.
// typedef struct {
//     jmp_buf env;
//     void *stack_ptr;
// } context;

// void ctx_start(context *old_context, context *new_context) {
//     // Save the current execution context
//     if (setjmp(old_context->env) == 0) {
//         // Store the current stack pointer
//         old_context->stack_ptr = &old_context;

//         // Switch to the new context
//         longjmp(new_context->env, 1);
//     }
// }

// void ctx_switch(context *old_context, context *new_context) {
//     // Save the current execution context
//     if (setjmp(old_context->env) == 0) {
//         // Store the current stack pointer
//         old_context->stack_ptr = &old_context;

//         // Switch to the new context
//         longjmp(new_context->env, 1);
//     }
// }


/** Forward declarations for thread functions **/
void thread_exit();

/** Multi-threading functions **/
// Thread structure
struct thread {
    void (*function)(void*);
    void *arg;
    bool is_active;
    jmp_buf env; // Environment buffer to store the thread context
    void *stack; // Pointer to the thread's stack
};

struct thread threads[MAX_THREADS]; // Array of threads
int current_thread_idx = -1;        // Index of the currently running thread, -1 indicates no thread is currently active

// Initialize thread management
void thread_init() {
    for (int i = 0; i < MAX_THREADS; ++i) {
        threads[i].is_active = false;
        threads[i].stack = NULL;
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



// Modified thread_create function
void thread_create(void (*f)(void *), void *arg) {
    for (int i = 0; i < MAX_THREADS; ++i) {
        if (!threads[i].is_active) {
            threads[i].function = f;
            threads[i].arg = arg;
            threads[i].is_active = true;

            // Allocate memory for the new thread's stack
            void* stack = malloc(STACK_SIZE);
            if (stack == NULL) {
                // Handle memory allocation failure
                threads[i].is_active = false;
                return;
            }

            // Calculate the top of the stack (assuming the stack grows downwards)
            void* stack_top = (void *)((char *)stack + STACK_SIZE);

            // Initialize the stack pointer for the new thread (platform-specific)
            // Here, you might need to store some initial values or registers on the stack
            // depending on the architecture and calling convention

            // Call ctx_start with the address of the current thread's jmp_buf and the new stack pointer
            ctx_start((void**)&threads[i].env, stack_top);

            return;
        }
    }
}


// Modified thread_yield function
void thread_yield() {
    int current_thread = current_thread_idx;
    int next_thread = (current_thread + 1) % MAX_THREADS;
    current_thread_idx = next_thread;

    if (threads[current_thread].is_active) {
        ctx_switch((void**)&threads[current_thread].env, &threads[next_thread].env);
    } else {
        ctx_switch(NULL, &threads[next_thread].env);
    }
}

// Modified thread_exit function
void thread_exit() {
    if (current_thread_idx >= 0) {
        free(threads[current_thread_idx].stack); // Free the stack memory
        threads[current_thread_idx].is_active = false;
        threads[current_thread_idx].stack = NULL;

        for (int i = (current_thread_idx + 1) % MAX_THREADS; i != current_thread_idx; i = (i + 1) % MAX_THREADS) {
            if (threads[i].is_active) {
                current_thread_idx = i;
                ctx_switch((void**)&threads[current_thread_idx].env, &threads[i].env);
                return;
            }
        }
    }
}

// Create a new thread
// void thread_create(void (*f)(void *), void *arg) {
//     for (int i = 0; i < MAX_THREADS; ++i) {
//         if (!threads[i].is_active) {
//             // Find an inactive thread slot and set up the thread
//             threads[i].function = f;
//             threads[i].arg = arg;
//             threads[i].is_active = true;

//             if (setjmp(threads[i].env) == 0) {
//                 // Save the current context (like a bookmark) and return to continue execution
//                 return;
//             } else {
//                 // This part is executed when longjmp is called, starting the thread function
//                 threads[i].function(threads[i].arg);
//                 thread_exit(); // Exit the thread after the function completes
//             }
//         }
//     }
// }

// // Yield execution to another thread
// void thread_yield() {
//     int current_thread = current_thread_idx;
//     int next_thread = (current_thread + 1) % MAX_THREADS; // Determine the next thread to run
//     current_thread_idx = next_thread; // Update the index of the current thread

//     if (threads[current_thread].is_active) {
//         if (setjmp(threads[current_thread].env) == 0) {
//             // Save the current thread's context and switch to the next thread
//             longjmp(threads[next_thread].env, 1);
//         }
//     } else {
//         // If the current thread is not active, just switch to the next thread
//         longjmp(threads[next_thread].env, 1);
//     }
// }

// // Exit the current thread
// void thread_exit() {
//     if (current_thread_idx >= 0) {
//         // Mark the current thread as inactive
//         threads[current_thread_idx].is_active = false;
//         // Loop to find the next active thread to switch to
//         for (int i = (current_thread_idx + 1) % MAX_THREADS; i != current_thread_idx; i = (i + 1) % MAX_THREADS) {
//             if (threads[i].is_active) {
//                 // Switch to the next active thread
//                 current_thread_idx = i;
//                 longjmp(threads[i].env, 1); // Jump to the next thread's saved context
//             }
//         }
//     }
// }



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
// Define a constant for the maximum number of threads that can wait on a semaphore
#define MAX_WAITING_THREADS 10

// Define the structure of a semaphore
struct sema {
    int count;  // The count represents the current value of the semaphore
    int waiting_threads[MAX_WAITING_THREADS];  // Array to store the IDs of waiting threads
    int num_waiting;  // The number of threads currently waiting on this semaphore
};

// Function to initialize a semaphore
void sema_init(struct sema *sema, unsigned int count) {
    sema->count = count;  // Set the semaphore's count to the specified value
    sema->num_waiting = 0;  // Initially, no threads are waiting
    // Initialize the waiting threads array with -1, indicating no thread is waiting
    for (int i = 0; i < MAX_WAITING_THREADS; i++) {
        sema->waiting_threads[i] = -1; 
    }
}

// Function to increment (signal) the semaphore
void sema_inc(struct sema *sema) {
    sema->count++;  // Increment the semaphore count
    // Check if there are any threads waiting for this semaphore
    if (sema->num_waiting > 0) {
        // Shift all waiting threads up in the array to fill the gap
        // after removing the first thread in the waiting list
        for (int i = 0; i < sema->num_waiting - 1; i++) {
            sema->waiting_threads[i] = sema->waiting_threads[i + 1];
        }
        sema->num_waiting--;  // Decrement the number of waiting threads

        // Yield the current thread to allow other threads (including the one just removed
        // from the waiting list) to get CPU time and check the semaphore
        thread_yield();  
    }
}

// Function to decrement (wait) the semaphore
void sema_dec(struct sema *sema) {
    // Continuously check the semaphore
    while (true) {
        // If the semaphore count is greater than zero, it means the resource is available
        if (sema->count > 0) {
            sema->count--;  // Decrement the semaphore count, indicating resource acquisition
            break;  // Exit the loop as the semaphore has been successfully decremented
        }

        // If the waiting list is not full
        if (sema->num_waiting < MAX_WAITING_THREADS) {
            // Add the current thread to the waiting list
            sema->waiting_threads[sema->num_waiting++] = current_thread_idx;
            // Yield the thread to allow others to run
            thread_yield(); 
        } else {
            // If the waiting list is full, continuously yield
            // until there's space in the waiting list
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

// static void producer(void *arg) {
//     char *messages[] = {"message1", "message2", "message3"};
//     for (int i = 0; i < 3; i++) {
//         sema_dec(&s_empty);
//         strcpy(slots[in], messages[i]); // Copy the message to the slot
//         in = (in + 1) % NSLOTS;
//         sema_inc(&s_full);
//     }
//     thread_exit(); // Exit the thread after producing messages
// }

// static void consumer(void *arg) {
//     char buf[50]; // Buffer to store received message
//     for (int i = 0; i < 3; i++) {
//         sema_dec(&s_full);
//         strcpy(buf, slots[out]); // Copy the message from the slot
//         out = (out + 1) % NSLOTS;
//         printf("%s: got '%s'\n", (char*)arg, buf);
//         sema_inc(&s_empty);
//     }
//     thread_exit(); // Exit the thread after consuming messages
// }

// int main() {
//     thread_init(); // Initialize threading system
//     sema_init(&s_full, 0); // Initialize full semaphore with 0 (buffer is initially empty)
//     sema_init(&s_empty, NSLOTS); // Initialize empty semaphore with the number of slots

//     // Initialize slots
//     for (int i = 0; i < NSLOTS; i++) {
//         slots[i] = malloc(50 * sizeof(char)); // Allocate memory for each slot
//     }

//     // Create producer and consumer threads
//     thread_create(producer, "Producer");
//     thread_create(consumer, "Consumer 1");
//     thread_create(consumer, "Consumer 2");

//     // Start the first thread (will not return)
//     ctx_start(&threads[0].env, threads[0].arg);

//     // Free allocated memory for slots (this part will not be reached in this example)
//     for (int i = 0; i < NSLOTS; i++) {
//         free(slots[i]);
//     }

//     return 0;
// }
