CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -O2 -g
LDFLAGS = -lpthread
BUILD   = build

SRCS    = src/epoch.c src/mpmc_queue.c src/ms_queue.c src/spsc_queue.c src/hashmap.c

all: $(BUILD)/test

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test: $(SRCS) test/test_all.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

run: $(BUILD)/test
	./$(BUILD)/test

clean:
	rm -rf $(BUILD)

.PHONY: all clean run
