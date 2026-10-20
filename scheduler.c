/**
 * Tony Givargis
 * Copyright (C), 2023-2026
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * scheduler.c
 */

#undef _FORTIFY_SOURCE
#define _GNU_SOURCE

#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include "system.h"
#include "scheduler.h"

/**
 * Needs:
 *   setjmp()
 *   longjmp()
 */

/* research the above Needed API and design accordingly */


enum status {
	INIT,
	RUNNING,
	RUNNABLE,
	TERMINATED
};

struct thread {
	enum status status;
	struct {
		void *mem;		/* malloc pointer, used with free */
		void *sp;		/* page-aligned top of the stack */
	} stack;
	struct {
		scheduler_fnc_t function;
		void *argument;
	} code;
	jmp_buf ctx;
	struct thread *link;
};

static struct thread *root;
static struct thread *curr;
static jmp_buf ctx;
static sigset_t alarm_set;

static void
block_alarm(void)
{
	sigprocmask(SIG_BLOCK, &alarm_set, NULL);
}

static void
unblock_alarm(void)
{
	sigprocmask(SIG_UNBLOCK, &alarm_set, NULL);
}

static void
thread_start(void)
{
	unblock_alarm();
	curr->code.function(curr->code.argument);
	curr->status = TERMINATED;
	block_alarm();
	longjmp(ctx, 1);
}

/* First RUNNABLE or INIT thread after curr, wrapping to root. */
static struct thread *
thread_candidate(void)
{
	struct thread *start;
	struct thread *t;

	if (!root) {
		return NULL;
	}
	if (curr && curr->link) {
		start = curr->link;
	} else {
		start = root;
	}
	t = start;
	do {
		if (t->status == INIT || t->status == RUNNABLE) {
			return t;
		}
		if (t->link) {
			t = t->link;
		} else {
			t = root;
		}
	} while (t != start);
	return NULL;
}

static void
schedule(void)
{
	struct thread *t;
	void *sp;
	void (*start)(void);

	t = thread_candidate();
	if (!t) {
		return;
	}
	curr = t;
	if (t->status == RUNNABLE) {
		t->status = RUNNING;
		longjmp(t->ctx, 1);
	}

	t->status = RUNNING;
	sp = t->stack.sp;
	start = thread_start;
	__asm__ __volatile__ (
		"movq %[sp], %%rsp\n\t"
		"call *%[fn]\n\t"
		:
		: [sp] "r" (sp), [fn] "r" (start)
		: "memory"
	);
}

static void
on_alarm(int signo)
{
	(void)signo;
	alarm(1);
	scheduler_yield();
}

static void
start_preemption(void)
{
	struct sigaction sa;

	sigemptyset(&alarm_set);
	sigaddset(&alarm_set, SIGALRM);
	block_alarm();
	memset(&sa, 0, sizeof (sa));
	sa.sa_handler = on_alarm;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, NULL);
	alarm(1);
}

static void
stop_preemption(void)
{
	struct sigaction sa;

	alarm(0);
	memset(&sa, 0, sizeof (sa));
	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, NULL);
}

static void
scheduler_destroy(void)
{
	struct thread *t;
	struct thread *n;

	t = root;
	while (t) {
		n = t->link;
		FREE(t->stack.mem);
		FREE(t);
		t = n;
	}
	root = NULL;
	curr = NULL;
}

static int
alloc_stack(struct thread *t)
{
	size_t pagesz;
	size_t npages;
	void *mem;
	void *aligned;

	pagesz = page_size();
	npages = (64 * 1024) / pagesz;
	if (npages < 1) {
		npages = 1;
	}
	/* One extra page so aligning still leaves npages pages. */
	mem = malloc((npages + 1) * pagesz);
	if (!mem) {
		return -1;
	}
	aligned = memory_align(mem, pagesz);
	t->stack.mem = mem;
	t->stack.sp = (char *)aligned + npages * pagesz;
	return 0;
}

int
scheduler_create(scheduler_fnc_t fn, void *arg)
{
	struct thread *t;

	if (!fn) {
		return -1;
	}
	t = malloc(sizeof (*t));
	if (!t) {
		return -1;
	}
	memset(t, 0, sizeof (*t));
	t->status = INIT;
	t->code.function = fn;
	t->code.argument = arg;
	if (alloc_stack(t)) {
		FREE(t);
		return -1;
	}
	t->link = root;
	root = t;
	return 0;
}

void
scheduler_yield(void)
{
	/*
	 * setjmp returns 0 the first time: this thread is stopping.
	 * It returns 1 when schedule() longjmp's back: keep running.
	 * Block the alarm before leaving so a tick cannot hit schedule().
	 */
	block_alarm();
	if (setjmp(curr->ctx) == 0) {
		curr->status = RUNNABLE;
		longjmp(ctx, 1);
	}
	unblock_alarm();
}

void
scheduler_execute(void)
{
	if (!root) {
		return;
	}
	start_preemption();
	setjmp(ctx);
	schedule();
	stop_preemption();
	scheduler_destroy();
	unblock_alarm();
}
