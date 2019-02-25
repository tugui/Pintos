#include "userprog/mmap.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/page.h"

mapid_t
add_mmapfile (struct list *mmapfiles, struct file *f, void *addr, off_t read_bytes)
{
	struct mmapfile *mf = MALLOC (1, struct mmapfile);
	if (mf == NULL)
		return MAPID_ERROR;

	if (list_empty (mmapfiles))
			mf->mapid = 1;
	else
		{
			struct list_elem *e = list_end (mmapfiles);
      struct mmapfile *mf_ = list_entry (e, struct mmapfile, elem);
			mf->mapid = mf_->mapid + 1;
		}

	mf->file = f;
	mf->addr = addr;
	mf->size = (read_bytes - 1) / PGSIZE + 1;

	off_t ofs = 0;
	void *upage = addr;
	while (read_bytes > 0)
		{
			size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;

			/* Add an mapfile entry to supplemental page table */
			if (!page_add_mapfile (f, ofs, upage, page_read_bytes))
				{
					free (mf);
					return MAPID_ERROR;
				}

			/* Advance. */
			read_bytes -= page_read_bytes;
			ofs += page_read_bytes;
			upage += PGSIZE;
		}

	list_push_back (mmapfiles, &mf->elem);
	return mf->mapid;
}

void
free_mmapfile (struct hash *h, struct mmapfile *mf)
{
	struct thread *cur = thread_current ();
	void *upage = mf->addr;
	unsigned int i;
	for (i = 1; i <= mf->size; i++)
		{
			struct page *p = page_delete (h, upage);
			if (p && p->loaded)
				{
					void *kpage = pagedir_get_page (cur->pagedir, upage);
					if (kpage != NULL)
						{
							if (pagedir_is_dirty (cur->pagedir, upage))
								{
									file_seek (p->source.mmapfile.handle, p->source.mmapfile.ofs);
									file_write (p->source.mmapfile.handle, kpage, p->source.mmapfile.read_bytes);
								}
							pagedir_clear_page (cur->pagedir, upage);
							frame_free (kpage);
						}
				}
			free (p);
			upage += PGSIZE;
		}
	file_close (mf->file);
}

void
free_mmapfiles ()
{
	struct thread *cur = thread_current ();
  while (!list_empty (&cur->mmapfiles))
		{
			struct list_elem *e = list_pop_front (&cur->mmapfiles);
			struct mmapfile *mf = list_entry (e, struct mmapfile, elem);
			free_mmapfile (&cur->pages, mf);
			free (mf);
		}
}
