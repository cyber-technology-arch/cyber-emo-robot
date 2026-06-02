/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "cw2015.h"

#define I2C_MASTER_SCL_IO GPIO_NUM_6
#define I2C_MASTER_SDA_IO GPIO_NUM_7

#define TEST_MEMORY_LEAK_FIRST_THRESHOLD (-110)

static i2c_master_bus_handle_t i2c_bus = NULL;
static cw2015_handle_t cw2015 = NULL;

static const char *TAG = "cw2015_test";

static void test_cw2015_init(void)
{
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
    TEST_ASSERT_NOT_NULL(cw2015);
}

static void test_cw2015_deinit(void)
{
    TEST_ASSERT(cw2015_delete(cw2015) == ESP_OK);
    cw2015 = NULL;
    TEST_ASSERT(i2c_del_master_bus(i2c_bus) == ESP_OK);
    i2c_bus = NULL;
}

static void test_cw2015_print_info(cw2015_handle_t h)
{
    uint8_t version = 0;
    uint16_t voltage_mv = 0;
    uint16_t soc_raw = 0;
    uint8_t soc_pct = 0;
    uint16_t rrt_min = 0;
    bool alrt = false;
    uint8_t athd = 0;
    bool ufg = false;

    cw2015_get_version(h, &version);
    cw2015_get_voltage_mv(h, &voltage_mv);
    cw2015_get_soc_raw(h, &soc_raw);
    cw2015_get_soc_percent(h, &soc_pct);
    cw2015_get_rrt_minutes(h, &rrt_min);
    cw2015_get_alert_flag(h, &alrt);
    cw2015_get_alert_threshold_percent(h, &athd);
    cw2015_get_ufg_flag(h, &ufg);

    ESP_LOGI(TAG, "版本=0x%02X, 电压=%umV, SOC=%u%%(raw=0x%04X), RRT=%umin, ALRT=%d, ATHD=%u%%, UFG=%d",
             version, voltage_mv, soc_pct, soc_raw, rrt_min, alrt, athd, ufg);
}

TEST_CASE("cw2015 basic information query test", "[voltage][soc][rrt][alert]")
{
    test_cw2015_init();
    test_cw2015_print_info(cw2015);
    test_cw2015_deinit();
}

static size_t before_free_8bit;
static size_t before_free_32bit;

static void check_leak(size_t before_free, size_t after_free, ssize_t threshold, const char *type)
{
    ssize_t delta = after_free - before_free;
    printf("MALLOC_CAP_%s: Before %u bytes free, After %u bytes free (delta %d)\n", type, before_free, after_free, delta);
    if (!(delta >= threshold)) {
        ESP_LOGE(TAG, "Memory leak detected, delta: %d bytes, threshold: %d bytes", delta, threshold);
    }
}

void setUp(void)
{
    before_free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    before_free_32bit = heap_caps_get_free_size(MALLOC_CAP_32BIT);
}

void tearDown(void)
{
    ssize_t threshold = TEST_MEMORY_LEAK_FIRST_THRESHOLD;
    static bool is_first = true;
    if (is_first) {
        is_first = false;
    } else {
        threshold = 0;
    }
    size_t after_free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t after_free_32bit = heap_caps_get_free_size(MALLOC_CAP_32BIT);
    check_leak(before_free_8bit, after_free_8bit, threshold, "8BIT");
    check_leak(before_free_32bit, after_free_32bit, threshold, "32BIT");
}

void app_main(void)
{
    printf("CW2015 TEST \n");
    unity_run_menu();
}
