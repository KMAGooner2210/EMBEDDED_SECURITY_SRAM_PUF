#include <stdio.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_now.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "mqtt_client.h"

static const char *TAG = "PUF_GATEWAY";

#define WIFI_SSID           "kmagooner"
#define WIFI_PASS           "22102004"
#define MQTT_BROKER_IP      "192.168.90.152"         // <-- Thay bằng IP Laptop vừa tìm được ở Bước 1

// Cấu trúc gói dữ liệu nhận từ Node qua ESP-NOW
typedef struct struct_message {
    char mac[18];
    uint8_t puf_data[128];
} struct_message;

struct_message incoming_data;
esp_mqtt_client_handle_t mqtt_client = NULL;
bool is_mqtt_connected = false;

// Event handler xử lý các sự kiện MQTT
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Da ket noi thanh cong den MQTT Broker tren Laptop!");
            is_mqtt_connected = true;
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Mat ket noi voi MQTT Broker!");
            is_mqtt_connected = false;
            break;
        default:
            break;
    }
}

// Hàm khởi tạo và kết nối WiFi Station
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

    ESP_LOGI(TAG, "Dang ket noi den WiFi: %s...", WIFI_SSID);
    ESP_ERROR_CHECK(esp_wifi_connect());
}

// Hàm khởi tạo kết nối MQTT Client gửi dữ liệu lên Laptop
static void mqtt_app_start(void) {
    char broker_url[50];
    sprintf(broker_url, "mqtt://%s:1883", MQTT_BROKER_IP);
    
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_url,
    };
    
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

// Callback tự động kích hoạt khi Gateway nhận được gói tin ESP-NOW từ Node gửi sang
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    memcpy(&incoming_data, data, sizeof(incoming_data));
    ESP_LOGI(TAG, "Nhan tin hieu ESP-NOW tu Node: %s", incoming_data.mac);

    if (is_mqtt_connected && mqtt_client != NULL) {
        // Chuyển mảng byte PUF thành mảng JSON số nguyên để Server Python dễ xử lý
        char json_payload[1024];
        char puf_array_str[800] = "";
        char temp[8];

        strcat(puf_array_str, "[");
        for (int i = 0; i < 128; i++) {
            sprintf(temp, "%d%s", incoming_data.puf_data[i], (i == 127) ? "" : ",");
            strcat(puf_array_str, temp);
        }
        strcat(puf_array_str, "]");

        // Tạo gói tin JSON gửi lên Server
        // Vì hiện tại ta đang gửi raw PUF để test, ta map nó vào trường "response"
        sprintf(json_payload, "{\"mac\": \"%s\", \"response\": %s}", incoming_data.mac, puf_array_str);

        // Phát tín hiệu lên MQTT Broker qua topic esp32/puf/response
        esp_mqtt_client_publish(mqtt_client, "esp32/puf/response", json_payload, 0, 1, 0);
        ESP_LOGI(TAG, "Da chuyen tiep du lieu PUF len Laptop qua MQTT!");
    } else {
        ESP_LOGE(TAG, "Loi: Chua ket noi MQTT, khong the chuyen tiep!");
    }
}

void app_main(void) {
    // 1. Khởi tạo bộ nhớ flash NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Khởi tạo WiFi STA và kết nối mạng
    wifi_init_sta();

    // Chờ 5 giây để mạch kết nối WiFi ổn định và nhận IP từ Router
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    // 3. Khởi động MQTT kết nối tới Laptop
    mqtt_app_start();

    // 4. Khởi tạo ESP-NOW để giao tiếp với Node
    if (esp_now_init() != ESP_OK) {
        ESP_LOGE(TAG, "Loi khoi tao ESP-NOW!");
        return;
    }
    ESP_LOGI(TAG, "ESP-NOW Gateway san sang. Dang doi tin hieu Node...");
    
    // Đăng ký hàm nhận dữ liệu từ Node
    ESP_ERROR_CHECK(esp_now_register_recv_cb(OnDataRecv));
}
