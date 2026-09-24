// main/chess_wifi.c —— 开发期硬编码 Wi-Fi 连接(方向 A,验证 JEV 棋评)
// STA 模式连路由器,异步:event handler 收 STA_START→connect,GOT_IP→connected。
// SSID 空(社区版)不启动;真 SSID 在 chess_wifi_secret.h(自编译版)。
#include "chess_wifi.h"
#include "demo_radio.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "chess_wifi";
static bool s_connected = false;
static bool s_started = false;

static void handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)data;
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            s_connected = false;
            esp_wifi_connect();       /* 自动重连 */
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        ESP_LOGI(TAG, "connected, got IP");
    }
}

void chess_wifi_init(void) {
    if (s_started) return;
    if (WIFI_SSID[0] == 0) return;     /* 占位空:社区版不连接 */
    if (demo_radio_network_prepare() != ESP_OK) {
        ESP_LOGE(TAG, "network prepare failed");
        return;
    }
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, handler, NULL);
    esp_wifi_set_mode(WIFI_MODE_STA);
    wifi_config_t wc;
    memset(&wc, 0, sizeof(wc));
    strncpy((char *)wc.sta.ssid, WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, WIFI_PASS, sizeof(wc.sta.password) - 1);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_start();
    s_started = true;
    ESP_LOGI(TAG, "started, connecting to %s", WIFI_SSID);
}

bool chess_wifi_is_connected(void) {
    return s_connected;
}
