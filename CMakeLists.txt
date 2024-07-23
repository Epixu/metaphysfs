# PhysicsFS; a portable, flexible file i/o abstraction.
#
# Please see the file LICENSE.txt in the source's root directory.

# The CMake project file is meant to get this compiling on all sorts of
#  platforms quickly, and serve as the way Unix platforms and Linux distros
#  package up official builds, but you don't _need_ to use this; we have
#  built PhysicsFS to (hopefully) be able to drop into your project and
#  compile, using preprocessor checks for platform-specific bits instead of
#  testing in here.
cmake_minimum_required(VERSION 3.24)

project(MetaPhysicsFS
    DESCRIPTION "MetaPhysicsFS; a portable, flexible file i/o abstraction built on top of PhysicsFS"
    HOMEPAGE_URL "https://github.com/Epixu/metaphysfs"
    VERSION 1.0.0
    LANGUAGES CXX
)

# Public options                                                                
option(METAPHYSFS_ARCHIVE_ZIP       "Enable ZIP support" TRUE)
option(METAPHYSFS_ARCHIVE_7Z        "Enable 7zip support" TRUE)
option(METAPHYSFS_ARCHIVE_GRP       "Enable Build Engine GRP support" TRUE)
option(METAPHYSFS_ARCHIVE_WAD       "Enable Doom WAD support" TRUE)
option(METAPHYSFS_ARCHIVE_CSM       "Enable Chasm: The Rift CSM.BIN support" TRUE)
option(METAPHYSFS_ARCHIVE_HOG       "Enable Descent I/II HOG support" TRUE)
option(METAPHYSFS_ARCHIVE_MVL       "Enable Descent I/II MVL support" TRUE)
option(METAPHYSFS_ARCHIVE_QPAK      "Enable Quake I/II QPAK support" TRUE)
option(METAPHYSFS_ARCHIVE_SLB       "Enable I-War / Independence War SLB support" TRUE)
option(METAPHYSFS_ARCHIVE_ISO9660   "Enable ISO9660 support" TRUE)
option(METAPHYSFS_ARCHIVE_VDF       "Enable Gothic I/II VDF archive support" TRUE)
option(METAPHYSFS_BUILD_STATIC      "Build static library" TRUE)
option(METAPHYSFS_BUILD_SHARED      "Build shared library" TRUE)
option(METAPHYSFS_BUILD_TEST        "Build stdio test program." TRUE)
option(METAPHYSFS_DISABLE_INSTALL   "Disable installing MetaPhysFS" OFF)
option(METAPHYSFS_BUILD_DOCS        "Build doxygen based documentation" TRUE)


message("Compiler: ${CMAKE_CXX_COMPILER_ID} - ${CMAKE_CXX_COMPILER_VERSION}")

include(GNUInstallDirs)


set(PHYSFS_M_SRCS)
set(PHYSFS_CPP_SRCS)

if(WIN32)
    list(APPEND OPTIONAL_LIBRARY_LIBS advapi32 shell32)
endif()

if(APPLE)
    set(OTHER_LDFLAGS ${OTHER_LDFLAGS} "-framework IOKit -framework Foundation")
    list(APPEND PHYSFS_M_SRCS src/physfs_platform_apple.m)
endif()

if(HAIKU)
    # We add this explicitly, since we don't want CMake to think this
    #  is a C++ project unless we're on Haiku.
    list(APPEND PHYSFS_CPP_SRCS src/physfs_platform_haiku.cpp)
    find_library(BE_LIBRARY be)
    find_library(ROOT_LIBRARY root)
    list(APPEND OPTIONAL_LIBRARY_LIBS ${BE_LIBRARY} ${ROOT_LIBRARY})
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "WindowsPhone" OR CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    set(WINRT TRUE)
endif()

if(WINRT)
    list(APPEND PHYSFS_CPP_SRCS src/physfs_platform_winrt.cpp)
endif()

if(UNIX AND NOT WIN32 AND NOT APPLE)  # (MingW and such might be UNIX _and_ WINDOWS!)
    find_library(PTHREAD_LIBRARY pthread)
    if(PTHREAD_LIBRARY)
        list(APPEND OPTIONAL_LIBRARY_LIBS ${PTHREAD_LIBRARY})
    endif()
endif()

# Almost everything is "compiled" here, but things that don't apply to the
#  build are #ifdef'd out. This is to make it easy to embed PhysicsFS into
#  another project or bring up a new build system: just compile all the source
#  code and #define the things you want.
set(PHYSFS_SRCS
    src/physfs.c
    src/physfs_byteorder.c
    src/physfs_unicode.c
    src/physfs_platform_posix.c
    src/physfs_platform_unix.c
    src/physfs_platform_windows.c
    src/physfs_platform_ogc.c
    src/physfs_platform_os2.c
    src/physfs_platform_qnx.c
    src/physfs_platform_android.c
    src/physfs_platform_playdate.c
    src/physfs_archiver_dir.c
    src/physfs_archiver_unpacked.c
    src/physfs_archiver_grp.c
    src/physfs_archiver_hog.c
    src/physfs_archiver_7z.c
    src/physfs_archiver_mvl.c
    src/physfs_archiver_qpak.c
    src/physfs_archiver_wad.c
    src/physfs_archiver_csm.c
    src/physfs_archiver_zip.c
    src/physfs_archiver_slb.c
    src/physfs_archiver_iso9660.c
    src/physfs_archiver_vdf.c
    ${PHYSFS_CPP_SRCS}
    ${PHYSFS_M_SRCS}
)

if(PHYSFS_BUILD_STATIC)
    add_library(physfs-static STATIC ${PHYSFS_SRCS})
    add_library(PhysFS::PhysFS-static ALIAS physfs-static)
    set_target_properties(physfs-static PROPERTIES EXPORT_NAME PhysFS-static)
    # Don't rename this on Windows, since DLLs will also produce an import
    #  library named "physfs.lib" which would conflict; Unix tend to like the
    #  same library name with a different extension for static libs, but
    #  Windows can just have a separate name.
    if(NOT MSVC)
        set_target_properties(physfs-static PROPERTIES OUTPUT_NAME "physfs")
    endif()
    if(WINRT)
        # Ignore LNK4264 warnings; we don't author any WinRT components, just consume them, so we're okay in a static library.
		set_target_properties(physfs-static PROPERTIES VS_WINRT_COMPONENT True)
        set_target_properties(physfs-static PROPERTIES STATIC_LIBRARY_FLAGS "/ignore:4264")
    endif()
    if(WIN32 OR WINRT OR OS2)
        # no dll exports from the static library
        target_compile_definitions(physfs-static PRIVATE "PHYSFS_STATIC")
    endif()
    target_include_directories(physfs-static PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>")
    target_link_libraries(physfs-static PRIVATE ${OPTIONAL_LIBRARY_LIBS} ${OTHER_LDFLAGS})
    set(PHYSFS_LIB_TARGET PhysFS::PhysFS-static)
    list(APPEND PHYSFS_INSTALL_TARGETS "physfs-static")
endif()

if(PHYSFS_BUILD_SHARED)
    add_library(physfs SHARED ${PHYSFS_SRCS})
    add_library(PhysFS::PhysFS ALIAS physfs)
    set_target_properties(physfs PROPERTIES MACOSX_RPATH 1)
    set_target_properties(physfs PROPERTIES VERSION ${PHYSFS_VERSION})
    set_target_properties(physfs PROPERTIES SOVERSION ${PHYSFS_SOVERSION})
    set_target_properties(physfs PROPERTIES EXPORT_NAME PhysFS)
    if(WINRT)
		set_target_properties(physfs PROPERTIES VS_WINRT_COMPONENT TRUE)
    endif()
    if(OS2) # OS/2 does not support a DLL name longer than 8 characters.
        set_target_properties(physfs PROPERTIES OUTPUT_NAME "physfs")
    endif()
    target_include_directories(physfs PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>")
    target_link_libraries(physfs PRIVATE ${OPTIONAL_LIBRARY_LIBS} ${OTHER_LDFLAGS})
    set(PHYSFS_LIB_TARGET PhysFS::PhysFS)
    list(APPEND PHYSFS_INSTALL_TARGETS "physfs")
endif()

if(NOT PHYSFS_BUILD_SHARED AND NOT PHYSFS_BUILD_STATIC)
    message(FATAL "Both shared and static libraries are disabled!")
endif()

mark_as_advanced(PHYSFS_BUILD_TEST)
if(PHYSFS_BUILD_TEST)
    add_executable(test_physfs test/test_physfs.c)
    target_link_libraries(test_physfs PRIVATE ${PHYSFS_LIB_TARGET} ${OTHER_LDFLAGS})

    find_path(READLINE_H readline/readline.h)
    find_path(HISTORY_H readline/history.h)
    find_library(READLINE_LIBRARY readline)
    find_package(Curses)
    if(READLINE_H AND HISTORY_H AND READLINE_LIBRARY AND CURSES_FOUND)
        set(HAVE_SYSTEM_READLINE TRUE)
        target_link_libraries(test_physfs PRIVATE ${READLINE_LIBRARY} ${CURSES_LIBRARIES})
        target_include_directories(test_physfs SYSTEM PRIVATE ${READLINE_H} ${HISTORY_H})
        target_compile_definitions(test_physfs PRIVATE PHYSFS_HAVE_READLINE=1)
    endif()
    list(APPEND PHYSFS_INSTALL_TARGETS test_physfs)
endif()

if(NOT PHYSFS_DISABLE_INSTALL)

    install(TARGETS ${PHYSFS_INSTALL_TARGETS} EXPORT PhysFSExport
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES src/physfs.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(EXPORT PhysFSExport
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/PhysFS"
            FILE PhysFSConfig.cmake
            NAMESPACE PhysFS::
    )

    if(NOT MSVC)
        configure_file(
            "extras/physfs.pc.in"
            "extras/physfs.pc"
            @ONLY
        )

        install(
            FILES "${CMAKE_CURRENT_BINARY_DIR}/extras/physfs.pc"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
        )
    endif()
endif()


if(UNIX)
    set(PHYSFS_TARBALL "${CMAKE_CURRENT_SOURCE_DIR}/../physfs-${PHYSFS_VERSION}.tar.gz")

    set(PHYSFS_TARGETNAME_DIST "dist" CACHE STRING "Name of 'dist' build target")
    add_custom_target(
        ${PHYSFS_TARGETNAME_DIST}
        git archive --prefix="physfs-${PHYSFS_VERSION}/" --output="${PHYSFS_TARBALL}" HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Building source tarball '${PHYSFS_TARBALL}'..."
    )

    set(PHYSFS_TARGETNAME_UNINSTALL "uninstall" CACHE STRING "Name of 'uninstall' build target")
    add_custom_target(
        ${PHYSFS_TARGETNAME_UNINSTALL}
        "${CMAKE_CURRENT_SOURCE_DIR}/extras/uninstall.sh"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Uninstall the project..."
    )
endif()

# Toggle compiler options                                                       
target_compile_options(MetaPhysicsFS PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:    /W4 /WX>
  $<$<CXX_COMPILER_ID:Clang>:   -Wall -Wextra -Wpedantic -Werror>
  $<$<CXX_COMPILER_ID:GNU>:     -Wall -Wextra -Wpedantic -Werror>
  $<$<CXX_COMPILER_ID:SunPro>:  -xldscope=hidden>
)

# Reflect enabled archivers                                                     
macro(reflect_option FORMAT VALUE)
    if(${VALUE})
        target_compile_definitions(MetaPhysFS PRIVATE "METAPHYSFS_SUPPORTS_${FORMAT}=1")
        message(STATUS "  ${FORMAT} support: enabled")
    else()
        target_compile_definitions(MetaPhysFS PRIVATE "METAPHYSFS_SUPPORTS_${FORMAT}=0")
        message(STATUS "  ${FORMAT} support: disabled")
    endif()
endmacro()

message(STATUS "MetaPhysicsFS will build with the following options:")
reflect_option("ZIP"        METAPHYSFS_ARCHIVE_ZIP)
reflect_option("7zip"       METAPHYSFS_ARCHIVE_7Z)
reflect_option("GRP"        METAPHYSFS_ARCHIVE_GRP)
reflect_option("WAD"        METAPHYSFS_ARCHIVE_WAD)
reflect_option("CSM"        METAPHYSFS_ARCHIVE_CSM)
reflect_option("HOG"        METAPHYSFS_ARCHIVE_HOG)
reflect_option("MVL"        METAPHYSFS_ARCHIVE_MVL)
reflect_option("QPAK"       METAPHYSFS_ARCHIVE_QPAK)
reflect_option("SLB"        METAPHYSFS_ARCHIVE_SLB)
reflect_option("VDF"        METAPHYSFS_ARCHIVE_VDF)
reflect_option("ISO9660"    METAPHYSFS_ARCHIVE_ISO9660)

# Toggle testing                                                                
if(METAPHYSFS_ENABLE_TESTING)
    message(STATUS "MetaPhysicsFS will build tests")
    enable_testing()
    add_subdirectory(test)
endif()

# Generate documentation                                                        
if(PHYSFS_BUILD_DOCS)
    find_package(Doxygen)
    add_subdirectory(docs)
endif()