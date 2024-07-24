///                                                                           
/// QPAK support routines for PhysicsFS.                                      
///                                                                           
///  This archiver handles the archive format utilized by Quake 1 and 2.      
///  Quake3-based games use the PkZip/Info-Zip format (which our              
///  physfs_archiver_zip.c handles).                                          
///                                                                           
///  ======================================================================== 
///                                                                           
///  This format info (in more detail) comes from:                            
///     https://web.archive.org/web/20040209101748/http://debian.fmi.uni-sofia.bg/~sergei/cgsr/docs/pak.txt
///                                                                           
///  Quake PAK Format                                                         
///                                                                           
///  Header                                                                   
///   (4 bytes)  signature = 'PACK'                                           
///   (4 bytes)  directory offset                                             
///   (4 bytes)  directory length                                             
///                                                                           
///  Directory                                                                
///   (56 bytes) file name                                                    
///   (4 bytes)  file position                                                
///   (4 bytes)  file length                                                  
///                                                                           
///  ======================================================================== 
///                                                                           
/// Please see the file LICENSE.txt in the source's root directory.           
///                                                                           
///  This file written by Ryan C. Gordon.                                     
///                                                                           
#include "physfs_internal.hpp"
#include <cassert>


constexpr PHYSFS_uint32 QPAK_SIG = 0x4B434150;   // "PACK" in ASCII

namespace
{
   int qpakLoadEntries(PHYSFS_Io* io, const PHYSFS_uint32 count, void* arc) {
      PHYSFS_uint32 i;
      for (i = 0; i < count; i++) {
         PHYSFS_uint32 size;
         PHYSFS_uint32 pos;
         char name[56];
         BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, name, 56), 0);
         BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, &pos, 4), 0);
         BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, &size, 4), 0);
         size = PHYSFS_swapLE(size);
         pos = PHYSFS_swapLE(pos);
         BAIL_IF_ERRPASS(!UNPK_addEntry(arc, name, 0, -1, -1, pos, size), 0);
      }

      return 1;
   }

   void* QPAK_openArchive(PHYSFS_Io* io, const char*, int forWriting, int* claimed) {
      PHYSFS_uint32 val = 0;
      PHYSFS_uint32 pos = 0;
      PHYSFS_uint32 count = 0;
      void* unpkarc;

      assert(io != nullptr);  /* shouldn't ever happen. */

      BAIL_IF(forWriting, PHYSFS_ERR_READ_ONLY, nullptr);

      BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, &val, 4), nullptr);
      if (PHYSFS_swapLE(val) != QPAK_SIG)
         BAIL(PHYSFS_ERR_UNSUPPORTED, nullptr);

      *claimed = 1;

      BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, &val, 4), nullptr);
      pos = PHYSFS_swapLE(val);  /* directory table offset. */

      BAIL_IF_ERRPASS(!__PHYSFS_readAll(io, &val, 4), nullptr);
      count = PHYSFS_swapLE(val);

      /* corrupted archive? */
      BAIL_IF((count % 64) != 0, PHYSFS_ERR_CORRUPT, nullptr);
      count /= 64;

      BAIL_IF_ERRPASS(!io->seek(io, pos), nullptr);

      /* !!! FIXME: check case_sensitive and only_usascii params for this archive. */
      unpkarc = UNPK_openArchive(io, 1, 0);
      BAIL_IF_ERRPASS(!unpkarc, nullptr);

      if (!qpakLoadEntries(io, count, unpkarc)) {
         UNPK_abandonArchive(unpkarc);
         return nullptr;
      }

      return unpkarc;
   }
}

const PHYSFS_Archiver __PHYSFS_Archiver_QPAK =
{
    CURRENT_PHYSFS_ARCHIVER_API_VERSION,
    {
        "PAK",
        "Quake I/II format",
        "Ryan C. Gordon <icculus@icculus.org>",
        "https://icculus.org/physfs/",
        0,  /* supportsSymlinks */
    },
    QPAK_openArchive,
    UNPK_enumerate,
    UNPK_openRead,
    UNPK_openWrite,
    UNPK_openAppend,
    UNPK_remove,
    UNPK_mkdir,
    UNPK_stat,
    UNPK_closeArchive
};