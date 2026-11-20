idf_component_register(
    SRCS
        "esp_port_main.cpp"
        "platform_esp.cpp"
        "backlight.cpp"
        "serial_export.cpp"
        "backup/backup.cpp"
        "backup/storage.cpp"
        "keyboard_config.cpp"
        "justtype/src/fonts/intl_14.c"
        "justtype/src/fonts/intl_16.c"
        "justtype/src/fonts/intl_18.c"
        "justtype/src/fonts/intl_20.c"
        "justtype/src/fonts/intl_22.c"
        "justtype/src/fonts/intl_24.c"
        "justtype/src/fonts/intl_26.c"
        "justtype/src/fonts/intl_28.c"
        "justtype/src/fonts/intl_30.c"
        "justtype/src/fonts/intl_32.c"
        "justtype/src/fonts/intl_36.c"
        "justtype/src/fonts/intl_40.c"
        "keyboard_input.cpp"
        "ble_keyboard.cpp"
        "justtype/src/app.cpp"
        "justtype/src/settings.cpp"
        "justtype/src/spellcheck.cpp"
        "justtype/src/spelling/spellcheck_core.cpp"
        "justtype/src/spelling/viewport_checker.cpp"
        "justtype/src/editor.cpp"
        "justtype/src/editor_word_count.cpp"
        "justtype/src/qr_export.cpp"
        "justtype/src/quick_open.cpp"
        "justtype/src/textarea_utils.cpp"
        "justtype/src/sidebar.cpp"
        "justtype/src/sidebar_snapshot.cpp"
        "justtype/src/sidebar_dialog.cpp"
        "justtype/src/sidebar_folder.cpp"
        "justtype/src/sidebar_file.cpp"
        "justtype/src/sidebar_file_utils.cpp"
        "justtype/src/sidebar_file_dialog.cpp"
        "justtype/src/search.cpp"
        "justtype/src/file_manager.cpp"
        "justtype/src/text_navigation.cpp"
        "justtype/src/icons/battery_icon.cpp"
        "justtype/src/icons/magnifying_glass_icon.cpp"
        "justtype/src/icons/add_document_icon.cpp"
        "justtype/src/icons/add_folder_icon.cpp"
    PRIV_REQUIRES
        lvgl
        spi_flash
        esp_lcd
        driver
        esp_driver_ledc
        esp_timer
        spiffs
        fatfs
        wear_levelling
        pthread
        bt
        esp_hid
        nvs_flash
        esp_wifi
        esp_netif
        esp_http_client
        esp-tls
        mbedtls
        json
        bootloader_support
    INCLUDE_DIRS
        "."
        "justtype"
        "justtype/src"
        "justtype/platforms"
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    LV_USE_SDL=0
    LV_USE_LINUX_FBDEV=0
    LV_USE_EVDEV=0
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=deprecated-enum-enum-conversion
)

# Materialize an ignored, local Google client configuration into a tracked build dependency.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/backup/backup_secrets.h")
    configure_file("backup/backup_secrets.h" "backup_client_config.h" COPYONLY)
else()
    configure_file("backup/backup_secrets.example.h" "backup_client_config.h" COPYONLY)
endif()
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
