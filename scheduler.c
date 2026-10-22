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


/* ------------------------------------------------------------------ */
/* DATA: what a thread is, and the scheduler's global state            */
/* ------------------------------------------------------------------ */

/* The four states a thread moves through during its life. */
enum status {
	INIT,
	RUNNING,
	RUNNABLE,
	TERMINATED
};

/* Everything we need to know about one thread. */
struct thread {
	enum status status;
	struct {
		void *mem;		/* malloc pointer, used with free */
		void *sp;		/* page-aligned top of the stack */
	} stack;			/* each thread has its own stack */
	
	struct {
		scheduler_fnc_t function;	/* what the thread runs */
		void *argument;			/* passed to that function */
	} code;
	jmp_buf ctx;			/* saved registers while paused */
	struct thread *link;		/* next thread in the list */
};

static struct thread *root;	/* head of the linked list of threads */
static struct thread *curr;	/* the thread running right now */
static jmp_buf ctx;		/* the scheduler's "home" jump point */
static sigset_t alarm_set;	/* just {SIGALRM}, used to block/unblock it */

/* ------------------------------------------------------------------ */
/* ALARM MASKING: keep the timer out of delicate code                  */
/* ------------------------------------------------------------------ */

/*
 * A timer tick in the middle of a context switch would corrupt the saved
 * state. So the alarm is blocked while the scheduler works, and unblocked
 * only while a user thread is running.
 */

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

/* ------------------------------------------------------------------ */
/* THREAD START: the first function that runs on a new thread's stack  */
/* ------------------------------------------------------------------ */

/*
 * Runs the user's function, then marks the thread TERMINATED and jumps
 * home. It never returns, because a brand new stack has no caller to
 * return to.
 */
static void
thread_start(void)
{
	unblock_alarm();		/* a user thread may be preempted */
	curr->code.function(curr->code.argument);	/* run the user code */
	curr->status = TERMINATED;	/* user code returned: thread is done */
	block_alarm();
	longjmp(ctx, 1);		/* jump home, pick someone else */
}

/* ------------------------------------------------------------------ */
/* PICKING THE NEXT THREAD: round robin                                */
/* ------------------------------------------------------------------ */

/*
 * Walk the list starting just after curr, wrapping around to the head.
 * Return the first thread that can run (INIT or RUNNABLE). Return NULL
 * if every thread is TERMINATED, which means we are finished.
 */

/* First RUNNABLE or INIT thread after curr, wrapping to root. */
static struct thread *
thread_candidate(void)
{
	struct thread *start;
	struct thread *t;

	/* Start after the current thread, or at the head if there is none. */
	if (curr && curr->link) {
		start = curr->link;
	} else {
		start = root;
	}
	t = start;
	/* Loop around the list once, stopping if we get back to the start. */
	do {
		if (t->status == INIT || t->status == RUNNABLE) {
			return t;
		}
		if (t->link) {
			t = t->link;
		} else {
			t = root;	/* wrap around to the head */
		}
	} while (t != start);
	return NULL;			/* nothing left to run */
}

/* ------------------------------------------------------------------ */
/* SCHEDULE: choose a thread and switch to it                          */
/* ------------------------------------------------------------------ */

static void
schedule(void)
{
	struct thread *t;
	void *sp;
	void (*start)(void);

	t = thread_candidate();
	if (!t) {
		return;		/* no threads left, back to scheduler_execute */
	}
	curr = t;
	if (t->status == RUNNABLE) {
		/* Resume a paused thread. */
		t->status = RUNNING;
		longjmp(t->ctx, 1);
	}

	/* First run of a new thread: switch to its own stack and start it. */
	t->status = RUNNING;
	sp = t->stack.sp;
	start = thread_start;
	__asm__ __volatile__ (
		"movq %[sp], %%rsp\n\t"		/* use the thread's stack */
		"call *%[fn]\n\t"		/* call thread_start() on it */
		:
		: [sp] "r" (sp), [fn] "r" (start)
		: "memory"
	);
}

/* ------------------------------------------------------------------ */
/* PREEMPTION: the 1-second timer (extra credit)                       */
/* ------------------------------------------------------------------ */

/*
 * Every second the kernel sends SIGALRM. The handler re-arms the timer
 * and yields for whatever thread was running. That thread never called
 * scheduler_yield() itself, so it does not know it was paused.
 */
static void
on_alarm(int signo)
{
	(void)signo;
	alarm(1);		/* ask for the next tick in 1 second */
	scheduler_yield();	/* force the running thread to give up the CPU */
}

/* Install the SIGALRM handler and start the timer. */
static void
start_preemption(void)
{
	struct sigaction sa;

	sigemptyset(&alarm_set);
	sigaddset(&alarm_set, SIGALRM);
	block_alarm();		/* stay blocked until a user thread starts */
	memset(&sa, 0, sizeof (sa));
	sa.sa_handler = on_alarm;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, NULL);
#ifdef PREEMPT		/* only the extra credit build starts the timer */
	alarm(1);
#endif
}

/* Cancel the timer and ignore any leftover SIGALRM. */
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

/* ------------------------------------------------------------------ */
/* CLEANUP: free everything when all threads are done                  */
/* ------------------------------------------------------------------ */

static void
scheduler_destroy(void)
{
	struct thread *t;
	struct thread *n;

	t = root;
	while (t) {
		n = t->link;
		FREE(t->stack.mem);	/* the thread's stack */
		FREE(t);		/* the thread struct */
		t = n;
	}
	root = NULL;
	curr = NULL;
}

/* ------------------------------------------------------------------ */
/* STACK ALLOCATION: give each thread its own 64 KB stack              */
/* ------------------------------------------------------------------ */

static void
alloc_stack(struct thread *t)
{
	size_t pagesz;
	size_t npages;
	void *mem;
	void *aligned;

	pagesz = page_size();
	npages = (64 * 1024) / pagesz;		/* 64 KB worth of pages */
	/* One extra page so aligning still leaves npages pages. */
	mem = malloc((npages + 1) * pagesz);
	aligned = memory_align(mem, pagesz);
	t->stack.mem = mem;			/* keep the original for free() */
	/* Stacks grow downward, so start at the top (the high address). */
	t->stack.sp = (char *)aligned + npages * pagesz;
}

/* ------------------------------------------------------------------ */
/* PUBLIC API                                                          */
/* ------------------------------------------------------------------ */

/*
 * scheduler_create: make a new thread in the INIT state.
 * It does not run yet. It is added at the FRONT of the list, so the most
 * recently created thread runs first.
 */
int
scheduler_create(scheduler_fnc_t fn, void *arg)
{
	struct thread *t;

	t = malloc(sizeof (*t));
	memset(t, 0, sizeof (*t));
	t->status = INIT;
	t->code.function = fn;
	t->code.argument = arg;
	alloc_stack(t);
	t->link = root;		/* new thread points at the old head... */
	root = t;		/* ...and becomes the new head */
	return 0;
}

/*
 * scheduler_yield: pause this thread and hand the CPU back to the scheduler.
 * Called by a thread directly, or by on_alarm() when the timer fires.
 */
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
		/* Going to sleep: remember where we are, then jump home. */
		curr->status = RUNNABLE;
		longjmp(ctx, 1);
	}
	/* Woken up again: we only get here via longjmp(t->ctx) in schedule(). */
	unblock_alarm();
}

/*
 * scheduler_execute: run all created threads until every one has finished.
 * main() calls this once after creating its threads.
 */
void
scheduler_execute(void)
{
	if (!root) {
		return;		/* no threads, nothing to do (the only NULL check kept) */
	}
	start_preemption();	/* arm the 1-second timer */
	setjmp(ctx);		/* "home": every thread longjmps back to here */
	schedule();		/* pick a thread and run it. Does not return
				   until no runnable threads are left. */
	/* Every thread has finished, so shut everything down. */
	stop_preemption();
	scheduler_destroy();
	unblock_alarm();
}