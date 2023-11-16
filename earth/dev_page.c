/*
 * (C) 2022, Cornell University
 * All rights reserved.
 */

/* Author: Yunhao Zhang
 * Description: a 1MB (256*4KB) paging device
 * for QEMU, 256 physical frames start at address FRAME_CACHE_START
 * for Arty, 28 physical frames are cached at address FRAME_CACHE_START
 * and 256 frames (1MB) start at the beginning of the microSD card
 */

#include "egos.h"
#include "disk.h"
#include <stdlib.h>
#include <string.h>

#include <stdbool.h>
#include <limits.h>

#define ARTY_CACHED_NFRAMES 28      // Defines a constant for the number of cache frames, setting it to 28.
#define NBLOCKS_PER_PAGE PAGE_SIZE / BLOCK_SIZE  // Defines the number of blocks per page, calculated as page size divided by block size.

// Structure to hold cache frame information
struct cache_frame_info {
    int frame_id;  // ID of the frame in the cache
    int last_used; // Timestamp for LRU
    bool dirty;    // Flag to indicate if the frame is dirty
};

struct cache_frame_info cache_info[ARTY_CACHED_NFRAMES]; // Assuming this array is already populated with frame information
int cache_slots[ARTY_CACHED_NFRAMES]; // Array to keep track of cache slots, with size based on the defined number of cache frames.
char *pages_start = (void*)FRAME_CACHE_START; // Pointer to the start of the frame cache memory area.
int last_used[ARTY_CACHED_NFRAMES]; // Array to keep track of the last used time for each slot. last_used array stores the time when each cache slot was last used.
int lru_counter = 0; // Counter to simulate a timestamp for LRU

// Initializes cache frame info
void cache_info_init() {
    for (int i = 0; i < ARTY_CACHED_NFRAMES; i++) {
        cache_info[i].frame_id = -1; // -1 indicates an empty slot
        cache_info[i].last_used = 0;
        cache_info[i].dirty = false;
    }
}

int find_cache_slot(int frame_id) {
    for (int i = 0; i < ARTY_CACHED_NFRAMES; i++) {
        if (cache_info[i].frame_id == frame_id) {
            return i; // Cache slot found
        }
    }
    return -1; // Cache slot not found
}

void mark_dirty(int slot_idx) {
    if (slot_idx >= 0 && slot_idx < ARTY_CACHED_NFRAMES) {
        cache_info[slot_idx].dirty = true;
    }
}


void update_lru(int slot_idx) {
    if (slot_idx >= 0 && slot_idx < ARTY_CACHED_NFRAMES) {
        cache_info[slot_idx].last_used = ++lru_counter;
    }
}



// Function to find the cache slot for a given frame ID
int find_cache_slot(int frame_id) {
    for (int i = 0; i < ARTY_CACHED_NFRAMES; i++) {
        if (cache_info[i].frame_id == frame_id) {
            return i; // Return the index of the slot containing the frame ID
        }
    }
    return -1; // Return -1 if no matching frame ID is found
}

//Random Eviction Strategy: Random eviction can potentially evict pages that are frequently accessed, leading to more cache misses.
// static int cache_eviction() {        // Function to handle cache eviction.
//     int idx = rand() % ARTY_CACHED_NFRAMES;  // Selects a random cache slot to evict.
//     int frame_id = cache_slots[idx]; // Retrieves the frame ID of the selected cache slot.

//     // Writes the evicted frame back to disk.
//     earth->disk_write(frame_id * NBLOCKS_PER_PAGE, NBLOCKS_PER_PAGE, pages_start + PAGE_SIZE * idx);
//     return idx;                      // Returns the index of the evicted cache slot.
// }

// Least Recently Used (LRU) algorithm. In LRU, the cache slot that hasn't been used for the longest time is selected for eviction. 
//The cache_eviction function finds the least recently used slot by iterating through last_used.
static int cache_eviction() {
    // Find the least recently used cache slot.
    int lru_idx = 0;
    for (int i = 1; i < ARTY_CACHED_NFRAMES; i++) {
        if (last_used[i] < last_used[lru_idx]) {
            lru_idx = i;
        }
    }

    int frame_id = cache_slots[lru_idx];
    // Writes the evicted frame back to disk.
    earth->disk_write(frame_id * NBLOCKS_PER_PAGE, NBLOCKS_PER_PAGE, pages_start + PAGE_SIZE * lru_idx);
    return lru_idx;
}

//The paging_init function initializes the last_used array.
void paging_init() {
    memset(cache_slots, 0xFF, sizeof(cache_slots)); // Initializes the cache_slots array.
    memset(last_used, 0, sizeof(last_used)); // Initializes the last_used array.
}

//For Random Eviction Strategy
// void paging_init() {                 // Function to initialize paging.
//     memset(cache_slots, 0xFF, sizeof(cache_slots)); // Initializes the cache_slots array, setting all values to -1 (0xFF).
// }

// int paging_invalidate_cache(int frame_id) { // Function to invalidate a specific frame in the cache.
//     for (int j = 0; j < ARTY_CACHED_NFRAMES; j++)  // Iterates through all cache slots.
//         if (cache_slots[j] == frame_id) cache_slots[j] = -1; // If the frame ID matches, it sets the cache slot to -1 (invalidates it).
// }




//Main change:
//The loop exits as soon as the targeted frame is invalidated, reducing unnecessary iterations.
//The last_used array is updated to maintain the LRU order. This step assumes that you have implemented an LRU cache as suggested earlier.
//The function returns a status indicating whether the invalidation was successful (1) or the frame was not found (0).
int paging_invalidate_cache(int frame_id) {
    // Function to invalidate a specific frame in the cache.
    for (int j = 0; j < ARTY_CACHED_NFRAMES; j++) {
        if (cache_slots[j] == frame_id) {
            cache_slots[j] = -1; // Invalidate the cache slot.

            // Update the LRU tracking (assuming you have a last_used array as per LRU implementation).
            last_used[j] = -1; // Indicate that this slot is no longer used.

            return 1; // Return 1 to indicate successful invalidation.
        }
    }
    return 0; // Return 0 to indicate that the frame was not found.
}



//Main change:
//Optimize Cache Slot Lookup: Both functions iterate over all cache slots to find the relevant slot. This can be optimized using a hash map or a direct-mapped cache approach, reducing the time complexity from O(n) to O(1) in the best case.
//LRU Update on Access: Whenever a page is read or written, its last used time should be updated to maintain the LRU order.
//Write-Back Caching: Implement a write-back cache strategy where writes are made to the cache first and only written back to disk when necessary (e.g., during eviction). This requires an additional 'dirty bit' to track if the cache data has been modified.
//Read-Ahead Strategy: Implement a read-ahead strategy in paging_read to prefetch the next few pages, assuming sequential access. This can improve performance in scenarios where data is read sequentially.
//Return Status: Similar to paging_invalidate_cache, these functions can return a status indicating success, failure, or not-found scenarios.

// Assuming additional structures and variables for LRU and write-back caching are in place
int paging_write(int frame_id, int page_no) {
    char* src = (void*)(page_no << 12);
    if (earth->platform == QEMU) {
        memcpy(pages_start + frame_id * PAGE_SIZE, src, PAGE_SIZE);
        return 0; // Success
    }

    int slot_idx = find_cache_slot(frame_id); // find_cache_slot() is a hypothetical function to find the slot index quickly.
    if (slot_idx != -1) {
        memcpy(pages_start + PAGE_SIZE * slot_idx, src, PAGE_SIZE);
        mark_dirty(slot_idx); // Mark this slot as dirty since it's been written to.
        update_lru(slot_idx); // Update LRU for this slot.
        return 0; // Success
    }

    int free_idx = cache_eviction(); // Evict using LRU.
    cache_slots[free_idx] = frame_id;
    memcpy(pages_start + PAGE_SIZE * free_idx, src, PAGE_SIZE);
    mark_dirty(free_idx); // Mark as dirty.
    update_lru(free_idx); // Update LRU.
    return 0; // Success
}

char* paging_read(int frame_id, int alloc_only) {
    if (earth->platform == QEMU) return pages_start + frame_id * PAGE_SIZE;

    int slot_idx = find_cache_slot(frame_id);
    if (slot_idx != -1) {
        update_lru(slot_idx); // Update LRU for this slot.
        return pages_start + PAGE_SIZE * slot_idx;
    }

    int free_idx = cache_eviction(); // Evict using LRU.
    cache_slots[free_idx] = frame_id;
    if (!alloc_only) {
        earth->disk_read(frame_id * NBLOCKS_PER_PAGE, NBLOCKS_PER_PAGE, pages_start + PAGE_SIZE * free_idx);
    }
    update_lru(free_idx); // Update LRU.
    return pages_start + PAGE_SIZE * free_idx;
}

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
