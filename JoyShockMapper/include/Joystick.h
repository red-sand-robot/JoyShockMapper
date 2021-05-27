#pragma once

#include "JoyShockMapper.h"
#include "JoyShockContext.h"
#include <chrono>

class DigitalButton;
class JoyShock;

constexpr int NumSamples = 64;

class ScrollAxis
{
protected:
	float _leftovers;
	DigitalButton *_negativeButton;
	DigitalButton *_positiveButton;
	int _touchpadId;
	ButtonID _pressedBtn;

public:
	static function<void(JoyShock *, ButtonID, int, bool)> _handleButtonChange;

	ScrollAxis();

	void init(DigitalButton &negativeBtn, DigitalButton &positiveBtn, int touchpadId = -1);

	void ProcessScroll(float distance, float sens, chrono::steady_clock::time_point now);

	void Reset(chrono::steady_clock::time_point now);
};


class Joystick
{
public:
	Joystick(ButtonID left, ButtonID right, ButtonID up, ButtonID down, ButtonID ring, SettingID innerThreshold,
	SettingID outerThreshold, SettingID ringMode, SettingID stickMode, shared_ptr<JoyShockContext> _context);

	bool processStick(FloatXY stickPos, FloatXY sens, int split_type, float mouseCalibrationFactor, float deltaTime, 
		 bool &lockMouse, float &camSpeedX, float &camSpeedY);

	const ButtonID LEFT;
	const ButtonID RIGHT;
	const ButtonID UP;
	const ButtonID DOWN;
	const ButtonID RING;
	const SettingID INNER_THRESHOLD;
	const SettingID OUTER_THRESHOLD;
	const SettingID RING_MODE;
	const SettingID STICK_MODE;


protected:
	map<ButtonID, DigitalButton> _stickdirections;
	
	// return true if it hits the outer deadzone
	bool processDeadZones(float &x, float &y, float innerDeadzone, float outerDeadzone);

	float handleFlickStick(float calX, float calY, float lastCalX, float lastCalY, float stickLength, float mouseCalibrationFactor, bool FLICK_ONLY, bool ROTATE_ONLY);

	void ResetSmoothSample();

	float GetSmoothedStickRotation(float value, float bottomThreshold, float topThreshold, int maxSamples);

	void updateButton(DigitalButton &button, bool pressed);

	shared_ptr<JoyShockContext> _context;
	float acceleration = 1.f;
	FloatXY last_cal;
	bool is_flicking;
	// Modeshifting the stick mode can create quirky behaviours on transition. These flags
	// will be set upon returning to standard mode and ignore stick inputs until the stick
	// returns to neutral
	bool ignore_mode_flag = false;
	ScrollAxis scroll;
	int touchpadIndex = -1;
	FloatXY lastPos = {0.f, 0.f};
	chrono::steady_clock::time_point started_flick;
	float delta_flick = 0.0;
	float flick_percent_done = 0.0;
	float flick_rotation_counter = 0.0;
	array<float, NumSamples> _flickSamples;
	int _frontSample = 0;
};


class TouchStick : public Joystick
{
	int _index = -1;
	FloatXY _currentLocation = { 0.f, 0.f };
	bool _prevDown = false;
	ScrollAxis touch_scroll_y;
	// Handle a single touch related action. On per touch point
public:
	vector<DigitalButton> buttons; // Each touchstick gets it's own digital buttons. Is that smart?

	TouchStick(int index, shared_ptr<JoyShockContext> common);

	void handleTouchStickChange(shared_ptr<JoyShock> js, bool down, short movX, short movY, float delta_time, int split_type);

	inline bool wasDown()
	{
		return _prevDown;
	}
};
