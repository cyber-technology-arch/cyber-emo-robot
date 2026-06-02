/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include <string.h>
#include "cw2015.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "priv_include/cw2015_reg.h"

static const char *TAG = "cw2015";

#define delay_ms(x) vTaskDelay(pdMS_TO_TICKS(x))

typedef struct {
    i2c_master_dev_handle_t i2c_dev;
    uint8_t i2c_addr;
} cw2015_data_t;

static esp_err_t cw2015_read(cw2015_handle_t handle, uint8_t reg, uint8_t *buf, size_t len)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "句柄无效");
    cw2015_data_t *d = (cw2015_data_t *)handle;
    return i2c_master_transmit_receive(d->i2c_dev, &reg, 1, buf, len, -1);
}

static esp_err_t cw2015_write(cw2015_handle_t handle, uint8_t reg, const uint8_t *buf, size_t len)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "句柄无效");
    cw2015_data_t *d = (cw2015_data_t *)handle;
    uint8_t tmp[1 + 8];
    if (len <= 8) {
        tmp[0] = reg;
        memcpy(&tmp[1], buf, len);
        return i2c_master_transmit(d->i2c_dev, tmp, 1 + len, -1);
    }

    uint8_t *out = (uint8_t *)malloc(1 + len);
    ESP_RETURN_ON_FALSE(out, ESP_ERR_NO_MEM, TAG, "内存不足");
    out[0] = reg;
    memcpy(&out[1], buf, len);
    esp_err_t ret = i2c_master_transmit(d->i2c_dev, out, 1 + len, -1);
    free(out);
    return ret;
}

static esp_err_t cw2015_read_u16_be(cw2015_handle_t handle, uint8_t reg, uint16_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "参数无效");
    uint8_t b[2] = {0};
    ESP_RETURN_ON_ERROR(cw2015_read(handle, reg, b, sizeof(b)), TAG, "I2C读取失败");
    *out = ((uint16_t)b[0] << 8) | b[1];
    return ESP_OK;
}

cw2015_handle_t cw2015_create(const cw2015_config_t *config)
{
    ESP_RETURN_ON_FALSE(config, NULL, TAG, "配置无效");
    ESP_RETURN_ON_FALSE(config->i2c_bus, NULL, TAG, "i2c总线无效");

    cw2015_data_t *handle = (cw2015_data_t *)calloc(1, sizeof(cw2015_data_t));
    if (!handle) {
        ESP_LOGE(TAG, "内存分配失败");
        return NULL;
    }

    uint8_t addr = config->i2c_addr ? config->i2c_addr : CW2015_I2C_ADDR_DEFAULT;
    uint32_t speed = config->scl_speed_hz ? config->scl_speed_hz : 400000;
    handle->i2c_addr = addr;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = speed,
    };

    if (i2c_master_bus_add_device(config->i2c_bus, &dev_cfg, &handle->i2c_dev) != ESP_OK) {
        ESP_LOGE(TAG, "i2c设备添加失败");
        free(handle);
        return NULL;
    }

    uint8_t version = 0;
    esp_err_t err = cw2015_get_version(handle, &version);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "读取版本失败: %s", esp_err_to_name(err));
        i2c_master_bus_rm_device(handle->i2c_dev);
        free(handle);
        return NULL;
    }

    ESP_LOGI(TAG, "CW2015 版本: 0x%02X, I2C地址: 0x%02X", version, addr);

    if (config->wake_on_create) {
        err = cw2015_set_sleep(handle, false);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "唤醒失败: %s", esp_err_to_name(err));
        } else {
            delay_ms(20);
        }
    }

    if (config->quick_start_on_create) {
        err = cw2015_quick_start(handle);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "快速启动失败: %s", esp_err_to_name(err));
        } else {
            delay_ms(20);
        }
    }

    return handle;
}

esp_err_t cw2015_delete(cw2015_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "句柄无效");
    cw2015_data_t *d = (cw2015_data_t *)handle;
    if (d->i2c_dev) {
        i2c_master_bus_rm_device(d->i2c_dev);
    }
    free(handle);
    return ESP_OK;
}

esp_err_t cw2015_get_version(cw2015_handle_t handle, uint8_t *out_version)
{
    ESP_RETURN_ON_FALSE(out_version, ESP_ERR_INVALID_ARG, TAG, "参数无效");
    return cw2015_read(handle, CW2015_REG_VERSION, out_version, 1);
}

esp_err_t cw2015_get_voltage_mv(cw2015_handle_t handle, uint16_t *out_mv)
{
    ESP_RETURN_ON_FALSE(out_mv, ESP_ERR_INVALID_ARG, TAG, "参数无效");
    uint16_t raw = 0;
    ESP_RETURN_ON_ERROR(cw2015_read_u16_be(handle, CW2015_REG_VCELL, &raw), TAG, "读取VCELL失败");
    raw &= CW2015_VCELL_RAW_MASK;
    uint32_t uv = (uint32_t)raw * 305;
    *out_mv = (uint16_t)((uv + 500) / 1000);
    return ESP_OK;
}

esp_err_t cw2015_get_soc_raw(cw2015_handle_t handle, uint16_t *out_soc_raw)
{
    return cw2015_read_u16_be(handle, CW2015_REG_SOC, out_soc_raw);
}

esp_err_t cw2015_get_soc_percent(cw2015_handle_t handle, uint8_t *out_percent)
{
    ESP_RETURN_ON_FALSE(out_percent, ESP_ERR_INVALID_ARG, TAG, "参数无效");
    uint16_t raw = 0;
    ESP_RETURN_ON_ERROR(cw2015_get_soc_raw(handle, &raw), TAG, "读取SOC失败");
    uint8_t pct = (uint8_t)(raw >> 8);
    if (pct > 100) pct = 100;
    *out_percent = pct;
    return ESP_OK;
}

esp_err_t cw2015_get_rrt_minutes(cw2015_handle_t handle, uint16_t *out_minutes)
{
    ESP_RETURN_ON_FALSE(out_minutes, ESP_ERR_INVALID_ARG, TAG, "参数无效");
    uint16_t raw = 0;
    ESP_RETURN_ON_ERROR(cw2015_read_u16_be(handle, CW2015_REG_RRT_ALRT, &raw), TAG, "读取RRT失败");
    *out_minutes = raw & CW2015_RRT_MASK;
    return ESP_OK;
}

esp_err_t cw2015_get_alert_flag(cw2015_handle_t handle, bool *out_alert)
{
    ESP_RETURN_ON_FALSE(out_alert, ESP_ERR_INVALID_ARG, TAG, "参数无效");
    uint16_t raw = 0;
    ESP_RETURN_ON_ERROR(cw2015_read_u16_be(handle, CW2015_REG_RRT_ALRT, &raw), TAG, "读取RRT/ALRT失败");
    *out_alert = (raw & CW2015_ALRT_MASK) != 0;
    return ESP_OK;
}

esp_err_t cw2015_clear_alert_flag(cw2015_handle_t handle)
{
    uint16_t raw = 0;
    esp_err_t err = cw2015_read_u16_be(handle, CW2015_REG_RRT_ALRT, &raw);
    if (err != ESP_OK) {
        raw = 0;
    }
    uint8_t b[2];
    b[0] = (uint8_t)((raw >> 8) & 0x7F);
    b[1] = (uint8_t)(raw & 0xFF);
    return cw2015_write(handle, CW2015_REG_RRT_ALRT, b, sizeof(b));
}

esp_err_t cw2015_get_alert_threshold_percent(cw2015_handle_t handle, uint8_t *out_threshold)
{
    ESP_RETURN_ON_FALSE(out_threshold, ESP_ERR_INVALID_ARG, TAG, "参数无效");
    uint8_t cfg = 0;
    ESP_RETURN_ON_ERROR(cw2015_read(handle, CW2015_REG_CONFIG, &cfg, 1), TAG, "读取CONFIG失败");
    *out_threshold = (cfg & CW2015_CONFIG_ATHD_MASK) >> CW2015_CONFIG_ATHD_SHIFT;
    return ESP_OK;
}

esp_err_t cw2015_set_alert_threshold_percent(cw2015_handle_t handle, uint8_t threshold)
{
    if (threshold > 31) threshold = 31;
    uint8_t cfg = 0;
    ESP_RETURN_ON_ERROR(cw2015_read(handle, CW2015_REG_CONFIG, &cfg, 1), TAG, "读取CONFIG失败");
    uint8_t out = (cfg & CW2015_CONFIG_UFG_MASK) | ((threshold & 0x1F) << CW2015_CONFIG_ATHD_SHIFT);
    return cw2015_write(handle, CW2015_REG_CONFIG, &out, 1);
}

esp_err_t cw2015_get_ufg_flag(cw2015_handle_t handle, bool *out_ufg)
{
    ESP_RETURN_ON_FALSE(out_ufg, ESP_ERR_INVALID_ARG, TAG, "参数无效");
    uint8_t cfg = 0;
    ESP_RETURN_ON_ERROR(cw2015_read(handle, CW2015_REG_CONFIG, &cfg, 1), TAG, "读取CONFIG失败");
    *out_ufg = (cfg & CW2015_CONFIG_UFG_MASK) != 0;
    return ESP_OK;
}

esp_err_t cw2015_set_sleep(cw2015_handle_t handle, bool enable)
{
    uint8_t mode = enable ? CW2015_MODE_SLEEP : CW2015_MODE_WAKE;
    return cw2015_write(handle, CW2015_REG_MODE, &mode, 1);
}

esp_err_t cw2015_quick_start(cw2015_handle_t handle)
{
    uint8_t mode = CW2015_MODE_WAKE | CW2015_MODE_QSTRT;
    ESP_RETURN_ON_ERROR(cw2015_write(handle, CW2015_REG_MODE, &mode, 1), TAG, "写入MODE失败");
    delay_ms(10);
    mode = CW2015_MODE_WAKE;
    return cw2015_write(handle, CW2015_REG_MODE, &mode, 1);
}

esp_err_t cw2015_por_reset(cw2015_handle_t handle)
{
    uint8_t mode = CW2015_MODE_POR;
    return cw2015_write(handle, CW2015_REG_MODE, &mode, 1);
}
