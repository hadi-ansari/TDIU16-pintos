#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"

#include "filesys/directory.h"


#include <stdio.h>
#include "threads/thread.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    disk_sector_t start;                /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[125];               /* Not used. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    disk_sector_t sector;               /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    struct inode_disk data;             /* Inode content. */
    struct lock lock;


    /////////////////////////////////////////////////////////////////////////
    struct lock rw_lock;
    struct condition rw_cond;
    int read_cnt;
    bool writing;
    /////////////////////////////////////////////////////////////////////////
  };


/* Returns the disk sector that contains byte offset POS within
   INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return inode->data.start + pos / DISK_SECTOR_SIZE;
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
static struct lock list_lock;
//static struct lock global_lock;
int size = 0;
/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  lock_init(&list_lock);
  //lock_init(&global_lock);
  dir_init();
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   disk.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      if (free_map_allocate (sectors, &disk_inode->start))
        {
          disk_write (filesys_disk, sector, disk_inode);
          if (sectors > 0) 
            {
              static char zeros[DISK_SECTOR_SIZE];
              size_t i;
              
              for (i = 0; i < sectors; i++) 
                disk_write (filesys_disk, disk_inode->start + i, zeros); 
            }
          success = true; 
        } 
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector) 
{
  struct list_elem *e;
  struct inode *inode;

  lock_acquire(&list_lock);
  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          lock_release(&list_lock);
          return inode; 
        }
    }
  
  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
  {
    lock_release(&list_lock);
    printf("# STACK OVERFLOW\n");
    return NULL;
  }

  list_push_front (&open_inodes, &inode->elem);

  /* Initialize. */
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->removed = false;

  //////////////////////////////////////////////////////////////////////////////////
  inode->writing = false;
  inode->read_cnt = 0;
//////////////////////////////////////////////////////////////////////////////////
  lock_init(&inode->lock);
  
  disk_read (filesys_disk, inode->sector, &inode->data);
  
//////////////////////////////////////////////////////////////////////////////////
  lock_init(&inode->rw_lock);
  cond_init(&inode->rw_cond);
//////////////////////////////////////////////////////////////////////////////////


  lock_release(&list_lock);
  return inode;
}


/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
  {
    lock_acquire(&inode->lock);
    inode->open_cnt++;
    lock_release(&inode->lock);
  }
  return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
   /* Ignore null pointer. */
  if (inode == NULL)
    return;
  lock_acquire(&inode->lock);
    
  /* Release resources if this was the last opener. */
  if (inode->open_cnt == 1)
    {
      lock_release(&inode->lock);

      lock_acquire(&list_lock);
      lock_acquire(&inode->lock);
      if (inode->open_cnt == 1){
        inode->open_cnt--;
        /* Remove from inode list. */
        list_remove (&inode->elem);
      }
      else{
        inode->open_cnt--;
        lock_release(&inode->lock);
        lock_acquire(&list_lock);
        return;
      }
 
      /* Deallocate blocks if the file is marked as removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length)); 
        }
      
      lock_release(&inode->lock);
      free (inode);
      
      lock_release(&list_lock);

      return;
    }
  inode->open_cnt--;
  lock_release(&inode->lock);
}

// void
// inode_close (struct inode *inode)
// {
//   /* Ignore null pointer. */
//   if (inode == NULL)
//   {
//       return;
//   }


//     lock_acquire(&list_lock);
//   /* Release resources if this was the last opener. */
//   lock_acquire(&inode->lock);
//   if (--inode->open_cnt == 0)
//     {
//       lock_release(&inode->lock);
//       /* Remove from inode list. */
//       list_remove (&inode->elem);


//       /* Deallocate blocks if the file is marked as removed. */

//       if (inode->removed)
//         {
//           free_map_release (inode->sector, 1);
//           free_map_release (inode->data.start,
//                             bytes_to_sectors (inode->data.length));
//         }
//       free (inode);
//       lock_release(&list_lock);
//       return;
//     }
//     lock_release(&inode->lock);
//     lock_release(&list_lock);
// }


/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  lock_acquire(&inode->lock);
  inode->removed = true;
  lock_release(&inode->lock);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;
  
///////////////////////////////////////////////////////////////////////
  lock_acquire(&inode->rw_lock);
  while(inode->writing)
  {
    cond_wait(&inode->rw_cond, &inode->rw_lock);
  }
  ++inode->read_cnt;
  lock_release(&inode->rw_lock);
///////////////////////////////////////////////////////////////////////



  //lock_acquire(&global_lock);
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) 
        {
          /* Read full sector directly into caller's buffer. */
          disk_read (filesys_disk, sector_idx, buffer + bytes_read); 
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (DISK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          disk_read (filesys_disk, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  //lock_release(&global_lock);
///////////////////////////////////////////////////////////////////////
  lock_acquire(&inode->rw_lock);
  inode->read_cnt--;
  cond_broadcast(&inode->rw_cond, &inode->rw_lock);
  lock_release(&inode->rw_lock);
///////////////////////////////////////////////////////////////////////

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  //lock_acquire(&list_lock);

  ///////////////////////////////////////////////////////////////////////
  lock_acquire(&inode->rw_lock);
  while(inode->read_cnt || inode->writing)
  {
    cond_wait(&inode->rw_cond,&inode->rw_lock);
  }
  inode->writing = true;
  lock_release(&inode->rw_lock);
  ///////////////////////////////////////////////////////////////////////

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) 
        {
          /* Write full sector directly to disk. */
          disk_write (filesys_disk, sector_idx, buffer + bytes_written); 
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (DISK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            disk_read (filesys_disk, sector_idx, bounce);
          else
            memset (bounce, 0, DISK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          disk_write (filesys_disk, sector_idx, bounce); 
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  //lock_release(&list_lock);

///////////////////////////////////////////////////////////////////////
  lock_acquire(&inode->rw_lock);
  inode->writing = false;
  cond_broadcast(&inode->rw_cond, &inode->rw_lock);
  lock_release(&inode->rw_lock);
///////////////////////////////////////////////////////////////////////

  return bytes_written;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

