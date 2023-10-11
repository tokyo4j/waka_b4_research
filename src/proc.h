// Per-CPU state
#include <stdint.h>
struct cpu {
  uchar apicid;              // Local APIC ID
  struct context *scheduler; // swtch() here to enter scheduler
  struct taskstate ts;       // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS]; // x86 global descriptor table
  volatile uint started;     // Has the CPU started?
  int ncli;                  // Depth of pushcli nesting.
  int intena;                // Were interrupts enabled before pushcli?
  struct proc *proc;         // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

// PAGEBREAK: 17
//  Saved registers for kernel context switches.
//  Don't need to save all the segment registers (%cs, etc),
//  because they are constant across kernel contexts.
//  Don't need to save %eax, %ecx, %edx, because the
//  x86 convention is that the caller has saved them.
//  Contexts are stored at the bottom of the stack they
//  describe; the stack pointer is the address of the context.
//  The layout of the context matches the layout of the stack in swtch.S
//  at the "Switch stacks" comment. Switch doesn't save eip explicitly,
//  but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate {
  UNUSED,
  EMBRYO,
  SLEEPING,
  RUNNABLE,
  RUNNING,
  ZOMBIE,
  ACQUIRED,
  RELEASED
};

// Per-process state
struct proc {
  uint sz;                    // Size of process memory (bytes)
  pde_t *pgdir;               // Page table
  char *kstack;               // Bottom of kernel stack for this process
  enum procstate state;       // Process state
  int pid;                    // Process ID
  struct proc *parent;        // Parent process
  struct trapframe *tf;       // Trap frame for current syscall
  struct context *context;    // swtch() here to run process
  void *chan;                 // If non-zero, sleeping on chan
  int killed;                 // If non-zero, have been killed
  struct file *ofile[NOFILE]; // Open files
  struct inode *cwd;          // Current directory
  char name[16];              // Process name (debugging)

  int priority;      // added
  struct proc *next; // for runqueue
  struct proc *prev; // for runqueue
  /* int already_stolen; */
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap

// added
struct clock {
  unsigned int hi;
  unsigned int lo;
};

enum events {
  ALLOCPROC,
  WAKEUP,
  YIELD,
  FORK,
  TICK,
  EXIT,
  WAIT,
  SLEEP,
  ALLOCEXIT,
  USERINIT,
  KILL,
  SWITCH,

  PTABLE_LOCK,
  RUNQUEUE_LOCK
};

// you can enable only one of them
#define IS_MLFQ 0
#define IS_MULTIPLE_RUNQUEUE 1
#define IS_ROUNDROBIN 0

#define IS_BOOST_PRIORITY 0
#define MAX_PRIO 100

struct schedlog {
  struct clock clock;
  int pid;

  char name[16];

  // events
  // 0:ALLOCPROC, 1:WAKEUP, 2:YIELD, 3:FORK, 4:TICK, 5:EXIT, 6:WAIT, 7:SLEEP,
  //
  // unnecessary?
  // 8:ALLOCEXIT(?), 9:USERINIT(?), 10:KILL(?), 11:SWITCH
  int event_name;

  // pstate
  // any of 'enum procstate'
  // 0:UNUSED, 1:EMBRYO, 2:SLEEPING, 3:RUNNABLE, 4:RUNNING, 5:ZOMBIE
  int prev_pstate;
  int next_pstate;
  int cpu;
  /* int cpu_to; */
};

// why extern?
extern struct schedlog buf_log[LOGBUFSIZE];
extern int buf_rest_size;

extern int finished_fork;

extern struct clock clock_log[NPROC][3];
extern int isnot_first_running[NPROC];

extern struct clock start_clock;
extern struct clock end_clock;
