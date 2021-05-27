#include "JslWrapper.h"
#include "Joystick.h"
#include "DigitalButton.h"
#include "InputHelpers.h"
#include <iomanip>
#include <algorithm>

extern JSMVariable<float> tick_time;
extern vector<JSMButton> mappings;
extern vector<JSMButton> touch_buttons;
extern float os_mouse_speed;
extern float last_flick_and_rotation;

ScrollAxis::ScrollAxis()
	: _leftovers(0.f)
	, _negativeButton(nullptr)
	, _positiveButton(nullptr)
	, _touchpadId(-1)
	, _pressedBtn(ButtonID::NONE)
{
}

void ScrollAxis::init(DigitalButton &negativeBtn, DigitalButton &positiveBtn, int touchpadId)
{
	_negativeButton = &negativeBtn;
	_positiveButton = &positiveBtn;
	_touchpadId = touchpadId;
}

void ScrollAxis::ProcessScroll(float distance, float sens, chrono::steady_clock::time_point now)
{
	if (!_negativeButton || !_positiveButton)
		return; // not initalized!

	_leftovers += distance;
	if (distance != 0)
		DEBUG_LOG << " leftover is now " << _leftovers << endl;
	//"[" << _negativeId << "," << _positiveId << "] moved " << distance << " so that

	Pressed isPressed;
	isPressed.time_now = now;
	isPressed.turboTime = 50;
	isPressed.holdTime = 150;
	Released isReleased;
	isReleased.time_now = now;
	isReleased.turboTime = 50;
	isReleased.holdTime = 150;
	if (_pressedBtn != ButtonID::NONE)
	{
		float pressedTime = 0;
		if (_pressedBtn == _negativeButton->_id)
		{
			pressedTime = _negativeButton->sendEvent(GetDuration{ now }).out_duration;
			if (pressedTime < MAGIC_TAP_DURATION)
			{
				_negativeButton->sendEvent(isPressed);
				_positiveButton->sendEvent(isReleased);
				return;
			}
		}
		else // _pressedBtn == _positiveButton->_id
		{
			pressedTime = _positiveButton->sendEvent(GetDuration{ now }).out_duration;
			if (pressedTime < MAGIC_TAP_DURATION)
			{
				_negativeButton->sendEvent(isReleased);
				_positiveButton->sendEvent(isPressed);
				return;
			}
		}
		// pressed time > TAP_DURATION meaning release the tap
		_negativeButton->sendEvent(isReleased);
		_positiveButton->sendEvent(isReleased);
		_pressedBtn = ButtonID::NONE;
	}
	else if (fabsf(_leftovers) > sens)
	{
		if (_leftovers > 0)
		{
			_negativeButton->sendEvent(isPressed);
			_positiveButton->sendEvent(isReleased);
			_pressedBtn = _negativeButton->_id;
		}
		else
		{
			_negativeButton->sendEvent(isReleased);
			_positiveButton->sendEvent(isPressed);
			_pressedBtn = _positiveButton->_id;
		}
		_leftovers = _leftovers > 0 ? _leftovers - sens : _leftovers + sens;
	}
	// else do nothing and accumulate leftovers
}

void ScrollAxis::Reset(chrono::steady_clock::time_point now)
{
	_leftovers = 0;
	Released isReleased;
	isReleased.time_now = now;
	isReleased.turboTime = 50;
	isReleased.holdTime = 150;
	_negativeButton->sendEvent(isReleased);
	_positiveButton->sendEvent(isReleased);
	_pressedBtn = ButtonID::NONE;
}

Joystick::Joystick(ButtonID left, ButtonID right, ButtonID up, ButtonID down, ButtonID ring, SettingID innerThreshold,
	SettingID outerThreshold, SettingID ringMode, SettingID stickMode, shared_ptr<JoyShockContext> _context)
	: LEFT(left)
	, RIGHT(right)
	, UP(up)
	, DOWN(down)
	, RING(ring)
	, INNER_THRESHOLD(innerThreshold)
	, OUTER_THRESHOLD(outerThreshold)
	, RING_MODE(ringMode)
	, STICK_MODE(STICK_MODE)
	, _context(_context)
{
	_stickdirections.emplace(left, DigitalButton(_context, mappings[int(left)]));
	_stickdirections.emplace(right, DigitalButton(_context, mappings[int(right)]));
	_stickdirections.emplace(up, DigitalButton(_context, mappings[int(up)]));
	_stickdirections.emplace(down, DigitalButton(_context, mappings[int(down)]));
	_stickdirections.emplace(ring, DigitalButton(_context, mappings[int(ring)]));
	scroll.init(_stickdirections[left], _stickdirections[right]);
	ResetSmoothSample();
}

bool Joystick::processStick(FloatXY stickPos, FloatXY sens, int controller_split_type, float mouseCalibrationFactor, float deltaTime, 
	bool &lockMouse, float &camSpeedX, float &camSpeedY)
{
	ControllerOrientation controllerOrientation = _context->getSetting<ControllerOrientation>(SettingID::CONTROLLER_ORIENTATION);
	bool anyStickInput = false;
	float temp;
	if (controllerOrientation == ControllerOrientation::JOYCON_SIDEWAYS)
	{
		if (controller_split_type == JS_SPLIT_TYPE_LEFT)
		{
			controllerOrientation = ControllerOrientation::LEFT;
		}
		else if (controller_split_type == JS_SPLIT_TYPE_RIGHT)
		{
			controllerOrientation = ControllerOrientation::RIGHT;
		}
		else
		{
			controllerOrientation = ControllerOrientation::FORWARD;
		}
	}

	switch (controllerOrientation)
	{
	case ControllerOrientation::LEFT:
		temp = stickPos.first;
		stickPos.first = -stickPos.second;
		stickPos.second = temp;
		temp = lastPos.first;
		lastPos.first = -lastPos.second;
		lastPos.second = temp;
		break;
	case ControllerOrientation::RIGHT:
		temp = stickPos.first;
		stickPos.first = stickPos.second;
		stickPos.second = -temp;
		temp = lastPos.first;
		lastPos.first = lastPos.second;
		lastPos.second = -temp;
		break;
	case ControllerOrientation::BACKWARD:
		stickPos.first = -stickPos.first;
		stickPos.second = -stickPos.second;
		lastPos.first = -lastPos.first;
		lastPos.second = -lastPos.second;
		break;
	}

	// Stick inversion
	stickPos.first *= signbit(sens.first) ? -1 : 1;
	stickPos.second *= signbit(sens.second) ? -1 : 1;
	lastPos.first *= signbit(sens.first) ? -1 : 1;
	lastPos.second *= signbit(sens.second) ? -1 : 1;

	float outerDeadzone = 1.0f - _context->getSetting(OUTER_THRESHOLD);
	processDeadZones(lastPos.first, lastPos.second, _context->getSetting(INNER_THRESHOLD), outerDeadzone);
	bool pegged = processDeadZones(stickPos.first, stickPos.second, _context->getSetting(INNER_THRESHOLD), outerDeadzone);
	float absX = abs(stickPos.first);
	float absY = abs(stickPos.second);
	bool left = stickPos.first < -0.5f * absY;
	bool right = stickPos.first > 0.5f * absY;
	bool down = stickPos.second < -0.5f * absX;
	bool up = stickPos.second > 0.5f * absX;
	float stickLength = sqrt(stickPos.first * stickPos.first + stickPos.second * stickPos.second);
	auto ringMode = _context->getSetting<RingMode>(RING_MODE);
	bool ring = ringMode == RingMode::INNER && stickLength > 0.0f && stickLength < 0.7f ||
	  ringMode == RingMode::OUTER && stickLength > 0.7f;
	updateButton(_stickdirections[RING], ring);

	auto stickMode = _context->getSetting<StickMode>(STICK_MODE);
	bool rotateOnly = stickMode == StickMode::ROTATE_ONLY;
	bool flickOnly = stickMode == StickMode::FLICK_ONLY;
	if (ignore_mode_flag && stickMode == StickMode::INVALID && stickPos.first == 0 && stickPos.second == 0)
	{
		// clear ignore flag when stick is back at neutral
		ignore_mode_flag = false;
	}
	else if (stickMode == StickMode::FLICK || flickOnly || rotateOnly)
	{
		camSpeedX += handleFlickStick(stickPos.first, stickPos.second, lastPos.first, lastPos.second, stickLength, mouseCalibrationFactor, flickOnly, rotateOnly);
		anyStickInput = pegged;
	}
	else if (stickMode == StickMode::AIM)
	{
		// camera movement
		if (!pegged)
		{
			acceleration = 1.0f; // reset
		}
		float stickLength = sqrt(stickPos.first * stickPos.first + stickPos.second * stickPos.second);
		if (stickLength != 0.0f)
		{
			anyStickInput = true;
			float warpedStickLengthX = pow(stickLength, _context->getSetting(SettingID::STICK_POWER));
			float warpedStickLengthY = warpedStickLengthX;
			warpedStickLengthX *= _context->getSetting<FloatXY>(SettingID::STICK_SENS).first * _context->getSetting(SettingID::REAL_WORLD_CALIBRATION) / os_mouse_speed / _context->getSetting(SettingID::IN_GAME_SENS);
			warpedStickLengthY *= _context->getSetting<FloatXY>(SettingID::STICK_SENS).second * _context->getSetting(SettingID::REAL_WORLD_CALIBRATION) / os_mouse_speed / _context->getSetting(SettingID::IN_GAME_SENS);
			camSpeedX += stickPos.first / stickLength * warpedStickLengthX * acceleration * deltaTime;
			camSpeedY += stickPos.second / stickLength * warpedStickLengthY * acceleration * deltaTime;
			if (pegged)
			{
				acceleration += _context->getSetting(SettingID::STICK_ACCELERATION_RATE) * deltaTime;
				auto cap = _context->getSetting(SettingID::STICK_ACCELERATION_CAP);
				if (acceleration > cap)
				{
					acceleration = cap;
				}
			}
		}
	}
	else if (stickMode == StickMode::MOUSE_AREA)
	{
		auto mouse_ring_radius = _context->getSetting(SettingID::MOUSE_RING_RADIUS);
		if (stickPos.first != 0.0f || stickPos.second != 0.0f)
		{
			// use difference with last cal values
			float mouseX = (stickPos.first - lastPos.first) * mouse_ring_radius;
			float mouseY = (stickPos.second - lastPos.second) * -1 * mouse_ring_radius;
			// do it!
			moveMouse(mouseX, mouseY);
			lastPos = stickPos;
		}
		else
		{
			// Return to center
			moveMouse(lastPos.first * -1 * mouse_ring_radius, lastPos.second * mouse_ring_radius);
			lastPos = { 0, 0 };
		}
	}
	else if (stickMode == StickMode::MOUSE_RING)
	{
		if (stickPos.first != 0.0f || stickPos.second != 0.0f)
		{
			auto mouse_ring_radius = _context->getSetting(SettingID::MOUSE_RING_RADIUS);
			float stickLength = sqrt(stickPos.first * stickPos.first + stickPos.second * stickPos.second);
			float normX = stickPos.first / stickLength;
			float normY = stickPos.second / stickLength;
			// use screen resolution
			float mouseX = (float)_context->getSetting(SettingID::SCREEN_RESOLUTION_X) * 0.5f + 0.5f + normX * mouse_ring_radius;
			float mouseY = (float)_context->getSetting(SettingID::SCREEN_RESOLUTION_Y) * 0.5f + 0.5f - normY * mouse_ring_radius;
			// normalize
			mouseX = mouseX / _context->getSetting(SettingID::SCREEN_RESOLUTION_X);
			mouseY = mouseY / _context->getSetting(SettingID::SCREEN_RESOLUTION_Y);
			// do it!
			setMouseNorm(mouseX, mouseY);
			lockMouse = true;
		}
	}
	else if (stickMode == StickMode::SCROLL_WHEEL)
	{
		if (stickPos.first == 0 && stickPos.second == 0)
		{
			scroll.Reset(_context->time_now);
		}
		else if (lastPos.first != 0 && lastPos.second != 0)
		{
			float lastAngle = atan2f(lastPos.second, lastPos.first) / PI * 180.f;
			float angle = atan2f(stickPos.second, stickPos.first) / PI * 180.f;
			if (((lastAngle > 0) ^ (angle > 0)) && fabsf(angle - lastAngle) > 270.f) // Handle loop the loop
			{
				lastAngle = lastAngle > 0 ? lastAngle - 360.f : lastAngle + 360.f;
			}
			//COUT << "Stick moved from " << lastAngle << " to " << angle; // << endl;
			scroll.ProcessScroll(angle - lastAngle, _context->getSetting<FloatXY>(SettingID::SCROLL_SENS).x(), _context->time_now);
		}
	}
	else if (stickMode == StickMode::NO_MOUSE || stickMode == StickMode::INNER_RING || stickMode == StickMode::OUTER_RING)
	{ // Do not do if invalid
		// left!
		updateButton(_stickdirections[LEFT], left);
		// right!
		updateButton(_stickdirections[RIGHT], right);
		// up!
		updateButton(_stickdirections[UP], up);
		// down!
		updateButton(_stickdirections[DOWN], down);

		anyStickInput = left || right || up || down; // ring doesn't count
	}
	else if (stickMode == StickMode::LEFT_STICK)
	{
		if (_context->_vigemController)
		{
			_context->_vigemController->setLeftStick(stickPos.first, stickPos.second);
		}
		anyStickInput = stickPos.first != 0 || stickPos.second != 0;
	}
	else if (stickMode == StickMode::RIGHT_STICK)
	{
		if (_context->_vigemController)
		{
			_context->_vigemController->setRightStick(stickPos.first, stickPos.second);
		}
		anyStickInput = stickPos.first != 0 || stickPos.second != 0;
	}
	return anyStickInput;
}

void Joystick::updateButton(DigitalButton &button, bool pressed)
{
	if (pressed)
	{
		Pressed evt;
		evt.time_now = _context->time_now;
		evt.turboTime = _context->getSetting(SettingID::TURBO_PERIOD);
		evt.holdTime = _context->getSetting(SettingID::HOLD_PRESS_TIME);
		evt.dblPressWindow = _context->getSetting(SettingID::DBL_PRESS_WINDOW);
		button.sendEvent(evt);
	}
	else
	{
		Released evt;
		evt.time_now = _context->time_now;
		evt.turboTime = _context->getSetting(SettingID::TURBO_PERIOD);
		evt.holdTime = _context->getSetting(SettingID::HOLD_PRESS_TIME);
		evt.dblPressWindow = _context->getSetting(SettingID::DBL_PRESS_WINDOW);
		button.sendEvent(evt);
	}
}


	// return true if it hits the outer deadzone
bool Joystick::processDeadZones(float &x, float &y, float innerDeadzone, float outerDeadzone)
{
	float length = sqrtf(x * x + y * y);
	if (length <= innerDeadzone)
	{
		x = 0.0f;
		y = 0.0f;
		return false;
	}
	if (length >= outerDeadzone)
	{
		// normalize
		x /= length;
		y /= length;
		return true;
	}
	if (length > innerDeadzone)
	{
		float scaledLength = (length - innerDeadzone) / (outerDeadzone - innerDeadzone);
		float rescale = scaledLength / length;
		x *= rescale;
		y *= rescale;
	}
	return false;
}

float Joystick::handleFlickStick(float calX, float calY, float lastCalX, float lastCalY, float stickLength, float mouseCalibrationFactor, bool FLICK_ONLY, bool ROTATE_ONLY)
{
	float camSpeedX = 0.0f;
	// let's centre this
	float offsetX = calX;
	float offsetY = calY;
	float lastOffsetX = lastCalX;
	float lastOffsetY = lastCalY;
	float flickStickThreshold = 0.995f;
	if (is_flicking)
	{
		flickStickThreshold *= 0.9f;
	}
	if (stickLength >= flickStickThreshold)
	{
		float stickAngle = atan2(-offsetX, offsetY);
		//COUT << ", %.4f\n", lastOffsetLength);
		if (!is_flicking)
		{
			// bam! new flick!
			is_flicking = true;
			if (!ROTATE_ONLY)
			{
				auto flick_snap_mode = _context->getSetting<FlickSnapMode>(SettingID::FLICK_SNAP_MODE);
				if (flick_snap_mode != FlickSnapMode::NONE)
				{
					// handle snapping
					float snapInterval = PI;
					if (flick_snap_mode == FlickSnapMode::FOUR)
					{
						snapInterval = PI / 2.0f; // 90 degrees
					}
					else if (flick_snap_mode == FlickSnapMode::EIGHT)
					{
						snapInterval = PI / 4.0f; // 45 degrees
					}
					float snappedAngle = round(stickAngle / snapInterval) * snapInterval;
					// lerp by snap strength
					auto flick_snap_strength = _context->getSetting(SettingID::FLICK_SNAP_STRENGTH);
					stickAngle = stickAngle * (1.0f - flick_snap_strength) + snappedAngle * flick_snap_strength;
				}
				if (abs(stickAngle) * (180.0f / PI) < _context->getSetting(SettingID::FLICK_DEADZONE_ANGLE))
				{
					stickAngle = 0.0f;
				}

				started_flick = chrono::steady_clock::now();
				delta_flick = stickAngle;
				flick_percent_done = 0.0f;
				ResetSmoothSample();
				flick_rotation_counter = stickAngle; // track all rotation for this flick
				// TODO: All these printfs should be hidden behind a setting. User might not want them.
				COUT << "Flick: " << setprecision(3) << stickAngle * (180.0f / (float)PI) << " degrees" << endl;
			}
		}
		else
		{
			if (!FLICK_ONLY)
			{
				// not new? turn camera?
				float lastStickAngle = atan2(-lastOffsetX, lastOffsetY);
				float angleChange = stickAngle - lastStickAngle;
				// https://stackoverflow.com/a/11498248/1130520
				angleChange = fmod(angleChange + PI, 2.0f * PI);
				if (angleChange < 0)
					angleChange += 2.0f * PI;
				angleChange -= PI;
				flick_rotation_counter += angleChange; // track all rotation for this flick
				float flickSpeedConstant = _context->getSetting(SettingID::REAL_WORLD_CALIBRATION) * mouseCalibrationFactor / _context->getSetting(SettingID::IN_GAME_SENS);
				float flickSpeed = -(angleChange * flickSpeedConstant);
				int maxSmoothingSamples = min(NumSamples, (int)ceil(64.0f / tick_time.get())); // target a max smoothing window size of 64ms
				float stepSize = 0.01f;                                                            // and we only want full on smoothing when the stick change each time we poll it is approximately the minimum stick resolution
				                                                                                   // the fact that we're using radians makes this really easy
				auto rotate_smooth_override = _context->getSetting(SettingID::ROTATE_SMOOTH_OVERRIDE);
				if (rotate_smooth_override < 0.0f)
				{
					camSpeedX = GetSmoothedStickRotation(flickSpeed, flickSpeedConstant * stepSize * 2.0f, flickSpeedConstant * stepSize * 4.0f, maxSmoothingSamples);
				}
				else
				{
					camSpeedX = GetSmoothedStickRotation(flickSpeed, flickSpeedConstant * rotate_smooth_override, flickSpeedConstant * rotate_smooth_override * 2.0f, maxSmoothingSamples);
				}
			}
		}
	}
	else if (is_flicking)
	{
		// was a flick! how much was the flick and rotation?
		if (!FLICK_ONLY && !ROTATE_ONLY)
		{
			last_flick_and_rotation = abs(flick_rotation_counter) / (2.0f * PI);
		}
		is_flicking = false;
	}
	// do the flicking
	float secondsSinceFlick = ((float)chrono::duration_cast<chrono::microseconds>(_context->time_now - started_flick).count()) / 1000000.0f;
	float newPercent = secondsSinceFlick / _context->getSetting(SettingID::FLICK_TIME);

	// don't divide by zero
	if (abs(delta_flick) > 0.0f)
	{
		newPercent = newPercent / pow(abs(delta_flick) / PI, _context->getSetting(SettingID::FLICK_TIME_EXPONENT));
	}

	if (newPercent > 1.0f)
		newPercent = 1.0f;
	// warping towards 1.0
	float oldShapedPercent = 1.0f - flick_percent_done;
	oldShapedPercent *= oldShapedPercent;
	oldShapedPercent = 1.0f - oldShapedPercent;
	//float oldShapedPercent = jc->flick_percent_done;
	flick_percent_done = newPercent;
	newPercent = 1.0f - newPercent;
	newPercent *= newPercent;
	newPercent = 1.0f - newPercent;
	float camSpeedChange = (newPercent - oldShapedPercent) * delta_flick * _context->getSetting(SettingID::REAL_WORLD_CALIBRATION) * -mouseCalibrationFactor / _context->getSetting(SettingID::IN_GAME_SENS);
	camSpeedX += camSpeedChange;

	return camSpeedX;
}

void Joystick::ResetSmoothSample()
{
	_frontSample = 0;
	for (int i = 0; i < NumSamples; i++)
	{
		_flickSamples[i] = 0.0;
	}
}

float Joystick::GetSmoothedStickRotation(float value, float bottomThreshold, float topThreshold, int maxSamples)
{
	// which sample in the circular smoothing buffer do we want to write over?
	_frontSample--;
	if (_frontSample < 0)
		_frontSample = NumSamples - 1;
	// if this input is bigger than the top threshold, it'll all be consumed immediately; 0 gets put into the smoothing buffer. If it's below the bottomThreshold, it'll all be put in the smoothing buffer
	float length = abs(value);
	float immediateFactor;
	if (topThreshold <= bottomThreshold)
	{
		immediateFactor = 1.0f;
	}
	else
	{
		immediateFactor = (length - bottomThreshold) / (topThreshold - bottomThreshold);
	}
	// clamp to [0, 1] range
	if (immediateFactor < 0.0f)
	{
		immediateFactor = 0.0f;
	}
	else if (immediateFactor > 1.0f)
	{
		immediateFactor = 1.0f;
	}
	float smoothFactor = 1.0f - immediateFactor;
	// now we can push the smooth sample (or as much of it as we want smoothed)
	float frontSample = _flickSamples[_frontSample] = value * smoothFactor;
	// and now calculate smoothed result
	float result = frontSample / maxSamples;
	for (int i = 1; i < maxSamples; i++)
	{
		int rotatedIndex = (_frontSample + i) % NumSamples;
		frontSample = _flickSamples[rotatedIndex];
		result += frontSample / maxSamples;
	}
	// finally, add immediate portion
	return result + value * immediateFactor;
}


TouchStick::TouchStick(int index, shared_ptr<JoyShockContext> _context)
	: Joystick(ButtonID::TLEFT, ButtonID::TRIGHT, ButtonID::TUP, ButtonID::TDOWN, ButtonID::TRING, SettingID::TOUCH_DEADZONE_INNER, 
		SettingID::TOUCH_DEADZONE_OUTER, SettingID::TOUCH_RING_MODE, SettingID::TOUCH_STICK_MODE, _context)
	, _index(index)
{
	size_t noTouchBtn = std::min(size_t(5), touch_buttons.size());
	buttons.reserve(noTouchBtn);
	for (int i = 0; i < noTouchBtn; ++i)
	{
		buttons.push_back(DigitalButton(_context, touch_buttons[i]));
	}
	touch_scroll_y.init(_stickdirections[UP], _stickdirections[DOWN]);
}

void TouchStick::handleTouchStickChange(shared_ptr<JoyShock> js, bool down, short movX, short movY, float delta_time, int split_type)
{
	float stickX = down ? clamp<float>((lastPos.first + movX) / _context->getSetting(SettingID::TOUCH_STICK_RADIUS), -1.f, 1.f) : 0.f;
	float stickY = down ? clamp<float>((lastPos.second - movY) / _context->getSetting(SettingID::TOUCH_STICK_RADIUS), -1.f, 1.f) : 0.f;
	float innerDeadzone = _context->getSetting(SettingID::TOUCH_DEADZONE_INNER);
	RingMode ringMode = _context->getSetting<RingMode>(SettingID::TOUCH_RING_MODE);
	StickMode stickMode = _context->getSetting<StickMode>(SettingID::TOUCH_STICK_MODE);
	ControllerOrientation controllerOrientation = _context->getSetting<ControllerOrientation>(SettingID::CONTROLLER_ORIENTATION);
	float mouseCalibrationFactor = 180.0f / PI / os_mouse_speed;

	FloatXY sens(_context->getSetting<FloatXY>(SettingID::STICK_SENS));
	sens.first *= _context->getSetting(SettingID::STICK_AXIS_X);
	sens.second *= _context->getSetting(SettingID::STICK_AXIS_Y);
	
	bool lockMouse = false;
	float camSpeedX = 0.f;
	float camSpeedY = 0.f;

	FloatXY stickPos{ stickX, stickY };

	bool anyStickInput = processStick(stickPos, sens, split_type, mouseCalibrationFactor, delta_time, lockMouse, camSpeedX, camSpeedY);

	moveMouse(camSpeedX * _context->getSetting(SettingID::STICK_AXIS_X), -camSpeedY * _context->getSetting(SettingID::STICK_AXIS_Y));

	if (!down && _prevDown)
	{
		lastPos = { 0.f, 0.f };
		is_flicking = false;
		acceleration = 1.0;
		ignore_mode_flag = false;
	}

	_prevDown = down;
}
