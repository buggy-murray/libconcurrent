/*
 * mpmc_queue.h — Bounded lock-free MPMC queue
 *
 * Based on Dmitry Vyukov's bounded MPMC queue design.
 * Each cell has a sequence counter; producers and consumers use CAS
 * on shared enqueue/dequeue positions. 1 CAS per operation.
 *
 * Properties:
 * - Bounded (fixed capacity, power of 2)
 * - Multi-producer, multi-consumer
 * - Lock-free (not wait-free; CAS may retry)
 * - Cache-line padded to avoid false sharing
 *
 * Author: G.H. Murray
 * Date:   2026-02-17
 */

#ifndef MPMC_QUEUE_H
#define MPMC_QUEUE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#define MPMC_CACHE_LINE 64

/*
 * Cell in the ring buffer. Each cell has its own sequence counter
 * that tracks whether it's available for writing or reading.
 */
typedef struct mpmc_cell {
    _Atomic(size_t) sequence;
    void           *data;
} __attribute__((aligned(MPMC_CACHE_LINE))) mpmc_cell_t;

/*
 * mpmc_queue_t — The queue.
 *
 * enqueue_pos and dequeue_pos are on separate cache lines
 * to avoid false sharing between producers and consumers.
 */
typedef struct mpmc_queue {
    mpmc_cell_t    *buffer;
    size_t          mask;       /* capacity - 1 */

    char _pad0[MPMC_CACHE_LINE - sizeof(mpmc_cell_t *) - sizeof(size_t)];

    _Atomic(size_t) enqueue_pos;

    char _pad1[MPMC_CACHE_LINE - sizeof(_Atomic(size_t))];

    _Atomic(size_t) dequeue_pos;

    char _pad2[MPMC_CACHE_LINE - sizeof(_Atomic(size_t))];
} mpmc_queue_t;

/*
 * mpmc_queue_create — Create a new bounded MPMC queue
 *
 * @capacity: Must be a power of 2
 * Returns NULL on failure.
 */
mpmc_queue_t *mpmc_queue_create(size_t capacity);

/*
 * mpmc_queue_destroy — Destroy the queue
 */
void mpmc_queue_destroy(mpmc_queue_t *q);

/*
 * mpmc_enqueue — Enqueue an item (non-blocking)
 *
 * Returns true if enqueued, false if queue is full.
 */
bool mpmc_enqueue(mpmc_queue_t *q, void *data);

/*
 * mpmc_dequeue — Dequeue an item (non-blocking)
 *
 * Returns true if dequeued (and sets *data), false if queue is empty.
 */
bool mpmc_dequeue(mpmc_queue_t *q, void **data);

/*
 * mpmc_queue_size — Approximate current size (not linearizable)
 */
size_t mpmc_queue_size(mpmc_queue_t *q);

#endif /* MPMC_QUEUE_H */
