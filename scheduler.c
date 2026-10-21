/**
 * Tony Givargis
 * Copyright (C), 2023-2026
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * scheduler.c
 */

#undef _FORTIFY_SOURCE

#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include "system.h"
#include "scheduler.h"

#define NUM_PAGES 100
/**
 * Needs:
 *   setjmp()
 *   longjmp()
 */

/* research the above Needed API and design accordingly */

struct thread {
    enum {
        INIT, 
        RUNNING, 
        RUNNABLE, 
        TERMINATED
    } status;
    struct {
        void *mem;
        void *sp;
    } stack;
    struct {
        scheduler_fnc_t fnc;
        void *arg;
    } code;
    jmp_buf thread_ctx;
    struct thread *link;
};

static struct thread *root = NULL;
static struct thread *curr = NULL;
static jmp_buf global_ctx; 

static void destroy(void);
static struct thread *thread_candidate(void);
static void schedule(void);

static void destroy (void){
    struct thread *t;
    while (root){
        t = root;
        root = root->link;
        FREE(t->stack.mem);
        FREE(t);
    }
    return;
}

static struct thread *thread_candidate(void){

    struct thread *rr_helper;

    rr_helper = curr ? curr->link : root;

    while (rr_helper){
        if (INIT == rr_helper->status || RUNNABLE == rr_helper->status){
            return rr_helper;
        }
        rr_helper = rr_helper->link;
    }
    rr_helper = root;
    while (rr_helper){
        if (INIT == rr_helper->status || RUNNABLE == rr_helper->status){
            return rr_helper;
        }
        rr_helper = rr_helper->link;
    }

    return NULL;
}

static void schedule(void){
    void *sp;
    struct thread *cand = thread_candidate();
    if (!cand){return;}
    curr = cand;

    if (RUNNABLE == cand->status){
        cand->status = RUNNING;
        longjmp(cand->thread_ctx, 1);
    }
    else if(INIT == cand->status){
        cand->status = RUNNING;
        sp = cand->stack.sp;
        __asm__ volatile ("mov sp, %[rs]" : : [rs] "r" (sp) : "memory"); /* Im on arm64 architecture*/
        curr->code.fnc(curr->code.arg);
        curr->status = TERMINATED;
        longjmp(global_ctx, 1);
    }
}

int scheduler_create(scheduler_fnc_t fnc, void *arg){
    struct thread *t;

    t = malloc(sizeof(struct thread));
    if (NULL == t){
        TRACE("malloc NULL in scheduler_create - THREAD");
        return -1;
    }

    t->status = INIT;

    t->code.fnc = fnc;
    t->code.arg = arg;

    t->stack.mem = malloc((NUM_PAGES + 1) * page_size());
    if (NULL == t->stack.mem){
        TRACE("malloc NULL in scheduler_create - MEM");
        FREE(t);
        return -1;
    }
    t->stack.sp = (char *)memory_align(t->stack.mem, page_size()) + (NUM_PAGES * page_size());

    t->link = root;
    root = t;

    return 0;
}

void scheduler_execute(void){
    setjmp(global_ctx);
    schedule();
    destroy();
    curr = NULL;
}

void scheduler_yield(void){

    if (setjmp(curr->thread_ctx) == 0) {
        curr->status = RUNNABLE;
        longjmp(global_ctx, 1);
    }
    return;
}
