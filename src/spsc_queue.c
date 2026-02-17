/*
 * spsc_queue.c — Lock-free bounded SPSC queue
 *
 * Wait-free: no CAS, no retry loops. Just atomic load/store.
 *
 * The producer owns `tail` (writes), reads `head` (to check full).
 * The consumer owns `head` (writes), reads `tail` (to check empty).
 *
 * Optimization: each side caches the other's counter to avoid
 * frequent cross-cache-line atomic reads. The cached value is
 * pessimistic — only refreshed when the local check suggests
 * the queue might be full/empty.
 *
 * Memory ordering:
 *   - Producer: store data (relaxed), then store_release tail
 *   - Consumer: load_acquire tail, then read data
 *   - Symmetric for head
 *
 * Author: G.H. Murray
 * Date:   2026-02-17
 */

#define _GNU_SOURCE
#include "spsc_queue.h"

#include <stdlib.h>
#include <string.h>

spsc_queue_t *spsc_queue_create(size_t capacity)
{
    if (capacity == 0 || (capacity & (capacity - 1)) != 0)
        return NULL;

    spsc_queue_t *q = aligned_alloc(SPSC_CACHE_LINE, sizeof(spsc_queue_t));
    if (!q) return NULL;
    memset(q, 0, sizeof(*q));

    q->buffer = calloc(capacity, sizeof(void *));
    if (!q->buffer) {
        free(q);
        return NULL;
    }

    q->mask = capacity - 1;
    atomic_store_explicit(&q->head, 0, memory_order_relaxed);
    atomic_store_explicit(&q->tail, 0, memory_order_relaxed);
    q->cached_head = 0;
    q->cached_tail = 0;

    return q;
}

void spsc_queue_destroy(spsc_queue_t *q)
{
    if (!q) return;
    free(q->buffer);
    free(q);
}

bool spsc_enqueue(spsc_queue_t *q, void *data)
{
    size_t tail = atomic_load_explicit(&q->tail, memory_order_relaxed);
    size_t next_tail = tail + 1;

    /* Check if full using cached head */
    if (next_tail - q->cached_head > q->mask + 1) {
        /* Refresh cached head */
        q->cached_head = atomic_load_explicit(&q->head, memory_order_acquire);
        if (next_tail - q->cached_head > q->mask + 1)
            return false;  /* truly full */
    }

    q->buffer[tail & q->mask] = data;
    atomic_store_explicit(&q->tail, next_tail, memory_order_release);
    return true;
}

bool spsc_dequeue(spsc_queue_t *q, void **data)
{
    size_t head = atomic_load_explicit(&q->head, memory_order_relaxed);

    /* Check if empty using cached tail */
    if (head == q->cached_tail) {
        /* Refresh cached tail */
        q->cached_tail = atomic_load_explicit(&q->tail, memory_order_acquire);
        if (head == q->cached_tail)
            return false;  /* truly empty */
    }

    *data = q->buffer[head & q->mask];
    atomic_store_explicit(&q->head, head + 1, memory_order_release);
    return true;
}

size_t spsc_queue_size(spsc_queue_t *q)
{
    size_t tail = atomic_load_explicit(&q->tail, memory_order_relaxed);
    size_t head = atomic_load_explicit(&q->head, memory_order_relaxed);
    return tail - head;
}
