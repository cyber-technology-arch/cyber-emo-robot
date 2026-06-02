#ifndef __POWER_MANAGER_H__
#define __POWER_MANAGER_H__

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <esp_timer.h>
#include "cw2015.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


class PowerManager {
private:
    esp_timer_handle_t timer_handle_ = nullptr;
    cw2015_handle_t cw_handle_ = nullptr;
    uint8_t battery_level_ = 100;
    bool is_charging_ = false;
    inline static bool battery_update_paused_ = false;  // 静态标志：是否暂停电量更新

    // 充电检测所需变量（250ms定时器，8个样本仍对应约2秒）
    static constexpr uint8_t kSocHistorySize = 8;
    static constexpr uint16_t kSocTrendThresholdRaw = 2; // 约0.008%，过滤CW2015小数位抖动
    static constexpr uint8_t kChargingConfirmCount = 2;
    static constexpr uint8_t kDischargingConfirmCount = 3;
    uint16_t soc_history_[kSocHistorySize] = {0};
    uint8_t history_idx_ = 0;
    uint8_t history_count_ = 0;
    uint8_t charging_vote_count_ = 0;
    uint8_t discharging_vote_count_ = 0;
    uint8_t print_cnt_ = 0;

    // 根据电压计算电量百分比（0-100%精确映射）
    uint8_t CalculateBatteryLevelFromVoltage(uint16_t voltage) const {
        // 锂电池电压-电量映射表（电压mV，电量%）
        static const struct VoltageToSoc {
            uint16_t voltage;
            uint8_t soc;
        } mapping[] = {
            {4130, 100},
            {4070, 95},
            {4010, 90},
            {3950, 80},
            {3890, 70},
            {3830, 60},
            {3770, 50},
            {3710, 40},
            {3650, 30},
            {3590, 20},
            {3530, 10},
            {3460, 5},
            {3400, 0}
        };
        
        size_t mapping_size = sizeof(mapping) / sizeof(mapping[0]);
        
        // 处理边界情况
        if (voltage >= mapping[0].voltage) return mapping[0].soc;
        if (voltage <= mapping[mapping_size - 1].voltage) return mapping[mapping_size - 1].soc;
        
        // 线性插值计算中间值
        for (size_t i = 0; i < mapping_size - 1; i++) {
            if (voltage >= mapping[i + 1].voltage && voltage <= mapping[i].voltage) {
                // 计算线性插值
                uint16_t v1 = mapping[i].voltage;
                uint16_t v2 = mapping[i + 1].voltage;
                uint8_t s1 = mapping[i].soc;
                uint8_t s2 = mapping[i + 1].soc;
                
                // 计算电压差和电量差
                int voltage_diff = v1 - v2;
                int soc_diff = s1 - s2;
                
                // 计算当前电压对应的电量
                int voltage_offset = v1 - voltage;
                int soc = s1 - (soc_diff * voltage_offset) / voltage_diff;
                
                // 确保电量在0-100范围内
                return soc > 100 ? 100 : (soc < 0 ? 0 : soc);
            }
        }
        
        return 0;
    }

    void CheckBatteryStatus() {
        // 如果电量更新被暂停（动作进行中），则跳过更新
        if (battery_update_paused_) {
            return;
        }

        if (!cw_handle_) return;

        // 读取电压
        uint16_t voltage = 0;
        cw2015_get_voltage_mv(cw_handle_, &voltage);
        
        uint16_t soc_raw = 0;
        esp_err_t err = cw2015_get_soc_raw(cw_handle_, &soc_raw);

        if (err == ESP_OK && soc_raw != 0) {
            // 比较当前SOC与窗口内最老的SOC；窗口保持约2秒，但样本从4个增加到8个。
            uint16_t oldest_soc = history_count_ >= kSocHistorySize ? soc_history_[history_idx_] : soc_history_[0];
            bool has_enough_history = history_count_ >= 4;
            int32_t soc_delta = static_cast<int32_t>(soc_raw) - static_cast<int32_t>(oldest_soc);

            if (has_enough_history) {
                if (soc_delta > kSocTrendThresholdRaw) {
                    if (charging_vote_count_ < kChargingConfirmCount) {
                        charging_vote_count_++;
                    }
                    discharging_vote_count_ = 0;
                } else if (soc_delta < -static_cast<int32_t>(kSocTrendThresholdRaw)) {
                    if (discharging_vote_count_ < kDischargingConfirmCount) {
                        discharging_vote_count_++;
                    }
                    charging_vote_count_ = 0;
                } else if (soc_raw >= 25600 && voltage >= 4100) {
                    // 满电高电压时允许保持/确认充电状态，但也需要连续确认，避免单次抖动触发。
                    if (charging_vote_count_ < kChargingConfirmCount) {
                        charging_vote_count_++;
                    }
                    discharging_vote_count_ = 0;
                }

                if (charging_vote_count_ >= kChargingConfirmCount) {
                    is_charging_ = true;
                } else if (discharging_vote_count_ >= kDischargingConfirmCount) {
                    is_charging_ = false;
                }
            }
            
            // 更新索引
            soc_history_[history_idx_] = soc_raw;
            history_idx_ = (history_idx_ + 1) % kSocHistorySize;
            if (history_count_ < kSocHistorySize) {
                history_count_++;
            }
        }
        
        // 当设备充电时，电压会升高，需要修正电压值（减小150mV）以获得更准确的电量计算
        uint16_t adjusted_voltage = voltage;
        if (is_charging_) {
            adjusted_voltage = voltage > 150 ? voltage - 150 : 0;
            // ESP_LOGI("PowerManager", "Raw voltage: %u mV, Adjusted voltage: %u mV (charging)", voltage, adjusted_voltage);
        }
        
        if (err != ESP_OK || soc_raw == 0) {
            // 根据修正后的电压计算电量（兜底）
            battery_level_ = CalculateBatteryLevelFromVoltage(adjusted_voltage);
        } else {
            battery_level_ = (uint8_t)(soc_raw >> 8);
            if (battery_level_ > 100) {
                battery_level_ = 100;
            }
        }
        
        uint16_t rrt = 0;
        cw2015_get_rrt_minutes(cw_handle_, &rrt);
        
        bool alert = false;
        cw2015_get_alert_flag(cw_handle_, &alert);
        
        uint16_t soc_int_part = battery_level_;
        uint16_t soc_frac_part_1e4 = 0;
        if (err == ESP_OK && soc_raw != 0) {
            // SOC低8位是1/256%，这里换算为4位小数并四舍五入。
            uint8_t soc_frac_raw = (uint8_t)(soc_raw & 0xFF);
            soc_int_part = (uint16_t)(soc_raw >> 8);
            soc_frac_part_1e4 = (uint16_t)(((uint32_t)soc_frac_raw * 10000 + 128) / 256);
            if (soc_frac_part_1e4 >= 10000) {
                soc_int_part += 1;
                soc_frac_part_1e4 -= 10000;
            }
            if (soc_int_part > 100) {
                soc_int_part = 100;
                soc_frac_part_1e4 = 0;
            }
        }

        // 降低日志打印频率，保持约1秒打印一次
        if (++print_cnt_ >= 2) {
            print_cnt_ = 0;
            // ESP_LOGI("PowerManager", "SOC: %u.%04u%%, Voltage: %u mV, RRT: %u min, Alert: %d, Charging: %d", soc_int_part, soc_frac_part_1e4, voltage, rrt, alert, is_charging_);
        }
    }

public:
    PowerManager(i2c_master_bus_handle_t i2c_bus) {
        cw2015_config_t cfg = {
            .i2c_bus = i2c_bus,
            .i2c_addr = 0,
            .scl_speed_hz = 400000,
            .wake_on_create = true,
            .quick_start_on_create = true
        };

        cw_handle_ = cw2015_create(&cfg);
        if (!cw_handle_) {
            ESP_LOGE("PowerManager", "Failed to initialize CW2015");
            return;
        }
        
        // 芯片初始化校准步骤
        ESP_LOGI("PowerManager", "Starting battery calibration...");
        
        // 1. 等待芯片稳定
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // 设置低电量告警阈值（10%）
        cw2015_set_alert_threshold_percent(cw_handle_, 10);
        
        // 2. 读取初始电池数据
        uint16_t voltage = 0;
        cw2015_get_voltage_mv(cw_handle_, &voltage);
        
        uint8_t soc = 0;
        esp_err_t err = cw2015_get_soc_percent(cw_handle_, &soc);
        if (err != ESP_OK || soc == 0) {
            soc = CalculateBatteryLevelFromVoltage(voltage);
        }
        
        ESP_LOGI("PowerManager", "Initial battery data: Voltage: %u mV, SOC: %u%%", voltage, soc);
        
        // 4. 初始化时读取一次数据，确保成员变量有初始值
        CheckBatteryStatus();

        esp_timer_create_args_t timer_args = {
            .callback =
                [](void* arg) {
                    PowerManager* self = static_cast<PowerManager*>(arg);
                    self->CheckBatteryStatus();
                },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "battery_check_timer",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_handle_));
        // 250ms采样、8个样本，检测窗口仍约2秒，但抗偶发抖动能力更强。
        ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handle_, 250000));  // 250ms
    }

    ~PowerManager() {
        if (timer_handle_) {
            esp_timer_stop(timer_handle_);
            esp_timer_delete(timer_handle_);
        }
        if (cw_handle_) {
            cw2015_delete(cw_handle_);
        }
    }

    uint8_t GetBatteryLevel() const {
        return battery_level_;
    }

    bool GetBatteryLevelWithDecimal(uint16_t& int_part, uint16_t& frac_part_1e4) const {
        if (!cw_handle_) return false;

        uint16_t soc_raw = 0;
        esp_err_t err = cw2015_get_soc_raw(cw_handle_, &soc_raw);
        if (err != ESP_OK || soc_raw == 0) {
            int_part = battery_level_;
            frac_part_1e4 = 0;
            return true;
        }

        int_part = (uint16_t)(soc_raw >> 8);
        uint8_t soc_frac_raw = (uint8_t)(soc_raw & 0xFF);
        frac_part_1e4 = (uint16_t)(((uint32_t)soc_frac_raw * 10000 + 128) / 256);
        if (frac_part_1e4 >= 10000) {
            int_part += 1;
            frac_part_1e4 -= 10000;
        }
        if (int_part > 100) {
            int_part = 100;
            frac_part_1e4 = 0;
        }
        return true;
    }

    bool IsCharging() const {
        return is_charging_;
    }

    uint16_t GetVoltage() const {
        if (!cw_handle_) return 0;
        uint16_t voltage = 0;
        cw2015_get_voltage_mv(cw_handle_, &voltage);
        return voltage;
    }

    int16_t GetCurrent() const {
        // CW2015 不支持电流读取
        return 0;
    }

    bool IsBatteryFull() const {
        if (!cw_handle_) return false;
        
        // 读取电压
        uint16_t voltage = 0;
        cw2015_get_voltage_mv(cw_handle_, &voltage);
        
        uint8_t soc = 0;
        esp_err_t err = cw2015_get_soc_percent(cw_handle_, &soc);
        if (err != ESP_OK || soc == 0) {
            soc = CalculateBatteryLevelFromVoltage(voltage);
        }
        
        // 检查电池电量是否达到100% 且电压达到满充阈值（例如4100mV）
        if (soc >= 100 && voltage >= 4100) {
            return true;
        }
        
        return false;
    }

    // 暂停/恢复电量更新（用于动作执行时屏蔽更新）
    static void PauseBatteryUpdate() { battery_update_paused_ = true; }
    static void ResumeBatteryUpdate() { battery_update_paused_ = false; }
};

#endif // __POWER_MANAGER_H__
