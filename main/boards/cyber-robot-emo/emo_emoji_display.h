#pragma once

#include "display/lcd_display.h"

/**
 * @brief Emo机器人GIF表情显示类
 * 继承SpiLcdDisplay，通过EmojiCollection添加GIF表情支持
 */
class EmoEmojiDisplay : public SpiLcdDisplay {
   public:
    /**
     * @brief 构造函数，参数与SpiLcdDisplay相同
     */
    EmoEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy);

    virtual ~EmoEmojiDisplay() override;
    virtual void SetStatus(const char* status) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void SetMusicInfo(const char* song_name) override;
    virtual void SetMusicProgress(int elapsed_ms, int total_ms) override;
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    virtual void ShowSuccessScreen(const char* message) override;

   private:
    void InitializeEmoEmojis();
    void SetupChatLabel();
    void SetupPreviewImage();
    void SetupLyricView();
    void ShowLyricView();
    void HideLyricView();
    void ClearLyricBuffers();

    void SetupStandbyClock();
    void StartStandbyClockDelay();
    void StopStandbyClockTimers();
    void ShowStandbyClock();
    void HideStandbyClock();
    void UpdateStandbyClock();

    static void StandbyClockDelayTimerCallback(void* arg);
    static void StandbyClockUpdateTimerCallback(void* arg);

    lv_obj_t* standby_clock_screen_ = nullptr;
    lv_obj_t* standby_clock_time_label_ = nullptr;
    lv_obj_t* standby_clock_minute_label_ = nullptr;
    lv_obj_t* standby_clock_date_label_ = nullptr;
    lv_obj_t* standby_clock_left_bar_ = nullptr;
    lv_obj_t* standby_clock_right_bar_ = nullptr;
    esp_timer_handle_t standby_clock_delay_timer_ = nullptr;
    esp_timer_handle_t standby_clock_update_timer_ = nullptr;
    bool standby_clock_visible_ = false;

    lv_obj_t* lyric_screen_ = nullptr;
    lv_obj_t* lyric_title_label_ = nullptr;
    lv_obj_t* lyric_artist_label_ = nullptr;
    lv_obj_t* lyric_lines_label_ = nullptr;
    lv_obj_t* lyric_progress_bg_ = nullptr;
    lv_obj_t* lyric_progress_fg_ = nullptr;
    lv_obj_t* lyric_time_label_ = nullptr;
    char* lyric_title_buffer_ = nullptr;
    char* lyric_artist_buffer_ = nullptr;
    char* lyric_lines_buffer_ = nullptr;
    char* lyric_time_buffer_ = nullptr;
    bool lyric_visible_ = false;
};
