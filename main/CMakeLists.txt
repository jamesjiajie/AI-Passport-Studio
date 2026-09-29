idf_component_register(
    SRCS "main.c"
         "monitor_data.c"
         "monitor_net.c"
         "monitor_font.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_http_client esp_netif esp_wifi json nvs_flash
)
