#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/directory.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
	cache_clear ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, enum inode_type type) 
{
	bool success = false;
  struct dir *dir = NULL, *current_dir = NULL;
  struct inode *inode = NULL;

	char *p = malloc (strlen (name) + 1);
	if (p == NULL)
		return success;
	strlcpy (p, name, strlen (name) + 1);

  char *save_ptr, *next_token;
  char *token = strtok_r (p, "/", &save_ptr);

	if (strlen (name) == 0)
		goto done;
	else if (name[0] == '/')
		dir = dir_open_root ();
	else if (thread_current ()->current_dir != NULL)
		{
			current_dir = thread_current ()->current_dir;
			next_token = strtok_r (NULL, "/", &save_ptr);
			if (next_token == NULL)
				{
					block_sector_t inode_sector = 0;
					success = (current_dir != NULL
									&& free_map_allocate (1, &inode_sector)
									&& ((type == INODE_DIR) ? dir_create (inode_sector, initial_size, inode_get_inumber (dir_get_inode (current_dir))) : inode_create (inode_sector, initial_size, type))
									&& dir_add (current_dir, token, inode_sector));
					if (!success && inode_sector != 0) 
						free_map_release (inode_sector, 1);
					goto done;
				}
			else
				{
					dir_lookup (current_dir, token, &inode);
					if (inode == NULL)
						goto done;
					dir = dir_open (inode);
				}
			token = next_token;
		}
	else
		dir = dir_open_root ();
	
  while (token != NULL && dir != NULL)
		{
			next_token = strtok_r (NULL, "/", &save_ptr);
			if (next_token == NULL)
				{
					block_sector_t inode_sector = 0;
					success = (dir != NULL
									&& free_map_allocate (1, &inode_sector)
									&& ((type == INODE_DIR) ? dir_create (inode_sector, initial_size, inode_get_inumber (dir_get_inode (dir))) : inode_create (inode_sector, initial_size, type))
									&& dir_add (dir, token, inode_sector));
					if (!success && inode_sector != 0) 
						free_map_release (inode_sector, 1);
					dir_close (dir);
					goto done;
				}
			else 
				{
					dir_lookup (dir, token, &inode);
					dir_close (dir);
					if (inode == NULL)
						goto done;
					dir = dir_open (inode);
				}
			token = next_token;
		}

done:
	free (p);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *dir = NULL, *current_dir = NULL;
  struct inode *inode = NULL;

	char *p = malloc (strlen (name) + 1);
	if (p == NULL)
		return NULL;
	strlcpy (p, name, strlen (name) + 1);

  char *save_ptr;
  char *token = strtok_r (p, "/", &save_ptr);

	if (strlen (name) == 0)
		goto done;
	else if (name[0] == '/')
		{
			dir = dir_open_root ();
			if (token == NULL)
				token = ".";
		}
	else if (thread_current ()->current_dir != NULL)
		{
			current_dir = thread_current ()->current_dir;
			dir_lookup (current_dir, token, &inode);
			if (inode == NULL)
				goto done;
			token = strtok_r (NULL, "/", &save_ptr);
			if (token != NULL)
				dir = dir_open (inode);
		}
	else
		dir = dir_open_root ();

  while (token != NULL && dir != NULL)
		{
			dir_lookup (dir, token, &inode);
			dir_close (dir);
			if (inode == NULL)
				goto done;
			token = strtok_r (NULL, "/", &save_ptr);
			if (token != NULL)
				dir = dir_open (inode);
		}

done:
	free (p);
	struct file *file = (struct file *) dir_open (inode);
	if (file == NULL)
		file = file_open (inode);
	return file;
}

void filesys_close (struct file *file)
{
	if (inode_is_dir (file_get_inode (file)))
		dir_close ((struct dir *) file);
	else
		file_close (file);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
	bool success = false;
  struct dir *dir = NULL, *current_dir = NULL;
  struct inode *inode = NULL;

	char *p = malloc (strlen (name) + 1);
	if (p == NULL)
		return success;
	strlcpy (p, name, strlen (name) + 1);

  char *save_ptr, *next_token;
  char *token = strtok_r (p, "/", &save_ptr);

	if (strlen (name) == 0)
		goto done;
	else if (name[0] == '/')
		dir = dir_open_root ();
	else if (thread_current ()->current_dir != NULL)
		{
			current_dir = thread_current ()->current_dir;
			next_token = strtok_r (NULL, "/", &save_ptr);
			if (next_token == NULL)
				{
					success = dir_remove (current_dir, token);
					goto done;
				}
			else
				{
					dir_lookup (current_dir, token, &inode);
					if (inode == NULL)
						goto done;
					dir = dir_open (inode);
				}
			token = next_token;
		}
	else
		dir = dir_open_root ();

  while (token != NULL && dir != NULL)
		{
			next_token = strtok_r (NULL, "/", &save_ptr);
			if (next_token == NULL)
				{
					success = dir_remove (dir, token);
					dir_close (dir);
					goto done;
				}
			else 
				{
					dir_lookup (dir, token, &inode);
					dir_close (dir);
					if (inode == NULL)
						goto done;
					dir = dir_open (inode);
				}
			token = next_token;
		}

done:
	free (p);
  return success;
}

bool
filesys_chdir (const char *name)
{
	bool success = false;
  struct dir *dir = NULL, *current_dir = NULL;
  struct inode *inode = NULL;

	char *p = malloc (strlen (name) + 1);
	if (p == NULL)
		return success;
	strlcpy (p, name, strlen (name) + 1);

  char *save_ptr, *next_token;
  char *token = strtok_r (p, "/", &save_ptr);

	if (name[0] == '/')
		{
			dir = dir_open_root ();
			if (token == NULL)
				token = ".";
		}
	else if (thread_current ()->current_dir != NULL)
		{
			current_dir = thread_current ()->current_dir;
			next_token = strtok_r (NULL, "/", &save_ptr);
			if (next_token == NULL)
				{
					dir_lookup (current_dir, token, &inode);
					if (inode != NULL)
						{
							dir_close (current_dir);
							thread_current ()->current_dir = dir_open (inode);
							success = true;
						}
					goto done;
				}
			else
				{
					dir_lookup (current_dir, token, &inode);
					dir = dir_open (inode);
				}
			token = next_token;
		}
	else
		dir = dir_open_root ();
	
  while (token != NULL && dir != NULL)
		{
			next_token = strtok_r (NULL, "/", &save_ptr);
			if (next_token == NULL)
				{
					dir_lookup (dir, token, &inode);
					if (inode != NULL)
						{
							dir_close (thread_current ()->current_dir);
							thread_current ()->current_dir = dir_open (inode);
							success = true;
						}
					goto done;
				}
			else 
				{
					dir_lookup (dir, token, &inode);
					dir_close (dir);
					dir = dir_open (inode);
				}
			token = next_token;
		}

done:
	free (p);
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, DEFAULT_DIR_SIZE, ROOT_DIR_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
