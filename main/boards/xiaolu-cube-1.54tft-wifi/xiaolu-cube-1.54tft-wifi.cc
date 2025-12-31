#include "wifi_board.h"
#include "codecs/xiaolu_no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "power_save_timer.h"
#include "led/single_led.h"
#include "assets/lang_config.h"
#include "power_manager.h"
#include "lvgl_theme.h"
#include "settings.h"
#include "mcp_server.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <font_awesome.h>
#include <variant>

#include <driver/rtc_io.h>
#include <esp_sleep.h>

#define TAG "XIAOLU_CUBE_1_54TFT_WIFI"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_awesome_30_4);

class XIAOLU_CUBE_1_54TFT_WIFI : public WifiBoard {
private:
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    SpiLcdDisplay* display_;
    PowerSaveTimer* power_save_timer_;
    PowerManager* power_manager_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    // 主题切换相关
    bool volume_up_pressed_ = false;
    bool volume_down_pressed_ = false;
    int current_theme_index_ = 0;
    const std::vector<std::string> theme_names_ = {
        "light", "dark", "blue", "green", "purple", "orange", "pink", "cyan", "yellow"
    };

    void SwitchToNextTheme() {
        current_theme_index_ = (current_theme_index_ + 1) % theme_names_.size();
        auto& theme_manager = LvglThemeManager::GetInstance();
        auto theme = theme_manager.GetTheme(theme_names_[current_theme_index_]);

        if (theme != nullptr) {
            auto display = GetDisplay();
            if (display) {
                display->SetTheme(theme);
                ESP_LOGI(TAG, "主题已切换为: %s", theme_names_[current_theme_index_].c_str());

                // 显示通知
                std::string theme_name_cn;
                if (theme_names_[current_theme_index_] == "light") {
                    theme_name_cn = "亮色主题";
                } else if (theme_names_[current_theme_index_] == "dark") {
                    theme_name_cn = "深色主题";
                } else if (theme_names_[current_theme_index_] == "blue") {
                    theme_name_cn = "蓝色主题";
                } else if (theme_names_[current_theme_index_] == "green") {
                    theme_name_cn = "绿色主题";
                } else if (theme_names_[current_theme_index_] == "purple") {
                    theme_name_cn = "紫色主题";
                } else if (theme_names_[current_theme_index_] == "orange") {
                    theme_name_cn = "橙色主题";
                } else if (theme_names_[current_theme_index_] == "pink") {
                    theme_name_cn = "粉色主题";
                } else if (theme_names_[current_theme_index_] == "cyan") {
                    theme_name_cn = "青色主题";
                } else if (theme_names_[current_theme_index_] == "yellow") {
                    theme_name_cn = "黄色主题";
                } else {
                    theme_name_cn = "未知主题";
                }

                display->ShowNotification(theme_name_cn);
            }
        }
    }

    void CheckSimultaneousPress() {
        if (volume_up_pressed_ && volume_down_pressed_) {
            ESP_LOGI(TAG, "检测到同时按下音量+和音量-，切换主题");
            SwitchToNextTheme();
            volume_up_pressed_ = false;
            volume_down_pressed_ = false;
        }
    }

    void InitializeCustomThemes() {
        auto text_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);
        auto icon_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);
        auto large_icon_font = std::make_shared<LvglBuiltInFont>(&font_awesome_30_4);

        // 亮色主题 - 采用温暖舒适的配色
        auto light_theme = new LvglTheme("light");
        light_theme->set_background_color(lv_color_hex(0xF5F5F5));        // 柔和的浅灰色背景
        light_theme->set_text_color(lv_color_hex(0x2C3E50));              // 深蓝灰色文字
        light_theme->set_chat_background_color(lv_color_hex(0xECF0F1));  // 浅灰蓝色聊天背景
        light_theme->set_user_bubble_color(lv_color_hex(0x3498DB));      // 明亮的蓝色用户气泡
        light_theme->set_assistant_bubble_color(lv_color_hex(0xFFFFFF)); // 纯白色助手气泡
        light_theme->set_system_bubble_color(lv_color_hex(0xF5F5F5));    // 浅灰系统气泡
        light_theme->set_system_text_color(lv_color_hex(0x7F8C8D));      // 中灰色系统文字
        light_theme->set_border_color(lv_color_hex(0xBDC3C7));           // 浅灰色边框
        light_theme->set_low_battery_color(lv_color_hex(0xE74C3C));      // 红色低电量
        light_theme->set_text_font(text_font);
        light_theme->set_icon_font(icon_font);
        light_theme->set_large_icon_font(large_icon_font);

        // 暗色主题 - 采用现代深色护眼配色
        auto dark_theme = new LvglTheme("dark");
        dark_theme->set_background_color(lv_color_hex(0x1C1C1E));        // 深灰色背景(类似iOS深色模式)
        dark_theme->set_text_color(lv_color_hex(0xF2F2F7));              // 柔和的白色文字
        dark_theme->set_chat_background_color(lv_color_hex(0x2C2C2E));  // 中度深灰色聊天背景
        dark_theme->set_user_bubble_color(lv_color_hex(0x0A84FF));      // iOS风格的蓝色用户气泡
        dark_theme->set_assistant_bubble_color(lv_color_hex(0x3A3A3C)); // 中度深灰助手气泡
        dark_theme->set_system_bubble_color(lv_color_hex(0x1C1C1E));    // 深灰系统气泡
        dark_theme->set_system_text_color(lv_color_hex(0x98989D));      // 中灰色系统文字
        dark_theme->set_border_color(lv_color_hex(0x48484A));           // 中灰色边框
        dark_theme->set_low_battery_color(lv_color_hex(0xFF453A));      // 柔和的红色低电量
        dark_theme->set_text_font(text_font);
        dark_theme->set_icon_font(icon_font);
        dark_theme->set_large_icon_font(large_icon_font);

        // 蓝色主题 - 清新海洋风
        auto blue_theme = new LvglTheme("blue");
        blue_theme->set_background_color(lv_color_hex(0xE3F2FD));        // 极浅蓝背景
        blue_theme->set_text_color(lv_color_hex(0x0D47A1));              // 深蓝色文字
        blue_theme->set_chat_background_color(lv_color_hex(0xBBDEFB));  // 浅蓝聊天背景
        blue_theme->set_user_bubble_color(lv_color_hex(0x2196F3));      // 标准蓝用户气泡
        blue_theme->set_assistant_bubble_color(lv_color_hex(0xFFFFFF)); // 白色助手气泡
        blue_theme->set_system_bubble_color(lv_color_hex(0xE3F2FD));    // 极浅蓝系统气泡
        blue_theme->set_system_text_color(lv_color_hex(0x5E92F3));      // 中蓝系统文字
        blue_theme->set_border_color(lv_color_hex(0x90CAF9));           // 浅蓝边框
        blue_theme->set_low_battery_color(lv_color_hex(0xF44336));      // 红色低电量
        blue_theme->set_text_font(text_font);
        blue_theme->set_icon_font(icon_font);
        blue_theme->set_large_icon_font(large_icon_font);

        // 绿色主题 - 自然护眼风
        auto green_theme = new LvglTheme("green");
        green_theme->set_background_color(lv_color_hex(0xE8F5E9));       // 极浅绿背景
        green_theme->set_text_color(lv_color_hex(0x1B5E20));             // 深绿色文字
        green_theme->set_chat_background_color(lv_color_hex(0xC8E6C9)); // 浅绿聊天背景
        green_theme->set_user_bubble_color(lv_color_hex(0x4CAF50));     // 清新绿用户气泡
        green_theme->set_assistant_bubble_color(lv_color_hex(0xFFFFFF));// 白色助手气泡
        green_theme->set_system_bubble_color(lv_color_hex(0xE8F5E9));   // 极浅绿系统气泡
        green_theme->set_system_text_color(lv_color_hex(0x66BB6A));     // 中绿系统文字
        green_theme->set_border_color(lv_color_hex(0xA5D6A7));          // 浅绿边框
        green_theme->set_low_battery_color(lv_color_hex(0xFF5722));     // 橙红色低电量
        green_theme->set_text_font(text_font);
        green_theme->set_icon_font(icon_font);
        green_theme->set_large_icon_font(large_icon_font);

        // 紫色主题 - 优雅高贵风
        auto purple_theme = new LvglTheme("purple");
        purple_theme->set_background_color(lv_color_hex(0xF3E5F5));      // 极浅紫背景
        purple_theme->set_text_color(lv_color_hex(0x4A148C));            // 深紫色文字
        purple_theme->set_chat_background_color(lv_color_hex(0xE1BEE7));// 浅紫聊天背景
        purple_theme->set_user_bubble_color(lv_color_hex(0x9C27B0));    // 标准紫用户气泡
        purple_theme->set_assistant_bubble_color(lv_color_hex(0xFFFFFF));// 白色助手气泡
        purple_theme->set_system_bubble_color(lv_color_hex(0xF3E5F5));  // 极浅紫系统气泡
        purple_theme->set_system_text_color(lv_color_hex(0xAB47BC));    // 中紫系统文字
        purple_theme->set_border_color(lv_color_hex(0xCE93D8));         // 浅紫边框
        purple_theme->set_low_battery_color(lv_color_hex(0xE91E63));    // 粉红色低电量
        purple_theme->set_text_font(text_font);
        purple_theme->set_icon_font(icon_font);
        purple_theme->set_large_icon_font(large_icon_font);

        // 橙色主题 - 温暖活力风
        auto orange_theme = new LvglTheme("orange");
        orange_theme->set_background_color(lv_color_hex(0xFFF3E0));      // 极浅橙背景
        orange_theme->set_text_color(lv_color_hex(0xE65100));            // 深橙色文字
        orange_theme->set_chat_background_color(lv_color_hex(0xFFE0B2));// 浅橙聊天背景
        orange_theme->set_user_bubble_color(lv_color_hex(0xFF9800));    // 标准橙用户气泡
        orange_theme->set_assistant_bubble_color(lv_color_hex(0xFFFFFF));// 白色助手气泡
        orange_theme->set_system_bubble_color(lv_color_hex(0xFFF3E0));  // 极浅橙系统气泡
        orange_theme->set_system_text_color(lv_color_hex(0xFFA726));    // 中橙系统文字
        orange_theme->set_border_color(lv_color_hex(0xFFCC80));         // 浅橙边框
        orange_theme->set_low_battery_color(lv_color_hex(0xD32F2F));    // 深红色低电量
        orange_theme->set_text_font(text_font);
        orange_theme->set_icon_font(icon_font);
        orange_theme->set_large_icon_font(large_icon_font);

        // 粉色主题 - 少女浪漫风
        auto pink_theme = new LvglTheme("pink");
        pink_theme->set_background_color(lv_color_hex(0xFCE4EC));        // 极浅粉背景
        pink_theme->set_text_color(lv_color_hex(0x880E4F));              // 深粉色文字
        pink_theme->set_chat_background_color(lv_color_hex(0xF8BBD0));  // 浅粉聊天背景
        pink_theme->set_user_bubble_color(lv_color_hex(0xE91E63));      // 标准粉用户气泡
        pink_theme->set_assistant_bubble_color(lv_color_hex(0xFFFFFF)); // 白色助手气泡
        pink_theme->set_system_bubble_color(lv_color_hex(0xFCE4EC));    // 极浅粉系统气泡
        pink_theme->set_system_text_color(lv_color_hex(0xF06292));      // 中粉系统文字
        pink_theme->set_border_color(lv_color_hex(0xF48FB1));           // 浅粉边框
        pink_theme->set_low_battery_color(lv_color_hex(0xC62828));      // 暗红色低电量
        pink_theme->set_text_font(text_font);
        pink_theme->set_icon_font(icon_font);
        pink_theme->set_large_icon_font(large_icon_font);

        // 青色主题 - 科技未来风
        auto cyan_theme = new LvglTheme("cyan");
        cyan_theme->set_background_color(lv_color_hex(0xE0F7FA));        // 极浅青背景
        cyan_theme->set_text_color(lv_color_hex(0x006064));              // 深青色文字
        cyan_theme->set_chat_background_color(lv_color_hex(0xB2EBF2));  // 浅青聊天背景
        cyan_theme->set_user_bubble_color(lv_color_hex(0x00BCD4));      // 标准青用户气泡
        cyan_theme->set_assistant_bubble_color(lv_color_hex(0xFFFFFF)); // 白色助手气泡
        cyan_theme->set_system_bubble_color(lv_color_hex(0xE0F7FA));    // 极浅青系统气泡
        cyan_theme->set_system_text_color(lv_color_hex(0x26C6DA));      // 中青系统文字
        cyan_theme->set_border_color(lv_color_hex(0x80DEEA));           // 浅青边框
        cyan_theme->set_low_battery_color(lv_color_hex(0xFF6F00));      // 深橙色低电量
        cyan_theme->set_text_font(text_font);
        cyan_theme->set_icon_font(icon_font);
        cyan_theme->set_large_icon_font(large_icon_font);

        // 黄色主题 - 阳光活力风
        auto yellow_theme = new LvglTheme("yellow");
        yellow_theme->set_background_color(lv_color_hex(0xFFFDE7));      // 极浅黄背景(柔和温暖)
        yellow_theme->set_text_color(lv_color_hex(0xF57F17));            // 深黄棕色文字(易读)
        yellow_theme->set_chat_background_color(lv_color_hex(0xFFF9C4)); // 浅黄聊天背景
        yellow_theme->set_user_bubble_color(lv_color_hex(0xFFEB3B));    // 明亮黄色用户气泡
        yellow_theme->set_assistant_bubble_color(lv_color_hex(0xFFFFFF));// 白色助手气泡
        yellow_theme->set_system_bubble_color(lv_color_hex(0xFFFDE7));  // 极浅黄系统气泡
        yellow_theme->set_system_text_color(lv_color_hex(0xFBC02D));    // 金黄色系统文字
        yellow_theme->set_border_color(lv_color_hex(0xFFF59D));         // 浅黄边框
        yellow_theme->set_low_battery_color(lv_color_hex(0xD84315));    // 深橙红色低电量
        yellow_theme->set_text_font(text_font);
        yellow_theme->set_icon_font(icon_font);
        yellow_theme->set_large_icon_font(large_icon_font);

        auto& theme_manager = LvglThemeManager::GetInstance();
        theme_manager.RegisterTheme("light", light_theme);
        theme_manager.RegisterTheme("dark", dark_theme);
        theme_manager.RegisterTheme("blue", blue_theme);
        theme_manager.RegisterTheme("green", green_theme);
        theme_manager.RegisterTheme("purple", purple_theme);
        theme_manager.RegisterTheme("orange", orange_theme);
        theme_manager.RegisterTheme("pink", pink_theme);
        theme_manager.RegisterTheme("cyan", cyan_theme);
        theme_manager.RegisterTheme("yellow", yellow_theme);
    }

    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();
        
        // 注册主题切换工具，提供详细的主题列表描述
        mcp_server.AddTool("self.screen.set_theme",
            "切换小路设备的主题颜色。可用主题:\n"
            "  - light: 温暖舒适的亮色主题\n"
            "  - dark: iOS风格深色护眼主题\n"
            "  - blue: 清新海洋风格\n"
            "  - green: 自然护眼风格\n"
            "  - purple: 优雅高贵风格\n"
            "  - orange: 温暖活力风格\n"
            "  - pink: 少女浪漫风格\n"
            "  - cyan: 科技未来风格\n"
            "  - yellow: 阳光活力风格",
            PropertyList({
                Property("theme", kPropertyTypeString)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                auto theme_name = properties["theme"].value<std::string>();
                auto& theme_manager = LvglThemeManager::GetInstance();
                auto theme = theme_manager.GetTheme(theme_name);

                if (theme != nullptr) {
                    auto display = GetDisplay();
                    if (display) {
                        display->SetTheme(theme);
                        ESP_LOGI(TAG, "主题已切换为: %s", theme_name.c_str());

                        // 显示通知
                        std::string theme_name_cn;
                        if (theme_name == "light") {
                            theme_name_cn = "亮色主题";
                        } else if (theme_name == "dark") {
                            theme_name_cn = "深色主题";
                        } else if (theme_name == "blue") {
                            theme_name_cn = "蓝色主题";
                        } else if (theme_name == "green") {
                            theme_name_cn = "绿色主题";
                        } else if (theme_name == "purple") {
                            theme_name_cn = "紫色主题";
                        } else if (theme_name == "orange") {
                            theme_name_cn = "橙色主题";
                        } else if (theme_name == "pink") {
                            theme_name_cn = "粉色主题";
                        } else if (theme_name == "cyan") {
                            theme_name_cn = "青色主题";
                        } else if (theme_name == "yellow") {
                            theme_name_cn = "黄色主题";
                        } else {
                            theme_name_cn = theme_name;
                        }

                        display->ShowNotification(theme_name_cn);
                        return true;
                    }
                }

                ESP_LOGW(TAG, "主题切换失败: %s", theme_name.c_str());
                return false;
            });
        
        // 列出所有可用主题的工具
        mcp_server.AddTool("self.screen.list_themes",
            "列出小路设备所有可用的主题。返回主题名称列表。",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                auto& theme_manager = LvglThemeManager::GetInstance();
                cJSON* json = cJSON_CreateObject();
                cJSON* themes_array = cJSON_CreateArray();
                
                for (const auto& theme_name : theme_names_) {
                    if (theme_manager.GetTheme(theme_name) != nullptr) {
                        cJSON_AddItemToArray(themes_array, cJSON_CreateString(theme_name.c_str()));
                    }
                }
                
                cJSON_AddItemToObject(json, "themes", themes_array);
                return json;
            });
        
        ESP_LOGI(TAG, "小路板子MCP工具已注册，支持 %zu 个自定义主题", theme_names_.size());
    }

    void InitializePowerManager() {
        power_manager_ = new PowerManager(GPIO_NUM_38);
        power_manager_->OnChargingStatusChanged([this](bool is_charging) {
            if (is_charging) {
                power_save_timer_->SetEnabled(false);
            } else {
                power_save_timer_->SetEnabled(true);
            }
        });
    }

    void InitializePowerSaveTimer() {
        rtc_gpio_init(GPIO_NUM_21);
        rtc_gpio_set_direction(GPIO_NUM_21, RTC_GPIO_MODE_OUTPUT_ONLY);
        rtc_gpio_set_level(GPIO_NUM_21, 1);

        power_save_timer_ = new PowerSaveTimer(-1, 60, -1);
        power_save_timer_->OnEnterSleepMode([this]() {
            ESP_LOGI(TAG, "Enabling sleep mode");
            display_->SetChatMessage("system", "");
            display_->SetEmotion("sleepy");
            GetBacklight()->SetBrightness(1);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            display_->SetChatMessage("system", "");
            display_->SetEmotion("neutral");
            GetBacklight()->RestoreBrightness();
        });
        power_save_timer_->OnShutdownRequest([this]() {
            ESP_LOGI(TAG, "Shutting down");
            rtc_gpio_set_level(GPIO_NUM_21, 0);
            // 启用保持功能，确保睡眠期间电平不变
            rtc_gpio_hold_en(GPIO_NUM_21);
            esp_lcd_panel_disp_on_off(panel_, false); //关闭显示
            esp_deep_sleep_start();
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SDA;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SCL;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        // 音量+ 按钮
        volume_up_button_.OnPressDown([this]() {
            volume_up_pressed_ = true;
            CheckSimultaneousPress();
        });

        volume_up_button_.OnPressUp([this]() {
            volume_up_pressed_ = false;
        });

        volume_up_button_.OnClick([this]() {
            if (!volume_down_pressed_) {  // 仅单独按下时调整音量
                power_save_timer_->WakeUp();
                auto codec = GetAudioCodec();
                auto volume = codec->output_volume() + 10;
                if (volume > 100) {
                    volume = 100;
                }
                codec->SetOutputVolume(volume);
                GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
            }
        });

        volume_up_button_.OnLongPress([this]() {
            power_save_timer_->WakeUp();
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        // 音量- 按钮
        volume_down_button_.OnPressDown([this]() {
            volume_down_pressed_ = true;
            CheckSimultaneousPress();
        });

        volume_down_button_.OnPressUp([this]() {
            volume_down_pressed_ = false;
        });

        volume_down_button_.OnClick([this]() {
            if (!volume_up_pressed_) {  // 仅单独按下时调整音量
                power_save_timer_->WakeUp();
                auto codec = GetAudioCodec();
                auto volume = codec->output_volume() - 10;
                if (volume < 0) {
                    volume = 0;
                }
                codec->SetOutputVolume(volume);
                GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
            }
        });

        volume_down_button_.OnLongPress([this]() {
            power_save_timer_->WakeUp();
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    void InitializeSt7789Display() {
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS;
        io_config.dc_gpio_num = DISPLAY_DC;
        io_config.spi_mode = 3;
        io_config.pclk_hz = 80 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io_));

        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RES;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io_, &panel_config, &panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, true));

        display_ = new SpiLcdDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, 
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

public:
    XIAOLU_CUBE_1_54TFT_WIFI() :
        boot_button_(BOOT_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        InitializeCustomThemes();
        InitializePowerManager();
        InitializePowerSaveTimer();
        InitializeSpi();
        InitializeButtons();
        InitializeSt7789Display();
        GetBacklight()->RestoreBrightness();
        InitializeTools();
    }

    virtual AudioCodec* GetAudioCodec() override {
        // static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
        //     AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        static XiaoluNoAudioCodecSimplexPdm audio_codec(
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK,
            AUDIO_I2S_SPK_GPIO_DOUT, GPIO_NUM_2, GPIO_NUM_3);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
    
    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        charging = power_manager_->IsCharging();
        discharging = power_manager_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }
        level = power_manager_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }
};

DECLARE_BOARD(XIAOLU_CUBE_1_54TFT_WIFI);

