/*
 * ms_queue.h — Lock-free unbounded MPMC queue (Michael & Scott, 1996)
 *
 * Classic two-pointer lock-free queue: CAS on tail to enqueue,
 * CAS on head to dequeue. Uses a sentinel node to simplify edge cases.
 *
 * Memory reclamation via epoch-based reclamation (EBR).
 *
 * Author: G.H. Murray
 * Date:   2026-02-17
 */

#ifndef MS_QUEUE_H
#define MS_QUEUE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>

#include "epoch.h"

/* Forward decl */
typedef struct ms_queue ms_queue_t;

/*
 * Queue node — internal, not exposed to user.
 */
struct ms_node {
    void               *data;
    _Atomic(struct ms_node *) next;
};

/*
 * ms_queue_t — The unbounded lock-free queue.
 */
struct ms_queue {
    _Atomic(struct ms_node *) head;
    _Atomic(struct ms_node *) tail;
    epoch_t                   epoch;
};

/*
 * ms_queue_create — Create a new unbounded queue
 */
ms_queue_t *ms_queue_create(void);

/*
 * ms_queue_thread_register — Register calling thread for EBR.
 * Must call before enqueue/dequeue. Returns slot id.
 */
int ms_queue_thread_register(ms_queue_t *q);

/*
 * ms_queue_thread_unregister — Unregister thread.
 */
void ms_queue_thread_unregister(ms_queue_t *q, int slot);

/*
 * ms_queue_destroy — Destroy the queue (must be empty or will leak)
 */
void ms_queue_destroy(ms_queue_t *q);

/*
 * ms_enqueue — Enqueue an item (always succeeds unless OOM)
 */
bool ms_enqueue(ms_queue_t *q, void *data);

/*
 * ms_dequeue — Dequeue an item
 *
 * Returns true and sets *data on success, false if empty.
 * Dequeued nodes are safely reclaimed via epoch-based reclamation.
 */
bool ms_dequeue(ms_queue_t *q, void **data);

#endif /* MS_QUEUE_H */
