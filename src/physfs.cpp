///                                                                           
/// PhysicsFS; a portable, flexible file i/o abstraction.                     
/// Documentation is in physfs.h. It's verbose, honest.  :)                   
/// Please see the file LICENSE.txt in the source's root directory.           
/// This file written by Ryan C. Gordon.                                      
///                                                                           
#include "physfs_internal.hpp"
#include <cassert>


struct DirHandle
{
   // Instance data unique to the archiver                              
   void* opaque;
   // Path to archive in platform-dependent notation                    
   char* dirName;
   // Mountpoint in virtual file tree                                   
   char* mountPoint;
   // Subdirectory of archiver to use as root of archive (nullptr for   
   // actual root)                                                      
   char* root;
   // Subdirectory of archiver to use as root of archive (nullptr for   
   // actual root)                                                      
   size_t rootlen;
   // Ptr to archiver info for this handle                              
   const PHYSFS_Archiver* funcs;
   // Linked list stuff                                                 
   struct DirHandle* next;
};

struct FileHandle
{
   // Instance data unique to the archiver for this file                
   PHYSFS_Io* io;
   // Non-zero if reading, zero if write/append                         
   PHYSFS_uint8 forReading;
   // Archiver instance that created this                               
   const DirHandle* dirHandle;
   // Buffer, if set (nullptr otherwise). Don't touch!                  
   PHYSFS_uint8* buffer;
   // Bufsize, if set (0 otherwise). Don't touch!                       
   size_t bufsize;
   // Buffer fill size. Don't touch!                                    
   size_t buffill;
   // Buffer position. Don't touch!                                     
   size_t bufpos;
   // linked list stuff                                                 
   struct FileHandle* next;
};

struct ErrState
{
   void* tid;
   PHYSFS_ErrorCode code;
   struct ErrState* next;
};


/// General PhysicsFS state ...                                               
static int initialized = 0;
static ErrState* errorStates = nullptr;
static DirHandle* searchPath = nullptr;
static DirHandle* writeDir = nullptr;
static FileHandle* openWriteList = nullptr;
static FileHandle* openReadList = nullptr;
static char* baseDir = nullptr;
static char* userDir = nullptr;
static char* prefDir = nullptr;
static int allowSymLinks = 0;
static PHYSFS_Archiver** archivers = nullptr;
static PHYSFS_ArchiveInfo** archiveInfo = nullptr;
static volatile size_t numArchivers = 0;
static size_t longest_root = 0;

/// Mutexes ...                                                               
static void* errorLock = nullptr;     // Protects error message list    
static void* stateLock = nullptr;     // Protects other PhysFS states   

#ifdef PHYSFS_NEED_ATOMIC_OP_FALLBACK
   static int __PHYSFS_atomicAdd(int* ptrval, const int val) {
      int retval;
      __PHYSFS_platformGrabMutex(stateLock);
      *ptrval += val;
      retval = *ptrval;
      __PHYSFS_platformReleaseMutex(stateLock);
      return retval;
   }

   int __PHYSFS_ATOMIC_INCR(int* ptrval) {
      return __PHYSFS_atomicAdd(ptrval, 1);
   }

   int __PHYSFS_ATOMIC_DECR(int* ptrval) {
      return __PHYSFS_atomicAdd(ptrval, -1);
   }
#endif


///                                                                           
/// PHYSFS_Io implementation for i/o to physical filesystem...                
///                                                                           
/* !!! FIXME: maybe refcount the paths in a string pool? */
typedef struct __PHYSFS_NativeIoInfo
{
   void* handle;
   const char* path;
   int mode;   /* 'r', 'w', or 'a' */
} NativeIoInfo;

static PHYSFS_sint64 nativeIo_read(PHYSFS_Io* io, void* buf, PHYSFS_uint64 len) {
   NativeIoInfo* info = (NativeIoInfo*) io->opaque;
   return __PHYSFS_platformRead(info->handle, buf, len);
}

static PHYSFS_sint64 nativeIo_write(PHYSFS_Io* io, const void* buffer,
   PHYSFS_uint64 len) {
   NativeIoInfo* info = (NativeIoInfo*) io->opaque;
   return __PHYSFS_platformWrite(info->handle, buffer, len);
}

static int nativeIo_seek(PHYSFS_Io* io, PHYSFS_uint64 offset) {
   NativeIoInfo* info = (NativeIoInfo*) io->opaque;
   return __PHYSFS_platformSeek(info->handle, offset);
}

static PHYSFS_sint64 nativeIo_tell(PHYSFS_Io* io) {
   NativeIoInfo* info = (NativeIoInfo*) io->opaque;
   return __PHYSFS_platformTell(info->handle);
}

static PHYSFS_sint64 nativeIo_length(PHYSFS_Io* io) {
   NativeIoInfo* info = (NativeIoInfo*) io->opaque;
   return __PHYSFS_platformFileLength(info->handle);
}

static PHYSFS_Io* nativeIo_duplicate(PHYSFS_Io* io) {
   NativeIoInfo* info = (NativeIoInfo*) io->opaque;
   return __PHYSFS_createNativeIo(info->path, info->mode);
}

static int nativeIo_flush(PHYSFS_Io* io) {
   NativeIoInfo* info = (NativeIoInfo*) io->opaque;
   return __PHYSFS_platformFlush(info->handle);
}

static void nativeIo_destroy(PHYSFS_Io* io) {
   auto info = static_cast<NativeIoInfo*>(io->opaque);
   __PHYSFS_platformClose(info->handle);
   PHYSFS_Allocator<>::Free((void*) info->path);
   PHYSFS_Allocator<>::Free((void*) info);
   PHYSFS_Allocator<>::Free((void*) io);
}

static const PHYSFS_Io __PHYSFS_nativeIoInterface =
{
   CURRENT_PHYSFS_IO_API_VERSION, nullptr,
   nativeIo_read,
   nativeIo_write,
   nativeIo_seek,
   nativeIo_tell,
   nativeIo_length,
   nativeIo_duplicate,
   nativeIo_flush,
   nativeIo_destroy
};

PHYSFS_Io* __PHYSFS_createNativeIo(const char* path, const int mode) {
   assert((mode == 'r') || (mode == 'w') || (mode == 'a'));
   auto io = PHYSFS_Allocator<PHYSFS_Io>(1, __PHYSFS_nativeIoInterface);
   auto info = PHYSFS_Allocator<NativeIoInfo>(1);
   auto pathdup = PHYSFS_Allocator<char>(strlen(path) + 1);

   void* handle = nullptr;
   if (mode == 'r')
      handle = __PHYSFS_platformOpenRead(path);
   else if (mode == 'w')
      handle = __PHYSFS_platformOpenWrite(path);
   else if (mode == 'a')
      handle = __PHYSFS_platformOpenAppend(path);

   if (not handle)
      MetaPhysFS::Throw<PHYSFS_ERR_OUT_OF_MEMORY>();

   strcpy(pathdup.Get(), path);
   info->handle = handle;
   info->path = pathdup.Ref();
   info->mode = mode;
   io->opaque = info.Ref();
   return io.Ref();
}


///                                                                           
/// PHYSFS_Io implementation for i/o to a memory buffer...                    
///                                                                           
struct MemoryIoInfo {
   const PHYSFS_uint8* buf;
   PHYSFS_uint64 len;
   PHYSFS_uint64 pos;
   PHYSFS_Io* parent;
   int refcount;
   void (*destruct)(void*);
};

static PHYSFS_sint64 memoryIo_read(PHYSFS_Io* io, void* buf, PHYSFS_uint64 len) {
   MemoryIoInfo* info = (MemoryIoInfo*) io->opaque;
   const PHYSFS_uint64 avail = info->len - info->pos;
   assert(avail <= info->len);

   if (avail == 0)
      return 0;  // We're at EOF; nothing to do                         

   if (len > avail)
      len = avail;

   memcpy(buf, info->buf + info->pos, (size_t) len);
   info->pos += len;
   return len;
}

static PHYSFS_sint64 memoryIo_write(PHYSFS_Io*, const void*, PHYSFS_uint64) {
   BAIL(PHYSFS_ERR_OPEN_FOR_READING, -1);
}

static int memoryIo_seek(PHYSFS_Io* io, PHYSFS_uint64 offset) {
   MemoryIoInfo* info = (MemoryIoInfo*) io->opaque;
   BAIL_IF(offset > info->len, PHYSFS_ERR_PAST_EOF, 0);
   info->pos = offset;
   return 1;
}

static PHYSFS_sint64 memoryIo_tell(PHYSFS_Io* io) {
   const MemoryIoInfo* info = (MemoryIoInfo*) io->opaque;
   return (PHYSFS_sint64) info->pos;
}

static PHYSFS_sint64 memoryIo_length(PHYSFS_Io* io) {
   const MemoryIoInfo* info = (MemoryIoInfo*) io->opaque;
   return (PHYSFS_sint64) info->len;
}

static PHYSFS_Io* memoryIo_duplicate(PHYSFS_Io* io) {
   auto info = static_cast<MemoryIoInfo*>(io->opaque);
   PHYSFS_Io* parent = info->parent;

   // Avoid deep copies                                                 
   assert(not parent or not ((MemoryIoInfo*) parent->opaque)->parent);

   // Share the buffer between duplicates                               
   if (parent != nullptr)  // Dup the parent, increment its refcount    
      return parent->duplicate(parent);

   // We're the parent                                                  
   auto newinfo = PHYSFS_Allocator<MemoryIoInfo>(1);
   (void) __PHYSFS_ATOMIC_INCR(&info->refcount);

   memset(newinfo.Get(), 0, sizeof(*info));
   newinfo->buf = info->buf;
   newinfo->len = info->len;
   newinfo->pos = 0;
   newinfo->parent = io;
   newinfo->refcount = 0;
   newinfo->destruct = nullptr;

   auto retval = PHYSFS_Allocator<PHYSFS_Io>(1, *io);
   retval->opaque = newinfo.Ref();
   return retval.Ref();
}

static int memoryIo_flush(PHYSFS_Io*) {
   return 1;  // It's read-only                                         
}

static void memoryIo_destroy(PHYSFS_Io* io) {
   auto info = static_cast<MemoryIoInfo*>(io->opaque);
   auto parent = info->parent;

   if (parent) {
      assert(info->buf == ((MemoryIoInfo*) info->parent->opaque)->buf);
      assert(info->len == ((MemoryIoInfo*) info->parent->opaque)->len);
      assert(info->refcount == 0);
      assert(info->destruct == nullptr);
      PHYSFS_Allocator<>::Free(info);
      PHYSFS_Allocator<>::Free(io);
      parent->destroy(parent);   // Decrements refcount                 
      return;
   }

   // We _are_ the parent                                               
   assert(info->refcount > 0);   // Even in a race, we hold a reference 

   if (__PHYSFS_ATOMIC_DECR(&info->refcount) == 0) {
      void (*destruct)(void*) = info->destruct;
      void* buf = (void*) info->buf;
      io->opaque = nullptr;      // Kill this here in case of race      
      PHYSFS_Allocator<>::Free(info);
      PHYSFS_Allocator<>::Free(io);
      if (destruct)
         destruct(buf);
   }
}

static const PHYSFS_Io __PHYSFS_memoryIoInterface =
{
   CURRENT_PHYSFS_IO_API_VERSION, nullptr,
   memoryIo_read,
   memoryIo_write,
   memoryIo_seek,
   memoryIo_tell,
   memoryIo_length,
   memoryIo_duplicate,
   memoryIo_flush,
   memoryIo_destroy
};

PHYSFS_Io* __PHYSFS_createMemoryIo(const void* buf, PHYSFS_uint64 len, void (*destruct)(void*)) {
   auto io = PHYSFS_Allocator<PHYSFS_Io>(1, __PHYSFS_memoryIoInterface);
   auto info = PHYSFS_Allocator<MemoryIoInfo>(1);

   memset(info.Get(), 0, sizeof(MemoryIoInfo));
   info->buf = (const PHYSFS_uint8*) buf;
   info->len = len;
   info->pos = 0;
   info->parent = nullptr;
   info->refcount = 1;
   info->destruct = destruct;
   io->opaque = info.Ref();
   return io.Ref();
}


///                                                                           
/// PHYSFS_Io implementation for i/o to a PHYSFS_File...                      
///                                                                           
static PHYSFS_sint64 handleIo_read(PHYSFS_Io* io, void* buf, PHYSFS_uint64 len) {
   return PHYSFS_readBytes((PHYSFS_File*) io->opaque, buf, len);
}

static PHYSFS_sint64 handleIo_write(PHYSFS_Io* io, const void* buffer,
   PHYSFS_uint64 len) {
   return PHYSFS_writeBytes((PHYSFS_File*) io->opaque, buffer, len);
}

static int handleIo_seek(PHYSFS_Io* io, PHYSFS_uint64 offset) {
   return PHYSFS_seek((PHYSFS_File*) io->opaque, offset);
}

static PHYSFS_sint64 handleIo_tell(PHYSFS_Io* io) {
   return PHYSFS_tell((PHYSFS_File*) io->opaque);
}

static PHYSFS_sint64 handleIo_length(PHYSFS_Io* io) {
   return PHYSFS_fileLength((PHYSFS_File*) io->opaque);
}

static PHYSFS_Io* handleIo_duplicate(PHYSFS_Io* io) {
   // There's no duplicate at the PHYSFS_File level, so we break the    
   // abstraction. We're allowed to: we're physfs.c!                    
   auto origfh = static_cast<FileHandle*>(io->opaque);
   auto newfh = PHYSFS_Allocator<FileHandle>(1);
   memset(newfh.Get(), 0, sizeof(FileHandle));

   newfh->io = origfh->io->duplicate(origfh->io);
   newfh->forReading = origfh->forReading;
   newfh->dirHandle = origfh->dirHandle;

   __PHYSFS_platformGrabMutex(stateLock);
      if (newfh->forReading) {
         newfh->next = openReadList;
         openReadList = newfh.Get();
      }
      else {
         newfh->next = openWriteList;
         openWriteList = newfh.Get();
      }
   __PHYSFS_platformReleaseMutex(stateLock);

   auto retval = PHYSFS_Allocator<PHYSFS_Io>(1, *io);
   retval->opaque = newfh.Ref();
   return retval.Ref();
}

static int handleIo_flush(PHYSFS_Io* io) {
   return PHYSFS_flush((PHYSFS_File*) io->opaque);
}

static void handleIo_destroy(PHYSFS_Io* io) {
   if (io->opaque)
      PHYSFS_close((PHYSFS_File*) io->opaque);
   PHYSFS_Allocator<>::Free(io);
}

static const PHYSFS_Io __PHYSFS_handleIoInterface = {
   CURRENT_PHYSFS_IO_API_VERSION, nullptr,
   handleIo_read,
   handleIo_write,
   handleIo_seek,
   handleIo_tell,
   handleIo_length,
   handleIo_duplicate,
   handleIo_flush,
   handleIo_destroy
};

static PHYSFS_Io* __PHYSFS_createHandleIo(PHYSFS_File* f) {
   auto io = PHYSFS_Allocator<PHYSFS_Io>(1, __PHYSFS_handleIoInterface);
   io->opaque = f;
   return io.Ref();
}


///                                                                           
/// Functions ...                                                             
///                                                                           

struct EnumStringListCallbackData {
   char** list;
   PHYSFS_uint32 size;
   PHYSFS_ErrorCode errcode;
};

static void enumStringListCallback(void* data, const char* str) {
   auto pecd = static_cast<EnumStringListCallbackData*>(data);
   if (pecd->errcode)
      return;

   auto ptr = PHYSFS_Allocator<>::Realloc(pecd->list, (pecd->size + 2) * sizeof(char*));
   auto newstr = PHYSFS_Allocator<char>(strlen(str) + 1);
   if (ptr)
      pecd->list = (char**) ptr;

   if (not ptr or not newstr) {
      pecd->errcode = PHYSFS_ERR_OUT_OF_MEMORY;
      pecd->list[pecd->size] = nullptr;
      PHYSFS_freeList(pecd->list);
      return;
   }

   strcpy(newstr.Get(), str);
   pecd->list[pecd->size] = newstr.Ref();
   pecd->size++;
}

static char** doEnumStringList(void (*func)(PHYSFS_StringCallback, void*)) {
   EnumStringListCallbackData ecd;
   memset(&ecd, '\0', sizeof(ecd));
   ecd.list = PHYSFS_Allocator<char*>(1).Ref();
   func(enumStringListCallback, &ecd);

   if (ecd.errcode) {
      PHYSFS_setErrorCode(ecd.errcode);
      return nullptr;
   }

   ecd.list[ecd.size] = nullptr;
   return ecd.list;
}

static void __PHYSFS_bubble_sort(void* a, size_t lo, size_t hi,
   int (*cmpfn)(void*, size_t, size_t),
   void (*swapfn)(void*, size_t, size_t)) {
   size_t i;
   int sorted;

   do {
      sorted = 1;
      for (i = lo; i < hi; i++) {
         if (cmpfn(a, i, i + 1) > 0) {
            swapfn(a, i, i + 1);
            sorted = 0;
         }
      }
   }
   while (not sorted);
}

static void __PHYSFS_quick_sort(
   void* a, size_t lo, size_t hi,
   int (*cmpfn)(void*, size_t, size_t),
   void (*swapfn)(void*, size_t, size_t)
) {
   size_t i, j, v;

   if ((hi - lo) <= PHYSFS_QUICKSORT_THRESHOLD)
      __PHYSFS_bubble_sort(a, lo, hi, cmpfn, swapfn);
   else {
      i = (hi + lo) / 2;

      if (cmpfn(a, lo, i) > 0)
         swapfn(a, lo, i);
      if (cmpfn(a, lo, hi) > 0)
         swapfn(a, lo, hi);
      if (cmpfn(a, i, hi) > 0)
         swapfn(a, i, hi);

      j = hi - 1;
      swapfn(a, i, j);
      i = lo;
      v = j;

      while (1) {
         while (cmpfn(a, ++i, v) < 0) { /* do nothing */ }
         while (cmpfn(a, --j, v) > 0) { /* do nothing */ }
         if (j < i)
            break;
         swapfn(a, i, j);
      }

      if (i != (hi - 1))
         swapfn(a, i, hi - 1);

      __PHYSFS_quick_sort(a, lo, j, cmpfn, swapfn);
      __PHYSFS_quick_sort(a, i + 1, hi, cmpfn, swapfn);
   }
}

void __PHYSFS_sort(
   void* entries, size_t max,
   int  (*cmpfn)(void*, size_t, size_t),
   void (*swapfn)(void*, size_t, size_t)
) {
   /// Quicksort w/ Bubblesort fallback algorithm inspired by code from here: 
   ///   https://www.cs.ubc.ca/spider/harrison/Java/sorting-demo.html         
   if (max > 0)
      __PHYSFS_quick_sort(entries, 0, max - 1, cmpfn, swapfn);
}

static ErrState* findErrorForCurrentThread(void) {
   ErrState* i;
   void* tid;

   if (errorLock)
      __PHYSFS_platformGrabMutex(errorLock);

   if (errorStates) {
      tid = __PHYSFS_platformGetThreadID();

      for (i = errorStates; i; i = i->next) {
         if (i->tid == tid) {
            if (errorLock)
               __PHYSFS_platformReleaseMutex(errorLock);
            return i;
         }
      }
   }

   if (errorLock != nullptr)
      __PHYSFS_platformReleaseMutex(errorLock);

   return nullptr;   // No error available                              
}

/// This doesn't reset the error state                                        
static inline PHYSFS_ErrorCode currentErrorCode(void) {
   const ErrState* err = findErrorForCurrentThread();
   return err ? err->code : PHYSFS_ERR_OK;
}

PHYSFS_ErrorCode PHYSFS_getLastErrorCode(void) {
   ErrState* err = findErrorForCurrentThread();
   const PHYSFS_ErrorCode retval = (err) ? err->code : PHYSFS_ERR_OK;
   if (err)
      err->code = PHYSFS_ERR_OK;
   return retval;
}

PHYSFS_DECL const char* PHYSFS_getErrorByCode(PHYSFS_ErrorCode code) {
   switch (code) {
   case PHYSFS_ERR_OK: return "no error";
   case PHYSFS_ERR_OTHER_ERROR: return "unknown error";
   case PHYSFS_ERR_OUT_OF_MEMORY: return "out of memory";
   case PHYSFS_ERR_NOT_INITIALIZED: return "not initialized";
   case PHYSFS_ERR_IS_INITIALIZED: return "already initialized";
   case PHYSFS_ERR_ARGV0_IS_NULL: return "argv[0] is nullptr";
   case PHYSFS_ERR_UNSUPPORTED: return "unsupported";
   case PHYSFS_ERR_PAST_EOF: return "past end of file";
   case PHYSFS_ERR_FILES_STILL_OPEN: return "files still open";
   case PHYSFS_ERR_INVALID_ARGUMENT: return "invalid argument";
   case PHYSFS_ERR_NOT_MOUNTED: return "not mounted";
   case PHYSFS_ERR_NOT_FOUND: return "not found";
   case PHYSFS_ERR_SYMLINK_FORBIDDEN: return "symlinks are forbidden";
   case PHYSFS_ERR_NO_WRITE_DIR: return "write directory is not set";
   case PHYSFS_ERR_OPEN_FOR_READING: return "file open for reading";
   case PHYSFS_ERR_OPEN_FOR_WRITING: return "file open for writing";
   case PHYSFS_ERR_NOT_A_FILE: return "not a file";
   case PHYSFS_ERR_READ_ONLY: return "read-only filesystem";
   case PHYSFS_ERR_CORRUPT: return "corrupted";
   case PHYSFS_ERR_SYMLINK_LOOP: return "infinite symbolic link loop";
   case PHYSFS_ERR_IO: return "i/o error";
   case PHYSFS_ERR_PERMISSION: return "permission denied";
   case PHYSFS_ERR_NO_SPACE: return "no space available for writing";
   case PHYSFS_ERR_BAD_FILENAME: return "filename is illegal or insecure";
   case PHYSFS_ERR_BUSY: return "tried to modify a file the OS needs";
   case PHYSFS_ERR_DIR_NOT_EMPTY: return "directory isn't empty";
   case PHYSFS_ERR_OS_ERROR: return "OS reported an error";
   case PHYSFS_ERR_DUPLICATE: return "duplicate resource";
   case PHYSFS_ERR_BAD_PASSWORD: return "bad password";
   case PHYSFS_ERR_APP_CALLBACK: return "app callback reported error";
   }

   return nullptr;  /* don't know this error code. */
}

void PHYSFS_setErrorCode(PHYSFS_ErrorCode errcode) {
   if (not errcode)
      return;

   auto err = findErrorForCurrentThread();
   if (not err) {
      err = PHYSFS_Allocator<ErrState>(1).Ref();
      memset(err, '\0', sizeof(ErrState));
      err->tid = __PHYSFS_platformGetThreadID();

      if (errorLock)
         __PHYSFS_platformGrabMutex(errorLock);

      err->next = errorStates;
      errorStates = err;

      if (errorLock)
         __PHYSFS_platformReleaseMutex(errorLock);
   }

   err->code = errcode;
}

const char* PHYSFS_getLastError() {
   const auto err = PHYSFS_getLastErrorCode();
   return err ? PHYSFS_getErrorByCode(err) : nullptr;
}

/// MAKE SURE that errorLock is held before calling this!                     
static void freeErrorStates() {
   ErrState* next;
   for (auto i = errorStates; i; i = next) {
      next = i->next;
      PHYSFS_Allocator<>::Free(i);
   }

   errorStates = nullptr;
}

void PHYSFS_getLinkedVersion(PHYSFS_Version* ver) {
   if (not ver)
      return;
   ver->major = PHYSFS_VER_MAJOR;
   ver->minor = PHYSFS_VER_MINOR;
   ver->patch = PHYSFS_VER_PATCH;
}

static const char* find_filename_extension(const char* fname) {
   const char* retval = nullptr;
   if (fname) {
      const char* p = strchr(fname, '.');
      retval = p;

      while (p != nullptr) {
         p = strchr(p + 1, '.');
         if (p != nullptr)
            retval = p;
      }

      if (retval != nullptr)
         retval++;  /* skip '.' */
   }

   return retval;
}

static DirHandle* tryOpenDir(
   PHYSFS_Io* io, const PHYSFS_Archiver* funcs,
   const char* d, int forWriting, int* _claimed
) {
   if (io)
      BAIL_IF_ERRPASS(not io->seek(io, 0), nullptr);

   DirHandle* retval = nullptr;
   auto opaque = funcs->openArchive(io, d, forWriting, _claimed);
   if (opaque) {
      retval = PHYSFS_Allocator<DirHandle>(1).Ref();
      if (not retval)
         funcs->closeArchive(opaque);
      else {
         memset(retval, '\0', sizeof(DirHandle));
         retval->mountPoint = nullptr;
         retval->funcs = funcs;
         retval->opaque = opaque;
      }
   }

   return retval;
}

/// Open directory                                                            
static DirHandle* openDirectory(PHYSFS_Io* io, const char* d, int forWriting) {
   assert(io or d);
   DirHandle* retval = nullptr;
   int created_io = 0;
   int claimed = 0;

   if (not io) {
      // File doesn't exist, etc? Just fail out                         
      PHYSFS_Stat statbuf;
      BAIL_IF_ERRPASS(not __PHYSFS_platformStat(d, &statbuf, 1), nullptr);

      // DIR gets first shot (unlike the rest, it doesn't deal with     
      // files)                                                         
      if (statbuf.filetype == PHYSFS_FILETYPE_DIRECTORY) {
         retval = tryOpenDir(io, &__PHYSFS_Archiver_DIR, d, forWriting, &claimed);
         if (retval or claimed)
            return retval;
      }

      io = __PHYSFS_createNativeIo(d, forWriting ? 'w' : 'r');
      BAIL_IF_ERRPASS(not io, nullptr);
      created_io = 1;
   }

   auto ext = find_filename_extension(d);
   if (ext) {
      // Look for archivers with matching file extensions first...      
      for (auto i = archivers; *i and not retval and not claimed; i++) {
         if (PHYSFS_utf8stricmp(ext, (*i)->info.extension) == 0)
            retval = tryOpenDir(io, *i, d, forWriting, &claimed);
      }

      // Failing an exact file extension match, try all the others...   
      for (auto i = archivers; *i and not retval and not claimed; i++) {
         if (PHYSFS_utf8stricmp(ext, (*i)->info.extension) != 0)
            retval = tryOpenDir(io, *i, d, forWriting, &claimed);
      }
   }
   else {
      // No extension? Gotta try them all!                              
      for (auto i = archivers; *i and not retval and not claimed; i++)
         retval = tryOpenDir(io, *i, d, forWriting, &claimed);
   }

   if (not retval and created_io)
      io->destroy(io);

   BAIL_IF(not retval, PHYSFS_ERR_UNSUPPORTED, nullptr);
   return retval;
}

///                                                                           
/// Make a platform-independent path string sane. Doesn't actually check the  
/// file hierarchy, it just cleans up the string.                             
/// (dst) must be a buffer at least as big as (src), as this is where the     
/// cleaned up string is deposited.                                           
/// If there are illegal bits in the path (".." entries, etc) then we         
/// return zero and (dst) is undefined. Non-zero if the path was sanitized.   
///                                                                           
static int sanitizePlatformIndependentPath(const char* src, char* dst) {
   char* prev;
   char ch;

   // Skip initial '/' chars...                                         
   while (*src == '/')
      src++;

   // Make sure the entire string isn't "." or ".."                     
   if (strcmp(src, ".") == 0 or strcmp(src, "..") == 0)
      BAIL(PHYSFS_ERR_BAD_FILENAME, 0);

   prev = dst;
   do {
      ch = *(src++);

      if (ch == ':' or ch == '\\') {
         // Illegal chars in a physfs path                              
         BAIL(PHYSFS_ERR_BAD_FILENAME, 0);
      }

      if (ch == '/') {
         // Path separator                                              
         *dst = '\0';

         // "." and ".." are illegal pathnames                          
         if (strcmp(prev, ".") == 0 or strcmp(prev, "..") == 0)
            BAIL(PHYSFS_ERR_BAD_FILENAME, 0);

         // Chop out doubles...                                         
         while (*src == '/')
            src++;

         // Ends with a pathsep?                                        
         if (not *src)
            break;  // We're done, don't add final pathsep to dst       

         prev = dst + 1;
      }

      *(dst++) = ch;
   }
   while (ch);

   return 1;
}

static inline size_t dirHandleRootLen(const DirHandle* h) {
   return h ? h->rootlen : 0;
}

static inline int sanitizePlatformIndependentPathWithRoot(
   const DirHandle* h, const char* src, char* dst
) {
   return sanitizePlatformIndependentPath(src, dst + dirHandleRootLen(h));
}

///                                                                           
/// Figure out if (fname) is part of (h)'s mountpoint. (fname) must be an     
/// output from sanitizePlatformIndependentPath(), so that it is in a known   
/// state.                                                                    
///                                                                           
/// This only finds legitimate segments of a mountpoint. If the mountpoint is 
/// "/a/b/c" and (fname) is "/a/b/c", "/", or "/a/b/c/d", then the results are
/// all zero. "/a/b" will succeed, though.                                    
///                                                                           
static int partOfMountPoint(DirHandle* h, char* fname) {
   if (h->mountPoint == nullptr)
      return 0;
   else if (*fname == '\0')
      return 1;

   size_t len = strlen(fname);
   size_t mntpntlen = strlen(h->mountPoint);
   if (len > mntpntlen)  // Can't be a subset of mountpoint             
      return 0;

   // If true, must be not a match or a complete match, but not a subset
   if ((len + 1) == mntpntlen)
      return 0;

   // !!! FIXME: case insensitive? 
   int rc = strncmp(fname, h->mountPoint, len);
   if (rc != 0)
      return 0;         // Not a match                                  

   // Make sure /a/b matches /a/b/ and not /a/bc ...                    
   return h->mountPoint[len] == '/';
}

///                                                                           
static DirHandle* createDirHandle(
   PHYSFS_Io* io, const char* newDir,
   const char* mountPoint, int forWriting
) {
   DirHandle* dirHandle = nullptr;
   char* tmpmntpnt = nullptr;

   assert(newDir);  /* should have caught this higher up. */

   if (mountPoint) {
      const size_t len = strlen(mountPoint) + 1;
      tmpmntpnt = (char*) __PHYSFS_smallAlloc(len);
      GOTO_IF(!tmpmntpnt, PHYSFS_ERR_OUT_OF_MEMORY, badDirHandle);
      if (!sanitizePlatformIndependentPath(mountPoint, tmpmntpnt))
         goto badDirHandle;
      mountPoint = tmpmntpnt;  /* sanitized version. */
   }

   dirHandle = openDirectory(io, newDir, forWriting);
   GOTO_IF_ERRPASS(!dirHandle, badDirHandle);

   dirHandle->dirName = (char*) allocator.Malloc(strlen(newDir) + 1);
   GOTO_IF(!dirHandle->dirName, PHYSFS_ERR_OUT_OF_MEMORY, badDirHandle);
   strcpy(dirHandle->dirName, newDir);

   if ((mountPoint != nullptr) && (*mountPoint != '\0')) {
      dirHandle->mountPoint = (char*) allocator.Malloc(strlen(mountPoint) + 2);
      if (!dirHandle->mountPoint)
         GOTO(PHYSFS_ERR_OUT_OF_MEMORY, badDirHandle);
      strcpy(dirHandle->mountPoint, mountPoint);
      strcat(dirHandle->mountPoint, "/");
   }

   __PHYSFS_smallFree(tmpmntpnt);
   return dirHandle;

badDirHandle:
   if (dirHandle != nullptr) {
      dirHandle->funcs->closeArchive(dirHandle->opaque);
      PHYSFS_Allocator<>::Free(dirHandle->dirName);
      PHYSFS_Allocator<>::Free(dirHandle->mountPoint);
      PHYSFS_Allocator<>::Free(dirHandle);
   }

   __PHYSFS_smallFree(tmpmntpnt);
   return nullptr;
}

/// MAKE SURE you've got the stateLock held before calling this!              
static int freeDirHandle(DirHandle* dh, FileHandle* openList) {
   if (not dh)
      return 1;

   for (auto i = openList; i; i = i->next)
      BAIL_IF(i->dirHandle == dh, PHYSFS_ERR_FILES_STILL_OPEN, 0);

   dh->funcs->closeArchive(dh->opaque);

   if (dh->root)
      PHYSFS_Allocator<>::Free(dh->root);
   PHYSFS_Allocator<>::Free(dh->dirName);
   PHYSFS_Allocator<>::Free(dh->mountPoint);
   PHYSFS_Allocator<>::Free(dh);
   return 1;
}

///                                                                           
static char* calculateBaseDir(const char* argv0) {
   const char dirsep = __PHYSFS_platformDirSeparator;
   char* retval = nullptr;
   char* ptr = nullptr;

   // Give the platform layer first shot at this                        
   retval = __PHYSFS_platformCalcBaseDir(argv0);
   if (retval)
      return retval;

   // We need argv0 to go on                                            
   BAIL_IF(not argv0, PHYSFS_ERR_ARGV0_IS_NULL, nullptr);

   ptr = strrchr(argv0, dirsep);
   if (ptr) {
      const size_t size = ((size_t) (ptr - argv0)) + 1;
      retval = (char*) allocator.Malloc(size + 1);
      BAIL_IF(!retval, PHYSFS_ERR_OUT_OF_MEMORY, nullptr);
      memcpy(retval, argv0, size);
      retval[size] = '\0';
      return retval;
   }

   // argv0 wasn't helpful                                              
   BAIL(PHYSFS_ERR_INVALID_ARGUMENT, nullptr);
}

///                                                                           
static int initializeMutexes(void) {
   errorLock = __PHYSFS_platformCreateMutex();
   if (errorLock == nullptr)
      goto initializeMutexes_failed;

   stateLock = __PHYSFS_platformCreateMutex();
   if (stateLock == nullptr)
      goto initializeMutexes_failed;

   // Success                                                           
   return 1;

initializeMutexes_failed:
   if (errorLock != nullptr)
      __PHYSFS_platformDestroyMutex(errorLock);

   if (stateLock != nullptr)
      __PHYSFS_platformDestroyMutex(stateLock);

   // Fail                                                              
   errorLock = stateLock = nullptr;
   return 0;
}


static int doRegisterArchiver(const PHYSFS_Archiver*);

static int initStaticArchivers(void) {
   #define REGISTER_STATIC_ARCHIVER(arc) { \
      if (not doRegisterArchiver(&__PHYSFS_Archiver_##arc)) \
         return 0; \
    }

   #if PHYSFS_SUPPORTS_ZIP
      REGISTER_STATIC_ARCHIVER(ZIP);
   #endif
   #if PHYSFS_SUPPORTS_7Z
      SZIP_global_init();
      REGISTER_STATIC_ARCHIVER(7Z);
   #endif
   #if PHYSFS_SUPPORTS_GRP
      REGISTER_STATIC_ARCHIVER(GRP);
   #endif
   #if PHYSFS_SUPPORTS_QPAK
      REGISTER_STATIC_ARCHIVER(QPAK);
   #endif
   #if PHYSFS_SUPPORTS_HOG
      REGISTER_STATIC_ARCHIVER(HOG);
   #endif
   #if PHYSFS_SUPPORTS_MVL
      REGISTER_STATIC_ARCHIVER(MVL);
   #endif
   #if PHYSFS_SUPPORTS_WAD
      REGISTER_STATIC_ARCHIVER(WAD);
   #endif
   #if PHYSFS_SUPPORTS_CSM
      REGISTER_STATIC_ARCHIVER(CSM);
   #endif
   #if PHYSFS_SUPPORTS_SLB
      REGISTER_STATIC_ARCHIVER(SLB);
   #endif
   #if PHYSFS_SUPPORTS_ISO9660
      REGISTER_STATIC_ARCHIVER(ISO9660);
   #endif
   #if PHYSFS_SUPPORTS_VDF
   REGISTER_STATIC_ARCHIVER(VDF)
   #endif

   #undef REGISTER_STATIC_ARCHIVER
   return 1;
}


static int  doDeinit(void);

int PHYSFS_init(const char* argv0) {
   BAIL_IF(initialized, PHYSFS_ERR_IS_INITIALIZED, 0);
   if (not __PHYSFS_platformInit(argv0))
      return 0;

   // Everything below here can be cleaned up safely by doDeinit()      
   if (not initializeMutexes())
      goto initFailed;

   baseDir = calculateBaseDir(argv0);
   if (not baseDir)
      goto initFailed;

   userDir = __PHYSFS_platformCalcUserDir();
   if (not userDir)
      goto initFailed;

   // Platform layer is required to append a dirsep                     
   #ifndef __ANDROID__  // It's an APK file, not a directory on Android 
      assert(baseDir[strlen(baseDir) - 1] == __PHYSFS_platformDirSeparator);
   #endif

   assert(userDir[strlen(userDir) - 1] == __PHYSFS_platformDirSeparator);

   if (not initStaticArchivers())
      goto initFailed;

   initialized = 1;

   // This makes sure that the error subsystem is initialized           
   PHYSFS_setErrorCode(PHYSFS_getLastErrorCode());
   return 1;

initFailed:
   doDeinit();
   return 0;
}

/// MAKE SURE you hold stateLock before calling this!                         
static int closeFileHandleList(FileHandle** list) {
   FileHandle* next = nullptr;
   for (auto i = *list; i; i = next) {
      auto io = i->io;
      next = i->next;

      if (io->flush and not io->flush(io)) {
         *list = i;
         return 0;
      }

      io->destroy(io);
      PHYSFS_Allocator<>::Free(i);
   }

   *list = nullptr;
   return 1;
}

/// MAKE SURE you hold the stateLock before calling this!                     
static void freeSearchPath(void) {
   closeFileHandleList(&openReadList);

   if (searchPath) {
      DirHandle* next = nullptr;
      for (auto i = searchPath; i; i = next) {
         next = i->next;
         freeDirHandle(i, openReadList);
      }

      searchPath = nullptr;
   }
}

/// MAKE SURE you hold stateLock before calling this!                         
static int archiverInUse(const PHYSFS_Archiver* arc, const DirHandle* list) {
   for (auto i = list; i; i = i->next) {
      if (i->funcs == arc)
         return 1;
   }

   return 0;  // Not in use                                             
}

/// MAKE SURE you hold stateLock before calling this!                         
static int doDeregisterArchiver(const size_t idx) {
   const size_t len = (numArchivers - idx) * sizeof(void*);
   auto info = archiveInfo[idx];
   auto arc = archivers[idx];

   // Make sure nothing is still using this archiver                    
   if (archiverInUse(arc, searchPath) or archiverInUse(arc, writeDir))
      BAIL(PHYSFS_ERR_FILES_STILL_OPEN, 0);

   PHYSFS_Allocator<>::Free((void*) info->extension);
   PHYSFS_Allocator<>::Free((void*) info->description);
   PHYSFS_Allocator<>::Free((void*) info->author);
   PHYSFS_Allocator<>::Free((void*) info->url);
   PHYSFS_Allocator<>::Free((void*) arc);

   memmove(&archiveInfo[idx], &archiveInfo[idx + 1], len);
   memmove(&archivers[idx], &archivers[idx + 1], len);

   assert(numArchivers > 0);
   numArchivers--;

   return 1;
}

/// Does NOT hold the state lock; we're shutting down                         
static void freeArchivers(void) {
   while (numArchivers > 0) {
      if (not doDeregisterArchiver(numArchivers - 1))
         assert(not "nothing should be mounted during shutdown.");
   }

   PHYSFS_Allocator<>::Free(archivers);
   PHYSFS_Allocator<>::Free(archiveInfo);
   archivers = nullptr;
   archiveInfo = nullptr;
}

///                                                                           
static int doDeinit(void) {
   closeFileHandleList(&openWriteList);
   BAIL_IF(not PHYSFS_setWriteDir(nullptr), PHYSFS_ERR_FILES_STILL_OPEN, 0);

   freeSearchPath();
   freeArchivers();
   freeErrorStates();

   if (baseDir) {
      PHYSFS_Allocator<>::Free(baseDir);
      baseDir = nullptr;
   }

   if (userDir) {
      PHYSFS_Allocator<>::Free(userDir);
      userDir = nullptr;
   }

   if (prefDir) {
      PHYSFS_Allocator<>::Free(prefDir);
      prefDir = nullptr;
   }

   if (archiveInfo) {
      PHYSFS_Allocator<>::Free(archiveInfo);
      archiveInfo = nullptr;
   }

   if (archivers) {
      PHYSFS_Allocator<>::Free(archivers);
      archivers = nullptr;
   }

   longest_root = 0;
   allowSymLinks = 0;
   initialized = 0;

   if (errorLock)
      __PHYSFS_platformDestroyMutex(errorLock);

   if (stateLock)
      __PHYSFS_platformDestroyMutex(stateLock);

   errorLock = stateLock = nullptr;

   __PHYSFS_platformDeinit();
   return 1;
}

///                                                                           
int PHYSFS_deinit() {
   BAIL_IF(not initialized, PHYSFS_ERR_NOT_INITIALIZED, 0);
   return doDeinit();
}

///                                                                           
int PHYSFS_isInit() {
   return initialized;
}

///                                                                           
char* __PHYSFS_strdup(const char* str) {
   char* retval = PHYSFS_Allocator<char>(strlen(str) + 1).Ref();
   if (retval)
      strcpy(retval, str);
   return retval;
}

///                                                                           
PHYSFS_uint32 __PHYSFS_hashString(const char* str) {
   PHYSFS_uint32 hash = 5381;
   while (1) {
      const char ch = *(str++);
      if (not ch)
         break;
      hash = ((hash << 5) + hash) ^ ch;
   }

   return hash;
}

///                                                                           
PHYSFS_uint32 __PHYSFS_hashStringCaseFold(const char* str) {
   PHYSFS_uint32 hash = 5381;
   while (1) {
      const auto cp = __PHYSFS_utf8codepoint(&str);
      if (not cp)
         break;

      PHYSFS_uint32 folded[3];
      const int numbytes = PHYSFS_caseFold(cp, folded) * sizeof(PHYSFS_uint32);
      const char* bytes  = (const char*) folded;
      for (auto i = 0; i < numbytes; i++)
         hash = ((hash << 5) + hash) ^ *(bytes++);
   }

   return hash;
}

PHYSFS_uint32 __PHYSFS_hashStringCaseFoldUSAscii(const char* str) {
   PHYSFS_uint32 hash = 5381;
   while (1) {
      char ch = *(str++);
      if (not ch)
         break;

      if (ch >= 'A' and ch <= 'Z')
         ch -= ('A' - 'a');
      hash = ((hash << 5) + hash) ^ ch;
   }

   return hash;
}

/// MAKE SURE you hold stateLock before calling this!                         
static int doRegisterArchiver(const PHYSFS_Archiver* _archiver) {
   const PHYSFS_uint32 maxver = CURRENT_PHYSFS_ARCHIVER_API_VERSION;
   const size_t len = (numArchivers + 2) * sizeof(void*);
   void* ptr = nullptr;

   BAIL_IF(not _archiver, PHYSFS_ERR_INVALID_ARGUMENT, 0);
   BAIL_IF(    _archiver->version > maxver, PHYSFS_ERR_UNSUPPORTED, 0);
   BAIL_IF(not _archiver->info.extension, PHYSFS_ERR_INVALID_ARGUMENT, 0);
   BAIL_IF(not _archiver->info.description, PHYSFS_ERR_INVALID_ARGUMENT, 0);
   BAIL_IF(not _archiver->info.author, PHYSFS_ERR_INVALID_ARGUMENT, 0);
   BAIL_IF(not _archiver->info.url, PHYSFS_ERR_INVALID_ARGUMENT, 0);
   BAIL_IF(not _archiver->openArchive, PHYSFS_ERR_INVALID_ARGUMENT, 0);
   BAIL_IF(not _archiver->enumerate, PHYSFS_ERR_INVALID_ARGUMENT, 0);
   BAIL_IF(not _archiver->openRead, PHYSFS_ERR_INVALID_ARGUMENT, 0);
   BAIL_IF(not _archiver->openWrite, PHYSFS_ERR_INVALID_ARGUMENT, 0);
   BAIL_IF(not _archiver->openAppend, PHYSFS_ERR_INVALID_ARGUMENT, 0);
   BAIL_IF(not _archiver->remove, PHYSFS_ERR_INVALID_ARGUMENT, 0);
   BAIL_IF(not _archiver->mkdir, PHYSFS_ERR_INVALID_ARGUMENT, 0);
   BAIL_IF(not _archiver->closeArchive, PHYSFS_ERR_INVALID_ARGUMENT, 0);
   BAIL_IF(not _archiver->stat, PHYSFS_ERR_INVALID_ARGUMENT, 0);

   auto ext = _archiver->info.extension;
   for (auto i = 0; i < numArchivers; i++) {
      if (PHYSFS_utf8stricmp(archiveInfo[i]->extension, ext) == 0)
         BAIL(PHYSFS_ERR_DUPLICATE, 0);
   }

   // Make a copy of the data                                           
   auto archiver = PHYSFS_Allocator<PHYSFS_Archiver>(1, *_archiver);
   auto info = &archiver->info;
   memset(info, '\0', sizeof(*info));  // nullptr in case an alloc fails

   #define CPYSTR(item) \
           info->item = __PHYSFS_strdup(_archiver->info.item); \
           GOTO_IF(!info->item, PHYSFS_ERR_OUT_OF_MEMORY, regfailed);
      CPYSTR(extension);
      CPYSTR(description);
      CPYSTR(author);
      CPYSTR(url);
      info->supportsSymlinks = _archiver->info.supportsSymlinks;
   #undef CPYSTR

   ptr = allocator.Realloc(archiveInfo, len);
   GOTO_IF(!ptr, PHYSFS_ERR_OUT_OF_MEMORY, regfailed);
   archiveInfo = (PHYSFS_ArchiveInfo**) ptr;

   ptr = allocator.Realloc(archivers, len);
   GOTO_IF(!ptr, PHYSFS_ERR_OUT_OF_MEMORY, regfailed);
   archivers = (PHYSFS_Archiver**) ptr;

   archiveInfo[numArchivers] = info;
   archiveInfo[numArchivers + 1] = nullptr;

   archivers[numArchivers] = archiver.Ref();
   archivers[numArchivers + 1] = nullptr;

   numArchivers++;
   return 1;

regfailed:
   if (info) {
      PHYSFS_Allocator<>::Free((void*) info->extension);
      PHYSFS_Allocator<>::Free((void*) info->description);
      PHYSFS_Allocator<>::Free((void*) info->author);
      PHYSFS_Allocator<>::Free((void*) info->url);
   }

   allocator.Free(archiver);
   return 0;
}

int PHYSFS_registerArchiver(const PHYSFS_Archiver* archiver) {
   BAIL_IF(not initialized, PHYSFS_ERR_NOT_INITIALIZED, 0);

   __PHYSFS_platformGrabMutex(stateLock);
   auto retval = doRegisterArchiver(archiver);
   __PHYSFS_platformReleaseMutex(stateLock);
   return retval;
}

int PHYSFS_deregisterArchiver(const char* ext) {
   BAIL_IF(not initialized, PHYSFS_ERR_NOT_INITIALIZED, 0);
   BAIL_IF(not ext, PHYSFS_ERR_INVALID_ARGUMENT, 0);

   __PHYSFS_platformGrabMutex(stateLock);
   for (size_t i = 0; i < numArchivers; i++) {
      if (PHYSFS_utf8stricmp(archiveInfo[i]->extension, ext) == 0) {
         const int retval = doDeregisterArchiver(i);
         __PHYSFS_platformReleaseMutex(stateLock);
         return retval;
      }
   }
   __PHYSFS_platformReleaseMutex(stateLock);

   BAIL(PHYSFS_ERR_NOT_FOUND, 0);
}

const PHYSFS_ArchiveInfo** PHYSFS_supportedArchiveTypes(void) {
   BAIL_IF(not initialized, PHYSFS_ERR_NOT_INITIALIZED, nullptr);
   return (const PHYSFS_ArchiveInfo**) archiveInfo;
}

void PHYSFS_freeList(void* list) {
   if (list) {
      for (auto i = static_cast<void**>(list); *i; i++)
         PHYSFS_Allocator<>::Free(*i);
      PHYSFS_Allocator<>::Free(list);
   }
}

const char* PHYSFS_getDirSeparator() {
   static char retval[2] = {__PHYSFS_platformDirSeparator, '\0'};
   return retval;
}

char** PHYSFS_getCdRomDirs() {
   return doEnumStringList(__PHYSFS_platformDetectAvailableCDs);
}

void PHYSFS_getCdRomDirsCallback(PHYSFS_StringCallback callback, void* data) {
   __PHYSFS_platformDetectAvailableCDs(callback, data);
}

const char* PHYSFS_getPrefDir(const char* org, const char* app) {
   const char dirsep = __PHYSFS_platformDirSeparator;
   PHYSFS_Stat statbuf;
   char* ptr = nullptr;
   char* endstr = nullptr;

   BAIL_IF(!initialized, PHYSFS_ERR_NOT_INITIALIZED, 0);
   BAIL_IF(!org, PHYSFS_ERR_INVALID_ARGUMENT, nullptr);
   BAIL_IF(*org == '\0', PHYSFS_ERR_INVALID_ARGUMENT, nullptr);
   BAIL_IF(!app, PHYSFS_ERR_INVALID_ARGUMENT, nullptr);
   BAIL_IF(*app == '\0', PHYSFS_ERR_INVALID_ARGUMENT, nullptr);

   allocator.Free(prefDir);
   prefDir = __PHYSFS_platformCalcPrefDir(org, app);
   BAIL_IF_ERRPASS(!prefDir, nullptr);

   assert(strlen(prefDir) > 0);
   endstr = prefDir + (strlen(prefDir) - 1);
   assert(*endstr == dirsep);
   *endstr = '\0';  /* mask out the final dirsep for now. */

   if (!__PHYSFS_platformStat(prefDir, &statbuf, 1)) {
      for (ptr = strchr(prefDir, dirsep); ptr; ptr = strchr(ptr + 1, dirsep)) {
         *ptr = '\0';
         __PHYSFS_platformMkDir(prefDir);
         *ptr = dirsep;
      }

      if (!__PHYSFS_platformMkDir(prefDir)) {
         allocator.Free(prefDir);
         prefDir = nullptr;
      }
   }

   *endstr = dirsep;  /* readd the final dirsep. */
   return prefDir;
}

const char* PHYSFS_getBaseDir(void) {
   return baseDir;   /* this is calculated in PHYSFS_init()... */
}

const char* __PHYSFS_getUserDir(void)  /* not deprecated internal version. */
{
   return userDir;   /* this is calculated in PHYSFS_init()... */
}

const char* PHYSFS_getUserDir(void) {
   return __PHYSFS_getUserDir();
}

const char* PHYSFS_getWriteDir(void) {
   const char* retval = nullptr;
   __PHYSFS_platformGrabMutex(stateLock);
   if (writeDir != nullptr)
      retval = writeDir->dirName;
   __PHYSFS_platformReleaseMutex(stateLock);
   return retval;
}

int PHYSFS_setWriteDir(const char* newDir) {
   int retval = 1;

   __PHYSFS_platformGrabMutex(stateLock);

   if (writeDir != nullptr) {
      BAIL_IF_MUTEX_ERRPASS(!freeDirHandle(writeDir, openWriteList),
         stateLock, 0);
      writeDir = nullptr;
   }

   if (newDir != nullptr) {
      writeDir = createDirHandle(nullptr, newDir, nullptr, 1);
      retval = (writeDir != nullptr);
   }

   __PHYSFS_platformReleaseMutex(stateLock);

   return retval;
}

int PHYSFS_setRoot(const char* archive, const char* subdir) {
   DirHandle* i;

   BAIL_IF(!archive, PHYSFS_ERR_INVALID_ARGUMENT, 0);

   __PHYSFS_platformGrabMutex(stateLock);
   for (i = searchPath; i != nullptr; i = i->next) {
      if ((i->dirName != nullptr) && (strcmp(archive, i->dirName) == 0)) {
         if (!subdir || (strcmp(subdir, "/") == 0)) {
            if (i->root)
               allocator.Free(i->root);
            i->root = nullptr;
            i->rootlen = 0;
         }
         else {
            const size_t len = strlen(subdir) + 1;
            char* ptr = (char*) allocator.Malloc(len);
            BAIL_IF_MUTEX(!ptr, PHYSFS_ERR_OUT_OF_MEMORY, stateLock, 0);
            if (!sanitizePlatformIndependentPath(subdir, ptr)) {
               allocator.Free(ptr);
               BAIL_MUTEX_ERRPASS(stateLock, 0);
            }

            if (i->root)
               allocator.Free(i->root);
            i->root = ptr;
            i->rootlen = strlen(i->root);  /* in case sanitizePlatformIndependentPath changed subdir */

            if (longest_root < i->rootlen)
               longest_root = i->rootlen;
         }

         break;
      }
   }
   __PHYSFS_platformReleaseMutex(stateLock);
   return 1;
}

static int doMount(
   PHYSFS_Io* io, const char* fname,
   const char* mountPoint, int appendToPath
) {
   DirHandle* dh;
   DirHandle* prev = nullptr;
   DirHandle* i;

   BAIL_IF(!fname, PHYSFS_ERR_INVALID_ARGUMENT, 0);

   if (mountPoint == nullptr)
      mountPoint = "/";

   __PHYSFS_platformGrabMutex(stateLock);

   for (i = searchPath; i != nullptr; i = i->next) {
      // Already in search path?                                        
      if ((i->dirName != nullptr) && (strcmp(fname, i->dirName) == 0))
         BAIL_MUTEX_ERRPASS(stateLock, 1);
      prev = i;
   }

   dh = createDirHandle(io, fname, mountPoint, 0);
   BAIL_IF_MUTEX_ERRPASS(!dh, stateLock, 0);

   if (appendToPath) {
      if (prev == nullptr)
         searchPath = dh;
      else
         prev->next = dh;
   }
   else {
      dh->next = searchPath;
      searchPath = dh;
   }

   __PHYSFS_platformReleaseMutex(stateLock);
   return 1;
}

int PHYSFS_mountIo(
   PHYSFS_Io* io, const char* fname,
   const char* mountPoint, int appendToPath
) {
   BAIL_IF(!io, PHYSFS_ERR_INVALID_ARGUMENT, 0);
   BAIL_IF(!fname, PHYSFS_ERR_INVALID_ARGUMENT, 0);
   BAIL_IF(io->version != 0, PHYSFS_ERR_UNSUPPORTED, 0);
   return doMount(io, fname, mountPoint, appendToPath);
}

int PHYSFS_mountMemory(
   const void* buf, PHYSFS_uint64 len, void (*del)(void*),
   const char* fname, const char* mountPoint, int appendToPath
) {
   int retval = 0;
   PHYSFS_Io* io = nullptr;

   BAIL_IF(!buf, PHYSFS_ERR_INVALID_ARGUMENT, 0);
   BAIL_IF(!fname, PHYSFS_ERR_INVALID_ARGUMENT, 0);

   io = __PHYSFS_createMemoryIo(buf, len, del);
   BAIL_IF_ERRPASS(!io, 0);
   retval = doMount(io, fname, mountPoint, appendToPath);
   if (!retval) {
      // Docs say not to call (del) in case of failure, so cheat        
      MemoryIoInfo* info = (MemoryIoInfo*) io->opaque;
      info->destruct = nullptr;
      io->destroy(io);
   }

   return retval;
}

int PHYSFS_mountHandle(
   PHYSFS_File* file, const char* fname,
   const char* mountPoint, int appendToPath
) {
   int retval = 0;
   PHYSFS_Io* io = nullptr;

   BAIL_IF(!file, PHYSFS_ERR_INVALID_ARGUMENT, 0);
   BAIL_IF(!fname, PHYSFS_ERR_INVALID_ARGUMENT, 0);

   io = __PHYSFS_createHandleIo(file);
   BAIL_IF_ERRPASS(!io, 0);
   retval = doMount(io, fname, mountPoint, appendToPath);
   if (!retval) {
      // Docs say not to destruct in case of failure, so cheat          
      io->opaque = nullptr;
      io->destroy(io);
   }

   return retval;
}

int PHYSFS_mount(
   const char* newDir, const char* mountPoint, int appendToPath
) {
   BAIL_IF(!newDir, PHYSFS_ERR_INVALID_ARGUMENT, 0);
   return doMount(nullptr, newDir, mountPoint, appendToPath);
}

int PHYSFS_addToSearchPath(const char* newDir, int appendToPath) {
   return PHYSFS_mount(newDir, nullptr, appendToPath);
}

int PHYSFS_removeFromSearchPath(const char* oldDir) {
   return PHYSFS_unmount(oldDir);
}

int PHYSFS_unmount(const char* oldDir) {
   DirHandle* i;
   DirHandle* prev = nullptr;
   DirHandle* next = nullptr;

   BAIL_IF(oldDir == nullptr, PHYSFS_ERR_INVALID_ARGUMENT, 0);

   __PHYSFS_platformGrabMutex(stateLock);
   for (i = searchPath; i != nullptr; i = i->next) {
      if (strcmp(i->dirName, oldDir) == 0) {
         next = i->next;
         BAIL_IF_MUTEX_ERRPASS(!freeDirHandle(i, openReadList),
            stateLock, 0);

         if (prev == nullptr)
            searchPath = next;
         else
            prev->next = next;

         BAIL_MUTEX_ERRPASS(stateLock, 1);
      }
      prev = i;
   }

   BAIL_MUTEX(PHYSFS_ERR_NOT_MOUNTED, stateLock, 0);
}

char** PHYSFS_getSearchPath(void) {
   return doEnumStringList(PHYSFS_getSearchPathCallback);
}

const char* PHYSFS_getMountPoint(const char* dir) {
   DirHandle* i;
   __PHYSFS_platformGrabMutex(stateLock);
   for (i = searchPath; i != nullptr; i = i->next) {
      if (strcmp(i->dirName, dir) == 0) {
         const char* retval = ((i->mountPoint) ? i->mountPoint : "/");
         __PHYSFS_platformReleaseMutex(stateLock);
         return retval;
      }
   }
   __PHYSFS_platformReleaseMutex(stateLock);

   BAIL(PHYSFS_ERR_NOT_MOUNTED, nullptr);
}

void PHYSFS_getSearchPathCallback(PHYSFS_StringCallback callback, void* data) {
   DirHandle* i;

   __PHYSFS_platformGrabMutex(stateLock);

   for (i = searchPath; i != nullptr; i = i->next)
      callback(data, i->dirName);

   __PHYSFS_platformReleaseMutex(stateLock);
}

typedef struct setSaneCfgEnumData
{
   const char* archiveExt;
   size_t archiveExtLen;
   int archivesFirst;
   PHYSFS_ErrorCode errcode;
} setSaneCfgEnumData;

static PHYSFS_EnumerateCallbackResult setSaneCfgEnumCallback(void* _data,
   const char* dir, const char* f) {
   setSaneCfgEnumData* data = (setSaneCfgEnumData*) _data;
   const size_t extlen = data->archiveExtLen;
   const size_t l = strlen(f);
   const char* ext;

   if ((l > extlen) && (f[l - extlen - 1] == '.')) {
      ext = f + (l - extlen);
      if (PHYSFS_utf8stricmp(ext, data->archiveExt) == 0) {
         const char dirsep = __PHYSFS_platformDirSeparator;
         const char* d = PHYSFS_getRealDir(f);
         const size_t allocsize = strlen(d) + l + 2;
         char* str = (char*) __PHYSFS_smallAlloc(allocsize);
         if (str == nullptr)
            data->errcode = PHYSFS_ERR_OUT_OF_MEMORY;
         else {
            snprintf(str, allocsize, "%s%c%s", d, dirsep, f);
            if (!PHYSFS_mount(str, nullptr, data->archivesFirst == 0))
               data->errcode = currentErrorCode();
            __PHYSFS_smallFree(str);
         }
      }
   }

   /* !!! FIXME: if we want to abort on errors... */
   /*return (data->errcode != PHYSFS_ERR_OK) ? PHYSFS_ENUM_ERROR : PHYSFS_ENUM_OK;*/

   return PHYSFS_ENUM_OK;  /* keep going */
}

int PHYSFS_setSaneConfig(const char* organization, const char* appName,
   const char* archiveExt, int includeCdRoms,
   int archivesFirst) {
   const char* basedir;
   const char* prefdir;

   BAIL_IF(!initialized, PHYSFS_ERR_NOT_INITIALIZED, 0);

   prefdir = PHYSFS_getPrefDir(organization, appName);
   BAIL_IF_ERRPASS(!prefdir, 0);

   basedir = PHYSFS_getBaseDir();
   BAIL_IF_ERRPASS(!basedir, 0);

   BAIL_IF(!PHYSFS_setWriteDir(prefdir), PHYSFS_ERR_NO_WRITE_DIR, 0);

   /* !!! FIXME: these can fail and we should report that... */

   /* Put write dir first in search path... */
   PHYSFS_mount(prefdir, nullptr, 0);

   /* Put base path on search path... */
   PHYSFS_mount(basedir, nullptr, 1);

   /* handle CD-ROMs... */
   if (includeCdRoms) {
      char** cds = PHYSFS_getCdRomDirs();
      char** i;
      for (i = cds; *i != nullptr; i++)
         PHYSFS_mount(*i, nullptr, 1);
      PHYSFS_freeList(cds);
   }

   /* Root out archives, and add them to search path... */
   if (archiveExt != nullptr) {
      setSaneCfgEnumData data;
      memset(&data, '\0', sizeof(data));
      data.archiveExt = archiveExt;
      data.archiveExtLen = strlen(archiveExt);
      data.archivesFirst = archivesFirst;
      data.errcode = PHYSFS_ERR_OK;
      if (!PHYSFS_enumerate("/", setSaneCfgEnumCallback, &data)) {
         /* !!! FIXME: use this if we're reporting errors.
         PHYSFS_ErrorCode errcode = currentErrorCode();
         if (errcode == PHYSFS_ERR_APP_CALLBACK)
             errcode = data->errcode; */
      }
   }

   return 1;
}

void PHYSFS_permitSymbolicLinks(int allow) {
   allowSymLinks = allow;
}

int PHYSFS_symbolicLinksPermitted(void) {
   return allowSymLinks;
}

///                                                                           
/// Verify that (fname) (in platform-independent notation), in relation       
/// to (h) is secure. That means that each element of fname is checked        
/// for symlinks (if they aren't permitted). This also allows for quick       
/// rejection of files that exist outside an archive's mountpoint.            
///                                                                           
/// With some exceptions (like PHYSFS_mkdir(), which builds multiple subdirs  
/// at a time), you should always pass zero for "allowMissing" for efficiency.
///                                                                           
/// (fname) must point to an output from sanitizePlatformIndependentPath(),   
/// since it will make sure that path names are in the right format for       
/// passing certain checks. It will also do checks for "insecure" pathnames   
/// like ".." which should be done once instead of once per archive. This also
/// gives us license to treat (fname) as scratch space in this function.      
///                                                                           
/// (fname)'s buffer must have enough space available before it for this      
/// function to prepend any root directory for this DirHandle.                
///                                                                           
/// Returns non-zero if string is safe, zero if there's a security issue.     
/// PHYSFS_getLastError() will specify what was wrong. (*fname) will be       
/// updated to point past any mount point elements so it is prepared to       
/// be used with the archiver directly.                                       
///                                                                           
static int verifyPath(DirHandle* h, char** _fname, int allowMissing) {
   char* fname = *_fname;
   int retval = 1;
   char* start;
   char* end;

   if (*fname == '\0' and not h->root)  // Quick rejection              
      return 1;

   /* !!! FIXME: This codeblock sucks. */
   if (h->mountPoint != nullptr)  /* nullptr mountpoint means "/". */
   {
      size_t mntpntlen = strlen(h->mountPoint);
      size_t len = strlen(fname);
      assert(mntpntlen > 1); /* root mount points should be nullptr. */
      /* not under the mountpoint, so skip this archive. */
      BAIL_IF(len < mntpntlen - 1, PHYSFS_ERR_NOT_FOUND, 0);
      /* !!! FIXME: Case insensitive? */
      retval = strncmp(h->mountPoint, fname, mntpntlen - 1);
      BAIL_IF(retval != 0, PHYSFS_ERR_NOT_FOUND, 0);
      if (len > mntpntlen - 1)  /* corner case... */
         BAIL_IF(fname[mntpntlen - 1] != '/', PHYSFS_ERR_NOT_FOUND, 0);
      fname += mntpntlen - 1;  /* move to start of actual archive path. */
      if (*fname == '/')
         fname++;
      *_fname = fname;  /* skip mountpoint for later use. */
      retval = 1;  /* may be reset, below. */
   } /* if */

   // Prepend the root directory, if any                                
   if (h->root) {
      const int isempty = (*fname == '\0');
      fname -= h->rootlen + (isempty ? 0 : 1);
      strcpy(fname, h->root);
      if (!isempty)
         fname[h->rootlen] = '/';
      *_fname = fname;
   }

   start = fname;
   if (not allowSymLinks) {
      while (1) {
         end = strchr(start, '/');
         if (end)
            *end = '\0';

         PHYSFS_Stat statbuf;
         auto rc = h->funcs->stat(h->opaque, fname, &statbuf);
         if (rc)
            rc = (statbuf.filetype == PHYSFS_FILETYPE_SYMLINK);
         else if (currentErrorCode() == PHYSFS_ERR_NOT_FOUND)
            retval = 0;

         if (end)
            *end = '/';

         // Insecure path (has a disallowed symlink in it)?             
         BAIL_IF(rc, PHYSFS_ERR_SYMLINK_FORBIDDEN, 0);

         // Break out early if path element is missing                  
         if (not retval) {
            // We need to clear it if it's the last element of the path,
            // since this might be a non-existant file we're opening    
            // for writing...                                           
            if (not end or allowMissing)
               retval = 1;
            break;
         }

         if (not end)
            break;

         start = end + 1;
      }
   }

   return retval;
}


/// This must hold the stateLock before calling                               
static int doMkdir(const char* _dname, char* dname) {
   DirHandle* h = writeDir;
   char* start;
   char* end;
   int retval = 0;
   int exists = 1;  // Force existance check on first path element      

   assert(h != nullptr);

   BAIL_IF_ERRPASS(!sanitizePlatformIndependentPathWithRoot(h, _dname, dname), 0);
   BAIL_IF_ERRPASS(!verifyPath(h, &dname, 1), 0);

   start = dname;
   while (1) {
      end = strchr(start, '/');
      if (end)
         *end = '\0';

      // Only check for existance if all parent dirs existed, too...    
      if (exists) {
         PHYSFS_Stat statbuf;
         const int rc = h->funcs->stat(h->opaque, dname, &statbuf);
         if ((!rc) && (currentErrorCode() == PHYSFS_ERR_NOT_FOUND))
            exists = 0;

         /* verifyPath made sure that (dname) doesn't have symlinks if they aren't
            allowed, but it's possible the mounted writeDir itself has symlinks in it,
            (for example "/var" on iOS is a symlink, and the prefpath will be somewhere
            under that)...if we mounted that writeDir, we must allow those symlinks here
            unconditionally. */
         retval = ((rc) && ((statbuf.filetype == PHYSFS_FILETYPE_DIRECTORY) || (statbuf.filetype == PHYSFS_FILETYPE_SYMLINK)));
      }

      if (not exists)
         retval = h->funcs->mkdir(h->opaque, dname);

      if (not retval or not end)
         break;

      *end = '/';
      start = end + 1;
   }

   return retval;
}

int PHYSFS_mkdir(const char* _dname) {
   int retval = 0;
   char* dname;
   size_t len;

   BAIL_IF(!_dname, PHYSFS_ERR_INVALID_ARGUMENT, 0);

   __PHYSFS_platformGrabMutex(stateLock);
   BAIL_IF_MUTEX(!writeDir, PHYSFS_ERR_NO_WRITE_DIR, stateLock, 0);
   len = strlen(_dname) + dirHandleRootLen(writeDir) + 1;
   dname = (char*) __PHYSFS_smallAlloc(len);
   BAIL_IF_MUTEX(!dname, PHYSFS_ERR_OUT_OF_MEMORY, stateLock, 0);
   retval = doMkdir(_dname, dname);
   __PHYSFS_platformReleaseMutex(stateLock);
   __PHYSFS_smallFree(dname);
   return retval;
}

/// This must hold the stateLock before calling                               
static int doDelete(const char* _fname, char* fname) {
   DirHandle* h = writeDir;
   BAIL_IF_ERRPASS(!sanitizePlatformIndependentPathWithRoot(h, _fname, fname), 0);
   BAIL_IF_ERRPASS(!verifyPath(h, &fname, 0), 0);
   return h->funcs->remove(h->opaque, fname);
}

int PHYSFS_delete(const char* _fname) {
   int retval;
   char* fname;
   size_t len;

   __PHYSFS_platformGrabMutex(stateLock);
   BAIL_IF_MUTEX(!writeDir, PHYSFS_ERR_NO_WRITE_DIR, stateLock, 0);
   len = strlen(_fname) + dirHandleRootLen(writeDir) + 1;
   fname = (char*) __PHYSFS_smallAlloc(len);
   BAIL_IF_MUTEX(!fname, PHYSFS_ERR_OUT_OF_MEMORY, stateLock, 0);
   retval = doDelete(_fname, fname);
   __PHYSFS_platformReleaseMutex(stateLock);
   __PHYSFS_smallFree(fname);
   return retval;
}

static DirHandle* getRealDirHandle(const char* _fname) {
   DirHandle* retval = nullptr;
   char* allocated_fname = nullptr;
   char* fname = nullptr;
   size_t len;

   BAIL_IF(!_fname, PHYSFS_ERR_INVALID_ARGUMENT, nullptr);

   __PHYSFS_platformGrabMutex(stateLock);
   len = strlen(_fname) + longest_root + 2;
   allocated_fname = __PHYSFS_smallAlloc(len);
   BAIL_IF_MUTEX(!allocated_fname, PHYSFS_ERR_OUT_OF_MEMORY, stateLock, nullptr);
   fname = allocated_fname + longest_root + 1;
   if (sanitizePlatformIndependentPath(_fname, fname)) {
      DirHandle* i;
      for (i = searchPath; i != nullptr; i = i->next) {
         char* arcfname = fname;
         if (partOfMountPoint(i, arcfname)) {
            retval = i;
            break;
         } /* if */
         else if (verifyPath(i, &arcfname, 0)) {
            PHYSFS_Stat statbuf;
            if (i->funcs->stat(i->opaque, arcfname, &statbuf)) {
               retval = i;
               break;
            } /* if */
         } /* if */
      } /* for */
   } /* if */

   __PHYSFS_platformReleaseMutex(stateLock);
   __PHYSFS_smallFree(allocated_fname);
   return retval;
}

const char* PHYSFS_getRealDir(const char* fname) {
   DirHandle* dh = getRealDirHandle(fname);
   return dh ? dh->dirName : nullptr;
}

static int locateInStringList(const char* str,
   char** list,
   PHYSFS_uint32* pos) {
   PHYSFS_uint32 len = *pos;
   PHYSFS_uint32 half_len;
   PHYSFS_uint32 lo = 0;
   PHYSFS_uint32 middle;
   int cmp;

   while (len > 0) {
      half_len = len >> 1;
      middle = lo + half_len;
      cmp = strcmp(list[middle], str);

      if (cmp == 0)  // It's in the list already                        
         return 1;
      else if (cmp > 0)
         len = half_len;
      else {
         lo = middle + 1;
         len -= half_len + 1;
      }
   }

   *pos = lo;
   return 0;
}

static PHYSFS_EnumerateCallbackResult enumFilesCallback(
   void* data, const char* origdir, const char* str
) {
   PHYSFS_uint32 pos;
   void* ptr;
   char* newstr;
   EnumStringListCallbackData* pecd = (EnumStringListCallbackData*) data;

   // See if file is in the list already, and if not, insert it in there
   // alphabetically...                                                 
   pos = pecd->size;
   if (locateInStringList(str, pecd->list, &pos))
      return PHYSFS_ENUM_OK;  // Already in the list, but keep going    

   ptr = allocator.Realloc(pecd->list, (pecd->size + 2) * sizeof(char*));
   newstr = (char*) allocator.Malloc(strlen(str) + 1);
   if (ptr != nullptr)
      pecd->list = (char**) ptr;

   if ((ptr == nullptr) || (newstr == nullptr)) {
      if (newstr)
         allocator.Free(newstr);

      pecd->errcode = PHYSFS_ERR_OUT_OF_MEMORY;
      return PHYSFS_ENUM_ERROR;  /* better luck next time. */
   }

   strcpy(newstr, str);

   if (pos != pecd->size) {
      memmove(&pecd->list[pos + 1], &pecd->list[pos],
         sizeof(char*) * ((pecd->size) - pos));
   }

   pecd->list[pos] = newstr;
   pecd->size++;
   return PHYSFS_ENUM_OK;
}

char** PHYSFS_enumerateFiles(const char* path) {
   EnumStringListCallbackData ecd;
   memset(&ecd, '\0', sizeof(ecd));
   ecd.list = (char**) allocator.Malloc(sizeof(char*));
   BAIL_IF(!ecd.list, PHYSFS_ERR_OUT_OF_MEMORY, nullptr);
   if (!PHYSFS_enumerate(path, enumFilesCallback, &ecd)) {
      const PHYSFS_ErrorCode errcode = currentErrorCode();
      PHYSFS_uint32 i;
      for (i = 0; i < ecd.size; i++)
         allocator.Free(ecd.list[i]);
      allocator.Free(ecd.list);
      BAIL_IF(errcode == PHYSFS_ERR_APP_CALLBACK, ecd.errcode, nullptr);
      return nullptr;
   }

   ecd.list[ecd.size] = nullptr;
   return ecd.list;
}

/// Broke out to seperate function so we can use stack allocation gratuitously
static PHYSFS_EnumerateCallbackResult enumerateFromMountPoint(DirHandle* i,
   const char* arcfname,
   PHYSFS_EnumerateCallback callback,
   const char* _fname, void* data) {
   PHYSFS_EnumerateCallbackResult retval;
   const size_t len = strlen(arcfname);
   char* ptr = nullptr;
   char* end = nullptr;
   const size_t slen = strlen(i->mountPoint) + 1;
   char* mountPoint = (char*) __PHYSFS_smallAlloc(slen);

   BAIL_IF(!mountPoint, PHYSFS_ERR_OUT_OF_MEMORY, PHYSFS_ENUM_ERROR);

   strcpy(mountPoint, i->mountPoint);
   ptr = mountPoint + ((len) ? len + 1 : 0);
   end = strchr(ptr, '/');
   assert(end);  // Should always find a terminating '/'                
   *end = '\0';
   retval = callback(data, _fname, ptr);
   __PHYSFS_smallFree(mountPoint);

   BAIL_IF(retval == PHYSFS_ENUM_ERROR, PHYSFS_ERR_APP_CALLBACK, retval);
   return retval;
}

struct SymlinkFilterData {
   PHYSFS_EnumerateCallback callback;
   void* callbackData;
   DirHandle* dirhandle;
   const char* arcfname;
   PHYSFS_ErrorCode errcode;
};

static PHYSFS_EnumerateCallbackResult enumCallbackFilterSymLinks(
   void* _data, const char* origdir, const char* fname
) {
   SymlinkFilterData* data = (SymlinkFilterData*) _data;
   const DirHandle* dh = data->dirhandle;
   const char* arcfname = data->arcfname;
   PHYSFS_Stat statbuf;
   const char* trimmedDir = (*arcfname == '/') ? (arcfname + 1) : arcfname;
   const size_t slen = strlen(trimmedDir) + strlen(fname) + 2;
   char* path = (char*) __PHYSFS_smallAlloc(slen);
   PHYSFS_EnumerateCallbackResult retval = PHYSFS_ENUM_OK;

   if (path == nullptr) {
      data->errcode = PHYSFS_ERR_OUT_OF_MEMORY;
      return PHYSFS_ENUM_ERROR;
   }

   snprintf(path, slen, "%s%s%s", trimmedDir, *trimmedDir ? "/" : "", fname);

   if (!dh->funcs->stat(dh->opaque, path, &statbuf)) {
      data->errcode = currentErrorCode();
      retval = PHYSFS_ENUM_ERROR;
   }
   else {
      // Pass it on to the application if it's not a symlink            
      if (statbuf.filetype != PHYSFS_FILETYPE_SYMLINK) {
         retval = data->callback(data->callbackData, origdir, fname);
         if (retval == PHYSFS_ENUM_ERROR)
            data->errcode = PHYSFS_ERR_APP_CALLBACK;
      }
   }

   __PHYSFS_smallFree(path);
   return retval;
}

int PHYSFS_enumerate(const char* _fn, PHYSFS_EnumerateCallback cb, void* data) {
   PHYSFS_EnumerateCallbackResult retval = PHYSFS_ENUM_OK;
   size_t len;
   char* allocated_fname;
   char* fname;

   BAIL_IF(!_fn, PHYSFS_ERR_INVALID_ARGUMENT, 0);
   BAIL_IF(!cb, PHYSFS_ERR_INVALID_ARGUMENT, 0);

   __PHYSFS_platformGrabMutex(stateLock);

   len = strlen(_fn) + longest_root + 2;
   allocated_fname = (char*) __PHYSFS_smallAlloc(len);
   BAIL_IF_MUTEX(!allocated_fname, PHYSFS_ERR_OUT_OF_MEMORY, stateLock, 0);
   fname = allocated_fname + longest_root + 1;
   if (!sanitizePlatformIndependentPath(_fn, fname))
      retval = PHYSFS_ENUM_STOP;
   else {
      DirHandle* i;
      SymlinkFilterData filterdata;

      if (!allowSymLinks) {
         memset(&filterdata, '\0', sizeof(filterdata));
         filterdata.callback = cb;
         filterdata.callbackData = data;
      }

      for (i = searchPath; (retval == PHYSFS_ENUM_OK) && i; i = i->next) {
         char* arcfname = fname;

         if (partOfMountPoint(i, arcfname))
            retval = enumerateFromMountPoint(i, arcfname, cb, _fn, data);

         else if (verifyPath(i, &arcfname, 0)) {
            PHYSFS_Stat statbuf;
            if (!i->funcs->stat(i->opaque, arcfname, &statbuf)) {
               if (currentErrorCode() == PHYSFS_ERR_NOT_FOUND)
                  continue;  /* no such dir in this archive, skip it. */
            }

            if (statbuf.filetype != PHYSFS_FILETYPE_DIRECTORY)
               continue;  /* not a directory in this archive, skip it. */

            else if ((!allowSymLinks) && (i->funcs->info.supportsSymlinks)) {
               filterdata.dirhandle = i;
               filterdata.arcfname = arcfname;
               filterdata.errcode = PHYSFS_ERR_OK;
               retval = i->funcs->enumerate(i->opaque, arcfname,
                  enumCallbackFilterSymLinks,
                  _fn, &filterdata);
               if (retval == PHYSFS_ENUM_ERROR) {
                  if (currentErrorCode() == PHYSFS_ERR_APP_CALLBACK)
                     PHYSFS_setErrorCode(filterdata.errcode);
               }
            }
            else {
               retval = i->funcs->enumerate(i->opaque, arcfname,
                  cb, _fn, data);
            }
         }
      }
   }

   __PHYSFS_platformReleaseMutex(stateLock);
   __PHYSFS_smallFree(allocated_fname);
   return (retval == PHYSFS_ENUM_ERROR) ? 0 : 1;
}

struct LegacyEnumFilesCallbackData {
   PHYSFS_EnumFilesCallback callback;
   void* data;
};

static PHYSFS_EnumerateCallbackResult enumFilesCallbackAlwaysSucceed(
   void* d, const char* origdir, const char* fname
) {
   LegacyEnumFilesCallbackData* cbdata = (LegacyEnumFilesCallbackData*) d;
   cbdata->callback(cbdata->data, origdir, fname);
   return PHYSFS_ENUM_OK;
}

void PHYSFS_enumerateFilesCallback(
   const char* fname, PHYSFS_EnumFilesCallback callback, void* data
) {
   LegacyEnumFilesCallbackData cbdata;
   cbdata.callback = callback;
   cbdata.data = data;
   (void) PHYSFS_enumerate(fname, enumFilesCallbackAlwaysSucceed, &cbdata);
}

int PHYSFS_exists(const char* fname) {
   return (getRealDirHandle(fname) != nullptr);
}

PHYSFS_sint64 PHYSFS_getLastModTime(const char* fname) {
   PHYSFS_Stat statbuf;
   BAIL_IF_ERRPASS(!PHYSFS_stat(fname, &statbuf), -1);
   return statbuf.modtime;
}

int PHYSFS_isDirectory(const char* fname) {
   PHYSFS_Stat statbuf;
   BAIL_IF_ERRPASS(!PHYSFS_stat(fname, &statbuf), 0);
   return (statbuf.filetype == PHYSFS_FILETYPE_DIRECTORY);
}

int PHYSFS_isSymbolicLink(const char* fname) {
   PHYSFS_Stat statbuf;
   BAIL_IF_ERRPASS(!PHYSFS_stat(fname, &statbuf), 0);
   return (statbuf.filetype == PHYSFS_FILETYPE_SYMLINK);
}

static PHYSFS_File* doOpenWrite(const char* _fname, const int appending) {
   FileHandle* fh = nullptr;
   DirHandle* h;
   size_t len;
   char* fname;

   BAIL_IF(!_fname, PHYSFS_ERR_INVALID_ARGUMENT, 0);

   __PHYSFS_platformGrabMutex(stateLock);
   h = writeDir;
   BAIL_IF_MUTEX(!h, PHYSFS_ERR_NO_WRITE_DIR, stateLock, 0);

   len = strlen(_fname) + dirHandleRootLen(h) + 1;
   fname = (char*) __PHYSFS_smallAlloc(len);
   BAIL_IF_MUTEX(!fname, PHYSFS_ERR_OUT_OF_MEMORY, stateLock, 0);

   if (sanitizePlatformIndependentPathWithRoot(h, _fname, fname)) {
      PHYSFS_Io* io = nullptr;
      char* arcfname = fname;
      if (verifyPath(h, &arcfname, 0)) {
         const PHYSFS_Archiver* f = h->funcs;
         if (appending)
            io = f->openAppend(h->opaque, arcfname);
         else
            io = f->openWrite(h->opaque, arcfname);

         if (io) {
            fh = (FileHandle*) allocator.Malloc(sizeof(FileHandle));
            if (fh == nullptr) {
               io->destroy(io);
               PHYSFS_setErrorCode(PHYSFS_ERR_OUT_OF_MEMORY);
            }
            else {
               memset(fh, '\0', sizeof(FileHandle));
               fh->io = io;
               fh->dirHandle = h;
               fh->next = openWriteList;
               openWriteList = fh;
            }
         }
      }
   }
   __PHYSFS_platformReleaseMutex(stateLock);

   __PHYSFS_smallFree(fname);
   return ((PHYSFS_File*) fh);
}

PHYSFS_File* PHYSFS_openWrite(const char* filename) {
   return doOpenWrite(filename, 0);
}

PHYSFS_File* PHYSFS_openAppend(const char* filename) {
   return doOpenWrite(filename, 1);
}

PHYSFS_File* PHYSFS_openRead(const char* _fname) {
   FileHandle* fh = nullptr;
   char* allocated_fname;
   char* fname;
   size_t len;

   BAIL_IF(!_fname, PHYSFS_ERR_INVALID_ARGUMENT, 0);

   __PHYSFS_platformGrabMutex(stateLock);

   BAIL_IF_MUTEX(!searchPath, PHYSFS_ERR_NOT_FOUND, stateLock, 0);

   len = strlen(_fname) + longest_root + 2;
   allocated_fname = (char*) __PHYSFS_smallAlloc(len);
   BAIL_IF_MUTEX(!allocated_fname, PHYSFS_ERR_OUT_OF_MEMORY, stateLock, 0);
   fname = allocated_fname + longest_root + 1;

   if (sanitizePlatformIndependentPath(_fname, fname)) {
      PHYSFS_Io* io = nullptr;
      DirHandle* i;

      for (i = searchPath; i != nullptr; i = i->next) {
         char* arcfname = fname;
         if (verifyPath(i, &arcfname, 0)) {
            io = i->funcs->openRead(i->opaque, arcfname);
            if (io)
               break;
         }
      }

      if (io) {
         fh = (FileHandle*) allocator.Malloc(sizeof(FileHandle));
         if (fh == nullptr) {
            io->destroy(io);
            PHYSFS_setErrorCode(PHYSFS_ERR_OUT_OF_MEMORY);
         }
         else {
            memset(fh, '\0', sizeof(FileHandle));
            fh->io = io;
            fh->forReading = 1;
            fh->dirHandle = i;
            fh->next = openReadList;
            openReadList = fh;
         }
      }
   }

   __PHYSFS_platformReleaseMutex(stateLock);
   __PHYSFS_smallFree(allocated_fname);
   return ((PHYSFS_File*) fh);
}

static int closeHandleInOpenList(FileHandle** list, FileHandle* handle) {
   FileHandle* prev = nullptr;
   FileHandle* i;

   for (i = *list; i != nullptr; i = i->next) {
      if (i == handle)  /* handle is in this list? */
      {
         PHYSFS_Io* io = handle->io;
         PHYSFS_uint8* tmp = handle->buffer;

         /* send our buffer to io... */
         if (!handle->forReading) {
            if (!PHYSFS_flush((PHYSFS_File*) handle))
               return -1;

            /* ...then have io send it to the disk... */
            else if (io->flush && !io->flush(io))
               return -1;
         } /* if */

         /* ...then close the underlying file. */
         io->destroy(io);

         if (tmp != nullptr)  /* free any associated buffer. */
            allocator.Free(tmp);

         if (prev == nullptr)
            *list = handle->next;
         else
            prev->next = handle->next;

         allocator.Free(handle);
         return 1;
      }

      prev = i;
   }

   return 0;
}

int PHYSFS_close(PHYSFS_File* _handle) {
   FileHandle* handle = (FileHandle*) _handle;
   int rc;

   __PHYSFS_platformGrabMutex(stateLock);

   /* -1 == close failure. 0 == not found. 1 == success. */
   rc = closeHandleInOpenList(&openReadList, handle);
   BAIL_IF_MUTEX_ERRPASS(rc == -1, stateLock, 0);
   if (!rc) {
      rc = closeHandleInOpenList(&openWriteList, handle);
      BAIL_IF_MUTEX_ERRPASS(rc == -1, stateLock, 0);
   }

   __PHYSFS_platformReleaseMutex(stateLock);
   BAIL_IF(!rc, PHYSFS_ERR_INVALID_ARGUMENT, 0);
   return 1;
}

static PHYSFS_sint64 doBufferedRead(FileHandle* fh, void* _buffer, size_t len) {
   PHYSFS_uint8* buffer = (PHYSFS_uint8*) _buffer;
   PHYSFS_sint64 retval = 0;

   while (len > 0) {
      const size_t avail = fh->buffill - fh->bufpos;
      if (avail > 0) {
         /* data available in the buffer. */
         const size_t cpy = (len < avail) ? len : avail;
         memcpy(buffer, fh->buffer + fh->bufpos, cpy);
         assert(len >= cpy);
         buffer += cpy;
         len -= cpy;
         fh->bufpos += cpy;
         retval += cpy;
      }
      else {
         /* buffer is empty, refill it. */
         PHYSFS_Io* io = fh->io;
         const PHYSFS_sint64 rc = io->read(io, fh->buffer, fh->bufsize);
         fh->bufpos = 0;
         if (rc > 0)
            fh->buffill = (size_t) rc;
         else {
            fh->buffill = 0;
            if (retval == 0)  /* report already-read data, or failure. */
               retval = rc;
            break;
         }
      }
   }

   return retval;
}

PHYSFS_sint64 PHYSFS_read(
   PHYSFS_File* handle, void* buffer,
   PHYSFS_uint32 size, PHYSFS_uint32 count
) {
   const PHYSFS_uint64 len = ((PHYSFS_uint64) size) * ((PHYSFS_uint64) count);
   const PHYSFS_sint64 retval = PHYSFS_readBytes(handle, buffer, len);
   return ((retval <= 0) ? retval : (retval / ((PHYSFS_sint64) size)));
}

PHYSFS_sint64 PHYSFS_readBytes(
   PHYSFS_File* handle, void* buffer, PHYSFS_uint64 _len
) {
   const size_t len = (size_t) _len;
   FileHandle* fh = (FileHandle*) handle;
   const PHYSFS_uint64 maxlen = __PHYSFS_UI64(0x7FFFFFFFFFFFFFFF);
   if (!__PHYSFS_ui64FitsAddressSpace(_len))
      BAIL(PHYSFS_ERR_INVALID_ARGUMENT, -1);

   BAIL_IF(_len > maxlen, PHYSFS_ERR_INVALID_ARGUMENT, -1);
   BAIL_IF(!fh->forReading, PHYSFS_ERR_OPEN_FOR_WRITING, -1);
   BAIL_IF_ERRPASS(len == 0, 0);
   if (fh->buffer)
      return doBufferedRead(fh, buffer, len);

   return fh->io->read(fh->io, buffer, len);
}

static PHYSFS_sint64 doBufferedWrite(
   PHYSFS_File* handle, const void* buffer, const size_t len
) {
   FileHandle* fh = (FileHandle*) handle;

   // Whole thing fits in the buffer?                                   
   if ((fh->buffill + len) < fh->bufsize) {
      memcpy(fh->buffer + fh->buffill, buffer, len);
      fh->buffill += len;
      return (PHYSFS_sint64) len;
   }

   // Would overflow buffer. Flush and then write the new objects, too  
   BAIL_IF_ERRPASS(!PHYSFS_flush(handle), -1);
   return fh->io->write(fh->io, buffer, len);
}

PHYSFS_sint64 PHYSFS_write(PHYSFS_File* handle, const void* buffer,
   PHYSFS_uint32 size, PHYSFS_uint32 count) {
   const PHYSFS_uint64 len = ((PHYSFS_uint64) size) * ((PHYSFS_uint64) count);
   const PHYSFS_sint64 retval = PHYSFS_writeBytes(handle, buffer, len);
   return ((retval <= 0) ? retval : (retval / ((PHYSFS_sint64) size)));
}

PHYSFS_sint64 PHYSFS_writeBytes(PHYSFS_File* handle, const void* buffer,
   PHYSFS_uint64 _len) {
   const size_t len = (size_t) _len;
   FileHandle* fh = (FileHandle*) handle;
   const PHYSFS_uint64 maxlen = __PHYSFS_UI64(0x7FFFFFFFFFFFFFFF);
   if (!__PHYSFS_ui64FitsAddressSpace(_len))
      BAIL(PHYSFS_ERR_INVALID_ARGUMENT, -1);

   BAIL_IF(_len > maxlen, PHYSFS_ERR_INVALID_ARGUMENT, -1);
   BAIL_IF(fh->forReading, PHYSFS_ERR_OPEN_FOR_READING, -1);
   BAIL_IF_ERRPASS(len == 0, 0);
   if (fh->buffer)
      return doBufferedWrite(handle, buffer, len);

   return fh->io->write(fh->io, buffer, len);
}

int PHYSFS_eof(PHYSFS_File* handle) {
   FileHandle* fh = (FileHandle*) handle;

   if (!fh->forReading) {
      // Never EOF on files opened for write/append                     
      return 0;
   }

   // Can't be eof if buffer isn't empty                                
   if (fh->bufpos == fh->buffill) {
      // Check the Io                                                   
      PHYSFS_Io* io = fh->io;
      const PHYSFS_sint64 pos = io->tell(io);
      const PHYSFS_sint64 len = io->length(io);
      if ((pos < 0) || (len < 0))
         return 0;  /* beats me. */

      return (pos >= len);
   }

   return 0;
}

PHYSFS_sint64 PHYSFS_tell(PHYSFS_File* handle) {
   FileHandle* fh = (FileHandle*) handle;
   const PHYSFS_sint64 pos = fh->io->tell(fh->io);
   const PHYSFS_sint64 retval = fh->forReading ?
      (pos - fh->buffill) + fh->bufpos :
      (pos + fh->buffill);
   return retval;
}

int PHYSFS_seek(PHYSFS_File* handle, PHYSFS_uint64 pos) {
   FileHandle* fh = (FileHandle*) handle;
   BAIL_IF_ERRPASS(!PHYSFS_flush(handle), 0);

   if (fh->buffer && fh->forReading) {
      /* avoid throwing away our precious buffer if seeking within it. */
      PHYSFS_sint64 offset = pos - PHYSFS_tell(handle);
      if ( /* seeking within the already-buffered range? */
         /* forward? */
         ((offset >= 0) && (((size_t) offset) <= fh->buffill - fh->bufpos)) ||
         /* backward? */
         ((offset < 0) && (((size_t) -offset) <= fh->bufpos))) {
         fh->bufpos = (size_t) (((PHYSFS_sint64) fh->bufpos) + offset);
         return 1; /* successful seek */
      }
   }

   // We have to fall back to a 'raw' seek                              
   fh->buffill = fh->bufpos = 0;
   return fh->io->seek(fh->io, pos);
}

PHYSFS_sint64 PHYSFS_fileLength(PHYSFS_File* handle) {
   PHYSFS_Io* io = ((FileHandle*) handle)->io;
   return io->length(io);
}

int PHYSFS_setBuffer(PHYSFS_File* handle, PHYSFS_uint64 _bufsize) {
   FileHandle* fh = (FileHandle*) handle;
   const size_t bufsize = (size_t) _bufsize;

   if (!__PHYSFS_ui64FitsAddressSpace(_bufsize))
      BAIL(PHYSFS_ERR_INVALID_ARGUMENT, 0);

   BAIL_IF_ERRPASS(!PHYSFS_flush(handle), 0);

   // For reads, we need to move the file pointer to where it would be  
   // if we weren't buffering, so that the next read will get the       
   // right chunk of stuff from the file. PHYSFS_flush() handles writes 
   if ((fh->forReading) && (fh->buffill != fh->bufpos)) {
      PHYSFS_uint64 pos;
      const PHYSFS_sint64 curpos = fh->io->tell(fh->io);
      BAIL_IF_ERRPASS(curpos == -1, 0);
      pos = ((curpos - fh->buffill) + fh->bufpos);
      BAIL_IF_ERRPASS(!fh->io->seek(fh->io, pos), 0);
   }

   if (bufsize == 0) {
      // Delete existing buffer                                         
      if (fh->buffer) {
         allocator.Free(fh->buffer);
         fh->buffer = nullptr;
      }
   }
   else {
      PHYSFS_uint8* newbuf;
      newbuf = (PHYSFS_uint8*) allocator.Realloc(fh->buffer, bufsize);
      BAIL_IF(!newbuf, PHYSFS_ERR_OUT_OF_MEMORY, 0);
      fh->buffer = newbuf;
   }

   fh->bufsize = bufsize;
   fh->buffill = fh->bufpos = 0;
   return 1;
}

int PHYSFS_flush(PHYSFS_File* handle) {
   FileHandle* fh = (FileHandle*) handle;
   PHYSFS_Io* io;
   PHYSFS_sint64 rc;

   if (fh->forReading or fh->bufpos == fh->buffill)
      return 1;  // Open for read or buffer empty are successful no-ops 

   // Dump buffer to disk                                               
   io = fh->io;
   rc = io->write(io, fh->buffer + fh->bufpos, fh->buffill - fh->bufpos);
   BAIL_IF_ERRPASS(rc <= 0, 0);
   fh->bufpos = fh->buffill = 0;
   return 1;
}

int PHYSFS_stat(const char* _fname, PHYSFS_Stat* stat) {
   int retval = 0;
   char* allocated_fname;
   char* fname;
   size_t len;

   BAIL_IF(!_fname, PHYSFS_ERR_INVALID_ARGUMENT, 0);
   BAIL_IF(!stat, PHYSFS_ERR_INVALID_ARGUMENT, 0);

   // Set some sane defaults...                                         
   stat->filesize = -1;
   stat->modtime = -1;
   stat->createtime = -1;
   stat->accesstime = -1;
   stat->filetype = PHYSFS_FILETYPE_OTHER;
   stat->readonly = 1;

   __PHYSFS_platformGrabMutex(stateLock);
   len = strlen(_fname) + longest_root + 2;
   allocated_fname = (char*) __PHYSFS_smallAlloc(len);
   BAIL_IF_MUTEX(!allocated_fname, PHYSFS_ERR_OUT_OF_MEMORY, stateLock, 0);
   fname = allocated_fname + longest_root + 1;

   if (sanitizePlatformIndependentPath(_fname, fname)) {
      if (*fname == '\0') {
         stat->filetype = PHYSFS_FILETYPE_DIRECTORY;
         stat->readonly = !writeDir; /* Writeable if we have a writeDir */
         retval = 1;
      }
      else {
         DirHandle* i;
         int exists = 0;
         for (i = searchPath; ((i != nullptr) && (!exists)); i = i->next) {
            char* arcfname = fname;
            exists = partOfMountPoint(i, arcfname);
            if (exists) {
               stat->filetype = PHYSFS_FILETYPE_DIRECTORY;
               stat->readonly = 1;
               retval = 1;
            }
            else if (verifyPath(i, &arcfname, 0)) {
               retval = i->funcs->stat(i->opaque, arcfname, stat);
               if ((retval) || (currentErrorCode() != PHYSFS_ERR_NOT_FOUND))
                  exists = 1;
            }
         }
      }
   }

   __PHYSFS_platformReleaseMutex(stateLock);
   __PHYSFS_smallFree(allocated_fname);
   return retval;
}

int __PHYSFS_readAll(PHYSFS_Io* io, void* buf, const size_t _len) {
   const PHYSFS_uint64 len = (PHYSFS_uint64) _len;
   return (io->read(io, buf, len) == len);
}

void* __PHYSFS_initSmallAlloc(void* ptr, const size_t len) {
   void* useHeap = ((ptr == nullptr) ? ((void*) 1) : ((void*) 0));
   if (useHeap) {
      // Too large for stack allocation or alloca() failed              
      ptr = allocator.Malloc(len + sizeof(void*));
   }

   if (ptr != nullptr) {
      void** retval = (void**) ptr;
      *retval = useHeap;
      return retval + 1;
   }

   // Allocation failed                                                 
   return nullptr;
}

void __PHYSFS_smallFree(void* ptr) {
   if (ptr != nullptr) {
      void** block = ((void**) ptr) - 1;
      const int useHeap = (*block != nullptr);
      if (useHeap)
         allocator.Free(block);
   }
}

#ifndef PHYSFS_NO_CRUNTIME_MALLOC
   static void* mallocAllocatorMalloc(PHYSFS_uint64 s) {
      if (!__PHYSFS_ui64FitsAddressSpace(s))
         BAIL(PHYSFS_ERR_OUT_OF_MEMORY, nullptr);
      #undef malloc
      return malloc((size_t) s);
   }

   static void* mallocAllocatorRealloc(void* ptr, PHYSFS_uint64 s) {
      if (!__PHYSFS_ui64FitsAddressSpace(s))
         BAIL(PHYSFS_ERR_OUT_OF_MEMORY, nullptr);
      #undef realloc
      return realloc(ptr, (size_t) s);
   }

   static void mallocAllocatorFree(void* ptr) {
      #undef free
      free(ptr);
   }
#endif

static void setDefaultAllocator() {
   assert(!externalAllocator);
   allocator.Init = nullptr;
   allocator.Deinit = nullptr;
   #ifndef PHYSFS_NO_CRUNTIME_MALLOC
      allocator.Malloc = mallocAllocatorMalloc;
      allocator.Realloc = mallocAllocatorRealloc;
      allocator.Free = mallocAllocatorFree;
   #endif
}
