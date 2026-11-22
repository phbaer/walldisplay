cmake_minimum_required(VERSION 3.16.0)
set(EXTRA_COMPONENT_DIRS src)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Embed the exact source revision in the image. CI builds use clean checkouts;
# local builds with tracked or untracked changes are marked dirty.
execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    RESULT_VARIABLE APP_BUILD_ID_RESULT
    OUTPUT_VARIABLE APP_BUILD_ID
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(APP_BUILD_ID_RESULT EQUAL 0 AND APP_BUILD_ID MATCHES "^[0-9a-fA-F]+$")
    string(LENGTH "${APP_BUILD_ID}" APP_BUILD_ID_LENGTH)
    if(APP_BUILD_ID_LENGTH EQUAL 40)
        execute_process(
            COMMAND git status --porcelain
            WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
            OUTPUT_VARIABLE APP_BUILD_STATUS
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(APP_BUILD_STATUS)
            string(APPEND APP_BUILD_ID "-dirty")
        endif()
    else()
        set(APP_BUILD_ID "unknown")
    endif()
else()
    set(APP_BUILD_ID "unknown")
endif()
add_compile_definitions(APP_BUILD_ID=\"${APP_BUILD_ID}\")

# Tagged release builds embed their exact tag (including -rc.N) in the
# firmware metadata. The checked-in header remains at the canonical project
# version for development and synchronization checks.
if(DEFINED APP_FW_VERSION_OVERRIDE)
    string(LENGTH "${APP_FW_VERSION_OVERRIDE}" APP_FW_VERSION_OVERRIDE_LENGTH)
    if(APP_FW_VERSION_OVERRIDE_LENGTH LESS 1 OR APP_FW_VERSION_OVERRIDE_LENGTH GREATER 32
       OR APP_FW_VERSION_OVERRIDE MATCHES "[^A-Za-z0-9.+-]")
        message(FATAL_ERROR "APP_FW_VERSION_OVERRIDE must contain 1–32 ASCII version characters")
    endif()
    add_compile_definitions(APP_FW_VERSION=\"${APP_FW_VERSION_OVERRIDE}\")
endif()

project(walldisplay)
