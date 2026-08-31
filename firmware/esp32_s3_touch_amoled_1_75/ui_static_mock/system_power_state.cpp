#include "system_power_state.h"

PowerButtonUpdate SystemPowerStateMachine::update(
    bool pressEdge,
    bool releaseEdge,
    uint32_t nowMs
)
{
    PowerButtonUpdate result = {
        false,
        false,
        0,
        PowerButtonAction::None,
        PowerButtonAction::None,
    };

    if (pressEdge && buttonPhase_ == PowerButtonPhase::Idle) {
        buttonPhase_ = PowerButtonPhase::Pressed;
        pressStartMs_ = nowMs;
        triggeredAction_ = PowerButtonAction::None;
        result.pressAccepted = true;
    }

    if (buttonPhase_ == PowerButtonPhase::Pressed) {
        result.durationMs = nowMs - pressStartMs_;
        if (result.durationMs >= kLongPressMs) {
            triggeredAction_ = state_ == SystemPowerState::SoftOff
                ? PowerButtonAction::Restart
                : PowerButtonAction::EnterSoftOff;
            if (triggeredAction_ == PowerButtonAction::EnterSoftOff) {
                state_ = SystemPowerState::ShutdownPending;
            }
            result.action = triggeredAction_;
            buttonPhase_ = PowerButtonPhase::WaitForRelease;
        }
    }

    if (releaseEdge && buttonPhase_ != PowerButtonPhase::Idle) {
        result.releaseAccepted = true;
        result.durationMs = nowMs - pressStartMs_;
        result.completedAction = triggeredAction_;
        buttonPhase_ = PowerButtonPhase::Idle;
        triggeredAction_ = PowerButtonAction::None;
    }

    return result;
}

void SystemPowerStateMachine::completeSoftOff()
{
    if (state_ == SystemPowerState::ShutdownPending) {
        state_ = SystemPowerState::SoftOff;
    }
}

SystemPowerState SystemPowerStateMachine::state() const
{
    return state_;
}

PowerButtonPhase SystemPowerStateMachine::buttonPhase() const
{
    return buttonPhase_;
}

bool SystemPowerStateMachine::allowsBusinessWork() const
{
    return state_ == SystemPowerState::Running;
}

const char* systemPowerStateName(SystemPowerState state)
{
    switch (state) {
    case SystemPowerState::Running:
        return "running";
    case SystemPowerState::ShutdownPending:
        return "shutdown_pending";
    case SystemPowerState::SoftOff:
        return "soft_off";
    }
    return "unknown";
}

const char* powerButtonActionName(PowerButtonAction action)
{
    switch (action) {
    case PowerButtonAction::None:
        return "none";
    case PowerButtonAction::EnterSoftOff:
        return "soft_off";
    case PowerButtonAction::Restart:
        return "restart";
    }
    return "unknown";
}
