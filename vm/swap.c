#include "vm/swap.h"
#include <bitmap.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* Size of a swap slot. */
#define SWAP_SLOT_SIZE (PGSIZE / BLOCK_SECTOR_SIZE)

/* Partition that contains the swap space. */
static struct block *swap_device;
static struct bitmap *free_map; /* Free map, one bit per sector. */
static struct lock swap_lock;

void
swap_init ()
{
	swap_device = block_get_role (BLOCK_SWAP);
	if (swap_device == NULL)
		PANIC ("No swap space device found, can't initialize swap space.");

	free_map = bitmap_create (block_size (swap_device) / SWAP_SLOT_SIZE);
	if (free_map == NULL)
		PANIC ("Bitmap creation failed--swap space device is too large.");

	lock_init (&swap_lock);
}

swap_index_t
swap_store (void *kpage)
{
	lock_acquire (&swap_lock);
	swap_index_t swap_index = bitmap_scan_and_flip (free_map, 0, 1, false);
	lock_release (&swap_lock);
	if (swap_index == BITMAP_ERROR)
		return swap_index;

	int i;
	for (i = 0; i < SWAP_SLOT_SIZE; i++)
		block_write (swap_device, swap_index * SWAP_SLOT_SIZE + i, kpage + i * BLOCK_SECTOR_SIZE);
  return swap_index;
}

void
swap_load (void *kpage, swap_index_t swap_index)
{
	int i;
	for (i = 0; i < SWAP_SLOT_SIZE; i++)
		block_read (swap_device, swap_index * SWAP_SLOT_SIZE + i, kpage + i * BLOCK_SECTOR_SIZE);
	lock_acquire (&swap_lock);
  bitmap_set (free_map, swap_index, false);
	lock_release (&swap_lock);
}

void
swap_free (swap_index_t swap_index)
{
	lock_acquire (&swap_lock);
  bitmap_set (free_map, swap_index, false);
	lock_release (&swap_lock);
}
