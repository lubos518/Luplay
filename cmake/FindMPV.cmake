# FindMPV.cmake
# Locate libmpv library and headers

find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(PC_MPV QUIET mpv)
endif()

find_path(MPV_INCLUDE_DIR
    NAMES mpv/client.h
    HINTS ${PC_MPV_INCLUDEDIR} ${PC_MPV_INCLUDE_DIRS}
    PATHS
        "${CMAKE_SOURCE_DIR}/libmpv/include"
        /usr/include
        /usr/local/include
        /opt/homebrew/include
)

set(SAVED_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
set(CMAKE_FIND_LIBRARY_SUFFIXES .lib .dll.a .a ${CMAKE_FIND_LIBRARY_SUFFIXES})

find_library(MPV_LIBRARY
    NAMES mpv libmpv libmpv-2 mpv-2 libmpv.dll.a
    HINTS ${PC_MPV_LIBDIR} ${PC_MPV_LIBRARY_DIRS}
    PATHS
        "${CMAKE_SOURCE_DIR}/libmpv"
        /usr/lib
        /usr/local/lib
        /usr/lib/x86_64-linux-gnu
        /opt/homebrew/lib
)

set(CMAKE_FIND_LIBRARY_SUFFIXES ${SAVED_CMAKE_FIND_LIBRARY_SUFFIXES})

if (NOT MPV_LIBRARY AND EXISTS "${CMAKE_SOURCE_DIR}/libmpv/libmpv.dll.a")
    set(MPV_LIBRARY "${CMAKE_SOURCE_DIR}/libmpv/libmpv.dll.a" CACHE FILEPATH "Path to libmpv library")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MPV
    REQUIRED_VARS MPV_LIBRARY MPV_INCLUDE_DIR
    VERSION_VAR PC_MPV_VERSION
)

if (MPV_FOUND)
    set(MPV_LIBRARIES ${MPV_LIBRARY})
    set(MPV_INCLUDE_DIRS ${MPV_INCLUDE_DIR})

    if (NOT TARGET mpv::mpv)
        add_library(mpv::mpv UNKNOWN IMPORTED)
        set_target_properties(mpv::mpv PROPERTIES
            IMPORTED_LOCATION "${MPV_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${MPV_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(MPV_INCLUDE_DIR MPV_LIBRARY)
