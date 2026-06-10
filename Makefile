CC           = gcc
CFLAGS       = -Wall -Wno-unused-function -Wextra -O2 -std=c11
CFLAGS_DEBUG = -Wall -Wno-unused-function -Wextra -g -std=c11

TARGET       = maze_bench
SRCS         = main.c linkedlist.c
OBJS         = $(SRCS:.c=.o)

HDRS         = linkedlist.h maze.h tremaux.h psll.h \
               algorithms_cache.h algorithms_sll.h algorithms_dll.h \
               sll.h dll.h
#must be in powers of 2 or it will not work because computer science
CACHE_SIZES  = 4 8 16 32 64
SWEEP_BIN    = _maze_sweep

SEED ?= 0
CPPFLAGS :=
ifeq ($(SEED),0)
  # no fixed seed; code will fall back to time(NULL)
else
  CPPFLAGS += -DMAZE_SEED=$(SEED)
endif

.PHONY: all debug clean run \
        nocache run_nocache \
        nofastpath run_nofastpath \
        variants sweep alltests

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

debug: CFLAGS = $(CFLAGS_DEBUG)
debug: clean $(TARGET)

run: $(TARGET)

	./$(TARGET)


# ---- Variant builds ----
$("CPPFLAGS")
nocache:
	$(CC) $(CFLAGS) $(CPPFLAGS) -DNODECACHE_DISABLE_POSITIONAL_CACHE -o $(TARGET)_nocache $(SRCS)


run_nocache: nocache
	./$(TARGET)_nocache


nofastpath:
	$(CC) $(CFLAGS) $(CPPFLAGS) -DNODECACHE_DISABLE_FASTPATH -o $(TARGET)_nofastpath $(SRCS)

run_nofastpath: nofastpath
	./$(TARGET)_nofastpath

variants: $(TARGET) nofastpath nocache
	@echo ""
	@echo "  ============================================================================="
	@echo "   VARIANT 1 — Node+cache (full: cache + fast-path)"
	@echo "  ============================================================================="
	./$(TARGET)
	@echo ""

	@echo "  ============================================================================="
	@echo "   VARIANT 2 — Node+cache (cache only, fast-path DISABLED)"
	@echo "  ============================================================================="
	./$(TARGET)_nofastpath
	@echo ""
	@echo "  ============================================================================="
	@echo "   VARIANT 3 — Node (cache disabled)"
	@echo "  ============================================================================="
	./$(TARGET)_nocache

# ---- Cache-size sweep (single-shell-line loop; avoids missing \ issues) ----

sweep:
	@echo ""
	@echo "  ============================================================================="
	@echo "   CACHE_SIZE Sweep -- Tremaux on LARGE 500x500"
	@echo "  ============================================================================="
	@for sz in $(CACHE_SIZES); do echo ""; echo "  ------------------------------ CACHE_SIZE=$$sz ------------------------------"; $(CC) $(CFLAGS) $(CPPFLAGS) -DCACHE_SIZE=$$sz -DSWEEP -o ./$(SWEEP_BIN) $(SRCS) || { echo "  [CACHE_SIZE=$$sz] compile failed"; continue; }; ./$(SWEEP_BIN); done

alltests: clean variants sweep
	@echo ""
	@echo "  ============================================================================="
	@echo "   DONE: variants + sweep complete"
	@echo "  ============================================================================="

clean:
	rm -f $(OBJS) $(TARGET) $(TARGET)_nocache $(TARGET)_nofastpath ./$(SWEEP_BIN)
