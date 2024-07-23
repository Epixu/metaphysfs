# PhysicsFS; a portable, flexible file i/o abstraction.
#
# Please see the file LICENSE.txt in the source's root directory.

# The CMake project file is meant to get this compiling on all sorts of
#  platforms quickly, and serve as the way Unix platforms and Linux distros
#  package up official builds, but you don't _need_ to use this; we have
#  built PhysicsFS to (hopefully) be able to drop into your project and
#  compile, using preprocessor checks for platform-specific bits instead of
#  testing in here.
cmake_minimum_required(VERSION 3.28)
include(GNUInstallDirs)

set(PHYSFS_VERSION 3.3.0)
# Increment this if/when we break backwards compatibility						
set(PHYSFS_SOVERSION 1)

project(MetaPhysicsFS
    DESCRIPTION "MetaPhysicsFS; a portable, flexible file i/o abstraction built on top of PhysicsFS"
    HOMEPAGE_URL "https://github.com/Epixu/metaphysfs"
    VERSION ${PHYSFS_VERSION}
)

# Public options                                                                
option(PHYSFS_BUILD_STATIC          "Build a static library"						TRUE)
option(PHYSFS_BUILD_SHARED          "Build a shared library"						TRUE)
option(PHYSFS_BUILD_TEST            "Build test"						            TRUE)

option(METAPHYSFS_ARCHIVE_ZIP       "Enable ZIP support"							TRUE)
option(METAPHYSFS_ARCHIVE_7Z        "Enable 7zip support"							TRUE)
option(METAPHYSFS_ARCHIVE_GRP       "Enable Build Engine GRP support"				TRUE)
option(METAPHYSFS_ARCHIVE_WAD       "Enable Doom WAD support"						TRUE)
option(METAPHYSFS_ARCHIVE_CSM       "Enable Chasm: The Rift CSM.BIN support"		TRUE)
option(METAPHYSFS_ARCHIVE_HOG       "Enable Descent I/II HOG support"				TRUE)
option(METAPHYSFS_ARCHIVE_MVL       "Enable Descent I/II MVL support"				TRUE)
option(METAPHYSFS_ARCHIVE_QPAK      "Enable Quake I/II QPAK support"				TRUE)
option(METAPHYSFS_ARCHIVE_SLB       "Enable I-War / Independence War SLB support"	TRUE)
option(METAPHYSFS_ARCHIVE_ISO9660   "Enable ISO9660 support"						TRUE)
option(METAPHYSFS_ARCHIVE_VDF       "Enable Gothic I/II VDF archive support"		TRUE)
option(METAPHYSFS_BUILD_STATIC      "Build static library"							TRUE)
option(METAPHYSFS_BUILD_SHARED      "Build shared library"							TRUE)
option(METAPHYSFS_BUILD_TEST        "Build stdio test program."						TRUE)
option(METAPHYSFS_DISABLE_INSTALL   "Disable installing MetaPhysFS"					OFF)
option(METAPHYSFS_BUILD_DOCS        "Build doxygen based documentation"				TRUE)

message("Compiler: ${CMAKE_CXX_COMPILER_ID} - ${CMAKE_CXX_COMPILER_VERSION}")

# Create a common target that is shared between static/shared builds			
add_library(MetaPhysicsFSCommon INTERFACE)
target_compile_features(MetaPhysicsFSCommon INTERFACE cxx_std_23)
target_include_directories(MetaPhysicsFSCommon INTERFACE  
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_options(MetaPhysicsFSCommon INTERFACE
	$<$<CXX_COMPILER_ID:MSVC>:    /W4 /WX>
	$<$<CXX_COMPILER_ID:Clang>:   -Wall -Wextra -Wpedantic -Werror>
	$<$<CXX_COMPILER_ID:GNU>:     -Wall -Wextra -Wpedantic -Werror>
	$<$<CXX_COMPILER_ID:SunPro>:  -xldscope=hidden>
)
list(APPEND PHYSFS_INSTALL_TARGETS "MetaPhysicsFSCommon")

# Pick sources and libraries depending on platform								
set(PHYSFS_M_SRCS)
set(PHYSFS_CPP_SRCS)

if(WIN32)
	target_link_libraries(MetaPhysicsFSCommon INTERFACE advapi32 shell32)
endif()

if(APPLE)
    set(OTHER_LDFLAGS ${OTHER_LDFLAGS} "-framework IOKit -framework Foundation")
	target_link_libraries(MetaPhysicsFSCommon INTERFACE ${OTHER_LDFLAGS})
    list(APPEND PHYSFS_M_SRCS src/physfs_platform_apple.m)
endif()

if(HAIKU)
    list(APPEND PHYSFS_CPP_SRCS src/physfs_platform_haiku.cpp)
    find_library(BE_LIBRARY be)
    find_library(ROOT_LIBRARY root)
	target_link_libraries(MetaPhysicsFSCommon INTERFACE ${BE_LIBRARY} ${ROOT_LIBRARY})
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
		target_link_libraries(MetaPhysicsFSCommon INTERFACE ${PTHREAD_LIBRARY})
    endif()
endif()

# Almost everything is "compiled" here, but things that don't apply to the
#  build are #ifdef'd out. This is to make it easy to embed PhysicsFS into
#  another project or bring up a new build system: just compile all the source
#  code and #define the things you want.
set(PHYSFS_SRCS
    src/physfs.cpp
    src/physfs_unicode.cpp
    src/physfs_platform_posix.cpp
    src/physfs_platform_unix.cpp
    src/physfs_platform_windows.cpp
    src/physfs_platform_ogc.cpp
    src/physfs_platform_os2.cpp
    src/physfs_platform_qnx.cpp
    src/physfs_platform_android.cpp
    src/physfs_platform_playdate.cpp
    src/physfs_archiver_dir.cpp
    src/physfs_archiver_unpacked.cpp
	$<$<BOOL:${METAPHYSFS_ARCHIVE_GRP}>:src/physfs_archiver_grp.cpp>
	$<$<BOOL:${METAPHYSFS_ARCHIVE_HOG}>:src/physfs_archiver_hog.cpp>
	$<$<BOOL:${METAPHYSFS_ARCHIVE_7Z}>:src/physfs_archiver_7z.cpp>
	$<$<BOOL:${METAPHYSFS_ARCHIVE_MVL}>:src/physfs_archiver_mvl.cpp>
	$<$<BOOL:${METAPHYSFS_ARCHIVE_QPAK}>:src/physfs_archiver_qpak.cpp>
	$<$<BOOL:${METAPHYSFS_ARCHIVE_WAD}>:src/physfs_archiver_wad.cpp>
	$<$<BOOL:${METAPHYSFS_ARCHIVE_CSM}>:src/physfs_archiver_csm.cpp>
	$<$<BOOL:${METAPHYSFS_ARCHIVE_ZIP}>:src/physfs_archiver_zip.cpp>
	$<$<BOOL:${METAPHYSFS_ARCHIVE_SLB}>:src/physfs_archiver_slb.cpp>
	$<$<BOOL:${METAPHYSFS_ARCHIVE_ISO9660}>:src/physfs_archiver_iso9660.cpp>
	$<$<BOOL:${METAPHYSFS_ARCHIVE_VDF}>:src/physfs_archiver_vdf.cpp>
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

	target_link_libraries(physfs-static PUBLIC MetaPhysicsFSCommon)

    set(PHYSFS_LIB_TARGET PhysFS::PhysFS-static)
    list(APPEND PHYSFS_INSTALL_TARGETS "physfs-static")
endif()

if(PHYSFS_BUILD_SHARED)
    add_library(physfs SHARED ${PHYSFS_SRCS})
    add_library(PhysFS::PhysFS ALIAS physfs)
    set_target_properties(physfs PROPERTIES MACOSX_RPATH 1)
    set_target_properties(physfs PROPERTIES VERSION      ${PHYSFS_VERSION})
    set_target_properties(physfs PROPERTIES SOVERSION    ${PHYSFS_SOVERSION})
    set_target_properties(physfs PROPERTIES EXPORT_NAME  PhysFS)
    if(WINRT)
		set_target_properties(physfs PROPERTIES VS_WINRT_COMPONENT TRUE)
    endif()
    if(OS2) # OS/2 does not support a DLL name longer than 8 characters.
        set_target_properties(physfs PROPERTIES OUTPUT_NAME "physfs")
    endif()

	target_link_libraries(physfs PUBLIC MetaPhysicsFSCommon)

    set(PHYSFS_LIB_TARGET PhysFS::PhysFS)
    list(APPEND PHYSFS_INSTALL_TARGETS "physfs")
endif()

if(NOT PHYSFS_BUILD_SHARED AND NOT PHYSFS_BUILD_STATIC)
    message(FATAL "Both shared and static libraries are disabled!")
endif()

# Toggle testing                                                                
if(PHYSFS_BUILD_TEST)
    message(STATUS "MetaPhysicsFS will build tests")
    enable_testing()
    add_subdirectory(test)
	list(APPEND PHYSFS_INSTALL_TARGETS test_physfs)
endif()

# Install																		
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

# Reflect enabled archivers                                                     
macro(reflect_option OPTION MESSAGE)
	if (${OPTION})
		message(STATUS "[FEATURE] ${MESSAGE} enabled (${OPTION})")
		target_compile_definitions(MetaPhysicsFSCommon INTERFACE "$<$<BOOL:${OPTION}>:${OPTION}>")
	endif()
endmacro()

message(STATUS "MetaPhysicsFS will build with the following options:")
reflect_option(METAPHYSFS_ARCHIVE_ZIP		"ZIP"        )
reflect_option(METAPHYSFS_ARCHIVE_7Z		"7zip"       )
reflect_option(METAPHYSFS_ARCHIVE_GRP		"GRP"        )
reflect_option(METAPHYSFS_ARCHIVE_WAD		"WAD"        )
reflect_option(METAPHYSFS_ARCHIVE_CSM		"CSM"        )
reflect_option(METAPHYSFS_ARCHIVE_HOG		"HOG"        )
reflect_option(METAPHYSFS_ARCHIVE_MVL		"MVL"        )
reflect_option(METAPHYSFS_ARCHIVE_QPAK		"QPAK"       )
reflect_option(METAPHYSFS_ARCHIVE_SLB		"SLB"        )
reflect_option(METAPHYSFS_ARCHIVE_VDF		"VDF"        )
reflect_option(METAPHYSFS_ARCHIVE_ISO9660	"ISO9660"    )

# Generate documentation                                                        
if(PHYSFS_BUILD_DOCS)
    find_package(Doxygen)
    add_subdirectory(docs)
endif()