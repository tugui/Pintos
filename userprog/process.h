#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
void free_children_list (struct list *);
void set_return_value (struct child *, int status);
void parent_wakeup (struct thread *parent, tid_t tid);

#endif /* userprog/process.h */
