#include "filesys/cache.h"
#include <string.h>
#include "devices/timer.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/thread.h"

static struct hash caches;
static struct list cache_list;
static struct lock cache_lock;

static struct cache *cache_evict (void);
static struct cache *cache_delete (block_sector_t sector);
static unsigned cache_hash (const struct hash_elem *c_, void *aux UNUSED);
static bool cache_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
static void write_behind (void *aux UNUSED);

void
cache_init ()
{
	lock_init (&cache_lock);
	list_init (&cache_list);
	hash_init (&caches, cache_hash, cache_less, NULL);
	thread_create ("cache-write-behind", PRI_DEFAULT, write_behind, NULL);
}

struct cache *
cache_get (block_sector_t sector)
{
	lock_acquire (&cache_lock);
	struct cache *c = cache_find (sector);
	if (c != NULL)
		{
			list_remove (&c->list_elem);	
			list_push_back (&cache_list, &c->list_elem);
			c->in_use = true;
			lock_release (&cache_lock);
			return c;	
		}
	lock_release (&cache_lock);

	size_t size = hash_size (&caches);
	if (size < CACHE_SIZE)
		{
			struct cache *c = MALLOC (1, struct cache);
			if (c != NULL)
				{
					void *data = malloc (BLOCK_SECTOR_SIZE);
					if (data == NULL)
						{
							free (c);
							return NULL;
						}
					c->data = data;
					c->dirty = false;
					c->in_use = true;
					c->readahead = false;
					c->sector = sector;
					c->t = thread_current ();
					block_read (fs_device, c->sector, c->data);	
					hash_insert (&caches, &c->hash_elem);
					lock_acquire (&cache_lock);
					list_push_back (&cache_list, &c->list_elem);
					lock_release (&cache_lock);
					return c;
				}
		}
	else
		{
			/* Try to get cache by eviction. */
			struct cache *c = cache_evict ();
			if (c != NULL)
				{
					c->dirty = false;
					c->in_use = true;
					c->readahead = false;
					c->sector = sector;
					c->t = thread_current ();
					block_read (fs_device, c->sector, c->data);
					hash_insert (&caches, &c->hash_elem);
					lock_acquire (&cache_lock);
					list_push_back (&cache_list, &c->list_elem);
					lock_release (&cache_lock);
					return c;
				}
		}
	return NULL;
}

void
cache_free (block_sector_t sector)
{
	struct cache *c = cache_delete (sector);
	if (c != NULL)
		{
			lock_acquire (&cache_lock);
			list_remove (&c->list_elem);
			lock_release (&cache_lock);
			if (c->dirty)
				block_write (fs_device, c->sector, c->data);
			free (c->data);
			free (c);
		}
}

void
cache_clear ()
{
	lock_acquire (&cache_lock);
	while (!list_empty (&cache_list))
		{
			struct list_elem *e = list_pop_front (&cache_list);
			struct cache *c = list_entry (e, struct cache, list_elem);
			if (c->dirty)
				block_write (fs_device, c->sector, c->data);
			hash_delete (&caches, &c->hash_elem);
			free (c->data);
			free (c);
		}
	lock_release (&cache_lock);
}

void
free_cache (struct thread *t)
{
	lock_acquire (&cache_lock);
	struct list_elem *e = list_begin (&cache_list);
	while (e != list_end (&cache_list))
		{
			struct cache *c = list_entry (e, struct cache, list_elem);
			if (c->t == t)
				{
					e = list_prev (e);
					if (c->dirty)
						block_write (fs_device, c->sector, c->data);
					list_remove (&c->list_elem);
					hash_delete (&caches, &c->hash_elem);
					free (c->data);
					free (c);
				}
			e = list_next (e);
		}
	lock_release (&cache_lock);
}

static struct cache *
cache_evict (void)
{
	struct list_elem *e;
	struct cache *evictor = NULL;
	lock_acquire (&cache_lock);
	for (e = list_begin (&cache_list); e != list_end (&cache_list);
			 e = list_next (e))
		{
			struct cache *c = list_entry (e, struct cache, list_elem);
			if (!c->in_use)
				{
					if (c->dirty)
						block_write (fs_device, c->sector, c->data);
					hash_delete (&caches, &c->hash_elem);
					list_remove (e);
					evictor = c;
					break;
				}
		}
	lock_release (&cache_lock);
	return evictor;
}

struct cache * 
cache_find (block_sector_t sector)
{
	struct cache c;
	c.sector = sector;
	struct hash_elem *e = hash_find (&caches, &c.hash_elem);
	return e != NULL ? hash_entry (e, struct cache, hash_elem) : NULL;
}

void
cache_read (block_sector_t sector, void *buffer, int offset, int size)
{
	struct cache *c = cache_get (sector);
	memcpy (buffer, c->data + offset, size);
	c->in_use = false;
}

void
cache_write (block_sector_t sector, const void *buffer, int offset, int size)
{
	struct cache *c = cache_get (sector);
	memcpy (c->data + offset, buffer, size);
	c->dirty = true;
	c->in_use = false;
}

uint32_t
cache_read_at (block_sector_t sector, off_t pos)
{
	struct cache *c = cache_get (sector);
	uint32_t value = *((uint32_t *)(c->data + pos));
	c->in_use = false;
	return value;
}

void
cache_write_at (block_sector_t sector, off_t pos, uint32_t value)
{
	struct cache *c = cache_get (sector);
	*((uint32_t *)(c->data + pos)) = value;
	c->dirty = true;
	c->in_use = false;
}

void
cache_set (block_sector_t sector, int value, int offset, int size)
{
	struct cache *c = cache_get (sector);
	memset (c->data + offset, value, size);
	c->dirty = true;
	c->in_use = false;
}

static struct cache * 
cache_delete (block_sector_t sector)
{
	struct cache c;
	c.sector = sector;
	struct hash_elem *e = hash_delete (&caches, &c.hash_elem);
	return e != NULL ? hash_entry (e, struct cache, hash_elem) : NULL;
}

/* Returns a hash value for cache c. */
static unsigned
cache_hash (const struct hash_elem *c_, void *aux UNUSED)
{
  const struct cache *c = hash_entry (c_, struct cache, hash_elem);
  return hash_int (c->sector);
}

/* Returns true if cache a precedes cache b. */
static bool
cache_less (const struct hash_elem *a_, const struct hash_elem *b_,
    void *aux UNUSED)
{
  const struct cache *a = hash_entry (a_, struct cache, hash_elem);
  const struct cache *b = hash_entry (b_, struct cache, hash_elem);

  return a->sector < b->sector;
}

static inline void
cache_flush (void)
{
	lock_acquire (&cache_lock);
	struct list_elem *e;
	for (e = list_begin (&cache_list); e != list_end (&cache_list);
			 e = list_next (e))
		{
			struct cache *c = list_entry (e, struct cache, list_elem);
			if (c->dirty)
				{
					block_write (fs_device, c->sector, c->data);
					c->dirty = false;
				}
		}
	lock_release (&cache_lock);
}

static void
write_behind (void *aux UNUSED)
{
	while (1)
		{
			timer_sleep (30);
			cache_flush ();
		}
}

bool
cache_readahead (block_sector_t sector)
{
	struct cache *c = cache_find (sector);
	if (c != NULL)
		return c->readahead;
	return false;
}

void
cache_set_readahead (block_sector_t sector)
{
	struct cache *c = cache_find (sector);
	if (c != NULL)
		c->readahead = true;
}

void
cache_clear_readahead (block_sector_t sector)
{
	struct cache *c = cache_find (sector);
	if (c != NULL)
		c->readahead = false;
}
