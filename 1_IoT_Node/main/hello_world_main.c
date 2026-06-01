#include <stdio.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_now.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PUF_NODE_SENDER";

#define PUF_SIZE 128

#define WIFI_SSID          "kmagooner"     
#define WIFI_PASS          "22102004"
// Khai báo vùng nhớ SRAM PUF
RTC_NOINIT_ATTR uint8_t sram_puf[PUF_SIZE];
RTC_NOINIT_ATTR uint32_t boot_count;

uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef struct struct_message {
    char mac[18];
    uint8_t puf_data[128];
} struct_message;

struct_message my_data;

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGI(TAG, "Da phat tin hieu ESP-NOW thanh cong!");
    } else {
        ESP_LOGE(TAG, "Loi phat tin hieu ESP-NOW!");
    }
}

// Khởi tạo WiFi STA và kết nối vào Router để đồng bộ kênh sóng
static void wifi_init_sta(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Node dang ket noi WiFi de dong bo kenh song...");
    esp_wifi_connect();
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Kết nối WiFi
    wifi_init_sta();

    // Chờ 5 giây để đồng bộ kênh sóng với Router
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    // Lấy địa chỉ MAC thật
    uint8_t mac_addr[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac_addr);
    sprintf(my_data.mac, "%02x:%02x:%02x:%02x:%02x:%02x", 
            mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

    // Đọc PUF
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_POWERON) {
        boot_count = 1;
    } else {
        boot_count++;
    }
    memcpy(my_data.puf_data, sram_puf, PUF_SIZE);

    // Khởi tạo ESP-NOW
    if (esp_now_init() != ESP_OK) {
        ESP_LOGE(TAG, "Loi khoi tao ESP-NOW!");
        return;
    }
    esp_now_register_send_cb(OnDataSent);

    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, broadcast_mac, 6);
    peer_info.channel = 0;  
    peer_info.encrypt = false;
    
    if (esp_now_add_peer(&peer_info) != ESP_OK){
        ESP_LOGE(TAG, "Loi add peer!");
        return;
    }

    while(1) {
        ESP_LOGI(TAG, "Dang phat van tay SRAM PUF cua MAC %s...", my_data.mac);
        esp_err_t outcome = esp_now_send(broadcast_mac, (uint8_t *) &my_data, sizeof(my_data));
        if (outcome != ESP_OK) {
            ESP_LOGE(TAG, "Goi tin loi!");
        }
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}
