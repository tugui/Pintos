#ifndef USERPROG_FILE_HANDLE_H
#define USERPROG_FILE_HANDLE_H

#include "threads/thread.h"

#define BITS_PER_BYTE 8

bool is_open (int fd);
bool fd_install (unsigned int fd, struct file *f);
int allocate_fd (void);
int expand_files (struct files_handler *files, unsigned int size);
void free_files_handler (struct files_handler *files);

#endif
