/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"
#include "cw2015.h"

static const char *TAG = "cw2015_sample";

#define I2C_MASTER_SCL_IO CONFIG_I2C_SCL_IO
#define I2C_MASTER_SDA_IO CONFIG_I2C_SDA_IO

static i2c_master_bus_handle_t i2c_bus = NULL;
static cw2015_handle_t cw2015 = NULL;

static void record_data(uint16_t low_vol)
{
    ESP_LOGI(TAG, "End discharge voltage: %d", low_vol);
    printf("\n-------- Start record --------\n");
    printf("ElapsedTime, \t Voltage, \t SOC, \t SOC_RAW, \t RRT(min), \t ALRT\n");
    uint16_t vol = 0;
    uint32_t start_time_ms = esp_timer_get_time() / 1000;
    cw2015_get_voltage_mv(cw2015, &vol);
    while (vol > low_vol) {
        uint32_t elapsed_time_ms = esp_timer_get_time() / 1000 - start_time_ms;
        uint8_t soc = 0;
        uint16_t soc_raw = 0;
        uint16_t rrt_min = 0;
        bool alrt = false;
        cw2015_get_voltage_mv(cw2015, &vol);
        cw2015_get_soc_percent(cw2015, &soc);
        cw2015_get_soc_raw(cw2015, &soc_raw);
        cw2015_get_rrt_minutes(cw2015, &rrt_min);
        cw2015_get_alert_flag(cw2015, &alrt);
        printf("%ld, \t %u, \t %u, \t 0x%04X, \t %u, \t %d\n",
               elapsed_time_ms / 1000, vol, soc, soc_raw, rrt_min, alrt);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    printf("\n-------- End record --------\n");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Hello CW2015 Battery Fuel Gauge!");
    ESP_LOGI(TAG, "I2C master SCL IO: %d", I2C_MASTER_SCL_IO);
    ESP_LOGI(TAG, "I2C master SDA IO: %d", I2C_MASTER_SDA_IO);

    i2c_master_bus_config_t conf = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = 1,
        },
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&conf, &i2c_bus));

    cw2015_config_t cfg = {
        .i2c_bus = i2c_bus,
        .i2c_addr = 0,
        .scl_speed_hz = 400000,
        .wake_on_create = true,
        .quick_start_on_create = false,
    };
    cw2015 = cw2015_create(&cfg);
    if (!cw2015) {
        ESP_LOGE(TAG, "CW2015 init failed");
        return;
    }

    record_data(CONFIG_END_DISCHAGE_VOLTAGE);

    cw2015_delete(cw2015);
    cw2015 = NULL;
    i2c_del_master_bus(i2c_bus);
    i2c_bus = NULL;
}
