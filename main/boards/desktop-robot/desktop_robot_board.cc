#include "adc_battery_monitor.h"
#include "application.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "device_state.h"
#include "display/lcd_display.h"
#include "led/single_led.h"
#include "mcp_server.h"
#include "motor_driver.h"
#include "robot_faces.h"
#include "robot_http_control.h"
#include "wifi_board.h"

#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_timer.h>

#if defined(LCD_TYPE_ILI9341_SERIAL)
#include "esp_lcd_ili9341.h"
#endif

#if defined(LCD_TYPE_GC9A01_SERIAL)
#include "esp_lcd_gc9a01.h"
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0,
     (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C, 0x04, 0x12, 0x14,
                 0x1f},
     14, 0},
    {0xf1,
     (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D, 0x0C, 0x1A, 0x14,
                 0x1E},
     14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
};
#endif

#define TAG "DesktopRobotBoard"

// MCP descriptions from documentation 05-software/xiaozhi-role-prompt.md (times in the
// description so the LLM does not invent PWM).
static const char kMoveToolDescription[] =
    "Drive the two wheels ONLY when the user clearly asked to move. Never move as a greeting, "
    "emotion, or filler. Named actions, milliseconds, and optional duty_percent (25-80). Do not "
    "send raw PWM ticks. Default duty is 50%. Hard max 8000 ms.\n\n"
    "Actions: forward, backward, spin_left, spin_right, turn_around.\n"
    "Do not use left or right (those names are confusing). forward = toward the screen. "
    "backward = away from the screen.\n\n"
    "Default times:\n"
    "- go forward / come here: forward 3000 ms, duty_percent 50, style=command\n"
    "- go farther / keep going: forward 6000 to 8000 ms, duty_percent 60\n"
    "- go back / reverse: backward 2000 ms\n"
    "- user says turn left 90: spin_right 800 ms (hardware left/right are swapped)\n"
    "- user says turn right 90: spin_left 800 ms\n"
    "- turn around: turn_around 1600 ms\n"
    "- user says turn left then go: sequence \"spin_right:800,forward:3000\"\n"
    "- user says turn right then go: sequence \"spin_left:800,forward:3000\"\n\n"
    "If sequence is set, ignore action. Call self.robot.stop when they say stop, wait, freeze, "
    "or don't move.";

static const char kStopToolDescription[] =
    "Stop both wheels immediately. Clear any queued moves. Use when the user says stop, wait, "
    "freeze, halt, or don't move. Do not use this to stop speech (use self.robot.quiet).";

static const char kQuietToolDescription[] =
    "Abort TTS and stay in the conversation. The speaker stops; the robot keeps listening and "
    "does not return to wake-word idle. Call this when the user says stop talking, be quiet, "
    "shut up, or hush. Do not speak after calling this. Do not call this to stop wheels "
    "(use self.robot.stop). Do not call this to end the session (use self.robot.standby).";

static const char kStandbyToolDescription[] =
    "End the voice session and return to wake-word standby (the state before Hi ESP). Stops "
    "wheels, closes the audio channel, and waits for the wake word again. Call this when the "
    "user says return to standby, go to sleep, that's all, goodbye, or stop listening. Do not "
    "call this to stop wheels only (use self.robot.stop). Do not call this to stop talking only "
    "(use self.robot.quiet). Do not call this from idle.";

class DesktopRobotDisplay : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;
    void SetEmotion(const char* emotion) override {
        MotorDriver::GetInstance().OnEmotion(emotion);
        InstallDesktopRobotFaces();
        SpiLcdDisplay::SetEmotion(MapDesktopRobotEmotion(emotion));
    }
};

class DesktopRobotLed : public SingleLed {
public:
    using SingleLed::SingleLed;
    void OnStateChanged() override {
        if (Application::GetInstance().GetDeviceState() == kDeviceStateListening) {
            MotorDriver::GetInstance().Stop();
        }
        SingleLed::OnStateChanged();
    }
};

class DesktopRobotBoard : public WifiBoard {
private:
    Button boot_button_;
    LcdDisplay* display_;
    AdcBatteryMonitor* adc_battery_monitor_ = nullptr;
    bool battery_was_low_ = false;
    esp_timer_handle_t quiet_timer_ = nullptr;

    static void QuietTimerCallback(void* /*arg*/) {
        Application::GetInstance().Schedule([]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateSpeaking) {
                app.AbortSpeaking(kAbortReasonNone);
            }
        });
    }

    void EnsureQuietTimer() {
        if (quiet_timer_ != nullptr) {
            return;
        }
        esp_timer_create_args_t args = {};
        args.callback = &QuietTimerCallback;
        args.name = "robot_quiet";
        ESP_ERROR_CHECK(esp_timer_create(&args, &quiet_timer_));
    }

    void RequestQuiet() {
        EnsureQuietTimer();
        Application::GetInstance().Schedule([]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateSpeaking) {
                app.AbortSpeaking(kAbortReasonNone);
            }
        });
        // Tool often runs before TTS starts; abort again shortly after.
        esp_timer_stop(quiet_timer_);
        ESP_ERROR_CHECK(esp_timer_start_once(quiet_timer_, 400000));
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
#if defined(LCD_TYPE_ILI9341_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
#ifdef LCD_TYPE_GC9A01_SERIAL
        panel_config.vendor_config = &gc9107_vendor_config;
#endif
        display_ = new DesktopRobotDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                           DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
                                           DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool(
            "self.robot.move", kMoveToolDescription,
            PropertyList({
                Property("action", kPropertyTypeString, std::string("")),
                Property("duration_ms", kPropertyTypeInteger, 0, 0, kMotorCommandMaxMs),
                Property("duty_percent", kPropertyTypeInteger, 0, 0, kMotorMaxDutyPercent),
                Property("style", kPropertyTypeString, std::string("command")),
                Property("sequence", kPropertyTypeString, std::string("")),
            }),
            [](const PropertyList& properties) -> ReturnValue {
                auto& motors = MotorDriver::GetInstance();
                if (!motors.IsMotionAllowed()) {
                    return std::string("Battery too low");
                }
                const std::string sequence = properties["sequence"].value<std::string>();
                const MotorStyle style =
                    MotorDriver::ParseStyle(properties["style"].value<std::string>());
                const int duty_percent = properties["duty_percent"].value<int>();
                if (!sequence.empty()) {
                    std::string error;
                    if (!motors.ParseSequence(sequence, style, &error, duty_percent)) {
                        return error;
                    }
                    return true;
                }
                const std::string action_name = properties["action"].value<std::string>();
                MotorAction action;
                if (!MotorDriver::ParseActionName(action_name, &action)) {
                    return std::string(
                        "Unknown action. Use forward|backward|left|right|"
                        "spin_left|spin_right|turn_around or a sequence string.");
                }
                const int duration_ms = properties["duration_ms"].value<int>();
                if (!motors.Move(action, duration_ms, style, duty_percent)) {
                    return std::string("Queue full");
                }
                return true;
            });

        mcp_server.AddTool("self.robot.stop", kStopToolDescription, PropertyList(),
                           [](const PropertyList&) -> ReturnValue {
                               MotorDriver::GetInstance().Stop();
                               return true;
                           });

        mcp_server.AddTool("self.robot.quiet", kQuietToolDescription, PropertyList(),
                           [this](const PropertyList&) -> ReturnValue {
                               RequestQuiet();
                               return true;
                           });

        mcp_server.AddTool("self.robot.standby", kStandbyToolDescription, PropertyList(),
                           [](const PropertyList&) -> ReturnValue {
                               MotorDriver::GetInstance().Stop();
                               // MCP runs off the main task. Schedule mutations; do not
                               // ToggleChatState from idle (that would start listening).
                               Application::GetInstance().Schedule([]() {
                                   auto& app = Application::GetInstance();
                                   const auto state = app.GetDeviceState();
                                   if (state == kDeviceStateIdle) {
                                       return;
                                   }
                                   if (state == kDeviceStateSpeaking) {
                                       app.AbortSpeaking(kAbortReasonNone);
                                       app.Schedule([]() {
                                           auto& a = Application::GetInstance();
                                           if (a.GetDeviceState() == kDeviceStateListening) {
                                               a.ToggleChatState();
                                           }
                                       });
                                       return;
                                   }
                                   if (state == kDeviceStateListening) {
                                       app.ToggleChatState();
                                   }
                               });
                               return true;
                           });
    }

    void InitializeBatteryMonitor() {
        if (!BATTERY_MONITOR_ENABLE) {
            ESP_LOGI(TAG, "Battery monitor off until the divider is soldered");
            return;
        }
        adc_battery_monitor_ = new AdcBatteryMonitor(ADC_UNIT_1, ADC_CHANNEL_0, BATTERY_DIVIDER_R1,
                                                     BATTERY_DIVIDER_R2, GPIO_NUM_NC);
        ESP_LOGI(TAG, "Battery ADC on GPIO%d (÷2, lock %s)", static_cast<int>(BATTERY_ADC_GPIO),
                 BATTERY_LOW_MOTOR_LOCK ? "on" : "off");
    }

    void Start() override {
        WifiBoard::Start();
        // Start HTTP control server once Wi-Fi stack is up.
        RobotHttpControlStart();
    }

public:
    DesktopRobotBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        MotorDriver::GetInstance().Initialize();
        InitializeBatteryMonitor();
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        InitializeTools();
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
    }

    virtual Led* GetLed() override {
        static DesktopRobotLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                               AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK,
                                               AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK,
                                               AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                              AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS,
                                              AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        if (!BATTERY_MONITOR_ENABLE || adc_battery_monitor_ == nullptr) {
            return false;
        }
        charging = adc_battery_monitor_->IsCharging();
        discharging = adc_battery_monitor_->IsDischarging();
        level = adc_battery_monitor_->GetBatteryLevel();
        if (BATTERY_LOW_MOTOR_LOCK) {
            const bool low = level <= BATTERY_LOW_PERCENT;
            MotorDriver::GetInstance().SetMotionAllowed(!low);
            if (low && !battery_was_low_ && display_ != nullptr) {
                display_->SetEmotion("sleepy");
            }
            battery_was_low_ = low;
        }
        return true;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }
};

DECLARE_BOARD(DesktopRobotBoard);
