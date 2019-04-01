#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "filesys/file.h"
#include "vm/swap.h"

/* Where does the page come from. */
enum page_position
  {
		PAGE_STACK	  =  0x01,
		PAGE_FILE			=  0x02,
		PAGE_MMAPFILE =  0x04,
		PAGE_SWAP			=  0x08,
  };

union page_source
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
		union page_source source;
		enum page_position position;
		swap_slot_t swap_slot;
		bool loaded;
		struct hash_elem elem;
	};

bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux);
unsigned page_hash (const struct hash_elem *p_, void *aux);
struct page *page_find (struct hash *h, void *upage);
struct page *page_delete (struct hash *h, void *upage);
void free_pages (struct hash *h);
bool load_page (struct page *p);

bool page_add_file (struct file *, off_t, uint8_t *, uint32_t, uint32_t, bool);
bool page_add_mapfile (struct file *, off_t, uint8_t *, uint32_t);
bool page_add_stack (uint8_t *upage);

#endif /* vm/page.h */
