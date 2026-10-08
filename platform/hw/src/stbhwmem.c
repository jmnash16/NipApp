/*******************************************************************************
 * Copyright © 2014 The DTVKit Open Software Foundation Ltd (www.dtvkit.org)
 * Copyright © 2007 Ocean Blue Software Ltd
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
 * @brief   Set Top Box - Hardware Layer, Memory Management
 * @file    stbhwmem.c
 * @date    September 2007
 * @author  Dave Carter
 */

#define  HEAP_DEBUG
/*#define  NVM_DEBUG*/
/*#define  SECURE_DEBUG*/

/*---includes for this file--------------------------------------------------*/
/* compiler library header files */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#ifdef NVM_DEBUG
#include <errno.h>
#endif

/* third party header files */

/* STB header files */
#include "techtype.h"
#include "dbgfuncs.h"
#include "stbhwos.h"
#include "stbhwmem.h"
#include "stbhwcrypt.h"
#include "stbhwdef.h"
#include "stbhwc.h"

/*---macro definitions for this file-----------------------------------------*/
#ifndef  HEAP_DEBUG
#define  HEAP_DBG(X)
#else
#define  HEAP_DBG(X)     STB_SPDebugWrite X
#endif

#ifndef  NVM_DEBUG
#define  NVM_DBG(X)
#else
#define  NVM_DBG(X)    STB_SPDebugWrite X
#endif

#ifndef  SECURE_DEBUG
#define  SEC_DBG(X)
#else
#define  SEC_DBG(X)     STB_SPDebugWrite X
#endif

#define AES_PAD(x)      (((x) + 0xf) & ~0xf)

/*---constant definitions for this file--------------------------------------*/

/*---local typedef structs for this file-------------------------------------*/

typedef struct s_secure_data
{
   struct s_secure_data *next;
   U8BIT key;
   U32BIT size;
   void *value;
} S_SECURE_DATA;

/*---local (static) variable declarations for this file----------------------*/
static char *nvm_file_string = "nvm.dat";
static const char *secure_data_file = "secure/data/secure.dat";

static S_SECURE_DATA *secure_data_list;
static void *nvm_mutex;

/*---local function prototypes for this file---------------------------------*/

/*---local function definitions----------------------------------------------*/

static void ReadSecureData(const char *filename);
static void WriteSecureData(const char *filename);
static void DecryptSecureData(U8BIT *data, U32BIT len);
static void EncryptSecureData(U8BIT *data, U32BIT len);
static void GetSecureDataKey(U8BIT **key, U8BIT **iv);

/*---global function definitions---------------------------------------------*/

/**
 * @brief   Initialises the heap
 * @note     Does nothing on this platform, already taken care of by Linux
 */
void  STB_MEMInitialiseRAM(void)
{
   FUNCTION_START(STB_MEMInitialiseRAM);
   FUNCTION_FINISH(STB_MEMInitialiseRAM);
}

/**
 * @brief   Allocates a new block of memory for system use
 * @param   bytes Size of required block in bytes
 * @return  A pointer to the allocated block of memory
 * @return  NULL allocation failed
 */
void* STB_MEMGetSysRAM(U32BIT bytes)
{
   void *retval = NULL;

   FUNCTION_START(STB_MEMGetSysRAM);

   if (bytes > 0)
   {
      retval = malloc((size_t)bytes);
      if (retval == NULL)
      {
         HEAP_DBG(("*** OUT OF MEMORY ***\n"));
      }
   }
   FUNCTION_FINISH(STB_MEMGetSysRAM);

   return(retval);
}

/**
 * @brief   Changes the size of the given block of memory
 * @param   ptr pointer to memory already allocated from the system heap
 * @param   new_num_bytes size of the memory block to be returned
 * @return  Address of new block of memory
 */
void* STB_MEMResizeSysRAM(void *ptr, U32BIT new_num_bytes)
{
   void *new_ptr;

   FUNCTION_START(STB_MEMResizeSysRAM);

   new_ptr = NULL;

   if ((ptr != NULL) && (new_num_bytes > 0))
   {
      new_ptr = realloc(ptr, new_num_bytes);
#ifdef HEAP_DEBUG
      if (new_ptr == NULL)
      {
         HEAP_DBG(("Failed to reallocate %u bytes for %p", new_num_bytes, ptr));
      }
#endif
   }

   FUNCTION_FINISH(STB_MEMResizeSysRAM);

   return(new_ptr);
}

/**
 * @brief   Releases a previously allocated block of system memory
 * @param   block_ptr address of block to be released
 */
void STB_MEMFreeSysRAM(void *block_ptr)
{
   FUNCTION_START(STB_MEMFreeSysRAM);

   if (block_ptr != NULL)
   {
      free(block_ptr);
   }

   FUNCTION_FINISH(STB_MEMFreeSysRAM);
}

/**
 * @brief   Returns the amount of available system memory consumed
 * @return  Memory used as a percentage of available
 */
U8BIT STB_MEMSysRAMUsed(void)
{
   U8BIT used_result = 0;

   FUNCTION_START(STB_MEMSysRAMUsed);

   FUNCTION_FINISH(STB_MEMSysRAMUsed);

   return used_result;
}

/**
 * @brief   Allocates a new block of memory for application use
 * @param   bytes Size of required block in bytes
 * @return  A pointer to the allocated block of memory
 * @return  NULL allocation failed
 */
void* STB_MEMGetAppRAM(U32BIT bytes)
{
   void *retval = NULL;

   FUNCTION_START(STB_MEMGetAppRAM);

   if (bytes > 0)
   {
      retval = malloc((size_t)bytes);
      if (retval == NULL)
      {
         HEAP_DBG(("*** OUT OF MEMORY ***\n"));
      }
   }

   FUNCTION_FINISH(STB_MEMGetAppRAM);

   return(retval);
}

/**
 * @brief   Changes the size of the given block of memory
 * @param   ptr pointer to memory already allocated from the app heap
 * @param   new_num_bytes size of the memory block to be returned
 * @return  Address of new block of memory
 */
void* STB_MEMResizeAppRAM(void *ptr, U32BIT new_num_bytes)
{
   void *new_ptr;

   FUNCTION_START(STB_MEMResizeAppRAM);

   new_ptr = NULL;

   if ((ptr != NULL) && (new_num_bytes > 0))
   {
      new_ptr = realloc(ptr, new_num_bytes);
#ifdef HEAP_DEBUG
      if (new_ptr == NULL)
      {
         HEAP_DBG(("Failed to reallocate %u bytes for %p", new_num_bytes, ptr));
      }
#endif
   }

   FUNCTION_FINISH(STB_MEMResizeAppRAM);

   return(new_ptr);
}

/**
 * @brief   Releases a previously allocated block of system memory
 * @param   block_ptr address of block to be released
 */
void STB_MEMFreeAppRAM(void *block_ptr)
{
   FUNCTION_START(STB_MEMFreeAppRAM);

   if (block_ptr != NULL)
   {
      free(block_ptr);
   }

   FUNCTION_FINISH(STB_MEMFreeAppRAM);
}

/**
 * @brief   Returns the amount of available application memory consumed
 * @return  Memory used as a percentage of available
 * @todo     Add STB810 code
 */
U8BIT STB_MEMAppRAMUsed(void)
{
   U8BIT used_result = 0;

   FUNCTION_START(STB_MEMFreeAppRAM);

   FUNCTION_FINISH(STB_MEMFreeAppRAM);

   return used_result;
}

/**
 * @brief   Initialises Non-Volatile memory access
 * For a new NVM device, this function formats it ready for use, otherwise
 * it prepares existing data for access
 */
void STB_MEMInitialiseNVM(void)
{
   FILE *fp;
   long file_size = 0;
   U8BIT *nvm_data;

   fp = fopen(nvm_file_string, "r");
   if (fp != NULL)
   {
      /* Move to end of file to check it's the correct size */
      fseek(fp, 0, SEEK_END);
      file_size = ftell(fp);
      fclose(fp);
   }

   if ((fp == NULL) || (file_size != NV_DEVICE_SIZE))
   {
      /* Need to create a new nvm file */
      fp = fopen(nvm_file_string, "w");
      if (fp != NULL)
      {
         nvm_data = (U8BIT *)STB_MEMGetSysRAM(NV_DEVICE_SIZE);
         if (nvm_data != NULL)
         {
            memset(nvm_data, 0, NV_DEVICE_SIZE);
            fwrite(nvm_data, 1, NV_DEVICE_SIZE, fp);

            STB_MEMFreeSysRAM(nvm_data);
         }

         fclose(fp);
      }
#ifdef NVM_DEBUG
      else
      {
         NVM_DBG(("STB_MEMInitialiseNVM: Failed to open %s, error %d", nvm_file_string, errno));
      }
#endif
   }

   nvm_mutex = STB_OSCreateMutex();
   if (nvm_mutex == NULL)
   {
      STB_SPDebugWrite("STB_OSCreateMutex failed\n");
   }

   /* Read secure data from NVM */
   ReadSecureData(secure_data_file);
}

/**
 * @brief   Read data from the NVM
 * @param   addr The NVM start address
 * @param   bytes The number of bytes to read
 * @param   dst_addr Callers buffer for the data
 * @return  TRUE if successful
 * @return  FALSE if there was a problem
 */
BOOLEAN STB_MEMReadNVM(U32BIT addr, U32BIT bytes, U8BIT *dst_addr)
{
   BOOLEAN ret_val = FALSE;
   void *fp;

   FUNCTION_START(STB_MEMReadNVM);

   NVM_DBG(("STB_MEMReadNVM(%lu, %lu, %p)", addr, bytes, dst_addr));

   if ((addr + bytes) <= NV_DEVICE_SIZE)
   {
      ASSERT((addr + bytes) <= NV_DEVICE_SIZE);
      ASSERT(dst_addr != NULL);

      fp = fopen(nvm_file_string, "r");
      ASSERT(fp != NULL);

      if (fp != NULL)
      {
         if (fseek(fp, addr, SEEK_SET) == 0)
         {
            if (fread(dst_addr, 1, bytes, fp) == bytes)
            {
               ret_val = TRUE;
            }
         }
#ifdef NVM_DEBUG
         else
         {
            NVM_DBG(("Failed to locate address in NVM datafile!"));
         }
#endif

         fclose(fp);
      }
#ifdef NVM_DEBUG
      else
      {
         NVM_DBG(("Failed to open NVM datafile for read!"));
      }
#endif
   }
#ifdef NVM_DEBUG
   else
   {
      NVM_DBG(("%s(%u, %u): Out of bounds!", __FUNCTION__, addr, bytes));
   }
#endif

   ASSERT(ret_val == TRUE);

   NVM_DBG((">> STB_MEMReadNVM OUT (ret %d) [%d %d %d %d]", ret_val, dst_addr[0], dst_addr[1], dst_addr[2], dst_addr[3]));

   FUNCTION_FINISH(STB_MEMReadNVM);

   return(ret_val);
}

/**
 * @brief   Writes data to the NVM
 * @param   addr The NVM start address to write to
 * @param   bytes The number of bytes to be written
 * @param   src_addr Pointer to the data to be written
 * @return  TRUE if successful
 * @return  FALSE if there was a problem
 */
BOOLEAN STB_MEMWriteNVM(U32BIT addr, U32BIT bytes, U8BIT *src_addr)
{
   BOOLEAN ret_val = FALSE;
   size_t actual;
   FILE *fp;

   FUNCTION_START(STB_MEMWriteNVM);

   NVM_DBG(("STB_MEMWriteNVM(%lu, %lu, %p)", addr, bytes, src_addr));

   if ((addr + bytes) <= NV_DEVICE_SIZE)
   {
      ASSERT((addr + bytes) <= NV_DEVICE_SIZE);
      ASSERT(src_addr != NULL);

      fp = fopen(nvm_file_string, "r+");
      ASSERT(fp != NULL);

      if (fp != NULL)
      {
         if (fseek(fp, addr, SEEK_SET) == 0)
         {
            actual = fwrite(src_addr, 1, bytes, fp);
            if (actual == bytes)
            {
               ret_val = TRUE;
            }
#ifdef NVM_DEBUG
            else
            {
               NVM_DBG(("NVM.DAT WRITE ERROR!!! Tried %ld, wrote %ld", bytes,
                        (unsigned long)actual));
            }
#endif
         }
#ifdef NVM_DEBUG
         else
         {
            NVM_DBG(("Failed to locate address in NVM datafile!"));
         }
#endif

         fclose(fp);
      }
#ifdef NVM_DEBUG
      else
      {
         NVM_DBG(("Failed to open NVM datafile for write!"));
      }
#endif
   }
#ifdef NVM_DEBUG
   else
   {
      NVM_DBG(("%s(%u, %u): Out of bounds!", __FUNCTION__, addr, bytes));
   }
#endif

   ASSERT(ret_val == TRUE);

   NVM_DBG((">> STB_MEMWriteNVM OUT (ret %d) [%d %d %d %d]", ret_val, src_addr[0], src_addr[1], src_addr[2], src_addr[3]));

   FUNCTION_FINISH(STB_MEMWriteNVM);

   return(ret_val);
}

/**
 * @brief   Returns the size (capacity) of the NVM
 * @return  Size of the NVM in bytes
 */
U32BIT STB_MEMGetNVMSize(void)
{
   FUNCTION_START(STB_MEMGetNVMSize);
   FUNCTION_FINISH(STB_MEMGetNVMSize);
   return(NV_DEVICE_SIZE);
}

/**
 * @brief   Returns any offset required in NVM to avoid private data
 * @return  The offset in bytes
 */
U32BIT STB_MEMGetNVMOffset(void)
{
   FUNCTION_START(STB_MEMGetNVMOffset);
   FUNCTION_FINISH(STB_MEMGetNVMOffset);
   return(0);
}

/**
 * @brief   Returns the word alignment size of the NVM device
 * @return  Alignment in bytes
 */
U8BIT STB_MEMGetNVMAlign(void)
{
   FUNCTION_START(STB_MEMGetNVMAlign);
   FUNCTION_FINISH(STB_MEMGetNVMAlign);
   return(1);
}

/**
 * @brief   Configures the multiloader data area in NVM with the required
 *          data to start an upgrade.
 * @param   U8BIT *del_sys_desc Terrestrial Delivery system descriptor as
 *          found in the NIT. Expected length is 11 bytes.
 * @param   U8BIT num_attempts
 * @return  TRUE if successful, FALSE otherwise
 * @warning None.
 * @bug     None.
 */
BOOLEAN STB_MEMConfigMloaderForUpgrade(void *loader_data, U32BIT data_size)
{
   USE_UNWANTED_PARAM(loader_data);
   USE_UNWANTED_PARAM(data_size);
   return TRUE;
}

/**
 * @brief   Reads from the memory area allocated for the OTA loader
 * @param   void *buffer pointer to the destination buffer
 * @param   U32BIT size number of bytes to read
 * @return  TRUE if successful, FALSE otherwise
 * @warning None.
 * @bug     None.
 */
BOOLEAN STB_MEMReadMloaderData(void *buffer, U32BIT size)
{
   USE_UNWANTED_PARAM(buffer);
   USE_UNWANTED_PARAM(size);
   return TRUE;
}

/**
 * @brief   Writes from the memory area allocated for the OTA loader
 * @param   void *buffer pointer to the source buffer
 * @param   U32BIT size number of bytes to write
 * @return  TRUE if successful, FALSE otherwise
 * @warning None.
 * @bug     None.
 */
BOOLEAN STB_MEMWriteMloaderData(void *buffer, U32BIT size)
{
   USE_UNWANTED_PARAM(buffer);
   USE_UNWANTED_PARAM(size);
   return TRUE;
}

/**
 * @brief   Read variable defined by the given key from secure NVM
 * @param   key data item to be read
 * @param   len returned length of data read
 * @return  Pointer to data read, or NULL on failure.
 * @note    The data must be released using STB_MEMReleaseSecureVariable
 */
void* STB_MEMReadSecureVariable(U8BIT key, U32BIT *len)
{
   S_SECURE_DATA *item;
   void *value;

   FUNCTION_START(STB_MEMReadSecureVariable);

   value = NULL;

   STB_OSMutexLock(nvm_mutex);

   for (item = secure_data_list; item != NULL; item = item->next)
   {
      if (item->key == key)
      {
         value = item->value;
         *len = item->size;
         break;
      }
   }

   STB_OSMutexUnlock(nvm_mutex);

   FUNCTION_FINISH(STB_MEMReadSecureVariable);

   return(value);
}

/**
 * @brief   Releas the data returned by STB_MEMReadSecureVariable
 * @param   value pointer to data to be released
 */
void STB_MEMReleaseSecureVariable(void *value)
{
   FUNCTION_START(STB_MEMReleaseSecureVariable);
   USE_UNWANTED_PARAM(value);
   FUNCTION_FINISH(STB_MEMReleaseSecureVariable);
}

/**
 * @brief   Writes data defined by the given key to secure NVM
 * @param   key data item to be written
 * @param   value pointer to data to be written
 * @param   len data size
 * @return  TRUE if the data is written successfully, FALSE otherwise
 */
BOOLEAN STB_MEMWriteSecureVariable(U8BIT key, void *value, U32BIT len)
{
   S_SECURE_DATA *item;
   void *new_value;
   BOOLEAN result;

   FUNCTION_START(STB_MEMWriteSecureVariable);

   result = FALSE;

   STB_OSMutexLock(nvm_mutex);

   for (item = secure_data_list; item != NULL; item = item->next)
   {
      if (item->key == key)
      {
         if (AES_PAD(item->size) == AES_PAD(len))
         {
            /* Can just overwrite the existing data */
            memcpy(item->value, value, len);
            if (AES_PAD(len) > len)
            {
               memset((char *)item->value + len, 0, AES_PAD(len) - len);
            }
            item->size = len;
            result = TRUE;
            break;
         }
         else
         {
            /* Need to change the size of data being stored */
            new_value = (void *)STB_MEMGetSysRAM(AES_PAD(len));
            if (new_value != NULL)
            {
               if (item->value != NULL)
               {
                  STB_MEMFreeSysRAM(item->value);
               }

               memcpy(new_value, value, len);
               if (AES_PAD(len) > len)
               {
                  memset((char *)item->value + len, 0, AES_PAD(len) - len);
               }
               item->value = new_value;
               item->size = len;
               result = TRUE;
            }
            break;
         }
      }
   }

   if (!result && (item == NULL))
   {
      /* Key hasn't been found so add it */
      item = (S_SECURE_DATA *)STB_MEMGetSysRAM(sizeof(S_SECURE_DATA));
      if (item != NULL)
      {
         item->value = (void *)STB_MEMGetSysRAM(AES_PAD(len));
         if (item->value != NULL)
         {
            item->key = key;
            item->size = len;

            memcpy(item->value, value, len);
            if (AES_PAD(len) > len)
            {
               memset((char *)item->value + len, 0, AES_PAD(len) - len);
            }

            item->next = secure_data_list;
            secure_data_list = item;

            result = TRUE;
         }
      }
   }

   if (result)
   {
      WriteSecureData(secure_data_file);
   }

   STB_OSMutexUnlock(nvm_mutex);

   FUNCTION_FINISH(STB_MEMWriteSecureVariable);

   return(result);
}

/**
 * @brief   Read constant defined by the given key from secure NVM
 * @param   key data item to be read
 * @param   len returned length of data read
 * @return  Pointer to data read, or NULL on failure.
 * @note    The data must not be released (it is managed by the platform layer)
 */
const void* STB_MEMReadSecureConstant(U8BIT key, U32BIT *len)
{
   S_SECURE_DATA *item;
   void *value;

   FUNCTION_START(STB_MEMReadSecureConstant);

   value = NULL;

   STB_OSMutexLock(nvm_mutex);

   for (item = secure_data_list; item != NULL; item = item->next)
   {
      if (item->key == key)
      {
         value = item->value;
         *len = item->size;
         break;
      }
   }

   STB_OSMutexUnlock(nvm_mutex);

   FUNCTION_FINISH(STB_MEMReadSecureConstant);

   return(value);
}

/******************************************************************************
 * @brief   Returns the size in KB of the given file
 * @param   filename - name of the file in the nvm
 * @param   filesize - returned value giving the file size in KB
 * @return  TRUE if the file exists, FALSE otherwise
 ******************************************************************************/
BOOLEAN STB_NVMFileSize(U8BIT *filename, U32BIT *filesize)
{
   FUNCTION_START(STB_NVMFileSize);
   USE_UNWANTED_PARAM(filename);
   USE_UNWANTED_PARAM(filesize);
   FUNCTION_FINISH(STB_NVMFileSize);
   return(FALSE);
}

/******************************************************************************
 * @brief   Opens an existing file or creates a new one.
 * @param   name - The filename (including path). When the mode is
 *          FILE_MODE_OVERWRITE, the directories in the path will be created
 *          when they don't exist.
 * @param   mode - The access mode
 * @return  The file handle
 ******************************************************************************/
void* STB_NVMOpenFile(U8BIT *name, E_STB_DSK_FILE_MODE mode)
{
   FUNCTION_START(STB_NVMOpenFile);
   USE_UNWANTED_PARAM(name);
   USE_UNWANTED_PARAM(mode);
   FUNCTION_FINISH(STB_NVMOpenFile);
   return(NULL);
}

/******************************************************************************
 * @brief    Flushes and closes an open file
 * @param    file - The file handle
 ******************************************************************************/
void STB_NVMCloseFile(void *file)
{
   FUNCTION_START(STB_NVMCloseFile);
   USE_UNWANTED_PARAM(file);
   FUNCTION_FINISH(STB_NVMCloseFile);
}

/******************************************************************************
 * @brief    Reads data from an open file
 * @param[in]    file - The file handle
 * @param[out]    data - The caller's buffer
 * @param[in]    size - Number of bytes to be read
 * @return   Number of bytes successfully read
 ******************************************************************************/
U32BIT STB_NVMReadFile(void *file, U8BIT *data, U32BIT size)
{
   FUNCTION_START(STB_NVMReadFile);
   USE_UNWANTED_PARAM(file);
   USE_UNWANTED_PARAM(data);
   USE_UNWANTED_PARAM(size);
   FUNCTION_FINISH(STB_NVMReadFile);
   return(0);
}

/******************************************************************************
 * @brief    Writes data to an open file
 * @param    file - The file handle
 * @param    data - Pointer to the data to be written
 * @param    size - Number of bytes to  write
 * @return   Number of bytes successfully written
 ******************************************************************************/
U32BIT STB_NVMWriteFile(void *file, U8BIT *data, U32BIT size)
{
   FUNCTION_START(STB_NVMWriteFile);
   USE_UNWANTED_PARAM(file);
   USE_UNWANTED_PARAM(data);
   USE_UNWANTED_PARAM(size);
   FUNCTION_FINISH(STB_NVMWriteFile);
   return(0);
}

/******************************************************************************
 * @brief   Deletes the file.
 * @param   filename - pathname of the file to be deleted
 * @retval  TRUE if successful, FALSE otherwise
 ******************************************************************************/
BOOLEAN STB_NVMDeleteFile(U8BIT *filename)
{
   FUNCTION_START(STB_NVMDeleteFile);
   USE_UNWANTED_PARAM(filename);
   FUNCTION_FINISH(STB_NVMDeleteFile);
   return(FALSE);
}

/******************************************************************************
 * @brief   Opens a directory in order to read the entries
 * @param   dir_name - name of the directory to open
 * @return  Handle to be used in all other operations, NULL if the open fails
 ******************************************************************************/
void* STB_NVMOpenDirectory(U8BIT *dir_name)
{
   FUNCTION_START(STB_NVMOpenDirectory);
   USE_UNWANTED_PARAM(dir_name);
   FUNCTION_FINISH(STB_NVMOpenDirectory);
   return(NULL);
}

/******************************************************************************
 * @brief   Reads the next entry from the directory, returning the name of the
 *          entry and the type of the entry
 * @param   dir - handle returned when the directory was opened
 * @param   filename - array in which the name is returned
 * @param   filename_len - size of the filename array
 * @param   entry_type - type of entry
 * @return  TRUE if and entry is read, FALSE otherwise which could indicate end
 *          of the directory
 ******************************************************************************/
BOOLEAN STB_NVMReadDirectory(void *dir, U8BIT *filename, U16BIT filename_len,
   E_STB_DIR_ENTRY_TYPE *entry_type)
{
   FUNCTION_START(STB_NVMReadDirectory);
   USE_UNWANTED_PARAM(dir);
   USE_UNWANTED_PARAM(filename);
   USE_UNWANTED_PARAM(filename_len);
   USE_UNWANTED_PARAM(entry_type);
   FUNCTION_FINISH(STB_NVMReadDirectory);
   return(FALSE);
}

/******************************************************************************
 * @brief   Closes the directory for reading
 * @param   dir - directory handle
 ******************************************************************************/
void STB_NVMCloseDirectory(void *dir)
{
   FUNCTION_START(STB_NVMCloseDirectory);
   USE_UNWANTED_PARAM(dir);
   FUNCTION_FINISH(STB_NVMCloseDirectory);
}

/*---local function definitions----------------------------------------------*/

/**
 * @brief   Read secure data from a file
 * @param   const char *filename name of secure data file
 * @warning None.
 * @bug     None.
 */
static void ReadSecureData(const char *filename)
{
   FILE *fp;
   U8BIT key;
   U32BIT data_size;
   U32BIT padded_size;
   S_SECURE_DATA *item;
   U8BIT raw_data_size[4];
   size_t nbytes;

   fp = fopen(filename, "r");
   if (fp != NULL)
   {
      /* Read item key */
      nbytes = fread(&key, sizeof(key), 1, fp);
      while (nbytes == sizeof(key))
      {
         /* Read data size: this is the "raw" size. The file actually
          * contains encrypted data which is padded for AES.
          */
         nbytes = fread(&raw_data_size, 1, sizeof(raw_data_size), fp);
         if (nbytes != sizeof(raw_data_size))
         {
            /* Invalid data */
            break;
         }

         data_size = (raw_data_size[0] << 24 | raw_data_size[1] << 16 |
                      raw_data_size[2] << 8 | raw_data_size[3]);
         padded_size = AES_PAD(data_size);

         #ifdef SECURE_DEBUG
         SEC_DBG(("ReadSecureData: Reading key %d, len %ld (%ld)",
                  key, data_size, padded_size));
         #endif

         item = STB_MEMGetSysRAM(sizeof(S_SECURE_DATA));
         if (item != NULL)
         {
            item->value = STB_MEMGetSysRAM(padded_size);
            if (item->value != NULL)
            {
               /* Read item data (encrypted) */
               nbytes = fread(item->value, 1, padded_size, fp);
               if (nbytes != padded_size)
               {
                  /* Invalid data */
                  SEC_DBG(("ReadSecureData: Invalid data (%ld != %ld)",
                           nbytes, padded_size));
                  STB_MEMFreeSysRAM(item->value);
                  STB_MEMFreeSysRAM(item);
                  break;
               }

               /* Decrypt secure data (in place) */
               DecryptSecureData(item->value, padded_size);

               #ifdef SECURE_DEBUG
               {
                  U16BIT i;

                  for (i = 0; i < data_size; i++)
                  {
                     printf("%02x ", ((U8BIT *)item->value)[i]);
                     if (i % 16 == 15)
                     {
                        printf("\n");
                     }
                  }
                  if (data_size % 16 != 0)
                  {
                     printf("\n");
                  }
               }
               #endif

               /* Add item */
               item->key = key;
               item->size = data_size;
               item->next = secure_data_list;
               secure_data_list = item;
            }
            else
            {
               /* Out of memory */
               STB_MEMFreeSysRAM(item);
            }
         }

         /* Read next item key (if possible) */
         nbytes = fread(&key, sizeof(key), 1, fp);
      }

      fclose(fp);
   }
}

/**
 * @brief   Write secure data into a file
 * @param   const char *filename name of secure data file
 * @warning None.
 * @bug     None.
 */
static void WriteSecureData(const char *filename)
{
   FILE *fp;
   S_SECURE_DATA *item;
   U8BIT raw_size[4];

   /* Write values to secure storage */
   fp = fopen(filename, "w");
   if (fp != NULL)
   {
      /* Save each item to file */
      for (item = secure_data_list; item != NULL; item = item->next)
      {
         /* Write item key */
         fwrite(&item->key, 1, sizeof(item->key), fp);

         /* Write item size (raw) */
         raw_size[0] = (U8BIT)(item->size >> 24);
         raw_size[1] = (U8BIT)(item->size >> 16);
         raw_size[2] = (U8BIT)(item->size >> 8);
         raw_size[3] = (U8BIT)item->size;
         fwrite(raw_size, 1, sizeof(raw_size), fp);

         /* Write encryped item data */
         EncryptSecureData(item->value, AES_PAD(item->size));
         fwrite(item->value, 1, AES_PAD(item->size), fp);
         DecryptSecureData(item->value, AES_PAD(item->size));
      }

      fclose(fp);
   }
}

/**
 * @brief   Decrypt secure data
 * @param   U8BIT *data data to decrypt
 * @param   U32BIT len number of bytes to decrypt (multiple of 16)
 * @warning None.
 * @bug     None.
 */
static void DecryptSecureData(U8BIT *data, U32BIT len)
{
   U8BIT *key, *iv;
   U8BIT local_iv[16];

   GetSecureDataKey(&key, &iv);
   memcpy(local_iv, iv, sizeof local_iv);
   STB_CRYPTAesCbcDecrypt(data, data, len, key, local_iv);
}

/**
 * @brief   Encrypt secure data
 * @param   U8BIT *data data to decrypt
 * @param   U32BIT len number of bytes to encrypt (multiple of 16)
 * @warning None.
 * @bug     None.
 */
static void EncryptSecureData(U8BIT *data, U32BIT len)
{
   U8BIT *key, *iv;
   U8BIT local_iv[16];

   GetSecureDataKey(&key, &iv);
   memcpy(local_iv, iv, sizeof local_iv);
   STB_CRYPTAesCbcEncrypt(data, data, len, key, local_iv);
}

/**
 * @brief   Return secure data key and IV
 * @param   U8BIT **key AES-128-CBC key (pointer to static data)
 * @param   U8BIT **iv AES-128-CBC IV (pointer to static data)
 * @warning None.
 * @bug     None.
 */
static void GetSecureDataKey(U8BIT **key, U8BIT **iv)
{
   /* dummy values - should be read from "real" secure NVM */
   static U8BIT dummy_key[] = {
      0x13, 0xd2, 0xff, 0x70, 0xa5, 0xd5, 0xb2, 0x37,
      0xdf, 0xdd, 0x56, 0xff, 0xa1, 0xf7, 0xeb, 0xff
   };
   static U8BIT dummy_iv[] = {
      0x2f, 0x06, 0xd8, 0xe6, 0x34, 0x42, 0xf2, 0xa7,
      0x10, 0xc0, 0x26, 0x06, 0x0d, 0x38, 0x7a, 0xab
   };

   *key = dummy_key;
   *iv = dummy_iv;
}

