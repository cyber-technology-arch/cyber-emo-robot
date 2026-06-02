#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define RIGHT_LEG_PIN GPIO_NUM_43
#define RIGHT_FOOT_PIN GPIO_NUM_44
#define LEFT_LEG_PIN GPIO_NUM_1
#define LEFT_FOOT_PIN GPIO_NUM_2

#define AUDIO_INPUT_SAMPLE_RATE  24000   
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
#define AUDIO_INPUT_REFERENCE    true

#define WS2812_RGB                      GPIO_NUM_5

#define AUDIO_I2S_GPIO_MCLK             GPIO_NUM_17
#define AUDIO_I2S_GPIO_WS               GPIO_NUM_18
#define AUDIO_I2S_GPIO_BCLK             GPIO_NUM_15
#define AUDIO_I2S_GPIO_DOUT             GPIO_NUM_8
#define AUDIO_I2S_GPIO_DIN              GPIO_NUM_16
#define AUDIO_CODEC_PA_PIN              GPIO_NUM_4
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR
#define AUDIO_CODEC_ES7210_ADDR  ES7210_CODEC_DEFAULT_ADDR

#define AUDIO_CODEC_I2C_SDA_PIN         GPIO_NUM_7
#define AUDIO_CODEC_I2C_SCL_PIN         GPIO_NUM_6

#if 1
#define CAMERA_XCLK                     GPIO_NUM_21
#define CAMERA_PCLK                     GPIO_NUM_45
#define CAMERA_VSYNC                    GPIO_NUM_3
#define CAMERA_HSYNC                    GPIO_NUM_46
#define CAMERA_D0                       GPIO_NUM_39
#define CAMERA_D1                       GPIO_NUM_41
#define CAMERA_D2                       GPIO_NUM_42
#define CAMERA_D3                       GPIO_NUM_40
#define CAMERA_D4                       GPIO_NUM_38
#define CAMERA_D5                       GPIO_NUM_48
#define CAMERA_D6                       GPIO_NUM_47
#define CAMERA_D7                       GPIO_NUM_9
#else
#define CAMERA_XCLK                     GPIO_NUM_39
#define CAMERA_PCLK                     GPIO_NUM_48
#define CAMERA_VSYNC                    GPIO_NUM_42
#define CAMERA_HSYNC                    GPIO_NUM_41
#define CAMERA_D0                       GPIO_NUM_21
#define CAMERA_D1                       GPIO_NUM_46
#define CAMERA_D2                       GPIO_NUM_3
#define CAMERA_D3                       GPIO_NUM_9
#define CAMERA_D4                       GPIO_NUM_47
#define CAMERA_D5                       GPIO_NUM_45
#define CAMERA_D6                       GPIO_NUM_38
#define CAMERA_D7                       GPIO_NUM_40
#endif
#define CAMERA_PWDN                     GPIO_NUM_NC
#define CAMERA_RESET                    GPIO_NUM_NC

#define XCLK_FREQ_HZ                    20000000

#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_10
#define DISPLAY_MOSI_PIN GPIO_NUM_13
#define DISPLAY_CLK_PIN GPIO_NUM_12
#define DISPLAY_DC_PIN GPIO_NUM_11
#define DISPLAY_RST_PIN GPIO_NUM_14
#define DISPLAY_CS_PIN GPIO_NUM_NC

#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 240
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR true
#define DISPLAY_RGB_ORDER LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X 0
#define DISPLAY_OFFSET_Y 0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 3

#define BOOT_BUTTON_GPIO GPIO_NUM_0

#define TOUCH_PAD1 BOOT_BUTTON_GPIO
#define TOUCH_WAKE_WORD "我在摸你可爱头"

#define EMO_ROBOT_VERSION "2.0.5"

#endif  // _BOARD_CONFIG_H_
