#include "filesys/inode.h"
#include <debug.h>
#include <list.h>
#include <log2.h>
#include <round.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define DIRECT_SECTOR_NUMBER 12
#define INODE_SECTOR_NUMBER  14
#define DISK_SECTOR_NUMBER (BLOCK_SECTOR_SIZE / 4)

static char zeros[BLOCK_SECTOR_SIZE];

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
		block_sector_t sector[INODE_SECTOR_NUMBER];
    off_t length;         /* File size in bytes. */
		struct lock lock;
		enum inode_type type;
    unsigned magic;       /* Magic number. */
    uint32_t unused[105]; /* Not used. */
  };

struct data_disk 
	{
		block_sector_t sector[DISK_SECTOR_NUMBER];
	};

static block_sector_t find_sector (const struct inode *inode, size_t index);
static bool allocate_first_level_sector (size_t remain, block_sector_t *sectorp);
static bool allocate_second_level_sector (size_t remain, block_sector_t *sectorp);
static void free_inode (struct inode_disk *data, size_t sectors);
static bool extend_inode (struct inode *inode, off_t size, off_t offset);

static void cache_sync_readahead (const struct inode *inode, struct inode_ra_state *ra_state, off_t offset, unsigned int req_size);
static void cache_async_readahead (const struct inode *inode, struct inode_ra_state *ra_state, block_sector_t sector, off_t offset, unsigned int req_size);

/* Returns the number of sectors to allocate for 
   an inode SIZE bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem; /* Element in inode list. */
    block_sector_t sector; /* Sector number of disk location. */
    int open_cnt;          /* Number of openers. */
    bool removed;          /* True if deleted, false otherwise. */
    int deny_write_cnt;    /* 0: writes ok, >0: deny writes. */
		struct lock lock;
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns SECTOR_ERROR if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t length, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < length)
		return find_sector (inode, pos >> BLOCK_SECTOR_SHIFT);
  return SECTOR_ERROR;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes the inode readahead state. */
void
inode_ra_state_init (struct inode_ra_state *ra)
{
	ra->ra_pages = READ_AHEAD_WINDOW_SIZE;
	ra->prev_pos = -1;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, enum inode_type type)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
			disk_inode->type = type;
      disk_inode->magic = INODE_MAGIC;

			size_t sectors = bytes_to_sectors (length);
			size_t i = 0;
			while (i < sectors)
				{
					if (i < DIRECT_SECTOR_NUMBER)
						{
							if (free_map_allocate (1, &disk_inode->sector[i])) 
								cache_write (disk_inode->sector[i], zeros, 0, BLOCK_SECTOR_SIZE);
							else
								goto error;
							i++;
						}
					else if (i < DIRECT_SECTOR_NUMBER + DISK_SECTOR_NUMBER)
						{
							int remain = sectors - i;
							if (!allocate_first_level_sector (remain, &disk_inode->sector[12]))
								goto error;
							i += DISK_SECTOR_NUMBER;
						}
					else if (i < DIRECT_SECTOR_NUMBER + DISK_SECTOR_NUMBER + DISK_SECTOR_NUMBER * DISK_SECTOR_NUMBER)
						{
							int remain = sectors - i;
							if (!allocate_second_level_sector (remain, &disk_inode->sector[13]))
								goto error;
							i += DISK_SECTOR_NUMBER * DISK_SECTOR_NUMBER;
						}
					else
						goto error;
				}
			cache_write (sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
			success = true; 
    }
  return success;

error:
	free_inode (disk_inode, DIRECT_SECTOR_NUMBER);
	free (disk_inode);
	return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = MALLOC (1, struct inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
	lock_init (&inode->lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's lock. */
struct lock *
inode_get_lock (struct inode *inode)
{
  return &inode->lock;
}

/* Returns INODE's open number. */
int
inode_get_open_number (const struct inode *inode)
{
  return inode->open_cnt;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          size_t sectors = bytes_to_sectors (inode_length (inode));
					struct inode_disk *disk_inode = calloc (1, sizeof *disk_inode);
					if (disk_inode != NULL)
						{
							cache_read (inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
							free_inode (disk_inode, sectors);
							free (disk_inode);
						}
          free_map_release (inode->sector, 1);
        }
      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, struct inode_ra_state *ra_state,
		void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
	off_t bytes_read = 0;
	off_t length = inode_length (inode);
	if (length == 0)
		return bytes_read;

	off_t index = offset >> BLOCK_SECTOR_SHIFT;
	off_t last_index = (offset + size + BLOCK_SECTOR_SIZE - 1) >> BLOCK_SECTOR_SHIFT;
	off_t prev_index = ra_state->prev_pos >> BLOCK_SECTOR_SHIFT;
	off_t prev_offset = ra_state->prev_pos & (BLOCK_SECTOR_SIZE - 1);
  off_t sector_ofs = offset & (BLOCK_SECTOR_SIZE - 1);

	while (size > 0)
		{
      /* Disk sector to read, starting byte offset within sector. */
			block_sector_t sector = find_sector (inode, index);
			struct cache *c = cache_find (sector);
			if (c == NULL)
				cache_sync_readahead (inode, ra_state, index, last_index - index);

			if (cache_readahead (sector))
				cache_async_readahead (inode, ra_state, sector, index, last_index - index);

			off_t end_index = (length - 1) >> BLOCK_SECTOR_SHIFT;
			if (index > end_index)
				goto out;

      /* Number of bytes to actually copy out of this sector. */
			int sector_left = BLOCK_SECTOR_SIZE;
			if (index == end_index)
				{
					sector_left = ((length - 1) & (BLOCK_SECTOR_SIZE - 1)) + 1;
					if (sector_left <= sector_ofs)
						goto out;
				}
			sector_left -= sector_ofs;

			int chunk_size = size < sector_left ? size : sector_left;
			if (chunk_size <= 0)
				break;

			prev_index = index;

			/* Copy data to user space. */
			cache_read (sector, buffer + bytes_read, sector_ofs, chunk_size);

      /* Advance. */
			sector_ofs += chunk_size;
			index += sector_ofs >> BLOCK_SECTOR_SHIFT;
			sector_ofs &= BLOCK_SECTOR_SIZE - 1;
			prev_offset = sector_ofs;
			bytes_read += chunk_size;
			size -= chunk_size;
		}

out:
	ra_state->prev_pos = prev_index;
	ra_state->prev_pos *= BLOCK_SECTOR_SIZE;
	ra_state->prev_pos |= prev_offset;

	return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs. */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size, off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

	/* If the inode is for a directory, it may have gotten the lock. */
	bool held_lock = lock_held_by_current_thread (inode_get_lock (inode));
	bool extend_flag = false;
	off_t length = inode_length (inode);
  off_t data_left = length - (offset + size);
	if (data_left < 0)
		{
			if (!extend_inode (inode, size, offset))
				return 0;
			if (!held_lock)
				lock_acquire (inode_get_lock (inode));
			extend_flag = true;
			length = offset + size;
		}

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, length, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
				break;

      if (!(sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE))
        {
          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector first.
						 Otherwise we start with a sector of all zeros. */
          if (!(sector_ofs > 0 || chunk_size < sector_left))
						cache_set (inode->sector, 0, 0, BLOCK_SECTOR_SIZE);
        }
			cache_write (sector_idx, buffer + bytes_written, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

	if (extend_flag)
		{
			cache_write_at (inode->sector, offsetof (struct inode_disk, length), length);
			if (!held_lock)
				lock_release (inode_get_lock (inode));
		}

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
	return cache_read_at (inode->sector, offsetof (struct inode_disk, length));
}

static block_sector_t 
find_sector (const struct inode *inode, size_t index)
{
	block_sector_t sector_idx = SECTOR_ERROR;
	if (index < DIRECT_SECTOR_NUMBER)
		{
			block_sector_t sector = cache_read_at (inode->sector, offsetof (struct inode_disk, sector[index]));
			if (sector != 0)
				sector_idx = sector;
		}
	else if (index < DIRECT_SECTOR_NUMBER + DISK_SECTOR_NUMBER)
		{
			block_sector_t sector = cache_read_at (inode->sector, offsetof (struct inode_disk, sector[12]));
			if (sector != 0)
				{
					struct data_disk *first_level = MALLOC (1, struct data_disk);
					if (first_level == NULL)
						return sector_idx;
					cache_read (sector, first_level, 0, BLOCK_SECTOR_SIZE);
					if (first_level->sector[index - DIRECT_SECTOR_NUMBER] != 0)
						sector_idx = first_level->sector[index - DIRECT_SECTOR_NUMBER];
					free (first_level);
				}
		}
	else if (index < DIRECT_SECTOR_NUMBER + DISK_SECTOR_NUMBER + DISK_SECTOR_NUMBER * DISK_SECTOR_NUMBER)
		{
			block_sector_t sector = cache_read_at (inode->sector, offsetof (struct inode_disk, sector[13]));
			if (sector != 0)
				{
					struct data_disk *first_level = MALLOC (1, struct data_disk);
					struct data_disk *second_level = MALLOC (1, struct data_disk);
					if (first_level == NULL || second_level == NULL)
						{
							free (first_level);
							free (second_level);
							return sector_idx;
						}

					size_t first_level_offset = (index - DIRECT_SECTOR_NUMBER - DISK_SECTOR_NUMBER) / DISK_SECTOR_NUMBER;
					size_t second_level_offset = (index - DIRECT_SECTOR_NUMBER - DISK_SECTOR_NUMBER) % DISK_SECTOR_NUMBER;

					cache_read (sector, first_level, 0, BLOCK_SECTOR_SIZE);
					if (first_level->sector[first_level_offset] != 0)
						{
							cache_read (first_level->sector[first_level_offset], second_level, 0, BLOCK_SECTOR_SIZE);
							if (second_level->sector[second_level_offset] != 0)
								sector_idx = second_level->sector[second_level_offset];
						}
					free (first_level);
					free (second_level);
				}
		}
	return sector_idx;
}

static void
free_sectors (struct data_disk *disk_data)
{
	int i;
	for (i = 0; i < DISK_SECTOR_NUMBER; i++)
		{
			if (disk_data->sector[i] != 0)
				free_map_release (disk_data->sector[i], 1);
		}
}

static void
free_indirect_sectors (struct data_disk *first_level)
{
	struct data_disk *second_level = MALLOC (1, struct data_disk);
	if (second_level != NULL)
		{
			int i;
			for (i = 0; i < DISK_SECTOR_NUMBER; i++)
				{
					if (first_level->sector[i] != 0)
						{
							cache_read (first_level->sector[i], second_level, 0, BLOCK_SECTOR_SIZE);
							free_sectors (second_level);
							free_map_release (first_level->sector[i], 1);
						}
				}
		}
}

static bool
allocate_first_level_sector (size_t remain, block_sector_t *sectorp)
{
	struct data_disk *first_level = NULL;
	bool success = false;

	ASSERT (remain > 0);
	ASSERT (sizeof *first_level == BLOCK_SECTOR_SIZE);

	first_level = calloc (1, sizeof *first_level);
	if (first_level != NULL)
		{
			if (free_map_allocate (1, sectorp)) 
				{
					int i = 0;
					while (remain > 0 && i < DISK_SECTOR_NUMBER)
						{
							if (free_map_allocate (1, &first_level->sector[i])) 
								cache_write (first_level->sector[i], zeros, 0, BLOCK_SECTOR_SIZE);
							else
								{
									free_sectors (first_level);
									free_map_release (*sectorp, 1);
									*sectorp = 0;
									goto out;
								}
							i++;
							remain--;
						}
					cache_write (*sectorp, first_level, 0, BLOCK_SECTOR_SIZE);
					success = true;
				}

out:
			free (first_level);
		}
	return success;
}

static bool
allocate_second_level_sector (size_t remain, block_sector_t *sectorp)
{
	struct data_disk *first_level = NULL;
	struct data_disk *second_level = NULL;
	bool success = false;

	ASSERT (remain > 0);
	ASSERT (sizeof *first_level == BLOCK_SECTOR_SIZE);
	ASSERT (sizeof *second_level == BLOCK_SECTOR_SIZE);

	first_level = calloc (1, sizeof *first_level);
	if (first_level != NULL)
		{
			if (free_map_allocate (1, sectorp)) 
				{
					int i = 0;
					while (remain > 0 && i < DISK_SECTOR_NUMBER)
						{
							if (free_map_allocate (1, &first_level->sector[i])) 
								{
									second_level = calloc (1, sizeof *second_level);
									if (second_level != NULL)
										{
											int j = 0;
											while (remain > 0 && j < DISK_SECTOR_NUMBER)
												{
													if (free_map_allocate (1, &second_level->sector[j])) 
														cache_write (second_level->sector[j], zeros, 0, BLOCK_SECTOR_SIZE);
													else
														{
															free_sectors (second_level);
															free_map_release (first_level->sector[i], 1);
															free (second_level);

															first_level->sector[i] = 0;
															free_indirect_sectors (first_level);
															free_map_release (*sectorp, 1);
															*sectorp = 0;
															goto out;
														}
													j++;
													remain--;
												}
											cache_write (first_level->sector[i], second_level, 0, BLOCK_SECTOR_SIZE);
										}
									else
										{
											free_indirect_sectors (first_level);
											free_map_release (*sectorp, 1);
											*sectorp = 0;
											goto out;
										}
								}
							else
								{
									free_indirect_sectors (first_level);
									free_map_release (*sectorp, 1);
									*sectorp = 0;
									goto out;
								}
							i++;
						}
					cache_write (*sectorp, first_level, 0, BLOCK_SECTOR_SIZE);
					success = true;
				}
out:
			free (first_level);
		}
	return success;
}

static void
free_inode (struct inode_disk *data, size_t sectors)
{
	if (sectors == 0)
		return;

	struct data_disk *first_level = NULL;
	struct data_disk *second_level = NULL;

	size_t i = 0;
	while (i < sectors && i < DIRECT_SECTOR_NUMBER)
		{
			if (data->sector[i] != 0)
				free_map_release (data->sector[i], 1);	
			i++;
		}

	if (i >= sectors)
		return;

	first_level = MALLOC (1, struct data_disk);
	if (data->sector[12] != 0)
		{
			cache_read (data->sector[12], first_level, 0, BLOCK_SECTOR_SIZE);
			while (i < sectors && i < DIRECT_SECTOR_NUMBER + DISK_SECTOR_NUMBER)
				{
					if (first_level->sector[i - DIRECT_SECTOR_NUMBER] != 0)
						free_map_release (first_level->sector[i - DIRECT_SECTOR_NUMBER], 1);	
					i++;
				}
			free_map_release (data->sector[12], 1);	
		}

	if (i >= sectors)
		goto out;

	second_level = MALLOC (1, struct data_disk);
	if (data->sector[13] != 0)
		{
			cache_read (data->sector[13], first_level, 0, BLOCK_SECTOR_SIZE);
			while (i < sectors && i < DIRECT_SECTOR_NUMBER + DISK_SECTOR_NUMBER + DISK_SECTOR_NUMBER * DISK_SECTOR_NUMBER)
				{
					size_t first_level_offset = (i - DIRECT_SECTOR_NUMBER - DISK_SECTOR_NUMBER) / DISK_SECTOR_NUMBER;
					if (first_level->sector[first_level_offset] != 0)
						{
							cache_read (first_level->sector[first_level_offset], second_level, 0, BLOCK_SECTOR_SIZE);
							while (i < sectors && i < DIRECT_SECTOR_NUMBER + DISK_SECTOR_NUMBER + (first_level_offset + 1) * DISK_SECTOR_NUMBER)
								{
									size_t second_level_offset = (i - DIRECT_SECTOR_NUMBER - DISK_SECTOR_NUMBER) % DISK_SECTOR_NUMBER;
									if (second_level->sector[second_level_offset] != 0)
										free_map_release (second_level->sector[second_level_offset], 1);	
									i++;
								}
							free_map_release (first_level->sector[first_level_offset], 1);
						}
					else
						i += DISK_SECTOR_NUMBER;
				}
			free_map_release (data->sector[13], 1);	
		}

out:
	free (first_level);
	free (second_level);
}

static bool
extend_inode (struct inode *inode, off_t size, off_t offset)
{
	struct data_disk *first_level = NULL;
	struct data_disk *second_level = NULL;
	bool success = false;

	size_t sectors = bytes_to_sectors (offset + size);
	struct inode_disk *data = MALLOC (1, struct inode_disk);
	if (data == NULL)
		return success;

	cache_read (inode->sector, data, 0, BLOCK_SECTOR_SIZE);
	size_t i = bytes_to_sectors (inode_length (inode));
	while (i < sectors && i < DIRECT_SECTOR_NUMBER)
		{
			if (free_map_allocate (1, &data->sector[i])) 
				cache_write (data->sector[i], zeros, 0, BLOCK_SECTOR_SIZE);
			else
				goto error;
			i++;
		}

	if (i >= sectors)
		goto out;

	first_level = MALLOC (1, struct data_disk);
	if (i < DIRECT_SECTOR_NUMBER + DISK_SECTOR_NUMBER)
		{
			if (data->sector[12] != 0)
				{
					cache_read (data->sector[12], first_level, 0, BLOCK_SECTOR_SIZE);
					while (i < sectors && i < DIRECT_SECTOR_NUMBER + DISK_SECTOR_NUMBER)
						{
							if (free_map_allocate (1, &first_level->sector[i - DIRECT_SECTOR_NUMBER]))
								cache_write (first_level->sector[i - DIRECT_SECTOR_NUMBER], zeros, 0, BLOCK_SECTOR_SIZE);
							else
								goto error;
							i++;
						}
					cache_write (data->sector[12], first_level, 0, BLOCK_SECTOR_SIZE);
				}
			else
				{
					int remain = sectors - i;
					if (!allocate_first_level_sector (remain, &data->sector[12]))
						goto error;
					i += DISK_SECTOR_NUMBER;
				}
		}

	if (i >= sectors)
		goto out;

	second_level = MALLOC (1, struct data_disk);
	if (i < DIRECT_SECTOR_NUMBER + DISK_SECTOR_NUMBER + DISK_SECTOR_NUMBER * DISK_SECTOR_NUMBER)
		{
			if (data->sector[13] != 0)
				{
					cache_read (data->sector[13], first_level, 0, BLOCK_SECTOR_SIZE);
					while (i < sectors && i < DIRECT_SECTOR_NUMBER + DISK_SECTOR_NUMBER + DISK_SECTOR_NUMBER * DISK_SECTOR_NUMBER)
						{
							size_t first_level_offset = (i - DIRECT_SECTOR_NUMBER - DISK_SECTOR_NUMBER) / DISK_SECTOR_NUMBER;
							if (first_level->sector[first_level_offset] != 0)
								{
									cache_read (first_level->sector[first_level_offset], second_level, 0, BLOCK_SECTOR_SIZE);
									while (i < sectors && i < DIRECT_SECTOR_NUMBER + DISK_SECTOR_NUMBER + (first_level_offset + 1) * DISK_SECTOR_NUMBER)
										{
											size_t second_level_offset = (i - DIRECT_SECTOR_NUMBER - DISK_SECTOR_NUMBER) % DISK_SECTOR_NUMBER;
											if (free_map_allocate (1, &second_level->sector[second_level_offset]))
												cache_write (second_level->sector[second_level_offset], zeros, 0, BLOCK_SECTOR_SIZE);
											else
												goto error;
											i++;
										}
									cache_write (first_level->sector[first_level_offset], second_level, 0, BLOCK_SECTOR_SIZE);
								}
							else if (free_map_allocate (1, &first_level->sector[first_level_offset]))
								{
									memset (second_level, 0, sizeof *second_level);
									while (i < sectors && i < DIRECT_SECTOR_NUMBER + DISK_SECTOR_NUMBER + (first_level_offset + 1) * DISK_SECTOR_NUMBER)
										{
											size_t second_level_offset = (i - DIRECT_SECTOR_NUMBER - DISK_SECTOR_NUMBER) % DISK_SECTOR_NUMBER;
											if (free_map_allocate (1, &second_level->sector[second_level_offset]))
												cache_write (second_level->sector[second_level_offset], zeros, 0, BLOCK_SECTOR_SIZE);
											else
												goto error;
											i++;
										}
									cache_write (first_level->sector[first_level_offset], second_level, 0, BLOCK_SECTOR_SIZE);
								}
							else
								goto error;
						}
					cache_write (data->sector[13], first_level, 0, BLOCK_SECTOR_SIZE);
				}
			else
				{
					int remain = sectors - i;
					if (!allocate_second_level_sector (remain, &data->sector[13]))
						goto error;
				}
		}
	else
		goto error;

out:
	cache_write (inode->sector, data, 0, BLOCK_SECTOR_SIZE);
	success = true;
	
error:
	free (first_level);
	free (second_level);
	return success;
}

bool
inode_is_dir (const struct inode *inode)
{
	if (inode != NULL)
		{
			enum inode_type type = cache_read_at (inode->sector, offsetof (struct inode_disk, type));
			if (type == INODE_DIR)
				return true;
		}
	return false;
}

/*
 * Set the initial window size.
 */
static unsigned int 
get_init_ra_size (unsigned int size, unsigned int max)
{
	unsigned int newsize = roundup_pow_of_two (size);

	if (newsize <= max / 32)
		newsize = newsize * 4;
	else if (newsize <= max / 4)
		newsize = newsize * 2;
	else
		newsize = max;

	return newsize;
}

/*
 * Get the previous window size, ramp it up, and
 * return it as the new window size.
 */
static unsigned int 
get_next_ra_size (struct inode_ra_state *ra, unsigned int max)
{
	unsigned int cur = ra->size;

	if (cur < max / 16)
		return 4 * cur;
	if (cur <= max / 2)
		return 2 * cur;
	return max;
}

/*
 * Find the next gap about this inode in the cache.
 */
static off_t
next_miss (const struct inode *inode, off_t index, unsigned int max_scan)
{
	while (max_scan--)
		{
			block_sector_t sector = find_sector (inode, index);
			if (sector == SECTOR_ERROR || cache_find (sector) == NULL)
				break;
			index++;
		}
	return index;
}

/*
 * Reads a certain number of logical continuous sectors of the inode 
 * which is calculated by the readahead algorithm.
 */
static unsigned int
do_cache_readahead (const struct inode *inode, off_t start,
		unsigned int nr_to_read, unsigned int lookahead_size)
{
	unsigned int nr_sectors = 0;
	off_t length = inode_length (inode);
	if (length == 0)
		goto out;

	int end_index = (length - 1) >> BLOCK_SECTOR_SHIFT;

	/*
	 * Preallocate as many caches as we need.
	 */
	unsigned int i;
	for (i = 0; i < nr_to_read; i++)
		{
			off_t index = start + i;
			if (index > end_index)
				break;

			block_sector_t sector = find_sector (inode, index);
			struct cache *c = cache_find (sector);
			if (c != NULL)
				nr_sectors = 0;
			else
				{
					c = cache_get (sector);
					/* Set a readahead flag to the first extra sector in cache. */
					if (i == nr_to_read - lookahead_size)
						cache_set_readahead (sector);
					c->in_use = false;
					nr_sectors++;
				}
		}

out:
	return nr_sectors;
}

/*
 * A minimal readahead algorithm for trivial sequential/random reads.
 */
static unsigned int 
ondemand_readahead (const struct inode *inode, struct inode_ra_state *ra_state,
		bool hit_readahead_marker, off_t offset, unsigned int req_size)
{
	unsigned int max_pages = ra_state->ra_pages;

	/* If the program reads from the head of the file,
	 * assume sequential access and initialize the readahead window.
	 * */
	if (!offset)
		goto initial_readahead;

	/*
	 * It's the expected offset, assume sequential access.
	 * Ramp up sizes, push forward the readahead window,
	 * and do asynchronous readahead operation in the background.
	 */
	if (((unsigned int) offset == (ra_state->start + ra_state->size - ra_state->async_size)
	  || (unsigned int) offset == (ra_state->start + ra_state->size)))
		{
			ra_state->start += ra_state->size;
			ra_state->size = get_next_ra_size	(ra_state, max_pages);
			ra_state->async_size = ra_state->size;
			goto readit;
		}

	/*
	 * Hit a marked page without valid readahead state such as interleaved reads.
	 * Query the pagecache for async_size which normally equals to readahead size. 
	 * Ramp it up and use it as the new readahead size.
	 */
	if (hit_readahead_marker)
		{
			off_t start = next_miss (inode, offset + 1, max_pages);
			if ((unsigned int) (start - offset) > max_pages)
				return 0;

			ra_state->start = start;
			ra_state->size = start - offset;
			ra_state->size += req_size;
			ra_state->size = get_next_ra_size (ra_state, max_pages);
			ra_state->async_size = ra_state->size;
			goto readit;
		}

	/*
	 * If the program reads a lot of data, assume sequential access.
	 */
	if (req_size > max_pages)
		goto initial_readahead;

	/*
	 * If the program reads the same index or the subsequent index,
	 * assume sequential access.
	 * trivial case: (offset - prev_offset) == 1
	 * unaligned reads: (offset - prev_offset) == 0
	 */
	if (offset - (ra_state->prev_pos >> BLOCK_SECTOR_SHIFT) <= 1)
		goto initial_readahead;

	/*
	 * Otherwise assume random access.
	 * Read as is, and do not pollute the readahead state.
	 */
	return do_cache_readahead (inode, offset, req_size, 0);

initial_readahead:
	ra_state->start = offset;
	ra_state->size = get_init_ra_size (req_size, max_pages);
	ra_state->async_size = ra_state->size > req_size ? ra_state->size - req_size : ra_state->size;

readit:
	/*
	 * Will this read hit the readahead marker made by itself?
	 * If so, trigger the readahead marker hit now, and merge
	 * the resulted next readahead window into the current one.
	 */
	if (offset == ra_state->start && ra_state->size == ra_state->async_size)
		{
			unsigned int add_pages = get_next_ra_size(ra_state, max_pages);
			if (ra_state->size + add_pages <= max_pages)
				{
					ra_state->async_size = add_pages;
					ra_state->size += add_pages;
				}
			else
				{
					ra_state->size = max_pages;
					ra_state->async_size = max_pages >> 1;
				}
		}

	return do_cache_readahead (inode, ra_state->start, ra_state->size, ra_state->async_size);
}

static void
cache_sync_readahead (const struct inode *inode, struct inode_ra_state *ra_state,
		off_t offset, unsigned int req_size)
{
	if (ra_state->ra_pages == 0)
		return;

	ondemand_readahead (inode, ra_state, false, offset, req_size);
}

static void
cache_async_readahead (const struct inode *inode, struct inode_ra_state *ra_state, 
		block_sector_t sector, off_t offset, unsigned int req_size)
{
	if (ra_state->ra_pages == 0)
		return;

	cache_clear_readahead (sector);
	
	ondemand_readahead (inode, ra_state, true, offset, req_size);
}
