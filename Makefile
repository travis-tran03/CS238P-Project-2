#
# Tony Givargis
# Copyright (C), 2023-2026
# University of California, Irvine
#
# CS 238P - Operating Systems
# Makefile
#

CC     = gcc
CFLAGS = -ansi -pedantic -Wall -Wextra -Werror -Wfatal-errors -fpic -O3
LDLIBS =
DEST   = cs238
SRCS  := $(wildcard *.c)
OBJS  := $(SRCS:.c=.o)

all: $(OBJS)
	@echo "[LN]" $(DEST)
	@$(CC) -o $(DEST) $(OBJS) $(LDLIBS)

%.o: %.c
	@echo "[CC]" $<
	@$(CC) $(CFLAGS) -c $<
	@$(CC) $(CFLAGS) -MM $< > $*.d

# tests/ is not part of $(wildcard *.c), so this does not change ./cs238.
test_sched: scheduler.c system.c tests/test_sched.c
	@echo "[CC]" test_sched
	@$(CC) $(CFLAGS) -I. -o test_sched scheduler.c system.c tests/test_sched.c $(LDLIBS)

test: test_sched
	./test_sched

.PHONY: test

clean:
	@rm -f $(DEST) test_sched *.so *.o *.d *~ *#

-include $(OBJS:.o=.d)
