#define _GNU_SOURCE

#include "ring_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>

void ring_buffer_init(RingBuffer *ring, PipelineMsg *buffer, size_t capacity) {
    /* Power-of-two capacity is strictly required for bitwise AND wrapping */
    assert((capacity & (capacity - 1)) == 0 && "capacity must be a power of 2");
    assert(buffer != NULL);

    atomic_init(&ring->prod.head, 0);
    ring->prod.cached_tail = 0;

    atomic_init(&ring->cons.tail, 0);
    ring->cons.cached_head = 0;

    ring->meta.capacity = capacity;
    ring->meta.mask     = capacity - 1;
    ring->meta.entries  = buffer;
}

/*
 * Section: Double-Mapped Virtual Memory Mirrored Ring Buffer
 *
 * Microarchitectural Invariants:
 *   1. Physical Memory Sharing:
 *      A single physical allocation of `capacity * sizeof(PipelineMsg)` is mapped twice
 *      into contiguous virtual address space: [addr, addr + bytes) and [addr + bytes, addr + 2*bytes).
 *   2. Wrap-around Branch Elimination:
 *      Any read or write slice starting at `head & mask` with length <= capacity is contiguous
 *      in virtual address space. Writes overflowing past `capacity` wrap seamlessly into the
 *      second mapping without branching or split copies.
 *   3. Page Alignment Guarantee:
 *      The total buffer byte size must be a multiple of the OS page size (typically 4096 bytes).
 */
bool ring_buffer_init_mmap(RingBuffer *ring, size_t capacity) {
    assert(ring != NULL);
    assert((capacity & (capacity - 1)) == 0 && "capacity must be a power of 2");

    size_t bytes = capacity * sizeof(PipelineMsg);
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        page_size = 4096;
    }
    assert(bytes % (size_t)page_size == 0 && "buffer size must be a multiple of page size");

    /*
     * Step 1: Create an anonymous in-memory file descriptor.
     * MFD_CLOEXEC prevents file descriptor leaks into child processes.
     */
    int fd = memfd_create("ring_buffer_mmap", MFD_CLOEXEC);
    if (fd < 0) {
        perror("memfd_create");
        return false;
    }

    if (ftruncate(fd, bytes) < 0) {
        perror("ftruncate");
        close(fd);
        return false;
    }

    /*
     * Step 2: Reserve a contiguous 2 * bytes virtual address window with PROT_NONE.
     * MAP_PRIVATE | MAP_ANONYMOUS reserves address space without committing physical pages.
     */
    uint8_t *addr = mmap(NULL, 2 * bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        perror("mmap reserve");
        close(fd);
        return false;
    }

    /*
     * Step 3: Map the same physical buffer TWICE side-by-side using MAP_FIXED:
     *   - First mapping:  [addr, addr + bytes)
     *   - Second mapping: [addr + bytes, addr + 2 * bytes)
     */
    void *m1 = mmap(addr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    if (m1 == MAP_FAILED) {
        perror("mmap primary buffer");
        munmap(addr, 2 * bytes);
        close(fd);
        return false;
    }

    void *m2 = mmap(addr + bytes, bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    if (m2 == MAP_FAILED) {
        perror("mmap mirror buffer");
        munmap(addr, 2 * bytes);
        close(fd);
        return false;
    }

    /*
     * File descriptor is no longer needed after mmap; kernel retains inode reference.
     */
    close(fd);

    atomic_init(&ring->prod.head, 0);
    ring->prod.cached_tail = 0;

    atomic_init(&ring->cons.tail, 0);
    ring->cons.cached_head = 0;

    ring->meta.capacity = capacity;
    ring->meta.mask     = capacity - 1;
    ring->meta.entries  = (PipelineMsg *)addr;

    return true;
}

void ring_buffer_destroy_mmap(RingBuffer *ring) {
    if (ring != NULL && ring->meta.entries != NULL) {
        size_t total_mapped_bytes = 2 * ring->meta.capacity * sizeof(PipelineMsg);
        munmap((void *)ring->meta.entries, total_mapped_bytes);
        ring->meta.entries = NULL;
    }
}
