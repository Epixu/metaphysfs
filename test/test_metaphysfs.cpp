/// Test program for PhysicsFS. May only work on Unix                            
/// Please see the file LICENSE.txt in the source's root directory.              
/// This file written by Ryan C. Gordon, modernized by Dimo Markov               
#define _CRT_SECURE_NO_WARNINGS
#include <print>

#if defined(__MWERKS__)
   #include <SIOUX.h>
#endif

#if defined(PHYSFS_HAVE_READLINE)
   #include <unistd.h>
   #include <readline/readline.h>
   #include <readline/history.h>
#endif

#include <chrono>
#include <physfs.hpp>

static constexpr int TEST_VERSION_MAJOR = 3;
static constexpr int TEST_VERSION_MINOR = 3;
static constexpr int TEST_VERSION_PATCH = 0;

static FILE* history_file = nullptr;
static PHYSFS_uint32 do_buffer_size = 0;


void output_versions() {
   PHYSFS_Version compiled;
   PHYSFS_Version linked;

   PHYSFS_VERSION(&compiled);
   PHYSFS_getLinkedVersion(&linked);

   std::println("test_physfs version {}.{}.{}.",
      TEST_VERSION_MAJOR, TEST_VERSION_MINOR, TEST_VERSION_PATCH);
   std::println(" Compiled against PhysicsFS version {}.{}.{},",
      compiled.major, compiled.minor, compiled.patch);
   std::println(" and linked against {}.{}.{}.\n",
      linked.major, linked.minor, linked.patch);
}

void output_archivers() {
   auto rc = PHYSFS_supportedArchiveTypes();
   std::println("Supported archive types:");

   if (not *rc)
      std::println(" * Apparently, NONE!");
   else for (auto i = rc; *i; ++i) {
      std::print(" * {}: {}\n    Written by {}.\n    {}\n",
         (*i)->extension, (*i)->description,
         (*i)->author,    (*i)->url
      );
      std::print("    {} symbolic links.\n",
         (*i)->supportsSymlinks ? "Supports" : "Does not support"
      );
   }
   std::println("");
}

int cmd_quit(char*) {
   return 0;
}

int cmd_init(char* args) {
   if (*args == '\"') {
      args++;
      args[strlen(args) - 1] = '\0';
   }

   if (PHYSFS_init(args))
      std::println("Successful.");
   else
      std::println("Failure. reason: {}.", PHYSFS_getLastError());
   return 1;
}

int cmd_deinit(char*) {
   if (PHYSFS_deinit())
      std::println ("Successful.");
   else
      std::println("Failure. reason: {}.", PHYSFS_getLastError());
   return 1;
}

int cmd_addarchive(char* args) {
   char* ptr = strrchr(args, ' ');
   int appending = atoi(ptr + 1);
   *ptr = '\0';

   if (*args == '\"') {
      args++;
      *(ptr - 1) = '\0';
   }

   if (PHYSFS_mount(args, nullptr, appending))
      std::println("Successful.");
   else
      std::println("Failure. reason: {}.", PHYSFS_getLastError());
   return 1;
}

/// Wrap free() to avoid calling convention wankery                           
void freeBuf(void* buf) {
   free(buf);
}

/// Wrap malloc, too                                                          
template<class T>
T* mpfsAloc(size_t n) {
   return static_cast<T*>(malloc(n));
}
template<class T>
T* mpfsRealloc(T* prev, size_t n) {
   return static_cast<T*>(realloc(prev, n));
}

enum class MountType { Path, Memory, Handle };

int cmd_mount_internal(char* args, const MountType mnttype) {
   char* ptr;
   if (*args == '\"') {
      args++;
      ptr = strchr(args, '\"');
      if (not ptr) {
         std::println("missing string terminator in argument.");
         return 1;
      }

      *(ptr) = '\0';
   }
   else {
      ptr = strchr(args, ' ');
      *ptr = '\0';
   }

   auto mntpoint = ptr + 1;
   if (*mntpoint == '\"') {
      mntpoint++;
      ptr = strchr(mntpoint, '\"');
      if (not ptr) {
         std::println("missing string terminator in argument.");
         return 1;
      }

      *(ptr) = '\0';
   }
   else {
      ptr = strchr(mntpoint, ' ');
      *(ptr) = '\0';
   }

   int rc = 0;
   auto appending = atoi(ptr + 1);
   if (mnttype == MountType::Path)
      rc = PHYSFS_mount(args, mntpoint, appending);

   else if (mnttype == MountType::Handle) {
      auto f = PHYSFS_openRead(args);
      if (not f) {
         std::println("PHYSFS_openRead('{}') failed. reason: {}.",
            args, PHYSFS_getLastError());
         return 1;
      }

      rc = PHYSFS_mountHandle(f, args, mntpoint, appending);

      if (not rc)
         PHYSFS_close(f);
   }

   else if (mnttype == MountType::Memory) {
      auto in = fopen(args, "rb");
      long len = 0;

      if (not in) {
         std::println("Failed to open {} to read into memory: {}.",
            args, strerror(errno));
         return 1;
      }

      if (fseek(in, 0, SEEK_END) != 0 or (len = ftell(in)) < 0) {
         std::println("Failed to find size of {} to read into memory: {}.",
            args, strerror(errno));
         fclose(in);
         return 1;
      }

      auto buf = malloc(len);

      if (not buf) {
         std::println("Failed to allocate space to read {} into memory: {}.",
            args, strerror(errno));
         fclose(in);
         return 1;
      }

      if (fseek(in, 0, SEEK_SET) != 0 or fread(buf, len, 1, in) != 1) {
         std::println("Failed to read {} into memory: {}.",
            args, strerror(errno));
         fclose(in);
         free(buf);
         return 1;
      }

      fclose(in);

      rc = PHYSFS_mountMemory(buf, len, freeBuf, args, mntpoint, appending);
   }

   if (rc)
      std::println("Successful.");
   else
      std::println("Failure. reason: {}.", PHYSFS_getLastError());
   return 1;
}

int cmd_mount(char* args) {
   return cmd_mount_internal(args, MountType::Path);
}

int cmd_mount_mem(char* args) {
   return cmd_mount_internal(args, MountType::Memory);
}

int cmd_mount_handle(char* args) {
   return cmd_mount_internal(args, MountType::Handle);
}

int cmd_getmountpoint(char* args) {
   if (*args == '\"') {
      args++;
      args[strlen(args) - 1] = '\0';
   }

   std::println("Dir [{}] is mounted at [{}].",
      args, PHYSFS_getMountPoint(args));
   return 1;
}

int cmd_setroot(char* args) {
   char* ptr;

   auto archive = args;
   if (*archive == '\"') {
      archive++;
      ptr = strchr(archive, '\"');
      if (not ptr) {
         std::println("missing string terminator in argument.");
         return 1;
      }

      *(ptr) = '\0';
   }
   else {
      ptr = strchr(archive, ' ');
      *ptr = '\0';
   }

   auto subdir = ptr + 1;
   if (*subdir == '\"') {
      subdir++;
      ptr = strchr(subdir, '\"');
      if (not ptr) {
         std::println("missing string terminator in argument.");
         return 1;
      }

      *(ptr) = '\0';
   }

   if (PHYSFS_setRoot(archive, subdir))
      std::println("Successful.");
   else
      std::println("Failure. reason: {}.", PHYSFS_getLastError());
   return 1;
}

int cmd_removearchive(char* args) {
   if (*args == '\"') {
      args++;
      args[strlen(args) - 1] = '\0';
   }

   if (PHYSFS_unmount(args))
      std::println("Successful.");
   else
      std::println("Failure. reason: {}.", PHYSFS_getLastError());
   return 1;
}

int cmd_enumerate(char* args) {
   if (*args == '\"') {
      args++;
      args[strlen(args) - 1] = '\0';
   }

   auto rc = PHYSFS_enumerateFiles(args);
   if (not rc)
      std::println("Failure. reason: {}.", PHYSFS_getLastError());
   else {
      int file_count = 0;
      for (auto i = rc; *i; i++, file_count++)
         std::println("{}", *i);

      std::print("\n total ({}) files.\n", file_count);
      PHYSFS_freeList(rc);
   }

   return 1;
}

#define STR_BOX_VERTICAL_RIGHT  "\xe2\x94\x9c"
#define STR_BOX_VERTICAL        "\xe2\x94\x82"
#define STR_BOX_HORIZONTAL      "\xe2\x94\x80"
#define STR_BOX_UP_RIGHT        "\xe2\x94\x94"
#define STR_NBSP                "\xc2\xa0"

#define PREFIX_DIRENTRY         STR_BOX_VERTICAL_RIGHT  STR_BOX_HORIZONTAL  STR_BOX_HORIZONTAL  STR_NBSP
#define PREFIX_DIRENTRY_LAST    STR_BOX_UP_RIGHT        STR_BOX_HORIZONTAL  STR_BOX_HORIZONTAL  STR_NBSP
#define PREFIX_RECURSIVE        STR_BOX_VERTICAL        STR_NBSP            STR_NBSP            STR_NBSP
#define PREFIX_RECURSIVE_LAST   STR_NBSP                STR_NBSP            STR_NBSP            STR_NBSP

void cmd_tree_recursive(
   const char* prefix,
   const char* fullPath,
   const char* name,
   unsigned depth,
   PHYSFS_uint64* total_dir_count,
   PHYSFS_uint64* total_file_count
) {
   std::print("{}", name);

   auto rc = PHYSFS_enumerateFiles(fullPath);
   if (not rc)
      std::println(" [Failure. reason: {}]", PHYSFS_getLastError());
   else {
      std::println("");

      int file_count = 0;
      for (auto i = rc; *i; i++, file_count++) {
         auto newFullPath = mpfsAloc<char>(strlen(fullPath) + strlen(*i) + 2);
         strcpy(newFullPath, fullPath);
         strcat(newFullPath, "/");
         strcat(newFullPath, *i);

         const char* thisPrefix;
         if (i[1])
            thisPrefix = PREFIX_DIRENTRY;
         else
            thisPrefix = PREFIX_DIRENTRY_LAST;

         if (PHYSFS_isSymbolicLink(newFullPath))
            std::println("{}{}{} [symbolic link]", prefix, thisPrefix, *i);
         else if (PHYSFS_isDirectory(newFullPath)) {
            *total_dir_count += 1;

            char* newPrefix;
            if (i[1]) {
               newPrefix = mpfsAloc<char>(strlen(prefix) + strlen(PREFIX_RECURSIVE) + 1);
               strcpy(newPrefix, prefix);
               strcat(newPrefix, PREFIX_RECURSIVE);
            }
            else {
               newPrefix = mpfsAloc<char>(strlen(prefix) + strlen(PREFIX_RECURSIVE_LAST) + 1);
               strcpy(newPrefix, prefix);
               strcat(newPrefix, PREFIX_RECURSIVE_LAST);
            }

            std::print("{}{}", prefix, thisPrefix);
            cmd_tree_recursive(newPrefix, newFullPath, *i, depth + 1, total_dir_count, total_file_count);
            free(newPrefix);
         }
         else {
            *total_file_count += 1;

            std::println("{}{}{}", prefix, thisPrefix, *i);
         }

         free(newFullPath);
      }

      PHYSFS_freeList(rc);
   }
}

int cmd_tree(char* args) {
   PHYSFS_uint64 total_dir_count = 0;
   PHYSFS_uint64 total_file_count = 0;
   if (*args == '\"') {
      args++;
      args[strlen(args) - 1] = '\0';
   }

   cmd_tree_recursive("", args, args, 0, &total_dir_count, &total_file_count);
   std::print("\n{} directories, {} files\n", total_dir_count, total_file_count);
   return 1;
}

int cmd_getdirsep(char*) {
   std::println("Directory separator is [{}].", PHYSFS_getDirSeparator());
   return 1;
}

int cmd_getlasterror(char*) {
   std::println("last error is [{}].", PHYSFS_getLastError());
   return 1;
}

int cmd_getcdromdirs(char* args) {
   auto rc = PHYSFS_getCdRomDirs();
   if (not rc)
      std::println("Failure. Reason: [{}].", PHYSFS_getLastError());
   else {
      int dir_count = 0;
      for (auto i = rc; *i; i++, dir_count++)
         std::println("{}", *i);

      std::print("\n total ({}) drives.\n", dir_count);
      PHYSFS_freeList(rc);
   }

   return 1;
}

int cmd_getsearchpath(char* args) {
   auto rc = PHYSFS_getSearchPath();
   if (not rc)
      std::println("Failure. reason: {}.", PHYSFS_getLastError());
   else {
      int dir_count = 0;
      for (auto i = rc; *i; i++, dir_count++)
         std::println("{}", *i);

      std::print("\n total ({}) directories.\n", dir_count);
      PHYSFS_freeList(rc);
   }

   return 1;
}

int cmd_getbasedir(char* args) {
   std::println("Base dir is [{}].", PHYSFS_getBaseDir());
   return 1;
}

int cmd_getuserdir(char* args) {
   std::println("User dir is [{}].", PHYSFS_getUserDir());
   return 1;
}

int cmd_getprefdir(char* args) {
   char* appName;
   auto ptr = args;
   auto org = ptr;
   ptr = strchr(ptr, ' '); *ptr = '\0'; ptr++; appName = ptr;
   std::println("Pref dir is [{}].", PHYSFS_getPrefDir(org, appName));
   return 1;
}

int cmd_getwritedir(char* args) {
   std::println("Write dir is [{}].", PHYSFS_getWriteDir());
   return 1;
}

int cmd_setwritedir(char* args) {
   if (*args == '\"') {
      args++;
      args[strlen(args) - 1] = '\0';
   }

   if (PHYSFS_setWriteDir(args))
      std::println("Successful.");
   else
      std::println("Failure. reason: {}.", PHYSFS_getLastError());
   return 1;
}

int cmd_permitsyms(char* args) {
   if (*args == '\"') {
      args++;
      args[strlen(args) - 1] = '\0';
   }

   auto num = atoi(args);
   PHYSFS_permitSymbolicLinks(num);
   std::println("Symlinks are now {}.", num ? "permitted" : "forbidden");
   return 1;
}

int cmd_setbuffer(char* args) {
   if (*args == '\"') {
      args++;
      args[strlen(args) - 1] = '\0';
   }

   do_buffer_size = (unsigned int) atoi(args);

   if (do_buffer_size)
      std::println("Further tests will set a ({}) size buffer.", do_buffer_size);
   else
      std::println("Further tests will NOT use a buffer.");
   return 1;
}

int cmd_stressbuffer(char* args) {
   if (*args == '\"') {
      args++;
      args[strlen(args) - 1] = '\0';
   }

   auto num = atoi(args);
   if (num < 0)
      std::println("buffer must be greater than or equal to zero.");
   else {
      int rndnum;

      std::println("Stress testing with ({}) byte buffer...", num);
      auto f = PHYSFS_openWrite("test.txt");
      if (not f)
         std::println("Couldn't open test.txt for writing: {}.", PHYSFS_getLastError());
      else {
         int i, j;
         char buf[37];
         char buf2[37];

         if (not PHYSFS_setBuffer(f, num)) {
            std::println("PHYSFS_setBuffer() failed: {}.", PHYSFS_getLastError());
            PHYSFS_close(f);
            PHYSFS_delete("test.txt");
            return 1;
         }

         strcpy(buf, "abcdefghijklmnopqrstuvwxyz0123456789");
         srand((unsigned int)time(nullptr));

         for (i = 0; i < 10; i++) {
            for (j = 0; j < 10000; j++) {
               PHYSFS_uint32 right = 1 + (PHYSFS_uint32)(35.0 * rand() / (RAND_MAX + 1.0));
               PHYSFS_uint32 left = 36 - right;
               if (PHYSFS_writeBytes(f, buf, left) != left) {
                  std::println("PHYSFS_writeBytes() failed: {}.", PHYSFS_getLastError());
                  PHYSFS_close(f);
                  return 1;
               }

               rndnum = 1 + (int)(1000.0 * rand() / (RAND_MAX + 1.0));
               if (rndnum == 42) {
                  if (not PHYSFS_flush(f)) {
                     std::println("PHYSFS_flush() failed: {}.", PHYSFS_getLastError());
                     PHYSFS_close(f);
                     return 1;
                  }
               }

               if (PHYSFS_writeBytes(f, buf + left, right) != right) {
                  std::println("PHYSFS_writeBytes() failed: {}.", PHYSFS_getLastError());
                  PHYSFS_close(f);
                  return 1;
               }

               rndnum = 1 + (int)(1000.0 * rand() / (RAND_MAX + 1.0));
               if (rndnum == 42) {
                  if (not PHYSFS_flush(f)) {
                     std::println("PHYSFS_flush() failed: {}.", PHYSFS_getLastError());
                     PHYSFS_close(f);
                     return 1;
                  }
               }
            }

            if (not PHYSFS_flush(f)) {
               std::println("PHYSFS_flush() failed: {}.", PHYSFS_getLastError());
               PHYSFS_close(f);
               return 1;
            }

         }

         if (not PHYSFS_close(f)) {
            std::println("PHYSFS_close() failed: {}.", PHYSFS_getLastError());
            return 1;
         }

         std::println(" ... test file written ...");
         f = PHYSFS_openRead("test.txt");
         if (not f) {
            std::println("Failed to reopen stress file for reading: {}.", PHYSFS_getLastError());
            return 1;
         }

         if (not PHYSFS_setBuffer(f, num)) {
            std::println("PHYSFS_setBuffer() failed: {}.", PHYSFS_getLastError());
            PHYSFS_close(f);
            return 1;
         }

         for (i = 0; i < 10; i++) {
            for (j = 0; j < 10000; j++) {
               PHYSFS_uint32 right = 1 + (PHYSFS_uint32)(35.0 * rand() / (RAND_MAX + 1.0));
               PHYSFS_uint32 left = 36 - right;
               if (PHYSFS_readBytes(f, buf2, left) != left) {
                  std::println("PHYSFS_readBytes() failed: {}.", PHYSFS_getLastError());
                  PHYSFS_close(f);
                  return 1;
               }

               rndnum = 1 + (int)(1000.0 * rand() / (RAND_MAX + 1.0));
               if (rndnum == 42) {
                  if (not PHYSFS_flush(f)) {
                     std::println("PHYSFS_flush() failed: {}.", PHYSFS_getLastError());
                     PHYSFS_close(f);
                     return 1;
                  }
               }

               if (PHYSFS_readBytes(f, buf2 + left, right) != right) {
                  std::println("PHYSFS_readBytes() failed: {}.", PHYSFS_getLastError());
                  PHYSFS_close(f);
                  return 1;
               }

               rndnum = 1 + (int)(1000.0 * rand() / (RAND_MAX + 1.0));

               if (rndnum == 42) {
                  if (not PHYSFS_flush(f)) {
                     std::println("PHYSFS_flush() failed: {}.", PHYSFS_getLastError());
                     PHYSFS_close(f);
                     return 1;
                  }
               }

               if (memcmp(buf, buf2, 36) != 0) {
                  std::println("readback is mismatched on iterations ({}, {}).", i, j);
                  std::print("wanted: [");
                  for (i = 0; i < 36; i++)
                     std::print("{}", buf[i]);
                  std::println("]");

                  std::print("   got: [");
                  for (i = 0; i < 36; i++)
                     std::print("{}", buf2[i]);
                  std::println("]");
                  PHYSFS_close(f);
                  return 1;
               }
            }

            if (not PHYSFS_flush(f)) {
               std::println("PHYSFS_flush() failed: {}.", PHYSFS_getLastError());
               PHYSFS_close(f);
               return 1;
            }
         }

         std::println(" ... test file read ...");

         if (not PHYSFS_eof(f))
            std::println("PHYSFS_eof() returned true! That's wrong.");

         if (not PHYSFS_close(f)) {
            std::println("PHYSFS_close() failed: {}.", PHYSFS_getLastError());
            return 1;
         }

         PHYSFS_delete("test.txt");
         std::println("stress test completed successfully.");
      }
   }

   return 1;
}

int cmd_setsaneconfig(char* args) {
   char* appName;
   char* arcExt;
   int inclCD;

   /* ugly. */
   char* ptr = args;
   auto org = ptr;
   ptr = strchr(ptr, ' '); *ptr = '\0'; ptr++; appName = ptr;
   ptr = strchr(ptr, ' '); *ptr = '\0'; ptr++; arcExt = ptr;
   ptr = strchr(ptr, ' '); *ptr = '\0'; ptr++; inclCD = atoi(arcExt);

   if (strcmp(arcExt, "!") == 0)
      arcExt = nullptr;

   int arcsFirst = atoi(ptr);
   if (PHYSFS_setSaneConfig(org, appName, arcExt, inclCD, arcsFirst))
      std::println("Successful.");
   else
      std::println("Failure. reason: {}.", PHYSFS_getLastError());
   return 1;
}

int cmd_mkdir(char* args) {
   if (*args == '\"') {
      args++;
      args[strlen(args) - 1] = '\0';
   }

   if (PHYSFS_mkdir(args))
      std::println("Successful.");
   else
      std::println("Failure. reason: {}.", PHYSFS_getLastError());
   return 1;
}

int cmd_delete(char* args) {
   if (*args == '\"') {
      args++;
      args[strlen(args) - 1] = '\0';
   }

   if (PHYSFS_delete(args))
      std::println("Successful.");
   else
      std::println("Failure. reason: {}.", PHYSFS_getLastError());
   return 1;
}

int cmd_getrealdir(char* args) {
   if (*args == '\"') {
      args++;
      args[strlen(args) - 1] = '\0';
   }

   auto rc = PHYSFS_getRealDir(args);
   if (rc)
      std::println("Found at [{}].", rc);
   else
      std::println("Not found.");
   return 1;
}

int cmd_exists(char* args) {
   if (*args == '\"') {
      args++;
      args[strlen(args) - 1] = '\0';
   }

   auto rc = PHYSFS_exists(args);
   std::println("File {} exists.", rc ? "" : "does not");
   return 1;
}

int cmd_isdir(char* args) {
   if (*args == '\"') {
      args++;
      args[strlen(args) - 1] = '\0';
   }

   PHYSFS_Stat statbuf;
   auto rc = PHYSFS_stat(args, &statbuf);
   if (rc)
      rc = (statbuf.filetype == PHYSFS_FILETYPE_DIRECTORY);

   std::println("File {} a directory.", rc ? "is" : "is NOT");
   return 1;
}

int cmd_issymlink(char* args) {
   if (*args == '\"') {
      args++;
      args[strlen(args) - 1] = '\0';
   }

   PHYSFS_Stat statbuf;
   auto rc = PHYSFS_stat(args, &statbuf);
   if (rc)
      rc = (statbuf.filetype == PHYSFS_FILETYPE_SYMLINK);

   std::println("File {} a symlink.", rc ? "is" : "is NOT");
   return 1;
}

int cmd_cat(char* args) {
   if (*args == '\"') {
      args++;
      args[strlen(args) - 1] = '\0';
   }

   auto f = PHYSFS_openRead(args);
   if (not f)
      std::println("failed to open. Reason: [{}].", PHYSFS_getLastError());
   else {
      if (do_buffer_size) {
         if (not PHYSFS_setBuffer(f, do_buffer_size)) {
            std::println("failed to set file buffer. Reason: [{}].", PHYSFS_getLastError());
            PHYSFS_close(f);
            return 1;
         }
      }

      while (1) {
         char buffer[128];
         auto rc = PHYSFS_readBytes(f, buffer, sizeof(buffer));

         for (PHYSFS_sint64 i = 0; i < rc; i++)
            std::print("{}", buffer[i]);

         if (rc < sizeof(buffer)) {
            std::print("\n\n");
            if (not PHYSFS_eof(f))
               std::print("\n (Error condition in reading. Reason: [{}])\n\n", PHYSFS_getLastError());

            PHYSFS_close(f);
            return 1;
         }
      }
   }

   return 1;
}

int cmd_cat2(char* args) {
   PHYSFS_File* f1 = nullptr;
   PHYSFS_File* f2 = nullptr;
   char* ptr;

   auto fname1 = args;
   if (*fname1 == '\"') {
      fname1++;
      ptr = strchr(fname1, '\"');
      if (not ptr) {
         std::println("missing string terminator in argument.");
         return 1;
      }

      *(ptr) = '\0';
   }
   else {
      ptr = strchr(fname1, ' ');
      *ptr = '\0';
   }

   auto fname2 = ptr + 1;
   if (*fname2 == '\"') {
      fname2++;
      ptr = strchr(fname2, '\"');

      if (ptr == nullptr) {
         std::println("missing string terminator in argument.");
         return 1;
      }

      *(ptr) = '\0';
   }

   if (not (f1 = PHYSFS_openRead(fname1)))
      std::println("failed to open '{}'. Reason: [{}].", fname1, PHYSFS_getLastError());
   else if (not (f2 = PHYSFS_openRead(fname2)))
      std::println("failed to open '{}'. Reason: [{}].", fname2, PHYSFS_getLastError());
   else {
      char* buffer1 = nullptr;
      size_t buffer1len = 0;
      char* buffer2 = nullptr;
      size_t buffer2len = 0;
      char* ptr = nullptr;
      size_t i;

      if (do_buffer_size) {
         if (not PHYSFS_setBuffer(f1, do_buffer_size)) {
            std::println("failed to set file buffer for '{}'. Reason: [{}].", fname1, PHYSFS_getLastError());
            PHYSFS_close(f1);
            PHYSFS_close(f2);
            return 1;
         }
         else if (not PHYSFS_setBuffer(f2, do_buffer_size)) {
            std::println("failed to set file buffer for '{}'. Reason: [{}].", fname2, PHYSFS_getLastError());
            PHYSFS_close(f1);
            PHYSFS_close(f2);
            return 1;
         }
      }

      do {
         int readlen = 128;
         PHYSFS_sint64 rc;

         ptr = mpfsRealloc(buffer1, buffer1len + readlen);
         if (not ptr) {
            printf("(Out of memory.)\n\n");
            free(buffer1);
            free(buffer2);
            PHYSFS_close(f1);
            PHYSFS_close(f2);
            return 1;
         }

         buffer1 = ptr;

         rc = PHYSFS_readBytes(f1, buffer1 + buffer1len, readlen);
         if (rc < 0) {
            printf("(Error condition in reading '%s'. Reason: [%s])\n\n",
               fname1, PHYSFS_getLastError());
            free(buffer1);
            free(buffer2);
            PHYSFS_close(f1);
            PHYSFS_close(f2);
            return 1;
         }

         buffer1len += (size_t)rc;

         ptr = mpfsRealloc(buffer2, buffer2len + readlen);
         if (not ptr) {
            printf("(Out of memory.)\n\n");
            free(buffer1);
            free(buffer2);
            PHYSFS_close(f1);
            PHYSFS_close(f2);
            return 1;
         }

         buffer2 = ptr;
         rc = PHYSFS_readBytes(f2, buffer2 + buffer2len, readlen);
         if (rc < 0) {
            printf("(Error condition in reading '%s'. Reason: [%s])\n\n",
               fname2, PHYSFS_getLastError());
            free(buffer1);
            free(buffer2);
            PHYSFS_close(f1);
            PHYSFS_close(f2);
            return 1;
         }

         buffer2len += (size_t)rc;
      }
      while (not PHYSFS_eof(f1) or not PHYSFS_eof(f2));

      printf("file '%s' ...\n\n", fname1);
      for (i = 0; i < buffer1len; i++)
         fputc((int)buffer1[i], stdout);
      free(buffer1);

      printf("\n\nfile '%s' ...\n\n", fname2);
      for (i = 0; i < buffer2len; i++)
         fputc((int)buffer2[i], stdout);
      free(buffer2);

      printf("\n\n");
   } /* else */

   if (f1)
      PHYSFS_close(f1);

   if (f2)
      PHYSFS_close(f2);

   return 1;
} /* cmd_cat2 */


#define CRC32_BUFFERSIZE 512
static int cmd_crc32(char* args) {
   PHYSFS_File* f;

   if (*args == '\"') {
      args++;
      args[strlen(args) - 1] = '\0';
   } /* if */

   f = PHYSFS_openRead(args);
   if (f == nullptr)
      printf("failed to open. Reason: [%s].\n", PHYSFS_getLastError());
   else {
      PHYSFS_uint8 buffer[CRC32_BUFFERSIZE];
      PHYSFS_uint32 crc = -1;
      PHYSFS_sint64 bytesread;

      while ((bytesread = PHYSFS_readBytes(f, buffer, CRC32_BUFFERSIZE)) > 0) {
         PHYSFS_uint32 i, bit;
         for (i = 0; i < bytesread; i++) {
            for (bit = 0; bit < 8; bit++, buffer[i] >>= 1)
               crc = (crc >> 1) ^ (((crc ^ buffer[i]) & 1) ? 0xEDB88320 : 0);
         } /* for */
      } /* while */

      if (bytesread < 0) {
         printf("error while reading. Reason: [%s].\n",
            PHYSFS_getLastError());
         return 1;
      } /* if */

      PHYSFS_close(f);

      crc ^= -1;
      printf("CRC32 for %s: 0x%08X\n", args, crc);
   } /* else */

   return 1;
} /* cmd_crc32 */


static int cmd_filelength(char* args) {
   PHYSFS_File* f;

   if (*args == '\"') {
      args++;
      args[strlen(args) - 1] = '\0';
   } /* if */

   f = PHYSFS_openRead(args);
   if (f == nullptr)
      printf("failed to open. Reason: [%s].\n", PHYSFS_getLastError());
   else {
      PHYSFS_sint64 len = PHYSFS_fileLength(f);
      if (len == -1)
         printf("failed to determine length. Reason: [%s].\n", PHYSFS_getLastError());
      else
         printf(" (cast to int) %d bytes.\n", (int)len);

      PHYSFS_close(f);
   } /* else */

   return 1;
} /* cmd_filelength */

#define WRITESTR "The cat sat on the mat.\n\n"

static int cmd_append(char* args) {
   PHYSFS_File* f;

   if (*args == '\"') {
      args++;
      args[strlen(args) - 1] = '\0';
   } /* if */

   f = PHYSFS_openAppend(args);
   if (f == nullptr)
      printf("failed to open. Reason: [%s].\n", PHYSFS_getLastError());
   else {
      size_t bw;
      PHYSFS_sint64 rc;

      if (do_buffer_size) {
         if (!PHYSFS_setBuffer(f, do_buffer_size)) {
            printf("failed to set file buffer. Reason: [%s].\n",
               PHYSFS_getLastError());
            PHYSFS_close(f);
            return 1;
         } /* if */
      } /* if */

      bw = strlen(WRITESTR);
      rc = PHYSFS_writeBytes(f, WRITESTR, bw);
      if (rc != bw) {
         printf("Wrote (%d) of (%d) bytes. Reason: [%s].\n",
            (int)rc, (int)bw, PHYSFS_getLastError());
      } /* if */
      else {
         printf("Successful.\n");
      } /* else */

      PHYSFS_close(f);
   } /* else */

   return 1;
} /* cmd_append */


static int cmd_write(char* args) {
   PHYSFS_File* f;

   if (*args == '\"') {
      args++;
      args[strlen(args) - 1] = '\0';
   } /* if */

   f = PHYSFS_openWrite(args);
   if (f == nullptr)
      printf("failed to open. Reason: [%s].\n", PHYSFS_getLastError());
   else {
      size_t bw;
      PHYSFS_sint64 rc;

      if (do_buffer_size) {
         if (!PHYSFS_setBuffer(f, do_buffer_size)) {
            printf("failed to set file buffer. Reason: [%s].\n",
               PHYSFS_getLastError());
            PHYSFS_close(f);
            return 1;
         } /* if */
      } /* if */

      bw = strlen(WRITESTR);
      rc = PHYSFS_writeBytes(f, WRITESTR, bw);
      if (rc != bw) {
         printf("Wrote (%d) of (%d) bytes. Reason: [%s].\n",
            (int)rc, (int)bw, PHYSFS_getLastError());
      } /* if */
      else {
         printf("Successful.\n");
      } /* else */

      PHYSFS_close(f);
   } /* else */

   return 1;
} /* cmd_write */


static char* modTimeToStr(PHYSFS_sint64 modtime, char* modstr, size_t strsize) {
   if (modtime < 0)
      strncpy(modstr, "Unknown\n", strsize);
   else {
      time_t t = (time_t)modtime;
      char* str = ctime(&t);
      strncpy(modstr, str, strsize);
   } /* else */

   modstr[strsize - 1] = '\0';
   return modstr;
} /* modTimeToStr */


static int cmd_getlastmodtime(char* args) {
   PHYSFS_Stat statbuf;
   if (!PHYSFS_stat(args, &statbuf))
      printf("Failed to determine. Reason: [%s].\n", PHYSFS_getLastError());
   else {
      char modstr[64];
      modTimeToStr(statbuf.modtime, modstr, sizeof(modstr));
      printf("Last modified: %s (%ld).\n", modstr, (long)statbuf.modtime);
   } /* else */

   return 1;
} /* cmd_getLastModTime */

static int cmd_stat(char* args) {
   PHYSFS_Stat stat;
   char timestring[65];

   if (*args == '\"') {
      args++;
      args[strlen(args) - 1] = '\0';
   } /* if */

   if (!PHYSFS_stat(args, &stat)) {
      printf("failed to stat. Reason [%s].\n", PHYSFS_getLastError());
      return 1;
   } /* if */

   printf("Filename: %s\n", args);
   printf("Size %d\n", (int)stat.filesize);

   if (stat.filetype == PHYSFS_FILETYPE_REGULAR)
      printf("Type: File\n");
   else if (stat.filetype == PHYSFS_FILETYPE_DIRECTORY)
      printf("Type: Directory\n");
   else if (stat.filetype == PHYSFS_FILETYPE_SYMLINK)
      printf("Type: Symlink\n");
   else
      printf("Type: Unknown\n");

   printf("Created at: %s", modTimeToStr(stat.createtime, timestring, 64));
   printf("Last modified at: %s", modTimeToStr(stat.modtime, timestring, 64));
   printf("Last accessed at: %s", modTimeToStr(stat.accesstime, timestring, 64));
   printf("Readonly: %s\n", stat.readonly ? "true" : "false");

   return 1;
} /* cmd_filelength */



/* must have spaces trimmed prior to this call. */
static int count_args(const char* str) {
   int retval = 0;
   int in_quotes = 0;

   if (str != nullptr) {
      for (; *str != '\0'; str++) {
         if (*str == '\"')
            in_quotes = !in_quotes;
         else if ((*str == ' ') && (!in_quotes))
            retval++;
      } /* for */
      retval++;
   } /* if */

   return retval;
} /* count_args */


static int cmd_help(char* args);

typedef struct
{
   const char* cmd;
   int (*func)(char* args);
   int argcount;
   const char* usage;
} command_info;

static const command_info commands[] =
{
   {"quit", cmd_quit, 0, nullptr},
   {"q", cmd_quit, 0, nullptr},
   {"help", cmd_help, 0, nullptr},
   {"init", cmd_init, 1, "<argv0>"},
   {"deinit", cmd_deinit, 0, nullptr},
   {"addarchive", cmd_addarchive, 2, "<archiveLocation> <append>"},
   {"mount", cmd_mount, 3, "<archiveLocation> <mntpoint> <append>"},
   {"mountmem", cmd_mount_mem, 3, "<archiveLocation> <mntpoint> <append>"},
   {"mounthandle", cmd_mount_handle, 3, "<archiveLocation> <mntpoint> <append>"},
   {"removearchive", cmd_removearchive, 1, "<archiveLocation>"},
   {"unmount", cmd_removearchive, 1, "<archiveLocation>"},
   {"enumerate", cmd_enumerate, 1, "<dirToEnumerate>"},
   {"ls", cmd_enumerate, 1, "<dirToEnumerate>"},
   {"tree", cmd_tree, 1, "<dirToEnumerate>"},
   {"getlasterror", cmd_getlasterror, 0, nullptr},
   {"getdirsep", cmd_getdirsep, 0, nullptr},
   {"getcdromdirs", cmd_getcdromdirs, 0, nullptr},
   {"getsearchpath", cmd_getsearchpath, 0, nullptr},
   {"getbasedir", cmd_getbasedir, 0, nullptr},
   {"getuserdir", cmd_getuserdir, 0, nullptr},
   {"getprefdir", cmd_getprefdir, 2, "<org> <app>"},
   {"getwritedir", cmd_getwritedir, 0, nullptr},
   {"setwritedir", cmd_setwritedir, 1, "<newWriteDir>"},
   {"permitsymlinks", cmd_permitsyms, 1, "<1or0>"},
   {"setsaneconfig", cmd_setsaneconfig, 5, "<org> <appName> <arcExt> <includeCdRoms> <archivesFirst>"},
   {"mkdir", cmd_mkdir, 1, "<dirToMk>"},
   {"delete", cmd_delete, 1, "<dirToDelete>"},
   {"getrealdir", cmd_getrealdir, 1, "<fileToFind>"},
   {"exists", cmd_exists, 1, "<fileToCheck>"},
   {"isdir", cmd_isdir, 1, "<fileToCheck>"},
   {"issymlink", cmd_issymlink, 1, "<fileToCheck>"},
   {"cat", cmd_cat, 1, "<fileToCat>"},
   {"cat2", cmd_cat2, 2, "<fileToCat1> <fileToCat2>"},
   {"filelength", cmd_filelength, 1, "<fileToCheck>"},
   {"stat", cmd_stat, 1, "<fileToStat>"},
   {"append", cmd_append, 1, "<fileToAppend>"},
   {"write", cmd_write, 1, "<fileToCreateOrTrash>"},
   {"getlastmodtime", cmd_getlastmodtime, 1, "<fileToExamine>"},
   {"setbuffer", cmd_setbuffer, 1, "<bufferSize>"},
   {"stressbuffer", cmd_stressbuffer, 1, "<bufferSize>"},
   {"crc32", cmd_crc32, 1, "<fileToHash>"},
   {"getmountpoint", cmd_getmountpoint, 1, "<dir>"},
   {"setroot", cmd_setroot, 2, "<archiveLocation> <root>"},
   {nullptr, nullptr, -1, nullptr}
};


static void output_usage(const char* intro, const command_info* cmdinfo) {
   if (cmdinfo->argcount == 0)
      printf("%s \"%s\" (no arguments)\n", intro, cmdinfo->cmd);
   else
      printf("%s \"%s %s\"\n", intro, cmdinfo->cmd, cmdinfo->usage);
} /* output_usage */


static int cmd_help(char* args) {
   const command_info* i;

   printf("Commands:\n");
   for (i = commands; i->cmd != nullptr; i++)
      output_usage("  -", i);

   return 1;
} /* output_cmd_help */


static void trim_command(const char* orig, char* copy) {
   const char* i;
   char* writeptr = copy;
   int spacecount = 0;
   int have_first = 0;

   for (i = orig; *i != '\0'; i++) {
      if (*i == ' ') {
         if ((*(i + 1) != ' ') && (*(i + 1) != '\0')) {
            if ((have_first) && (!spacecount)) {
               spacecount++;
               *writeptr = ' ';
               writeptr++;
            } /* if */
         } /* if */
      } /* if */
      else {
         have_first = 1;
         spacecount = 0;
         *writeptr = *i;
         writeptr++;
      } /* else */
   } /* for */

   *writeptr = '\0';

   /*
   printf("\n command is [%s].\n", copy);
   */
} /* trim_command */


static int process_command(char* complete_cmd) {
   const command_info* i;
   char* cmd_copy;
   char* args;
   int rc = 1;

   if (complete_cmd == nullptr)  /* can happen if user hits CTRL-D, etc. */
   {
      printf("\n");
      return 0;
   } /* if */

   cmd_copy = (char*)malloc(strlen(complete_cmd) + 1);
   if (cmd_copy == nullptr) {
      printf("\n\n\nOUT OF MEMORY!\n\n\n");
      return 0;
   } /* if */

   trim_command(complete_cmd, cmd_copy);
   args = strchr(cmd_copy, ' ');
   if (args != nullptr) {
      *args = '\0';
      args++;
   } /* else */

   if (cmd_copy[0] != '\0') {
      for (i = commands; i->cmd != nullptr; i++) {
         if (strcmp(i->cmd, cmd_copy) == 0) {
            if ((i->argcount >= 0) && (count_args(args) != i->argcount))
               output_usage("usage:", i);
            else
               rc = i->func(args);
            break;
         } /* if */
      } /* for */

      if (i->cmd == nullptr)
         printf("Unknown command. Enter \"help\" for instructions.\n");

   #if (defined PHYSFS_HAVE_READLINE)
      add_history(complete_cmd);
      if (history_file) {
         fprintf(history_file, "%s\n", complete_cmd);
         fflush(history_file);
      } /* if */
   #endif

   } /* if */

   free(cmd_copy);
   return rc;
} /* process_command */


static void open_history_file(void) {
#if (defined PHYSFS_HAVE_READLINE)
#if 0
   const char* envr = getenv("TESTPHYSFS_HISTORY");
   if (!envr)
      return;
#else
   char envr[256];
   strcpy(envr, PHYSFS_getUserDir());
   strcat(envr, ".testphys_history");
#endif

   if (access(envr, F_OK) == 0) {
      char buf[512];
      FILE* f = fopen(envr, "r");
      if (!f) {
         printf("\n\n"
            "Could not open history file [%s] for reading!\n"
            "  Will not have past history available.\n\n",
            envr);
         return;
      } /* if */

      do {
         if (fgets(buf, sizeof(buf), f) == nullptr)
            break;

         if (buf[strlen(buf) - 1] == '\n')
            buf[strlen(buf) - 1] = '\0';
         add_history(buf);
      } while (!feof(f));

      fclose(f);
   } /* if */

   history_file = fopen(envr, "ab");
   if (!history_file) {
      printf("\n\n"
         "Could not open history file [%s] for appending!\n"
         "  Will not be able to record this session's history.\n\n",
         envr);
   } /* if */
#endif
} /* open_history_file */


int main(int argc, char** argv) {
   char* buf = nullptr;
   int rc = 1;

#if (defined __MWERKS__)
   extern tSIOUXSettings SIOUXSettings;
   SIOUXSettings.asktosaveonclose = 0;
   SIOUXSettings.autocloseonquit = 1;
   SIOUXSettings.rows = 40;
   SIOUXSettings.columns = 120;
#endif

   printf("\n");

   if (!PHYSFS_init(argv[0])) {
      printf("PHYSFS_init() failed!\n  reason: %s.\n", PHYSFS_getLastError());
      return 1;
   } /* if */

   output_versions();
   output_archivers();

   open_history_file();

   printf("Enter commands. Enter \"help\" for instructions.\n");
   fflush(stdout);

   for (auto i = 1; i < argc && rc; i++) {
      rc = process_command(argv[i]);
   }

   while (rc) {
   #if (defined PHYSFS_HAVE_READLINE)
      buf = readline("> ");
   #else
      int i;
      buf = (char*)malloc(512);
      memset(buf, '\0', 512);
      printf("> ");
      fflush(stdout);
      for (i = 0; i < 511; i++) {
         int ch = fgetc(stdin);
         if (ch == EOF) {
            strcpy(buf, "quit");
            break;
         } /* if */
         else if ((ch == '\n') || (ch == '\r')) {
            buf[i] = '\0';
            break;
         } /* else if */
         else if (ch == '\b') {
            if (i > 0)
               i--;
         } /* else if */
         else {
            buf[i] = (char)ch;
         } /* else */
      } /* for */
   #endif

      rc = process_command(buf);
      fflush(stdout);
      if (buf != nullptr)
         free(buf);
   }

   if (!PHYSFS_deinit())
      printf("PHYSFS_deinit() failed!\n  reason: %s.\n", PHYSFS_getLastError());

   if (history_file)
      fclose(history_file);

   /*
       printf("\n\ntest_physfs written by ryan c. gordon.\n");
       printf(" it makes you shoot teh railgun bettar.\n");
   */

   return 0;
} /* main */

/* end of test_physfs.c ... */

