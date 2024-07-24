///                                                                           
/// GRP support routines for PhysicsFS.                                       
///                                                                           
/// This driver handles BUILD engine archives ("groupfiles"). This format     
/// (but not this driver) was put together by Ken Silverman.                  
///                                                                           
/// The format is simple enough. In Ken's words:                              
///                                                                           
///   What's the .GRP file format?                                            
///                                                                           
///    The ".grp" file format is just a collection of a lot of files stored   
///    into 1 big one. I tried to make the format as simple as possible: The  
///    first 12 bytes contains my name, "KenSilverman". The next 4 bytes is   
///    the number of files that were compacted into the group file. Then for  
///    each file, there is a 16 byte structure, where the first 12 bytes are  
///    the filename, and the last 4 bytes are the file's size. The rest of    
///    the group file is just the raw data packed one after the other in the  
///    same order as the list of files.                                       
///                                                                           
/// (That info is from http://www.advsys.net/ken/build.htm ...)               
///                                                                           
/// Please see the file LICENSE.txt in the source's root directory.           
///                                                                           
/// This file written by Ryan C. Gordon.                                      
///                                                                           
#include "../physfs_internal.hpp"
#include <cassert>


namespace
{
   int grpLoadEntries(PHYSFS_Io* io, const PHYSFS_uint32 count, void* arc) {
      PHYSFS_uint32 pos = 16 + (16 * count);  // Past sig+metadata      
      PHYSFS_uint32 i;

      for (i = 0; i < count; i++) {
         char name[13];
         BAIL_IF_ERRPASS(not __PHYSFS_readAll(io, name, 12), 0);

         PHYSFS_uint32 size;
         BAIL_IF_ERRPASS(not __PHYSFS_readAll(io, &size, 4), 0);

         name[12] = '\0';  // Name isn't null-terminated in file        

         auto ptr = strchr(name, ' ');
         if (ptr)
            *ptr = '\0';   // Trim extra spaces                         

         size = PHYSFS_swapLE(size);
         BAIL_IF_ERRPASS(not UNPK_addEntry(arc, name, 0, -1, -1, pos, size), 0);

         pos += size;
      }

      return 1;
   }

   void* GRP_openArchive(PHYSFS_Io* io, const char*, int forWriting, int* claimed) {
      assert(io != nullptr);  // Shouldn't ever happen                  
      BAIL_IF(forWriting, PHYSFS_ERR_READ_ONLY, nullptr);

      PHYSFS_uint8 buf[12];
      BAIL_IF_ERRPASS(not __PHYSFS_readAll(io, buf, sizeof(buf)), nullptr);
      if (memcmp(buf, "KenSilverman", sizeof(buf)) != 0)
         BAIL(PHYSFS_ERR_UNSUPPORTED, nullptr);

      *claimed = 1;

      PHYSFS_uint32 count = 0;
      BAIL_IF_ERRPASS(not __PHYSFS_readAll(io, &count, sizeof(count)), nullptr);
      count = PHYSFS_swapLE(count);

      auto unpkarc = UNPK_openArchive(io, 0, 1);
      BAIL_IF_ERRPASS(!unpkarc, nullptr);

      if (not grpLoadEntries(io, count, unpkarc)) {
         UNPK_abandonArchive(unpkarc);
         return nullptr;
      }

      return unpkarc;
   }
}

const PHYSFS_Archiver __PHYSFS_Archiver_GRP = {
    CURRENT_PHYSFS_ARCHIVER_API_VERSION, {
        "GRP",
        "Build engine Groupfile format",
        "Ryan C. Gordon <icculus@icculus.org>",
        "https://icculus.org/physfs/",
        0,  /* supportsSymlinks */
    },
    GRP_openArchive,
    UNPK_enumerate,
    UNPK_openRead,
    UNPK_openWrite,
    UNPK_openAppend,
    UNPK_remove,
    UNPK_mkdir,
    UNPK_stat,
    UNPK_closeArchive
};