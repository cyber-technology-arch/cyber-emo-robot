#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <wifi_station.h>

#include "application.h"
#include "codecs/box_audio_codec.h"
#include "config.h"
#include "display/lcd_display.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "mcp_server.h"
#include "emo_emoji_display.h"
#include "power_manager.h"
#include "system_reset.h"
#include "wifi_board.h"
#include "esp32_camera.h"
#include "cyber_led.h"
#include "touch_sensor.h"

#define TAG "EmoRobot"

extern void InitializeEmoController();

class EmoRobot : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    LcdDisplay* display_;
    PowerManager* power_manager_;
    TouchSensor touch_sensor_;
    Esp32Camera* camera_;
    CyberLed* led_;

    void InitializePowerManager() {
        power_manager_ = new PowerManager(i2c_bus_);
    }

    void InitializeI2c()
    {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;

        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        display_ = new EmoEmojiDisplay(
            panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeEmoController() {
        ESP_LOGI(TAG, "初始化Emo机器人MCP控制器");
        ::InitializeEmoController();
    }

    void StartNetwork() override {
        WifiBoard::StartNetwork();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    void InitializeCamera() {
        camera_config_t config = {};
        config.ledc_channel = LEDC_CHANNEL_2;   // LEDC通道选择  用于生成XCLK时钟 但是S3不用
        config.ledc_timer = LEDC_TIMER_2;       // LEDC timer选择  用于生成XCLK时钟 但是S3不用
        config.pin_d0 = CAMERA_D0;
        config.pin_d1 = CAMERA_D1;
        config.pin_d2 = CAMERA_D2;
        config.pin_d3 = CAMERA_D3;
        config.pin_d4 = CAMERA_D4;
        config.pin_d5 = CAMERA_D5;
        config.pin_d6 = CAMERA_D6;
        config.pin_d7 = CAMERA_D7;
        config.pin_xclk = CAMERA_XCLK;
        config.pin_pclk = CAMERA_PCLK;
        config.pin_vsync = CAMERA_VSYNC;
        config.pin_href = CAMERA_HSYNC;
        config.pin_sccb_sda = -1;  // 这里如果写-1 表示使用已经初始化的I2C接口
        config.pin_sccb_scl = -1;
        config.sccb_i2c_port = I2C_NUM_0;               //  这里如果写1 默认使用I2C1
        config.pin_pwdn = CAMERA_PWDN;
        config.pin_reset = CAMERA_RESET;
        config.xclk_freq_hz = XCLK_FREQ_HZ;
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

        camera_ = new Esp32Camera(config);
        camera_->SetVFlip(0);
        // camera_->SetRotation(90);
        camera_->SetRotation(270);

    }

public:
    EmoRobot() {
        led_ = new CyberLed(WS2812_RGB);
        InitializeI2c();
        InitializeSpi();
        InitializeLcdDisplay();
        touch_sensor_.init();
        InitializePowerManager();
        InitializeCamera();
        InitializeEmoController();
        GetBacklight()->RestoreBrightness();
        
        // 启动电池信息显示，每2秒更新一次
        // StartBatteryDisplay();
    }

    virtual Led* GetLed() override {
        return led_;
    }

    virtual AudioCodec* GetAudioCodec() override{
        static BoxAudioCodec audio_codec(i2c_bus_,AUDIO_INPUT_SAMPLE_RATE,AUDIO_OUTPUT_SAMPLE_RATE,
                                        AUDIO_I2S_GPIO_MCLK,AUDIO_I2S_GPIO_BCLK,AUDIO_I2S_GPIO_WS,
                                        AUDIO_I2S_GPIO_DOUT,AUDIO_I2S_GPIO_DIN,AUDIO_CODEC_PA_PIN,
                                        AUDIO_CODEC_ES8311_ADDR,AUDIO_CODEC_ES7210_ADDR,AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        charging = power_manager_->IsCharging();
        discharging = !charging;
        level = power_manager_->GetBatteryLevel();
        return true;
    }
    virtual Camera* GetCamera() override {
        return camera_;
    }
    
    virtual void* GetPowerManager() override {
        return power_manager_;
    }

    // void TestBatteryDisplay() {
    //     auto& board = Board::GetInstance();
    //     auto display = board.GetDisplay();
    //     if (!display) return;

    //     uint16_t voltage = power_manager_->GetVoltage();
    //     int16_t current = power_manager_->GetCurrent();
    //     uint16_t soc_int, soc_frac;
    //     if (power_manager_->GetBatteryLevelWithDecimal(soc_int, soc_frac)) {
    //         char buffer[100];
    //         snprintf(buffer, sizeof(buffer), "电压: %d mV\n电流: %d mA\n电量: %u.%04u%%", voltage, current, soc_int, soc_frac);
    //         display->SetChatMessage("系统", buffer);
    //     }
    // }

    // // 电池信息更新定时器回调
    // static void BatteryUpdateTimerCallback(void* arg) {
    //     auto robot = static_cast<EmoRobot*>(arg);
    //     robot->TestBatteryDisplay();
    // }

    // // 启动电池信息更新
    // void StartBatteryDisplay() {
    //     // 使用Application调度，确保在所有初始化完成后再启动
    //     Application::GetInstance().Schedule([this]() {
    //         // 创建定时器，每2秒更新一次电池信息
    //         esp_timer_handle_t timer_handle;
    //         esp_timer_create_args_t timer_args = {
    //             .callback = BatteryUpdateTimerCallback,
    //             .arg = this,
    //             .dispatch_method = ESP_TIMER_TASK,
    //             .name = "battery_display_timer",
    //             .skip_unhandled_events = true,
    //         };
    //         esp_timer_create(&timer_args, &timer_handle);
    //         esp_timer_start_periodic(timer_handle, 1000000); // 2秒
            
    //         // 立即执行一次，显示初始电池信息
    //         TestBatteryDisplay();
    //     });
    // }
};

DECLARE_BOARD(EmoRobot);
