#ifndef ESP32_CAMERA_H
#define ESP32_CAMERA_H

#include <string>
#include <esp_camera.h>
#include <lvgl.h>
#include <thread>
#include <memory>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "camera.h"

struct JpegChunk {
    uint8_t* data;
    size_t len;
};

class Esp32Camera : public Camera {
private:
    camera_fb_t* fb_ = nullptr;
    struct CapturedFrame {
        std::vector<uint8_t> data;
        size_t len = 0;
        int width = 0;
        int height = 0;
        pixformat_t format = PIXFORMAT_RGB565;
        bool valid = false;

        void reset() {
            data.clear();
            len = 0;
            width = 0;
            height = 0;
            valid = false;
        }
    };
    CapturedFrame last_frame_;
    std::string explain_url_;
    std::string explain_token_;
    std::thread encoder_thread_;
    int rotation_ = 0; // 0, 90, 180, 270

public:
    Esp32Camera(const camera_config_t& config);
    ~Esp32Camera();

    void SetRotation(int rotation) { rotation_ = rotation; }

    virtual void SetExplainUrl(const std::string& url, const std::string& token) override;
    virtual bool Capture() override;
    // 翻转控制函数
    virtual bool SetHMirror(bool enabled) override;
    virtual bool SetVFlip(bool enabled) override;
    virtual std::string Explain(const std::string& question) override;
};

#endif // ESP32_CAMERA_H