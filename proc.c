#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

// added
struct schedlog buf_log[LOGBUFSIZE];
int buf_rest_size = 0;

// added
// get clock
inline struct clock rdtsc(void) {
  /* __asm__ __volatile__("mfence"); */
  unsigned int lo, hi;
  struct clock c;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  c.hi = hi;
  c.lo = lo;
  /* __asm__ __volatile__("mfence"); */
  return c;
}

struct {
  struct spinlock lock;
  struct proc proc[NPROC]; // attention: not sorted by pid
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void pinit(void) {
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int cpuid() {
  return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu *mycpu(void) {
  int apicid, i;

  if (readeflags() & FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc *myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

int mycpuid(void) {
  pushcli();
  int curcpuid = cpuid();
  popcli();
  return curcpuid;
}

struct runqueue {
  int size;
  struct proc head;
  struct spinlock lock;
};

#define MAX_CPU_NUM 100

/* struct runqueue rqtable[MAX_CPU_NUM]; */
struct rqtable {
  struct runqueue runqueue[MAX_CPU_NUM];
  struct spinlock lock; // delete later
};
struct rqtable rqtable;
struct proc head[MAX_CPU_NUM];

void runqueueinit(void) {
  for (int i = 0; i < ncpu; i++) {
    acquire(&rqtable.lock);
    struct runqueue *rq = &rqtable.runqueue[i];
    struct proc *head   = &rq->head;
    head->pid           = (-1) * (i + 1);
    head->next          = head;
    head->prev          = head;
    rq->size            = 0;
    release(&rqtable.lock);
  }
}

void printrunqueue(void) {
  for (int i = 0; i < ncpu; i++) {
    cprintf("runqueue %d : pid ", i + 1);
    acquire(&rqtable.lock);
    struct proc *head = &rqtable.runqueue[i].head;
    struct proc *p    = head->next;
    while (p != head) {
      cprintf("%d, ", p->pid);
      p = p->next;
    }
    cprintf("size %d\n", rqtable.runqueue[i].size);
    release(&rqtable.lock);
  }
}

struct proc *pop_rq(void) {
  printrunqueue();

  cprintf("try to pop()\n");
  acquire(&rqtable.lock);
  struct runqueue *rq = &rqtable.runqueue[mycpuid()];
  /* if (rq->size == 0) */
  /*   panic("pop_rq : empty runqueue"); */
  struct proc *head    = &rq->head;
  struct proc *p_poped = head->next;
  head->next           = p_poped->next;
  p_poped->next->prev  = head;
  p_poped->next        = NULL;
  p_poped->prev        = NULL;
  rq->size--;

  release(&rqtable.lock);
  return p_poped;
}

void push_rq(struct proc *p) {
  // why needed?
  if (p->pid == 2)
    return;

  printrunqueue();

  acquire(&rqtable.lock);
  cprintf("try to push(): pid %d\n", p->pid);

  // panic when same pid is pushed
  struct proc *head1 = &rqtable.runqueue[mycpuid()].head;
  struct proc *p1    = head1->next;
  while (p1 != head1) {
    if (p->pid == p1->pid) {
      cprintf("ppid %d, p1pid %d\n", p->pid, p1->pid);
      panic("push same pid");
      return;
    }
    p1 = p1->next;
  }

  struct runqueue *rq = &rqtable.runqueue[mycpuid()];
  struct proc *head   = &rq->head;
  struct proc *tail   = rq->head.prev;
  rq->size++;
  p->prev    = tail;
  p->next    = head;
  head->prev = p;
  tail->next = p;
  release(&rqtable.lock);
}

// added
// from ulib.c
int mystrcmp(const char *p, const char *q) {
  while (*p && *p == *q)
    p++, q++;
  return (uchar)*p - (uchar)*q;
}

// added
void writelog(int pid, char *pname, char event_name, int prev_pstate,
              int next_pstate) {
  /* if (buf_rest_size > 0) { */
  if (buf_rest_size > 0 && !mystrcmp(pname, "bufwrite")) {
    buf_log[LOGBUFSIZE - buf_rest_size].clock = rdtsc();
    buf_log[LOGBUFSIZE - buf_rest_size].pid   = pid;

    for (int i = 0; i < 16; i++) {
      buf_log[LOGBUFSIZE - buf_rest_size].name[i] = pname[i];
    }

    buf_log[LOGBUFSIZE - buf_rest_size].event_name  = event_name;
    buf_log[LOGBUFSIZE - buf_rest_size].prev_pstate = prev_pstate;
    buf_log[LOGBUFSIZE - buf_rest_size].next_pstate = next_pstate;
    buf_log[LOGBUFSIZE - buf_rest_size].cpu         = mycpuid();

    buf_rest_size--;

    /* if (buf_rest_size == 0) { */
    /*   cprintf("logging finished\n"); */
    /* } */
  }
}

// PAGEBREAK: 32
//  Look in the process table for an UNUSED proc.
//  If found, change state to EMBRYO and initialize
//  state required to run in the kernel.
//  Otherwise return 0.
static struct proc *allocproc(void) {
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->pid = nextpid++;

  // added
  p->priority = MAX_PRIO;

  // added
  writelog(p->pid, p->name, ALLOCPROC, p->state, EMBRYO);
  p->state = EMBRYO;

  release(&ptable.lock);

  // Allocate kernel stack.
  if ((p->kstack = kalloc()) == 0) {
    // added
    // what is it mean??
    /* writelog(p->pid, ALLOCEXIT, p->state, UNUSED); */
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe *)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint *)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context *)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

// PAGEBREAK: 32
//  Set up first user process.
void userinit(void) {
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;
  if ((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs     = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds     = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es     = p->tf->ds;
  p->tf->ss     = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp    = PGSIZE;
  p->tf->eip    = 0; // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  // added
  /* writelog(p->pid, USERINIT, p->state, RUNNABLE); */
  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n) {
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if (n > 0) {
    if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if (n < 0) {
    if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void) {
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0) {
    return -1;
  }

  // Copy process state from proc.
  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0) {
    kfree(np->kstack);
    np->kstack = 0;
    np->state  = UNUSED;
    return -1;
  }
  np->sz     = curproc->sz;
  np->parent = curproc;
  *np->tf    = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for (i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  // enqueue
  push_rq(np);

  acquire(&ptable.lock);

  // added
  writelog(np->pid, np->name, FORK, np->state, RUNNABLE);
  /* writelog(curproc->pid, FORK, curproc->state, RUNNABLE); */
  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void) {
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if (curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++) {
    if (curproc->ofile[fd]) {
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if (p->parent == curproc) {
      p->parent = initproc;
      if (p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // added
  writelog(curproc->pid, curproc->name, EXIT, curproc->state, ZOMBIE);

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(void) {
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for (;;) {
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->parent != curproc)
        continue;
      havekids = 1;
      if (p->state == ZOMBIE) {
        // Found one.

        // dequeue
        pop_rq();

        // added
        // Is it OK to place here?
        writelog(p->pid, p->name, WAIT, p->state, UNUSED);

        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid     = 0;
        p->parent  = 0;
        p->name[0] = 0;
        p->killed  = 0;
        p->state   = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || curproc->killed) {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); // DOC: wait-sleep
  }
}

// added
void boost_prio(void) {
  struct proc *p;

  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    p->priority = MAX_PRIO;
  }

  release(&ptable.lock);
}

// PAGEBREAK: 42
//  Per-CPU process scheduler.
//  Each CPU calls scheduler() after setting itself up.
//  Scheduler never returns.  It loops, doing:
//   - choose a process to run
//   - swtch to start running that process
//   - eventually that process transfers control
//       via swtch back to the scheduler.
void scheduler(void) {
  struct proc *p;
  struct cpu *c = mycpu();
  // ???
  c->proc = 0;
  /* int is_first = 1; // added */

  for (;;) {
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

    // MLFQ-like scheduler
    if (IS_MLFQ) {
      int sched_idx = 0; // added
      int is_found  = 0;
      for (int search_prio = MAX_PRIO; search_prio >= 0; search_prio--) {
        // search next process
        for (int i = 0; i < NPROC; i++) {
          p = &ptable.proc[sched_idx];

          is_found = 0;
          if (p->state == RUNNABLE && p->priority == search_prio) {
            is_found = 1;
            c->proc  = p;
            switchuvm(p);
            writelog(p->pid, p->name, TICK, p->state, RUNNING);
            p->state = RUNNING;
            swtch(&(c->scheduler), p->context);
            switchkvm(); // resume from here
            c->proc = 0; // ???
          } else {
            if (sched_idx == NPROC - 1) {
              sched_idx = 0;
            } else {
              sched_idx++;
            }
          }
          if (is_found)
            break;
        }
        if (is_found)
          break;
      }
    }

    // default round robin scheduler
    else {
      for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->state == RUNNABLE) {
          c->proc = p;
          switchuvm(p);
          writelog(p->pid, p->name, TICK, p->state, RUNNING);
          p->state = RUNNING;

          // dequeue
          // ignore init proc
          /* if (!is_first) */
          /* pop_rq(); */
          /* is_first = 0; */

          swtch(&(c->scheduler), p->context);
          // resume from here
          switchkvm();
          c->proc = 0; // ???
        }
      }
    }

    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void) {
  int intena;
  struct proc *p = myproc();

  if (!holding(&ptable.lock))
    panic("sched ptable.lock");
  if (mycpu()->ncli != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (readeflags() & FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void) {
  struct proc *curproc = myproc();

  acquire(&ptable.lock); // DOC: yieldlock

  // decrease priority
  if (IS_MLFQ && curproc->priority > 0)
    curproc->priority--;

  // added
  writelog(curproc->pid, curproc->name, YIELD, curproc->state, RUNNABLE);

  myproc()->state = RUNNABLE;

  // enqueue
  /* push_rq(curproc); */

  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void) {
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk) {
  struct proc *p = myproc();

  if (p == 0)
    panic("sleep");

  if (lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if (lk != &ptable.lock) { // DOC: sleeplock0
    acquire(&ptable.lock);  // DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;

  // added
  writelog(p->pid, p->name, SLEEP, p->state, SLEEPING);

  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if (lk != &ptable.lock) { // DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

// PAGEBREAK!
//  Wake up all processes sleeping on chan.
//  The ptable lock must be held.
static void wakeup1(void *chan) {
  struct proc *p;

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == SLEEPING && p->chan == chan) {
      // added
      writelog(p->pid, p->name, WAKEUP, p->state, RUNNABLE);

      // enqueue
      /* push_rq(p); */

      p->state = RUNNABLE;
    }
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan) {
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid) {
  struct proc *p;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if (p->pid == pid) {
      p->killed = 1;
      // Wake process from sleep if necessary.
      if (p->state == SLEEPING) {
        // added
        /* writelog(p->pid, KILL, p->state, RUNNABLE); */
        p->state = RUNNABLE;
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

// PAGEBREAK: 36
//  Print a process listing to console.  For debugging.
//  Runs when user types ^P on console.
//  No lock to avoid wedging a stuck machine further.
void procdump(void) {
  static char *states[] = {
      [UNUSED] "unused",   [EMBRYO] "embryo",  [SLEEPING] "sleep ",
      [RUNNABLE] "runble", [RUNNING] "run   ", [ZOMBIE] "zombie"};
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if (p->state == SLEEPING) {
      getcallerpcs((uint *)p->context->ebp + 2, pc);
      for (i = 0; i < 10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}