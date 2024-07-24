///                                                                           
/// Internal function/structure declaration. Do NOT include in your           
/// application.                                                              
/// Please see the file LICENSE.txt in the source's root directory.           
/// This file written by Ryan C. Gordon.                                      
///                                                                           
#pragma once
#include "physfs_internal.hpp"


/// Optional API many archivers use this to manage their directory tree       
/// !!! FIXME: document this better                                           

struct __PHYSFS_DirTreeEntry {
   char* name;                              // Full path in archive.      
   struct __PHYSFS_DirTreeEntry* hashnext;  // next item in hash bucket.  
   struct __PHYSFS_DirTreeEntry* children;  // linked list of kids, if dir
   struct __PHYSFS_DirTreeEntry* sibling;   // next item in same dir.     
   int isdir;
};

struct __PHYSFS_DirTree {
   __PHYSFS_DirTreeEntry* root;    /* root of directory tree.             */
   __PHYSFS_DirTreeEntry** hash;  /* all entries hashed for fast lookup. */
   size_t hashBuckets;            /* number of buckets in hash.          */
   size_t entrylen;    /* size in bytes of entries (including subclass). */
   int case_sensitive;  /* non-zero to treat entries as case-sensitive in DirTreeFind */
   int only_usascii;  /* non-zero to treat paths as US ASCII only (one byte per char, only 'A' through 'Z' are considered for case folding). */
};

/* LOTS of legacy formats that only use US ASCII, not actually UTF-8, so let them optimize here. */
int   __PHYSFS_DirTreeInit(__PHYSFS_DirTree* dt, const size_t entrylen, const int case_sensitive, const int only_usascii);
void* __PHYSFS_DirTreeAdd(__PHYSFS_DirTree* dt, char* name, const int isdir);
void* __PHYSFS_DirTreeFind(__PHYSFS_DirTree* dt, const char* path);

PHYSFS_EnumerateCallbackResult __PHYSFS_DirTreeEnumerate(void* opaque,
   const char* dname, PHYSFS_EnumerateCallback cb,
   const char* origdir, void* callbackdata
);

void __PHYSFS_DirTreeDeinit(__PHYSFS_DirTree* dt);

