#pragma once

#include "JoyShockMapper.h"
#include "Gamepad.h" // Just for the callback :(
#include "DigitalButton.h"
#include "JSMVariable.hpp"
#include <deque>

// Contains all settings that can be modeshifted. They should be accessed only via Joyshock::_context->getSetting
JSMSetting<StickMode> left_stick_mode = JSMSetting<StickMode>(SettingID::LEFT_STICK_MODE, StickMode::NO_MOUSE);
JSMSetting<StickMode> right_stick_mode = JSMSetting<StickMode>(SettingID::RIGHT_STICK_MODE, StickMode::NO_MOUSE);
JSMSetting<StickMode> motion_stick_mode = JSMSetting<StickMode>(SettingID::MOTION_STICK_MODE, StickMode::NO_MOUSE);
JSMSetting<RingMode> left_ring_mode = JSMSetting<RingMode>(SettingID::LEFT_RING_MODE, RingMode::OUTER);
JSMSetting<RingMode> right_ring_mode = JSMSetting<RingMode>(SettingID::LEFT_RING_MODE, RingMode::OUTER);
JSMSetting<RingMode> motion_ring_mode = JSMSetting<RingMode>(SettingID::MOTION_RING_MODE, RingMode::OUTER);
JSMSetting<GyroAxisMask> mouse_x_from_gyro = JSMSetting<GyroAxisMask>(SettingID::MOUSE_X_FROM_GYRO_AXIS, GyroAxisMask::Y);
JSMSetting<GyroAxisMask> mouse_y_from_gyro = JSMSetting<GyroAxisMask>(SettingID::MOUSE_Y_FROM_GYRO_AXIS, GyroAxisMask::X);
JSMSetting<GyroSettings> gyro_settings = JSMSetting<GyroSettings>(SettingID::GYRO_ON, GyroSettings()); // Ignore mode none means no GYRO_OFF button
JSMSetting<JoyconMask> joycon_gyro_mask = JSMSetting<JoyconMask>(SettingID::JOYCON_GYRO_MASK, JoyconMask::IGNORE_LEFT);
JSMSetting<JoyconMask> joycon_motion_mask = JSMSetting<JoyconMask>(SettingID::JOYCON_MOTION_MASK, JoyconMask::IGNORE_RIGHT);
JSMSetting<TriggerMode> zlMode = JSMSetting<TriggerMode>(SettingID::ZL_MODE, TriggerMode::NO_FULL);
JSMSetting<TriggerMode> zrMode = JSMSetting<TriggerMode>(SettingID::ZR_MODE, TriggerMode::NO_FULL);
JSMSetting<FlickSnapMode> flick_snap_mode = JSMSetting<FlickSnapMode>(SettingID::FLICK_SNAP_MODE, FlickSnapMode::NONE);
JSMSetting<FloatXY> min_gyro_sens = JSMSetting<FloatXY>(SettingID::MIN_GYRO_SENS, { 0.0f, 0.0f });
JSMSetting<FloatXY> max_gyro_sens = JSMSetting<FloatXY>(SettingID::MAX_GYRO_SENS, { 0.0f, 0.0f });
JSMSetting<float> min_gyro_threshold = JSMSetting<float>(SettingID::MIN_GYRO_THRESHOLD, 0.0f);
JSMSetting<float> max_gyro_threshold = JSMSetting<float>(SettingID::MAX_GYRO_THRESHOLD, 0.0f);
JSMSetting<float> stick_power = JSMSetting<float>(SettingID::STICK_POWER, 1.0f);
JSMSetting<FloatXY> stick_sens = JSMSetting<FloatXY>(SettingID::STICK_SENS, { 360.0f, 360.0f });
// There's an argument that RWC has no interest in being modeshifted and thus could be outside this structure.
JSMSetting<float> real_world_calibration = JSMSetting<float>(SettingID::REAL_WORLD_CALIBRATION, 40.0f);
JSMSetting<float> in_game_sens = JSMSetting<float>(SettingID::IN_GAME_SENS, 1.0f);
JSMSetting<float> trigger_threshold = JSMSetting<float>(SettingID::TRIGGER_THRESHOLD, 0.0f);
JSMSetting<AxisMode> aim_x_sign = JSMSetting<AxisMode>(SettingID::STICK_AXIS_X, AxisMode::STANDARD);
JSMSetting<AxisMode> aim_y_sign = JSMSetting<AxisMode>(SettingID::STICK_AXIS_Y, AxisMode::STANDARD);
JSMSetting<AxisMode> gyro_y_sign = JSMSetting<AxisMode>(SettingID::GYRO_AXIS_Y, AxisMode::STANDARD);
JSMSetting<AxisMode> gyro_x_sign = JSMSetting<AxisMode>(SettingID::GYRO_AXIS_X, AxisMode::STANDARD);
JSMSetting<float> flick_time = JSMSetting<float>(SettingID::FLICK_TIME, 0.1f);
JSMSetting<float> flick_time_exponent = JSMSetting<float>(SettingID::FLICK_TIME_EXPONENT, 0.0f);
JSMSetting<float> gyro_smooth_time = JSMSetting<float>(SettingID::GYRO_SMOOTH_TIME, 0.125f);
JSMSetting<float> gyro_smooth_threshold = JSMSetting<float>(SettingID::GYRO_SMOOTH_THRESHOLD, 0.0f);
JSMSetting<float> gyro_cutoff_speed = JSMSetting<float>(SettingID::GYRO_CUTOFF_SPEED, 0.0f);
JSMSetting<float> gyro_cutoff_recovery = JSMSetting<float>(SettingID::GYRO_CUTOFF_RECOVERY, 0.0f);
JSMSetting<float> stick_acceleration_rate = JSMSetting<float>(SettingID::STICK_ACCELERATION_RATE, 0.0f);
JSMSetting<float> stick_acceleration_cap = JSMSetting<float>(SettingID::STICK_ACCELERATION_CAP, 1000000.0f);
JSMSetting<float> left_stick_deadzone_inner = JSMSetting<float>(SettingID::LEFT_STICK_DEADZONE_INNER, 0.15f);
JSMSetting<float> left_stick_deadzone_outer = JSMSetting<float>(SettingID::LEFT_STICK_DEADZONE_OUTER, 0.1f);
JSMSetting<float> flick_deadzone_angle = JSMSetting<float>(SettingID::FLICK_DEADZONE_ANGLE, 0.0f);
JSMSetting<float> right_stick_deadzone_inner = JSMSetting<float>(SettingID::RIGHT_STICK_DEADZONE_INNER, 0.15f);
JSMSetting<float> right_stick_deadzone_outer = JSMSetting<float>(SettingID::RIGHT_STICK_DEADZONE_OUTER, 0.1f);
JSMSetting<float> motion_deadzone_inner = JSMSetting<float>(SettingID::MOTION_DEADZONE_INNER, 15.f);
JSMSetting<float> motion_deadzone_outer = JSMSetting<float>(SettingID::MOTION_DEADZONE_OUTER, 135.f);
JSMSetting<float> lean_threshold = JSMSetting<float>(SettingID::LEAN_THRESHOLD, 15.f);
JSMSetting<ControllerOrientation> controller_orientation = JSMSetting<ControllerOrientation>(SettingID::CONTROLLER_ORIENTATION, ControllerOrientation::FORWARD);
JSMSetting<float> trackball_decay = JSMSetting<float>(SettingID::TRACKBALL_DECAY, 1.0f);
JSMSetting<float> mouse_ring_radius = JSMSetting<float>(SettingID::MOUSE_RING_RADIUS, 128.0f);
JSMSetting<float> screen_resolution_x = JSMSetting<float>(SettingID::SCREEN_RESOLUTION_X, 1920.0f);
JSMSetting<float> screen_resolution_y = JSMSetting<float>(SettingID::SCREEN_RESOLUTION_Y, 1080.0f);
JSMSetting<float> rotate_smooth_override = JSMSetting<float>(SettingID::ROTATE_SMOOTH_OVERRIDE, -1.0f);
JSMSetting<float> flick_snap_strength = JSMSetting<float>(SettingID::FLICK_SNAP_STRENGTH, 01.0f);
JSMSetting<float> trigger_skip_delay = JSMSetting<float>(SettingID::TRIGGER_SKIP_DELAY, 150.0f);
JSMSetting<float> turbo_period = JSMSetting<float>(SettingID::TURBO_PERIOD, 80.0f);
JSMSetting<float> hold_press_time = JSMSetting<float>(SettingID::HOLD_PRESS_TIME, 150.0f);
JSMVariable<float> sim_press_window = JSMVariable<float>(50.0f);
JSMSetting<float> dbl_press_window = JSMSetting<float>(SettingID::DBL_PRESS_WINDOW, 150.0f);
JSMVariable<float> tick_time = JSMSetting<float>(SettingID::TICK_TIME, 3);
JSMSetting<Color> light_bar = JSMSetting<Color>(SettingID::LIGHT_BAR, 0xFFFFFF);
JSMSetting<FloatXY> scroll_sens = JSMSetting<FloatXY>(SettingID::SCROLL_SENS, { 30.f, 30.f });
JSMVariable<Switch> autoloadSwitch = JSMVariable<Switch>(Switch::ON);
JSMVariable<FloatXY> grid_size = JSMVariable(FloatXY{ 2.f, 1.f }); // Default left side and right side button
JSMSetting<TouchpadMode> touchpad_mode = JSMSetting<TouchpadMode>(SettingID::TOUCHPAD_MODE, TouchpadMode::GRID_AND_STICK);
JSMSetting<StickMode> touch_stick_mode = JSMSetting<StickMode>(SettingID::TOUCH_STICK_MODE, StickMode::NO_MOUSE);
JSMSetting<float> touch_stick_radius = JSMSetting<float>(SettingID::TOUCH_STICK_RADIUS, 300.f);
JSMSetting<float> touch_deadzone_inner = JSMSetting<float>(SettingID::TOUCH_DEADZONE_INNER, 0.3f);
JSMSetting<RingMode> touch_ring_mode = JSMSetting<RingMode>(SettingID::TOUCH_RING_MODE, RingMode::OUTER);
JSMSetting<FloatXY> touchpad_sens = JSMSetting<FloatXY>(SettingID::TOUCHPAD_SENS, { 1.f, 1.f });
JSMVariable<Switch> hide_minimized = JSMVariable<Switch>(Switch::OFF);
JSMVariable<ControllerScheme> virtual_controller = JSMVariable<ControllerScheme>(ControllerScheme::NONE);
JSMSetting<TriggerMode> touch_ds_mode = JSMSetting<TriggerMode>(SettingID::TOUCHPAD_DUAL_STAGE_MODE, TriggerMode::NO_SKIP);
JSMSetting<Switch> rumble_enable = JSMSetting<Switch>(SettingID::RUMBLE, Switch::ON);
JSMSetting<Switch> adaptive_trigger = JSMSetting<Switch>(SettingID::ADAPTIVE_TRIGGER, Switch::ON);
JSMVariable<int> left_trigger_offset = JSMVariable<int>(25);
JSMVariable<int> left_trigger_range = JSMVariable<int>(150);
JSMVariable<int> right_trigger_offset = JSMVariable<int>(25);
JSMVariable<int> right_trigger_range = JSMVariable<int>(25);
JSMVariable<PathString> currentWorkingDir = JSMVariable<PathString>(PathString());


class GamepadMotion;

// All digital buttons need a reference to the same instance of the common structure within the same controller.
// It enables the buttons to synchronize and be aware of the state of the whole controller, access gyro etc...
struct JoyShockContext
{
	JoyShockContext(Gamepad::Callback virtualControllerCallback, GamepadMotion *mainMotion);
	deque<pair<ButtonID, KeyCode>> gyroActionQueue; // Queue of gyro control actions currently in effect
	deque<pair<ButtonID, KeyCode>> activeTogglesQueue;
	deque<ButtonID> chordStack; // Represents the current active buttons in order from most recent to latest
	unique_ptr<Gamepad> _vigemController;
	function<DigitalButton *(ButtonID)> _getMatchingSimBtn; // A functor to JoyShock::GetMatchingSimBtn
	function<void(int small, int big)> _rumble;             // A functor to JoyShock::Rumble
	mutex callback_lock;                                    // Needs to be in the common struct for both joycons to use the same
	GamepadMotion *rightMainMotion = nullptr;
	GamepadMotion *leftMotion = nullptr;
	chrono::steady_clock::time_point time_now;

	template<typename E>
	E getSetting(SettingID index);

	float getSetting(SettingID index);
	
	template<>
	FloatXY getSetting<FloatXY>(SettingID index);

	template<>
	GyroSettings getSetting<GyroSettings>(SettingID index);

	template<>
	Color getSetting<Color>(SettingID index);

private:
	template<typename E1, typename E2>
	static inline optional<E1> GetOptionalSetting(const JSMSetting<E2> &setting, ButtonID chord)
	{
		return setting.get(chord) ? optional<E1>(static_cast<E1>(*setting.get(chord))) : nullopt;
	}
};

template<typename E>
E JoyShockContext::getSetting(SettingID index)
{
	static_assert(is_enum<E>::value, "Parameter of JoyShock::getSetting<E> has to be an enum type");
	// Look at active chord mappings starting with the latest activates chord
	for (auto activeChord = chordStack.begin(); activeChord != chordStack.end(); activeChord++)
	{
		optional<E> opt;
		switch (index)
		{
		case SettingID::MOUSE_X_FROM_GYRO_AXIS:
			opt = GetOptionalSetting<E>(mouse_x_from_gyro, *activeChord);
			break;
		case SettingID::MOUSE_Y_FROM_GYRO_AXIS:
			opt = GetOptionalSetting<E>(mouse_y_from_gyro, *activeChord);
			break;
		case SettingID::LEFT_STICK_MODE:
			opt = GetOptionalSetting<E>(left_stick_mode, *activeChord);
			if (ignore_left_stick_mode && *activeChord == ButtonID::NONE)
				opt = optional<E>(static_cast<E>(StickMode::INVALID));
			else
				ignore_left_stick_mode |= (opt && *activeChord != ButtonID::NONE);
			break;
		case SettingID::RIGHT_STICK_MODE:
			opt = GetOptionalSetting<E>(right_stick_mode, *activeChord);
			break;
		case SettingID::MOTION_STICK_MODE:
			opt = GetOptionalSetting<E>(motion_stick_mode, *activeChord);
			if (ignore_motion_stick_mode && *activeChord == ButtonID::NONE)
				opt = optional<E>(static_cast<E>(StickMode::INVALID));
			else
				ignore_motion_stick_mode |= (opt && *activeChord != ButtonID::NONE);
			break;
		case SettingID::LEFT_RING_MODE:
			opt = GetOptionalSetting<E>(left_ring_mode, *activeChord);
			break;
		case SettingID::RIGHT_RING_MODE:
			opt = GetOptionalSetting<E>(right_ring_mode, *activeChord);
			break;
		case SettingID::MOTION_RING_MODE:
			opt = GetOptionalSetting<E>(motion_ring_mode, *activeChord);
			break;
		case SettingID::JOYCON_GYRO_MASK:
			opt = GetOptionalSetting<E>(joycon_gyro_mask, *activeChord);
			break;
		case SettingID::JOYCON_MOTION_MASK:
			opt = GetOptionalSetting<E>(joycon_motion_mask, *activeChord);
			break;
		case SettingID::CONTROLLER_ORIENTATION:
			opt = GetOptionalSetting<E>(controller_orientation, *activeChord);
			break;
		case SettingID::ZR_MODE:
			opt = GetOptionalSetting<E>(zrMode, *activeChord);
			break;
		case SettingID::ZL_MODE:
			opt = GetOptionalSetting<E>(zlMode, *activeChord);
			break;
		case SettingID::FLICK_SNAP_MODE:
			opt = GetOptionalSetting<E>(flick_snap_mode, *activeChord);
			break;
		case SettingID::TOUCHPAD_MODE:
			opt = GetOptionalSetting<E>(touchpad_mode, *activeChord);
			break;
		case SettingID::TOUCH_STICK_MODE:
			opt = GetOptionalSetting<E>(touch_stick_mode, *activeChord);
			break;
		case SettingID::TOUCH_RING_MODE:
			opt = GetOptionalSetting<E>(touch_stick_mode, *activeChord);
			break;
		case SettingID::TOUCHPAD_DUAL_STAGE_MODE:
			opt = GetOptionalSetting<E>(touch_ds_mode, *activeChord);
			break;
		case SettingID::RUMBLE:
			opt = GetOptionalSetting<E>(rumble_enable, *activeChord);
			break;
		case SettingID::ADAPTIVE_TRIGGER:
			opt = GetOptionalSetting<E>(adaptive_trigger, *activeChord);
			break;
		}
		if (opt)
			return *opt;
	}
	stringstream ss;
	ss << "Index " << index << " is not a valid enum setting";
	throw invalid_argument(ss.str().c_str());
}

float JoyShockContext::getSetting(SettingID index)
{
	// Look at active chord mappings starting with the latest activates chord
	for (auto activeChord = chordStack.begin(); activeChord != chordStack.end(); activeChord++)
	{
		optional<float> opt;
		switch (index)
		{
		case SettingID::MIN_GYRO_THRESHOLD:
			opt = min_gyro_threshold.get(*activeChord);
			break;
		case SettingID::MAX_GYRO_THRESHOLD:
			opt = max_gyro_threshold.get(*activeChord);
			break;
		case SettingID::STICK_POWER:
			opt = stick_power.get(*activeChord);
			break;
		case SettingID::REAL_WORLD_CALIBRATION:
			opt = real_world_calibration.get(*activeChord);
			break;
		case SettingID::IN_GAME_SENS:
			opt = in_game_sens.get(*activeChord);
			break;
		case SettingID::TRIGGER_THRESHOLD:
			opt = trigger_threshold.get(*activeChord);
			break;
		case SettingID::STICK_AXIS_X:
			opt = GetOptionalSetting<float>(aim_x_sign, *activeChord);
			break;
		case SettingID::STICK_AXIS_Y:
			opt = GetOptionalSetting<float>(aim_y_sign, *activeChord);
			break;
		case SettingID::GYRO_AXIS_X:
			opt = GetOptionalSetting<float>(gyro_x_sign, *activeChord);
			break;
		case SettingID::GYRO_AXIS_Y:
			opt = GetOptionalSetting<float>(gyro_y_sign, *activeChord);
			break;
		case SettingID::FLICK_TIME:
			opt = flick_time.get(*activeChord);
			break;
		case SettingID::FLICK_TIME_EXPONENT:
			opt = flick_time_exponent.get(*activeChord);
			break;
		case SettingID::GYRO_SMOOTH_THRESHOLD:
			opt = gyro_smooth_threshold.get(*activeChord);
			break;
		case SettingID::GYRO_SMOOTH_TIME:
			opt = gyro_smooth_time.get(*activeChord);
			break;
		case SettingID::GYRO_CUTOFF_SPEED:
			opt = gyro_cutoff_speed.get(*activeChord);
			break;
		case SettingID::GYRO_CUTOFF_RECOVERY:
			opt = gyro_cutoff_recovery.get(*activeChord);
			break;
		case SettingID::STICK_ACCELERATION_RATE:
			opt = stick_acceleration_rate.get(*activeChord);
			break;
		case SettingID::STICK_ACCELERATION_CAP:
			opt = stick_acceleration_cap.get(*activeChord);
			break;
		case SettingID::LEFT_STICK_DEADZONE_INNER:
			opt = left_stick_deadzone_inner.get(*activeChord);
			break;
		case SettingID::LEFT_STICK_DEADZONE_OUTER:
			opt = left_stick_deadzone_outer.get(*activeChord);
			break;
		case SettingID::RIGHT_STICK_DEADZONE_INNER:
			opt = right_stick_deadzone_inner.get(*activeChord);
			break;
		case SettingID::RIGHT_STICK_DEADZONE_OUTER:
			opt = right_stick_deadzone_outer.get(*activeChord);
			break;
		case SettingID::MOTION_DEADZONE_INNER:
			opt = motion_deadzone_inner.get(*activeChord);
			break;
		case SettingID::MOTION_DEADZONE_OUTER:
			opt = motion_deadzone_outer.get(*activeChord);
			break;
		case SettingID::LEAN_THRESHOLD:
			opt = lean_threshold.get(*activeChord);
			break;
		case SettingID::FLICK_DEADZONE_ANGLE:
			opt = flick_deadzone_angle.get(*activeChord);
			break;
		case SettingID::TRACKBALL_DECAY:
			opt = trackball_decay.get(*activeChord);
			break;
		case SettingID::MOUSE_RING_RADIUS:
			opt = mouse_ring_radius.get(*activeChord);
			break;
		case SettingID::SCREEN_RESOLUTION_X:
			opt = screen_resolution_x.get(*activeChord);
			break;
		case SettingID::SCREEN_RESOLUTION_Y:
			opt = screen_resolution_y.get(*activeChord);
			break;
		case SettingID::ROTATE_SMOOTH_OVERRIDE:
			opt = rotate_smooth_override.get(*activeChord);
			break;
		case SettingID::FLICK_SNAP_STRENGTH:
			opt = flick_snap_strength.get(*activeChord);
			break;
		case SettingID::TRIGGER_SKIP_DELAY:
			opt = trigger_skip_delay.get(*activeChord);
			break;
		case SettingID::TURBO_PERIOD:
			opt = turbo_period.get(*activeChord);
			break;
		case SettingID::HOLD_PRESS_TIME:
			opt = hold_press_time.get(*activeChord);
			break;
		case SettingID::TOUCH_STICK_RADIUS:
			opt = touch_stick_radius.get(*activeChord);
			break;
		case SettingID::TOUCH_DEADZONE_INNER:
			opt = touch_deadzone_inner.get(*activeChord);
			break;
		case SettingID::TOUCH_DEADZONE_OUTER:
			opt = 0.f;
			break;
		case SettingID::DBL_PRESS_WINDOW:
			opt = dbl_press_window.get(*activeChord);
			break;
			// SIM_PRESS_WINDOW are not chorded, they can be accessed as is.
		}
		if (opt)
			return *opt;
	}

	std::stringstream message;
	message << "Index " << index << " is not a valid float setting";
	throw std::out_of_range(message.str().c_str());
}

template<>
FloatXY JoyShockContext::getSetting<FloatXY>(SettingID index)
{
	// Look at active chord mappings starting with the latest activates chord
	for (auto activeChord = chordStack.begin(); activeChord != chordStack.end(); activeChord++)
	{
		optional<FloatXY> opt;
		switch (index)
		{
		case SettingID::MIN_GYRO_SENS:
			opt = min_gyro_sens.get(*activeChord);
			break;
		case SettingID::MAX_GYRO_SENS:
			opt = max_gyro_sens.get(*activeChord);
			break;
		case SettingID::STICK_SENS:
			opt = stick_sens.get(*activeChord);
			break;
		case SettingID::TOUCHPAD_SENS:
			opt = touchpad_sens.get(*activeChord);
			break;
		case SettingID::SCROLL_SENS:
			opt = scroll_sens.get(*activeChord);
			break;
		}
		if (opt)
			return *opt;
	} // Check next Chord

	stringstream ss;
	ss << "Index " << index << " is not a valid FloatXY setting";
	throw invalid_argument(ss.str().c_str());
}

template<>
GyroSettings JoyShockContext::getSetting<GyroSettings>(SettingID index)
{
	if (index == SettingID::GYRO_ON || index == SettingID::GYRO_OFF)
	{
		// Look at active chord mappings starting with the latest activates chord
		for (auto activeChord = chordStack.begin(); activeChord != chordStack.end(); activeChord++)
		{
			auto opt = gyro_settings.get(*activeChord);
			if (opt)
				return *opt;
		}
	}
	stringstream ss;
	ss << "Index " << index << " is not a valid GyroSetting";
	throw invalid_argument(ss.str().c_str());
}

template<>
Color JoyShockContext::getSetting<Color>(SettingID index)
{
	if (index == SettingID::LIGHT_BAR)
	{
		// Look at active chord mappings starting with the latest activates chord
		for (auto activeChord = chordStack.begin(); activeChord != chordStack.end(); activeChord++)
		{
			auto opt = light_bar.get(*activeChord);
			if (opt)
				return *opt;
		}
	}
	stringstream ss;
	ss << "Index " << index << " is not a valid Color";
	throw invalid_argument(ss.str().c_str());
}
