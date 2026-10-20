/**
 * Scheduler tests.
 *
 *   make test
 *
 * One test at a time, after `make test` has built ./test_sched:
 *   ./test_sched rr
 *   ./test_sched sleep
 *
 * Names: all, empty, only, rr, life, stack, many, sleep, cpu, twice
 */

#include "system.h"
#include "scheduler.h"

static int failures;

static void
pass_fail(const char *name, int ok)
{
	if (ok) {
		printf("PASS %s\n", name);
	} else {
		printf("FAIL %s\n", name);
		failures++;
	}
	fflush(stdout);
}

/* scheduler_execute() with no threads returns. */

static void
test_empty(void)
{
	scheduler_execute();
	pass_fail("empty", 1);
}

/* One thread can yield and still finish. */

static int only_n;

static void
only_thread(void *arg)
{
	int i;

	UNUSED(arg);
	for (i = 0; i < 3; ++i) {
		only_n++;
		scheduler_yield();
	}
}

static void
test_only(void)
{
	int ok;

	only_n = 0;
	ok = scheduler_create(only_thread, 0) == 0;
	if (ok) {
		scheduler_execute();
	}
	pass_fail("only", ok && only_n == 3);
}

/*
 * Three threads, each yielding three times. With no sleeping this finishes
 * long before the one-second alarm, so the order is pure round-robin.
 * create() inserts at the front, so the newest thread runs first: c, b, a.
 */

static char rr_log[32];
static int rr_n;

static void
rr_thread(void *arg)
{
	int i;
	char name;

	name = *(const char *)arg;
	for (i = 0; i < 3; ++i) {
		rr_log[rr_n++] = name;
		scheduler_yield();
	}
}

static void
test_rr(void)
{
	int ok;

	rr_n = 0;
	memset(rr_log, 0, sizeof (rr_log));
	ok = scheduler_create(rr_thread, "a") == 0 &&
		scheduler_create(rr_thread, "b") == 0 &&
		scheduler_create(rr_thread, "c") == 0;
	if (ok) {
		scheduler_execute();
		rr_log[rr_n] = 0;
		ok = strcmp(rr_log, "cbacbacba") == 0;
	}
	if (!ok) {
		printf("  log \"%s\"\n", rr_log);
	}
	pass_fail("rr", ok);
}

/* A thread that returns immediately must not stop the others. */

static int short_ran;
static int long_hits;

static void
short_thread(void *arg)
{
	UNUSED(arg);
	short_ran++;
}

static void
long_thread(void *arg)
{
	int i;

	UNUSED(arg);
	for (i = 0; i < 5; ++i) {
		long_hits++;
		scheduler_yield();
	}
}

static void
test_life(void)
{
	int ok;

	short_ran = 0;
	long_hits = 0;
	ok = scheduler_create(short_thread, 0) == 0 &&
		scheduler_create(long_thread, 0) == 0 &&
		scheduler_create(long_thread, 0) == 0;
	if (ok) {
		scheduler_execute();
		ok = short_ran == 1 && long_hits == 10;
	}
	if (!ok) {
		printf("  short %d long %d\n", short_ran, long_hits);
	}
	pass_fail("life", ok);
}

/* Locals on one thread's stack must survive while the other thread runs. */

static int stack_bad;

static void
stack_thread(void *arg)
{
	char marker;
	char buf[8192];
	int i;

	marker = *(const char *)arg;
	memset(buf, marker, sizeof (buf));
	for (i = 0; i < 30; ++i) {
		scheduler_yield();
		if (buf[0] != marker || buf[sizeof (buf) - 1] != marker) {
			stack_bad++;
			return;
		}
	}
}

static void
test_stack(void)
{
	int ok;

	stack_bad = 0;
	ok = scheduler_create(stack_thread, "A") == 0 &&
		scheduler_create(stack_thread, "B") == 0;
	if (ok) {
		scheduler_execute();
		ok = stack_bad == 0;
	}
	pass_fail("stack", ok);
}

/* More threads than the demo, all of them making progress. */

static int many_n;

static void
many_thread(void *arg)
{
	int i;

	UNUSED(arg);
	for (i = 0; i < 5; ++i) {
		many_n++;
		scheduler_yield();
	}
}

static void
test_many(void)
{
	int i;
	int ok;

	many_n = 0;
	ok = 1;
	for (i = 0; i < 40; ++i) {
		if (scheduler_create(many_thread, 0)) {
			ok = 0;
			break;
		}
	}
	if (ok) {
		scheduler_execute();
		ok = many_n == 200;
	}
	if (!ok) {
		printf("  many_n %d\n", many_n);
	}
	pass_fail("many", ok);
}

/*
 * Extra credit. These threads never call scheduler_yield(). The alarm has
 * to switch them. Each test waits a few seconds.
 */

static volatile int other_ran;
static volatile int seen_other;
static volatile unsigned long steps;
static volatile unsigned long other_at;
static volatile unsigned long steps_end;

static void
greedy_sleep(void *arg)
{
	int i;

	UNUSED(arg);
	for (i = 0; i < 3; ++i) {
		us_sleep(1000000);
	}
	seen_other = other_ran;
}

static void
other_thread(void *arg)
{
	UNUSED(arg);
	other_at = steps;
	other_ran = 1;
}

static void
test_sleep(void)
{
	int ok;

	printf("sleep (about 3 seconds, no yield)\n");
	fflush(stdout);
	other_ran = 0;
	seen_other = 0;
	/* Newest thread is the head of the list, so the greedy one runs first. */
	ok = scheduler_create(other_thread, 0) == 0 &&
		scheduler_create(greedy_sleep, 0) == 0;
	if (ok) {
		scheduler_execute();
		ok = seen_other == 1;
	}
	pass_fail("sleep", ok);
}

static void
greedy_cpu(void *arg)
{
	clock_t start;

	UNUSED(arg);
	start = clock();
	/*
	 * clock() every pass keeps this on the CPU for about three seconds,
	 * long enough for more than one alarm tick.
	 */
	while (steps < 100000000UL &&
		(clock() - start) < 3 * CLOCKS_PER_SEC) {
		steps++;
	}
	steps_end = steps;
}

static void
test_cpu(void)
{
	int ok;
	clock_t start;

	printf("cpu (about 3 seconds, no yield)\n");
	fflush(stdout);
	other_ran = 0;
	steps = 0;
	other_at = 0;
	steps_end = 0;
	start = clock();
	ok = start != (clock_t)-1 &&
		scheduler_create(other_thread, 0) == 0 &&
		scheduler_create(greedy_cpu, 0) == 0;
	if (ok) {
		scheduler_execute();
		ok = other_at > 0 && other_at < steps_end;
	}
	if (!ok) {
		printf("  other_at %lu steps_end %lu\n", other_at, steps_end);
	}
	pass_fail("cpu", ok);
}

/*
 * After execute returns, a leftover alarm must not fire into a freed
 * thread. Then a second batch of threads has to run.
 */

static int twice_n;

static void
twice_thread(void *arg)
{
	UNUSED(arg);
	twice_n++;
	scheduler_yield();
	twice_n++;
}

static void
test_twice(void)
{
	int ok;

	printf("twice (about 2 seconds)\n");
	fflush(stdout);
	twice_n = 0;
	ok = scheduler_create(twice_thread, 0) == 0;
	if (ok) {
		scheduler_execute();
		ok = twice_n == 2;
	}
	us_sleep(1500000);
	if (ok) {
		ok = scheduler_create(twice_thread, 0) == 0;
	}
	if (ok) {
		scheduler_execute();
		ok = twice_n == 4;
	}
	if (!ok) {
		printf("  twice_n %d\n", twice_n);
	}
	pass_fail("twice", ok);
}

static void
usage(void)
{
	printf("usage: ./test_sched [all|empty|only|rr|life|stack|many|sleep|cpu|twice]\n");
}

int
main(int argc, char *argv[])
{
	const char *mode;

	mode = argc > 1 ? argv[1] : "all";
	if (!strcmp(mode, "all")) {
		test_empty();
		test_only();
		test_rr();
		test_life();
		test_stack();
		test_many();
		test_sleep();
		test_cpu();
		test_twice();
	} else if (!strcmp(mode, "empty")) {
		test_empty();
	} else if (!strcmp(mode, "only")) {
		test_only();
	} else if (!strcmp(mode, "rr")) {
		test_rr();
	} else if (!strcmp(mode, "life")) {
		test_life();
	} else if (!strcmp(mode, "stack")) {
		test_stack();
	} else if (!strcmp(mode, "many")) {
		test_many();
	} else if (!strcmp(mode, "sleep")) {
		test_sleep();
	} else if (!strcmp(mode, "cpu")) {
		test_cpu();
	} else if (!strcmp(mode, "twice")) {
		test_twice();
	} else {
		usage();
		return 1;
	}
	printf("%d failed\n", failures);
	return failures ? 1 : 0;
}
