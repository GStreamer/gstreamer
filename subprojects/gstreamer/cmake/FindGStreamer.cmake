# SPDX-FileCopyrightText: 2024 L. E. Segovia <amy@centricular.com>
# SPDX-License-Ref: LGPL-2.1-or-later

#[=======================================================================[.rst:
FindGStreamer
-------

Finds the GStreamer library. Requires ``pkg-config`` to be installed.

Configuration
^^^^^^^^^^^^^

This module can be configured with the following variables:

``GStreamer_STATIC``
  Link against GStreamer statically (see below).

Imported Targets
^^^^^^^^^^^^^^^^

This module defines the following :prop_tgt:`IMPORTED` targets:

``GStreamer::GStreamer``
  The GStreamer library.

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``GStreamer_FOUND``
  True if the system has the GStreamer library.
``GStreamer_VERSION``
  The version of the GStreamer library which was found.
``GStreamer_INCLUDE_DIRS``
  Include directories needed to use GStreamer.
``GStreamer_LIBRARIES``
  Libraries needed to link to GStreamer.

Cache Variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``GStreamer_INCLUDE_DIR``
  The directory containing ``gst/gstversion.h``.
``GStreamer_LIBRARY``
  The path to the GStreamer library.

Configuration Variables
^^^^^^^^^^^^^^^

Setting the following variables is required, depending on the operating system:

``GStreamer_ROOT_DIR``
  Installation prefix of the GStreamer SDK.

``GStreamer_USE_STATIC_LIBS`
  Set to ON to force the use of the static libraries. Default is OFF.

``GStreamer_EXTRA_DEPS``
  pkg-config names of the extra dependencies that will be included whenever linking against GStreamer.

#]=======================================================================]

if (GStreamer_FOUND)
    return()
endif()

#####################
#  Setup variables  #
#####################

if (NOT DEFINED GStreamer_ROOT_DIR AND DEFINED GSTREAMER_ROOT)
    set(GStreamer_ROOT_DIR ${GSTREAMER_ROOT})
endif()

if (NOT DEFINED GStreamer_USE_STATIC_LIBS)
set(GStreamer_USE_STATIC_LIBS OFF)
endif()

if (DEFINED GStreamer_ROOT_DIR)
if (NOT EXISTS "${GStreamer_ROOT_DIR}")
    message(FATAL_ERROR "The directory GStreamer_ROOT_DIR=${GStreamer_ROOT_DIR} does not exist")
endif()

# Set the environment for pkg-config
set(_old_pkg_config_path "$ENV{PKG_CONFIG_PATH}")
if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(ENV{PKG_CONFIG_PATH} "${GStreamer_ROOT_DIR}/lib/pkgconfig;${GStreamer_ROOT_DIR}/lib/gstreamer-1.0/pkgconfig;${GStreamer_ROOT_DIR}/lib/gio/modules/pkgconfig")
    # Block pkgconfig's forced relocation for non-lib/pkgconfig modules on Windows -- https://github.com/pkgconf/pkgconf/commit/dcf529b83d621ed09e99e41fc35fdffd068bd87a
    set(_old_pkg_config_dont_define_prefix "$ENV{PKG_CONFIG_DONT_DEFINE_PREFIX}")
    set(ENV{PKG_CONFIG_DONT_DEFINE_PREFIX} 1)
else()
    include(GNUInstallDirs)
    set(ENV{PKG_CONFIG_PATH} "${GStreamer_ROOT_DIR}/${CMAKE_INSTALL_LIBDIR}/pkgconfig:${GStreamer_ROOT_DIR}/${CMAKE_INSTALL_LIBDIR}/gstreamer-1.0/pkgconfig:${GStreamer_ROOT_DIR}/${CMAKE_INSTALL_LIBDIR}/gio/modules/pkgconfig")
    message(STATUS "PKG_CONFIG_PATH is $ENV{PKG_CONFIG_PATH}")
endif()
endif() # GStreamer_ROOT_DIR

# Set the list of extra dependencies
if (NOT DEFINED GStreamer_EXTRA_DEPS)
    set(GStreamer_EXTRA_DEPS)
    if (DEFINED GSTREAMER_EXTRA_DEPS)
        set(GStreamer_EXTRA_DEPS ${GSTREAMER_EXTRA_DEPS})
    endif()
endif()

macro(_gst_set_library_prefixes)
    set(_gst_suffixes "${CMAKE_FIND_LIBRARY_SUFFIXES}")
    set(_gst_prefixes "${CMAKE_FIND_LIBRARY_PREFIXES}")
    if (APPLE)
        set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".dylib" ".so" ".tbd")
        set(CMAKE_FIND_LIBRARY_PREFIXES ";lib")
    elseif (UNIX)
        set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".so")
        set(CMAKE_FIND_LIBRARY_PREFIXES ";lib")
    else()
        set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".lib")
        set(CMAKE_FIND_LIBRARY_PREFIXES ";lib")
    endif()
endmacro()

macro(_gst_unset_library_prefixes)
    set(CMAKE_FIND_LIBRARY_SUFFIXES "${_gst_suffixes}")
    set(CMAKE_FIND_LIBRARY_PREFIXES "${_gst_prefixes}")
    unset(_gst_suffixes)
    unset(_gst_prefixes)
endmacro()

# Find libraries. This is meant to be used with static libraries
# (hence the reprioritization) but I've added a fallback to shared libraries
# and stub modules in case any are non-existent.
function(_gst_find_library LOCAL_LIB GST_LOCAL_LIB)
    if (DEFINED ${GST_LOCAL_LIB})
        return()
    endif()

    _gst_set_library_prefixes()

    if ("${LOCAL_LIB}" IN_LIST _gst_IGNORED_SYSTEM_LIBRARIES)
        set(${GST_LOCAL_LIB} ${LOCAL_LIB} PARENT_SCOPE)
    else()
        # NO_DEFAULT_PATH is to avoid CMake's application of
        # CMAKE_SYSROOT to every path in HINTS
        find_library(${GST_LOCAL_LIB}
            ${LOCAL_LIB}
            HINTS ${ARGN}
            NO_DEFAULT_PATH
            NO_CMAKE_FIND_ROOT_PATH
            REQUIRED
        )
        if (${GST_LOCAL_LIB})
            set(${GST_LOCAL_LIB} "${${GST_LOCAL_LIB}}" PARENT_SCOPE)
        else()
            message(FATAL_ERROR "${LOCAL_LIB} was unexpectedly not found.")
        endif()
    endif()

    _gst_unset_library_prefixes()
endfunction()

macro(_gst_apply_link_libraries HIDE PC_LIBRARIES PC_HINTS GST_TARGET)
    if (APPLE AND ${HIDE})
        target_link_directories(${GST_TARGET} INTERFACE
            ${${PC_HINTS}}
        )
    endif()
    foreach(LOCAL_LIB IN LISTS ${PC_LIBRARIES})
        if (LOCAL_LIB MATCHES "${_gst_SRT_REGEX_PATCH}")
            string(REGEX REPLACE "${_gst_SRT_REGEX_PATCH}" "\\1" LOCAL_LIB "${LOCAL_LIB}")
        endif()
        string(MAKE_C_IDENTIFIER "_gst_${LOCAL_LIB}" GST_LOCAL_LIB)
        if (NOT ${GST_LOCAL_LIB})
            _gst_find_library(${LOCAL_LIB} ${GST_LOCAL_LIB} ${${PC_HINTS}})
        endif()
        # Do not use --exclude-libs on ELF platforms as it
        # neuters the effect of whole_archive
        target_link_libraries(${GST_TARGET} INTERFACE
            ${${GST_LOCAL_LIB}})
        unset(GST_LOCAL_LIB)
    endforeach()
endmacro()

macro(_gst_filter_missing_directories GST_INCLUDE_DIRS)
    set(_gst_include_dirs)
    foreach(DIR IN LISTS ${GST_INCLUDE_DIRS})
        string(MAKE_C_IDENTIFIER "${DIR}" _gst_dir_id)
        if (DEFINED _gst_exists_${_gst_dir_id})
            if (_gst_exists_${_gst_dir_id})
                list(APPEND _gst_include_dirs "${DIR}")
            endif()
        elseif (EXISTS "${DIR}")
            list(APPEND _gst_include_dirs "${DIR}")
            set(_gst_exists_${_gst_dir_id} TRUE)
        else()
            message(WARNING "Skipping missing include folder ${DIR}.")
            set(_gst_exists_${_gst_dir_id} FALSE)
        endif()
    endforeach()
    set(${GST_INCLUDE_DIRS} "${_gst_include_dirs}")
    unset(DIR)
endmacro()

macro(_gst_apply_frameworks PC_STATIC_LDFLAGS_OTHER GST_TARGET)
    if (APPLE)
        # LDFLAGS_OTHER may include framework linkage. Because CMake
        # iterates over arguments separated by spaces, it doesn't realise
        # that those arguments must not be split.
        set(new_ldflags)
        set(assemble_framework FALSE)
        foreach(_arg IN LISTS ${PC_STATIC_LDFLAGS_OTHER})
            if (assemble_framework)
                set(assemble_framework FALSE)
                find_library(GST_${_arg}_LIB ${_arg} REQUIRED)
                target_link_libraries(${GST_TARGET}
                    INTERFACE
                        "${GST_${_arg}_LIB}"
                )
            elseif (_arg STREQUAL "-framework")
                set(assemble_framework TRUE)
            else()
                set(assemble_framework FALSE)
                list(APPEND new_ldflags "${_arg}")
            endif()
        endforeach()
        unset(_arg)
        set_target_properties(${GST_TARGET} PROPERTIES
            INTERFACE_LINK_OPTIONS "${new_ldflags}"
        )
        unset(new_ldflags)
        unset(assemble_framework)
    else()
        set_target_properties(${TARGET} PROPERTIES
            INTERFACE_LINK_OPTIONS "${${PC_STATIC_LDFLAGS_OTHER}}"
        )
    endif()
endmacro()

################################
#      Set up the targets      #
################################

find_package(PkgConfig REQUIRED)

# For metadata only -- needed in UNIX
pkg_check_modules(PC_GST gstreamer-1.0)
# GStreamer's pkg-config modules are a MUST -- but we'll test them below
pkg_check_modules(PC_GStreamer gstreamer-1.0 ${GStreamer_EXTRA_DEPS})
# Simulate the list that'll be wholearchive'd.
# Unfortunately, this uses an option only available with pkgconf.
# set(_old_pkg_config_executable "${PKG_CONFIG_EXECUTABLE}")
# set(PKG_CONFIG_EXECUTABLE ${PKG_CONFIG_EXECUTABLE} --maximum-traverse-depth=1)
# pkg_check_modules(PC_GStreamer_NoDeps QUIET REQUIRED gstreamer-1.0 ${GStreamer_EXTRA_DEPS})
# set(PKG_CONFIG_EXECUTABLE "${_old_pkg_config_executable}")

set(GStreamer_VERSION "${PC_GST_VERSION}")

# Now, let's set up targets for each of the components supplied
# These are the required plugins
set(GSTREAMER_PLUGINS ${GStreamer_FIND_COMPONENTS})
# These are the API packages
set(GSTREAMER_APIS ${GSTREAMER_PLUGINS})
list(FILTER GSTREAMER_APIS INCLUDE REGEX "^gstreamer-")
# Filter them out, although they're handled the same
# they cannot be considered for the purposes of initialization
list(FILTER GSTREAMER_PLUGINS EXCLUDE REGEX "^gstreamer-")

# Test validity of the paths
# NOTE: only paths that must be considered are those provided by pkg-config
# NOTE 2: also exclude sysroots
find_path(GStreamer_INCLUDE_DIR
    NAMES gst/gstversion.h
    PATHS ${PC_GStreamer_INCLUDE_DIRS}
    PATH_SUFFIXES gstreamer-1.0
    NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH
    REQUIRED
)

if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(_gst_suffixes "${CMAKE_FIND_LIBRARY_SUFFIXES}")
    set(_gst_prefixes "${CMAKE_FIND_LIBRARY_PREFIXES}")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".lib")
    set(CMAKE_FIND_LIBRARY_PREFIXES "" "lib")
endif()
find_library(GStreamer_LIBRARY
    NAMES gstreamer-1.0
    PATHS ${PC_GStreamer_LIBRARY_DIRS}
    NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH
    REQUIRED
)
if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(CMAKE_FIND_LIBRARY_SUFFIXES "${_gst_suffixes}")
    set(CMAKE_FIND_LIBRARY_PREFIXES "${_gst_prefixes}")
    unset(_gst_suffixes)
    unset(_gst_prefixes)
endif()

# Android: Ignore these libraries when constructing the IMPORTED_LOCATION
set(_gst_IGNORED_SYSTEM_LIBRARIES c stdc++ c++ unwind m dl atomic)
if (ANDROID)
    list(APPEND _gst_IGNORED_SYSTEM_LIBRARIES log GLESv2 EGL OpenSLES android vulkan)
elseif(WIN32)
    list(APPEND _gst_IGNORED_SYSTEM_LIBRARIES ws2_32 kernel32 user32 gdi32 winspool shell32 ole32 oleaut32 uuid comdlg32 advapi32 winmm shlwapi dnsapi iphlpapi opengl32 crypt32 d3d11 dxgi d2d1 runtimeobject mf mfplat mfreadwrite mfuuid strmiids ksuser mmdevapi)
elseif(APPLE)
    list(APPEND _gst_IGNORED_SYSTEM_LIBRARIES iconv resolv System)
endif()

# Normalize library flags coming from srt/haisrt
# https://github.com/Haivision/srt/commit/b90b64d26f850fb0efcc4cdd8b31cbf74bd4db0c
set(_gst_SRT_REGEX_PATCH "^:lib(.+)\\.(a|so|lib|dylib)$")

if(PC_GStreamer_FOUND AND (NOT TARGET GStreamer::GStreamer))
    # This is not UNKNOWN but INTERFACE, as we only intend to
    # make a target suitable for downstream consumption.
    # FindPkgConfig already takes care of things, however it is totally unable
    # to discern between shared and static libraries when populating
    # xxx_STATIC_LINK_LIBRARIES, so we need to populate them manually.
    add_library(GStreamer::GStreamer INTERFACE IMPORTED)

    if (GStreamer_USE_STATIC_LIBS)
        _gst_filter_missing_directories(PC_GStreamer_STATIC_INCLUDE_DIRS)
        set_target_properties(GStreamer::GStreamer PROPERTIES
            INTERFACE_COMPILE_OPTIONS "${PC_GStreamer_STATIC_CFLAGS_OTHER}"
        )
        if (PC_GStreamer_STATIC_INCLUDE_DIRS)
            set_target_properties(GStreamer::GStreamer PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${PC_GStreamer_STATIC_INCLUDE_DIRS}"
            )
        endif()
        _gst_apply_frameworks(PC_GStreamer_STATIC_LDFLAGS_OTHER GStreamer::GStreamer)
    else()
        set_target_properties(GStreamer::GStreamer PROPERTIES
            INTERFACE_COMPILE_OPTIONS "${PC_GStreamer_CFLAGS_OTHER}"
            INTERFACE_INCLUDE_DIRECTORIES "${PC_GStreamer_INCLUDE_DIRS}"
            INTERFACE_LINK_OPTIONS "${PC_GStreamer_LDFLAGS_OTHER}"
        )
    endif()

    add_library(GStreamer::deps INTERFACE IMPORTED)

    if (NOT GStreamer_USE_STATIC_LIBS)
        set_target_properties(GStreamer::deps PROPERTIES
            INTERFACE_LINK_LIBRARIES "${PC_GStreamer_LINK_LIBRARIES}"
        )
        # We're done
    else()
        # Handle all libraries, even those specified with -l:libfoo.a (srt)
        # Due to the unavailability of pkgconf's `--maximum-traverse-depth`
        # on stock pkg-config, I attempt to simulate it through the shared
        # libraries listing.
        # If pkgconf is available, replace all PC_GStreamer_ entries with
        # PC_GStreamer_NoDeps and uncomment the code block above.
        foreach(LOCAL_LIB IN LISTS PC_GStreamer_LIBRARIES)
            # list(TRANSFORM REPLACE) is of no use here
            # https://gitlab.kitware.com/cmake/cmake/-/issues/16899
            if (LOCAL_LIB MATCHES "${_gst_SRT_REGEX_PATCH}")
                string(REGEX REPLACE "${_gst_SRT_REGEX_PATCH}" "\\1" LOCAL_LIB "${LOCAL_LIB}")
            endif()
            string(MAKE_C_IDENTIFIER "_gst_${LOCAL_LIB}" GST_LOCAL_LIB)
            if (NOT ${GST_LOCAL_LIB})
                _gst_find_library(${LOCAL_LIB} ${GST_LOCAL_LIB} ${PC_GStreamer_STATIC_LIBRARY_DIRS})
            endif()
            target_link_libraries(GStreamer::GStreamer INTERFACE
                "${${GST_LOCAL_LIB}}"
            )
            unset(GST_LOCAL_LIB)
        endforeach()

        _gst_apply_link_libraries(ON PC_GStreamer_STATIC_LIBRARIES PC_GStreamer_STATIC_LIBRARY_DIRS GStreamer::deps)
        unset(LOCAL_LIB)
    endif()

    target_link_libraries(GStreamer::GStreamer
        INTERFACE
            GStreamer::deps
    )
endif()

foreach(_gst_PLUGIN IN LISTS GSTREAMER_PLUGINS)
    # Safety valve for the custom targets above
    if ("${_gst_plugin}" IN_LIST _gst_CUSTOM_TARGETS)
        continue()
    endif()

    if (TARGET GStreamer::${_gst_PLUGIN})
        continue()
    endif()

    if (GStreamer_FIND_REQUIRED_${_gst_PLUGIN})
        set(_gst_PLUGIN_REQUIRED REQUIRED)
    else()
        set(_gst_PLUGIN_REQUIRED)
    endif()

    pkg_check_modules(PC_GStreamer_${_gst_PLUGIN} "gst${_gst_PLUGIN}" IMPORTED_TARGET)

    set(GStreamer_${_gst_PLUGIN}_FOUND "${PC_GStreamer_${_gst_PLUGIN}_FOUND}")
    if (NOT GStreamer_${_gst_PLUGIN}_FOUND)
        continue()
    endif()

    add_library(GStreamer::${_gst_PLUGIN} INTERFACE IMPORTED)
    _gst_filter_missing_directories(PC_GStreamer_${_gst_PLUGIN}_INCLUDE_DIRS)
    set_target_properties(GStreamer::${_gst_PLUGIN} PROPERTIES
        INTERFACE_COMPILE_OPTIONS "${PC_GStreamer_${_gst_PLUGIN}_CFLAGS_OTHER}"
    )
    if (PC_GStreamer_${_gst_PLUGIN}_INCLUDE_DIRS)
        set_target_properties(GStreamer::${_gst_PLUGIN} PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${PC_GStreamer_${_gst_PLUGIN}_INCLUDE_DIRS}"
        )
    endif()
    if (GStreamer_USE_STATIC_LIBS AND NOT WIN32)
        _gst_apply_frameworks(PC_GStreamer_${_gst_PLUGIN}_STATIC_LDFLAGS_OTHER GStreamer::${_gst_PLUGIN})
        _gst_apply_link_libraries(OFF PC_GStreamer_${_gst_PLUGIN}_STATIC_LIBRARIES PC_GStreamer_${_gst_PLUGIN}_STATIC_LIBRARY_DIRS GStreamer::${_gst_PLUGIN})
    else()
        target_link_libraries(GStreamer::${_gst_PLUGIN} INTERFACE 
            $<TARGET_PROPERTY:PkgConfig::PC_GStreamer_${_gst_PLUGIN},INTERFACE_LINK_LIBRARIES>
        )
        target_link_options(GStreamer::${_gst_PLUGIN} INTERFACE 
            $<TARGET_PROPERTY:PkgConfig::PC_GStreamer_${_gst_PLUGIN},INTERFACE_LINK_OPTIONS>
        )
    endif()
endforeach()

foreach(_gst_PLUGIN IN LISTS GSTREAMER_APIS)
    # Safety valve for the custom targets above
    if ("${_gst_plugin}" IN_LIST _gst_CUSTOM_TARGETS)
        continue()
    endif()

    if (TARGET GStreamer::${_gst_PLUGIN})
        continue()
    endif()

    if (GStreamer_FIND_REQUIRED_${_gst_PLUGIN})
        set(_gst_PLUGIN_REQUIRED REQUIRED)
    else()
        set(_gst_PLUGIN_REQUIRED)
    endif()

    pkg_check_modules(PC_GStreamer_${_gst_PLUGIN} ${_gst_PLUGIN} IMPORTED_TARGET)

    set(GStreamer_${_gst_PLUGIN}_FOUND "${PC_GStreamer_${_gst_PLUGIN}_FOUND}")
    if (NOT GStreamer_${_gst_PLUGIN}_FOUND)
        continue()
    endif()

    add_library(GStreamer::${_gst_PLUGIN} INTERFACE IMPORTED)
    if (GStreamer_USE_STATIC_LIBS AND NOT WIN32)
        _gst_apply_frameworks(PC_GStreamer_${_gst_PLUGIN}_STATIC_LDFLAGS_OTHER GStreamer::${_gst_PLUGIN})
        # Handle all libraries, even those specified with -l:libfoo.a (srt)
        _gst_apply_link_libraries(OFF PC_GStreamer_${_gst_PLUGIN}_STATIC_LIBRARIES PC_GStreamer_${_gst_PLUGIN}_STATIC_LIBRARY_DIRS GStreamer::${_gst_PLUGIN})
        target_link_options(GStreamer::${_gst_PLUGIN}
            INTERFACE
                $<TARGET_PROPERTY:PkgConfig::PC_GStreamer_${_gst_PLUGIN},INTERFACE_LINK_OPTIONS>
        )
        target_include_directories(GStreamer::${_gst_PLUGIN}
            INTERFACE
                $<TARGET_PROPERTY:PkgConfig::PC_GStreamer_${_gst_PLUGIN},INTERFACE_INCLUDE_DIRECTORIES>

        )
    else()
        target_link_libraries(GStreamer::${_gst_PLUGIN} INTERFACE 
            PkgConfig::PC_GStreamer_${_gst_PLUGIN}
        )
    endif()
endforeach()

# Perform final validation
include(FindPackageHandleStandardArgs)
set(_gst_handle_version_range)
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.19.0")
    set(_gst_handle_version_range "HANDLE_VERSION_RANGE")
endif()
set(GStreamer_PKG_CONFIG_DIRS "$ENV{PKG_CONFIG_PATH}")
find_package_handle_standard_args(GStreamer
    REQUIRED_VARS
        GStreamer_LIBRARY
        GStreamer_INCLUDE_DIR
        GStreamer_PKG_CONFIG_DIRS
    VERSION_VAR GStreamer_VERSION
    ${_gst_handle_version_range}
    HANDLE_COMPONENTS
)

if (DEFINED GStreamer_ROOT_DIR)
if (_old_pkg_config_path)
    set(ENV{PKG_CONFIG_PATH} "${_old_pkg_config_path}")
else()
    unset(ENV{PKG_CONFIG_PATH})
endif()
if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    if(_old_pkg_config_dont_define_prefix)
        set(ENV{PKG_CONFIG_DONT_DEFINE_PREFIX} "${_old_pkg_config_dont_define_prefix}")
    else()
        unset(ENV{PKG_CONFIG_DONT_DEFINE_PREFIX})
    endif()
endif()
endif() # GStreamer_ROOT_DIR
