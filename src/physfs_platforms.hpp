#pragma once

/// Sorry, MetaPhysFS is designed for at least C++20                          
#if __cplusplus <= 201703L and not defined(_MSC_VER)
   #error metaphysfs requires at least a C++20 compliant compiler in order to build
#endif

/// These only define the platforms to determine which files in the platforms 
/// directory should be compiled. For example, technically BeOS can be called 
/// a "unix" system, but since it doesn't use unix.c, we don't define         
/// PHYSFS_PLATFORM_UNIX on that system.                                      

#if defined(TARGET_EXTENSION) and (defined(TARGET_PLAYDATE) or defined(TARGET_SIMULATOR))
   #define PHYSFS_PLATFORM_PLAYDATE 1
   #define PHYSFS_NO_CRUNTIME_MALLOC 1
#elif defined(__HAIKU__)
   #define PHYSFS_PLATFORM_HAIKU 1
   #define PHYSFS_PLATFORM_POSIX 1
#elif defined(__BEOS__) or defined(__beos__)
   #error BeOS support was dropped since PhysicsFS 2.1. Sorry. Try Haiku!
#elif defined(_WIN32_WCE) or defined(_WIN64_WCE)
   #error PocketPC support was dropped since PhysicsFS 2.1. Sorry. Try WinRT!
#elif defined(_MSC_VER) and (_MSC_VER >= 1700) and not _USING_V110_SDK71_	// _MSC_VER==1700 for MSVC 2012
   #include <winapifamily.h>
   #define PHYSFS_PLATFORM_WINDOWS 1
   #if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP) and not WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
      #define PHYSFS_NO_CDROM_SUPPORT 1
      #define PHYSFS_PLATFORM_WINRT 1
   #endif
#elif (defined(_WIN32) or defined(_WIN64)) and not defined(__CYGWIN__)
   #define PHYSFS_PLATFORM_WINDOWS 1
#elif defined(__OS2__) or defined(OS2)
   #define PHYSFS_PLATFORM_OS2 1
#elif defined(__MACH__) and defined(__APPLE__)
   // To check if iOS or not, we need to include this file                 
   #include <TargetConditionals.h>
   #if TARGET_IPHONE_SIMULATOR or TARGET_OS_IPHONE
      #define PHYSFS_NO_CDROM_SUPPORT 1
   #endif
   #define PHYSFS_PLATFORM_APPLE 1
   #define PHYSFS_PLATFORM_POSIX 1
#elif defined(macintosh)
   #error Classic Mac OS support was dropped from PhysicsFS 2.0. Move to OS X.
#elif defined(__ANDROID__)
   #define PHYSFS_PLATFORM_LINUX 1
   #define PHYSFS_PLATFORM_ANDROID 1
   #define PHYSFS_PLATFORM_POSIX 1
   #define PHYSFS_NO_CDROM_SUPPORT 1
#elif defined(__linux)
   #define PHYSFS_PLATFORM_LINUX 1
   #define PHYSFS_PLATFORM_UNIX 1
   #define PHYSFS_PLATFORM_POSIX 1
#elif defined(__sun) or defined(sun)
   #define PHYSFS_PLATFORM_SOLARIS 1
   #define PHYSFS_PLATFORM_UNIX 1
   #define PHYSFS_PLATFORM_POSIX 1
#elif defined(__FreeBSD__) or defined(__FreeBSD_kernel__) or defined(__DragonFly__)
   #define PHYSFS_PLATFORM_FREEBSD 1
   #define PHYSFS_PLATFORM_BSD 1
   #define PHYSFS_PLATFORM_UNIX 1
   #define PHYSFS_PLATFORM_POSIX 1
#elif defined(__NetBSD__) or defined(__OpenBSD__) or defined(__bsdi__)
   #define PHYSFS_PLATFORM_BSD 1
   #define PHYSFS_PLATFORM_UNIX 1
   #define PHYSFS_PLATFORM_POSIX 1
#elif defined(__EMSCRIPTEN__)
   #define PHYSFS_NO_CDROM_SUPPORT 1
   #define PHYSFS_PLATFORM_UNIX 1
   #define PHYSFS_PLATFORM_POSIX 1
#elif defined(__QNX__)
   #define PHYSFS_PLATFORM_QNX 1
   #define PHYSFS_PLATFORM_POSIX 1
#elif defined(unix) or defined(__unix__)
   #define PHYSFS_PLATFORM_UNIX 1
   #define PHYSFS_PLATFORM_POSIX 1
#elif defined(__wii__) or defined(__gamecube__)
   #define PHYSFS_PLATFORM_OGC 1
   #define PHYSFS_NO_CDROM_SUPPORT 1 // TODO
#else
   #error Unknown platform.
#endif