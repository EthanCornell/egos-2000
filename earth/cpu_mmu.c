/*
 * (C) 2022, Cornell University
 * All rights reserved.
 */

/* Author: Yunhao Zhang
 * Description: abstractions of the CPU memory management unit (MMU);
 * there are 256 physical frames at the start of the SD card and 20 of
 * them are cached in the memory (more precisely, in the CPU L1 cache);
 * the first 12 pages in memory are used as the virtual memory address
 * space for a process; there are totally 20 + 12 = 32 pages used by
 * kernel/user processes; other pages are used by earth and grass;
 * find more information in the documents
 */

#include "egos.h"
#include "earth.h"

#include "mmu.h"
#define INUSE(x) (x.flag & F_INUSE)
#define USE(x)   x.flag |= F_INUSE

int curr_vm_pid;
struct translation_table_t* trans_table;

int cache_frame_no[CACHED_NFRAMES];
struct frame_t* cache = (void*)CACHE_START;

static int cache_read(int frame_no);
static int cache_write(int frame_no, struct frame_t* src);

int mmu_init() {
    curr_vm_pid = -1;

    trans_table = (void*) TRANS_TABLE_START;
    if (TRANS_TABLE_START + TRANS_TABLE_SIZE > TRANS_TABLE_TOP)
        FATAL("Translation table exceeds memory limit", MAX_NFRAMES);
    
    memset(cache_frame_no, 0xff, sizeof(cache_frame_no));
    memset(trans_table, 0, sizeof(struct translation_table_t));
    return 0;
}

int mmu_alloc(int* frame_no, int* addr) {
    for (int i = 0; i < MAX_NFRAMES; i++) {
        if (!INUSE(trans_table->frame[i])) {
            *frame_no = i;
            *addr = cache_read(i);
            USE(trans_table->frame[i]);
            return 0;
        }
    }
    return -1;
}

int mmu_map(int pid, int page_no, int frame_no, int flag) {
    if (!INUSE(trans_table->frame[frame_no])) {
        ERROR("Frame %d has not been allocated", frame_no);
        return -1;
    }

    trans_table->frame[frame_no].pid = pid;
    trans_table->frame[frame_no].page_no = page_no;
    trans_table->frame[frame_no].flag = flag;
    return 0;
}

int mmu_switch(int pid) {
    if (curr_vm_pid != -1) {
        FATAL("context switch not implemented");
    }

    char* base = (void*) VADDR_START;
    for (int i = 0; i < MAX_NFRAMES; i++) {
        if (INUSE(trans_table->frame[i])
            && trans_table->frame[i].pid == pid) {
            int addr = cache_read(i);
            memcpy(base + PAGE_SIZE * trans_table->frame[i].page_no, (char*)addr, PAGE_SIZE);
            INFO("Map frame #%d to page #%d of process #%d", i, trans_table->frame[i].page_no, pid);
        }
    }
    return 0;
}

static int cache_read(int frame_no) {
    int free_no = -1;
    for (int i = 0; i < CACHED_NFRAMES; i++) {
        if (cache_frame_no[i] == frame_no)
            return (int)(cache + i);
        if (cache_frame_no[i] == -1 && free_no == -1)
            free_no = i;
    }

    if (free_no != -1) {
        cache_frame_no[free_no] = frame_no;
        int group = PAGE_SIZE / BLOCK_SIZE;
        disk_read(free_no * group, group, (char*)(cache + free_no));

        return (int)(cache + free_no);
    } else {
        FATAL("Cache is full and eviction is not implemented");
    }
}
