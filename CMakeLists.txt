cmake_minimum_required(VERSION 3.16.0)
set(EXTRA_COMPONENT_DIRS src)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

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
