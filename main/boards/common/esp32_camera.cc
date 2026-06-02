#include "esp32_camera.h"
#include <cstdio>
#include <string>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <lvgl.h>
#include <thread>
#include <memory>
#include <freertos/queue.h>

#include "mcp_server.h"
#include "display.h"
#include "board.h"
#include "system_info.h"
#include "lvgl_display.h"
#include "display/lvgl_display/jpg/image_to_jpeg.h"
#include <http.h>

#define TAG "Esp32Camera"

Esp32Camera::Esp32Camera(const camera_config_t& config) {
    // camera init
    esp_err_t err = esp_camera_init(&config); // 配置上面定义的参数
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return;
    }

    sensor_t *s = esp_camera_sensor_get(); // 获取摄像头型号
    if (s == nullptr) {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return;
    }
    
    if (s->id.PID == GC0308_PID) {
        s->set_hmirror(s, 0);  // 这里控制摄像头镜像 写1镜像 写0不镜像
    }
    
    // 优化OV2640传感器配置以减少条纹
    if (s->id.PID == OV2640_PID) {
        // 设置图像质量参数以减少条纹
        if (s->set_brightness) s->set_brightness(s, 0);  // 亮度
        if (s->set_contrast) s->set_contrast(s, 1);      // 对比度
        if (s->set_saturation) s->set_saturation(s, 0); // 饱和度
        if (s->set_sharpness) s->set_sharpness(s, 0);   // 锐度
        if (s->set_denoise) s->set_denoise(s, 0);       // 降噪
        if (s->set_gainceiling) s->set_gainceiling(s, GAINCEILING_2X); // 降低增益上限，减少噪声
        if (s->set_colorbar) s->set_colorbar(s, 0);     // 关闭彩条测试模式
        if (s->set_whitebal) s->set_whitebal(s, 1);      // 启用自动白平衡
        if (s->set_awb_gain) s->set_awb_gain(s, 1);      // 启用AWB增益
        if (s->set_wpc) s->set_wpc(s, 1);                // 启用白像素校正
        if (s->set_bpc) s->set_bpc(s, 1);                // 启用黑像素校正
        if (s->set_hmirror) s->set_hmirror(s, 0);        // 水平镜像
        if (s->set_vflip) s->set_vflip(s, 0);            // 垂直翻转
        ESP_LOGI(TAG, "OV2640 sensor configured to reduce stripes");
    }
}

Esp32Camera::~Esp32Camera() {
    if (fb_) {
        esp_camera_fb_return(fb_);
        fb_ = nullptr;
    }
    esp_camera_deinit();
}

void Esp32Camera::SetExplainUrl(const std::string& url, const std::string& token) {
    explain_url_ = url;
    explain_token_ = token;
}

bool Esp32Camera::Capture() {
    if (encoder_thread_.joinable()) {
        encoder_thread_.join();
    }

    last_frame_.reset();
    auto start_time = esp_timer_get_time();
    const int max_retries = 5; // 增加重试次数
    const int frames_to_get = 3; // 增加获取帧数，确保获取最新图像
    bool success = false;
    
    // 尝试多次获取帧
    for (int retry = 0; retry < max_retries && !success; retry++) {
        if (fb_ != nullptr) {
            esp_camera_fb_return(fb_);
            fb_ = nullptr;
        }
        
        // 尝试获取帧
        fb_ = esp_camera_fb_get();
        if (fb_ != nullptr) {
            // 验证帧的有效性
            if (fb_->len > 0 && fb_->buf != nullptr) {
                ESP_LOGI(TAG, "Camera captured frame successfully on retry %d, size: %zu bytes", retry + 1, fb_->len);
                success = true;
                
                // 获取额外的帧以确保获取到最新图像
                for (int i = 1; i < frames_to_get && success; i++) {
                    esp_camera_fb_return(fb_);
                    fb_ = esp_camera_fb_get();
                    if (fb_ == nullptr || fb_->len == 0 || fb_->buf == nullptr) {
                        success = false;
                        ESP_LOGW(TAG, "Failed to get stable frame");
                    }
                }
                
                if (success) {
                    ESP_LOGI(TAG, "Successfully captured %d consecutive frames, using the latest one", frames_to_get);
                }
            } else {
                ESP_LOGW(TAG, "Invalid frame data on retry %d", retry + 1);
                esp_camera_fb_return(fb_);
                fb_ = nullptr;
            }
        } else {
            ESP_LOGW(TAG, "Failed to get frame on retry %d", retry + 1);
        }
        
        // 如果失败，短暂延迟后重试
        if (!success && retry < max_retries - 1) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    
    if (!success) {
        ESP_LOGE(TAG, "Camera capture failed after %d retries", max_retries);
        return false;
    }
    
    auto end_time = esp_timer_get_time();
    ESP_LOGI(TAG, "Camera captured %d frames in %d ms", frames_to_get, int((end_time - start_time) / 1000));

    // 显示预览图片
    auto display = dynamic_cast<LvglDisplay*>(Board::GetInstance().GetDisplay());
    if (display != nullptr && fb_ != nullptr) {
        ESP_LOGI(TAG, "Display found, preparing to show preview image");
        
        // 修复照片显示延迟问题的优化解决方案
        // 1. 立即清除旧图像
        ESP_LOGI(TAG, "Step 1: Clear previous image");
        display->SetPreviewImage(nullptr);
        
        // 2. 强制刷新显示系统
        ESP_LOGI(TAG, "Step 2: Refresh display buffer");
        {
            DisplayLockGuard display_lock(display);
            lv_refr_now(NULL);
        }
        
        // 3. 添加适当延迟，确保旧图像清除
        ESP_LOGI(TAG, "Step 3: Adding delay for display cleanup");
        vTaskDelay(pdMS_TO_TICKS(30));
        
        // 5. 分配内存用于预览图像
        // 根据旋转角度调整输出宽高
        int out_width = fb_->width;
        int out_height = fb_->height;
        if (rotation_ == 90 || rotation_ == 270) {
            out_width = fb_->height;
            out_height = fb_->width;
        }

        auto data = (uint8_t*)heap_caps_malloc(fb_->len, MALLOC_CAP_SPIRAM);
        if (data == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate memory for preview image");
            esp_camera_fb_return(fb_);
            fb_ = nullptr;
            return false;
        }
        
        ESP_LOGI(TAG, "Allocated %zu bytes for preview image, rotation=%d", fb_->len, rotation_);

        if (fb_->format == PIXFORMAT_RGB565) {
            ESP_LOGI(TAG, "Processing RGB565 image, dimensions: %d x %d", fb_->width, fb_->height);
            // 使用内存对齐的复制和字节序转换，避免条纹
            auto src = (uint16_t*)fb_->buf;
            auto dst = (uint16_t*)data;
            size_t pixel_count = fb_->len / 2;
            
            if (rotation_ == 0) {
                // 确保数据对齐，使用优化的字节序转换
                // 对于ESP32-S3，需要交换字节序以匹配LVGL的RGB565格式
                for (size_t i = 0; i < pixel_count; i++) {
                    uint16_t pixel = src[i];
                    // 交换字节序：将大端序转换为小端序
                    dst[i] = ((pixel & 0xFF) << 8) | ((pixel & 0xFF00) >> 8);
                }
            } else if (rotation_ == 90) { // Clockwise 90
                for (int y = 0; y < fb_->height; y++) {
                    for (int x = 0; x < fb_->width; x++) {
                        int src_idx = y * fb_->width + x;
                        int dst_x = fb_->height - 1 - y;
                        int dst_y = x;
                        int dst_idx = dst_y * out_width + dst_x;
                        
                        uint16_t pixel = src[src_idx];
                        dst[dst_idx] = ((pixel & 0xFF) << 8) | ((pixel & 0xFF00) >> 8);
                    }
                }
            } else if (rotation_ == 180) {
                for (size_t i = 0; i < pixel_count; i++) {
                    uint16_t pixel = src[pixel_count - 1 - i];
                    dst[i] = ((pixel & 0xFF) << 8) | ((pixel & 0xFF00) >> 8);
                }
            } else if (rotation_ == 270) { // Counter-Clockwise 90
                for (int y = 0; y < fb_->height; y++) {
                    for (int x = 0; x < fb_->width; x++) {
                        int src_idx = y * fb_->width + x;
                        int dst_x = y;
                        int dst_y = fb_->width - 1 - x;
                        int dst_idx = dst_y * out_width + dst_x;
                        
                        uint16_t pixel = src[src_idx];
                        dst[dst_idx] = ((pixel & 0xFF) << 8) | ((pixel & 0xFF00) >> 8);
                    }
                }
            }
        } else {
            // 对于其他格式，直接复制数据 (暂不支持旋转)
            ESP_LOGI(TAG, "Processing image format: %d, dimensions: %d x %d", fb_->format, fb_->width, fb_->height);
            memcpy(data, fb_->buf, fb_->len);
        }

        // 创建图像对象并显示
        ESP_LOGI(TAG, "Creating LvglAllocatedImage with: width=%d, height=%d, stride=%d, color_format=%d", 
                 out_width, out_height, out_width * 2, LV_COLOR_FORMAT_RGB565);
        
        auto image = std::make_unique<LvglAllocatedImage>(data, fb_->len, out_width, out_height, out_width * 2, LV_COLOR_FORMAT_RGB565);
        
        // 检查图像是否有效
        auto img_dsc = image->image_dsc();
        if (img_dsc && img_dsc->data && img_dsc->header.w > 0 && img_dsc->header.h > 0) {
            ESP_LOGI(TAG, "Image is valid (data=%p, w=%d, h=%d), calling SetPreviewImage", 
                     img_dsc->data, img_dsc->header.w, img_dsc->header.h);
            
            // 直接调用display的SetPreviewImage方法来显示图片
            display->SetPreviewImage(std::move(image));
            
            // 短暂延迟确保图像数据完全准备好
            ESP_LOGI(TAG, "Step 4: Waiting for image data to stabilize");
            vTaskDelay(pdMS_TO_TICKS(20));
            
            // 触发显示更新，确保图像正确显示
            ESP_LOGI(TAG, "Step 5: Requesting stable display updates");
            for (int i = 0; i < 2; i++) {
                {
                    DisplayLockGuard display_lock(display);
                    lv_display_trigger_activity(NULL);
                }
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            
            // 确保图片会一直显示，不自动隐藏
            ESP_LOGI(TAG, "Preview image fully displayed and stabilized");
        } else {
            // 提前计算宽度和高度，避免在ESP_LOGE宏中使用条件运算符
            int width = img_dsc ? img_dsc->header.w : -1;
            int height = img_dsc ? img_dsc->header.h : -1;
            ESP_LOGE(TAG, "Invalid image data or dimensions (w=%d, h=%d)", width, height);
            heap_caps_free(data);
            esp_camera_fb_return(fb_);
            fb_ = nullptr;
            return false;
        }
        
        ESP_LOGI(TAG, "Preview image displayed successfully");
    } else {
        ESP_LOGE(TAG, "Display is null (%p) or frame buffer is null (%p)", display, fb_);
    }

    if (fb_ != nullptr) {
        last_frame_.data.resize(fb_->len);
        if (!last_frame_.data.empty()) {
            memcpy(last_frame_.data.data(), fb_->buf, fb_->len);
        }
        last_frame_.len = fb_->len;
        last_frame_.width = fb_->width;
        last_frame_.height = fb_->height;
        last_frame_.format = fb_->format;
        last_frame_.valid = true;

        esp_camera_fb_return(fb_);
        fb_ = nullptr;
    }

    return true;
}

bool Esp32Camera::SetHMirror(bool enabled) {
    sensor_t *s = esp_camera_sensor_get();
    if (s == nullptr) {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return false;
    }
    
    esp_err_t err = s->set_hmirror(s, enabled);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set horizontal mirror: %d", err);
        return false;
    }
    
    ESP_LOGI(TAG, "Camera horizontal mirror set to: %s", enabled ? "enabled" : "disabled");
    return true;
}

bool Esp32Camera::SetVFlip(bool enabled) {
    sensor_t *s = esp_camera_sensor_get();
    if (s == nullptr) {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return false;
    }
    
    esp_err_t err = s->set_vflip(s, enabled);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set vertical flip: %d", err);
        return false;
    }
    
    ESP_LOGI(TAG, "Camera vertical flip set to: %s", enabled ? "enabled" : "disabled");
    return true;
}

/**
 * @brief 将摄像头捕获的图像发送到远程服务器进行AI分析和解释
 *
 * 该函数将当前摄像头缓冲区中的图像编码为JPEG格式，并通过HTTP POST请求
 * 以multipart/form-data的形式发送到指定的解释服务器。服务器将根据提供的
 * 问题对图像进行AI分析并返回结果。
 *
 * 实现特点：
 * - 使用独立线程编码JPEG，与主线程分离
 * - 采用分块传输编码(chunked transfer encoding)优化内存使用
 * - 通过队列机制实现编码线程和发送线程的数据同步
 * - 支持设备ID、客户端ID和认证令牌的HTTP头部配置
 *
 * @param question 要向AI提出的关于图像的问题，将作为表单字段发送
 * @return std::string 服务器返回的JSON格式响应字符串
 *         成功时包含AI分析结果，失败时包含错误信息
 *         格式示例：{"success": true, "result": "分析结果"}
 *                  {"success": false, "message": "错误信息"}
 *
 * @note 调用此函数前必须先调用SetExplainUrl()设置服务器URL
 * @note 函数会等待之前的编码线程完成后再开始新的处理
 * @warning 如果摄像头缓冲区为空或网络连接失败，将返回错误信息
 */

std::string Esp32Camera::Explain(const std::string& question) {
    if (explain_url_.empty()) {
        throw std::runtime_error("Image explain URL or token is not set");
    }

    // 创建局部的 JPEG 队列, 40 entries is about to store 512 * 40 = 20480 bytes of JPEG data
    QueueHandle_t jpeg_queue = xQueueCreate(40, sizeof(JpegChunk));
    if (jpeg_queue == nullptr) {
        ESP_LOGE(TAG, "Failed to create JPEG queue");
        throw std::runtime_error("Failed to create JPEG queue");
    }

    // 检查帧缓冲区是否有效
    if (!last_frame_.valid || last_frame_.data.empty() || last_frame_.len == 0) {
        ESP_LOGE(TAG, "No captured frame available, cannot encode image");
        vQueueDelete(jpeg_queue);
        throw std::runtime_error("Frame buffer is null");
    }

    // 在创建线程前保存帧数据的当前值到局部变量，防止在多线程环境中被主线程修改
    uint8_t* frame_data = last_frame_.data.data();
    size_t frame_len = last_frame_.len;
    int frame_width = last_frame_.width;
    int frame_height = last_frame_.height;
    pixformat_t frame_format = last_frame_.format;

    // We spawn a thread to encode the image to JPEG using optimized encoder (cost about 500ms and 8KB SRAM)
    encoder_thread_ = std::thread([frame_data, frame_len, frame_width, frame_height, frame_format, jpeg_queue]() {
        // 使用局部变量访问帧缓冲区，避免空指针解引用
        if (frame_data != nullptr && frame_len > 0) {
            image_to_jpeg_cb(frame_data, frame_len, frame_width, frame_height, frame_format, 80,
                [](void* arg, size_t index, const void* data, size_t len) -> size_t {
                auto jpeg_queue = (QueueHandle_t)arg;
                if (data == nullptr || len == 0) {
                    return len;
                }
                JpegChunk chunk = {
                    .data = (uint8_t*)heap_caps_aligned_alloc(16, len, MALLOC_CAP_SPIRAM),
                    .len = len
                };
                if (chunk.data == nullptr) {
                    ESP_LOGE(TAG, "Failed to allocate aligned buffer for JPEG chunk, size=%zu", len);
                    return 0;
                }
                memcpy(chunk.data, data, len);
                if (xQueueSend(jpeg_queue, &chunk, portMAX_DELAY) != pdPASS) {
                    ESP_LOGE(TAG, "Failed to enqueue JPEG chunk");
                    heap_caps_free(chunk.data);
                    return 0;
                }
                return len;
            }, jpeg_queue);
        }
        JpegChunk end_chunk = {
            .data = nullptr,
            .len = 0
        };
        xQueueSend(jpeg_queue, &end_chunk, portMAX_DELAY);
    });

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(3);
    // 构造multipart/form-data请求体
    std::string boundary = "----ESP32_CAMERA_BOUNDARY";

    // 配置HTTP客户端，使用分块传输编码
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
    if (!explain_token_.empty()) {
        http->SetHeader("Authorization", "Bearer " + explain_token_);
    }
    http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http->SetHeader("Transfer-Encoding", "chunked");
    if (!http->Open("POST", explain_url_)) {
        ESP_LOGE(TAG, "Failed to connect to explain URL");
        // Clear the queue
        encoder_thread_.join();
        JpegChunk chunk;
        while (xQueueReceive(jpeg_queue, &chunk, portMAX_DELAY) == pdPASS) {
            if (chunk.data != nullptr) {
                heap_caps_free(chunk.data);
            } else {
                break;
            }
        }
        vQueueDelete(jpeg_queue);
        throw std::runtime_error("Failed to connect to explain URL");
    }
    
    {
        // 第一块：question字段
        std::string question_field;
        question_field += "--" + boundary + "\r\n";
        question_field += "Content-Disposition: form-data; name=\"question\"\r\n";
        question_field += "\r\n";
        question_field += question + "\r\n";
        http->Write(question_field.c_str(), question_field.size());
    }
    {
        // 第二块：文件字段头部
        std::string file_header;
        file_header += "--" + boundary + "\r\n";
        file_header += "Content-Disposition: form-data; name=\"file\"; filename=\"camera.jpg\"\r\n";
        file_header += "Content-Type: image/jpeg\r\n";
        file_header += "\r\n";
        http->Write(file_header.c_str(), file_header.size());
    }

    // 第三块：JPEG数据
    size_t total_sent = 0;
    while (true) {
        JpegChunk chunk;
        if (xQueueReceive(jpeg_queue, &chunk, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG, "Failed to receive JPEG chunk");
            break;
        }
        if (chunk.data == nullptr) {
            break; // The last chunk
        }
        http->Write((const char*)chunk.data, chunk.len);
        total_sent += chunk.len;
        heap_caps_free(chunk.data);
    }
    // Wait for the encoder thread to finish
    encoder_thread_.join();
    // 清理队列
    vQueueDelete(jpeg_queue);

    {
        // 第四块：multipart尾部
        std::string multipart_footer;
        multipart_footer += "\r\n--" + boundary + "--\r\n";
        http->Write(multipart_footer.c_str(), multipart_footer.size());
    }
    // 结束块
    http->Write("", 0);

    if (http->GetStatusCode() != 200) {
        ESP_LOGE(TAG, "Failed to upload photo, status code: %d", http->GetStatusCode());
        throw std::runtime_error("Failed to upload photo");
    }

    std::string result = http->ReadAll();
    http->Close();

    // Get remain task stack size
    size_t remain_stack_size = uxTaskGetStackHighWaterMark(nullptr);
    ESP_LOGI(TAG, "Explain image size=%dx%d, compressed size=%d, remain stack size=%d, question=%s\n%s",
        frame_width, frame_height, total_sent, remain_stack_size, question.c_str(), result.c_str());
    
    last_frame_.reset();

    return result;
}
