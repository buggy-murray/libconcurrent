/*
 * mpmc_queue.c — Bounded lock-free MPMC queue (Vyukov)
 *
 * Algorithm:
 *   Each cell has a sequence number initialized to its index.
 *
 *   Enqueue:
 *     1. Load enqueue_pos
 *     2. Load cell[pos & mask].sequence
 *     3. If seq == pos: CAS enqueue_pos to pos+1, write data, store seq = pos+1
 *        If seq < pos: queue full (return false)
 *        If seq > pos: lost race, retry from step 1
 *
 *   Dequeue:
 *     1. Load dequeue_pos
 *     2. Load cell[pos & mask].sequence
 *     3. If seq == pos+1: CAS dequeue_pos to pos+1, read data, store seq = pos+mask+1
 *        If seq < pos+1: queue empty (return false)
 *        If seq > pos+1: lost race, retry from step 1
 *
 * The sequence numbers serve as per-cell state machines:
 *   seq == index:           cell is empty, available for writing
 *   seq == index + 1:       cell is full, available for reading
 *   seq == index + cap:     cell has been read, available for next write cycle
 *
 * Author: G.H. Murray
 * Date:   2026-02-17
 */

#define _GNU_SOURCE
#include "mpmc_queue.h"

#include <stdlib.h>
#include <string.h>

mpmc_queue_t *mpmc_queue_create(size_t capacity)
{
    /* Must be power of 2 */
    if (capacity == 0 || (capacity & (capacity - 1)) != 0)
        return NULL;

    mpmc_queue_t *q = aligned_alloc(MPMC_CACHE_LINE, sizeof(mpmc_queue_t));
    if (!q) return NULL;
    memset(q, 0, sizeof(*q));

    q->buffer = aligned_alloc(MPMC_CACHE_LINE, capacity * sizeof(mpmc_cell_t));
    if (!q->buffer) {
        free(q);
        return NULL;
    }

    q->mask = capacity - 1;

    /* Initialize each cell's sequence to its index */
    for (size_t i = 0; i < capacity; i++) {
        atomic_store_explicit(&q->buffer[i].sequence, i, memory_order_relaxed);
        q->buffer[i].data = NULL;
    }

    atomic_store_explicit(&q->enqueue_pos, 0, memory_order_relaxed);
    atomic_store_explicit(&q->dequeue_pos, 0, memory_order_relaxed);

    return q;
}

void mpmc_queue_destroy(mpmc_queue_t *q)
{
    if (!q) return;
    free(q->buffer);
    free(q);
}

bool mpmc_enqueue(mpmc_queue_t *q, void *data)
{
    mpmc_cell_t *cell;
    size_t pos = atomic_load_explicit(&q->enqueue_pos, memory_order_relaxed);

    for (;;) {
        cell = &q->buffer[pos & q->mask];
        size_t seq = atomic_load_explicit(&cell->sequence, memory_order_acquire);
        intptr_t diff = (intptr_t)seq - (intptr_t)pos;

        if (diff == 0) {
            /* Cell is available for writing — try to claim this position */
            if (atomic_compare_exchange_weak_explicit(
                    &q->enqueue_pos, &pos, pos + 1,
                    memory_order_relaxed, memory_order_relaxed)) {
                break;  /* claimed! */
            }
            /* CAS failed — pos was updated by CAS, retry with new pos */
        } else if (diff < 0) {
            /* Queue is full */
            return false;
        } else {
            /* Another producer claimed this cell first — reload pos */
            pos = atomic_load_explicit(&q->enqueue_pos, memory_order_relaxed);
        }
    }

    /* Write data and publish by advancing the cell's sequence */
    cell->data = data;
    atomic_store_explicit(&cell->sequence, pos + 1, memory_order_release);
    return true;
}

bool mpmc_dequeue(mpmc_queue_t *q, void **data)
{
    mpmc_cell_t *cell;
    size_t pos = atomic_load_explicit(&q->dequeue_pos, memory_order_relaxed);

    for (;;) {
        cell = &q->buffer[pos & q->mask];
        size_t seq = atomic_load_explicit(&cell->sequence, memory_order_acquire);
        intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);

        if (diff == 0) {
            /* Cell has data — try to claim this position */
            if (atomic_compare_exchange_weak_explicit(
                    &q->dequeue_pos, &pos, pos + 1,
                    memory_order_relaxed, memory_order_relaxed)) {
                break;  /* claimed! */
            }
        } else if (diff < 0) {
            /* Queue is empty */
            return false;
        } else {
            /* Another consumer claimed this cell — reload pos */
            pos = atomic_load_explicit(&q->dequeue_pos, memory_order_relaxed);
        }
    }

    /* Read data and release the cell for the next write cycle */
    *data = cell->data;
    atomic_store_explicit(&cell->sequence, pos + q->mask + 1, memory_order_release);
    return true;
}

size_t mpmc_queue_size(mpmc_queue_t *q)
{
    size_t enq = atomic_load_explicit(&q->enqueue_pos, memory_order_relaxed);
    size_t deq = atomic_load_explicit(&q->dequeue_pos, memory_order_relaxed);
    return (enq >= deq) ? (enq - deq) : 0;
}
