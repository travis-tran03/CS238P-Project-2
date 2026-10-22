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
# -DPREEMPT turns on the 1-second timer (the sleep, cpu and twice tests need it).
test_sched: scheduler.c system.c tests/test_sched.c
	@echo "[CC]" test_sched
	@$(CC) $(CFLAGS) -DPREEMPT -I. -o test_sched scheduler.c system.c tests/test_sched.c $(LDLIBS)

test: test_sched
	./test_sched

# demo/ is not part of $(wildcard *.c), so this does not change ./cs238.
# -DPREEMPT turns on the 1-second timer.
extra_credit: scheduler.c system.c demo/extra_credit.c
	@echo "[CC]" extra_credit
	@$(CC) $(CFLAGS) -DPREEMPT -I. -o extra_credit scheduler.c system.c demo/extra_credit.c $(LDLIBS)
	./extra_credit

.PHONY: test extra_credit

clean:
	@rm -f $(DEST) test_sched extra_credit *.so *.o *.d *~ *#

-include $(OBJS:.o=.d)