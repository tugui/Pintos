#include "vm/frame.h"
#include "devices/timer.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/swap.h"

static struct hash frames;
static struct list active_frames;
static struct list inactive_frames;
static struct lock frame_lock;
static unsigned int nr_active = 0;
static unsigned int nr_inactive = 0;

static struct frame *frame_delete (void *kpage);
static unsigned frame_hash (const struct hash_elem *f_, void *aux UNUSED);
static bool frame_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
static inline void shrink_active_list (void);

void
frame_init ()
{
	hash_init (&frames, frame_hash, frame_less, NULL);
	list_init (&active_frames);
	list_init (&inactive_frames);
	lock_init (&frame_lock);
}

void *
frame_get (enum palloc_flags flags)
{
	return frame_get_multiple (flags, 1);
}

void *
frame_get_multiple (enum palloc_flags flags, size_t page_cnt)
{
	void *kpage = palloc_get_multiple (flags, page_cnt);
	if (kpage != NULL && flags & PAL_USER)
		{
			struct frame *f = MALLOC (1, struct frame);
			if (f != NULL)
				{
					f->kpage = kpage;
					f->upage = NULL;
					f->t = thread_current ();
					f->size = page_cnt;
					hash_insert (&frames, &f->hash_elem);
					lock_acquire (&frame_lock);
					f->active = true;
					list_push_back (&active_frames, &f->list_elem);
					nr_active++;
					lock_release (&frame_lock);
					return f->kpage;
				}
		}
	else if (flags & PAL_USER)
		{
			/* Try to get pages by eviction. */
			struct frame *f = frame_evict ();
			f->upage = NULL;
			f->t = thread_current ();
			f->size = 1;
			lock_acquire (&frame_lock);
			f->active = true;
			list_push_back (&active_frames, &f->list_elem);
			nr_active++;
			lock_release (&frame_lock);
			return f->kpage;
		}
	return NULL;
}

void
frame_free (void *kpage)
{
	struct frame *f = frame_delete (kpage);
	if (f != NULL)
		{
			palloc_free_multiple (kpage, f->size);
			if (f->active)
				nr_active--;
			else
				nr_inactive--;
			lock_acquire (&frame_lock);
			list_remove (&f->list_elem);
			lock_release (&frame_lock);
			free (f);
		}
}

static bool
frame_save (struct frame *f)
{
	struct page *p = page_find (&f->t->pages, f->upage);
	if (p == NULL || !p->loaded)
		return false;

	if ((p->position & PAGE_FILE && p->source.file.writable) || p->position & PAGE_STACK)
		{
			swap_slot_t swap_slot = swap_store (f->kpage);
			if (swap_slot == BITMAP_ERROR)
				return false;

			p->swap_slot = swap_slot;
			p->position |= PAGE_SWAP;
		}
	else if (p->position & PAGE_MMAPFILE
		 	  && pagedir_is_dirty (f->t->pagedir, p->upage))
		{
			file_seek (p->source.mmapfile.handle, p->source.mmapfile.ofs);
			file_write (p->source.mmapfile.handle, f->kpage, p->source.mmapfile.read_bytes);
		}

	pagedir_clear_page (f->t->pagedir, p->upage);
	p->loaded = false;
	return true;
}

struct frame *
frame_evict (void)
{
	struct frame *evictor = NULL;
	lock_acquire (&frame_lock);
	while (!list_empty (&inactive_frames))
		{
			nr_inactive--;
			struct list_elem *e = list_pop_front (&inactive_frames);
			struct frame *f = list_entry (e, struct frame, list_elem);
			if (pagedir_is_accessed (f->t->pagedir, f->upage))
				{
					pagedir_set_accessed (f->t->pagedir, f->upage, false);
					f->active = true;
					list_push_back (&active_frames, e);
					nr_active++;
				}
			else if (frame_save (f))
				{
					evictor = f;
					break;
				}
		}

	if (evictor == NULL)
		{
			struct list_elem *e;
			for (e = list_begin (&active_frames); e != list_end (&active_frames);
					 e = list_next (e))
				{
					struct frame *f = list_entry (e, struct frame, list_elem);
					if (pagedir_is_accessed (f->t->pagedir, f->upage))
						pagedir_set_accessed (f->t->pagedir, f->upage, false);
					else if (frame_save (f))
						{
							nr_active--;
							list_remove (e);
							evictor = f;
							break;
						}
				}
		}

	if (evictor == NULL)
		{
			nr_active--;
			struct list_elem *e = list_pop_front (&active_frames);
			struct frame *f = list_entry (e, struct frame, list_elem);
			if (frame_save (f))
				evictor = f;
		}

	shrink_active_list ();

	lock_release (&frame_lock);
	return evictor;
}

struct frame * 
frame_find (void *kpage)
{
	struct frame f;
	f.kpage = kpage;
	struct hash_elem *e = hash_find (&frames, &f.hash_elem);
	return e != NULL ? hash_entry (e, struct frame, hash_elem) : NULL;
}

static struct frame * 
frame_delete (void *kpage)
{
	struct frame f;
	f.kpage = kpage;
	struct hash_elem *e = hash_delete (&frames, &f.hash_elem);
	return e != NULL ? hash_entry (e, struct frame, hash_elem) : NULL;
}

/* Returns a hash value for frame f. */
static unsigned
frame_hash (const struct hash_elem *f_, void *aux UNUSED)
{
  const struct frame *f = hash_entry (f_, struct frame, hash_elem);
  return hash_bytes (&f->kpage, sizeof f->kpage);
}

/* Returns true if frame a precedes frame b. */
static bool
frame_less (const struct hash_elem *a_, const struct hash_elem *b_,
    void *aux UNUSED)
{
  const struct frame *a = hash_entry (a_, struct frame, hash_elem);
  const struct frame *b = hash_entry (b_, struct frame, hash_elem);

  return a->kpage < b->kpage;
}

/* Keep the size in a certain range for better eviction candidates. */
static inline void
shrink_active_list ()
{
	while (nr_inactive < 10)
		{
			nr_active--;
			struct list_elem *e = list_pop_front (&active_frames);
			struct frame *f = list_entry (e, struct frame, list_elem);
			pagedir_set_accessed (f->t->pagedir, f->upage, false);
			f->active = false;
			list_push_back (&inactive_frames, e);
			nr_inactive++;
		}
}
