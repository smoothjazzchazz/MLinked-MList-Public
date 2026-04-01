#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "maze.h"
#include "tremaux.h"
#include "algorithms_cache.h"
#include "algorithms_sll.h"
#include "algorithms_dll.h"
#include "sll.h"
#include "dll.h"

// ── Fair Benchmarking Protocols ──────────────────────────────────────────────

static void warm_allocator() {
    const int NODES = 100000;
    void **ptrs = malloc(sizeof(void*) * NODES);
    if (!ptrs) return;
    for (int i = 0; i < NODES; i++) {
        ptrs[i] = malloc(24);
        memset(ptrs[i], 0, 24);
    }
    for (int i = 0; i < NODES; i++) free(ptrs[i]);
    free(ptrs);
}

static void flush_cpu_cache() {
    const int SIZE = 16 * 1024 * 1024;
    volatile char *m = (char*)malloc(SIZE);
    if (m) {
        for (int i = 0; i < SIZE; i += 64) m[i] = 1;
        free((void*)m);
    }
}

static double elapsed_ms(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) * 1000.0 + (b.tv_nsec - a.tv_nsec) / 1e6;
}

#ifdef SWEEP
// RUNS=4 matches the 4-algorithm bench4_tremaux rotation, giving each algorithm
// exactly one run in each position (fully balanced for the sweep comparison).
#define RUNS 4
#else
// RUNS=12 is the LCM of 3 and 4, ensuring both bench3 (3 algorithms) and
// bench4_tremaux (4 algorithms) complete full rotation cycles with no position bias.
#define RUNS 12
#endif

typedef struct { double cache_ms, sll_ms, dll_ms; } AlgoResult;
typedef struct { double cache_ms, psll_ms, sll_ms, dll_ms; } Tremaux4Result;

static AlgoResult bench3(
    int (*fy)(const Maze*), int (*fs)(const Maze*), int (*fd)(const Maze*),
    const Maze *m)
{
    AlgoResult r = {0,0,0};
    for (int i = 0; i < RUNS; i++) {
        struct timespec s, e;
        int order = i % 3;

        if (order == 0) {
            flush_cpu_cache(); clock_gettime(CLOCK_MONOTONIC,&s); fy(m); clock_gettime(CLOCK_MONOTONIC,&e); r.cache_ms += elapsed_ms(s,e);
            flush_cpu_cache(); clock_gettime(CLOCK_MONOTONIC,&s); fs(m); clock_gettime(CLOCK_MONOTONIC,&e);   r.sll_ms += elapsed_ms(s,e);
            flush_cpu_cache(); clock_gettime(CLOCK_MONOTONIC,&s); fd(m); clock_gettime(CLOCK_MONOTONIC,&e);   r.dll_ms += elapsed_ms(s,e);
        } else if (order == 1) {
            flush_cpu_cache(); clock_gettime(CLOCK_MONOTONIC,&s); fs(m); clock_gettime(CLOCK_MONOTONIC,&e);   r.sll_ms += elapsed_ms(s,e);
            flush_cpu_cache(); clock_gettime(CLOCK_MONOTONIC,&s); fd(m); clock_gettime(CLOCK_MONOTONIC,&e);   r.dll_ms += elapsed_ms(s,e);
            flush_cpu_cache(); clock_gettime(CLOCK_MONOTONIC,&s); fy(m); clock_gettime(CLOCK_MONOTONIC,&e); r.cache_ms += elapsed_ms(s,e);
        } else {
            flush_cpu_cache(); clock_gettime(CLOCK_MONOTONIC,&s); fd(m); clock_gettime(CLOCK_MONOTONIC,&e);   r.dll_ms += elapsed_ms(s,e);
            flush_cpu_cache(); clock_gettime(CLOCK_MONOTONIC,&s); fy(m); clock_gettime(CLOCK_MONOTONIC,&e); r.cache_ms += elapsed_ms(s,e);
            flush_cpu_cache(); clock_gettime(CLOCK_MONOTONIC,&s); fs(m); clock_gettime(CLOCK_MONOTONIC,&e);   r.sll_ms += elapsed_ms(s,e);
        }
    }
    r.cache_ms /= RUNS; r.sll_ms /= RUNS; r.dll_ms /= RUNS;
    return r;
}

static Tremaux4Result bench4_tremaux(
    int (*fy)(const Maze*), int (*fpsll)(const Maze*), int (*fs)(const Maze*), int (*fd)(const Maze*),
    const Maze *m)
{
    Tremaux4Result r = {0,0,0,0};

    for (int i = 0; i < RUNS; i++) {
        struct timespec s, e;

        // Deterministic 4-way rotation each run for fairness
        int order = i % 4;

        // helper macro to time one call into a field
        #define TIME_CALL(field, fn) do { \
            flush_cpu_cache(); \
            clock_gettime(CLOCK_MONOTONIC,&s); \
            (fn)(m); \
            clock_gettime(CLOCK_MONOTONIC,&e); \
            (field) += elapsed_ms(s,e); \
        } while(0)

        if (order == 0) {
            TIME_CALL(r.cache_ms, fy);
            TIME_CALL(r.psll_ms,  fpsll);
            TIME_CALL(r.sll_ms,     fs);
            TIME_CALL(r.dll_ms,     fd);
        } else if (order == 1) {
            TIME_CALL(r.psll_ms,  fpsll);
            TIME_CALL(r.sll_ms,     fs);
            TIME_CALL(r.dll_ms,     fd);
            TIME_CALL(r.cache_ms,   fy);
        } else if (order == 2) {
            TIME_CALL(r.sll_ms,     fs);
            TIME_CALL(r.dll_ms,     fd);
            TIME_CALL(r.cache_ms,   fy);
            TIME_CALL(r.psll_ms,  fpsll);
        } else {
            TIME_CALL(r.dll_ms,     fd);
            TIME_CALL(r.cache_ms,   fy);
            TIME_CALL(r.psll_ms,  fpsll);
            TIME_CALL(r.sll_ms,     fs);
        }

        #undef TIME_CALL
    }

    r.cache_ms /= RUNS; r.psll_ms /= RUNS; r.sll_ms /= RUNS; r.dll_ms /= RUNS;
    return r;
}

// ── Formatting ───────────────────────────────────────────────────────────────
static void printHeader3(const char *title) {
    printf("\n  === %s\n", title);
    printf("  %-14s │ %9s   │ %9s   │ %9s   │ Place\n",
           "Algorithm", "Node+cache", "SLLNode", "DLLNode");
    printf("  ───────────────┼───────────┼───────────┼───────────┼────────────────────────\n");
}

static void printRow3(const char *name, AlgoResult r) {
    int beats_sll = (r.cache_ms < r.sll_ms);
    int beats_dll = (r.cache_ms < r.dll_ms);
    int rank = 1 + (r.cache_ms > r.sll_ms) + (r.cache_ms > r.dll_ms);

    char place[56];
    if (rank == 1 && beats_sll && beats_dll)
        snprintf(place, sizeof(place), "1st ★ (beats SLLNode & DLLNode)");
    else if (rank == 1)
        snprintf(place, sizeof(place), "1st (tied)");
    else if (rank == 2 && beats_sll)
        snprintf(place, sizeof(place), "2nd  (beats SLLNode)");
    else if (rank == 2 && beats_dll)
        snprintf(place, sizeof(place), "2nd  (beats DLLNode)");
    else if (rank == 2)
        snprintf(place, sizeof(place), "2nd");
    else
        snprintf(place, sizeof(place), "3rd");

    printf("  %-14s │ %8.3fms  │ %8.3fms  │ %8.3fms  │ %s\n",
           name, r.cache_ms, r.sll_ms, r.dll_ms, place);
}

static void printFooter() {
    printf("  ───────────────────────────────────────────────────────────────────────────\n");
}

static void printHeaderTremaux4() {
    printf("\n  Trémaux — cache-isolation breakdown\n");
    printf("  (same Node size across all four; only the cache differs between col 1 and col 2)\n");
    printf("  %-14s │ %-11s │ %-12s │ %-9s │ %-9s\n",
           "Variant", "Node+cache", "Node+nocache", "SLLNode", "DLLNode");
    printf("  ───────────────┼────────────┼─────────────┼───────────┼───────────\n");
}

static void printRowTremaux4(Tremaux4Result r) {
    printf("  %-14s │ %9.3fms  │ %10.3fms  │ %8.3fms  │ %8.3fms\n",
           "Trémaux", r.cache_ms, r.psll_ms, r.sll_ms, r.dll_ms);
}

// ── Runner ───────────────────────────────────────────────────────────────────
static void runTremaux(const char *label, int r, int c, unsigned int seed) {
    Maze *m = maze_create(r, c);
    maze_generate(m, 0, 0, seed);

    printHeader3(label);

#ifndef SWEEP
    printRow3("DFS",        bench3(cache_dfs,   sll_dfs,   dll_dfs,   m));
    printRow3("BFS",        bench3(cache_bfs,   sll_bfs,   dll_bfs,   m));
    printRow3("Greedy BFS", bench3(cache_gbfs,  sll_gbfs,  dll_gbfs,  m));
    printRow3("A*",         bench3(cache_astar, sll_astar, dll_astar, m));
    printFooter();
#endif

    // 4-way Trémaux comparison: isolates the cache contribution.
    // Col 1 (Node+cache) vs Col 2 (Node+nocache) → cache speedup only
    // Col 2 (Node+nocache) vs Col 3 (SLLNode)    → same algorithm, different type
    // Col 3 (SLLNode) vs Col 4 (DLLNode)         → node-size / pointer overhead
    printHeaderTremaux4();
    printRowTremaux4(bench4_tremaux(cache_tremaux, psll_tremaux, sll_tremaux, dll_tremaux, m));
    printFooter();

    maze_free(m);
}
int main(void) {
    warm_allocator();

    unsigned int seed;
#ifdef MAZE_SEED
    seed = (unsigned int)MAZE_SEED;
#else
    seed = (unsigned int)time(NULL);
#endif

    printf("\n");
    printf("  =============================================================================\n");

#ifdef SWEEP
    printf("   CACHE_SIZE SWEEP: Trémaux only, LARGE 500x500, %d runs\n", RUNS);
    printf("   CACHE_SIZE = %d\n", CACHE_SIZE);
#else
    /* Build-accurate headline (avoid misleading “Node+cache” when cache is compiled out) */
#ifdef NODECACHE_DISABLE_POSITIONAL_CACHE
    printf("   MAZE BENCHMARK: Node (cache DISABLED) vs. SLLNode / DLLNode\n");
#elif defined(NODECACHE_DISABLE_FASTPATH)
    printf("   MAZE BENCHMARK: Node+cache (fast-path DISABLED) vs. Node+nocache / SLLNode / DLLNode\n");
#else
    printf("   MAZE BENCHMARK: Node+cache vs. Node+nocache / SLLNode / DLLNode\n");
#endif
    printf("   CACHE_SIZE = %d  |  Cold-Cache Protocol + Interleaved Run Order  |  %d runs\n",
           CACHE_SIZE, RUNS);
#endif

    /* Seed line (reproducibility) */
#ifdef MAZE_SEED
    printf("   MAZE_SEED = %u (fixed via -DMAZE_SEED)\n", seed);
#else
    printf("   MAZE_SEED = %u (from time(NULL))\n", seed);
#endif

    printf("   sizeof(Node)=%zu  sizeof(SLLNode)=%zu  sizeof(DLLNode)=%zu\n",
           sizeof(Node), sizeof(SLLNode), sizeof(DLLNode));

    /* Build notes */
#ifdef NODECACHE_DISABLE_POSITIONAL_CACHE
    printf("   NOTE: NODECACHE_DISABLE_POSITIONAL_CACHE is ENABLED (Node+cache behaves as linear traversal)\n");
#elif defined(NODECACHE_DISABLE_FASTPATH)
    printf("   NOTE: NODECACHE_DISABLE_FASTPATH is ENABLED (cache active; position-1 short-circuit disabled)\n");
#endif


    /* “Winner” legend — keep truthful across build modes */
#ifdef SWEEP
    printf("   1st ★ = Node+cache wins  |  All times are averages over %d interleaved runs\n", RUNS);
#elif defined(NODECACHE_DISABLE_POSITIONAL_CACHE)
    printf("   1st ★ = Node (cache disabled) wins  |  All times are averages over %d interleaved runs\n", RUNS);
#else
    printf("   1st ★ = Node+cache wins  |  All times are averages over %d interleaved runs\n", RUNS);
#endif

    printf("  =============================================================================\n");

#ifdef SWEEP
    runTremaux("LARGE   500x500  (250,000 cells)", 500, 500, seed);
#else
    runTremaux("SMALL   50x50    (2,500 cells)",    50,  50, seed);

    runTremaux("MEDIUM  200x200  (40,000 cells)",  200, 200, seed);
    runTremaux("LARGE   500x500  (250,000 cells)", 500, 500, seed);
#endif

    return 0;
}

