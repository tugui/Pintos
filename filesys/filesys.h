#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/inode.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR	 0 /* Free map file inode sector. */
#define ROOT_DIR_SECTOR  1 /* Root directory file inode sector. */
#define DEFAULT_DIR_SIZE 16

#define SECTOR_ERROR SIZE_MAX
#define READ_AHEAD_WINDOW_SIZE 32

/* Block device that contains the file system. */
struct block *fs_device;

void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *name, off_t initial_size, enum inode_type type);
struct file *filesys_open (const char *name);
void filesys_close (struct file *);
bool filesys_remove (const char *name);
bool filesys_chdir (const char *name);

#endif /* filesys/filesys.h */
