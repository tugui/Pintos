#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <debug.h>
#include "filesys/off_t.h"
#include "threads/thread.h"

#define OUTPUT_MAX 128

void syscall_init (void);
void syscall_halt (void);
void syscall_exit (int status);
pid_t syscall_exec (const char *cmd_line);
int syscall_wait (pid_t);
bool syscall_create (const char *file, off_t initial_size);
bool syscall_remove (const char *file);
int syscall_open (const char *file);
int syscall_filesize (int fd);
int syscall_read (int fd, void *buffer, off_t size);
int syscall_write (int fd, const void *buffer, off_t);
void syscall_seek (int fd, off_t);
off_t syscall_tell (int fd);
void syscall_close (int fd);

#endif /* userprog/syscall.h */
