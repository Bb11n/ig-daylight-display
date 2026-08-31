#include "system_power_state.h"

#include <assert.h>
#include <stdio.h>

namespace {

PowerButtonUpdate pressFor(SystemPowerStateMachine& state, uint32_t startMs, uint32_t durationMs)
{
    const PowerButtonUpdate pressed = state.update(true, false, startMs);
    assert(pressed.pressAccepted);
    state.update(false, false, startMs + durationMs);
    return state.update(false, true, startMs + durationMs);
}

void verifyShortPress(uint32_t durationMs)
{
    SystemPowerStateMachine state;
    const PowerButtonUpdate released = pressFor(state, 100, durationMs);
    assert(released.releaseAccepted);
    assert(released.durationMs == durationMs);
    assert(released.completedAction == PowerButtonAction::None);
    assert(state.state() == SystemPowerState::Running);
    assert(state.allowsBusinessWork());
}

}  // namespace

int main()
{
    verifyShortPress(100);
    verifyShortPress(1000);
    verifyShortPress(1999);

    bool projectionOn = true;
    SystemPowerStateMachine state;
    state.update(true, false, 1000);
    const PowerButtonUpdate longPress = state.update(false, false, 3000);
    assert(longPress.action == PowerButtonAction::EnterSoftOff);
    assert(state.state() == SystemPowerState::ShutdownPending);
    assert(!state.allowsBusinessWork());
    assert(projectionOn);

    const PowerButtonUpdate held = state.update(false, false, 6000);
    assert(held.action == PowerButtonAction::None);
    assert(state.buttonPhase() == PowerButtonPhase::WaitForRelease);

    state.completeSoftOff();
    assert(state.state() == SystemPowerState::SoftOff);
    assert(!state.allowsBusinessWork());

    const PowerButtonUpdate released = state.update(false, true, 6100);
    assert(released.releaseAccepted);
    assert(released.completedAction == PowerButtonAction::EnterSoftOff);
    assert(state.buttonPhase() == PowerButtonPhase::Idle);

    const PowerButtonUpdate softOffShort = pressFor(state, 7000, 1000);
    assert(softOffShort.completedAction == PowerButtonAction::None);
    assert(state.state() == SystemPowerState::SoftOff);

    state.update(true, false, 9000);
    const PowerButtonUpdate restart = state.update(false, false, 11000);
    assert(restart.action == PowerButtonAction::Restart);
    assert(state.state() == SystemPowerState::SoftOff);
    assert(state.update(false, false, 12000).action == PowerButtonAction::None);

    puts("SYSTEM_POWER_STATE_TEST: PASS cases=13");
    return 0;
}
