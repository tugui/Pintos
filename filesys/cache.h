#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <hash.h>
#include "devices/block.h"
#include "filesys/off_t.h"

#define CACHE_SIZE 64

struct cache
	{
		block_sector_t sector;
		void *data;
		bool dirty;
		bool in_use; /* While the cache is being used, it cannot be evicted. */
		bool readahead;
		struct thread *t; /* User process that is using it. */
		struct hash_elem hash_elem;
		struct list_elem list_elem;
	};

void cache_init (void);
void cache_clear (void);
void cache_free (block_sector_t sector);
void free_cache (struct thread *t);
struct cache *cache_get (block_sector_t sector);
struct cache *cache_find (block_sector_t sector);

void cache_read (block_sector_t sector, void *buffer, int offset, int size);
void cache_write (block_sector_t sector, const void *buffer, int offset, int size);
uint32_t cache_read_at (block_sector_t sector, off_t pos);
void cache_write_at (block_sector_t sector, off_t pos, uint32_t value);
void cache_set (block_sector_t sector, int value, int offset, int size);

bool cache_readahead (block_sector_t sector);
void cache_set_readahead (block_sector_t sector);
void cache_clear_readahead (block_sector_t sector);

#endif /* filesys/cache.h */
