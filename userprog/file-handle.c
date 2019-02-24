#include "userprog/file-handle.h"
#include <bitmap.h> 
#include <errno.h>
#include <log2.h>
#include <string.h>
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

static int expand_fdtable (struct files_handler *files, unsigned int size);
static struct fdtable * allocate_fdtable (unsigned int size);
static void copy_fdtable (struct fdtable *new_fdt, struct fdtable *old_fdt);
static void free_fdtable (struct fdtable *fdt);

bool
is_open (int fd)
{
	struct files_handler *files = thread_current ()->files;
	if (fd < 0 || (unsigned int) fd >= files->fdt->max_fds)
		return false;
	return bitmap_test (files->fdt->fd_map, fd);
}

bool
fd_install (unsigned int fd, struct file *f)
{
	struct files_handler *files = thread_current ()->files;
	struct fdtable *fdt = files->fdt;
	lock_acquire (&files->file_lock);
	if (fdt->fd[fd])
		return false;
	fdt->fd[fd] = f;
	lock_release (&files->file_lock);
	return true;
}

/*
 * Expand files.
 * Return <0 error code on error;
 * 0 when nothing done;
 * 1 when files were expanded and execution may have blocked.
 */
int
expand_files (struct files_handler *files, unsigned int size)
{
	struct fdtable *fdt = files->fdt;
	if (size < fdt->max_fds)
		return 0; /* Don't need to expand. */
	return expand_fdtable (files, size);
}

int
allocate_fd (void)
{
	struct files_handler *files = thread_current ()->files;
	unsigned int fd = 0;

repeat:
	if (!lock_held_by_current_thread (&files->file_lock))
		lock_acquire (&files->file_lock);
	struct fdtable *fdt = files->fdt;
	unsigned int start = files->next_fd;

	if (start < fdt->max_fds)
		fd = bitmap_find_next_bit (fdt->fd_map, start, false);
	else
		fd = start;

	int error = expand_files (files, fd);
	/* If error is 0, the table is stable and the operation is atomic. */
	if (error < 0)
		goto out;
	else if (error)
		/* When the table is expanded, we have to assign a new fd again.
		 * Because this fd may be used by another thread which is also expanding the table in multithreaded environment. */
		goto repeat;

	files->next_fd = fd + 1;
	bitmap_set (fdt->fd_map, fd, true);
	error = fd;

out:
	lock_release (&files->file_lock);
	return error;
}

/*
 * Expand the file descriptor table.
 * Return <0 error code on error;
 * 1 on successful completion.
 */
static int
expand_fdtable (struct files_handler *files, unsigned int size)
{
	lock_release (&files->file_lock);
	struct fdtable *new_fdt = allocate_fdtable (size);
	lock_acquire (&files->file_lock);
	if (!new_fdt)
		return -ENOMEM;

	if (new_fdt->max_fds <= size)
		{
			free_fdtable (new_fdt);
			return -EMFILE;
		}

	struct fdtable *cur_fdt = files->fdt;
	if (new_fdt->max_fds > cur_fdt->max_fds)
		{
			copy_fdtable (new_fdt, cur_fdt);
			files->fdt = new_fdt;
			if (cur_fdt->max_fds > OPEN_DEFAULT)
				free_fdtable (cur_fdt);
		}
	else
		{
		/* Somebody else expanded, so undo our attempt */
			free_fdtable(new_fdt);
		}
	return 1;
}

static struct
fdtable * allocate_fdtable (unsigned int size)
{
	size /= (1024 / sizeof (struct file *));
	size = roundup_pow_of_two (size + 1);
	size *= (1024 / sizeof (struct file *));

	struct fdtable *fdt = malloc (sizeof (struct fdtable));
	if (!fdt)
		goto out;
	fdt->max_fds = size;

	struct file **new_fd = calloc (size, sizeof (struct file *));
	if (!new_fd)
		goto out_fdt;
	fdt->fd = new_fd;

	struct bitmap *new_fd_map = bitmap_create (size);
	if (!new_fd_map)
		goto out_fd;
	fdt->fd_map = new_fd_map; 

	return fdt;

out_fd:
	free (fdt->fd);
out_fdt:
	free (fdt);
out:
	return NULL;
}

static void
copy_fdtable (struct fdtable *new_fdt, struct fdtable *old_fdt)
{
	unsigned int cpy;

	ASSERT (new_fdt->max_fds > old_fdt->max_fds);

	cpy = old_fdt->max_fds * sizeof (struct file *);
	memcpy (new_fdt->fd, old_fdt->fd, cpy);

	cpy = old_fdt->max_fds / BITS_PER_BYTE;
	memcpy (new_fdt->fd_map->bits, old_fdt->fd_map->bits, cpy);
}

static void
free_fdtable (struct fdtable *fdt)
{
	free (fdt->fd);
	bitmap_destroy (fdt->fd_map);
	free (fdt);
}

static void
close_files (struct files_handler *files)
{
	unsigned int i, j = 0;
	struct fdtable *fdt = files->fdt;

	for (;;)
		{
			elem_type set;
			i = j * NFDBITS;
			if (i >= fdt->max_fds)
				break;
			set = fdt->fd_map->bits[j++];

			/* Check every bit in a set from right-hand side. */
			while (set)
				{
					if (set & 1)
						{
							struct file * file = fdt->fd[i];
							if (file)
								{
									file_close (file);
									fdt->fd[i] = NULL;
									thread_yield (); // this function may take time
								}
						}
					i++;
					set >>= 1;
				}
		}
}

void
free_files_handler (struct files_handler *files)
{
	if (files)
		{
			close_files (files);

			/* Free the fd and fdset arrays if we expanded them. */
			struct fdtable *fdt = files->fdt;
			if (fdt != &files->fdtab)
				free_fdtable (fdt);
			else
				bitmap_destroy (fdt->fd_map);
		}
}
