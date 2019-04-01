#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"

struct bitmap;

enum inode_type
	{
		INODE_DIR,
		INODE_FILE,
	};

/*
 * Track a single inode's readahead state.
 */
struct inode_ra_state
	{
		off_t start;
		unsigned int size; /* Number of readahead pages. */
		unsigned int async_size; /* Number of asynchronous readahead pages. */
		unsigned int ra_pages; /* Maximum readahead window. */
		off_t prev_pos;	/* Cache last read position. */
	};

void inode_init (void);
void inode_ra_state_init (struct inode_ra_state *ra);
bool inode_create (block_sector_t, off_t length, enum inode_type);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);

bool inode_is_dir (const struct inode *);
off_t inode_length (const struct inode *);
struct lock *inode_get_lock (struct inode *);
int inode_get_open_number (const struct inode *inode);
block_sector_t inode_get_inumber (const struct inode *);

off_t inode_read_at (struct inode *, struct inode_ra_state *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);

#endif /* filesys/inode.h */
