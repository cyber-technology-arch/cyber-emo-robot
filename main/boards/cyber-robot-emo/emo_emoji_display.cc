#include "emo_emoji_display.h"

#include <esp_log.h>
#include <esp_heap_caps.h>

#include <cstring>
#include <ctime>
#include <cstdio>
#include <string>

#include "assets/lang_config.h"
#include "display/lvgl_display/emoji_collection.h"
#include "display/lvgl_display/lvgl_image.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "otto_emoji_gif.h"

#define TAG "EmoEmojiDisplay"

LV_FONT_DECLARE(font_puhui_basic_30_4);
LV_FONT_DECLARE(lv_font_montserrat_48);
LV_FONT_DECLARE(lv_fanye);
LV_FONT_DECLARE(lv_fanye_18);
LV_FONT_DECLARE(EMO_ICON_FONT);

namespace {
constexpr size_t kLyricTitleBufferSize = 96;
constexpr size_t kLyricArtistBufferSize = 96;
constexpr size_t kLyricLinesBufferSize = 768;
constexpr size_t kLyricTimeBufferSize = 24;

char* AllocPsramTextBuffer(size_t size, const char* name) {
    char* buffer = static_cast<char*>(heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate %s in PSRAM (%u bytes)", name, static_cast<unsigned>(size));
    }
    return buffer;
}

void CopyText(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        src = "";
    }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

void SetLabelTextPreferPsram(lv_obj_t* label, char* buffer, size_t buffer_size, const char* text) {
    if (!label) {
        return;
    }
    if (buffer) {
        CopyText(buffer, buffer_size, text);
        lv_label_set_text_static(label, buffer);
    } else {
        lv_label_set_text(label, text ? text : "");
    }
}
}

EmoEmojiDisplay::EmoEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy) {
    InitializeEmoEmojis();
    SetupChatLabel();
    SetupPreviewImage();
    SetupLyricView();
    SetupStandbyClock();
}

EmoEmojiDisplay::~EmoEmojiDisplay() {
    StopStandbyClockTimers();
    if (standby_clock_delay_timer_) {
        esp_timer_delete(standby_clock_delay_timer_);
        standby_clock_delay_timer_ = nullptr;
    }
    if (standby_clock_update_timer_) {
        esp_timer_delete(standby_clock_update_timer_);
        standby_clock_update_timer_ = nullptr;
    }
    ClearLyricBuffers();
}

void EmoEmojiDisplay::SetupLyricView() {
    DisplayLockGuard lock(this);

    lyric_title_buffer_ = AllocPsramTextBuffer(kLyricTitleBufferSize, "lyric title");
    lyric_artist_buffer_ = AllocPsramTextBuffer(kLyricArtistBufferSize, "lyric artist");
    lyric_lines_buffer_ = AllocPsramTextBuffer(kLyricLinesBufferSize, "lyric lines");
    lyric_time_buffer_ = AllocPsramTextBuffer(kLyricTimeBufferSize, "lyric time");

    lyric_screen_ = lv_obj_create(lv_screen_active());
    lv_obj_set_size(lyric_screen_, width_, height_);
    lv_obj_set_style_bg_color(lyric_screen_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lyric_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(lyric_screen_, 0, 0);
    lv_obj_set_style_radius(lyric_screen_, 0, 0);
    lv_obj_set_style_pad_all(lyric_screen_, 0, 0);
    lv_obj_clear_flag(lyric_screen_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(lyric_screen_, LV_OBJ_FLAG_HIDDEN);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();

    lyric_title_label_ = lv_label_create(lyric_screen_);
    SetLabelTextPreferPsram(lyric_title_label_, lyric_title_buffer_, kLyricTitleBufferSize, "");
    lv_label_set_long_mode(lyric_title_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lyric_title_label_, width_ - 28);
    lv_obj_set_style_text_align(lyric_title_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lyric_title_label_, lv_color_white(), 0);
    lv_obj_set_style_text_font(lyric_title_label_, text_font, 0);
    lv_obj_align(lyric_title_label_, LV_ALIGN_TOP_MID, 0, 8);

    lyric_artist_label_ = lv_label_create(lyric_screen_);
    SetLabelTextPreferPsram(lyric_artist_label_, lyric_artist_buffer_, kLyricArtistBufferSize, "");
    lv_label_set_long_mode(lyric_artist_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lyric_artist_label_, width_ - 36);
    lv_obj_set_style_text_align(lyric_artist_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lyric_artist_label_, lv_color_make(160, 160, 160), 0);
    lv_obj_set_style_text_font(lyric_artist_label_, text_font, 0);
    lv_obj_align(lyric_artist_label_, LV_ALIGN_TOP_MID, 0, 34);

    lyric_lines_label_ = lv_label_create(lyric_screen_);
    SetLabelTextPreferPsram(lyric_lines_label_, lyric_lines_buffer_, kLyricLinesBufferSize, "");
    lv_label_set_recolor(lyric_lines_label_, true);
    lv_label_set_long_mode(lyric_lines_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(lyric_lines_label_, width_ - 24, height_ - 82);
    lv_obj_set_style_text_align(lyric_lines_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lyric_lines_label_, lv_color_make(112, 112, 112), 0);
    lv_obj_set_style_text_font(lyric_lines_label_, text_font, 0);
    lv_obj_set_style_text_line_space(lyric_lines_label_, 7, 0);
    lv_obj_align(lyric_lines_label_, LV_ALIGN_TOP_MID, 0, 58);

    lyric_progress_bg_ = lv_obj_create(lyric_screen_);
    lv_obj_set_size(lyric_progress_bg_, width_ - 42, 4);
    lv_obj_set_style_bg_color(lyric_progress_bg_, lv_color_make(58, 58, 58), 0);
    lv_obj_set_style_bg_opa(lyric_progress_bg_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(lyric_progress_bg_, 0, 0);
    lv_obj_set_style_radius(lyric_progress_bg_, 2, 0);
    lv_obj_set_style_pad_all(lyric_progress_bg_, 0, 0);
    lv_obj_clear_flag(lyric_progress_bg_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(lyric_progress_bg_, LV_ALIGN_BOTTOM_MID, 0, -19);

    lyric_progress_fg_ = lv_obj_create(lyric_progress_bg_);
    lv_obj_set_size(lyric_progress_fg_, 0, 4);
    lv_obj_set_style_bg_color(lyric_progress_fg_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(lyric_progress_fg_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(lyric_progress_fg_, 0, 0);
    lv_obj_set_style_radius(lyric_progress_fg_, 2, 0);
    lv_obj_set_style_pad_all(lyric_progress_fg_, 0, 0);
    lv_obj_clear_flag(lyric_progress_fg_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(lyric_progress_fg_, LV_ALIGN_LEFT_MID, 0, 0);

    lyric_time_label_ = lv_label_create(lyric_screen_);
    SetLabelTextPreferPsram(lyric_time_label_, lyric_time_buffer_, kLyricTimeBufferSize, "00:00 / --:--");
    lv_obj_set_width(lyric_time_label_, width_ - 36);
    lv_obj_set_style_text_align(lyric_time_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lyric_time_label_, lv_color_make(150, 150, 150), 0);
    lv_obj_set_style_text_font(lyric_time_label_, text_font, 0);
    lv_obj_align(lyric_time_label_, LV_ALIGN_BOTTOM_MID, 0, -3);
}

void EmoEmojiDisplay::ClearLyricBuffers() {
    if (lyric_title_label_) {
        lv_label_set_text(lyric_title_label_, "");
    }
    if (lyric_artist_label_) {
        lv_label_set_text(lyric_artist_label_, "");
    }
    if (lyric_lines_label_) {
        lv_label_set_text(lyric_lines_label_, "");
    }
    if (lyric_time_label_) {
        lv_label_set_text(lyric_time_label_, "");
    }
    if (lyric_title_buffer_) {
        heap_caps_free(lyric_title_buffer_);
        lyric_title_buffer_ = nullptr;
    }
    if (lyric_artist_buffer_) {
        heap_caps_free(lyric_artist_buffer_);
        lyric_artist_buffer_ = nullptr;
    }
    if (lyric_lines_buffer_) {
        heap_caps_free(lyric_lines_buffer_);
        lyric_lines_buffer_ = nullptr;
    }
    if (lyric_time_buffer_) {
        heap_caps_free(lyric_time_buffer_);
        lyric_time_buffer_ = nullptr;
    }
}

void EmoEmojiDisplay::ShowLyricView() {
    if (!lyric_screen_) {
        return;
    }
    HideStandbyClock();
    lyric_visible_ = true;
    lv_obj_move_foreground(lyric_screen_);
    lv_obj_clear_flag(lyric_screen_, LV_OBJ_FLAG_HIDDEN);
}

void EmoEmojiDisplay::HideLyricView() {
    lyric_visible_ = false;
    if (lyric_screen_) {
        lv_obj_add_flag(lyric_screen_, LV_OBJ_FLAG_HIDDEN);
    }
}

void EmoEmojiDisplay::SetupPreviewImage() {
    DisplayLockGuard lock(this);
    lv_obj_set_size(preview_image_, width_ , height_ );
}

void EmoEmojiDisplay::SetupStandbyClock() {
    DisplayLockGuard lock(this);

    standby_clock_screen_ = lv_obj_create(lv_screen_active());
    lv_obj_set_size(standby_clock_screen_, width_, height_);
    lv_obj_set_style_bg_color(standby_clock_screen_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(standby_clock_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(standby_clock_screen_, 0, 0);
    lv_obj_set_style_radius(standby_clock_screen_, 0, 0);
    lv_obj_clear_flag(standby_clock_screen_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(standby_clock_screen_, LV_OBJ_FLAG_HIDDEN);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();

    static lv_style_t card_style;
    static bool card_style_inited = false;
    if (!card_style_inited) {
        lv_style_init(&card_style);
        lv_style_set_radius(&card_style, 18);
        lv_style_set_bg_color(&card_style, lv_color_hex(0x33333A));
        lv_style_set_bg_opa(&card_style, LV_OPA_COVER);
        lv_style_set_border_width(&card_style, 0);
        lv_style_set_pad_all(&card_style, 0);
        card_style_inited = true;
    }

    static lv_style_t divider_style;
    static bool divider_style_inited = false;
    if (!divider_style_inited) {
        lv_style_init(&divider_style);
        lv_style_set_bg_color(&divider_style, lv_color_black());
        lv_style_set_bg_opa(&divider_style, LV_OPA_COVER);
        lv_style_set_border_width(&divider_style, 0);
        lv_style_set_radius(&divider_style, 0);
        lv_style_set_pad_all(&divider_style, 0);
        divider_style_inited = true;
    }

    const int card_w = 96;
    const int card_h = 118;
    const int card_gap = 12;
    const int cards_x = (width_ - card_w * 2 - card_gap) / 2 - 15;
    const int cards_y = 48;

    lv_obj_t* hour_card = lv_obj_create(standby_clock_screen_);
    lv_obj_add_style(hour_card, &card_style, 0);
    lv_obj_set_size(hour_card, card_w, card_h);
    lv_obj_set_pos(hour_card, cards_x, cards_y);
    lv_obj_clear_flag(hour_card, LV_OBJ_FLAG_SCROLLABLE);

    standby_clock_time_label_ = lv_label_create(hour_card);
    lv_label_set_text(standby_clock_time_label_, "--");
    lv_obj_set_style_text_align(standby_clock_time_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(standby_clock_time_label_, lv_color_white(), 0);
    lv_obj_set_style_text_font(standby_clock_time_label_, &lv_fanye, 0);
    lv_obj_center(standby_clock_time_label_);

    standby_clock_left_bar_ = lv_obj_create(hour_card);
    lv_obj_add_style(standby_clock_left_bar_, &divider_style, 0);
    lv_obj_set_size(standby_clock_left_bar_, card_w, 3);
    lv_obj_align(standby_clock_left_bar_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(standby_clock_left_bar_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* minute_card = lv_obj_create(standby_clock_screen_);
    lv_obj_add_style(minute_card, &card_style, 0);
    lv_obj_set_size(minute_card, card_w, card_h);
    lv_obj_set_pos(minute_card, cards_x + card_w + card_gap, cards_y);
    lv_obj_clear_flag(minute_card, LV_OBJ_FLAG_SCROLLABLE);

    standby_clock_minute_label_ = lv_label_create(minute_card);
    lv_label_set_text(standby_clock_minute_label_, "--");
    lv_obj_set_style_text_align(standby_clock_minute_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(standby_clock_minute_label_, lv_color_white(), 0);
    lv_obj_set_style_text_font(standby_clock_minute_label_, &lv_fanye, 0);
    lv_obj_center(standby_clock_minute_label_);

    standby_clock_right_bar_ = lv_obj_create(minute_card);
    lv_obj_add_style(standby_clock_right_bar_, &divider_style, 0);
    lv_obj_set_size(standby_clock_right_bar_, card_w, 3);
    lv_obj_align(standby_clock_right_bar_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(standby_clock_right_bar_, LV_OBJ_FLAG_SCROLLABLE);

    standby_clock_date_label_ = lv_label_create(standby_clock_screen_);
    lv_label_set_text(standby_clock_date_label_, "");
    lv_obj_set_width(standby_clock_date_label_, width_ - 16);
    lv_obj_set_style_text_align(standby_clock_date_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(standby_clock_date_label_, lv_color_make(170, 170, 170), 0);
    lv_obj_set_style_text_font(standby_clock_date_label_, text_font, 0);
    lv_obj_align(standby_clock_date_label_, LV_ALIGN_BOTTOM_MID, 0, -28);

    const esp_timer_create_args_t delay_timer_args = {
        .callback = &EmoEmojiDisplay::StandbyClockDelayTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "emo_idle_clock_delay",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&delay_timer_args, &standby_clock_delay_timer_));

    const esp_timer_create_args_t update_timer_args = {
        .callback = &EmoEmojiDisplay::StandbyClockUpdateTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "emo_idle_clock_update",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&update_timer_args, &standby_clock_update_timer_));
}

void EmoEmojiDisplay::InitializeEmoEmojis() {
    ESP_LOGI(TAG, "初始化Emo GIF表情");

    auto emo_emoji_collection = std::make_shared<EmojiCollection>();

    // 中性/平静类表情 -> staticstate
    emo_emoji_collection->AddEmoji("staticstate", new LvglRawImage((void*)staticstate.data, staticstate.data_size));
    emo_emoji_collection->AddEmoji("neutral", new LvglRawImage((void*)staticstate.data, staticstate.data_size));
    emo_emoji_collection->AddEmoji("relaxed", new LvglRawImage((void*)staticstate.data, staticstate.data_size));
    emo_emoji_collection->AddEmoji("sleepy", new LvglRawImage((void*)staticstate.data, staticstate.data_size));
    emo_emoji_collection->AddEmoji("idle", new LvglRawImage((void*)staticstate.data, staticstate.data_size));

    // 积极/开心类表情 -> happy
    emo_emoji_collection->AddEmoji("happy", new LvglRawImage((void*)happy.data, happy.data_size));
    emo_emoji_collection->AddEmoji("laughing", new LvglRawImage((void*)happy.data, happy.data_size));
    emo_emoji_collection->AddEmoji("funny", new LvglRawImage((void*)happy.data, happy.data_size));
    emo_emoji_collection->AddEmoji("loving", new LvglRawImage((void*)happy.data, happy.data_size));
    emo_emoji_collection->AddEmoji("confident", new LvglRawImage((void*)happy.data, happy.data_size));
    emo_emoji_collection->AddEmoji("winking", new LvglRawImage((void*)happy.data, happy.data_size));
    emo_emoji_collection->AddEmoji("cool", new LvglRawImage((void*)happy.data, happy.data_size));
    emo_emoji_collection->AddEmoji("delicious", new LvglRawImage((void*)happy.data, happy.data_size));
    emo_emoji_collection->AddEmoji("kissy", new LvglRawImage((void*)happy.data, happy.data_size));
    emo_emoji_collection->AddEmoji("silly", new LvglRawImage((void*)happy.data, happy.data_size));

    // 悲伤类表情 -> sad
    emo_emoji_collection->AddEmoji("sad", new LvglRawImage((void*)sad.data, sad.data_size));
    emo_emoji_collection->AddEmoji("crying", new LvglRawImage((void*)sad.data, sad.data_size));

    // 愤怒类表情 -> anger
    emo_emoji_collection->AddEmoji("anger", new LvglRawImage((void*)anger.data, anger.data_size));
    emo_emoji_collection->AddEmoji("angry", new LvglRawImage((void*)anger.data, anger.data_size));

    // 惊讶类表情 -> scare
    emo_emoji_collection->AddEmoji("scare", new LvglRawImage((void*)scare.data, scare.data_size));
    emo_emoji_collection->AddEmoji("surprised", new LvglRawImage((void*)scare.data, scare.data_size));
    emo_emoji_collection->AddEmoji("shocked", new LvglRawImage((void*)scare.data, scare.data_size));

    // 思考/困惑类表情 -> buxue
    emo_emoji_collection->AddEmoji("buxue", new LvglRawImage((void*)buxue.data, buxue.data_size));
    emo_emoji_collection->AddEmoji("thinking", new LvglRawImage((void*)buxue.data, buxue.data_size));
    emo_emoji_collection->AddEmoji("confused", new LvglRawImage((void*)buxue.data, buxue.data_size));
    emo_emoji_collection->AddEmoji("embarrassed", new LvglRawImage((void*)buxue.data, buxue.data_size));

    // 将表情集合添加到主题中
    auto& theme_manager = LvglThemeManager::GetInstance();
    auto light_theme = theme_manager.GetTheme("light");
    auto dark_theme = theme_manager.GetTheme("dark");

    if (light_theme != nullptr) {
        light_theme->set_emoji_collection(emo_emoji_collection);
    }
    if (dark_theme != nullptr) {
        dark_theme->set_emoji_collection(emo_emoji_collection);
    }

    // 设置默认表情为staticstate
    SetEmotion("staticstate");

    ESP_LOGI(TAG, "Emo GIF表情初始化完成");
}

void EmoEmojiDisplay::SetupChatLabel() {
    DisplayLockGuard lock(this);

    if (chat_message_label_) {
        lv_obj_del(chat_message_label_);
    }

    chat_message_label_ = lv_label_create(container_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, width_ * 0.9);                        // 限制宽度为屏幕宽度的 90%
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);            
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);  // 设置文本居中对齐
    lv_obj_set_style_text_color(chat_message_label_, lv_color_white(), 0);
    SetTheme(LvglThemeManager::GetInstance().GetTheme("dark"));
}

void EmoEmojiDisplay::SetStatus(const char* status) {
    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    DisplayLockGuard lock(this);
    if (!status) {
        ESP_LOGE(TAG, "SetStatus: status is nullptr");
        return;
    }

    if (strcmp(status, Lang::Strings::LISTENING) == 0) {
        HideLyricView();
        HideStandbyClock();
        lv_obj_set_style_text_font(status_label_, &EMO_ICON_FONT, 0);
        lv_label_set_text(status_label_, "\xEF\x84\xB0");  // U+F130 麦克风图标
        lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(network_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    } else if (strcmp(status, Lang::Strings::SPEAKING) == 0) {
        HideLyricView();
        HideStandbyClock();
        lv_obj_set_style_text_font(status_label_, &EMO_ICON_FONT, 0);
        lv_label_set_text(status_label_, "\xEF\x80\xA8");  // U+F028 说话图标
        lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(network_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    } else if (strcmp(status, Lang::Strings::CONNECTING) == 0) {
        HideLyricView();
        HideStandbyClock();
        lv_obj_set_style_text_font(status_label_, &EMO_ICON_FONT, 0);
        lv_label_set_text(status_label_, "\xEF\x83\x81");  // U+F0c1 连接图标
        lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    } else if (strcmp(status, Lang::Strings::STANDBY) == 0) {
        lv_obj_set_style_text_font(status_label_, text_font, 0);
        lv_label_set_text(status_label_, "");
        lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        StartStandbyClockDelay();
        return;
    }

    if (standby_clock_visible_) {
        return;
    }
    lv_obj_set_style_text_font(status_label_, text_font, 0);
    lv_label_set_text(status_label_, status);
    lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(network_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);
}

void EmoEmojiDisplay::SetMusicInfo(const char* song_name) {
    DisplayLockGuard lock(this);

    if (song_name == nullptr || song_name[0] == '\0') {
        HideLyricView();
        if (lyric_title_label_) {
            SetLabelTextPreferPsram(lyric_title_label_, lyric_title_buffer_, kLyricTitleBufferSize, "");
        }
        if (lyric_artist_label_) {
            SetLabelTextPreferPsram(lyric_artist_label_, lyric_artist_buffer_, kLyricArtistBufferSize, "");
        }
        if (lyric_lines_label_) {
            SetLabelTextPreferPsram(lyric_lines_label_, lyric_lines_buffer_, kLyricLinesBufferSize, "");
        }
        if (lyric_progress_fg_) {
            lv_obj_set_width(lyric_progress_fg_, 0);
            lv_obj_align(lyric_progress_fg_, LV_ALIGN_LEFT_MID, 0, 0);
        }
        if (lyric_time_label_) {
            SetLabelTextPreferPsram(lyric_time_label_, lyric_time_buffer_, kLyricTimeBufferSize, "00:00 / --:--");
        }
        return;
    }

    std::string title(song_name);
    std::string artist;
    size_t split = title.find('\n');
    if (split != std::string::npos) {
        artist = title.substr(split + 1);
        title = title.substr(0, split);
    }

    if (lyric_title_label_) {
        SetLabelTextPreferPsram(lyric_title_label_, lyric_title_buffer_, kLyricTitleBufferSize, title.c_str());
    }
    if (lyric_artist_label_) {
        SetLabelTextPreferPsram(lyric_artist_label_, lyric_artist_buffer_, kLyricArtistBufferSize, artist.c_str());
    }
    ShowLyricView();
}

void EmoEmojiDisplay::SetMusicProgress(int elapsed_ms, int total_ms) {
    DisplayLockGuard lock(this);

    if (elapsed_ms < 0) {
        elapsed_ms = 0;
    }
    if (total_ms < 0) {
        total_ms = 0;
    }

    if (lyric_progress_bg_ && lyric_progress_fg_) {
        int bg_width = width_ > 42 ? width_ - 42 : lv_obj_get_width(lyric_progress_bg_);
        int fg_width = 0;
        if (total_ms > 0) {
            if (elapsed_ms > total_ms) {
                elapsed_ms = total_ms;
            }
            fg_width = bg_width * elapsed_ms / total_ms;
        }
        lv_obj_set_width(lyric_progress_fg_, fg_width);
        lv_obj_align(lyric_progress_fg_, LV_ALIGN_LEFT_MID, 0, 0);
    }

    if (lyric_time_label_) {
        int elapsed_sec = elapsed_ms / 1000;
        int total_sec = total_ms / 1000;
        char time_text[24];
        if (total_sec > 0) {
            snprintf(time_text, sizeof(time_text), "%02d:%02d / %02d:%02d",
                     elapsed_sec / 60, elapsed_sec % 60, total_sec / 60, total_sec % 60);
        } else {
            snprintf(time_text, sizeof(time_text), "%02d:%02d / --:--",
                     elapsed_sec / 60, elapsed_sec % 60);
        }
        SetLabelTextPreferPsram(lyric_time_label_, lyric_time_buffer_, kLyricTimeBufferSize, time_text);
    }
}

void EmoEmojiDisplay::ShowSuccessScreen(const char* message) {
    DisplayLockGuard lock(this);
    ESP_LOGI(TAG, "ShowSuccessScreen: %s", message ? message : "");

    HideLyricView();
    HideStandbyClock();

    lv_obj_t* screen = lv_screen_active();
    if (screen == nullptr) {
        return;
    }

    lv_obj_t* container = lv_obj_create(screen);
    lv_obj_set_size(container, width_, height_);
    lv_obj_center(container);
    lv_obj_move_foreground(container);
    lv_obj_set_style_bg_color(container, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_radius(container, 0, 0);
    lv_obj_set_style_pad_all(container, 18, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* icon_label = lv_label_create(container);
    lv_label_set_text(icon_label, LV_SYMBOL_OK);
    lv_obj_set_style_text_color(icon_label, lv_color_hex(0x2BFF80), 0);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_margin_bottom(icon_label, 12, 0);

    lv_obj_t* msg_label = lv_label_create(container);
    lv_obj_set_width(msg_label, width_ - 36);
    lv_label_set_long_mode(msg_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(msg_label, message ? message : "");
    lv_obj_set_style_text_align(msg_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(msg_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(msg_label, static_cast<LvglTheme*>(current_theme_)->text_font()->font(), 0);

    lv_obj_invalidate(screen);
}

void EmoEmojiDisplay::SetChatMessage(const char* role, const char* content) {
    if (role != nullptr && strcmp(role, "lyric") == 0) {
        DisplayLockGuard lock(this);
        if (lyric_lines_label_) {
            SetLabelTextPreferPsram(lyric_lines_label_, lyric_lines_buffer_, kLyricLinesBufferSize, content ? content : "");
        }
        ShowLyricView();
        return;
    }

    {
        DisplayLockGuard lock(this);
        HideLyricView();
    }
    LcdDisplay::SetChatMessage(role, content);
}

void EmoEmojiDisplay::StartStandbyClockDelay() {
    if (standby_clock_visible_) {
        if (standby_clock_delay_timer_) {
            esp_timer_stop(standby_clock_delay_timer_);
        }
        if (standby_clock_update_timer_) {
            esp_timer_start_periodic(standby_clock_update_timer_, 1 * 1000 * 1000);
        }
        return;
    }

    StopStandbyClockTimers();
    if (standby_clock_delay_timer_) {
        esp_timer_start_once(standby_clock_delay_timer_, 5 * 1000 * 1000);
    }
}

void EmoEmojiDisplay::StopStandbyClockTimers() {
    if (standby_clock_delay_timer_) {
        esp_timer_stop(standby_clock_delay_timer_);
    }
    if (standby_clock_update_timer_) {
        esp_timer_stop(standby_clock_update_timer_);
    }
}

void EmoEmojiDisplay::ShowStandbyClock() {
    if (!standby_clock_screen_) {
        return;
    }
    standby_clock_visible_ = true;
    lv_obj_move_foreground(standby_clock_screen_);
    lv_obj_clear_flag(standby_clock_screen_, LV_OBJ_FLAG_HIDDEN);

    if (status_bar_) {
        lv_obj_set_parent(status_bar_, standby_clock_screen_);
        lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);
        lv_obj_move_foreground(status_bar_);
        
        // 确保状态栏宽度铺满，并使用两端对齐
        lv_obj_set_width(status_bar_, width_);
        lv_obj_set_flex_align(status_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // 仅调整电池图标的右边距，使其不那么靠边缘
        lv_obj_set_style_pad_right(status_bar_, 16, 0);

        // 隐藏不需要的元素
        lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mute_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

        // 交换图标顺序：WIFI放在最左边 (index 0)，电量图标放在最右边 (index -1)
        lv_obj_move_to_index(network_label_, 0);
        lv_obj_move_to_index(battery_label_, -1);

        lv_obj_remove_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(network_label_, LV_OBJ_FLAG_HIDDEN);
    }

    UpdateStandbyClock();
    if (standby_clock_update_timer_) {
        esp_timer_start_periodic(standby_clock_update_timer_, 1 * 1000 * 1000);
    }
}

void EmoEmojiDisplay::HideStandbyClock() {
    StopStandbyClockTimers();
    standby_clock_visible_ = false;

    if (status_bar_ && container_) {
        lv_obj_set_parent(status_bar_, container_);
        lv_obj_move_to_index(status_bar_, 0);
        lv_obj_set_style_bg_opa(status_bar_, LV_OPA_50, 0);
        
        // 恢复原有状态栏的右边距
        auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
        lv_obj_set_style_pad_right(status_bar_, lvgl_theme->spacing(4), 0);
        
        // 恢复原有图标顺序
        lv_obj_move_to_index(network_label_, 0);
        lv_obj_move_to_index(battery_label_, -1);
    }

    if (standby_clock_screen_) {
        lv_obj_add_flag(standby_clock_screen_, LV_OBJ_FLAG_HIDDEN);
    }
}

void EmoEmojiDisplay::UpdateStandbyClock() {
    if (!standby_clock_visible_ || !standby_clock_time_label_ || !standby_clock_minute_label_ || !standby_clock_date_label_) {
        return;
    }

    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year < 120) {
        lv_label_set_text(standby_clock_time_label_, "--");
        lv_label_set_text(standby_clock_minute_label_, "--");
        lv_label_set_text(standby_clock_date_label_, "Time not synced");
        return;
    }

    char hour_buf[3];
    char minute_buf[3];
    snprintf(hour_buf, sizeof(hour_buf), "%02d", timeinfo.tm_hour);
    snprintf(minute_buf, sizeof(minute_buf), "%02d", timeinfo.tm_min);
    lv_label_set_text(standby_clock_time_label_, hour_buf);
    lv_label_set_text(standby_clock_minute_label_, minute_buf);
    lv_obj_set_style_text_font(standby_clock_time_label_, &lv_fanye, 0);
    lv_obj_set_style_text_font(standby_clock_minute_label_, &lv_fanye, 0);
    lv_obj_center(standby_clock_time_label_);
    lv_obj_center(standby_clock_minute_label_);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();

    static const char* kWeekdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char* kMonths[] = {"January", "February", "March", "April", "May", "June",
                                    "July", "August", "September", "October", "November", "December"};
    const char* weekday = kWeekdays[(timeinfo.tm_wday >= 0 && timeinfo.tm_wday <= 6) ? timeinfo.tm_wday : 0];
    const char* month = kMonths[(timeinfo.tm_mon >= 0 && timeinfo.tm_mon <= 11) ? timeinfo.tm_mon : 0];
    char date_buf[64];
    snprintf(date_buf, sizeof(date_buf), "%s.%s %d.%d", weekday, month, timeinfo.tm_mday, timeinfo.tm_year + 1900);
    lv_label_set_text(standby_clock_date_label_, date_buf);
    lv_obj_set_style_text_font(standby_clock_date_label_, &lv_fanye_18, 0);

    if (status_bar_) {
        lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mute_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(network_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void EmoEmojiDisplay::StandbyClockDelayTimerCallback(void* arg) {
    auto self = static_cast<EmoEmojiDisplay*>(arg);
    DisplayLockGuard lock(self);
    self->ShowStandbyClock();
}

void EmoEmojiDisplay::StandbyClockUpdateTimerCallback(void* arg) {
    auto self = static_cast<EmoEmojiDisplay*>(arg);
    DisplayLockGuard lock(self);
    self->UpdateStandbyClock();
}

void EmoEmojiDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    DisplayLockGuard lock(this);
    if (preview_image_ == nullptr) {
        ESP_LOGE(TAG, "Preview image is not initialized");
        return;
    }

    if (image == nullptr) {
        esp_timer_stop(preview_timer_);
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        preview_image_cached_.reset();
        if (gif_controller_) {
            gif_controller_->Start();
        }
        return;
    }

    preview_image_cached_ = std::move(image);
    auto img_dsc = preview_image_cached_->image_dsc();
    // 设置图片源并显示预览图片
    lv_image_set_src(preview_image_, img_dsc);
    lv_image_set_rotation(preview_image_, -900);
    if (img_dsc->header.w > 0 && img_dsc->header.h > 0) {
        // zoom factor 1.0
        lv_image_set_scale(preview_image_, 256 * width_ / img_dsc->header.w);
    }

    // Hide emoji_box_
    if (gif_controller_) {
        gif_controller_->Stop();
    }
    lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    esp_timer_stop(preview_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(preview_timer_, PREVIEW_IMAGE_DURATION_MS * 1000));
}
