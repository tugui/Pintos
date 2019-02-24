#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdint.h>

typedef uint32_t swap_index_t;

void swap_init (void);
swap_index_t swap_store (void *kpage);
void swap_load (void *kpage, swap_index_t swap_index);
void swap_free (swap_index_t swap_index);

#endif
