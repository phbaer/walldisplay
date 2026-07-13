set(APP_CONFIG_YAML ${CMAKE_SOURCE_DIR}/config/panel_config.yaml)
set(APP_CONFIG_GEN_DIR ${CMAKE_BINARY_DIR}/generated/include/walldisplay)
set(APP_CONFIG_GEN_HEADER ${APP_CONFIG_GEN_DIR}/app_config_defaults.h)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated/include)

if(NOT CMAKE_SCRIPT_MODE_FILE)
    add_custom_command(
        OUTPUT ${APP_CONFIG_GEN_HEADER}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${APP_CONFIG_GEN_DIR}
        COMMAND ${PYTHON} ${CMAKE_SOURCE_DIR}/tools/generate_app_config_defaults.py ${APP_CONFIG_YAML} ${APP_CONFIG_GEN_HEADER}
        DEPENDS ${APP_CONFIG_YAML} ${CMAKE_SOURCE_DIR}/tools/generate_app_config_defaults.py
        VERBATIM
    )

    add_custom_target(app_config_defaults_header DEPENDS ${APP_CONFIG_GEN_HEADER})
endif()

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.c)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "../include" "${CMAKE_BINARY_DIR}/generated/include"
    REQUIRES app_update esp_http_client esp_lcd driver esp_driver_ledc esp_event esp_timer heap mbedtls nvs_flash esp_netif esp_wifi mqtt cjson
    EMBED_FILES
        "assets/panel_background.rgb565"
        "assets/weather/weather_clear_night.argb8888"
        "assets/weather/weather_cloudy.argb8888"
        "assets/weather/weather_fog.argb8888"
        "assets/weather/weather_lightning.argb8888"
        "assets/weather/weather_lightning_rainy_day.argb8888"
        "assets/weather/weather_lightning_rainy_night.argb8888"
        "assets/weather/weather_partly_cloudy_day.argb8888"
        "assets/weather/weather_partly_cloudy_night.argb8888"
        "assets/weather/weather_pouring.argb8888"
        "assets/weather/weather_rainy.argb8888"
        "assets/weather/weather_snowy.argb8888"
        "assets/weather/weather_snowy_rainy.argb8888"
        "assets/weather/weather_sunny.argb8888"
        "assets/weather/weather_unknown.argb8888"
)

if(NOT CMAKE_SCRIPT_MODE_FILE)
    add_dependencies(${COMPONENT_LIB} app_config_defaults_header)
endif()
