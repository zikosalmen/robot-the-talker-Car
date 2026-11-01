#ifndef DESKTOP_ROBOT_MOTOR_DRIVER_H_
#define DESKTOP_ROBOT_MOTOR_DRIVER_H_

#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <esp_timer.h>

// Open-loop times. PWM is percent duty (not raw LEDC). Clamp after T7 if needed.
static constexpr int kMotorDutyPercent = 50;
static constexpr int kMotorMinDutyPercent = 25;
static constexpr int kMotorMaxDutyPercent = 80;
static constexpr int kMotorMinDurationMs = 200;
static constexpr int kMotorCommandMaxMs = 8000;
static constexpr int kMotorGestureMaxMs = 1500;
static constexpr int kMotorQueueMax = 3;
static constexpr int kMotorDefaultForwardMs = 3000;
static constexpr int kMotorDefaultBackwardMs = 2000;
static constexpr int kMotorDefaultTurnMs = 800;
static constexpr int kMotorDefaultSpin90Ms = 800;
static constexpr int kMotorDefaultTurnAroundMs = 1600;
static constexpr int kMotorHappySpinMs = 400;
static constexpr int kMotorWiggleMs = 250;

enum class MotorAction {
    kForward,
    kBackward,
    kLeft,
    kRight,
    kSpinLeft,
    kSpinRight,
    kTurnAround,
};

enum class MotorStyle {
    kCommand,
    kGesture,
};

struct MotorStep {
    MotorAction action;
    int duration_ms;
    MotorStyle style;
    int duty_percent;
};

class MotorDriver {
public:
    static MotorDriver& GetInstance();

    void Initialize();
    // Enqueue one step, or start it immediately if idle. Returns false if the queue is full.
    bool Move(MotorAction action, int duration_ms, MotorStyle style, int duty_percent = 0);
    void SetMotionAllowed(bool allowed);
    bool IsMotionAllowed() const;
    bool Queue(const std::vector<MotorStep>& steps);
    bool ParseSequence(const std::string& sequence, MotorStyle style, std::string* error,
                       int duty_percent = 0);
    void Stop();
    void OnEmotion(const char* emotion);
    bool IsMoving() const;
    static bool ParseActionName(const std::string& name, MotorAction* action);
    static MotorStyle ParseStyle(const std::string& style);

private:
    MotorDriver() = default;

    void ApplyHardwareLocked();
    void StartStepLocked(const MotorStep& step);
    void OnStepTimer();
    void SetWheel(bool left, int sign);
    int ClampDuration(int duration_ms, MotorStyle style, MotorAction action) const;
    static int ClampDutyPercent(int duty_percent);
    static bool ParseAction(const std::string& name, MotorAction* action);
    static int DefaultDuration(MotorAction action);

    mutable std::mutex mutex_;
    bool initialized_ = false;
    bool running_ = false;
    bool motion_allowed_ = true;
    MotorStep current_{};
    std::deque<MotorStep> queue_;
    esp_timer_handle_t step_timer_ = nullptr;
};

#endif  // DESKTOP_ROBOT_MOTOR_DRIVER_H_
