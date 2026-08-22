# FindFFmpeg.cmake
# Locate FFmpeg components: avformat, avcodec, swresample, avutil

find_package(PkgConfig QUIET)

# Check for vcpkg installed FFmpeg directly
if (EXISTS "A:/vcpkg/installed/x64-windows/include/libavcodec/avcodec.h")
    set(FFMPEG_INCLUDE_DIRS "A:/vcpkg/installed/x64-windows/include")
    set(FFMPEG_LIBRARIES 
        "A:/vcpkg/installed/x64-windows/lib/avformat.lib"
        "A:/vcpkg/installed/x64-windows/lib/avcodec.lib"
        "A:/vcpkg/installed/x64-windows/lib/swresample.lib"
        "A:/vcpkg/installed/x64-windows/lib/avutil.lib"
    )
    set(FFMPEG_FOUND TRUE)
    
    foreach(COMP avformat avcodec swresample avutil)
        if (NOT TARGET FFmpeg::${COMP})
            add_library(FFmpeg::${COMP} UNKNOWN IMPORTED)
            set_target_properties(FFmpeg::${COMP} PROPERTIES
                IMPORTED_LOCATION "A:/vcpkg/installed/x64-windows/lib/${COMP}.lib"
                INTERFACE_INCLUDE_DIRECTORIES "A:/vcpkg/installed/x64-windows/include"
            )
        endif()
    endforeach()
    return()
endif()

set(FFMPEG_COMPONENTS avformat avcodec swresample avutil)
set(FFMPEG_FOUND TRUE)
set(FFMPEG_LIBRARIES "")
set(FFMPEG_INCLUDE_DIRS "")

foreach(COMPONENT ${FFMPEG_COMPONENTS})
    string(TOUPPER ${COMPONENT} COMPONENT_UPPER)
    
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(PC_FFMPEG_${COMPONENT_UPPER} QUIET lib${COMPONENT})
    endif()

    find_path(FFMPEG_${COMPONENT_UPPER}_INCLUDE_DIR
        NAMES lib${COMPONENT}/${COMPONENT}.h
        HINTS ${PC_FFMPEG_${COMPONENT_UPPER}_INCLUDEDIR} ${PC_FFMPEG_${COMPONENT_UPPER}_INCLUDE_DIRS}
        PATHS 
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
            "${CMAKE_PREFIX_PATH}/include"
            "A:/vcpkg/installed/x64-windows/include"
            /usr/include 
            /usr/local/include 
            /opt/homebrew/include
    )

    find_library(FFMPEG_${COMPONENT_UPPER}_LIBRARY
        NAMES ${COMPONENT} lib${COMPONENT}
        HINTS ${PC_FFMPEG_${COMPONENT_UPPER}_LIBDIR} ${PC_FFMPEG_${COMPONENT_UPPER}_LIBRARY_DIRS}
        PATHS 
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib"
            "${CMAKE_PREFIX_PATH}/lib"
            "A:/vcpkg/installed/x64-windows/lib"
            /usr/lib 
            /usr/local/lib 
            /usr/lib/x86_64-linux-gnu 
            /opt/homebrew/lib
    )

    if (FFMPEG_${COMPONENT_UPPER}_INCLUDE_DIR AND FFMPEG_${COMPONENT_UPPER}_LIBRARY)
        list(APPEND FFMPEG_LIBRARIES ${FFMPEG_${COMPONENT_UPPER}_LIBRARY})
        list(APPEND FFMPEG_INCLUDE_DIRS ${FFMPEG_${COMPONENT_UPPER}_INCLUDE_DIR})

        if (NOT TARGET FFmpeg::${COMPONENT})
            add_library(FFmpeg::${COMPONENT} UNKNOWN IMPORTED)
            set_target_properties(FFmpeg::${COMPONENT} PROPERTIES
                IMPORTED_LOCATION "${FFMPEG_${COMPONENT_UPPER}_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_${COMPONENT_UPPER}_INCLUDE_DIR}"
            )
        endif()
    else()
        set(FFMPEG_FOUND FALSE)
    endif()

    mark_as_advanced(FFMPEG_${COMPONENT_UPPER}_INCLUDE_DIR FFMPEG_${COMPONENT_UPPER}_LIBRARY)
endforeach()

if (FFMPEG_INCLUDE_DIRS)
    list(REMOVE_DUPLICATES FFMPEG_INCLUDE_DIRS)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS FFMPEG_LIBRARIES FFMPEG_INCLUDE_DIRS
    HANDLE_COMPONENTS
)
