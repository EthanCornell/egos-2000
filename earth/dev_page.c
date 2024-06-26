/*
 * (C) 2022, Cornell University
 * All rights reserved.
 */

/* Author: Yunhao Zhang(Random) & I-Hsuan Huang(Writeback-Aware Cache & LFRU & LRU)
 * Description: a 1MB (256*4KB) paging device
 * for QEMU, 256 physical frames start at address FRAME_CACHE_START
 * for Arty, 28 physical frames are cached at address FRAME_CACHE_START
 * and 256 frames (1MB) start at the beginning of the microSD card
 */

#include "egos.h"
#include "disk.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>//WB cache
#include <pthread.h>//WB cache

#define ARTY_CACHED_NFRAMES 28
#define NBLOCKS_PER_PAGE PAGE_SIZE / BLOCK_SIZE  /* 4KB / 512B == 8 */
int cache_slots[ARTY_CACHED_NFRAMES];
char *pages_start = (void*)FRAME_CACHE_START;

//Write-Back Caching
pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;
bool dirty_pages[ARTY_CACHED_NFRAMES] = {false};// Array to track dirty pages
void paging_init() {
    memset(cache_slots, 0xFF, sizeof(cache_slots));
    memset(dirty_pages, 0, sizeof(dirty_pages));  // Initialize dirty pages array
}

// Writeback-Aware Cache Eviction:
// Original Policy: Random eviction without considering the state of the pages.
// Improved Approach: Implemented a write-back mechanism to track dirty pages (pages modified but not written to disk). This ensures that only necessary data is written back to the disk upon eviction, reducing unnecessary I/O operations.
static int cache_eviction() {
    pthread_mutex_lock(&cache_lock);
    int idx = rand() % ARTY_CACHED_NFRAMES;
    if (dirty_pages[idx]) {
        int frame_id = cache_slots[idx];
        earth->disk_write(frame_id * NBLOCKS_PER_PAGE, NBLOCKS_PER_PAGE, pages_start + PAGE_SIZE * idx);
        dirty_pages[idx] = false;
    }
    pthread_mutex_unlock(&cache_lock);
    return idx;
}


int paging_invalidate_cache(int frame_id) {
    for (int j = 0; j < ARTY_CACHED_NFRAMES; j++)
        if (cache_slots[j] == frame_id) cache_slots[j] = -1;
}

//helper function
int find_or_evict_cache_slot(int frame_id) {
    // First, try to find a free cache slot
    for (int i = 0; i < ARTY_CACHED_NFRAMES; i++) {
        if (cache_slots[i] == -1) { // Check for a free slot
            return i;
        }
    }

    // If no free slot is found, use the cache eviction strategy
    return cache_eviction();
}

// Original Policy: Direct write to cache without dirty tracking.
// Improved Approach: Included logic to mark pages as dirty when written to. This optimizes the decision-making process for which pages need to be written back to the disk.
// Efficiency in Memory and I/O Operations:
// Original Policy: Standard memory operations.
// Improved Approach: Optimized memcpy usage to ensure it is only used when necessary and efficient. This optimization reduces system overhead and improves overall performance.
int paging_write(int frame_id, int page_no) {
    pthread_mutex_lock(&cache_lock);
    char* src = (void*)(page_no << 12);

    if (earth->platform == QEMU) {
        int cache_idx = find_or_evict_cache_slot(frame_id); 
        memcpy(pages_start + PAGE_SIZE * cache_idx, src, PAGE_SIZE);
        dirty_pages[cache_idx] = true; // Mark the page as dirty even for QEMU
        pthread_mutex_unlock(&cache_lock);
        return 0;
    }
    

    // Check if the frame is already in the cache
    // for (int i = 0; i < ARTY_CACHED_NFRAMES; i++) {
    //     if (cache_slots[i] == frame_id) {
    //         memcpy(pages_start + PAGE_SIZE * i, src, PAGE_SIZE);
    //         dirty_pages[i] = true; // Mark the page as dirty
    //         return 0;
    //     }
    // }

    // Check if the frame is already in the cache
    for (int i = 0; i < ARTY_CACHED_NFRAMES; i++) {
        if (cache_slots[i] == frame_id) {
            // Only perform memcpy if the data is different
            if (memcmp(pages_start + PAGE_SIZE * i, src, PAGE_SIZE) != 0) {
                memcpy(pages_start + PAGE_SIZE * i, src, PAGE_SIZE);
                dirty_pages[i] = true; // Mark the page as dirty
            }
            return 0;
        }
    }

    // If the frame is not in the cache, find a free slot or use eviction
    int free_idx = -1;
    for (int i = 0; i < ARTY_CACHED_NFRAMES; i++) {
        if (cache_slots[i] == -1) { // Check for a free slot
            free_idx = i;
            break;
        }
    }

    // If no free slot is found, evict a random page
    if (free_idx == -1) {
        free_idx = cache_eviction();
    }

    cache_slots[free_idx] = frame_id;
    memcpy(pages_start + PAGE_SIZE * free_idx, src, PAGE_SIZE);
    dirty_pages[free_idx] = true; // Mark the new page as dirty
    pthread_mutex_unlock(&cache_lock);
    return 0;
}

// Original Policy: Immediate disk read for non-cached pages.
// Improved Approach: Implemented lazy loading (Cache-aside), where pages are loaded into the cache only if they are going to be used, minimizing unnecessary disk reads.
char* paging_read(int frame_id, int alloc_only) {
    pthread_mutex_lock(&cache_lock);
    if (earth->platform == QEMU) {
        int cache_idx = find_or_evict_cache_slot(frame_id); // Use this function to manage cache slots

        // Check if the data needs to be loaded or updated
        if (!alloc_only && memcmp(pages_start + PAGE_SIZE * cache_idx, (void*)(frame_id << 12), PAGE_SIZE) != 0) {
            earth->disk_read(frame_id * NBLOCKS_PER_PAGE, NBLOCKS_PER_PAGE, pages_start + PAGE_SIZE * cache_idx);
            dirty_pages[cache_idx] = false; // Mark the page as not dirty after loading
        }

        pthread_mutex_unlock(&cache_lock);
        return pages_start + PAGE_SIZE * cache_idx;
    }

    int cache_idx = -1;

    // Search for the frame in the cache
    for (int i = 0; i < ARTY_CACHED_NFRAMES; i++) {
        if (cache_slots[i] == frame_id) {
            return pages_start + PAGE_SIZE * i; // Frame found in cache
        }
        if (cache_slots[i] == -1 && cache_idx == -1) {
            cache_idx = i; // Remember the first free slot
        }
    }

    // If the frame is not in the cache, find or create a slot for it
    if (cache_idx == -1) {
        cache_idx = cache_eviction(); // Evict a page if no free slot available
    }

    cache_slots[cache_idx] = frame_id;

    // Lazy loading: Load the page into the cache only if it's going to be used
    if (!alloc_only) {
        earth->disk_read(frame_id * NBLOCKS_PER_PAGE, NBLOCKS_PER_PAGE, pages_start + PAGE_SIZE * cache_idx);
        dirty_pages[cache_idx] = false; // Mark the newly loaded page as not dirty
    }
    pthread_mutex_unlock(&cache_lock);
    return pages_start + PAGE_SIZE * cache_idx;
}

// //Random Policy
// static int cache_eviction() {
//     /* Randomly select a cache slot to evict */
//     int idx = rand() % ARTY_CACHED_NFRAMES;
//     int frame_id = cache_slots[idx];

//     earth->disk_write(frame_id * NBLOCKS_PER_PAGE, NBLOCKS_PER_PAGE, pages_start + PAGE_SIZE * idx);
//     return idx;
// }


// // For Random Eviction Strategy
// void paging_init() {                 // Function to initialize paging.
//     memset(cache_slots, 0xFF, sizeof(cache_slots)); // Initializes the cache_slots array, setting all values to -1 (0xFF).
// }

// int paging_invalidate_cache(int frame_id) { // Function to invalidate a specific frame in the cache.
//     for (int j = 0; j < ARTY_CACHED_NFRAMES; j++)  // Iterates through all cache slots.
//         if (cache_slots[j] == frame_id) cache_slots[j] = -1; // If the frame ID matches, it sets the cache slot to -1 (invalidates it).
// }


// int paging_write(int frame_id, int page_no) { // Function to handle writing a page to a cache frame.
//     char* src = (void*)(page_no << 12); // Calculates the source address by shifting the page number left by 12 bits (effectively multiplying by 4096, the size of a page).
//     if (earth->platform == QEMU) {      // Checks if the platform is QEMU.
//         memcpy(pages_start + frame_id * PAGE_SIZE, src, PAGE_SIZE); // Directly copies the page to the cache for QEMU platform.
//         return 0;
//     }

//     for (int i = 0; i < ARTY_CACHED_NFRAMES; i++)  // Iterates through the cache slots.
//         if (cache_slots[i] == frame_id) {          // Checks if the current slot holds the frame.
//             memcpy(pages_start + PAGE_SIZE * i, src, PAGE_SIZE) != NULL; // Copies the page to the cache slot.
//             return 0;
//         }

//     int free_idx = cache_eviction();  // If no matching frame is found, evict a frame.
//     cache_slots[free_idx] = frame_id; // Update the cache slot with the new frame ID.
//     memcpy(pages_start + PAGE_SIZE * free_idx, src, PAGE_SIZE); // Copy the page to the newly freed cache slot.
// }

// char* paging_read(int frame_id, int alloc_only) {  // Function to handle reading a page from a cache frame.
//     if (earth->platform == QEMU) return pages_start + frame_id * PAGE_SIZE; // Directly returns the page address for QEMU platform.

//     int free_idx = -1;                            // Initializes the index for a free slot.
//     for (int i = 0; i < ARTY_CACHED_NFRAMES; i++) { // Iterates through the cache slots.
//         if (cache_slots[i] == -1 && free_idx == -1) free_idx = i; // Finds the first free slot.
//         if (cache_slots[i] == frame_id) return pages_start + PAGE_SIZE * i; // Returns the address of the page if found in cache.
//     }

//     if (free_idx == -1) free_idx = cache_eviction(); // If no free slot, evict a slot.
//     cache_slots[free_idx] = frame_id;                // Updates the cache slot with the new frame ID.

//     if (!alloc_only) // If not just allocating, also read the page from the disk.
//         earth->disk_read(frame_id * NBLOCKS_PER_PAGE, NBLOCKS_PER_PAGE, pages_start + PAGE_SIZE * free_idx);

//     return pages_start + PAGE_SIZE * free_idx; // Returns the address of the page in cache.
// }




//LFRU policy
// #define MAX_CACHE_SIZE 256  // Total cache size
// #define PRIVILEGED_PARTITION_SIZE 128  // Size of privileged partition
// typedef struct cache_page {
//     int frame_id;
//     int access_frequency;
//     struct cache_page* next;
//     struct cache_page* prev;
// } cache_page;

// cache_page cache[MAX_CACHE_SIZE];  // Array to store cache pages
// int privileged_count = 0;  // Counter for the number of pages in the privileged partition

// void initialize_cache() {
//     for (int i = 0; i < MAX_CACHE_SIZE; i++) {
//         cache[i].frame_id = -1;  // Indicates an empty slot
//         cache[i].access_frequency = 0;
//         cache[i].next = (i < MAX_CACHE_SIZE - 1) ? &cache[i + 1] : NULL;
//         cache[i].prev = (i > 0) ? &cache[i - 1] : NULL;
//     }
//     for (int i = 0; i < ARTY_CACHED_NFRAMES; i++) {
//         cache_slots[i] = -1;  // Indicates that the slot is initially empty
//     }
// }


// cache_page* find_page(int frame_id) {
//     for (int i = 0; i < MAX_CACHE_SIZE; i++) {
//         if (cache[i].frame_id == frame_id) {
//             return &cache[i];
//         }
//     }
//     return NULL;
// }

// void move_to_front(cache_page* page) {
//     // Remove the page from its current position
//     if (page->prev) page->prev->next = page->next;
//     if (page->next) page->next->prev = page->prev;

//     // Insert the page at the front of the list
//     page->prev = NULL;
//     page->next = cache;
//     if (cache) cache->prev = page;
//     cache = page;
// }

// void access_page(int frame_id) {
//     cache_page* page = find_page(frame_id);

//     if (page) {
//         page->access_frequency++;
//         if (page < cache + PRIVILEGED_PARTITION_SIZE) {
//             // Page is in the privileged partition, apply LRU
//             move_to_front(page);
//         } else if (privileged_count < PRIVILEGED_PARTITION_SIZE) {
//             // Move page to privileged partition if there's space
//             move_to_front(page);
//             privileged_count++;
//         }
//         // Else, the page is in unprivileged partition, keep it there (handled by ALFU)
//     } else {
//         // Page is not in cache, load it into the unprivileged partition
//         // Here, we'll just use the last slot in the cache array for simplicity
//         cache[MAX_CACHE_SIZE - 1].frame_id = frame_id;
//         cache[MAX_CACHE_SIZE - 1].access_frequency = 1;
//         move_to_front(&cache[MAX_CACHE_SIZE - 1]);
//     }
// }

// void evict_from_privileged() {
//     cache_page* lru = &cache[PRIVILEGED_PARTITION_SIZE - 1];
//     int cache_index = lru->frame_id % ARTY_CACHED_NFRAMES; // Example calculation
//     cache_slots[cache_index] = -1; // Remove from cache_slots
//     lru->frame_id = -1;  // Mark as empty
//     lru->access_frequency = 0;
//     privileged_count--;
// }

// void evict_from_unprivileged() {
//     // Evict the least frequently used page from the unprivileged partition
//     int min_freq = __INT_MAX__;
//     cache_page* lfu = NULL;
//     for (int i = PRIVILEGED_PARTITION_SIZE; i < MAX_CACHE_SIZE; i++) {
//         if (cache[i].access_frequency < min_freq) {
//             min_freq = cache[i].access_frequency;
//             lfu = &cache[i];
//         }
//     }
//     if (lfu) {
//         lfu->frame_id = -1;  // Mark as empty
//         lfu->access_frequency = 0;
//     }
// }



// static int cache_eviction() {
//     // Evict from the privileged partition if it's full
//     if (privileged_count == PRIVILEGED_PARTITION_SIZE) {
//         evict_from_privileged();
//         return 1; // Indicates eviction from the privileged partition
//     } else {
//         // Otherwise, evict from the unprivileged partition
//         evict_from_unprivileged();
//         return 0; // Indicates eviction from the unprivileged partition
//     }
// }


// void paging_init() {
//     initialize_cache();
// }


// int paging_invalidate_cache(int frame_id) {
//     cache_page* page = find_page(frame_id);
//     if (page) {
//         page->frame_id = -1; // Mark the page as invalid
//         page->access_frequency = 0;
//         if (page < cache + PRIVILEGED_PARTITION_SIZE) {
//             privileged_count--; // Adjust count if the page is in the privileged partition
//         }
//         return 0; // Success
//     }
//     return -1; // Page not found
// }


// int paging_write(int frame_id, int page_no) {
//     char* src = (void*)(page_no << 12);

//     // Handling for QEMU platform
//     if (earth->platform == QEMU) {
//         memcpy(pages_start + frame_id * PAGE_SIZE, src, PAGE_SIZE);
//         return 0;
//     }

//     // Handling for non-QEMU platforms
//     access_page(frame_id); // Update cache access pattern

//     int cache_index = frame_id % ARTY_CACHED_NFRAMES; 
//     if (cache_slots[cache_index] == frame_id) {
//         memcpy(pages_start + cache_index * PAGE_SIZE, src, PAGE_SIZE);
//         return 0;
//     }

//     int free_idx = cache_eviction();
//     cache_slots[free_idx] = frame_id;
//     memcpy(pages_start + PAGE_SIZE * free_idx, src, PAGE_SIZE);

//     return 0; // Success
// }

// char* paging_read(int frame_id, int alloc_only) {
//     // Handling for QEMU platform
//     if (earth->platform == QEMU) {
//         return pages_start + frame_id * PAGE_SIZE;
//     }

//     // Handling for non-QEMU platforms
//     int free_idx = -1;
//     for (int i = 0; i < ARTY_CACHED_NFRAMES; i++) {
//         if (cache_slots[i] == -1 && free_idx == -1) free_idx = i;
//         if (cache_slots[i] == frame_id) return pages_start + PAGE_SIZE * i;
//     }

//     if (free_idx == -1) free_idx = cache_eviction();
//     cache_slots[free_idx] = frame_id;

//     if (!alloc_only) {
//         access_page(frame_id); // Load the page into the cache if not allocation only
//         earth->disk_read(frame_id * NBLOCKS_PER_PAGE, NBLOCKS_PER_PAGE, pages_start + PAGE_SIZE * free_idx);
//     }

//     return pages_start + PAGE_SIZE * free_idx;
// }







//LRU Policy

// Structure to hold cache frame information
// struct cache_frame_info {
//     int frame_id;  // ID of the frame in the cache
//     int last_used; // Timestamp for LRU
//     bool dirty;    // Flag to indicate if the frame is dirty
// };

// struct cache_frame_info cache_info[ARTY_CACHED_NFRAMES]; // Assuming this array is already populated with frame information
// int cache_slots[ARTY_CACHED_NFRAMES]; // Array to keep track of cache slots, with size based on the defined number of cache frames.
// char *pages_start = (void*)FRAME_CACHE_START; // Pointer to the start of the frame cache memory area.
// int last_used[ARTY_CACHED_NFRAMES]; // Array to keep track of the last used time for each slot. last_used array stores the time when each cache slot was last used.
// int lru_counter = 0; // Counter to simulate a timestamp for LRU

// Initializes cache frame info
// void cache_info_init() {
//     for (int i = 0; i < ARTY_CACHED_NFRAMES; i++) {
//         cache_info[i].frame_id = -1; // -1 indicates an empty slot
//         cache_info[i].last_used = 0;
//         cache_info[i].dirty = false;
//     }
// }

// Function to find the cache slot for a given frame ID
// int find_cache_slot(int frame_id) {
//     for (int i = 0; i < ARTY_CACHED_NFRAMES; i++) {
//         if (cache_info[i].frame_id == frame_id) {
//             return i; // Cache slot found, return the index of the slot containing the frame ID
//         }
//     }
//     return -1; // Cache slot not found, return -1 if no matching frame ID is found
// }

// void mark_dirty(int slot_idx) {
//     if (slot_idx >= 0 && slot_idx < ARTY_CACHED_NFRAMES) {
//         cache_info[slot_idx].dirty = true;
//     }
// }


// void update_lru(int slot_idx) {
//     if (slot_idx >= 0 && slot_idx < ARTY_CACHED_NFRAMES) {
//         cache_info[slot_idx].last_used = ++lru_counter;
//     }
// }



// Least Recently Used (LRU) algorithm. In LRU, the cache slot that hasn't been used for the longest time is selected for eviction. 
//The cache_eviction function finds the least recently used slot by iterating through last_used.
// static int cache_eviction() {
//     // Find the least recently used cache slot.
//     int lru_idx = 0;
//     for (int i = 1; i < ARTY_CACHED_NFRAMES; i++) {
//         if (last_used[i] < last_used[lru_idx]) {
//             lru_idx = i;
//         }
//     }

//     int frame_id = cache_slots[lru_idx];
//     // Writes the evicted frame back to disk.
//     earth->disk_write(frame_id * NBLOCKS_PER_PAGE, NBLOCKS_PER_PAGE, pages_start + PAGE_SIZE * lru_idx);
//     return lru_idx;
// }

//The paging_init function initializes the last_used array.
// void paging_init() {
//     memset(cache_slots, 0xFF, sizeof(cache_slots)); // Initializes the cache_slots array.
//     memset(last_used, 0, sizeof(last_used)); // Initializes the last_used array.
// }






//Main change:
//The loop exits as soon as the targeted frame is invalidated, reducing unnecessary iterations.
//The last_used array is updated to maintain the LRU order. This step assumes that you have implemented an LRU cache as suggested earlier.
//The function returns a status indicating whether the invalidation was successful (1) or the frame was not found (0).
// int paging_invalidate_cache(int frame_id) {
//     // Function to invalidate a specific frame in the cache.
//     for (int j = 0; j < ARTY_CACHED_NFRAMES; j++) {
//         if (cache_slots[j] == frame_id) {
//             cache_slots[j] = -1; // Invalidate the cache slot.

//             // Update the LRU tracking (assuming you have a last_used array as per LRU implementation).
//             last_used[j] = -1; // Indicate that this slot is no longer used.

//             return 1; // Return 1 to indicate successful invalidation.
//         }
//     }
//     return 0; // Return 0 to indicate that the frame was not found.
// }



//Main change:
//Optimize Cache Slot Lookup: Both functions iterate over all cache slots to find the relevant slot. This can be optimized using a hash map or a direct-mapped cache approach, reducing the time complexity from O(n) to O(1) in the best case.
//LRU Update on Access: Whenever a page is read or written, its last used time should be updated to maintain the LRU order.
//Write-Back Caching: Implement a write-back cache strategy where writes are made to the cache first and only written back to disk when necessary (e.g., during eviction). This requires an additional 'dirty bit' to track if the cache data has been modified.
//Read-Ahead Strategy: Implement a read-ahead strategy in paging_read to prefetch the next few pages, assuming sequential access. This can improve performance in scenarios where data is read sequentially.
//Return Status: Similar to paging_invalidate_cache, these functions can return a status indicating success, failure, or not-found scenarios.

// Assuming additional structures and variables for LRU and write-back caching are in place
// int paging_write(int frame_id, int page_no) {
//     char* src = (void*)(page_no << 12);
//     if (earth->platform == QEMU) {
//         memcpy(pages_start + frame_id * PAGE_SIZE, src, PAGE_SIZE);
//         return 0; // Success
//     }

//     int slot_idx = find_cache_slot(frame_id); // find_cache_slot() is a hypothetical function to find the slot index quickly.
//     if (slot_idx != -1) {
//         memcpy(pages_start + PAGE_SIZE * slot_idx, src, PAGE_SIZE);
//         mark_dirty(slot_idx); // Mark this slot as dirty since it's been written to.
//         update_lru(slot_idx); // Update LRU for this slot.
//         return 0; // Success
//     }

//     int free_idx = cache_eviction(); // Evict using LRU.
//     cache_slots[free_idx] = frame_id;
//     memcpy(pages_start + PAGE_SIZE * free_idx, src, PAGE_SIZE);
//     mark_dirty(free_idx); // Mark as dirty.
//     update_lru(free_idx); // Update LRU.
//     return 0; // Success
// }

// char* paging_read(int frame_id, int alloc_only) {
//     if (earth->platform == QEMU) return pages_start + frame_id * PAGE_SIZE;

//     int slot_idx = find_cache_slot(frame_id);
//     if (slot_idx != -1) {
//         update_lru(slot_idx); // Update LRU for this slot.
//         return pages_start + PAGE_SIZE * slot_idx;
//     }

//     int free_idx = cache_eviction(); // Evict using LRU.
//     cache_slots[free_idx] = frame_id;
//     if (!alloc_only) {
//         earth->disk_read(frame_id * NBLOCKS_PER_PAGE, NBLOCKS_PER_PAGE, pages_start + PAGE_SIZE * free_idx);
//     }
//     update_lru(free_idx); // Update LRU.
//     return pages_start + PAGE_SIZE * free_idx;
// }


