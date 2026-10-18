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

#define num_pages = 100
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
        TERMINATED,
    } status;
    struct {
        void *mem;
        void *sp;
    } stack;
    struct {
        scheduler_fnc_t fnc;
        void *arg;
    } code;
    jmp_buf ctx;
    struct thread *link;
}

struct thread *root = NULL;
struct thread *curr = NULL;
jmp_buf global_ctx; //Inspired from lecture notes, still don't understand

int scheduler_create(fnc, arg){
    struct thread *t;
    t = malloc(sizeof(struct thread));
    t->status = INIT;
    t->code.fnc = fnc;
    t->code.arg = arg;
    t->stack.mem = malloc((num_pages * page_size) + 1);
    t->stack.sp = memory_align(t->stack.mem, P) + (num_pages + page_size);
    t->link = root;
    root = t;
    return 0;
}

void destory (void){
    struct thread *t;
    while (root){
        t = root;
        root = root->link;
        // here I need to free t's mem and sp, not sure exactly how to do that
        FREE(t);
        return;
    }
}

static struct thread *thread_candidate(void){
    //step 1, needs to traverse the linked list, 
    //1.a implement red robin
    //1.b find a RUNNABLE or INIT thread 
    //1.c either return it or return NULL
}

void scheduler_execute(void){
    setjmp(*global_ctx);
    schedule();
    destroy();
}

static void schedule(void){
    struct thread cand = thread_candidate();
    if (!cand){return;}

    // If its INIT, use assemply machine code to stick cand->sp into CPU sp

    // If its RUNANBLE, we just need to long jump to it? 
    // Then set cand->status to RUNNING
    // longjump(cand->ctx; 1) ?????? Not sure
}

void scheduler_yield(void){
    // decide based off of return from setjmp()

    // 0 should be become runnable, jump to scheduler, and longjump global context

    // what do i do if 1? nothing?
}
