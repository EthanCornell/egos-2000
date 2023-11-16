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
#define ARTY_CACHED_NFRAMES 28      // Defines a constant for the number of cache frames, setting it to 28.
#define NBLOCKS_PER_PAGE PAGE_SIZE / BLOCK_SIZE  // Defines the number of blocks per page, calculated as page size divided by block size.

int cache_slots[ARTY_CACHED_NFRAMES]; // Array to keep track of cache slots, with size based on the defined number of cache frames.
char *pages_start = (void*)FRAME_CACHE_START; // Pointer to the start of the frame cache memory area.

static int cache_eviction() {        // Function to handle cache eviction.
    int idx = rand() % ARTY_CACHED_NFRAMES;  // Selects a random cache slot to evict.
    int frame_id = cache_slots[idx]; // Retrieves the frame ID of the selected cache slot.

    // Writes the evicted frame back to disk.
    earth->disk_write(frame_id * NBLOCKS_PER_PAGE, NBLOCKS_PER_PAGE, pages_start + PAGE_SIZE * idx);
    return idx;                      // Returns the index of the evicted cache slot.
}

void paging_init() {                 // Function to initialize paging.
    memset(cache_slots, 0xFF, sizeof(cache_slots)); // Initializes the cache_slots array, setting all values to -1 (0xFF).
}

int paging_invalidate_cache(int frame_id) { // Function to invalidate a specific frame in the cache.
    for (int j = 0; j < ARTY_CACHED_NFRAMES; j++)  // Iterates through all cache slots.
        if (cache_slots[j] == frame_id) cache_slots[j] = -1; // If the frame ID matches, it sets the cache slot to -1 (invalidates it).
}

int paging_write(int frame_id, int page_no) { // Function to handle writing a page to a cache frame.
    char* src = (void*)(page_no << 12); // Calculates the source address by shifting the page number left by 12 bits (effectively multiplying by 4096, the size of a page).
    if (earth->platform == QEMU) {      // Checks if the platform is QEMU.
        memcpy(pages_start + frame_id * PAGE_SIZE, src, PAGE_SIZE); // Directly copies the page to the cache for QEMU platform.
        return 0;
    }

    for (int i = 0; i < ARTY_CACHED_NFRAMES; i++)  // Iterates through the cache slots.
        if (cache_slots[i] == frame_id) {          // Checks if the current slot holds the frame.
            memcpy(pages_start + PAGE_SIZE * i, src, PAGE_SIZE) != NULL; // Copies the page to the cache slot.
            return 0;
        }

    int free_idx = cache_eviction();  // If no matching frame is found, evict a frame.
    cache_slots[free_idx] = frame_id; // Update the cache slot with the new frame ID.
    memcpy(pages_start + PAGE_SIZE * free_idx, src, PAGE_SIZE); // Copy the page to the newly freed cache slot.
}

char* paging_read(int frame_id, int alloc_only) {  // Function to handle reading a page from a cache frame.
    if (earth->platform == QEMU) return pages_start + frame_id * PAGE_SIZE; // Directly returns the page address for QEMU platform.

    int free_idx = -1;                            // Initializes the index for a free slot.
    for (int i = 0; i < ARTY_CACHED_NFRAMES; i++) { // Iterates through the cache slots.
        if (cache_slots[i] == -1 && free_idx == -1) free_idx = i; // Finds the first free slot.
        if (cache_slots[i] == frame_id) return pages_start + PAGE_SIZE * i; // Returns the address of the page if found in cache.
    }

    if (free_idx == -1) free_idx = cache_eviction(); // If no free slot, evict a slot.
    cache_slots[free_idx] = frame_id;                // Updates the cache slot with the new frame ID.

    if (!alloc_only) // If not just allocating, also read the page from the disk.
        earth->disk_read(frame_id * NBLOCKS_PER_PAGE, NBLOCKS_PER_PAGE, pages_start + PAGE_SIZE * free_idx);

    return pages_start + PAGE_SIZE * free_idx; // Returns the address of the page in cache.
}
