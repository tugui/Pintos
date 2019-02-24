#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <bitmap.h>
#include <debug.h>
#include <hash.h>
#include <list.h>
#include <stdint.h>
#include "threads/fixed-point.h"
#include "threads/synch.h"

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

typedef int pid_t;
#define PID_ERROR ((pid_t) -1)          /* Error value for pid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

#define OPEN_DEFAULT 32									/* Default number of open files. */
#define OPEN_BITMAP_DEFAULT 128					/* Default size of open file bitmap. */

/* Subprocess information. */
struct child 
	{
		tid_t tid;
		pid_t pid;
		int retval;
		bool waited;
		bool be_wait;
		bool terminated;
		struct thread *t;
		struct list_elem elem;
	};

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */

		/* Used for advanced scheduler. */
		int nice;														/* Niceness. */
		struct float_number recent_cpu;			/* Recent cpu. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

		/* Used for priority donation. */
		struct thread *donee;								/* Priority donee. */
		int original_priority;							/* Original priority while being donated. */

		struct thread *parent;
#ifdef USERPROG
    /* Owned by userprog/process.c. */
		pid_t pid;													/* Process identifier. */
    uint32_t *pagedir;                  /* Page directory. */
		struct file *exec_file;							/* Executing file. */
		struct child *self;									/* Its own information as a subprocess. */
		struct list children_list;					/* Subprocess information list. */
		struct files_handler *files;				/* Files handler. */
		struct semaphore child_load;				/* Used for exec system call. */
		struct semaphore child_wait;				/* Used for wait system call. */
#endif

#ifdef VM
		struct hash pages;
		struct list mmapfiles;
#endif

    /* Shared between thread.c and timer.c. */
	  int64_t jet_lag;										/* Jet lag in the timer list. */
    struct list_elem timerelem;         /* List element for the timer list. */

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
		
  };

struct fdtable {
	struct file **fd; /* Open file array pointer. */
	struct bitmap *fd_map; /* Open file bitmap pointer. */
	unsigned int max_fds; /* Length of fd or number of open files. */
};

struct files_handler {
	int next_fd;

	struct fdtable *fdt;
	struct fdtable fdtab;

	struct lock file_lock;

	struct file *fd_array[OPEN_DEFAULT]; /* Initial open file array. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;
extern bool preempt_active;
extern struct list timer_list;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

#endif /* threads/thread.h */
