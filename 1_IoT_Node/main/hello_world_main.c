#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_attr.h"

#define PUF_SIZE  128   // 128 bytes lam khoa PUF

// Khai bao vung nho RTC khong bi Bootloader xoa khi khoi dong
RTC_NOINIT_ATTR   uint8_t sram_puf[PUF_SIZE];
RTC_NOINIT_ATTR   uint32_t boot_count;

static const char * TAG = "SRAM_PUF_NODE";

void app_main(void){
	
	// Doc ly do khoi dong lai cua chip
	esp_reset_reason_t reason = esp_reset_reason();
	
	if(reason == ESP_RST_POWERON){
		// Cold Boot (Cam dien lan dau)
		ESP_LOGI(TAG, "Phat hien COLD BOOT! SRAM dang o trang thai nguyen thuy.");
		boot_count = 1;
	}else{
		// Warm Boot (An nut Reset hoac reset mem bang code)
		boot_count++;
		ESP_LOGW(TAG, "Phat hien WARM BOOT (Lan reset thu: %lu). SRAM giu nguyen gia tri cu.", boot_count);
	}
	
	// In 128 bytes du lieu SRAM PUF ra man hinh duoi dang ma HEX
	ESP_LOGI(TAG, "===== DU LIEU SRAM PUF (%d Bytes) =====", PUF_SIZE);
	for(int i = 0; i < PUF_SIZE; i+=16){
		ESP_LOGI(TAG, "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
		sram_puf[i], sram_puf[i+1], sram_puf[i+2], sram_puf[i+3],
                sram_puf[i+4], sram_puf[i+5], sram_puf[i+6], sram_puf[i+7],
                sram_puf[i+8], sram_puf[i+9], sram_puf[i+10], sram_puf[i+11],
                sram_puf[i+12], sram_puf[i+13], sram_puf[i+14], sram_puf[i+15]);
        }
        ESP_LOGI(TAG, "=============");
        while(1){
        	vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
  }
