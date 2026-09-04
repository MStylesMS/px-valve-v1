idf_component_register(
    SRCS "main.c" "mcp23s17.c" "valve_engine.c" "web_ui.c" "web_ui_json.c"
    REQUIRES px_system svc_wifi svc_mqtt lib_json_helper lib_http_proxy svc_nvs_config svc_ota
             esp_http_server esp_wifi esp_netif nvs_flash esp_app_format esp_timer spiffs mdns app_update
             esp_driver_gpio esp_driver_spi mbedtls espressif__cjson
    EMBED_TXTFILES "webui/index.html" "webui/config.html" "webui/connection.html" "webui/update.html" "webui/monitor.html" "webui/styles.css" "webui/app.js"
    EMBED_FILES "webui/assets/logo.png"
)
