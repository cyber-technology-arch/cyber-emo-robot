#include "cyber_led.h"
#include "application.h"
#include "board.h"
#include <esp_log.h>

#define TAG "CyberLed"

void CyberLed::OnStateChanged() {
    auto& app = Application::GetInstance();
    auto device_state = app.GetDeviceState();
    
    // ESP_LOGI(TAG, "OnStateChanged: %d", device_state);

    switch (device_state) {
        case kDeviceStateListening:
        case kDeviceStateAudioTesting:
            {
                std::lock_guard<std::mutex> lock(mutex_);
                esp_timer_stop(strip_timer_);
                // Reduce brightness to avoid power issues
                uint8_t safe_brightness = default_brightness_;
                if (safe_brightness == 0) safe_brightness = 1;

                for (int i = 0; i < max_leds_; i++) {
                    // Blue for Listening
                    colors_[i] = {0, 0, safe_brightness};
                    led_strip_set_pixel(led_strip_, i, 0, 0, safe_brightness);
                }
                led_strip_refresh(led_strip_);
            }
            break;
            
        case kDeviceStateSpeaking:
            {
                // Speaking state is now handled by SetMusicLevel dynamically
                // But we set a base color here in case audio hasn't started
                std::lock_guard<std::mutex> lock(mutex_);
                esp_timer_stop(strip_timer_);
                // Reduce brightness to avoid power issues
                uint8_t safe_brightness = default_brightness_;
                if (safe_brightness == 0) safe_brightness = 1;
                
                for (int i = 0; i < max_leds_; i++) {
                    // Green for Speaking
                    colors_[i] = {0, safe_brightness, 0};
                    led_strip_set_pixel(led_strip_, i, 0, safe_brightness, 0);
                }
                led_strip_refresh(led_strip_);
            }
            break;

        case kDeviceStateIdle:
            {
                // 空闲状态下不再根据音乐状态做灯效，统一清空
                std::lock_guard<std::mutex> lock(mutex_);
                esp_timer_stop(strip_timer_);
                for (int i = 0; i < max_leds_; i++) {
                    colors_[i] = {0, 0, 0};
                }
                led_strip_clear(led_strip_);
            }
            break;
            
        default:
            // For other states (Connecting, Upgrading, etc.), fall back to base
            CircularStrip::OnStateChanged();
            break;
    }
}
