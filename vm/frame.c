#include "vm/frame.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/swap.h"

struct hash frames;
struct list active_frames;
struct list inactive_frames;

static struct lock frame_lock;

static struct frame *frame_delete (struct hash *h, void *kpage);
static unsigned frame_hash (const struct hash_elem *f_, void *aux UNUSED);
static bool frame_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);

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
			struct frame *f = malloc (sizeof (struct frame));
			if (f != NULL)
				{
					f->kpage = kpage;
					f->upage = NULL;
					f->t = thread_current ();
					f->size = page_cnt;
					hash_insert (&frames, &f->hash_elem);
					lock_acquire (&frame_lock);
					list_push_back (&active_frames, &f->list_elem);
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
			list_push_back (&active_frames, &f->list_elem);
			lock_release (&frame_lock);
			return f->kpage;
		}
	return NULL;
}

void
frame_free (void *kpage)
{
	struct frame *f = frame_delete (&frames, kpage);
	if (f != NULL)
		{
			palloc_free_multiple (kpage, f->size);
			lock_acquire (&frame_lock);
			list_remove (&f->list_elem);
			lock_release (&frame_lock);
			free (f);
		}
}

struct frame *
frame_evict (void)
{
	struct frame *evictor = NULL;
	lock_acquire (&frame_lock);
	while (!list_empty (&inactive_frames))
		{
			struct list_elem *e = list_pop_front (&inactive_frames);
			struct frame *f = list_entry (e, struct frame, list_elem);
			if (pagedir_is_accessed (f->t->pagedir, f->upage))
				list_push_back (&active_frames, e);
			else
				{
					bool success = frame_save (f);
					if (success)
						{
							evictor = f;
							break;
						}
				}
		}

	if (evictor == NULL)
		{
			struct list_elem *e;
			for (e = list_begin (&active_frames); e != list_end (&active_frames);
					 e = list_next (e))
				{
					struct frame *f = list_entry (e, struct frame, list_elem);
					bool success = frame_save (f);
					if (success)
						{
							list_remove (e);
							evictor = f;
							break;
						}
				}
		}

	/* Keep the size in a certain range for better eviction candidates. */
	int size = list_size (&inactive_frames);
	while (size < 10)
		{
			struct list_elem *e = list_pop_front (&active_frames);
			struct frame *f = list_entry (e, struct frame, list_elem);
			pagedir_set_accessed (f->t->pagedir, f->upage, false);
			list_push_back (&inactive_frames, e);
			size++;
		}
	lock_release (&frame_lock);
	return evictor;
}

bool
frame_save (struct frame *f)
{
	struct page *p = page_find (&f->t->pages, f->upage);
	if (p == NULL || !p->loaded)
		return false;

	if (p->position & FILE || p->position & STACK)
		{
			swap_index_t swap_index = swap_store (f->kpage);
			if (swap_index == BITMAP_ERROR)
				return false;

			p->swap_index = swap_index;
			p->position |= SWAP;
		}
	else if (p->position & MMAPFILE
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
frame_find (struct hash *h, void *kpage)
{
	struct frame f;
	f.kpage = kpage;
	struct hash_elem *e = hash_find (h, &f.hash_elem);
	return e != NULL ? hash_entry (e, struct frame, hash_elem) : NULL;
}

static struct frame * 
frame_delete (struct hash *h, void *kpage)
{
	struct frame f;
	f.kpage = kpage;
	struct hash_elem *e = hash_delete (h, &f.hash_elem);
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
