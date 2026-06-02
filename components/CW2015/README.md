# CellWise CW2015

CW2015 是 CellWise 的低成本单节锂电电量计（Fuel Gauge）IC，提供电池端电压、SOC（电量百分比）与 RRT（剩余运行时间）等信息，并支持低电量告警与 Quick Start。该组件基于 ESP-IDF 的 I2C master bus 驱动实现。

该目录还包含一个工具 [sample-data-app](tools/sample-data-app)，用于以 CSV 形式周期打印 CW2015 数据。

## 功能范围（基于 datasheet）

- 读取：Version、VCELL（电压）、SOC（百分比与 1/256% 小数）、RRT、ALRT 标志。
- 配置：低电量告警阈值（0–31%）。
- 控制：Sleep/Wake、Quick Start、POR 复位。

说明：CW2015 不提供电流/温度/容量（mAh）等寄存器，本组件不会提供与这些无关的“伪实现”接口。

## 使用示例

```c
#include "driver/i2c_master.h"
#include "cw2015.h"

static i2c_master_bus_handle_t i2c_bus = NULL;
static cw2015_handle_t cw = NULL;

static void init_cw2015(void)
{
    i2c_master_bus_config_t conf = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_2,
        .scl_io_num = GPIO_NUM_1,
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
    cw = cw2015_create(&cfg);
}

static void read_cw2015(void)
{
    uint16_t mv = 0;
    uint8_t soc = 0;
    uint16_t rrt_min = 0;
    bool alrt = false;
    cw2015_get_voltage_mv(cw, &mv);
    cw2015_get_soc_percent(cw, &soc);
    cw2015_get_rrt_minutes(cw, &rrt_min);
    cw2015_get_alert_flag(cw, &alrt);
}
```

## API 说明

- 句柄生命周期：`cw2015_create()` / `cw2015_delete()`
- 电压：`cw2015_get_voltage_mv()`
- SOC：`cw2015_get_soc_percent()` / `cw2015_get_soc_raw()`
- RRT：`cw2015_get_rrt_minutes()`
- 告警：`cw2015_get_alert_flag()` / `cw2015_clear_alert_flag()` / `cw2015_get_alert_threshold_percent()` / `cw2015_set_alert_threshold_percent()`
- 控制：`cw2015_set_sleep()` / `cw2015_quick_start()` / `cw2015_por_reset()`
