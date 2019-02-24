#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <hash.h>
#include <stdint.h>
#include "threads/palloc.h"

struct frame
	{
		void *kpage; /* Physical memory can be accessed through kernel virtual memory. */
		void *upage;
		struct thread *t; /* User process that obtains it. */
		size_t size; /* Number of pages. */
		struct hash_elem hash_elem;
		struct list_elem list_elem;
	};

void frame_init (void);
void *frame_get (enum palloc_flags flags);
void *frame_get_multiple (enum palloc_flags flags, size_t page_cnt);
void frame_free (void *);
bool frame_save (struct frame *);
struct frame *frame_evict (void);
struct frame *frame_find (struct hash *h, void *kpage);

#endif /* vm/frame.h */
