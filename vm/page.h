#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "filesys/file.h"
#include "vm/swap.h"

/* Where does the page come from. */
enum position
  {
		STACK	   =  1,
		FILE	   =  2,
		MMAPFILE =  4,
		SWAP	   =  8,
  };

union source
	{
		struct
			{
				void *handle; 
				off_t ofs;
				uint32_t read_bytes;
				uint32_t zero_bytes;
				bool writable;
			}	file;
		struct
			{
				void *handle; 
				off_t ofs;
				uint32_t read_bytes;
			}	mmapfile;
	};

struct page
	{
		void *upage;
		union source source;
		enum position position;
		swap_index_t swap_index;
		bool loaded;
		struct hash_elem elem;
	};

struct page *page_find (struct hash *h, void *upage);
struct page *page_delete (struct hash *h, void *upage);
unsigned page_hash (const struct hash_elem *p_, void *aux);
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux);
void free_pages (struct hash *h);
bool load_page (struct page *p);
bool page_add_file (struct file *, off_t, uint8_t *, uint32_t, uint32_t, bool);
bool page_add_mapfile (struct file *, off_t, uint8_t *, uint32_t);
bool page_add_stack (uint8_t *upage);

#endif /* vm/page.h */
