#ifndef LINKEDLIST_H
#define LINKEDLIST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CACHE_SIZE
#define CACHE_SIZE 8
#endif
// CACHE_SIZE must be a power of 2 so that (vpos & (CACHE_SIZE-1)) == (vpos % CACHE_SIZE).
// The bitwise form is a single AND instruction vs. an integer division.
_Static_assert((CACHE_SIZE & (CACHE_SIZE - 1)) == 0,
               "CACHE_SIZE must be a power of 2 (4, 8, 16, 32, 64, …)");

// Minimum physical position worth caching (position 0 is always the head).
#define CACHE_THRESHOLD 1

// ----------------------- Node -----------------------
typedef struct Node {
    int value;
    struct Node *next;
} Node;

// ----------------------- Cache -----------------------
typedef struct {
    unsigned int position;  // Stores the virtual position (= physical_pos + cache->offset)
    Node *behind;
    Node *current;
    unsigned int generation; 
} NodeCacheEntry;

typedef struct {
    NodeCacheEntry entries[CACHE_SIZE];
    unsigned int generation; // Incrementing this invalidates all entries in O(1)
    int offset;              // Virtual offset: virtual_pos = physical_pos + offset
                             // Decrement on push, increment on pop — O(1), no data movement
} ListCache;

// ── Internal cache lookup (shared by .c and inline callers) ──────────────────
#ifndef NODECACHE_DISABLE_POSITIONAL_CACHE
static inline NodeCacheEntry *checkCache(ListCache *cache, int position) {
    if (position < CACHE_THRESHOLD) return NULL;
    unsigned int vpos = (unsigned int)(position + cache->offset);
    unsigned int slot = vpos & (CACHE_SIZE - 1);
    if (cache->entries[slot].generation == cache->generation &&
        cache->entries[slot].position   == vpos) {
        return &cache->entries[slot];
    }
    return NULL;
}
#endif

// ----------------------- List -----------------------
typedef struct {
    Node *head;
    ListCache cache;
} List;

typedef struct { List list; } Stack;
typedef struct { List list; Node *tail; } Deque;

// ----------------------- Function Prototypes -----------------------
// Non-hot-path functions are in linkedlist.c.
// Hot-path node/stack/deque ops are static inline below for zero call overhead.
void  listInsert(List *list, int position, Node *nd);
void  listDelete(List *list, int position);
void  listPrint(List *list);
void  traverseList_pub(List *list, int position, Node **outBehind, Node **outCurrent);

// Internal Cache management
void invalidateCache_pub(ListCache *cache, int mutatedPosition);
void warmCache_pub(ListCache *cache, int position, Node *behind, Node *current);

// O(1) push/pop cache maintenance via offset arithmetic.
// Call cacheOnPush_pub after manually prepending a node to a bare List head.
// Call cacheOnPop_pub  after manually removing the head node of a bare List.
// (The Stack/Deque ops below already handle this internally.)
void cacheOnPush_pub(ListCache *cache);
void cacheOnPop_pub(ListCache *cache);

// ── Inline node creation ──────────────────────────────────────────────────────
// Kept inline so the compiler sees malloc + push as one unit and eliminates
// the call overhead for the DFS/BFS inner loops.
static inline Node *createNode(int val) {
    Node *nd = (Node *)malloc(sizeof(Node));
    if (!nd) return NULL;
    nd->value = val;
    nd->next  = NULL;
    return nd;
}

// ── Stack ops (static inline — zero call overhead on the hot path) ────────────
static inline void push(Stack *s, Node *nd) {
    // Head prepend: all cached entries shift one position deeper via offset (O(1)).
    s->list.cache.offset--;
    nd->next     = s->list.head;
    s->list.head = nd;
}

static inline Node *pop(Stack *s) {
    if (!s->list.head) return NULL;
    // Head removal: generation bump invalidates all cached entries in O(1).
    s->list.cache.generation++;
    Node *nd     = s->list.head;
    s->list.head = nd->next;
    nd->next     = NULL;
    return nd;
}

static inline Node *peek(Stack *s)      { return s->list.head; }
static inline int stackIsEmpty(Stack *s) { return s->list.head == NULL; }

// ── Deque ops (static inline) ─────────────────────────────────────────────────
static inline void enqueueBack(Deque *dq, Node *nd) {
    // Tail append: no existing position changes, so the cache remains valid.
    // Do NOT call invalidateCache — that would destroy entries useful to Trémaux.
    nd->next = NULL;
    if (!dq->list.head) {
        dq->list.head = dq->tail = nd;
    } else {
        dq->tail->next = nd;
        dq->tail       = nd;
    }
}

static inline Node *dequeueFront(Deque *dq) {
    if (!dq->list.head) return NULL;
    // Head removal shifts all positions: bump generation to invalidate.
    dq->list.cache.generation++;
    Node *nd     = dq->list.head;
    dq->list.head = nd->next;
    if (!dq->list.head) dq->tail = NULL;
    nd->next = NULL;
    return nd;
}

static inline int dequeIsEmpty(Deque *dq) { return dq->list.head == NULL; }

// enqueueFront and dequeueBack are infrequently used; they remain in linkedlist.c.
void  enqueueFront(Deque *dq, Node *nd);
Node *dequeueBack(Deque *dq);

// ── Inline fast-path wrapper around traverseList_pub ─────────────────────────
// Checks the direct cache slot first (O(1)); falls back to traverseList_pub
// (which handles the position-1 fast-path and the general O(CACHE_SIZE) floor
// search) only on a miss.  Use this in hot positional-scan loops.
static inline void traverseList_inline(List *list, int position,
                                       Node **outBehind, Node **outCurrent)
{
#ifndef NODECACHE_DISABLE_POSITIONAL_CACHE
    NodeCacheEntry *hit = checkCache(&list->cache, position);
    if (hit) {
        *outBehind  = hit->behind;
        *outCurrent = hit->current;
        return;
    }
#endif
    traverseList_pub(list, position, outBehind, outCurrent);
}

#endif
