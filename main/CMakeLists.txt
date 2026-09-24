idf_component_register(
    SRCS "main.c"
         "demo_navigation.c"
         "ui_pixel.c"
         "ui_pixel_math.c"
         "demo_chess.c"
         "chess_rules.c"
         "chess_ai.c"
         "chess_commentary.c"
         "chess_wifi.c"
         "demo_radio.c"
         "fonts/chess_cjk_14.c"
         "fonts/chess_cjk_24.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_event esp_hw_support esp_http_client esp_netif esp_timer esp_wifi json nvs_flash
    PRIV_REQUIRES mbedtls
)
