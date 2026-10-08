/*******************************************************************************
 * Copyright © 2014 The DTVKit Open Software Foundation Ltd (www.dtvkit.org)
 * Copyright © 2008 Ocean Blue Software Ltd
 *
 * This file is part of a DTVKit Software Component
 * You are permitted to copy, modify or distribute this file subject to the terms
 * of the DTVKit 1.0 Licence which can be found in licence.txt or at www.dtvkit.org
 * 
 * THIS CODE AND INFORMATION ARE PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.
 * 
 * If you or your organisation is not a member of DTVKit then you have access
 * to this source code outside of the terms of the licence agreement
 * and you are expected to delete this and any associated files immediately.
 * Further information on DTVKit, membership and terms can be found at www.dtvkit.org
 *******************************************************************************/
/**
 * @brief   Disk Functions
 * @file    stbhwdsk.c
 * @date    June 2008
 * @author  Steve Ford
 */

/*#define  DISK_DEBUG*/

/*---includes for this file--------------------------------------------------*/
/* compiler library header files */
#include <stdio.h>
#include <stdlib.h>
#include <stdio_ext.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/statfs.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <linux/hdreg.h>

/* third party header files */
/* STB Header Files */
#include "techtype.h"
#include "dbgfuncs.h"
#include "stbhwc.h"
#include "stbhwos.h"
#include "stbhwmem.h"
#include "stbhwdsk.h"

/*---macro definitions for this file-----------------------------------------*/
#ifndef  DISK_DEBUG
   #define  DISK_DBG(X)
#else
   #define  DISK_DBG(X)    STB_SPDebugWrite X
#endif

/*---constant definitions for this file--------------------------------------*/
#define DISKS_ROOT         "disks"

#define DISK_NAME_SIZE     40

/*---local typedef structs for this file-------------------------------------*/
typedef struct s_disk_info
{
   struct s_disk_info *next;
   BOOLEAN is_mounted;     /* TRUE if the disk is mounted */
   BOOLEAN is_removeable;  /* TRUE if the disk isn't an internal disk */
   U16BIT disk_id;         /* Disk id */
   char *mount_path;
   char *disk_name;
   U32BIT disk_size;       /* Disk size in KB */
   BOOLEAN blocked;
   BOOLEAN found;
} S_DISK_INFO;


/*---local (static) variable declarations for this file----------------------*/
static S_DISK_INFO *disks;
static void *disk_sem;

/*---local function prototypes for this file---------------------------------*/
static void AddDisk(S_DISK_INFO *disk_info);
static void DeleteDisk(S_DISK_INFO *del_disk);
static S_DISK_INFO* FindDisk(U16BIT disk_id);

static void CheckMountedDisks(BOOLEAN send_events);
static void DiskMonitorTask(void *param);


/*---global function definitions----------------------------------------------*/
/**
 * @brief   Initialise the hard disk component
 */
void STB_DSKInitialise(void)
{
   FUNCTION_START(STB_DSKInitialise);

   /* Create a semaphore to control access to disk info */
   disk_sem = STB_OSCreateSemaphore();

   /* Create a task to monitor addition/removal of disks */
   STB_OSCreateTask(DiskMonitorTask, NULL, 4096, 7, (U8BIT *)"DiskMonitor");

   FUNCTION_FINISH(STB_DSKInitialise);
}

/**
 * @brief   Returns the number of disks currently detected
 * @return  Number of disks
 */
U16BIT STB_DSKGetNumDisks(void)
{
   U16BIT num_disks = 0;
   S_DISK_INFO *disk;

   FUNCTION_START(STB_DSKGetNumDisks);

   STB_OSSemaphoreWait(disk_sem);

   for (disk = disks; disk != NULL; disk = disk->next)
   {
      num_disks++;
   }

   STB_OSSemaphoreSignal(disk_sem);

   FUNCTION_FINISH(STB_DSKGetNumDisks);

   return(num_disks);
}

/**
 * @brief   Returns the id of the disk at the given index
 * @param   index zero based index
 * @return  Disk id, 0 if no disk found
 */
U16BIT STB_DSKGetDiskIdByIndex(U16BIT index)
{
   U16BIT disk_id = INVALID_DISK_ID;
   S_DISK_INFO *disk;

   FUNCTION_START(STB_DSKGetDiskIdByIndex);

   STB_OSSemaphoreWait(disk_sem);

   for (disk = disks; (index > 0) && (disk != NULL); index--)
   {
      disk = disk->next;
   }

   if (disk != NULL)
   {
      disk_id = disk->disk_id;
   }

   STB_OSSemaphoreSignal(disk_sem);

   FUNCTION_FINISH(STB_DSKGetDiskIdByIndex);

   return(disk_id);
}

/**
 * @brief   Checks if the given disk is removeable
 * @param   disk_id ID of the disk to be checked
 * @return  TRUE if the disk is removeable, FALSE otherwise
 */
BOOLEAN STB_DSKIsRemoveable(U16BIT disk_id)
{
   BOOLEAN retval = FALSE;
   S_DISK_INFO *disk;

   FUNCTION_START(STB_DSKIsRemoveable);

   STB_OSSemaphoreWait(disk_sem);

   disk = FindDisk(disk_id);
   if (disk != NULL)
   {
      retval = disk->is_removeable;
   }

   STB_OSSemaphoreSignal(disk_sem);

   FUNCTION_FINISH(STB_DSKIsRemoveable);

   return(retval);
}

/**
 * @brief   Checks if the given disk is mounted
 * @param   disk_id ID of the disk to be checked
 * @return  TRUE if the disk is mounted, FALSE otherwise
 */
BOOLEAN STB_DSKIsMounted(U16BIT disk_id)
{
   BOOLEAN retval = FALSE;
   S_DISK_INFO *disk;

   FUNCTION_START(STB_DSKIsMounted);

   STB_OSSemaphoreWait(disk_sem);

   disk = FindDisk(disk_id);
   if ((disk != NULL) && disk->is_mounted)
   {
      retval = TRUE;
   }

   STB_OSSemaphoreSignal(disk_sem);

   FUNCTION_FINISH(STB_DSKIsMounted);

   return(retval);
}

/**
 * @brief   Attempts to mount the given disk, if it isn't already mounted
 * @param   disk_id ID of the disk to be mounted
 * @return  TRUE if the disk is mounted, FALSE otherwise
 */
BOOLEAN STB_DSKMountDisk(U16BIT disk_id)
{
   BOOLEAN retval = FALSE;
   BOOLEAN disk_mounted = FALSE;
   S_DISK_INFO *disk;

   FUNCTION_START(STB_DSKMountDisk);

   STB_OSSemaphoreWait(disk_sem);

   disk = FindDisk(disk_id);
   if (disk != NULL)
   {
      if (!disk->is_mounted)
      {
         disk->is_mounted = TRUE;
         disk->blocked = FALSE;
         disk_mounted = TRUE;

         DISK_DBG(("Mounted %s", disk->disk_name));
      }

      if (disk->is_mounted)
      {
         retval = TRUE;
      }
   }

   STB_OSSemaphoreSignal(disk_sem);

   if (disk_mounted)
   {
      /* Send an event to indicate the disk is available */
      STB_OSSendEvent(FALSE, HW_EV_CLASS_DISK, HW_EV_TYPE_DISK_CONNECTED, NULL, 0);
   }

   FUNCTION_FINISH(STB_DSKMountDisk);

   return(retval);
}

/**
 * @brief   Attempts to unmount the given disk, if it isn't already unmounted
 * @param   disk_id ID of the disk to be unmounted
 * @return  TRUE if the disk is unmounted, FALSE otherwise
 */
BOOLEAN STB_DSKUnmountDisk(U16BIT disk_id)
{
   BOOLEAN retval = FALSE;
   BOOLEAN disk_unmounted = FALSE;
   S_DISK_INFO *disk;

   FUNCTION_START(STB_DSKUnmountDisk);

   STB_OSSemaphoreWait(disk_sem);

   disk = FindDisk(disk_id);
   if (disk != NULL)
   {
      if (disk->is_mounted)
      {
         disk->is_mounted = FALSE;

         /* To prevent the background task from automatically remounting the disk,
          * set it to blocked */
         disk->blocked = TRUE;

         disk_unmounted = TRUE;

         DISK_DBG(("Unmounted %s", disk->disk_name));
      }

      if (!disk->is_mounted)
      {
         retval = TRUE;
      }
   }

   STB_OSSemaphoreSignal(disk_sem);

   if (disk_unmounted)
   {
      /* Send an event to indicate the disk is no longer available */
      STB_OSSendEvent(FALSE, HW_EV_CLASS_DISK, HW_EV_TYPE_DISK_REMOVED, NULL, 0);
   }

   FUNCTION_FINISH(STB_DSKUnmountDisk);

   return(retval);
}

/**
 * @brief   Gets the name of a disk and copies it into the array provided
 * @param   disk_id ID of the disk
 * @param   name array of U8BIT into which the name will be copied
 * @param   name_len max number of characters in the name
 * @return  TRUE if the disk is found, FALSE otherwise
 */
BOOLEAN STB_DSKGetDiskName(U16BIT disk_id, U8BIT *name, U16BIT name_len)
{
   BOOLEAN disk_found = FALSE;
   S_DISK_INFO *disk;

   FUNCTION_START(STB_DSKGetDiskName);

   STB_OSSemaphoreWait(disk_sem);

   disk = FindDisk(disk_id);
   if (disk != NULL)
   {
      if (strlen(disk->disk_name) > name_len)
      {
         strncpy((char *)name, disk->disk_name, name_len - 1);
         name[name_len - 1] = '\0';
      }
      else
      {
         strcpy((char *)name, disk->disk_name);
      }

      disk_found = TRUE;
   }

   STB_OSSemaphoreSignal(disk_sem);

   FUNCTION_FINISH(STB_DSKGetDiskName);

   return(disk_found);
}

/**
 * @brief   Returns the amount of space used on the disk
 * @param   disk_id ID of the disk
 * @return  Space used in kilobytes
 */
U32BIT STB_DSKGetUsed(U16BIT disk_id)
{
   U32BIT disk_used = 0;
   S_DISK_INFO *disk;
   struct statfs fs_info;
   U32BIT blocks_used;

   FUNCTION_START(STB_DSKGetUsed);

   STB_OSSemaphoreWait(disk_sem);

   disk = FindDisk(disk_id);
   if ((disk != NULL) && disk->is_mounted)
   {
      /* Found the disk */
      if (statfs(disk->mount_path, &fs_info) == 0)
      {
         /* Calculate the disk space used in KB */
         blocks_used = fs_info.f_blocks - fs_info.f_bavail;

         if ((fs_info.f_bsize > 1024) && ((fs_info.f_bsize % 1024) == 0))
         {
            disk_used = (fs_info.f_bsize / 1024) * blocks_used;
         }
         else
         {
            disk_used = (fs_info.f_bsize * blocks_used) / 1024;
         }
      }
#ifdef DISK_DEBUG
      else
      {
         DISK_DBG(("STB_DSKGetUsed(0x%x): Failed to get info on the disk, errno=%u", disk_id, errno));
      }
#endif
   }

   STB_OSSemaphoreSignal(disk_sem);

   FUNCTION_FINISH(STB_DSKGetUsed);

   return(disk_used);
}

/**
 * @brief   Queries whether the disk is formatted
 * @return  FALSE unformatted
 */
BOOLEAN STB_DSKIsFormatted(U16BIT disk_id)
{
   BOOLEAN is_formatted = FALSE;
   S_DISK_INFO *disk;

   FUNCTION_START(STB_DSKIsFormatted);

   STB_OSSemaphoreWait(disk_sem);

   disk = FindDisk(disk_id);
   if ((disk != NULL) && disk->is_mounted)
   {
      /* Disk is formatted if it's mounted */
      is_formatted = TRUE;
   }

   STB_OSSemaphoreSignal(disk_sem);

   FUNCTION_FINISH(STB_DSKIsFormatted);

   return(is_formatted);
}

/**
 * @brief   Returns the size (capacity) of the disk
 * @return  The size of the disk in kilobytes
 */
U32BIT STB_DSKGetSize(U16BIT disk_id)
{
   U32BIT disk_size = 0;
   S_DISK_INFO *disk;

   FUNCTION_START(STB_DSKGetSize);

   STB_OSSemaphoreWait(disk_sem);

   disk = FindDisk(disk_id);
   if ((disk != NULL) && disk->is_mounted)
   {
      disk_size = disk->disk_size;
   }

   STB_OSSemaphoreSignal(disk_sem);

   FUNCTION_FINISH(STB_DSKGetSize);

   return(disk_size);
}

/**
 * @brief   Initiates formatting and partitioning of the hard disk. This will erase all data on the
 *          disk!
 */
void STB_DSKFormat(U16BIT disk_id)
{
   FUNCTION_START(STB_DSKFormat);
   USE_UNWANTED_PARAM(disk_id);
   FUNCTION_FINISH(STB_DSKFormat);
}

/**
 * @brief   Gets the progress of the format operation
 * @return  The progress as percent complete
 */
U8BIT STB_DSKGetFormatProgress(U16BIT disk_id)
{
   FUNCTION_START(STB_DSKGetFormatProgress);
   USE_UNWANTED_PARAM(disk_id);
   FUNCTION_FINISH(STB_DSKGetFormatProgress);
   return(100);
}

/**
 * @brief   Returns a summary of the disk integrity
 * @return   TRUE if disk integrity ok
 */
BOOLEAN STB_DSKGetIntegrity(U16BIT disk_id)
{
   FUNCTION_START(STB_DSKGetIntegrity);
   USE_UNWANTED_PARAM(disk_id);
   FUNCTION_FINISH(STB_DSKGetIntegrity);
   return TRUE;
}

/**
 * @brief   Initiates a data repair of the hard disk. This *may* cause the disk to become unreadable
 */
void STB_DSKRepair(U16BIT disk_id)
{
   FUNCTION_START(STB_DSKRepair);
   USE_UNWANTED_PARAM(disk_id);

   STB_OSSendEvent(FALSE, HW_EV_CLASS_DISK, HW_EV_TYPE_REPAIR_COMPLETE, NULL, 0);

   FUNCTION_FINISH(STB_DSKRepair);
}

/**
 * @brief   Gets the progress of the repair operation
 * @return  The progress as percent complete
 */
U8BIT STB_DSKGetRepairProgress(U16BIT disk_id)
{
   FUNCTION_START(STB_DSKGetRepairProgress);
   USE_UNWANTED_PARAM(disk_id);
   FUNCTION_FINISH(STB_DSKGetRepairProgress);
   return 0;
}

/**
 * @brief   Copies the full pathname for the given filename, including the mount directory,
 *          to the given string array
 * @param   disk_id disk
 * @param   filename name of the file on the disk
 * @param   pathname array into which the full pathname will be copied
 * @param   max_pathname_len size of the pathname array
 * @return  TRUE if the disk is valid exists and is mounted and the destination string
 *          is long enough to take the full pathname
 */
BOOLEAN STB_DSKFullPathname(U16BIT disk_id, U8BIT *filename, U8BIT *pathname, U16BIT max_pathname_len)
{
   BOOLEAN result = FALSE;
   S_DISK_INFO *disk;

   FUNCTION_START(STB_DSKFullPathname);

   STB_OSSemaphoreWait(disk_sem);

   disk = FindDisk(disk_id);
   if ((disk != NULL) && disk->is_mounted)
   {
      if (strlen((char *)filename) + strlen(disk->mount_path) + 2 <= max_pathname_len)
      {
         sprintf((char *)pathname, "%s/%s", disk->mount_path, filename);
         result = TRUE;
      }
   }

   STB_OSSemaphoreSignal(disk_sem);

   FUNCTION_FINISH(STB_DSKFullPathname);

   return(result);
}

/**
 * @brief   Opens an existing file or creates a new one
 * @param   name The filename (including path)
 * @param   mode The access mode
 * @return  The file handle
 */
void* STB_DSKOpenFile(U16BIT disk_id, U8BIT *name, E_STB_DSK_FILE_MODE mode)
{
   FILE *file = NULL;
   S_DISK_INFO *disk;
   char *fullpath;

   FUNCTION_START(STB_DSKOpenFile);

   ASSERT(name != NULL);

   STB_OSSemaphoreWait(disk_sem);

   disk = FindDisk(disk_id);
   if ((disk != NULL) && disk->is_mounted)
   {
      fullpath = STB_MEMGetSysRAM(strlen(disk->mount_path) + strlen((char *)name) + 2);
      if (fullpath != NULL)
      {
         sprintf(fullpath, "%s/%s", disk->mount_path, name);
         DISK_DBG(("STB_DSKOpenFile: Opening %s", fullpath));

         switch (mode)
         {
            case FILE_MODE_READ:
               file = fopen((const char *)fullpath, "r");
               break;

            case FILE_MODE_WRITE:
               file = fopen((const char *)fullpath, "r+");
               break;

            case FILE_MODE_OVERWRITE:
               file = fopen((const char *)fullpath, "w");
               break;

            default:
               DISK_DBG(("STB_DSKOpenFile(%s): Unknown file mode %u", name, mode));
               file = NULL;
               break;
         }

#ifdef DISK_DEBUG
         if (file != NULL)
         {
            DISK_DBG(("STB_DSKOpenFile: returning %p", file));
         }
#endif

         STB_MEMFreeSysRAM(fullpath);
      }
   }
#ifdef DISK_DEBUG
   else
   {
      DISK_DBG(("STB_DSKOpenFile: Failed to find disk with ID 0x%x", disk_id));
   }
#endif

#ifdef DISK_DEBUG
   if (file == NULL)
   {
      DISK_DBG(("Failed to open \"%s\", error %d", name, errno));
   }
#endif

   STB_OSSemaphoreSignal(disk_sem);

   FUNCTION_FINISH(STB_DSKOpenFile);

   return((void *)file);
}

/**
 * @brief   Flushes and closes an open file
 * @param   file The file handle
 */
void STB_DSKCloseFile(void *file)
{
   FILE *fp;

   FUNCTION_START(STB_DSKCloseFile);

   ASSERT(file != NULL);

   DISK_DBG(("STB_DSKCloseFile(%p)", file));

   fp = (FILE *)file;

   if (__fwritable(fp))
   {
      fflush(fp);
      fsync(fileno(fp));
   }

   fclose(fp);

   FUNCTION_FINISH(STB_DSKCloseFile);
}

/**
 * @brief   Reads data from an open file
 * @param   file The file handle
 * @param   data The caller's buffer
 * @param   size Number of bytes to be read
 * @return  Number of bytes successfully read
 */
U32BIT STB_DSKReadFile(void *file, U8BIT *data, U32BIT size)
{
   U32BIT ret_val;

   FUNCTION_START(STB_DSKReadFile);

   ASSERT(file != NULL);
   ASSERT(data != NULL);

   if (feof((FILE *)file))
   {
      /* Attempting to read from the file again, so clear the eof condition as the file may
       * now have had more data written into it */
      clearerr((FILE *)file);
   }

   ret_val = (U32BIT)fread((void *)data, 1, (size_t)size, (FILE *)file);

   FUNCTION_FINISH(STB_DSKReadFile);

   return(ret_val);
}

/**
 * @brief   Writes data to an open file
 * @param   file The file handle
 * @param   data Pointer to the data to be written
 * @param   size Number of bytes to  write
 * @return  Number of bytes successfully written
 */
U32BIT STB_DSKWriteFile(void *file, U8BIT *data, U32BIT size)
{
   U32BIT ret_val;

   FUNCTION_START(STB_DSKWriteFile);

   ASSERT(file != NULL);
   ASSERT(data != NULL);

   ret_val = (U32BIT)fwrite((void *)data, 1, (size_t)size, (FILE *)file);

   FUNCTION_FINISH(STB_DSKWriteFile);

   return(ret_val);
}

/**
 * @brief   Sets the read/write position of an open file
 * @param   file The file handle
 * @param   position Position to move relative to, i.e. start,end,current
 * @param   offset Where to move to relative to position
 * @return  TRUE success, FALSE otherwise
 */
BOOLEAN STB_DSKSeekFile(void *file, E_STB_DSK_FILE_POSITION position, S32BIT offset)
{
   BOOLEAN ret_val = FALSE;

   FUNCTION_START(STB_DSKSeekFile);

   ASSERT(file != NULL);

   switch (position)
   {
      case FILE_POSITION_START:
      {
         if (fseek((FILE *)file, offset, SEEK_SET) == 0)
         {
            ret_val = TRUE;
         }
         break;
      }

      case FILE_POSITION_END:
      {
         if (fseek((FILE *)file, offset, SEEK_END) == 0)
         {
            ret_val = TRUE;
         }
         break;
      }

      case FILE_POSITION_CURRENT:
      {
         if (offset == 0L || (fseek((FILE *)file, offset, SEEK_CUR) == 0))
         {
            ret_val = TRUE;
         }
         break;
      }

      default:
      {
         /* If we have gone off the end of the file, clear the error condition
          * and reposition to exact EOF
          */
         if (feof((FILE *)file))
         {
            clearerr((FILE *)file);
            fseek((FILE *)file, 0, SEEK_END);
         }
         break;
      }
   }

   FUNCTION_FINISH(STB_DSKSeekFile);

   return(ret_val);
}

/**
 * @brief   Gets the current position in an open file
 * @param   file The file handle
 * @param   offset Variable to contain result (byte position in file)
 * @return  TRUE on success
 */
BOOLEAN STB_DSKTellFile(void *file, U32BIT *offset)
{
   ASSERT(file != NULL);

   *offset = (U32BIT)ftell(file);

   return(TRUE);
}

/**
 * @brief   Deletes the file from the given disk
 * @param   disk_id disk ID
 * @param   filename pathname on the disk of the file to be deleted
 * @return  TRUE if successful, FALSE otherwise
 */
BOOLEAN STB_DSKDeleteFile(U16BIT disk_id, U8BIT *filename)
{
   BOOLEAN retval = FALSE;
   S_DISK_INFO *disk;
   char *fullpath;

   FUNCTION_START(STB_DSKDeleteFile);

   STB_OSSemaphoreWait(disk_sem);

   disk = FindDisk(disk_id);
   if ((disk != NULL) && disk->is_mounted)
   {
      fullpath = STB_MEMGetSysRAM(strlen(disk->mount_path) + strlen((char *)filename) + 2);
      if (fullpath != NULL)
      {
         sprintf(fullpath, "%s/%s", disk->mount_path, filename);
         if (unlink(fullpath) == 0)
         {
            retval = TRUE;
         }
#ifdef DISK_DEBUG
         else
         {
            DISK_DBG(("STB_DSKDeleteFile: failed to delete \"%s\", errno=0x%x", fullpath, errno));
         }
#endif

         STB_MEMFreeSysRAM(fullpath);
      }
   }

   STB_OSSemaphoreSignal(disk_sem);

   FUNCTION_FINISH(STB_DSKDeleteFile);

   return(retval);
}

/**
 * @brief   Checks whether a file/directory will the given name exists
 * @param   disk_id disk ID
 * @param   filename pathname on the disk of the file
 * @return  TRUE if exists, FALSE otherwise
 */
BOOLEAN STB_DSKFileExists(U16BIT disk_id, U8BIT *filename)
{
   BOOLEAN retval = FALSE;
   S_DISK_INFO *disk;
   char *fullpath;
   struct stat statbuf;

   FUNCTION_START(STB_DSKFileExists);

   STB_OSSemaphoreWait(disk_sem);

   disk = FindDisk(disk_id);
   if ((disk != NULL) && disk->is_mounted)
   {
      fullpath = STB_MEMGetSysRAM(strlen(disk->mount_path) + strlen((char *)filename) + 2);
      if (fullpath != NULL)
      {
         sprintf(fullpath, "%s/%s", disk->mount_path, filename);

         if (stat(fullpath, &statbuf) == 0)
         {
            retval = TRUE;
         }

         STB_MEMFreeSysRAM(fullpath);
      }
   }

   STB_OSSemaphoreSignal(disk_sem);

   FUNCTION_FINISH(STB_DSKFileExists);

   return(retval);
}

/**
 * @brief   Returns the size in KB of the given file
 * @param   disk_id disk on which the file exists
 * @param   filename name of the file on disk
 * @param   filesize returned value giving the file size in KB
 * @return  TRUE if the file exists, FALSE otherwise
 */
BOOLEAN STB_DSKFileSize(U16BIT disk_id, U8BIT *filename, U32BIT *filesize)
{
   BOOLEAN result = FALSE;
   S_DISK_INFO *disk;
   char *fullpath;
   struct stat statbuf;

   FUNCTION_START(STB_DSKFileSize);

   STB_OSSemaphoreWait(disk_sem);

   disk = FindDisk(disk_id);
   if ((disk != NULL) && disk->is_mounted)
   {
      fullpath = STB_MEMGetSysRAM(strlen(disk->mount_path) + strlen((char *)filename) + 2);
      if (fullpath != NULL)
      {
         sprintf(fullpath, "%s/%s", disk->mount_path, filename);

         if (stat(fullpath, &statbuf) == 0)
         {
            *filesize = statbuf.st_size / 1024;
            result = TRUE;
         }
#ifdef DISK_DEBUG
         else
         {
            DISK_DBG(("STB_DSKFileSize: Failed to stat \"%s\", errno=%d", fullpath, errno));
         }
#endif

         STB_MEMFreeSysRAM(fullpath);
      }
   }

   STB_OSSemaphoreSignal(disk_sem);

   FUNCTION_FINISH(STB_DSKFileSize);

   return(result);
}

/**
 * @brief   Opens a directory in order to read the entries
 * @param   disk_id disk containing to the directory to be read
 * @param   dir_name name of the directory to open
 * @return  Handle to be used in all other operations, NULL if the open fails
 */
void* STB_DSKOpenDirectory(U16BIT disk_id, U8BIT *dir_name)
{
   U8BIT *fullpath;
   U16BIT path_len;
   S_DISK_INFO *disk;
   DIR *dir = NULL;

   FUNCTION_START(STB_DSKOpenDirectory);

   STB_OSSemaphoreWait(disk_sem);

   /* Get the full pathname of the directory to be opened */
   disk = FindDisk(disk_id);
   if (disk != NULL)
   {
      path_len = strlen((char *)dir_name) + strlen(disk->mount_path) + 3;
      fullpath = (U8BIT *)STB_MEMGetSysRAM(path_len);

      STB_OSSemaphoreSignal(disk_sem);

      if (fullpath != NULL)
      {
         if (STB_DSKFullPathname(disk_id, dir_name, fullpath, path_len))
         {
            dir = opendir((char *)fullpath);
#ifdef DISK_DEBUG
            if (dir == NULL)
            {
               DISK_DBG(("STB_DSKOpenDirectory(0x%x): Failed to open directory \"%s\", errno %d",
                         disk_id, fullpath, errno));
            }
#endif
         }

         STB_MEMFreeSysRAM(fullpath);
      }
   }
   else
   {
      STB_OSSemaphoreSignal(disk_sem);
   }

   FUNCTION_FINISH(STB_DSKOpenDirectory);

   return((void *)dir);
}

/**
 * @brief   Reads the next entry from the directory, returning the name of the
 *          entry and the type of the entry
 * @param   dir handle returned when the directory was opened
 * @param   filename array in which the name is returned
 * @param   filename_len size of the filename array
 * @param   entry_type type of entry
 * @return  TRUE if and entry is read, FALSE otherwise which could indicate end of the directory
 */
BOOLEAN STB_DSKReadDirectory(void *dir, U8BIT *filename, U16BIT filename_len,
   E_STB_DIR_ENTRY_TYPE *entry_type)
{
   BOOLEAN result = FALSE;
   struct dirent *entry;

   FUNCTION_START(STB_DSKReadDirectory);

   if (dir != NULL)
   {
      entry = readdir((DIR *)dir);
      if (entry != NULL)
      {
         strncpy((char *)filename, entry->d_name, filename_len - 1);
         filename[filename_len - 1] = '\0';

         switch (entry->d_type)
         {
            case 0:
            case 8:
               *entry_type = DIR_ENTRY_FILE;
               break;

            case 1:
               *entry_type = DIR_ENTRY_DIRECTORY;
               break;

            default:
               *entry_type = DIR_ENTRY_OTHER;
               break;
         }

         result = TRUE;
      }
   }

   FUNCTION_FINISH(STB_DSKReadDirectory);

   return(result);
}

/**
 * @brief   Closes the directory for reading
 * @param   dir directory handle
 */
void STB_DSKCloseDirectory(void *dir)
{
   FUNCTION_START(STB_DSKCloseDirectory);

   if (dir != NULL)
   {
      closedir((DIR *)dir);
   }

   FUNCTION_FINISH(STB_DSKCloseDirectory);
}

/**
 * @brief   Creates a directory with the given name
 * @param   disk_id disk on which the directory is to be created
 * @param   dir_name name of the directory to be created
 * @return  TRUE is the directory is successfully created
 */
BOOLEAN STB_DSKCreateDirectory(U16BIT disk_id, U8BIT *dir_path)
{
   S_DISK_INFO *disk;
   char *pathname;
   char *dirname;
   struct stat statbuf;
   BOOLEAN result;

   FUNCTION_START(STB_DSKCreateDirectory);

   result = FALSE;

   STB_OSSemaphoreWait(disk_sem);

   disk = FindDisk(disk_id);
   if ((disk != NULL) && disk->is_mounted)
   {
      pathname = (char *)STB_MEMGetSysRAM(strlen(disk->mount_path) + strlen((char *)dir_path) + 2);
      if (pathname != NULL)
      {
         result = TRUE;
         dirname = (char *)dir_path;

         /* Create all directories in the path upto the final name */
         while ((dirname = strchr(dirname, '/')) != NULL)
         {
            /* Terminate the directory path at the next slash char */
            *dirname = '\0';

            sprintf(pathname, "%s/%s", disk->mount_path, dir_path);

            /* Check whether the directory already exists */
            if (stat((char *)pathname, &statbuf) != 0)
            {
               /* Create the directory up to this point */
               if (mkdir((char *)pathname, (S_IRWXU | S_IRWXG | S_IRWXO)) != 0)
               {
                  /* Restore the slash char */
                  *dirname = '/';
                  result = FALSE;
                  break;
               }
            }

            /* Restore the slash char and move to the next char to start search for the next one */
            *dirname = '/';
            dirname++;
         }

         if (result)
         {
            sprintf(pathname, "%s/%s", disk->mount_path, dir_path);

            /* Check whether the directory already exists */
            if (stat((char *)pathname, &statbuf) != 0)
            {
               /* Now create the final directory in the path */
               if (mkdir((char *)pathname, (S_IRWXU | S_IRWXG | S_IRWXO)) != 0)
               {
                  result = FALSE;
               }
            }
         }

         STB_OSSemaphoreSignal(disk_sem);

         STB_MEMFreeSysRAM(pathname);
      }
      else
      {
         STB_OSSemaphoreSignal(disk_sem);
      }
   }
   else
   {
      STB_OSSemaphoreSignal(disk_sem);
   }

   FUNCTION_FINISH(STB_DSKCreateDirectory);

   return(result);
}

/**
 * @brief   Deletes a directory and all it contents, so use with care!
 * @param   disk_id disk
 * @param   dir_name name of the directory to be deleted
 */
BOOLEAN STB_DSKDeleteDirectory(U16BIT disk_id, U8BIT *dir_name)
{
   S_DISK_INFO *disk;
   char *dir_path;
   char *pathname;
   DIR *dp;
   struct dirent *entry;
   struct stat statbuf;
   BOOLEAN result = TRUE;

   FUNCTION_START(STB_DSKDeleteDirectory);

   DISK_DBG(("STB_DSKDeleteDirectory(0x%x, \"%s\")", disk_id, dir_name));

   STB_OSSemaphoreWait(disk_sem);

   disk = FindDisk(disk_id);
   if ((disk != NULL) && disk->is_mounted)
   {
      dir_path = (char *)STB_MEMGetSysRAM(strlen(disk->mount_path) + strlen((char *)dir_name) + 2);
      if (dir_path != NULL)
      {
         sprintf(dir_path, "%s/%s", disk->mount_path, dir_name);

         STB_OSSemaphoreSignal(disk_sem);

         dp = opendir((char *)dir_path);
         if (dp != NULL)
         {
            while ((entry = readdir(dp)) != NULL)
            {
               if ((strcmp(entry->d_name, ".") != 0) && (strcmp(entry->d_name, "..") != 0))
               {
                  pathname = (char *)STB_MEMGetSysRAM(strlen((char *)dir_path) + strlen(entry->d_name) + 2);
                  if (pathname != NULL)
                  {
                     sprintf(pathname, "%s/%s", (char *)dir_path, entry->d_name);

                     if (stat(pathname, &statbuf) == 0)
                     {
                        if (S_ISDIR(statbuf.st_mode))
                        {
                           DISK_DBG(("Recursing into \"%s\"...", pathname));
                           /* Recurse down into this directory to delete its contents */
                           if (!STB_DSKDeleteDirectory(disk_id, (U8BIT *)pathname))
                           {
                              result = FALSE;
                           }
                        }
                        else
                        {
                           DISK_DBG(("Deleting \"%s\"", pathname));
                           if (unlink(pathname) != 0)
                           {
                              /* Failed to remove an item but continue trying to delete the rest */
                              result = FALSE;
                              DISK_DBG(("STB_DSKDeleteDirectory: Failed to delete \"%s\", errno %d",
                                        pathname, errno));
                           }
                        }
                     }
#ifdef DISK_DEBUG
                     else
                     {
                        DISK_DBG(("STB_DSKDeleteDirectory: Failed to stat \"%s\", so not deleted, errno %d",
                                  pathname, errno));
                     }
#endif

                     STB_MEMFreeSysRAM(pathname);
                  }
               }
            }

            closedir(dp);

            if (rmdir((char *)dir_path) != 0)
            {
               result = FALSE;
               DISK_DBG(("STB_DSKDeleteDirectory: Failed to delete directory \"%s\", errno %d",
                         dir_path, errno));
            }
         }

         STB_MEMFreeSysRAM(dir_path);
      }
      else
      {
         STB_OSSemaphoreSignal(disk_sem);
      }
   }
   else
   {
      STB_OSSemaphoreSignal(disk_sem);
   }

   FUNCTION_FINISH(STB_DSKDeleteDirectory);

   return(result);
}

/**
 * @brief   Put all disks into or out of standby mode
 * @param   state standby mode, TRUE=Standby FALSE=On
 */
void STB_DSKSetStandby(BOOLEAN state)
{
   FUNCTION_START(STB_DSKSetStandby);
   USE_UNWANTED_PARAM(state);
   FUNCTION_FINISH(STB_DSKSetStandby);
}

/*---local function definitions----------------------------------------------*/

static void AddDisk(S_DISK_INFO *disk_info)
{
   S_DISK_INFO *ptr;

   disk_info->next = NULL;

   if (disks == NULL)
   {
      disks = disk_info;
   }
   else
   {
      ptr = disks;

      while (ptr->next != NULL)
      {
         ptr = ptr->next;
      }

      ptr->next = disk_info;
   }
}

static void DeleteDisk(S_DISK_INFO *del_disk)
{
   S_DISK_INFO *prev;
   S_DISK_INFO *disk;

   prev = NULL;

   for (disk = disks; disk != del_disk; )
   {
      prev = disk;
      disk = disk->next;
   }

   if (disk != NULL)
   {
      if (prev == NULL)
      {
         /* First disk in the list is being deleted */
         disks = disk->next;
      }
      else
      {
         prev->next = disk->next;
      }

      if (disk->mount_path != NULL)
      {
         STB_MEMFreeSysRAM(disk->mount_path);
      }
      if (disk->disk_name != NULL)
      {
         STB_MEMFreeSysRAM(disk->disk_name);
      }

      STB_MEMFreeSysRAM(disk);
   }
}

static S_DISK_INFO* FindDisk(U16BIT disk_id)
{
   S_DISK_INFO *disk;

   for (disk = disks; (disk != NULL) && (disk->disk_id != disk_id); )
   {
      disk = disk->next;
   }

   return(disk);
}

static void DiskMonitorTask(void *param)
{
   USE_UNWANTED_PARAM(param);

   /* Mount any disks already available on startup, but don't send mount events
    * as there won't be anything to handle them during startup */
   CheckMountedDisks(FALSE);

   while (1)
   {
      CheckMountedDisks(TRUE);

      /* Run the task every 10 seconds */
      STB_OSTaskDelay(10000);
   }
}

static void CheckMountedDisks(BOOLEAN send_events)
{
   DIR *dp;
   struct dirent *entry;
   struct stat statbuf;
   S_DISK_INFO *disk;
   BOOLEAN disk_change;
   int retval;
   S_DISK_INFO *next_disk;
   struct statfs fs_info;
   char *pathname;

   static U16BIT disk_id = 1;

   /* Check for any subdirectories, with each one representing a disk */
   dp = opendir(DISKS_ROOT);
   if (dp != NULL)
   {
      STB_OSSemaphoreWait(disk_sem);

      /* Mark all disks as not found so that any that have been removed can be detected */
      for (disk = disks; disk != NULL; disk = disk->next)
      {
         disk->found = FALSE;
      }

      STB_OSSemaphoreSignal(disk_sem);

      disk_change = FALSE;

      while ((entry = readdir(dp)) != NULL)
      {
         if ((strcmp(entry->d_name, ".") != 0) && (strcmp(entry->d_name, "..") != 0))
         {
            pathname = (char *)STB_MEMGetSysRAM(strlen(DISKS_ROOT) + strlen(entry->d_name) + 2);
            if (pathname != NULL)
            {
               sprintf(pathname, "%s/%s", DISKS_ROOT, entry->d_name);

               if (stat(pathname, &statbuf) == 0)
               {
                  if (S_ISDIR(statbuf.st_mode))
                  {
                     /* Check to see if this disk is already known */
                     STB_OSSemaphoreWait(disk_sem);

                     for (disk = disks; (disk != NULL) && (strcmp(disk->disk_name, entry->d_name) != 0); )
                     {
                        disk = disk->next;
                     }

                     if (disk == NULL)
                     {
                        /* This is a new disk */
                        disk = (S_DISK_INFO *)STB_MEMGetSysRAM(sizeof(S_DISK_INFO));
                        ASSERT(disk != NULL);

                        memset(disk, 0, sizeof(S_DISK_INFO));

                        disk->disk_id = disk_id;
                        disk_id++;

                        disk->is_mounted = TRUE;
                        disk->found = TRUE;
                        disk->blocked = FALSE;

                        disk->mount_path = pathname;
                        pathname = NULL;

                        /* All disks are removeable */
                        disk->is_removeable = TRUE;

                        /* Use the directory name as the disk name */
                        if ((disk->disk_name = STB_MEMGetSysRAM(strlen(entry->d_name) + 1)) != NULL)
                        {
                           strcpy(disk->disk_name, entry->d_name);
                        }

                        /* Add this disk to the list */
                        AddDisk(disk);

                        disk_change = TRUE;
                     }
                     else
                     {
                        disk->found = TRUE;
                     }

                     if (disk != NULL)
                     {
                        /* The size of the disk may have been set when getting its name,
                         * so only do the following if the size is still 0 */
                        if (disk->disk_size == 0)
                        {
                           /* statfs can take a long time the first time it's called for a disk,
                            * so release the semaphore to prevent a lockup while it's running */
                           STB_OSSemaphoreSignal(disk_sem);
                           retval = statfs(disk->mount_path, &fs_info);
                           STB_OSSemaphoreWait(disk_sem);

                           if (retval == 0)
                           {
                              /* Calculate the disk size in KB. It's done various ways to avoid overflow */
                              if ((fs_info.f_bsize > 1024) && ((fs_info.f_bsize % 1024) == 0))
                              {
                                 disk->disk_size = (fs_info.f_bsize / 1024) * fs_info.f_blocks;
                              }
                              else
                              {
                                 disk->disk_size = (fs_info.f_bsize * fs_info.f_blocks) / 1024;
                              }

                              DISK_DBG(("Disk %s, size=%lu KB", disk->disk_name, disk->disk_size));
                           }
                        }
                     }

                     STB_OSSemaphoreSignal(disk_sem);
                  }
               }

               if (pathname != NULL)
               {
                  STB_MEMFreeSysRAM(pathname);
               }
            }
         }
      }

      closedir(dp);

      if (disk_change && send_events)
      {
         /* Send an event to indicate a device has been attached */
         STB_OSSendEvent(FALSE, HW_EV_CLASS_DISK, HW_EV_TYPE_DISK_CONNECTED, NULL, 0);
      }

      STB_OSSemaphoreWait(disk_sem);

      /* Now check all disks and unmount any that weren't found in the partitions file */
      disk_change = FALSE;

      for (disk = disks; disk != NULL; )
      {
         if (!disk->found)
         {
            if (disk->is_mounted)
            {
               /* Whatever happened, mark it as unmounted because it isn't available anymore */
               disk->is_mounted = FALSE;
            }

            /* Now the disk has disappeared, delete it from the list of known disks */
            next_disk = disk->next;
            DeleteDisk(disk);
            disk = next_disk;

            disk_change = TRUE;
         }
         else
         {
            disk = disk->next;
         }
      }

      STB_OSSemaphoreSignal(disk_sem);

      if (disk_change && send_events)
      {
         /* Send an event to indicate a device has been removed */
         STB_OSSendEvent(FALSE, HW_EV_CLASS_DISK, HW_EV_TYPE_DISK_REMOVED, NULL, 0);
      }
   }
}

