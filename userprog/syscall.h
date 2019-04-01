#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#define OUTPUT_MAX 128

void syscall_init (void);
void syscall_exit (int status);

#endif /* userprog/syscall.h */
