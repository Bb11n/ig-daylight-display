#pragma once

#include <stdint.h>

enum class SystemPowerState : uint8_t {
    Running,
    ShutdownPending,
    SoftOff,
};

enum class PowerButtonPhase : uint8_t {
    Idle,
    Pressed,
    WaitForRelease,
};

enum class PowerButtonAction : uint8_t {
    None,
    EnterSoftOff,
    Restart,
};

struct PowerButtonUpdate {
    bool pressAccepted;
    bool releaseAccepted;
    uint32_t durationMs;
    PowerButtonAction action;
    PowerButtonAction completedAction;
};

class SystemPowerStateMachine {
public:
    static constexpr uint32_t kLongPressMs = 2000;

    PowerButtonUpdate update(bool pressEdge, bool releaseEdge, uint32_t nowMs);
    void completeSoftOff();

    SystemPowerState state() const;
    PowerButtonPhase buttonPhase() const;
    bool allowsBusinessWork() const;

private:
    SystemPowerState state_ = SystemPowerState::Running;
    PowerButtonPhase buttonPhase_ = PowerButtonPhase::Idle;
    PowerButtonAction triggeredAction_ = PowerButtonAction::None;
    uint32_t pressStartMs_ = 0;
};

const char* systemPowerStateName(SystemPowerState state);
const char* powerButtonActionName(PowerButtonAction action);
