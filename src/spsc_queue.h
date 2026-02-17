/*
 * spsc_queue.h — Lock-free bounded SPSC queue
 *
 * Single-producer single-consumer queue using a ring buffer.
 * No CAS needed — just atomic load/store with acquire/release ordering.
 * This is the same fundamental pattern used by Linux io_uring.
 *
 * Properties:
 * - Bounded (fixed capacity, power of 2)
 * - Single producer, single consumer (NOT safe for multiple of either)
 * - Wait-free (no retry loops)
 * - Cache-line padded head/tail to avoid false sharing
 *
 * Author: G.H. Murray
 * Date:   2026-02-17
 */

#ifndef SPSC_QUEUE_H
#define SPSC_QUEUE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>

#define SPSC_CACHE_LINE 64

typedef struct spsc_queue {
    void          **buffer;
    size_t          mask;

    char _pad0[SPSC_CACHE_LINE - sizeof(void **) - sizeof(size_t)];

    _Atomic(size_t) head;   /* consumer reads and advances */

    char _pad1[SPSC_CACHE_LINE - sizeof(_Atomic(size_t))];

    _Atomic(size_t) tail;   /* producer writes and advances */

    char _pad2[SPSC_CACHE_LINE - sizeof(_Atomic(size_t))];

    /* Cached copies to avoid cross-cache-line reads */
    size_t          cached_head;  /* producer's cached copy of head */
    size_t          cached_tail;  /* consumer's cached copy of tail */
} spsc_queue_t;

/*
 * spsc_queue_create — Create a bounded SPSC queue
 * @capacity: must be power of 2
 */
spsc_queue_t *spsc_queue_create(size_t capacity);

/*
 * spsc_queue_destroy — Destroy the queue
 */
void spsc_queue_destroy(spsc_queue_t *q);

/*
 * spsc_enqueue — Enqueue (producer only, wait-free)
 * Returns true if enqueued, false if full.
 */
bool spsc_enqueue(spsc_queue_t *q, void *data);

/*
 * spsc_dequeue — Dequeue (consumer only, wait-free)
 * Returns true and sets *data on success, false if empty.
 */
bool spsc_dequeue(spsc_queue_t *q, void **data);

/*
 * spsc_queue_size — Approximate size
 */
size_t spsc_queue_size(spsc_queue_t *q);

#endif /* SPSC_QUEUE_H */
