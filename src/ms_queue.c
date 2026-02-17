/*
 * ms_queue.c — Lock-free unbounded MPMC queue (Michael & Scott, 1996)
 *
 * Algorithm:
 *   Enqueue: alloc node, CAS onto tail->next, then swing tail forward.
 *   Dequeue: read head->next (sentinel trick), CAS head forward, return data.
 *
 * The sentinel node means head always points to a dummy (already dequeued).
 * The real first element is at head->next.
 *
 * Helping: if tail is lagging (tail->next != NULL), any thread can
 * swing tail forward before retrying its own operation.
 *
 * Author: G.H. Murray
 * Date:   2026-02-17
 */

#define _GNU_SOURCE
#include "ms_queue.h"

#include <stdlib.h>

static void node_free_cb(void *ptr) { free(ptr); }

static __thread int tls_ms_slot = -1;

static struct ms_node *node_alloc(void *data)
{
    struct ms_node *n = malloc(sizeof(struct ms_node));
    if (!n) return NULL;
    n->data = data;
    atomic_store_explicit(&n->next, NULL, memory_order_relaxed);
    return n;
}

ms_queue_t *ms_queue_create(void)
{
    ms_queue_t *q = malloc(sizeof(ms_queue_t));
    if (!q) return NULL;

    /* Sentinel node */
    struct ms_node *sentinel = node_alloc(NULL);
    if (!sentinel) {
        free(q);
        return NULL;
    }

    atomic_store(&q->head, sentinel);
    atomic_store(&q->tail, sentinel);
    epoch_init(&q->epoch, node_free_cb);
    return q;
}

int ms_queue_thread_register(ms_queue_t *q)
{
    int slot = epoch_register(&q->epoch);
    tls_ms_slot = slot;
    return slot;
}

void ms_queue_thread_unregister(ms_queue_t *q, int slot)
{
    epoch_unregister(&q->epoch, slot);
    if (tls_ms_slot == slot) tls_ms_slot = -1;
}

void ms_queue_destroy(ms_queue_t *q)
{
    if (!q) return;

    /* Drain pending epoch retires */
    epoch_destroy(&q->epoch);

    /* Drain remaining nodes */
    struct ms_node *node = atomic_load(&q->head);
    while (node) {
        struct ms_node *next = atomic_load(&node->next);
        free(node);
        node = next;
    }
    free(q);
}

bool ms_enqueue(ms_queue_t *q, void *data)
{
    struct ms_node *node = node_alloc(data);
    if (!node) return false;

    int slot = tls_ms_slot;
    if (slot >= 0) epoch_enter(&q->epoch, slot);

    for (;;) {
        struct ms_node *tail = atomic_load_explicit(&q->tail, memory_order_acquire);
        struct ms_node *next = atomic_load_explicit(&tail->next, memory_order_acquire);

        if (tail != atomic_load_explicit(&q->tail, memory_order_acquire))
            continue;

        if (next == NULL) {
            if (atomic_compare_exchange_weak_explicit(
                    &tail->next, &next, node,
                    memory_order_release, memory_order_relaxed)) {
                atomic_compare_exchange_strong_explicit(
                    &q->tail, &tail, node,
                    memory_order_release, memory_order_relaxed);
                if (slot >= 0) epoch_exit(&q->epoch, slot);
                return true;
            }
        } else {
            atomic_compare_exchange_weak_explicit(
                &q->tail, &tail, next,
                memory_order_release, memory_order_relaxed);
        }
    }
}

bool ms_dequeue(ms_queue_t *q, void **data)
{
    int slot = tls_ms_slot;
    if (slot >= 0) epoch_enter(&q->epoch, slot);

    for (;;) {
        struct ms_node *head = atomic_load_explicit(&q->head, memory_order_acquire);
        struct ms_node *tail = atomic_load_explicit(&q->tail, memory_order_acquire);
        struct ms_node *next = atomic_load_explicit(&head->next, memory_order_acquire);

        if (head != atomic_load_explicit(&q->head, memory_order_acquire))
            continue;

        if (head == tail) {
            if (next == NULL) {
                if (slot >= 0) epoch_exit(&q->epoch, slot);
                return false;
            }
            atomic_compare_exchange_weak_explicit(
                &q->tail, &tail, next,
                memory_order_release, memory_order_relaxed);
        } else {
            void *val = next->data;

            if (atomic_compare_exchange_weak_explicit(
                    &q->head, &head, next,
                    memory_order_release, memory_order_relaxed)) {
                *data = val;
                /* Retire old sentinel via EBR — safe deferred free */
                epoch_retire(&q->epoch, head);
                if (slot >= 0) epoch_exit(&q->epoch, slot);
                return true;
            }
        }
    }
}
