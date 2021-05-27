#pragma once

#include "pocket_fsm.h"
#include "JoyShockMapper.h"
#include "Gamepad.h"
#include <chrono>
#include <deque>
#include <mutex>

// Forward declarations
class JSMButton;
class GamepadMotion;
class DigitalButton;      // Finite State Machine
struct DigitalButtonImpl; // Button implementation
struct JoyShockContext;

// The enum values match the concrete class names
enum class BtnState
{
	NoPress,
	BtnPress,
	TapPress,
	WaitSim,
	SimPressMaster,
	SimPressSlave,
	SimRelease,
	DblPressStart,
	DblPressNoPress,
	DblPressNoPressTap,
	DblPressNoPressHold,
	DblPressPress,
	InstRelease,
	INVALID
};

// Send this event anytime the button is pressed down or active
struct Pressed
{
	chrono::steady_clock::time_point time_now; // Timestamp of the poll
	float turboTime;                           // active turbo period setting in ms
	float holdTime;                            // active hold press setting in ms
	float dblPressWindow;					   // active dbl press window setting in ms
};

// Send this event anytime the button is at rest or inactive
struct Released
{
	chrono::steady_clock::time_point time_now; // Timestamp of the poll
	float turboTime;                           // active turbo period setting in ms
	float holdTime;                            // active hold press setting in ms
	float dblPressWindow;					   // active dbl press window setting in ms
};

// The sync event is created internally
struct Sync;

// Getter for the button press duration. Pass timestamp of the poll.
struct GetDuration
{
	chrono::steady_clock::time_point in_now;
	float out_duration;
};

// Setter for the press time
typedef chrono::steady_clock::time_point SetPressTime;

// A basic digital button state reacts to the following events
class DigitalButtonState : public pocket_fsm::StatePimplIF<DigitalButtonImpl>
{
	BASE_STATE(DigitalButtonState)

	// Display logs on entry for debigging
	REACT(OnEntry)
	override;

	// ignored
	REACT(OnExit)
	override { }

	// Adds chord to stack if absent
	REACT(Pressed);

	// Remove chord from stack if present
	REACT(Released);

	// ignored by default
	REACT(Sync) { }

	// Always assign press time
	REACT(SetPressTime)
	final;

	// Return press duration
	REACT(GetDuration)
	final;

	// Get matching enum value
	virtual BtnState getState() const = 0;
};

// Feed this state machine with Pressed and Released events and it will sort out
// what mappings to activate internally
class DigitalButton : public pocket_fsm::FiniteStateMachine<DigitalButtonState>
{
public:
	DigitalButton(shared_ptr<JoyShockContext> _context, JSMButton &mapping);

	const ButtonID _id;

	// Get the enum identifier of the current state
	BtnState getState() const
	{
		return getCurrentState()->getState();
	}
};
