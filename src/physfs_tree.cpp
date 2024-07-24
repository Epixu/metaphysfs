///                                                                           
/// Internal function/structure declaration. Do NOT include in your           
/// application.                                                              
/// Please see the file LICENSE.txt in the source's root directory.           
/// This file written by Ryan C. Gordon.                                      
///                                                                           
#include "physfs_tree.hpp"


//TODO SHOULD THROW ON FAIL
int __PHYSFS_DirTreeInit(__PHYSFS_DirTree* dt, const size_t entrylen, const int case_sensitive, const int only_usascii) {
   static char rootpath[2] = {'/', '\0'};
   size_t alloclen;

   assert(entrylen >= sizeof(__PHYSFS_DirTreeEntry));

   memset(dt, '\0', sizeof(*dt));
   dt->case_sensitive = case_sensitive;
   dt->only_usascii = only_usascii;

   dt->root = (__PHYSFS_DirTreeEntry*) allocator.Malloc(entrylen);
   BAIL_IF(!dt->root, PHYSFS_ERR_OUT_OF_MEMORY, 0);
   memset(dt->root, '\0', entrylen);
   dt->root->name = rootpath;
   dt->root->isdir = 1;
   dt->hashBuckets = 64;
   if (!dt->hashBuckets)
      dt->hashBuckets = 1;
   dt->entrylen = entrylen;

   alloclen = dt->hashBuckets * sizeof(__PHYSFS_DirTreeEntry*);
   dt->hash = (__PHYSFS_DirTreeEntry**) allocator.Malloc(alloclen);
   BAIL_IF(!dt->hash, PHYSFS_ERR_OUT_OF_MEMORY, 0);
   memset(dt->hash, '\0', alloclen);
   return 1;
}

static PHYSFS_uint32 hashPathName(__PHYSFS_DirTree* dt, const char* name) {
   const PHYSFS_uint32 hashval = dt->case_sensitive ? __PHYSFS_hashString(name) : dt->only_usascii ? __PHYSFS_hashStringCaseFoldUSAscii(name) : __PHYSFS_hashStringCaseFold(name);
   return hashval % dt->hashBuckets;
}

/// Fill in missing parent directories                                        
static __PHYSFS_DirTreeEntry* addAncestors(__PHYSFS_DirTree* dt, char* name) {
   __PHYSFS_DirTreeEntry* retval = dt->root;
   char* sep = strrchr(name, '/');

   if (sep) {
      *sep = '\0';         // Chop off last piece                       
      retval = (__PHYSFS_DirTreeEntry*) __PHYSFS_DirTreeFind(dt, name);

      if (retval != nullptr) {
         *sep = '/';
         BAIL_IF(!retval->isdir, PHYSFS_ERR_CORRUPT, nullptr);
         return retval;    // Already hashed                            
      }

      // Okay, this is a new dir. Build and hash us                     
      retval = (__PHYSFS_DirTreeEntry*) __PHYSFS_DirTreeAdd(dt, name, 1);
      *sep = '/';
   }

   return retval;
}

void* __PHYSFS_DirTreeAdd(__PHYSFS_DirTree* dt, char* name, const int isdir) {
   __PHYSFS_DirTreeEntry* retval = __PHYSFS_DirTreeFind(dt, name);
   if (retval)
      return retval;

   const size_t alloclen = strlen(name) + 1 + dt->entrylen;
   PHYSFS_uint32 hashval;
   auto parent = addAncestors(dt, name);
   BAIL_IF_ERRPASS(!parent, nullptr);
   assert(dt->entrylen >= sizeof(__PHYSFS_DirTreeEntry));

   retval = (__PHYSFS_DirTreeEntry*) allocator.Malloc(alloclen);
   BAIL_IF(!retval, PHYSFS_ERR_OUT_OF_MEMORY, nullptr);
   memset(retval, '\0', dt->entrylen);
   retval->name = ((char*) retval) + dt->entrylen;
   strcpy(retval->name, name);
   hashval = hashPathName(dt, name);
   retval->hashnext = dt->hash[hashval];
   dt->hash[hashval] = retval;
   retval->sibling = parent->children;
   retval->isdir = isdir;
   parent->children = retval;
   return retval;
}

/// Find the __PHYSFS_DirTreeEntry for a path in platform-independent notation   
void* __PHYSFS_DirTreeFind(__PHYSFS_DirTree* dt, const char* path) {
   const int cs = dt->case_sensitive;
   __PHYSFS_DirTreeEntry* prev = nullptr;
   __PHYSFS_DirTreeEntry* retval;

   if (*path == '\0')
      return dt->root;

   auto hashval = hashPathName(dt, path);
   for (retval = dt->hash[hashval]; retval; retval = retval->hashnext) {
      const int cmp = cs
         ? strcmp(retval->name, path)
         : PHYSFS_utf8stricmp(retval->name, path);

      if (cmp == 0) {
         if (prev) {
            // Move this to the front of the list                       
            prev->hashnext = retval->hashnext;
            retval->hashnext = dt->hash[hashval];
            dt->hash[hashval] = retval;
         }

         return retval;
      }

      prev = retval;
   }

   BAIL(PHYSFS_ERR_NOT_FOUND, nullptr);
}

PHYSFS_EnumerateCallbackResult __PHYSFS_DirTreeEnumerate(
   void* opaque, const char* dname, PHYSFS_EnumerateCallback cb,
   const char* origdir, void* callbackdata
) {
   PHYSFS_EnumerateCallbackResult retval = PHYSFS_ENUM_OK;
   __PHYSFS_DirTree* tree = (__PHYSFS_DirTree*) opaque;
   const __PHYSFS_DirTreeEntry* entry = __PHYSFS_DirTreeFind(tree, dname);
   BAIL_IF(!entry, PHYSFS_ERR_NOT_FOUND, PHYSFS_ENUM_ERROR);

   entry = entry->children;

   while (entry and retval == PHYSFS_ENUM_OK) {
      const char* name = entry->name;
      const char* ptr = strrchr(name, '/');
      retval = cb(callbackdata, origdir, ptr ? ptr + 1 : name);
      BAIL_IF(retval == PHYSFS_ENUM_ERROR, PHYSFS_ERR_APP_CALLBACK, retval);
      entry = entry->sibling;
   }

   return retval;
}

void __PHYSFS_DirTreeDeinit(__PHYSFS_DirTree* dt) {
   if (not dt)
      return;

   if (dt->root) {
      assert(not dt->root->sibling);
      assert(dt->hash or not dt->root->children);
      allocator.Free(dt->root);
   }

   if (dt->hash) {
      for (size_t i = 0; i < dt->hashBuckets; i++) {
         __PHYSFS_DirTreeEntry* next;
         for (auto entry = dt->hash[i]; entry; entry = next) {
            next = entry->hashnext;
            allocator.Free(entry);
         }
      }

      allocator.Free(dt->hash);
   }
}
