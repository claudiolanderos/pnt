#include <stdio.h>
#include <string.h>
#include <list.h>
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "filesys/directory.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    disk_sector_t inode_sector;         /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

/* Create a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (disk_sector_t sector, size_t entry_cnt) 
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry), true);
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL && inode_isdir (inode))
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer when failure occurs. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      // inode_close (dir->inode);
      // free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is not null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
  {
    *inode = inode_open (e.inode_sector);
  }
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true on success, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, disk_sector_t inode_sector) 
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
            if (strcmp (e.name, ".") != 0 && strcmp (e.name, "..") != 0)
            {
              strlcpy (name, e.name, NAME_MAX + 1);
              return true;
            }
        } 
    }
  return false;
}

static void
dir_parse (char *cmd, int *argc, char ***argv)
{
  char *token, *saved_ptr;

  *argc = 0;
  *argv = (char **) (cmd + strlen (cmd) + 1);
  
  for (token = strtok_r (cmd, "/", &saved_ptr);
       token != NULL;
       token = strtok_r (NULL, "/", &saved_ptr))
  {
    (*argv)[(*argc)++] = token;
  }
  
  (*argv)[*argc] = NULL;
}

struct inode *
dir_find (const char *path)
{
  char *data = palloc_get_page (0);
  int i, n;
  char **paths;
  struct dir *dir;
  struct inode *inode;

  if (path[0] == '/')
  {
    dir = dir_open_root ();
    ++path;
  }
  else
  {
    dir = thread_get_pwd (thread_current ());
    dir = dir == NULL ? dir_open_root () : dir;
    dir = dir_reopen (dir);
  }

  strlcpy (data, path, PGSIZE);
  dir_parse (data, &n, &paths);

  if (n == 0)
  {
    palloc_free_page (0);
    inode = dir->inode;
    dir_close (dir);
    return inode;
  }
  
  for (i = 0; i < n; ++i)
  {
    if (paths[i][0] == '\0') continue;
    if (dir_lookup (dir, paths[i], &inode))
    {
      dir_close (dir);
      if (i + 1 < n)
      {
        dir = dir_open (inode);
      }
    }
    else
    {
      dir_close (dir);
      palloc_free_page (data);
      return NULL;
    }
  }
  
  palloc_free_page (data);
  return inode;
}

bool
dir_split (const char *path, struct inode **inode, char *to)
{
  char *data = palloc_get_page (0);
  int i, n;
  char **paths;
  struct dir *dir;

  if (path[0] == '/')
  {
    dir = dir_open_root ();
    ++path;
  }
  else
  {
    dir = thread_current ()->pwd;
    dir = dir == NULL ? dir_open_root () : dir;
    dir = dir_reopen (dir);
  }

  *inode = dir_get_inode (dir);

  strlcpy (data, path, PGSIZE);
  dir_parse (data, &n, &paths);
  
  if (n == 0)
  {
    palloc_free_page (0);
    return false;
  }
  
  for (i = 0; i < n; ++i)
  {
    if (paths[i][0] == '\0') continue;

    if (strlen (paths[i]) > NAME_MAX)
    {
      dir_close (dir);
      palloc_free_page (data);
      return false;
    }
    
    if (i + 1 < n)
    {
      if (dir_lookup (dir, paths[i], inode))
      {
        dir_close (dir);
        dir = dir_open (*inode);
      }
    }
  }

  strlcpy (to, paths[n - 1], NAME_MAX + 1);
  palloc_free_page (data);
  return true;
}
