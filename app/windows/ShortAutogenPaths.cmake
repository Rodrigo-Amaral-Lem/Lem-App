# SPDX-License-Identifier: GPL-3.0-only
# Loaded only by the app-local Qt source build through CMAKE_PROJECT_INCLUDE.
# Qt's generated plugin class names repeat the long target name. Its Windows
# moc uses _wfopen_s, which can fail at MAX_PATH even when the compiler works.
# Set the standard property at target creation, before Qt calculates paths for
# metatype extraction. Other targets retain their existing output/compile paths.
include_guard(GLOBAL)

function(_lem_short_plugin_autogen target)
    if(NOT TARGET "${target}")
        return()
    endif()
    get_target_property(imported "${target}" IMPORTED)
    get_target_property(alias "${target}" ALIASED_TARGET)
    if(imported OR alias)
        return()
    endif()
    string(TOLOWER "${target}" lower_name)
    if(NOT lower_name MATCHES "plugin$")
        return()
    endif()
    set(estimated "${CMAKE_CURRENT_BINARY_DIR}/${target}_autogen/include/${target}_${target}Plugin.moc.json")
    string(LENGTH "${estimated}" length)
    if(length GREATER 240)
        string(SHA256 key "${CMAKE_CURRENT_BINARY_DIR}/${target}")
        string(SUBSTRING "${key}" 0 20 key)
        set_property(TARGET "${target}" PROPERTY AUTOGEN_BUILD_DIR "${CMAKE_BINARY_DIR}/.ag/${key}")
    endif()
endfunction()

function(add_library)
    _add_library(${ARGV})
    _lem_short_plugin_autogen("${ARGV0}")
endfunction()
