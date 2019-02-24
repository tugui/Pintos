#include "vm/page.h"
#include <string.h>
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "vm/frame.h"
#include "vm/swap.h"

static bool load_from_file (struct page *p);
static bool load_from_mapfile (struct page *p);
static bool load_from_swap (struct page *p);
static void page_free (struct hash_elem *e, void *aux);

/* Returns the page containing the given virtual address,
   or a null pointer if no such page exists. */
struct page *
page_find (struct hash *h, void *upage)
{
  struct page p;
  p.upage = upage;
  struct hash_elem *e = hash_find (h, &p.elem);
  return e != NULL ? hash_entry (e, struct page, elem) : NULL;
}

struct page *
page_delete (struct hash *h, void *upage)
{
  struct page p;
  p.upage = upage;
  struct hash_elem *e = hash_delete (h, &p.elem);
  return e != NULL ? hash_entry (e, struct page, elem) : NULL;
}

/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux)
{
  const struct page *p = hash_entry (p_, struct page, elem);
  return hash_bytes (&p->upage, sizeof p->upage);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux)
{
  const struct page *a = hash_entry (a_, struct page, elem);
  const struct page *b = hash_entry (b_, struct page, elem);

  return a->upage < b->upage;
}

static void
page_free (struct hash_elem *e, void *aux)
{
	struct page *p = hash_entry (e, struct page, elem);
	if (p->position & SWAP)
		swap_free (p->swap_index);
	free (p);
}

void
free_pages (struct hash *h)
{
	hash_destroy (h, page_free);
}

bool
load_page (struct page *p)
{
	bool success = false;
	switch (p->position)
		{
		case FILE:
			success = load_from_file (p);
			break;
		case MMAPFILE:
			success = load_from_mapfile (p);
			break;
		case SWAP | FILE:
		case SWAP | STACK:
			success = load_from_swap (p);
			break;
		default:
			break;
		}
	return success;
}

static bool
load_from_file (struct page *p)
{
	/* Get a page of memory. */
	uint8_t *kpage = frame_get (PAL_USER);
	if (kpage == NULL)
		return false;

	/* Load this page. */
	file_seek (p->source.file.handle, p->source.file.ofs);
	if (file_read (p->source.file.handle, kpage, p->source.file.read_bytes) != (int) p->source.file.read_bytes)
		{
			frame_free (kpage);
			return false; 
		}
	memset (kpage + p->source.file.read_bytes, 0, p->source.file.zero_bytes);

	/* Add the page to the process's address space. */
	if (!install_page (p->upage, kpage, p->source.file.writable)) 
		{
			frame_free (kpage);
			return false; 
		}
	p->loaded = true;
	return true;
}

static bool
load_from_mapfile (struct page *p)
{
	/* Get a page of memory. */
	uint8_t *kpage = frame_get (PAL_USER);
	if (kpage == NULL)
		return false;

	/* Load this page. */
	file_seek (p->source.mmapfile.handle, p->source.mmapfile.ofs);
	if (file_read (p->source.mmapfile.handle, kpage, p->source.mmapfile.read_bytes) != (int) p->source.mmapfile.read_bytes)
		{
			frame_free (kpage);
			return false; 
		}
	
	if (p->source.mmapfile.read_bytes < PGSIZE)
		memset (kpage + p->source.mmapfile.read_bytes, 0, PGSIZE - p->source.file.read_bytes);

	/* Add the page to the process's address space. */
	if (!install_page (p->upage, kpage, true)) 
		{
			frame_free (kpage);
			return false; 
		}
	p->loaded = true;
	return true;
}

static bool
load_from_swap (struct page *p)
{
	/* Get a page of memory. */
	uint8_t *kpage = frame_get (PAL_USER);
	if (kpage == NULL)
		return false;

	/* Load this page. */
	swap_load (kpage, p->swap_index);

	/* Add the page to the process's address space. */
	if (!install_page (p->upage, kpage, true)) 
		{
			frame_free (kpage);
			return false; 
		}

	if (p->position == (FILE | SWAP))
    {
      p->position = FILE;
			p->loaded = true;
    }
	else if (p->position == (STACK | SWAP))
    {
      p->position = STACK;
			p->loaded = true;
    }
	return true;
}

bool
page_add_file (struct file *file, off_t ofs, uint8_t *upage, uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	struct thread *t = thread_current ();
	struct page *p = malloc (sizeof (struct page));
	if (p == NULL)
		return false;

	p->upage = upage;
	p->position = FILE;
	p->source.file.handle = file;
	p->source.file.ofs = ofs;
	p->source.file.read_bytes = read_bytes;
	p->source.file.zero_bytes = zero_bytes;
	p->source.file.writable = writable;
	p->loaded = false;

	struct hash_elem *e = hash_insert (&t->pages, &p->elem);
	if (e != NULL)
		{
			free (p);
			return false;
		}
	return true;
}

bool
page_add_mapfile (struct file *file, off_t ofs, uint8_t *upage, uint32_t read_bytes)
{
	struct thread *t = thread_current ();
	struct page *p = malloc (sizeof (struct page));
	if (p == NULL)
		return false;

	p->upage = upage;
	p->position = MMAPFILE;
	p->source.mmapfile.handle = file;
	p->source.mmapfile.ofs = ofs;
	p->source.mmapfile.read_bytes = read_bytes;
	p->loaded = false;

	struct hash_elem *e = hash_insert (&t->pages, &p->elem);
	if (e != NULL)
		{
			free (p);
			return false;
		}
	return true;
}

bool
page_add_stack (uint8_t *upage)
{
	struct thread *t = thread_current ();
	struct page *p = malloc (sizeof (struct page));
	if (p == NULL)
		return false;

	p->upage = upage;
	p->position = STACK;
	p->loaded = true;

	struct hash_elem *e = hash_insert (&t->pages, &p->elem);
	if (e != NULL)
		{
			free (p);
			return false;
		}
	return true;
}
