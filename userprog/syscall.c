#include "userprog/syscall.h"
#include <console.h>
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "userprog/file-handle.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/frame.h"
#include "vm/page.h"

static void syscall_handler (struct intr_frame *);
static bool is_valid_uddr (const void *vaddr);
static inline void put_unused_fd (struct files_handler *files, int fd);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
	uintptr_t *p = f->esp;
	// hex_dump(p, p, 128, true);

	/* Make sure all the arguments are in the user memory space even if they are unused. */
	if (!is_valid_uddr (p) || !is_valid_uddr (p + 1)
	 || !is_valid_uddr (p + 2) || !is_valid_uddr (p + 3)
	 || !is_valid_uddr (p + 4) || !is_valid_uddr (p + 5)
	 || !is_valid_uddr (p + 6))
		syscall_exit (-1);

	/* Because the arguments stored in the user stack are unordered and discontinuous, so we need to specify their positions. */
	int syscall_number = *p;
	// printf ("Name %s, No: %d\n", thread_name (), syscall_number);
	switch (syscall_number)
		{
		case SYS_HALT: // syscall0
			syscall_halt();
			break;
		case SYS_EXIT: // syscall1
			syscall_exit (*(p + 1));
			break;
		case SYS_EXEC:
			if (!is_valid_uddr ((char*) *(p + 1)))
				syscall_exit (-1);
			else
				f->eax = syscall_exec ((char*) *(p + 1));
			break;
		case SYS_WAIT:
			f->eax = syscall_wait (*(p + 1));
			break;
		case SYS_REMOVE:
			if (!is_valid_uddr ((char*) *(p + 1)))
				syscall_exit (-1);
			else
				f->eax = syscall_remove ((char *) *(p + 1));
			break;
		case SYS_OPEN:
			if (!is_valid_uddr ((char*) *(p + 1)))
				syscall_exit (-1);
			else
				f->eax = syscall_open ((char *) *(p + 1));
			break;
		case SYS_FILESIZE:
			f->eax = syscall_filesize (*(p + 1));
			break;
		case SYS_TELL:
			f->eax = syscall_tell (*(p + 1));
			break;
		case SYS_CLOSE:
			syscall_close (*(p + 1));
			break;
		case SYS_CREATE: // syscall2
			if (!is_valid_uddr ((char*) *(p + 4)))
				syscall_exit (-1);
			else
				f->eax = syscall_create ((char *) *(p + 4), *(p + 5));
			break;
		case SYS_SEEK:
			syscall_seek (*(p + 4), *(p + 5));
			break;
		case SYS_READ: // syscall3
			if (!is_user_vaddr ((void *) *(p + 6)))
				syscall_exit (-1);
			else
					f->eax = syscall_read (*(p + 2), (void *) *(p + 6), *(p + 3));
			break;
		case SYS_WRITE:
			if (!is_valid_uddr ((void *) *(p + 6)))
				syscall_exit (-1);
			else
				f->eax = syscall_write (*(p + 2), (void *) *(p + 6), *(p + 3));
			break;
		case SYS_MMAP:
			if (!is_user_vaddr ((void *) *(p + 5)))
				syscall_exit (-1);
			else
				f->eax = syscall_mmap (*(p + 4), (void *) *(p + 5));
			break;
		case SYS_MUNMAP:
				syscall_munmap (*(p + 1));
			break;
		default:
			break;
		}
}

void
syscall_halt (void)
{
	shutdown_power_off ();
}

void
syscall_exit (int status)
{
	struct thread *cur = thread_current ();
#ifdef VM
	free_mmapfiles ();
	free_pages (&cur->pages); 
#endif
	
	/* If its parent is alive and is waiting for it, set the return value and wake its parent up. */
	if (cur->self)
		{
			set_return_value (cur->self, status);
			if (cur->self->be_wait)
				sema_up (&cur->parent->child_wait);
		}

	printf ("%s: exit(%d)\n", cur->name, status);
	thread_exit ();
}

pid_t
syscall_exec (const char *cmd_line)
{
	pid_t pid = -1;
	tid_t tid = process_execute (cmd_line);
	if (tid == TID_ERROR)
		return pid;

	struct thread *cur = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&cur->children_list); e != list_end (&cur->children_list);
       e = list_next (e))
		{
			struct child *s = list_entry (e, struct child, elem);
			if (tid == s->tid)
				{
					if (!s->terminated)
						sema_down (&cur->child_load);

					if (s->pid > 0)
						pid = s->pid;
					else
						{
							list_remove (&s->elem);
							free (s);
						}
					break;
				}
		}
	return pid;
}

int
syscall_wait (pid_t pid)
{
	struct thread *cur = thread_current ();
  struct list_elem *e;
	int status = -1;
  for (e = list_begin (&cur->children_list); e != list_end (&cur->children_list);
       e = list_next (e))
		{
			struct child *s = list_entry (e, struct child, elem);
			if (s->pid == pid && !s->waited)
				{
					if (s->waited)
						return -1;
					if (!s->terminated)
						{
							s->be_wait = true;
							sema_down (&cur->child_wait);
						}
					s->waited = true;
					status = s->retval;
					break;
				}
		}
	return status;
}

bool
syscall_create (const char *file, off_t initial_size)
{
	return filesys_create (file, initial_size);
}

bool
syscall_remove (const char *file)
{
	return filesys_remove (file);
}

int
syscall_open (const char *file)
{
	struct file *f = filesys_open (file);
	if (f != NULL)
		{
			int fd = allocate_fd ();
			if (fd < 0 || !fd_install (fd, f))
				return -1;
			return fd + 2; // start from 2 because of reserved number 0 and 1
		}
	else
		return -1;
}

int
syscall_filesize (int fd)
{
	fd -= 2;
	if (!is_open (fd))
		return -1;
	struct thread *cur = thread_current ();
	return file_length (cur->files->fdt->fd[fd]);
}

int
syscall_read (int fd, void *buffer, off_t size)
{
	if (fd == STDIN_FILENO)
		{
			uint8_t key;
			uint8_t* p = (uint8_t*) buffer;

			int i;
			for (i = 0; i < size; i++)
				{
					key = input_getc ();
					p = &key;
					p++;
				}
			return size;
		}

	fd -= 2;
	if (!is_open (fd))
		return -1;
	struct thread *cur = thread_current ();
	return file_read (cur->files->fdt->fd[fd], buffer, size);
}

int
syscall_write (int fd, const void *buffer, off_t size)
{
	if (fd == STDOUT_FILENO)
		{
			if (size < OUTPUT_MAX)
				{
					putbuf (buffer, size);
					return size;
				}
			else
				{
					putbuf (buffer, OUTPUT_MAX);
					return OUTPUT_MAX;
				}
		}

	fd -= 2;
	if (!is_open (fd))
		return -1;
	struct thread *cur = thread_current ();
	return file_write (cur->files->fdt->fd[fd], buffer, size);
}

void
syscall_seek (int fd, off_t position)
{
	fd -= 2;
	if (!is_open (fd) || position < 0)
		return;
	struct thread *cur = thread_current ();
	file_seek (cur->files->fdt->fd[fd], position);
}

off_t
syscall_tell (int fd)
{
	fd -= 2;
	if (!is_open (fd))
		return -1;
	struct thread *cur = thread_current ();
	return file_tell (cur->files->fdt->fd[fd]); 
}

void
syscall_close (int fd)
{
	fd -= 2;
	if (!is_open (fd))
		return;
	struct files_handler *files = thread_current ()->files; 
	lock_acquire (&files->file_lock);
	struct fdtable *fdt = files->fdt;
	if (fdt->fd[fd] == NULL)
		goto out_unlock;
	file_close (fdt->fd[fd]);
	fdt->fd[fd] = NULL;
	put_unused_fd (files, fd);

out_unlock:
	lock_release (&files->file_lock);
}

mapid_t
syscall_mmap (int fd, void *addr)
{
	if (fd == 0 || fd == 1 || addr == NULL || pg_ofs (addr) != 0)
		return -1;

	fd -= 2;
	if (!is_open (fd))
		return -1;
	
	struct thread *cur = thread_current ();
	struct file *f = file_reopen (cur->files->fdt->fd[fd]);
	if (f == NULL)
		return -1;

	off_t read_bytes = file_length (f);
	if (read_bytes == 0)
		return -1;

  off_t offset = 0;
  while (offset < read_bytes)
    {
      if (page_find (&cur->pages, addr + offset) != NULL)
				return -1;
      offset += PGSIZE;
    }	

	return add_mmapfile (&cur->mmapfiles, f, addr, read_bytes);
}

void
syscall_munmap (mapid_t mapping)
{
	struct list_elem *e;
	struct thread *cur = thread_current ();
  for (e = list_begin (&cur->mmapfiles); e != list_end (&cur->mmapfiles); e = list_next (e))
		{
      struct mmapfile *mf = list_entry (e, struct mmapfile, elem);
			if (mf->mapid == mapping)
				{
					free_mmapfile (&cur->pages, mf);
					list_remove (e);
					free (mf);
					break;
				}
		}
}

static inline void
put_unused_fd (struct files_handler *files, int fd)
{
	bitmap_set (files->fdt->fd_map, fd, false);
	if (fd < files->next_fd)
		files->next_fd = fd;
}

static bool
is_valid_uddr (const void *vaddr)
{
	uint32_t *pd = thread_current ()->pagedir;
	if (is_user_vaddr (vaddr))
		return pagedir_get_page (pd, vaddr) != NULL;
	return false;
}
