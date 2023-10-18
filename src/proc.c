#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

struct {
  struct spinlock lock;
  int value;
} bufsize;

struct runqueue {
  struct spinlock lock;
  int size;
  struct proc head;
};

int nextpid       = 1;
int finished_fork = 0;
int isnot_first_running[NPROC];
struct clock end_clock;
struct runqueue runqueue[NCPU];
struct proc head[NCPU];
static struct proc *initproc;
struct schedlog buf_log[BUFSIZE];
struct clock clock_log[NPROC][3];

extern void forkret(void);
extern void trapret(void);
static void wakeup1(void *chan);

inline struct clock rdtsc(void) {
  unsigned int lo, hi;
  struct clock c;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  c.hi = hi;
  c.lo = lo;
  return c;
}

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

void runqueueinit(void) {
  for (int i = 0; i < ncpu; i++) {
    struct runqueue *rq = &runqueue[i];
    /* initlock(&rq->lock, "runqueue"); */
    acquire(&rq->lock);
    acquire(&ptable.lock);
    struct proc *head = &rq->head;
    head->pid         = (-1) * (i + 1);
    head->next        = head;
    head->prev        = head;
    rq->size          = 0;
    release(&ptable.lock);
    release(&rq->lock);
  }

  bufsize.value = 0;
  initlock(&bufsize.lock, "bufsize");
}

// Must be called with ptable.lock and rq->lock
void enqueue(struct runqueue *rq, struct proc *p) {
  struct proc *head      = &rq->head;
  struct proc *tail      = rq->head.prev;
  struct proc *queue_elm = head->next;
  while (queue_elm != head) {
    if (queue_elm->pid == p->pid) {
      panic("enqueue: push same pid");
      return;
    }
    queue_elm = queue_elm->next;
  }
  rq->size++;
  p->prev    = tail;
  p->next    = head;
  head->prev = p;
  tail->next = p;
}

// Must be called with rq->lock
struct proc *dequeue(struct runqueue *rq) {
  if (rq->size == 0) {
    panic("dequeue : empty runqueue");
  }
  struct proc *head    = &rq->head;
  struct proc *p_poped = head->next;
  acquire(&ptable.lock);
  head->next          = p_poped->next;
  p_poped->next->prev = head;
  p_poped->next       = NULL;
  p_poped->prev       = NULL;
  release(&ptable.lock);
  rq->size--;
  return p_poped;
}

int mystrcmp(const char *p, const char *q) {
  while (*p && *p == *q)
    p++, q++;
  return (uchar)*p - (uchar)*q;
}

void writelog(int pid, char *pname, char event_name, int prev_pstate,
              int next_pstate) {
  if (finished_fork && !mystrcmp(pname, "bufwrite") && pid != -1) {
    /* if (finished_fork && (!mystrcmp(pname, "bufwrite") || !mystrcmp(pname,
     * "test"))) { */
    /* if (!mystrcmp(pname, "bufwrite") && pid != -1) { */
    struct clock cl = rdtsc();
    end_clock       = cl;

    acquire(&bufsize.lock);
    if (bufsize.value >= BUFSIZE) {
      release(&bufsize.lock);
      return;
    }
    bufsize.value++;
    buf_log[bufsize.value].clock       = cl;
    buf_log[bufsize.value].pid         = pid;
    buf_log[bufsize.value].event_name  = event_name;
    buf_log[bufsize.value].prev_pstate = prev_pstate;
    buf_log[bufsize.value].next_pstate = next_pstate;
    buf_log[bufsize.value].cpu         = mycpuid();
    for (int i = 0; i < 16; i++)
      buf_log[bufsize.value].name[i] = pname[i];
    release(&bufsize.lock);
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
  writelog(-1, "test", PTABLE_LOCK, RELEASED, ACQUIRED);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
      goto found;

  writelog(-1, "test", PTABLE_LOCK, ACQUIRED, RELEASED);
  release(&ptable.lock);
  return 0;

found:
  p->pid = nextpid++;

  writelog(p->pid, p->name, ALLOCPROC, p->state, EMBRYO);
  p->state = EMBRYO;

  writelog(-1, "test", PTABLE_LOCK, ACQUIRED, RELEASED);
  release(&ptable.lock);

  // Allocate kernel stack.
  if ((p->kstack = kalloc()) == 0) {
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
  writelog(-1, "test", PTABLE_LOCK, RELEASED, ACQUIRED);
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  struct runqueue *cur_rq = &runqueue[mycpuid()];
  acquire(&cur_rq->lock);
  enqueue(cur_rq, p); // don't forget to push initproc!
  release(&cur_rq->lock);

  writelog(-1, "test", PTABLE_LOCK, ACQUIRED, RELEASED);
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

  clock_log[np->pid][0] = rdtsc();

  writelog(-1, "test", PTABLE_LOCK, RELEASED, ACQUIRED);
  acquire(&ptable.lock);

  writelog(np->pid, np->name, FORK, np->state, RUNNABLE);
  np->state = RUNNABLE;

  // enqueue
  if (IS_MULTIPLE_RUNQUEUE) {
    // seek small runqueue
    // (not always ensured because rq->lock is not acquired)
    struct runqueue *target = NULL;
    int min_rqsize          = NPROC;
    for (int i = 0; i < ncpu; i++) {
      struct runqueue *rq = &runqueue[i];
      if (rq->size < min_rqsize) {
        target     = rq;
        min_rqsize = rq->size;
      }
    }
    if (target == NULL)
      panic("target not exist");

    acquire(&target->lock);
    enqueue(target, np);
    release(&target->lock);
  }

  release(&ptable.lock);
  writelog(-1, "test", PTABLE_LOCK, ACQUIRED, RELEASED);

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

  writelog(-1, "test", PTABLE_LOCK, RELEASED, ACQUIRED);
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

  clock_log[curproc->pid][2] = rdtsc();

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

  writelog(-1, "test", PTABLE_LOCK, RELEASED, ACQUIRED);
  acquire(&ptable.lock);
  for (;;) {
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->parent != curproc)
        continue;
      havekids = 1;

      // Found one.
      if (p->state == ZOMBIE) {
        writelog(p->pid, p->name, WAIT, p->state, UNUSED);

        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid    = 0;
        p->parent = 0;
        writelog(-1, "test", PTABLE_LOCK, ACQUIRED, RELEASED);
        p->name[0] = 0;
        p->killed  = 0;
        p->state   = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || curproc->killed) {
      writelog(-1, "test", PTABLE_LOCK, ACQUIRED, RELEASED);
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); // DOC: wait-sleep
  }
}

void work_steal(struct runqueue *cur_rq) {
  struct runqueue *steal_target = NULL;
  int max_rqsize                = 0;

  int stealid = -1;

  // seek stealing target
  for (int i = 0; i < ncpu; i++) {
    struct runqueue *rq = &runqueue[i];
    if (rq->size > max_rqsize && rq != cur_rq) {
      steal_target = rq;
      max_rqsize   = rq->size;

      stealid = i;
    }
  }

  if (steal_target == NULL || stealid == 0) {
    return;
  }

  if (steal_target == cur_rq)
    panic("steal itself");

  // steal process
  acquire(&steal_target->lock);
  // steal_target may be vanished by execution
  if (steal_target->size != 0) {
    struct proc *p_popped = dequeue(steal_target);

    if (p_popped == 0)
      panic("test");

    acquire(&ptable.lock);
    enqueue(cur_rq, p_popped);
    release(&ptable.lock);
  }
  release(&steal_target->lock);
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
  struct cpu *c           = mycpu();
  c->proc                 = 0;
  struct runqueue *cur_rq = &runqueue[mycpuid()];

  // Multiple runqueue scheduler
  while (IS_MULTIPLE_RUNQUEUE) {
    // Enable interrupts on this processor.
    sti();

    // current runqueue is empty
    acquire(&cur_rq->lock);
    if (cur_rq->size == 0) {
      work_steal(cur_rq);
      release(&cur_rq->lock);
    }

    // current runqueue is not empty
    else {
      p = dequeue(cur_rq);
      release(&cur_rq->lock);
      c->proc = p;
      switchuvm(p);

      if (isnot_first_running[p->pid] == 0) {
        clock_log[p->pid][1]        = rdtsc();
        isnot_first_running[p->pid] = 1;
      }

      writelog(p->pid, p->name, TICK, p->state, RUNNING);
      /* acquire(&ptable.lock); */
      p->state = RUNNING;
      /* release(&ptable.lock); */

      swtch(&(c->scheduler), p->context);
      switchkvm();
      c->proc = 0;
    }
  }

  // default round robin scheduler
  while (IS_ROUNDROBIN) {
    // Enable interrupts on this processor.
    sti();
    // Loop over process table looking for process to run.
    writelog(-1, "test", PTABLE_LOCK, RELEASED, ACQUIRED);
    acquire(&ptable.lock);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->state == RUNNABLE) {
        c->proc = p;
        switchuvm(p);

        if (isnot_first_running[p->pid] == 0) {
          clock_log[p->pid][1]        = rdtsc();
          isnot_first_running[p->pid] = 1;
        }

        writelog(p->pid, p->name, TICK, p->state, RUNNING);
        p->state = RUNNING;
        swtch(&(c->scheduler), p->context);
        switchkvm();
        c->proc = 0;
      }
    }
    writelog(-1, "test", PTABLE_LOCK, ACQUIRED, RELEASED);
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

  if (IS_MULTIPLE_RUNQUEUE) {
    struct cpu *curcpu = mycpu();
    writelog(-1, "test", PTABLE_LOCK, ACQUIRED, RELEASED);
    release(&ptable.lock);
    swtch(&p->context, curcpu->scheduler);
    writelog(-1, "test", PTABLE_LOCK, RELEASED, ACQUIRED);
    acquire(&ptable.lock);
    curcpu->intena = intena;
  } else {
    swtch(&p->context, mycpu()->scheduler);
    mycpu()->intena = intena;
  }
}

// Give up the CPU for one scheduling round.
void yield(void) {
  struct proc *curproc = myproc();

  writelog(curproc->pid, curproc->name, YIELD, curproc->state, RUNNABLE);

  writelog(-1, "test", PTABLE_LOCK, RELEASED, ACQUIRED);
  acquire(&ptable.lock); // DOC: yieldlock

  // enqueue
  if (IS_MULTIPLE_RUNQUEUE) {
    struct runqueue *cur_rq = &runqueue[mycpuid()];
    acquire(&cur_rq->lock);
    enqueue(cur_rq, curproc);
    release(&cur_rq->lock);
  }

  curproc->state = RUNNABLE;

  sched();
  writelog(-1, "test", PTABLE_LOCK, ACQUIRED, RELEASED);
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void) {
  static int first = 1;
  // Still holding ptable.lock from scheduler.

  if (!IS_MULTIPLE_RUNQUEUE) {
    writelog(-1, "test", PTABLE_LOCK, ACQUIRED, RELEASED);
    release(&ptable.lock);
  }

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
    writelog(-1, "test", PTABLE_LOCK, RELEASED, ACQUIRED);
    acquire(&ptable.lock); // DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;

  writelog(p->pid, p->name, SLEEP, p->state, SLEEPING);

  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if (lk != &ptable.lock) { // DOC: sleeplock2
    writelog(-1, "test", PTABLE_LOCK, ACQUIRED, RELEASED);
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
      writelog(p->pid, p->name, WAKEUP, p->state, RUNNABLE);

      // enqueue
      if (IS_MULTIPLE_RUNQUEUE) {
        struct runqueue *cur_rq = &runqueue[mycpuid()];
        acquire(&cur_rq->lock);
        enqueue(cur_rq, p);
        release(&cur_rq->lock);
      }

      p->state = RUNNABLE;
    }
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan) {
  if (holding(&ptable.lock))
    panic("already locked!\n");

  writelog(-1, "test", PTABLE_LOCK, RELEASED, ACQUIRED);
  acquire(&ptable.lock);
  wakeup1(chan);
  writelog(-1, "test", PTABLE_LOCK, ACQUIRED, RELEASED);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid) {
  struct proc *p;

  writelog(-1, "test", PTABLE_LOCK, RELEASED, ACQUIRED);
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if (p->pid == pid) {
      p->killed = 1;
      // Wake process from sleep if necessary.
      if (p->state == SLEEPING) {
        p->state = RUNNABLE;
      }
      writelog(-1, "test", PTABLE_LOCK, ACQUIRED, RELEASED);
      release(&ptable.lock);
      return 0;
    }
  }
  writelog(-1, "test", PTABLE_LOCK, ACQUIRED, RELEASED);
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
