idf_component_register(
    SRCS "main.c"
         "monitor_data.c"
         "monitor_usb.c"
         "monitor_font.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_driver_usb_serial_jtag esp_timer json
)
