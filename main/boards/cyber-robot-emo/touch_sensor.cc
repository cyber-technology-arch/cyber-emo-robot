/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "config.h"
#include "iot_button.h"
#include "button_gpio.h" // Added include
#include "touch_sensor.h"
#include "board.h"
#include "display/display.h"
#include "assets/lang_config.h"
#include "application.h"
#include "wifi_station.h"
#include "wifi_board.h"

static const char *TAG = "Touch";

static button_handle_t touch_btn_handle = NULL;

static void HandleWakeWord() {
    auto &app = Application::GetInstance();
    ESP_LOGI(TAG, "模仿触摸，测试使用.........");
    ESP_LOGI(TAG, "Triggering Wake Word via Touch (Long Press)");
    if (app.GetDeviceState() == kDeviceStateListening || app.GetDeviceState() == kDeviceStateSpeaking) {
        app.TriggerWakeWord(TOUCH_WAKE_WORD);
    } else if (app.GetDeviceState() == kDeviceStateIdle) {
        std::string wake_word = TOUCH_WAKE_WORD;
        app.WakeWordInvoke(wake_word);
    }
}

static void HandleBootKey() {
    auto& app = Application::GetInstance();
    ESP_LOGI(TAG, "单击按下，测试使用.........");
    ESP_LOGI(TAG, "Handling Boot Key Action (Single Click)");
    if (app.GetDeviceState() == kDeviceStateStarting &&
        !WifiStation::GetInstance().IsConnected()) {
        auto& board = Board::GetInstance();
        WifiBoard* wifi_board = static_cast<WifiBoard*>(&board);
        wifi_board->ResetWifiConfiguration();
    }
    app.ToggleChatState();
}

static void touch_btn_event_cb(void *button_handle, void *usr_data)
{
    button_event_t event = iot_button_get_event((button_handle_t)button_handle);
    switch (event) {
    case BUTTON_SINGLE_CLICK:
        // Short press / Click acts as original Boot Button
        HandleBootKey();
        break;
    case BUTTON_LONG_PRESS_START:
        // Long press acts as Touch Wake Word
        HandleWakeWord();
        break;
    default:
        break;
    }
}

static esp_err_t init_touch_button(void)
{
    // Initialize GPIO0 as a button with Long Press support
    button_config_t btn_cfg = {
        .long_press_time = 1500, // 1500ms for Long Press to trigger Touch function
        .short_press_time = 50,  // 50ms debounce
    };

    button_gpio_config_t gpio_cfg = {
        .gpio_num = (int32_t)BOOT_BUTTON_GPIO,
        .active_level = 0, // Assuming active low for Boot button
        .enable_power_save = false,
        .disable_pull = false,
    };

    esp_err_t ret = iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &touch_btn_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create gpio button device: %s", esp_err_to_name(ret));
        return ret;
    }

    iot_button_register_cb(touch_btn_handle, BUTTON_SINGLE_CLICK, NULL, touch_btn_event_cb, NULL);
    iot_button_register_cb(touch_btn_handle, BUTTON_LONG_PRESS_START, NULL, touch_btn_event_cb, NULL);

    ESP_LOGI(TAG, "Touch/Boot Button initialized on GPIO %d", BOOT_BUTTON_GPIO);
    return ESP_OK;
}

TouchSensor::TouchSensor()
{
}

TouchSensor::~TouchSensor()
{
}

bool TouchSensor::init()
{
    esp_err_t ret = init_touch_button();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize touch button: %s", esp_err_to_name(ret));
        return false;
    }
    return true;
}

button_handle_t TouchSensor::get_button_handle()
{
    return touch_btn_handle;
}

bool TouchSensor::read_value(int &value)
{
    // Return inverted logic because active low
    value = gpio_get_level(BOOT_BUTTON_GPIO) == 0 ? 1 : 0;
    return true;
}
