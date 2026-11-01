#include "motor_driver.h"

#include "config.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_log.h>

#define TAG "MotorDriver"

namespace {

constexpr ledc_timer_t kMotorLedcTimer = LEDC_TIMER_1;
constexpr ledc_channel_t kMotorLedcLeft = LEDC_CHANNEL_1;
constexpr ledc_channel_t kMotorLedcRight = LEDC_CHANNEL_2;
constexpr ledc_timer_bit_t kMotorDutyRes = LEDC_TIMER_10_BIT;
constexpr uint32_t kMotorPwmHz = 20000;
constexpr uint32_t kMotorMaxDuty = (1u << 10) - 1u;

const char* ActionName(MotorAction action) {
    switch (action) {
        case MotorAction::kForward:
            return "forward";
        case MotorAction::kBackward:
            return "backward";
        case MotorAction::kLeft:
            return "left";
        case MotorAction::kRight:
            return "right";
        case MotorAction::kSpinLeft:
            return "spin_left";
        case MotorAction::kSpinRight:
            return "spin_right";
        case MotorAction::kTurnAround:
            return "turn_around";
    }
    return "?";
}

std::string Trim(std::string_view in) {
    size_t start = 0;
    while (start < in.size() && std::isspace(static_cast<unsigned char>(in[start]))) {
        ++start;
    }
    size_t end = in.size();
    while (end > start && std::isspace(static_cast<unsigned char>(in[end - 1]))) {
        --end;
    }
    return std::string(in.substr(start, end - start));
}

}  // namespace

MotorDriver& MotorDriver::GetInstance() {
    static MotorDriver instance;
    return instance;
}

void MotorDriver::Initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return;
    }

    gpio_config_t io = {};
#if defined(MOTOR_DRIVE_A_PIN)
    // RC-Car Drive (e.g. bread-compact-wifi)
    io.pin_bit_mask = (1ULL << MOTOR_DRIVE_A_PIN) | (1ULL << MOTOR_DRIVE_B_PIN) | 
                      (1ULL << MOTOR_STEER_A_PIN) | (1ULL << MOTOR_STEER_B_PIN);
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(MOTOR_DRIVE_A_PIN, 0);
    gpio_set_level(MOTOR_DRIVE_B_PIN, 0);
    gpio_set_level(MOTOR_STEER_A_PIN, 0);
    gpio_set_level(MOTOR_STEER_B_PIN, 0);
#elif defined(MOTOR_AIN1)
    // Tank Drive (e.g. desktop-robot)
    io.pin_bit_mask = (1ULL << MOTOR_AIN1) | (1ULL << MOTOR_AIN2) | (1ULL << MOTOR_BIN1) |
                      (1ULL << MOTOR_BIN2) | (1ULL << MOTOR_STBY);
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(MOTOR_AIN1, 0);
    gpio_set_level(MOTOR_AIN2, 0);
    gpio_set_level(MOTOR_BIN1, 0);
    gpio_set_level(MOTOR_BIN2, 0);
    gpio_set_level(MOTOR_STBY, 0);

    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = kMotorDutyRes;
    timer.timer_num = kMotorLedcTimer;
    timer.freq_hz = kMotorPwmHz;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t left = {};
    left.gpio_num = MOTOR_PWMA;
    left.speed_mode = LEDC_LOW_SPEED_MODE;
    left.channel = kMotorLedcLeft;
    left.intr_type = LEDC_INTR_DISABLE;
    left.timer_sel = kMotorLedcTimer;
    left.duty = 0;
    left.hpoint = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&left));

    ledc_channel_config_t right = {};
    right.gpio_num = MOTOR_PWMB;
    right.speed_mode = LEDC_LOW_SPEED_MODE;
    right.channel = kMotorLedcRight;
    right.intr_type = LEDC_INTR_DISABLE;
    right.timer_sel = kMotorLedcTimer;
    right.duty = 0;
    right.hpoint = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&right));
#endif

    const esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) { static_cast<MotorDriver*>(arg)->OnStepTimer(); },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "motor_step",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &step_timer_));

    initialized_ = true;
#if defined(MOTOR_DRIVE_A_PIN)
    ESP_LOGI(TAG, "Motor driver ready (RC-car drive pins %d/%d, steer %d/%d)",
             MOTOR_DRIVE_A_PIN, MOTOR_DRIVE_B_PIN, MOTOR_STEER_A_PIN, MOTOR_STEER_B_PIN);
#else
    ESP_LOGI(TAG, "TB6612 ready (STBY low, duty %d%%)", kMotorDutyPercent);
#endif
}

void MotorDriver::SetMotionAllowed(bool allowed) {
    if (!allowed) {
        Stop();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    motion_allowed_ = allowed;
}

bool MotorDriver::IsMotionAllowed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return motion_allowed_;
}

bool MotorDriver::Move(MotorAction action, int duration_ms, MotorStyle style, int duty_percent) {
    MotorStep step{action, ClampDuration(duration_ms, style, action), style,
                   ClampDutyPercent(duty_percent)};
    return Queue({step});
}

bool MotorDriver::Queue(const std::vector<MotorStep>& steps) {
    if (steps.empty() || steps.size() > static_cast<size_t>(kMotorQueueMax)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        return false;
    }
    if (!motion_allowed_) {
        ESP_LOGW(TAG, "blocked: low battery");
        return false;
    }
    const size_t depth = (running_ ? 1 : 0) + queue_.size();
    if (depth + steps.size() > static_cast<size_t>(kMotorQueueMax)) {
        ESP_LOGW(TAG, "Queue full (%u + %u)", static_cast<unsigned>(depth),
                 static_cast<unsigned>(steps.size()));
        return false;
    }

    if (!running_) {
        StartStepLocked(steps[0]);
        for (size_t i = 1; i < steps.size(); ++i) {
            queue_.push_back(steps[i]);
        }
    } else {
        for (const auto& step : steps) {
            queue_.push_back(step);
        }
    }
    return true;
}

bool MotorDriver::ParseSequence(const std::string& sequence, MotorStyle style, std::string* error,
                                int duty_percent) {
    std::vector<MotorStep> steps;
    std::string_view rest(sequence);
    while (!rest.empty()) {
        size_t comma = rest.find(',');
        std::string token = Trim(rest.substr(0, comma));
        rest = (comma == std::string_view::npos) ? std::string_view() : rest.substr(comma + 1);
        if (token.empty()) {
            continue;
        }
        size_t colon = token.find(':');
        std::string name = (colon == std::string::npos) ? token : token.substr(0, colon);
        name = Trim(name);
        MotorAction action;
        if (!ParseAction(name, &action)) {
            if (error) {
                *error = "Unknown action: " + name;
            }
            return false;
        }
        int duration = 0;
        if (colon != std::string::npos) {
            duration = atoi(Trim(token.substr(colon + 1)).c_str());
        }
        steps.push_back({action, ClampDuration(duration, style, action), style,
                         ClampDutyPercent(duty_percent)});
    }
    if (steps.empty()) {
        if (error) {
            *error = "Empty sequence";
        }
        return false;
    }
    if (steps.size() > static_cast<size_t>(kMotorQueueMax)) {
        if (error) {
            *error = "Sequence longer than 3 steps";
        }
        return false;
    }
    if (!Queue(steps)) {
        if (error) {
            *error = "Queue full";
        }
        return false;
    }
    return true;
}

void MotorDriver::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (step_timer_ != nullptr) {
        esp_timer_stop(step_timer_);
    }
    queue_.clear();
    running_ = false;
    if (!initialized_) {
#if defined(MOTOR_STBY)
        gpio_set_level(MOTOR_STBY, 0);
#endif
        return;
    }
#if defined(MOTOR_DRIVE_A_PIN)
    gpio_set_level(MOTOR_DRIVE_A_PIN, 0);
    gpio_set_level(MOTOR_DRIVE_B_PIN, 0);
    gpio_set_level(MOTOR_STEER_A_PIN, 0);
    gpio_set_level(MOTOR_STEER_B_PIN, 0);
#else
    SetWheel(true, 0);
    SetWheel(false, 0);
#if defined(MOTOR_STBY)
    gpio_set_level(MOTOR_STBY, 0);
#endif
#endif
}

void MotorDriver::OnEmotion(const char* emotion) {
#if !MOTOR_EMOTION_FALLBACK_ENABLE
    (void)emotion;
    return;
#else
    if (emotion == nullptr || emotion[0] == '\0') {
        return;
    }
    if (IsMoving()) {
        return;
    }

    if (strcmp(emotion, "happy") == 0 || strcmp(emotion, "laughing") == 0 ||
        strcmp(emotion, "funny") == 0) {
        Move(MotorAction::kSpinLeft, kMotorHappySpinMs, MotorStyle::kGesture);
        return;
    }
    if (strcmp(emotion, "confused") == 0 || strcmp(emotion, "thinking") == 0 ||
        strcmp(emotion, "embarrassed") == 0) {
        Queue({
            {MotorAction::kLeft, kMotorWiggleMs, MotorStyle::kGesture, kMotorDutyPercent},
            {MotorAction::kRight, kMotorWiggleMs, MotorStyle::kGesture, kMotorDutyPercent},
        });
    }
#endif
}

bool MotorDriver::IsMoving() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

void MotorDriver::StartStepLocked(const MotorStep& step) {
    current_ = step;
    current_.duty_percent = ClampDutyPercent(current_.duty_percent);
    running_ = true;
    ApplyHardwareLocked();
    if (step_timer_ != nullptr) {
        esp_timer_stop(step_timer_);
        ESP_ERROR_CHECK(
            esp_timer_start_once(step_timer_, static_cast<uint64_t>(step.duration_ms) * 1000));
    }
    ESP_LOGI(TAG, "Move action=%s duration=%d ms duty=%d%%", ActionName(current_.action),
             current_.duration_ms, current_.duty_percent);
}

void MotorDriver::OnStepTimer() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!queue_.empty()) {
        auto next = queue_.front();
        queue_.pop_front();
        StartStepLocked(next);
        return;
    }
    running_ = false;
#if defined(MOTOR_DRIVE_A_PIN)
    gpio_set_level(MOTOR_DRIVE_A_PIN, 0);
    gpio_set_level(MOTOR_DRIVE_B_PIN, 0);
    gpio_set_level(MOTOR_STEER_A_PIN, 0);
    gpio_set_level(MOTOR_STEER_B_PIN, 0);
#else
    SetWheel(true, 0);
    SetWheel(false, 0);
#if defined(MOTOR_STBY)
    gpio_set_level(MOTOR_STBY, 0);
#endif
#endif
}

void MotorDriver::ApplyHardwareLocked() {
#if defined(MOTOR_DRIVE_A_PIN)
    int drive = 0;
    int steer = 0;
    switch (current_.action) {
        case MotorAction::kForward:    drive = 1; break;
        case MotorAction::kBackward:   drive = -1; break;
        case MotorAction::kLeft:       drive = 1; steer = -1; break;
        case MotorAction::kRight:      drive = 1; steer = 1; break;
        case MotorAction::kSpinLeft:   steer = -1; break;
        case MotorAction::kSpinRight:  steer = 1; break;
        case MotorAction::kTurnAround: steer = 1; drive = -1; break;
    }
#if MOTOR_DRIVE_INVERT
    drive = -drive;
#endif
#if MOTOR_STEER_INVERT
    steer = -steer;
#endif
    gpio_set_level(MOTOR_DRIVE_A_PIN, drive > 0 ? 1 : 0);
    gpio_set_level(MOTOR_DRIVE_B_PIN, drive < 0 ? 1 : 0);
    
    gpio_set_level(MOTOR_STEER_A_PIN, steer > 0 ? 1 : 0);
    gpio_set_level(MOTOR_STEER_B_PIN, steer < 0 ? 1 : 0);
#elif defined(MOTOR_AIN1)
    int left = 0;
    int right = 0;
    switch (current_.action) {
        case MotorAction::kForward:
            left = 1;
            right = 1;
            break;
        case MotorAction::kBackward:
            left = -1;
            right = -1;
            break;
        case MotorAction::kLeft:
        case MotorAction::kSpinLeft:
        case MotorAction::kTurnAround:
            left = -1;
            right = 1;
            break;
        case MotorAction::kRight:
        case MotorAction::kSpinRight:
            left = 1;
            right = -1;
            break;
    }
#if MOTOR_CHASSIS_INVERT
    left = -left;
    right = -right;
#endif
    gpio_set_level(MOTOR_STBY, 1);
    SetWheel(true, left);
    SetWheel(false, right);
#endif
}

void MotorDriver::SetWheel(bool left, int sign) {
#if defined(MOTOR_AIN1)
    if (left && MOTOR_LEFT_INVERT) {
        sign = -sign;
    } else if (!left && MOTOR_RIGHT_INVERT) {
        sign = -sign;
    }
    const gpio_num_t in1 = left ? MOTOR_AIN1 : MOTOR_BIN1;
    const gpio_num_t in2 = left ? MOTOR_AIN2 : MOTOR_BIN2;
    const ledc_channel_t channel = left ? kMotorLedcLeft : kMotorLedcRight;
    if (sign > 0) {
        gpio_set_level(in1, 1);
        gpio_set_level(in2, 0);
    } else if (sign < 0) {
        gpio_set_level(in1, 0);
        gpio_set_level(in2, 1);
    } else {
        gpio_set_level(in1, 0);
        gpio_set_level(in2, 0);
    }
    const uint32_t duty =
        (sign == 0) ? 0 : (kMotorMaxDuty * static_cast<uint32_t>(current_.duty_percent)) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
#endif
}

int MotorDriver::ClampDuration(int duration_ms, MotorStyle style, MotorAction action) const {
    if (duration_ms <= 0) {
        duration_ms = DefaultDuration(action);
        if (style == MotorStyle::kGesture && action == MotorAction::kSpinLeft) {
            duration_ms = kMotorHappySpinMs;
        }
    }
    if (duration_ms < kMotorMinDurationMs) {
        duration_ms = kMotorMinDurationMs;
    }
    const int cap = (style == MotorStyle::kGesture) ? kMotorGestureMaxMs : kMotorCommandMaxMs;
    if (duration_ms > cap) {
        duration_ms = cap;
    }
    return duration_ms;
}

int MotorDriver::ClampDutyPercent(int duty_percent) {
    if (duty_percent <= 0) {
        duty_percent = kMotorDutyPercent;
    }
    if (duty_percent < kMotorMinDutyPercent) {
        duty_percent = kMotorMinDutyPercent;
    }
    if (duty_percent > kMotorMaxDutyPercent) {
        duty_percent = kMotorMaxDutyPercent;
    }
    return duty_percent;
}

bool MotorDriver::ParseActionName(const std::string& name, MotorAction* action) {
    return ParseAction(name, action);
}

MotorStyle MotorDriver::ParseStyle(const std::string& style) {
    if (style == "gesture") {
        return MotorStyle::kGesture;
    }
    return MotorStyle::kCommand;
}

bool MotorDriver::ParseAction(const std::string& name, MotorAction* action) {
    if (name == "forward") {
        *action = MotorAction::kForward;
    } else if (name == "backward") {
        *action = MotorAction::kBackward;
    } else if (name == "left") {
        *action = MotorAction::kLeft;
    } else if (name == "right") {
        *action = MotorAction::kRight;
    } else if (name == "spin_left") {
        *action = MotorAction::kSpinLeft;
    } else if (name == "spin_right") {
        *action = MotorAction::kSpinRight;
    } else if (name == "turn_around") {
        *action = MotorAction::kTurnAround;
    } else {
        return false;
    }
    return true;
}

int MotorDriver::DefaultDuration(MotorAction action) {
    switch (action) {
        case MotorAction::kForward:
            return kMotorDefaultForwardMs;
        case MotorAction::kBackward:
            return kMotorDefaultBackwardMs;
        case MotorAction::kLeft:
        case MotorAction::kRight:
            return kMotorDefaultTurnMs;
        case MotorAction::kSpinLeft:
        case MotorAction::kSpinRight:
            return kMotorDefaultSpin90Ms;
        case MotorAction::kTurnAround:
            return kMotorDefaultTurnAroundMs;
    }
    return kMotorDefaultForwardMs;
}
