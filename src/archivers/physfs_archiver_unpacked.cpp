///                                                                           
/// High-level PhysicsFS archiver for simple unpacked file formats.           
///                                                                           
/// This is a framework that basic archivers build on top of. It's for simple 
/// formats that can just hand back a list of files and the offsets of their  
/// uncompressed data. There are an alarming number of formats like this.     
///                                                                           
/// RULES: Archive entries must be uncompressed. Dirs and files allowed, but  
/// no symlinks, etc. We can relax some of these rules as necessary.          
///                                                                           
/// Please see the file LICENSE.txt in the source's root directory.           
///                                                                           
/// This file written by Ryan C. Gordon.                                      
///                                                                           
#include "../physfs_internal.hpp"
#include "../physfs_tree.hpp"


struct UNPKinfo {
   __PHYSFS_DirTree tree;
   PHYSFS_Io* io;
};

struct UNPKentry {
   __PHYSFS_DirTreeEntry tree;
   PHYSFS_uint64 startPos;
   PHYSFS_uint64 size;
   PHYSFS_sint64 ctime;
   PHYSFS_sint64 mtime;
};

struct UNPKfileinfo {
   PHYSFS_Io* io;
   UNPKentry* entry;
   PHYSFS_uint32 curPos;
};

namespace
{
   PHYSFS_sint64 UNPK_read(PHYSFS_Io* io, void* buffer, PHYSFS_uint64 len) {
      auto finfo = static_cast<UNPKfileinfo*>(io->opaque);
      const auto* entry = finfo->entry;
      const auto bytesLeft = (PHYSFS_uint64)(entry->size - finfo->curPos);
      if (bytesLeft < len)
         len = bytesLeft;

      auto rc = finfo->io->read(finfo->io, buffer, len);
      if (rc > 0)
         finfo->curPos += static_cast<PHYSFS_uint32>(rc);
      return rc;
   }

   PHYSFS_sint64 UNPK_write(PHYSFS_Io*, const void*, PHYSFS_uint64) {
      BAIL(PHYSFS_ERR_READ_ONLY, -1);
   }

   PHYSFS_sint64 UNPK_tell(PHYSFS_Io* io) {
      return ((UNPKfileinfo*)io->opaque)->curPos;
   }

   int UNPK_seek(PHYSFS_Io* io, PHYSFS_uint64 offset) {
      auto finfo = static_cast<UNPKfileinfo*>(io->opaque);
      const auto* entry = finfo->entry;

      BAIL_IF(offset >= entry->size, PHYSFS_ERR_PAST_EOF, 0);
      auto rc = finfo->io->seek(finfo->io, entry->startPos + offset);
      if (rc)
         finfo->curPos = (PHYSFS_uint32)offset;
      return rc;
   }

   PHYSFS_sint64 UNPK_length(PHYSFS_Io* io) {
      const auto* finfo = static_cast<UNPKfileinfo*>(io->opaque);
      return static_cast<PHYSFS_sint64>(finfo->entry->size);
   }

   PHYSFS_Io* UNPK_duplicate(PHYSFS_Io* _io) {
      auto origfinfo = static_cast<UNPKfileinfo*>(_io->opaque);
      auto finfo = PHYSFS_Allocator<UNPKfileinfo>(1);
      finfo->io = origfinfo->io->duplicate(origfinfo->io);
      finfo->entry = origfinfo->entry;
      finfo->curPos = 0;

      auto retval = PHYSFS_Allocator<PHYSFS_Io>(1, *_io);
      retval->opaque = finfo.Ref();
      return retval.Ref();
   }

   int UNPK_flush(PHYSFS_Io*) {
      return 1;  // No write support                                    
   }

   void UNPK_destroy(PHYSFS_Io* io) {
      auto finfo = static_cast<UNPKfileinfo*>(io->opaque);
      finfo->io->destroy(finfo->io);
      PHYSFS_Allocator<>::Free(finfo);
      PHYSFS_Allocator<>::Free(io);
   }

   const PHYSFS_Io UNPK_Io =
   {
      CURRENT_PHYSFS_IO_API_VERSION, nullptr,
      UNPK_read,
      UNPK_write,
      UNPK_seek,
      UNPK_tell,
      UNPK_length,
      UNPK_duplicate,
      UNPK_flush,
      UNPK_destroy
   };

   UNPKentry* findEntry(UNPKinfo* info, const char* path) {
      return (UNPKentry*)__PHYSFS_DirTreeFind(&info->tree, path);
   }
}

void UNPK_closeArchive(void* opaque) {
   auto info = static_cast<UNPKinfo*>(opaque);
   if (info) {
      __PHYSFS_DirTreeDeinit(&info->tree);
      if (info->io)
         info->io->destroy(info->io);
      PHYSFS_Allocator<>::Free(info);
   }
}

void UNPK_abandonArchive(void* opaque) {
   auto info = static_cast<UNPKinfo*>(opaque);
   if (info) {
      info->io = nullptr;
      UNPK_closeArchive(info);
   }
}

PHYSFS_Io* UNPK_openRead(void* opaque, const char* name) {
   auto info = static_cast<UNPKinfo*>(opaque);
   auto entry = findEntry(info, name);
   BAIL_IF_ERRPASS(not entry, nullptr);
   BAIL_IF(entry->tree.isdir, PHYSFS_ERR_NOT_A_FILE, nullptr);

   auto finfo  = PHYSFS_Allocator<UNPKfileinfo>(1);
   finfo->io = info->io->duplicate(info->io);
   finfo->io->seek(finfo->io, entry->startPos);
   finfo->curPos = 0;
   finfo->entry = entry;

   auto retval = PHYSFS_Allocator<PHYSFS_Io>(1, UNPK_Io);
   retval->opaque = finfo.Ref();
   return retval.Ref();
}

PHYSFS_Io* UNPK_openWrite(void*, const char*) {
   BAIL(PHYSFS_ERR_READ_ONLY, nullptr);
}

PHYSFS_Io* UNPK_openAppend(void*, const char*) {
   BAIL(PHYSFS_ERR_READ_ONLY, nullptr);
}

int UNPK_remove(void*, const char*) {
   BAIL(PHYSFS_ERR_READ_ONLY, 0);
}

int UNPK_mkdir(void*, const char*) {
   BAIL(PHYSFS_ERR_READ_ONLY, 0);
}

int UNPK_stat(void* opaque, const char* path, PHYSFS_Stat* stat) {
   auto info = static_cast<UNPKinfo*>(opaque);
   const auto* entry = findEntry(info, path);
   BAIL_IF_ERRPASS(not entry, 0);

   if (entry->tree.isdir) {
      stat->filetype = PHYSFS_FILETYPE_DIRECTORY;
      stat->filesize = 0;
   }
   else {
      stat->filetype = PHYSFS_FILETYPE_REGULAR;
      stat->filesize = entry->size;
   }

   stat->modtime = entry->mtime;
   stat->createtime = entry->ctime;
   stat->accesstime = -1;
   stat->readonly = 1;
   return 1;
}

void* UNPK_addEntry(
   void* opaque, char* name, const int isdir,
   const PHYSFS_sint64 ctime, const PHYSFS_sint64 mtime,
   const PHYSFS_uint64 pos, const PHYSFS_uint64 len
) {
   auto info  = static_cast<UNPKinfo*>(opaque);
   auto entry = static_cast<UNPKentry*>(__PHYSFS_DirTreeAdd(&info->tree, name, isdir));
   BAIL_IF_ERRPASS(not entry, nullptr);

   entry->startPos = isdir ? 0 : pos;
   entry->size = isdir ? 0 : len;
   entry->ctime = ctime;
   entry->mtime = mtime;
   return entry;
}

void* UNPK_openArchive(PHYSFS_Io* io, const int case_sensitive, const int only_usascii) {
   auto info = PHYSFS_Allocator<UNPKinfo>(1);
   __PHYSFS_DirTreeInit(&info->tree, sizeof(UNPKentry), case_sensitive, only_usascii);
   info->io = io;
   return info.Ref();
}