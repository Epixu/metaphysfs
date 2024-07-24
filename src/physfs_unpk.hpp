///                                                                           
/// Internal function/structure declaration. Do NOT include in your           
/// application.                                                              
/// Please see the file LICENSE.txt in the source's root directory.           
/// This file written by Ryan C. Gordon.                                      
///                                                                           
#pragma once
#include "physfs_internal.hpp"
#include "physfs_tree.hpp"


/// These are shared between some archivers                                   
/// LOTS of legacy formats that only use US ASCII, not actually UTF-8, so     
/// let them optimize here                                                    
void* UNPK_openArchive(PHYSFS_Io* io, const int case_sensitive, const int only_usascii);
void  UNPK_abandonArchive(void* opaque);
void  UNPK_closeArchive(void* opaque);
void* UNPK_addEntry(void* opaque, char* name, const int isdir,
   const PHYSFS_sint64 ctime, const PHYSFS_sint64 mtime,
   const PHYSFS_uint64 pos, const PHYSFS_uint64 len
);

PHYSFS_Io* UNPK_openRead(void* opaque, const char* name);
PHYSFS_Io* UNPK_openWrite(void* opaque, const char* name);
PHYSFS_Io* UNPK_openAppend(void* opaque, const char* name);

int UNPK_remove(void* opaque, const char* name);
int UNPK_mkdir(void* opaque, const char* name);
int UNPK_stat(void* opaque, const char* fn, PHYSFS_Stat* st);

#define UNPK_enumerate __PHYSFS_DirTreeEnumerate


