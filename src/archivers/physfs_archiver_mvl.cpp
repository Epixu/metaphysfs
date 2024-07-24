/// 
/// MVL support routines for PhysicsFS.
/// 
/// This driver handles Descent II Movielib archives.
/// 
/// The file format of MVL is quite easy...
/// 
///   //MVL File format - Written by Heiko Herrmann
///   char sig[4] = {'D','M', 'V', 'L'}; // "DMVL"=Descent MoVie Library
/// 
///   int num_files; // the number of files in this MVL
/// 
///   struct {
///    char file_name[13]; // Filename, padded to 13 bytes with 0s
///    int file_size; // filesize in bytes
///   }DIR_STRUCT[num_files];
/// 
///   struct {
///    char data[file_size]; // The file data
///   }FILE_STRUCT[num_files];
/// 
/// (That info is from http://www.descent2.com/ddn/specs/mvl/)
/// 
/// Please see the file LICENSE.txt in the source's root directory.
/// 
///  This file written by Bradley Bell.
///  Based on grp.c by Ryan C. Gordon.
/// 
#include "physfs_internal.hpp"
#include "physfs_unpk.hpp"
#include <cassert>
#include <cstring>


namespace
{
   int mvlLoadEntries(
      PHYSFS_Io* io, const PHYSFS_uint32 count, void* arc
   ) {
      PHYSFS_uint32 pos = 8 + (17 * count);   /* past sig+metadata. */
      PHYSFS_uint32 i;

      for (i = 0; i < count; i++) {
         PHYSFS_uint32 size;
         char name[13];
         BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, name, 13), 0);
         BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, &size, 4), 0);
         name[12] = '\0';  /* just in case. */
         size = PHYSFS_swapLE(size);
         BAIL_IF_ERRPASS(!UNPK_addEntry(arc, name, 0, -1, -1, pos, size), 0);
         pos += size;
      }

      return 1;
   }

   void* MVL_openArchive(
      PHYSFS_Io* io, const char*, int forWriting, int* claimed
   ) {
      PHYSFS_uint8 buf[4];
      PHYSFS_uint32 count = 0;
      void* unpkarc;

      assert(io != nullptr);  /* shouldn't ever happen. */
      BAIL_IF(forWriting, PHYSFS_ERR_READ_ONLY, nullptr);
      BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, buf, 4), nullptr);
      BAIL_IF(memcmp(buf, "DMVL", 4) != 0, PHYSFS_ERR_UNSUPPORTED, nullptr);

      *claimed = 1;

      BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, &count, sizeof(count)), nullptr);
      count = PHYSFS_swapLE(count);

      unpkarc = UNPK_openArchive(io, 0, 1);
      BAIL_IF_ERRPASS(!unpkarc, nullptr);

      if (!mvlLoadEntries(io, count, unpkarc)) {
         UNPK_abandonArchive(unpkarc);
         return nullptr;
      }

      return unpkarc;
   }
}

const PHYSFS_Archiver __PHYSFS_Archiver_MVL = {
   CURRENT_PHYSFS_ARCHIVER_API_VERSION, {
      "MVL",
      "Descent II Movielib format",
      "Bradley Bell <btb@icculus.org>",
      "https://icculus.org/physfs/",
      0,  /* supportsSymlinks */
   },
   MVL_openArchive,
   UNPK_enumerate,
   UNPK_openRead,
   UNPK_openWrite,
   UNPK_openAppend,
   UNPK_remove,
   UNPK_mkdir,
   UNPK_stat,
   UNPK_closeArchive
};
