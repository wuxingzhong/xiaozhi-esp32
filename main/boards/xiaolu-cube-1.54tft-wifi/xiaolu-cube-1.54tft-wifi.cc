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
#include "display/lvgl_display/emoji_collection.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <font_awesome.h>
#include <variant>
#include <cmath>
#include <vector>

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

    // 烟花类型枚举
    enum FireworkType {
        FIREWORK_NORMAL,      // 普通圆形爆炸
        FIREWORK_HEART,       // 心形
        FIREWORK_RING,        // 环形
        FIREWORK_FOUNTAIN,    // 喷泉式
        FIREWORK_WILLOW       // 柳树形（拖尾长）
    };

    // 烟花粒子结构
    struct FireworkParticle {
        lv_obj_t* obj;
        lv_obj_t* tail_obj;  // 拖尾对象
        float x, y;
        float vx, vy;
        lv_color_t base_color;
        lv_color_t secondary_color;  // 第二颜色（用于渐变）
        uint32_t life;
        uint32_t max_life;
        float size;  // 粒子大小
        bool twinkle;  // 是否闪烁
        uint32_t twinkle_phase;  // 闪烁相位
        bool has_secondary_explosion;  // 是否有二次爆炸
        uint32_t explosion_time;  // 爆炸时间点
        FireworkType type;  // 烟花类型
    };

    // 烟花容器和粒子
    std::vector<FireworkParticle> firework_particles_;
    lv_obj_t* firework_container_ = nullptr;
    lv_timer_t* firework_timer_ = nullptr;

    // 烟花动画更新函数
    static void FireworkTimerCallback(lv_timer_t* timer) {
        auto* self = static_cast<XIAOLU_CUBE_1_54TFT_WIFI*>(lv_timer_get_user_data(timer));
        self->UpdateFirework();
    }

    void UpdateFirework() {
        bool any_alive = false;

        for (auto& particle : firework_particles_) {
            if (particle.life > 0) {
                any_alive = true;

                // 保存旧位置用于拖尾
                float old_x = particle.x;
                float old_y = particle.y;

                // 更新位置
                particle.x += particle.vx;
                particle.y += particle.vy;
                particle.vy += 0.4f; // 重力
                particle.vx *= 0.98f; // 空气阻力
                particle.vy *= 0.98f;

                // 更新粒子对象位置
                lv_obj_set_pos(particle.obj, (int)particle.x, (int)particle.y);

                // 更新拖尾位置（从旧位置到新位置的中点）
                if (particle.tail_obj) {
                    float tail_x = (old_x + particle.x) / 2;
                    float tail_y = (old_y + particle.y) / 2;
                    lv_obj_set_pos(particle.tail_obj, (int)tail_x, (int)tail_y);
                }

                // 减少生命值
                particle.life--;

                // 计算生命比例（0.0 ~ 1.0）
                float life_ratio = (float)particle.life / particle.max_life;

                // 二次爆炸效果
                if (particle.has_secondary_explosion && particle.life == particle.explosion_time) {
                    // 创建小型二次爆炸
                    CreateMiniExplosion(particle.x, particle.y, particle.base_color, 8);
                }

                // 颜色渐变效果：从基础色到次要色再到黑色
                lv_color_t current_color;
                if (life_ratio > 0.5f) {
                    // 前半生命：基础色到次要色
                    uint8_t mix_ratio = (uint8_t)((1.0f - life_ratio) * 2.0f * 255);
                    current_color = lv_color_mix(
                        particle.base_color,
                        particle.secondary_color,
                        mix_ratio
                    );
                } else {
                    // 后半生命：次要色到黑色
                    uint8_t brightness = (uint8_t)(life_ratio * 2.0f * 255);
                    current_color = lv_color_mix(
                        particle.secondary_color,
                        lv_color_hex(0x000000),
                        255 - brightness
                    );
                }
                lv_obj_set_style_bg_color(particle.obj, current_color, 0);

                // 透明度：开始和结束时淡入淡出
                uint8_t opacity;
                if (life_ratio > 0.8f) {
                    // 淡入阶段
                    opacity = (uint8_t)((1.0f - life_ratio) * 5.0f * 255);
                } else if (life_ratio < 0.2f) {
                    // 淡出阶段
                    opacity = (uint8_t)(life_ratio * 5.0f * 255);
                } else {
                    // 完全不透明
                    opacity = 255;
                }

                // 闪烁效果
                if (particle.twinkle) {
                    particle.twinkle_phase++;
                    float twinkle = 0.7f + 0.3f * std::sin(particle.twinkle_phase * 0.3f);
                    opacity = (uint8_t)(opacity * twinkle);
                }

                lv_obj_set_style_opa(particle.obj, opacity, 0);

                // 拖尾透明度更低
                if (particle.tail_obj) {
                    lv_obj_set_style_opa(particle.tail_obj, opacity / 3, 0);
                    lv_obj_set_style_bg_color(particle.tail_obj, current_color, 0);
                }
            }
        }

        // 如果所有粒子都消失了，停止定时器并清理
        if (!any_alive) {
            if (firework_timer_) {
                lv_timer_pause(firework_timer_);
            }
            CleanupFirework();
        }
    }

    // 创建小型二次爆炸
    void CreateMiniExplosion(float x, float y, lv_color_t color, int particle_count) {
        if (!firework_container_) return;

        // 小屏幕减少二次爆炸粒子数量
        particle_count = particle_count / 2;
        if (particle_count < 4) particle_count = 4;

        for (int i = 0; i < particle_count; i++) {
            FireworkParticle particle;

            particle.size = 2.0f;  // 小粒子

            // 创建粒子对象
            particle.obj = lv_obj_create(firework_container_);
            lv_obj_set_size(particle.obj, 2, 2);
            lv_obj_set_style_radius(particle.obj, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(particle.obj, color, 0);
            lv_obj_set_style_bg_opa(particle.obj, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(particle.obj, 0, 0);

            // 创建拖尾
            particle.tail_obj = lv_obj_create(firework_container_);
            lv_obj_set_size(particle.tail_obj, 1, 1);
            lv_obj_set_style_radius(particle.tail_obj, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(particle.tail_obj, color, 0);
            lv_obj_set_style_border_width(particle.tail_obj, 0, 0);

            particle.x = x;
            particle.y = y;

            // 小范围随机发射
            float angle = (float)i / particle_count * 2 * 3.14159f;
            float speed = 1.0f + (rand() % 50) / 100.0f;
            particle.vx = std::cos(angle) * speed;
            particle.vy = std::sin(angle) * speed;

            particle.base_color = color;
            particle.secondary_color = lv_color_mix(color, lv_color_hex(0xFFFFFF), 128);
            particle.max_life = 20 + rand() % 10;
            particle.life = particle.max_life;
            particle.twinkle = false;
            particle.has_secondary_explosion = false;
            particle.type = FIREWORK_NORMAL;

            firework_particles_.push_back(particle);
        }
    }

    void CleanupFirework() {
        for (auto& particle : firework_particles_) {
            if (particle.obj) {
                lv_obj_del(particle.obj);
            }
            if (particle.tail_obj) {
                lv_obj_del(particle.tail_obj);
            }
        }
        firework_particles_.clear();

        if (firework_container_) {
            lv_obj_del(firework_container_);
            firework_container_ = nullptr;
        }

        if (firework_timer_) {
            lv_timer_delete(firework_timer_);
            firework_timer_ = nullptr;
        }
    }

    void LaunchFirework(int x, int y, lv_color_t color, int particle_count, FireworkType type = FIREWORK_NORMAL) {
        // 清理之前的烟花
        CleanupFirework();

        // 创建烟花容器
        firework_container_ = lv_obj_create(lv_screen_active());
        lv_obj_set_size(firework_container_, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        lv_obj_set_pos(firework_container_, 0, 0);
        lv_obj_set_style_bg_opa(firework_container_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(firework_container_, 0, 0);
        lv_obj_set_style_pad_all(firework_container_, 0, 0);

        // 根据类型决定颜色方案
        lv_color_t secondary_color;
        if (type == FIREWORK_WILLOW) {
            // 柳树形：金色到橙色渐变
            secondary_color = lv_color_hex(0xFF8C00);
        } else {
            // 其他：颜色到白色渐变
            secondary_color = lv_color_mix(color, lv_color_hex(0xFFFFFF), 150);
        }

        // 创建粒子 - 使用多层次粒子系统
        firework_particles_.reserve(particle_count);
        for (int i = 0; i < particle_count; i++) {
            FireworkParticle particle;
            particle.type = type;

            // 随机粒子大小（3种尺寸）- 针对240x240小屏幕优化
            int size_type = rand() % 3;
            if (size_type == 0) {
                particle.size = 4.0f;  // 大粒子（小屏幕减小）
            } else if (size_type == 1) {
                particle.size = 3.0f;  // 中等粒子
            } else {
                particle.size = 2.0f;  // 小粒子
            }

            // 创建主粒子对象
            particle.obj = lv_obj_create(firework_container_);
            lv_obj_set_size(particle.obj, (int)particle.size, (int)particle.size);
            lv_obj_set_style_radius(particle.obj, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(particle.obj, color, 0);
            lv_obj_set_style_bg_opa(particle.obj, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(particle.obj, 0, 0);

            // 添加发光效果（阴影）- 小屏幕减小阴影
            lv_obj_set_style_shadow_width(particle.obj, (int)(particle.size * 1.2f), 0);
            lv_obj_set_style_shadow_color(particle.obj, color, 0);
            lv_obj_set_style_shadow_opa(particle.obj, LV_OPA_40, 0);

            // 创建拖尾对象
            particle.tail_obj = lv_obj_create(firework_container_);
            lv_obj_set_size(particle.tail_obj, (int)(particle.size * 0.7f), (int)(particle.size * 0.7f));
            lv_obj_set_style_radius(particle.tail_obj, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(particle.tail_obj, color, 0);
            lv_obj_set_style_border_width(particle.tail_obj, 0, 0);

            // 初始位置
            particle.x = x;
            particle.y = y;

            // 根据烟花类型设置发射角度和速度
            float angle, speed;

            switch (type) {
                case FIREWORK_HEART: {
                    // 心形图案
                    float t = (float)i / particle_count * 2 * 3.14159f;
                    float heart_x = 16 * std::pow(std::sin(t), 3);
                    float heart_y = -(13 * std::cos(t) - 5 * std::cos(2*t) - 2 * std::cos(3*t) - std::cos(4*t));
                    angle = std::atan2(heart_y, heart_x);
                    speed = 1.2f + (rand() % 60) / 100.0f;  // 降低速度
                    break;
                }
                case FIREWORK_RING: {
                    // 环形 - 只向外，不上下
                    angle = (float)i / particle_count * 2 * 3.14159f;
                    speed = 2.0f + (rand() % 50) / 100.0f;  // 降低速度
                    break;
                }
                case FIREWORK_FOUNTAIN: {
                    // 喷泉式 - 主要向上
                    angle = -3.14159f / 2 + (rand() % 100 - 50) / 100.0f;
                    speed = 1.5f + (rand() % 150) / 100.0f;  // 降低速度
                    break;
                }
                case FIREWORK_WILLOW: {
                    // 柳树形 - 先上后下，拖尾长
                    angle = (float)i / particle_count * 2 * 3.14159f;
                    speed = 1.5f + (rand() % 100) / 100.0f;  // 降低速度
                    break;
                }
                default: // FIREWORK_NORMAL
                    angle = (float)i / particle_count * 2 * 3.14159f;
                    angle += (rand() % 100 - 50) / 500.0f;
                    speed = 1.8f + (rand() % 120) / 100.0f;  // 降低速度
                    speed = speed * (5.0f / particle.size);  // 调整速度比例
                    break;
            }

            particle.vx = std::cos(angle) * speed;
            particle.vy = std::sin(angle) * speed - (type == FIREWORK_RING ? 0.0f : 1.0f);  // 降低初始向上速度

            particle.base_color = color;
            particle.secondary_color = secondary_color;

            // 生命值根据粒子大小和类型变化 - 小屏幕缩短生命周期
            if (type == FIREWORK_WILLOW) {
                particle.max_life = 80 + (int)(particle.size * 12) + rand() % 30; // 柳树形稍长
            } else {
                particle.max_life = 60 + (int)(particle.size * 8) + rand() % 25;
            }
            particle.life = particle.max_life;

            // 30% 的粒子会闪烁
            particle.twinkle = (rand() % 100) < 30;
            particle.twinkle_phase = rand() % 100;

            // 20% 的粒子有二次爆炸
            particle.has_secondary_explosion = (rand() % 100) < 20 && type == FIREWORK_NORMAL;
            if (particle.has_secondary_explosion) {
                particle.explosion_time = particle.max_life / 2 + rand() % (particle.max_life / 4);
            }

            firework_particles_.push_back(particle);
        }

        // 创建定时器
        if (!firework_timer_) {
            firework_timer_ = lv_timer_create(FireworkTimerCallback, 25, this);
        } else {
            lv_timer_resume(firework_timer_);
        }
    }

    void PlayFireworkShow(int count, int delay_ms) {
        auto display = GetDisplay();
        if (!display) {
            ESP_LOGW(TAG, "Display not available");
            return;
        }

        DisplayLockGuard guard(display);

        // 更丰富的颜色数组 - 包括渐变和金色效果
        lv_color_t colors[] = {
            lv_color_hex(0xFF1744), // 鲜红色
            lv_color_hex(0x00E676), // 翠绿色
            lv_color_hex(0x2979FF), // 蓝色
            lv_color_hex(0xFFD600), // 金黄色
            lv_color_hex(0xE040FB), // 紫色
            lv_color_hex(0x00E5FF), // 青色
            lv_color_hex(0xFF6E40), // 橙色
            lv_color_hex(0xFF4081), // 粉红色
            lv_color_hex(0xFFFFFF), // 纯白色（璀璨效果）
            lv_color_hex(0xFFD700), // 金色
            lv_color_hex(0xFF69B4), // 热粉色
            lv_color_hex(0x7FFF00), // 春绿色
        };

        // 烟花类型数组
        FireworkType types[] = {
            FIREWORK_NORMAL,
            FIREWORK_NORMAL,    // 普通类型权重更高
            FIREWORK_NORMAL,
            FIREWORK_HEART,
            FIREWORK_RING,
            FIREWORK_FOUNTAIN,
            FIREWORK_WILLOW
        };

        for (int i = 0; i < count; i++) {
            // 随机位置 - 根据类型调整，针对240x240屏幕
            FireworkType type = types[rand() % (sizeof(types) / sizeof(types[0]))];

            int x, y;
            if (type == FIREWORK_FOUNTAIN) {
                // 喷泉式在底部中央
                x = DISPLAY_WIDTH / 2;
                y = DISPLAY_HEIGHT - 20;  // 距离底部20px
            } else {
                // 其他类型在屏幕中上部分 - 留出更多空间
                x = 30 + rand() % (DISPLAY_WIDTH - 60);  // 左右各留30px边距
                y = 25 + rand() % (DISPLAY_HEIGHT / 3);  // 在上1/3区域
            }

            // 随机颜色
            lv_color_t color = colors[rand() % (sizeof(colors) / sizeof(colors[0]))];

            // 粒子数量根据类型调整 - 小屏幕减少粒子数量
            int particle_count;
            if (type == FIREWORK_HEART) {
                particle_count = 30 + rand() % 10; // 心形需要更多粒子
            } else if (type == FIREWORK_WILLOW) {
                particle_count = 35 + rand() % 15; // 柳树形更密集
            } else if (type == FIREWORK_FOUNTAIN) {
                particle_count = 20 + rand() % 15; // 喷泉适中
            } else {
                particle_count = 25 + rand() % 20; // 25-45个粒子
            }

            // 发射烟花
            LaunchFirework(x, y, color, particle_count, type);

            // 延迟
            if (i < count - 1) {
                vTaskDelay(pdMS_TO_TICKS(delay_ms));
            }
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

        // 创建默认表情集合并设置到所有主题
        auto default_emoji_collection = std::static_pointer_cast<EmojiCollection>(std::make_shared<Twemoji64>());
        
        // 为所有主题设置表情集合
        light_theme->set_emoji_collection(default_emoji_collection);
        dark_theme->set_emoji_collection(default_emoji_collection);
        blue_theme->set_emoji_collection(default_emoji_collection);
        green_theme->set_emoji_collection(default_emoji_collection);
        purple_theme->set_emoji_collection(default_emoji_collection);
        orange_theme->set_emoji_collection(default_emoji_collection);
        pink_theme->set_emoji_collection(default_emoji_collection);
        cyan_theme->set_emoji_collection(default_emoji_collection);
        yellow_theme->set_emoji_collection(default_emoji_collection);

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

        // 注册烟花效果工具
        mcp_server.AddTool("self.screen.play_firework",
            "在小路设备的屏幕上播放烟花动画效果。可以指定烟花数量和间隔时间。",
            PropertyList({
                Property("count", kPropertyTypeInteger, 5),
                Property("delay_ms", kPropertyTypeInteger, 800)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int count = properties["count"].value<int>();
                int delay_ms = properties["delay_ms"].value<int>();

                // 限制范围
                if (count < 1) count = 1;
                if (count > 10) count = 10;
                if (delay_ms < 100) delay_ms = 100;
                if (delay_ms > 3000) delay_ms = 3000;

                ESP_LOGI(TAG, "播放烟花效果: count=%d, delay=%d", count, delay_ms);

                // 直接调用烟花效果
                PlayFireworkShow(count, delay_ms);

                cJSON* json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "status", "success");
                cJSON_AddStringToObject(json, "message", "烟花效果已播放");
                cJSON_AddNumberToObject(json, "count", count);
                cJSON_AddNumberToObject(json, "delay_ms", delay_ms);
                return json;
            });

        ESP_LOGI(TAG, "小路板子MCP工具已注册，支持 %zu 个自定义主题和烟花效果", theme_names_.size());
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

