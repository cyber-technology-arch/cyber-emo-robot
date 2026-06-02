/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_master_bus_handle_t i2c_bus;
    uint8_t i2c_addr;
    uint32_t scl_speed_hz;
    bool wake_on_create;
    bool quick_start_on_create;
} cw2015_config_t;

typedef void *cw2015_handle_t;

cw2015_handle_t cw2015_create(const cw2015_config_t *config);
esp_err_t cw2015_delete(cw2015_handle_t handle);

esp_err_t cw2015_get_version(cw2015_handle_t handle, uint8_t *out_version);
esp_err_t cw2015_get_voltage_mv(cw2015_handle_t handle, uint16_t *out_mv);
esp_err_t cw2015_get_soc_raw(cw2015_handle_t handle, uint16_t *out_soc_raw);
esp_err_t cw2015_get_soc_percent(cw2015_handle_t handle, uint8_t *out_percent);
esp_err_t cw2015_get_rrt_minutes(cw2015_handle_t handle, uint16_t *out_minutes);
esp_err_t cw2015_get_alert_flag(cw2015_handle_t handle, bool *out_alert);
esp_err_t cw2015_clear_alert_flag(cw2015_handle_t handle);
esp_err_t cw2015_get_alert_threshold_percent(cw2015_handle_t handle, uint8_t *out_threshold);
esp_err_t cw2015_set_alert_threshold_percent(cw2015_handle_t handle, uint8_t threshold);
esp_err_t cw2015_get_ufg_flag(cw2015_handle_t handle, bool *out_ufg);
esp_err_t cw2015_set_sleep(cw2015_handle_t handle, bool enable);
esp_err_t cw2015_quick_start(cw2015_handle_t handle);
esp_err_t cw2015_por_reset(cw2015_handle_t handle);

#ifdef __cplusplus
}
#endif
