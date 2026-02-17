# libconcurrent

A collection of lock-free concurrent data structures in C11, sharing a unified
epoch-based memory reclamation system.

**Author:** G.H. Murray

## Components

| Structure | Algorithm | Properties |
|-----------|-----------|------------|
| **Bounded MPMC Queue** | Vyukov ring buffer | Fixed-capacity, 1 CAS/op, ~7 ns/pair |
| **Unbounded MPMC Queue** | Michael & Scott (1996) | Linked-list, sentinel, EBR-safe |
| **Lock-Free Hash Map** | Split-ordered lists (Shalev & Shavit) | Amortized resize, Harris deletion |
| **Epoch-Based Reclamation** | Fraser (2004) | 3-epoch, per-thread retire lists |

## Why a Unified Library?

Each structure uses the same EBR system for safe memory reclamation. No duplicated
code, consistent API patterns, composable pieces.

## Building

```bash
make        # Build test binary
make run    # Build and run all tests
```

Requires: GCC (C11), pthreads. Tested on ARM64 Linux.

## Quick Start

```c
#include "mpmc_queue.h"
#include "hashmap.h"

// Bounded queue — no registration needed
mpmc_queue_t *q = mpmc_queue_create(1024);
mpmc_enqueue(q, data);
mpmc_dequeue(q, &out);

// Hash map — register thread for EBR
hashmap_t *map = hashmap_create();
int slot = hashmap_thread_register(map);
hashmap_put(map, key, value);
void *v = hashmap_get(map, key);
hashmap_thread_unregister(map, slot);
```

## Performance (ARM64, Apple M-series via Docker)

| Benchmark | Result |
|-----------|--------|
| MPMC enq+deq pair (1 thread) | 7 ns |
| MPMC 8P+8C, 1.6M ops | 98 ns/op |
| M&S queue 4P+4C, 400K ops | 148 ns/op |
| Hash map 4T, 60K ops (put+get+remove) | ~22 µs/op |

## References

- Vyukov, "Bounded MPMC Queue" (1024cores.net)
- Michael & Scott, "Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms" (PODC 1996)
- Shalev & Shavit, "Split-Ordered Lists: Lock-Free Extensible Hash Tables" (JACM 2006)
- Harris, "A Pragmatic Implementation of Non-Blocking Linked-Lists" (DISC 2001)
- Fraser, "Practical Lock-Freedom" (PhD thesis, Cambridge, 2004)

## License

MIT
