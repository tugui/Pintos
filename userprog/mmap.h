#include <hash.h>
#include <list.h>
#include "filesys/file.h"

typedef int mapid_t;
#define MAPID_ERROR ((mapid_t) -1)      /* Error value for mapid_t. */

struct mmapfile
{
	mapid_t mapid;
	void *addr;
	size_t size; // from bottom to up in page size
	struct file *file;
	struct list_elem elem;
};

mapid_t add_mmapfile (struct list *mmapfiles, struct file *f, void *addr, off_t read_bytes);
void free_mmapfile (struct hash *h, struct mmapfile *mf);
void free_mmapfiles (void);
