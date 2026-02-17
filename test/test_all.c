/*
 * test_all.c — Unified test suite for libconcurrent
 *
 * Tests all lock-free data structures:
 *   - Epoch-based reclamation (EBR)
 *   - Bounded MPMC queue (Vyukov)
 *   - Unbounded M&S queue (Michael & Scott)
 *   - Lock-free hash map (split-ordered lists)
 *
 * Author: G.H. Murray
 * Date:   2026-02-17
 */

#define _GNU_SOURCE
#include "../src/epoch.h"
#include "../src/mpmc_queue.h"
#include "../src/ms_queue.h"
#include "../src/hashmap.h"
#include "../src/spsc_queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <time.h>

static int passed = 0;
static int failed = 0;

#define TEST(name) do { \
    printf("  %-40s ", #name); \
    fflush(stdout); \
    name(); \
    printf("OK\n"); \
    passed++; \
} while(0)

static double elapsed_ms(struct timespec *start, struct timespec *end) {
    return (end->tv_sec - start->tv_sec) * 1000.0 +
           (end->tv_nsec - start->tv_nsec) / 1e6;
}

/* ── EBR Tests ── */

static _Atomic int ebr_freed = 0;
static void ebr_free_fn(void *p) { atomic_fetch_add(&ebr_freed, 1); free(p); }

static void test_ebr_basic(void)
{
    epoch_t e;
    epoch_init(&e, ebr_free_fn);
    atomic_store(&ebr_freed, 0);

    int slot = epoch_register(&e);
    epoch_enter(&e, slot);
    for (int i = 0; i < 10; i++)
        epoch_retire(&e, malloc(64));
    epoch_exit(&e, slot);

    for (int i = 0; i < 5; i++) {
        epoch_enter(&e, slot);
        epoch_exit(&e, slot);
    }

    assert(atomic_load(&ebr_freed) == 10);
    epoch_unregister(&e, slot);
    epoch_destroy(&e);
}

/* ── MPMC Queue Tests ── */

static void test_mpmc_fifo(void)
{
    mpmc_queue_t *q = mpmc_queue_create(16);
    int a = 1, b = 2, c = 3;
    void *out;

    mpmc_enqueue(q, &a);
    mpmc_enqueue(q, &b);
    mpmc_enqueue(q, &c);

    assert(mpmc_dequeue(q, &out) && out == &a);
    assert(mpmc_dequeue(q, &out) && out == &b);
    assert(mpmc_dequeue(q, &out) && out == &c);
    assert(!mpmc_dequeue(q, &out));

    mpmc_queue_destroy(q);
}

static void test_mpmc_full_empty(void)
{
    mpmc_queue_t *q = mpmc_queue_create(8);
    int x = 42;
    void *out;

    for (int i = 0; i < 8; i++) assert(mpmc_enqueue(q, &x));
    assert(!mpmc_enqueue(q, &x));
    for (int i = 0; i < 8; i++) assert(mpmc_dequeue(q, &out));
    assert(!mpmc_dequeue(q, &out));

    mpmc_queue_destroy(q);
}

#define MPMC_MT_N 4
#define MPMC_MT_OPS 50000

struct mpmc_worker_args { mpmc_queue_t *q; size_t count; };

static void *mpmc_prod(void *arg) {
    struct mpmc_worker_args *a = arg;
    for (size_t i = 0; i < MPMC_MT_OPS; i++)
        while (!mpmc_enqueue(a->q, (void *)(uintptr_t)(i+1))) {}
    a->count = MPMC_MT_OPS;
    return NULL;
}

static void *mpmc_cons(void *arg) {
    struct mpmc_worker_args *a = arg;
    void *data;
    size_t n = 0;
    while (n < MPMC_MT_OPS) {
        if (mpmc_dequeue(a->q, &data)) n++;
    }
    a->count = n;
    return NULL;
}

static void test_mpmc_mt(void)
{
    mpmc_queue_t *q = mpmc_queue_create(1 << 14);
    pthread_t pt[MPMC_MT_N], ct[MPMC_MT_N];
    struct mpmc_worker_args pa[MPMC_MT_N], ca[MPMC_MT_N];

    for (int i = 0; i < MPMC_MT_N; i++) {
        pa[i] = (struct mpmc_worker_args){ .q = q };
        ca[i] = (struct mpmc_worker_args){ .q = q };
        pthread_create(&pt[i], NULL, mpmc_prod, &pa[i]);
        pthread_create(&ct[i], NULL, mpmc_cons, &ca[i]);
    }

    size_t te = 0, td = 0;
    for (int i = 0; i < MPMC_MT_N; i++) { pthread_join(pt[i], NULL); te += pa[i].count; }
    for (int i = 0; i < MPMC_MT_N; i++) { pthread_join(ct[i], NULL); td += ca[i].count; }
    assert(te == td);
    mpmc_queue_destroy(q);
}

/* ── M&S Queue Tests ── */

static void test_ms_fifo(void)
{
    ms_queue_t *q = ms_queue_create();
    int slot = ms_queue_thread_register(q);
    int a = 10, b = 20;
    void *out;

    ms_enqueue(q, &a);
    ms_enqueue(q, &b);
    assert(ms_dequeue(q, &out) && out == &a);
    assert(ms_dequeue(q, &out) && out == &b);
    assert(!ms_dequeue(q, &out));

    ms_queue_thread_unregister(q, slot);
    ms_queue_destroy(q);
}

#define MS_MT_N 4
#define MS_MT_OPS 25000

struct ms_worker_args { ms_queue_t *q; size_t count; };

static void *ms_prod(void *arg) {
    struct ms_worker_args *a = arg;
    int slot = ms_queue_thread_register(a->q);
    for (size_t i = 0; i < MS_MT_OPS; i++)
        ms_enqueue(a->q, (void *)(uintptr_t)(i+1));
    a->count = MS_MT_OPS;
    ms_queue_thread_unregister(a->q, slot);
    return NULL;
}

static void *ms_cons(void *arg) {
    struct ms_worker_args *a = arg;
    int slot = ms_queue_thread_register(a->q);
    void *data;
    size_t n = 0;
    while (n < MS_MT_OPS) {
        if (ms_dequeue(a->q, &data)) n++;
    }
    a->count = n;
    ms_queue_thread_unregister(a->q, slot);
    return NULL;
}

static void test_ms_mt(void)
{
    ms_queue_t *q = ms_queue_create();
    pthread_t pt[MS_MT_N], ct[MS_MT_N];
    struct ms_worker_args pa[MS_MT_N], ca[MS_MT_N];

    for (int i = 0; i < MS_MT_N; i++) {
        pa[i] = (struct ms_worker_args){ .q = q };
        ca[i] = (struct ms_worker_args){ .q = q };
        pthread_create(&pt[i], NULL, ms_prod, &pa[i]);
        pthread_create(&ct[i], NULL, ms_cons, &ca[i]);
    }

    size_t te = 0, td = 0;
    for (int i = 0; i < MS_MT_N; i++) { pthread_join(pt[i], NULL); te += pa[i].count; }
    for (int i = 0; i < MS_MT_N; i++) { pthread_join(ct[i], NULL); td += ca[i].count; }
    assert(te == td);
    ms_queue_destroy(q);
}

/* ── Hash Map Tests ── */

static void test_hashmap_basic(void)
{
    hashmap_t *map = hashmap_create();
    int slot = hashmap_thread_register(map);
    int v1 = 42, v2 = 99;

    assert(hashmap_put(map, 1, &v1) == NULL);
    assert(hashmap_put(map, 2, &v2) == NULL);
    assert(hashmap_get(map, 1) == &v1);
    assert(hashmap_get(map, 2) == &v2);
    assert(hashmap_get(map, 3) == NULL);

    void *old = hashmap_remove(map, 1);
    assert(old == &v1);
    assert(hashmap_get(map, 1) == NULL);

    hashmap_thread_unregister(map, slot);
    hashmap_destroy(map);
}

#define HM_MT_N 4
#define HM_MT_OPS 5000

struct hm_worker_args { hashmap_t *map; int tid; int ok; };

static void *hm_worker(void *arg) {
    struct hm_worker_args *a = arg;
    int slot = hashmap_thread_register(a->map);
    int base = a->tid * HM_MT_OPS;
    int ok = 0;

    for (int i = 0; i < HM_MT_OPS; i++) {
        int *v = malloc(sizeof(int));
        *v = base + i;
        hashmap_put(a->map, (uint64_t)(base + i + 1), v);
    }
    for (int i = 0; i < HM_MT_OPS; i++) {
        void *v = hashmap_get(a->map, (uint64_t)(base + i + 1));
        if (v && *(int*)v == base + i) ok++;
    }
    for (int i = 0; i < HM_MT_OPS; i++) {
        void *v = hashmap_remove(a->map, (uint64_t)(base + i + 1));
        if (v) free(v);
    }

    a->ok = ok;
    hashmap_thread_unregister(a->map, slot);
    return NULL;
}

static void test_hashmap_mt(void)
{
    hashmap_t *map = hashmap_create();
    pthread_t threads[HM_MT_N];
    struct hm_worker_args args[HM_MT_N];

    for (int i = 0; i < HM_MT_N; i++) {
        args[i] = (struct hm_worker_args){ .map = map, .tid = i };
        pthread_create(&threads[i], NULL, hm_worker, &args[i]);
    }

    int total_ok = 0;
    for (int i = 0; i < HM_MT_N; i++) {
        pthread_join(threads[i], NULL);
        total_ok += args[i].ok;
    }

    assert(total_ok == HM_MT_N * HM_MT_OPS);
    assert(hashmap_count(map) == 0);
    hashmap_destroy(map);
}

/* ── SPSC Queue Tests ── */

static void test_spsc_fifo(void)
{
    spsc_queue_t *q = spsc_queue_create(16);
    int a = 1, b = 2, c = 3;
    void *out;

    spsc_enqueue(q, &a);
    spsc_enqueue(q, &b);
    spsc_enqueue(q, &c);

    assert(spsc_dequeue(q, &out) && out == &a);
    assert(spsc_dequeue(q, &out) && out == &b);
    assert(spsc_dequeue(q, &out) && out == &c);
    assert(!spsc_dequeue(q, &out));

    spsc_queue_destroy(q);
}

static void test_spsc_full_empty(void)
{
    spsc_queue_t *q = spsc_queue_create(8);
    int x = 42;
    void *out;

    for (int i = 0; i < 8; i++) assert(spsc_enqueue(q, &x));
    assert(!spsc_enqueue(q, &x));
    for (int i = 0; i < 8; i++) assert(spsc_dequeue(q, &out));
    assert(!spsc_dequeue(q, &out));

    spsc_queue_destroy(q);
}

struct spsc_mt_args { spsc_queue_t *q; size_t count; };

static void *spsc_prod(void *arg) {
    struct spsc_mt_args *a = arg;
    for (size_t i = 0; i < 500000; i++)
        while (!spsc_enqueue(a->q, (void *)(uintptr_t)(i+1))) {}
    a->count = 500000;
    return NULL;
}

static void *spsc_cons(void *arg) {
    struct spsc_mt_args *a = arg;
    void *data;
    size_t n = 0;
    while (n < 500000) {
        if (spsc_dequeue(a->q, &data)) {
            assert(data != NULL);
            n++;
        }
    }
    a->count = n;
    return NULL;
}

static void test_spsc_mt(void)
{
    spsc_queue_t *q = spsc_queue_create(1 << 14);
    pthread_t pt, ct;
    struct spsc_mt_args pa = { .q = q }, ca = { .q = q };

    pthread_create(&pt, NULL, spsc_prod, &pa);
    pthread_create(&ct, NULL, spsc_cons, &ca);

    pthread_join(pt, NULL);
    pthread_join(ct, NULL);

    assert(pa.count == ca.count);
    assert(spsc_queue_size(q) == 0);

    spsc_queue_destroy(q);
}

/* ── Benchmarks ── */

static void bench_mpmc_throughput(void)
{
    mpmc_queue_t *q = mpmc_queue_create(1 << 16);
    struct timespec s, e;
    void *data;
    size_t ops = 500000;

    clock_gettime(CLOCK_MONOTONIC, &s);
    for (size_t i = 0; i < ops; i++) {
        mpmc_enqueue(q, (void*)(uintptr_t)(i+1));
        mpmc_dequeue(q, &data);
    }
    clock_gettime(CLOCK_MONOTONIC, &e);

    printf("\n  Benchmarks:\n");
    printf("    MPMC enq+deq pair:  %.1f ns\n", elapsed_ms(&s, &e) * 1e6 / ops);
    mpmc_queue_destroy(q);

    /* SPSC benchmark */
    spsc_queue_t *sq = spsc_queue_create(1 << 16);
    void *sdata;
    clock_gettime(CLOCK_MONOTONIC, &s);
    for (size_t i = 0; i < ops; i++) {
        spsc_enqueue(sq, (void*)(uintptr_t)(i+1));
        spsc_dequeue(sq, &sdata);
    }
    clock_gettime(CLOCK_MONOTONIC, &e);
    printf("    SPSC enq+deq pair:  %.1f ns\n", elapsed_ms(&s, &e) * 1e6 / ops);
    spsc_queue_destroy(sq);
}

int main(void)
{
    printf("libconcurrent — unified test suite\n");
    printf("===================================\n\n");

    printf("Epoch-Based Reclamation:\n");
    TEST(test_ebr_basic);

    printf("\nBounded MPMC Queue (Vyukov):\n");
    TEST(test_mpmc_fifo);
    TEST(test_mpmc_full_empty);
    TEST(test_mpmc_mt);

    printf("\nUnbounded M&S Queue:\n");
    TEST(test_ms_fifo);
    TEST(test_ms_mt);

    printf("\nSPSC Queue (io_uring-style):\n");
    TEST(test_spsc_fifo);
    TEST(test_spsc_full_empty);
    TEST(test_spsc_mt);

    printf("\nLock-Free Hash Map:\n");
    TEST(test_hashmap_basic);
    TEST(test_hashmap_mt);

    bench_mpmc_throughput();

    printf("\n===================================\n");
    printf("%d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
