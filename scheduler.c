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

/* Room for libc calls, and larger than MINSIGSTKSZ. */
#define THREAD_STACK_SIZE (64 * 1024)

struct thread {
	sigjmp_buf ctx;
	volatile int done;
	scheduler_fnc_t fnc;
	void *arg;
	void *stack_mem;
	struct thread *next;
};

static struct thread *head;
static struct thread *tail;
static struct thread *current;
static struct thread *new_thread;
static sigjmp_buf sched_ctx;
static sigjmp_buf create_ctx;
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

/* Live thread after prev, wrapping around the creation-order list. */
static struct thread *
next_thread(struct thread *prev)
{
	struct thread *start;
	struct thread *t;

	if (!head) {
		return NULL;
	}
	if (prev && prev->next) {
		start = prev->next;
	} else {
		start = head;
	}
	t = start;
	do {
		if (!t->done) {
			return t;
		}
		if (t->next) {
			t = t->next;
		} else {
			t = head;
		}
	} while (t != start);
	return NULL;
}

void
scheduler_yield(void)
{
	/*
	 * Block the tick first so it cannot land halfway through a switch.
	 * Coming back means this thread was chosen again: allow the tick.
	 */
	block_alarm();
	if (sigsetjmp(current->ctx, 0) == 0) {
		siglongjmp(sched_ctx, 1);
	}
	unblock_alarm();
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
thread_trampoline(int signo)
{
	(void)signo;

	/*
	 * Running on the new stack (SA_ONSTACK). The saved context resumes
	 * here, on this stack, when the scheduler first enters the thread.
	 */
	if (sigsetjmp(new_thread->ctx, 0) == 0) {
		/* savesigs was 1 in scheduler_create, so SIGUSR1 turns back on. */
		siglongjmp(create_ctx, 1);
	}

	unblock_alarm();
	current->fnc(current->arg);
	current->done = 1;
	block_alarm();
	siglongjmp(sched_ctx, 1);
}

int
scheduler_create(scheduler_fnc_t fnc, void *arg)
{
	struct thread *t;
	void *mem;
	size_t pagesz;
	stack_t ss;
	stack_t old_ss;
	struct sigaction sa;
	struct sigaction old_sa;

	pagesz = page_size();
	mem = malloc(THREAD_STACK_SIZE + pagesz);
	if (!mem) {
		return -1;
	}
	t = malloc(sizeof (*t));
	if (!t) {
		FREE(mem);
		return -1;
	}
	memset(t, 0, sizeof (*t));
	t->fnc = fnc;
	t->arg = arg;
	t->stack_mem = mem;

	memset(&ss, 0, sizeof (ss));
	ss.ss_sp = memory_align(mem, pagesz);
	ss.ss_size = THREAD_STACK_SIZE;
	if (sigaltstack(&ss, &old_ss)) {
		FREE(t->stack_mem);
		FREE(t);
		return -1;
	}

	memset(&sa, 0, sizeof (sa));
	sa.sa_handler = thread_trampoline;
	sa.sa_flags = SA_ONSTACK;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGUSR1, &sa, &old_sa)) {
		sigaltstack(&old_ss, NULL);
		FREE(t->stack_mem);
		FREE(t);
		return -1;
	}

	new_thread = t;
	if (sigsetjmp(create_ctx, 1) == 0) {
		raise(SIGUSR1);
	}

	sigaction(SIGUSR1, &old_sa, NULL);
	sigaltstack(&old_ss, NULL);

	if (!head) {
		head = t;
	} else {
		tail->next = t;
	}
	tail = t;
	return 0;
}

static void
destroy_threads(void)
{
	struct thread *t;
	struct thread *n;

	t = head;
	while (t) {
		n = t->next;
		FREE(t->stack_mem);
		FREE(t);
		t = n;
	}
	head = NULL;
	tail = NULL;
	current = NULL;
}

void
scheduler_execute(void)
{
	if (!head) {
		return;
	}
	start_preemption();
	current = NULL;
	for (;;) {
		current = next_thread(current);
		if (!current) {
			break;
		}
		if (sigsetjmp(sched_ctx, 0) == 0) {
			siglongjmp(current->ctx, 1);
		}
	}
	stop_preemption();
	destroy_threads();
	unblock_alarm();
}
