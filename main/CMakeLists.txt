set(JUSTTYPE_LVGL_ROOT "/Users/yonah/xdev/justType/justTypeLVGL/libs/lvgl")

file(GLOB_RECURSE LVGL_SOURCES "${JUSTTYPE_LVGL_ROOT}/src/*.c")

idf_component_register(
    SRCS
        "esp_port_main.cpp"
        "platform_esp.cpp"
        "justtype/src/app.cpp"
        "justtype/src/editor.cpp"
        "justtype/src/editor_word_count.cpp"
        "justtype/src/qr_export.cpp"
        "justtype/src/textarea_utils.cpp"
        "justtype/src/sidebar.cpp"
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
        ${LVGL_SOURCES}
    PRIV_REQUIRES
        spi_flash
        esp_lcd
        driver
        esp_timer
        spiffs
        pthread
    INCLUDE_DIRS
        "."
        "justtype"
        "justtype/src"
        "justtype/platforms"
        "${JUSTTYPE_LVGL_ROOT}"
        "${JUSTTYPE_LVGL_ROOT}/src"
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
