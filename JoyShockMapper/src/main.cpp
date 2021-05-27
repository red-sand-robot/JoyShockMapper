#include "JoyShockMapper.h"
#include "JSMVersion.h"
#include "JslWrapper.h"
#include "GamepadMotion.h"
#include "InputHelpers.h"
#include "Whitelister.h"
#include "TrayIcon.h"
#include "JSMAssignment.hpp"
#include "quatMaths.cpp"
#include "Joystick.h"
#include "GamepadMotion.h"

#include <mutex>
#include <iomanip>
#include <filesystem>
#include <shellapi.h>

#pragma warning(disable : 4996) // Disable deprecated API warnings

const KeyCode KeyCode::EMPTY = KeyCode();
const Mapping Mapping::NO_MAPPING = Mapping("NONE");
function<bool(in_string)> Mapping::_isCommandValid = function<bool(in_string)>();
unique_ptr<JslWrapper> jsl;
unique_ptr<TrayIcon> tray;
unique_ptr<Whitelister> whitelister;

class JoyShock;
void joyShockPollCallback(int jcHandle, JOY_SHOCK_STATE state, JOY_SHOCK_STATE lastState, IMU_STATE imuState, IMU_STATE lastImuState, float deltaTime);
void TouchCallback(int jcHandle, TOUCH_STATE newState, TOUCH_STATE prevState, float delta_time);

vector<JSMButton> touch_buttons; // array of virtual buttons on the touchpad grid
vector<JSMButton> mappings;      // array enables use of for each loop and other i/f
mutex loading_lock;

float os_mouse_speed = 1.0;
float last_flick_and_rotation = 0.0;
unique_ptr<PollingThread> autoLoadThread;
unique_ptr<PollingThread> minimizeThread;
bool devicesCalibrating = false;
unordered_map<int, shared_ptr<JoyShock>> handle_to_joyshock;
int triggersCalibrating = 0;


struct TOUCH_POINT
{
	float posX = -1.f;
	float posY = -1.f;
	short movX = 0;
	short movY = 0;
	inline bool isDown()
	{
		return posX >= 0.f && posX <= 1.f && posY >= 0.f && posY <= 1.f;
	}
};

KeyCode::KeyCode()
	: code()
	, name()
{
}

KeyCode::KeyCode(in_string keyName)
	: code(nameToKey(keyName))
	, name()
{
	if (code == COMMAND_ACTION)
		name = keyName.substr(1, keyName.size() - 2); // Remove opening and closing quotation marks
	else if (keyName.compare("SMALL_RUMBLE") == 0)
	{
		name = SMALL_RUMBLE;
		code = RUMBLE;
	}
	else if (keyName.compare("BIG_RUMBLE") == 0)
	{
		name = BIG_RUMBLE;
		code = RUMBLE;
	}
	else if (code != 0)
		name = keyName;
}

JoyShockContext::JoyShockContext(Gamepad::Callback virtualControllerCallback, GamepadMotion *mainMotion)
 : rightMainMotion(mainMotion)
{
	chordStack.push_front(ButtonID::NONE); //Always hold mapping none at the end to handle modeshifts and chords
	if (virtual_controller.get() != ControllerScheme::NONE)
	{
		_vigemController.reset(Gamepad::getNew(virtual_controller.get(), virtualControllerCallback));
	}
}

// An instance of this class represents a single controller device that JSM is listening to.
class JoyShock
{
private:
	float _weightsRemaining[64];

	FloatXY _gyroSamples[64];
	int _frontGyroSample = 0;

public:
	const int MaxGyroSamples = 64;
	int handle;
	GamepadMotion motion;
	int platform_controller_type;

	vector<DigitalButton> buttons; // does not contain stick directions and ring
	vector<DigitalButton> gridButtons;
	vector<TouchStick> touchpads;
	Joystick leftStick;
	Joystick rightStick;
	Joystick motionStick;

	TOUCH_STATE prevTouchState;

	int controller_split_type = 0;

	vector<DstState> triggerState; // State of analog triggers when skip mode is active
	vector<deque<float>> prevTriggerPosition;
	shared_ptr<JoyShockContext> _context;

	float neutralQuatW = 1.0f;
	float neutralQuatX = 0.0f;
	float neutralQuatY = 0.0f;
	float neutralQuatZ = 0.0f;

	bool set_neutral_quat = false;

	int numLastGyroSamples = 100;
	float lastGyroX[100] = { 0.f };
	float lastGyroY[100] = { 0.f };
	float lastGyroAbsX = 0.f;
	float lastGyroAbsY = 0.f;
	int lastGyroIndexX = 0;
	int lastGyroIndexY = 0;

	Color _light_bar;
	JOY_SHOCK_TRIGGER_EFFECT left_effect;
	JOY_SHOCK_TRIGGER_EFFECT right_effect;
	JOY_SHOCK_TRIGGER_EFFECT unused_effect;

	JoyShock(int uniqueHandle, int controllerSplitType, shared_ptr<JoyShockContext> sharedContext = nullptr)
	  : handle(uniqueHandle)
	  , controller_split_type(controllerSplitType)
	  , platform_controller_type(jsl->GetControllerType(uniqueHandle))
	  , triggerState(NUM_ANALOG_TRIGGERS, DstState::NoPress)
	  , prevTriggerPosition(NUM_ANALOG_TRIGGERS, deque<float>(MAGIC_TRIGGER_SMOOTHING, 0.f))
	  , _light_bar(*light_bar.get())
	  , _context(sharedContext ? sharedContext : make_shared<JoyShockContext>(
			  bind(&JoyShock::handleViGEmNotification, this, placeholders::_1, placeholders::_2, placeholders::_3), &motion))
	  , leftStick(ButtonID::LLEFT, ButtonID::LRIGHT, ButtonID::LUP, ButtonID::LDOWN, ButtonID::LRING, SettingID::LEFT_STICK_DEADZONE_INNER, SettingID::LEFT_STICK_DEADZONE_OUTER, SettingID::LEFT_RING_MODE, SettingID::LEFT_STICK_MODE, _context)
	  , rightStick(ButtonID::RLEFT, ButtonID::RRIGHT, ButtonID::RUP, ButtonID::RDOWN, ButtonID::RRING, SettingID::RIGHT_STICK_DEADZONE_INNER, SettingID::RIGHT_STICK_DEADZONE_OUTER, SettingID::RIGHT_RING_MODE, SettingID::RIGHT_STICK_MODE, _context)
	  , motionStick(ButtonID::MLEFT, ButtonID::MRIGHT, ButtonID::MUP, ButtonID::MDOWN, ButtonID::MRING, SettingID::MOTION_DEADZONE_INNER, SettingID::MOTION_DEADZONE_OUTER, SettingID::MOTION_RING_MODE, SettingID::MOTION_STICK_MODE, _context)
	{
		_light_bar = _context->getSetting<Color>(SettingID::LIGHT_BAR);

		platform_controller_type = jsl->GetControllerType(handle);
		_context->_getMatchingSimBtn = bind(&JoyShock::GetMatchingSimBtn, this, placeholders::_1);
		_context->_rumble = bind(&JoyShock::Rumble, this, placeholders::_1, placeholders::_2);

		buttons.reserve(mappings.size());
		for (int i = 0; i < LAST_ANALOG_TRIGGER; ++i)
		{
			buttons.push_back(DigitalButton(_context, mappings[i]));
		}
		if (!CheckVigemState())
		{
			virtual_controller = ControllerScheme::NONE;
		}
		jsl->SetLightColour(handle, _context->getSetting<Color>(SettingID::LIGHT_BAR).raw);
		for (int i = 0; i < MAX_NO_OF_TOUCH; ++i)
		{
			touchpads.push_back(TouchStick(i, _context));
		}
		updateGridSize(grid_size.get().x() * grid_size.get().y() + 5);
		prevTouchState.t0Down = false;
		prevTouchState.t1Down = false;
	}

	~JoyShock()
	{
		if (controller_split_type == JS_SPLIT_TYPE_LEFT)
		{
			_context->leftMotion = nullptr;
		}
		else
		{
			_context->rightMainMotion = nullptr;
		}
	}

	void Rumble(int smallRumble, int bigRumble)
	{
		if (_context->getSetting<Switch>(SettingID::RUMBLE) == Switch::ON)
		{
			// DEBUG_LOG << "Rumbling at " << smallRumble << " and " << bigRumble << endl;
			jsl->SetRumble(handle, smallRumble, bigRumble);
		}
	}

	bool CheckVigemState()
	{
		if (virtual_controller.get() != ControllerScheme::NONE)
		{
			string error = "There is no controller object";
			if (!_context->_vigemController || _context->_vigemController->isInitialized(&error) == false)
			{
				CERR << "[ViGEm Client] " << error << endl;
				return false;
			}
			else if (_context->_vigemController->getType() != virtual_controller.get())
			{
				CERR << "[ViGEm Client] The controller is of the wrong type!" << endl;
				return false;
			}
		}
		return true;
	}

	void handleViGEmNotification(UCHAR largeMotor, UCHAR smallMotor, Indicator indicator)
	{
		//static chrono::steady_clock::time_point last_call;
		//auto now = chrono::steady_clock::now();
		//auto diff = ((float)chrono::duration_cast<chrono::microseconds>(now - last_call).count()) / 1000000.0f;
		//last_call = now;
		//COUT_INFO << "Time since last vigem rumble is " << diff << " us" << endl;
		lock_guard guard(this->_context->callback_lock);
		switch (platform_controller_type)
		{
		case JS_TYPE_DS4:
		case JS_TYPE_DS:
			jsl->SetLightColour(handle, _light_bar.raw);
			break;
		default:
			jsl->SetPlayerNumber(handle, indicator.led);
			break;
		}
		Rumble(smallMotor, largeMotor);
	}

public:
	DigitalButton *GetMatchingSimBtn(ButtonID index)
	{
		// Find the simMapping where the other btn is in the same state as this btn.
		// POTENTIAL FLAW: The mapping you find may not necessarily be the one that got you in a
		// Simultaneous state in the first place if there is a second SimPress going on where one
		// of the buttons has a third SimMap with this one. I don't know if it's worth solving though...
		for (int id = 0; id < mappings.size(); ++id)
		{
			auto simMap = mappings[int(index)].getSimMap(ButtonID(id));
			if (simMap && index != simMap->first && buttons[int(simMap->first)].getState() == buttons[int(index)].getState())
			{
				return &buttons[int(simMap->first)];
			}
		}
		return nullptr;
	}

	void GetSmoothedGyro(float x, float y, float length, float bottomThreshold, float topThreshold, int maxSamples, float &outX, float &outY)
	{
		// this is basically the same as we use for smoothing flick-stick rotations, but because this deals in vectors, it's a slightly different function. Not worth abstracting until it'll be used in more ways
		// which item in the circular smoothing buffer will we write over?
		_frontGyroSample--;
		if (_frontGyroSample < 0)
			_frontGyroSample = MaxGyroSamples - 1;
		float immediateFactor;
		if (topThreshold <= bottomThreshold)
		{
			immediateFactor = length < bottomThreshold ? 0.0f : 1.0f;
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
		FloatXY frontSample = _gyroSamples[_frontGyroSample] = { x * smoothFactor, y * smoothFactor };
		// and now calculate smoothed result
		float xResult = frontSample.x() / maxSamples;
		float yResult = frontSample.y() / maxSamples;
		for (int i = 1; i < maxSamples; i++)
		{
			int rotatedIndex = (_frontGyroSample + i) % MaxGyroSamples;
			frontSample = _gyroSamples[rotatedIndex];
			xResult += frontSample.x() / maxSamples;
			yResult += frontSample.y() / maxSamples;
		}
		// finally, add immediate portion
		outX = xResult + x * immediateFactor;
		outY = yResult + y * immediateFactor;
	}

private:
	DigitalButton *GetButton(ButtonID index, int touchpadIndex = -1)
	{
		DigitalButton *button = index < ButtonID::SIZE           ? &buttons[int(index)] :
		  touchpadIndex >= 0 && touchpadIndex < touchpads.size() ? &touchpads[touchpadIndex].buttons[int(index) - FIRST_TOUCH_BUTTON] :
		  index >= ButtonID::T1                                  ? &gridButtons[int(index) - int(ButtonID::T1)] :
                                                                   throw exception("What index is this?");
		return button;
	}

	bool isSoftPullPressed(int triggerIndex, float triggerPosition)
	{
		float threshold = _context->getSetting(SettingID::TRIGGER_THRESHOLD);
		if (platform_controller_type == JS_TYPE_DS && _context->getSetting<Switch>(SettingID::ADAPTIVE_TRIGGER) == Switch::ON)
			threshold = max(0.f, threshold); // hair trigger disabled on dual sense when adaptive triggers are active
		if (threshold >= 0)
		{
			return triggerPosition > threshold;
		}
		// else HAIR TRIGGER

		// Calculate 3 sample averages with the last MAGIC_TRIGGER_SMOOTHING samples + new sample
		float sum = 0.f;
		for_each(prevTriggerPosition[triggerIndex].begin(), prevTriggerPosition[triggerIndex].begin() + 3, [&sum](auto data) { sum += data; });
		float avg_tm3 = sum / 3.0f;
		sum = sum - *(prevTriggerPosition[triggerIndex].begin()) + *(prevTriggerPosition[triggerIndex].end() - 2);
		float avg_tm2 = sum / 3.0f;
		sum = sum - *(prevTriggerPosition[triggerIndex].begin() + 1) + *(prevTriggerPosition[triggerIndex].end() - 1);
		float avg_tm1 = sum / 3.0f;
		sum = sum - *(prevTriggerPosition[triggerIndex].begin() + 2) + triggerPosition;
		float avg_t0 = sum / 3.0f;
		//if (avg_t0 > 0) COUT << "Trigger: " << avg_t0 << endl;

		// Soft press is pressed if we got three averaged samples in a row that are pressed
		bool isPressed;
		if (avg_t0 > avg_tm1 && avg_tm1 > avg_tm2 && avg_tm2 > avg_tm3)
		{
			//DEBUG_LOG << "Hair Trigger pressed: " << avg_t0 << " > " << avg_tm1 << " > " << avg_tm2 << " > " << avg_tm3 << endl;
			isPressed = true;
		}
		else if (avg_t0 < avg_tm1 && avg_tm1 < avg_tm2 && avg_tm2 < avg_tm3)
		{
			//DEBUG_LOG << "Hair Trigger released: " << avg_t0 << " < " << avg_tm1 << " < " << avg_tm2 << " < " << avg_tm3 << endl;
			isPressed = false;
		}
		else
		{
			isPressed = triggerState[triggerIndex] != DstState::NoPress && triggerState[triggerIndex] != DstState::QuickSoftTap;
		}
		prevTriggerPosition[triggerIndex].pop_front();
		prevTriggerPosition[triggerIndex].push_back(triggerPosition);
		return isPressed;
	}


public:
	void handleButtonChange(ButtonID id, bool pressed, int touchpadID = -1)
	{
		if (pressed)
		{
			Pressed evt;
			evt.time_now = _context->time_now;
			evt.turboTime = _context->getSetting(SettingID::TURBO_PERIOD);
			evt.holdTime = _context->getSetting(SettingID::HOLD_PRESS_TIME);
			evt.dblPressWindow = _context->getSetting(SettingID::DBL_PRESS_WINDOW);
			GetButton(id, touchpadID)->sendEvent(evt);
		}
		else
		{
			Released evt;
			evt.time_now = _context->time_now;
			evt.turboTime = _context->getSetting(SettingID::TURBO_PERIOD);
			evt.holdTime = _context->getSetting(SettingID::HOLD_PRESS_TIME);
			evt.dblPressWindow = _context->getSetting(SettingID::DBL_PRESS_WINDOW);
			GetButton(id, touchpadID)->sendEvent(evt);
		}
	}

	float getTriggerEffectStartPos()
	{
		float threshold = _context->getSetting(SettingID::TRIGGER_THRESHOLD);
		if (platform_controller_type == JS_TYPE_DS && _context->getSetting<Switch>(SettingID::ADAPTIVE_TRIGGER) == Switch::ON)
				threshold = max(0.f, threshold); // hair trigger disabled on dual sense when adaptive triggers are active
		return clamp(threshold + 0.05f, 0.0f, 1.0f);
	}

	void handleTriggerChange(ButtonID softIndex, ButtonID fullIndex, TriggerMode mode, float position, JOY_SHOCK_TRIGGER_EFFECT &trigger_rumble)
	{
		uint8_t offset = softIndex == ButtonID::ZL ? left_trigger_offset : right_trigger_offset;
		uint8_t range = softIndex == ButtonID::ZL ? left_trigger_range : right_trigger_range;
		auto idxState = int(fullIndex) - FIRST_ANALOG_TRIGGER; // Get analog trigger index
		if (idxState < 0 || idxState >= (int)triggerState.size())
		{
			COUT << "Error: Trigger " << fullIndex << " does not exist in state map. Dual Stage Trigger not possible." << endl;
			return;
		}

		if (mode != TriggerMode::X_LT && mode != TriggerMode::X_RT && (platform_controller_type == JS_TYPE_PRO_CONTROLLER || platform_controller_type == JS_TYPE_JOYCON_LEFT || platform_controller_type == JS_TYPE_JOYCON_RIGHT))
		{
			// Override local variable because the controller has digital triggers. Effectively ignore Full Pull binding.
			mode = TriggerMode::NO_FULL;
		}

		if (mode == TriggerMode::X_LT)
		{
			if (_context->_vigemController)
				_context->_vigemController->setLeftTrigger(position);
			trigger_rumble.mode = 1;
			trigger_rumble.strength = 0;
			trigger_rumble.start = offset + 0.05 * range;
			return;
		}
		else if (mode == TriggerMode::X_RT)
		{
			if (_context->_vigemController)
				_context->_vigemController->setRightTrigger(position);
			trigger_rumble.mode = 1;
			trigger_rumble.strength = 0;
			trigger_rumble.start = offset + 0.05 * range;
			return;
		}

		// if either trigger is waiting to be tap released, give it a go
		if (buttons[int(softIndex)].getState() == BtnState::TapPress)
		{
			// keep triggering until the tap release is complete
			handleButtonChange(softIndex, false);
		}
		if (buttons[int(fullIndex)].getState() == BtnState::TapPress)
		{
			// keep triggering until the tap release is complete
			handleButtonChange(fullIndex, false);
		}

		switch (triggerState[idxState])
		{
		case DstState::NoPress:
			// It actually doesn't matter what the last Press is. Theoretically, we could have missed the edge.
			if (mode == TriggerMode::NO_FULL)
			{
				trigger_rumble.mode = 1;
				trigger_rumble.strength = UINT16_MAX;
				trigger_rumble.start = offset + getTriggerEffectStartPos() * range;
			}
			else
			{
				trigger_rumble.mode = 2;
				trigger_rumble.strength = 0.1 * UINT16_MAX;
				trigger_rumble.start = offset + getTriggerEffectStartPos() * range;
				trigger_rumble.end = offset + min(1.f, getTriggerEffectStartPos() + 0.1f)  * range;
			}
			if (isSoftPullPressed(idxState, position))
			{
				if (mode == TriggerMode::MAY_SKIP || mode == TriggerMode::MUST_SKIP)
				{
					// Start counting press time to see if soft binding should be skipped
					triggerState[idxState] = DstState::PressStart;
					buttons[int(softIndex)].sendEvent(_context->time_now);
				}
				else if (mode == TriggerMode::MAY_SKIP_R || mode == TriggerMode::MUST_SKIP_R)
				{
					triggerState[idxState] = DstState::PressStartResp;
					buttons[int(softIndex)].sendEvent(_context->time_now);
					handleButtonChange(softIndex, true);
				}
				else // mode == NO_FULL or NO_SKIP, NO_SKIP_EXCLUSIVE
				{
					triggerState[idxState] = DstState::SoftPress;
					handleButtonChange(softIndex, true);
				}
			}
			else
			{
				handleButtonChange(softIndex, false);
			}
			break;
		case DstState::PressStart:
			// don't change trigger rumble : keep whatever was set at no press
			if (!isSoftPullPressed(idxState, position))
			{
				// Trigger has been quickly tapped on the soft press
				triggerState[idxState] = DstState::QuickSoftTap;
				handleButtonChange(softIndex, true);
			}
			else if (position == 1.0)
			{
				// Trigger has been full pressed quickly
				triggerState[idxState] = DstState::QuickFullPress;
				handleButtonChange(fullIndex, true);
			}
			else
			{
				GetDuration getter{ _context->time_now };
				if (buttons[int(softIndex)].sendEvent(getter).out_duration >= _context->getSetting(SettingID::TRIGGER_SKIP_DELAY))
				{
					if (mode == TriggerMode::MUST_SKIP)
					{
						trigger_rumble.start = offset + (position + 0.05) * range;
					}
					triggerState[idxState] = DstState::SoftPress;
					// Reset the time for hold soft press purposes.
					buttons[int(softIndex)].sendEvent(_context->time_now);
					handleButtonChange(softIndex, true);
				}
			}
			// Else, time passes as soft press is being held, waiting to see if the soft binding should be skipped
			break;
		case DstState::PressStartResp:
			// don't change trigger rumble : keep whatever was set at no press
			if (!isSoftPullPressed(idxState, position))
			{
				// Soft press is being released
				triggerState[idxState] = DstState::NoPress;
				handleButtonChange(softIndex, false);
			}
			else if (position == 1.0)
			{
				// Trigger has been full pressed quickly
				triggerState[idxState] = DstState::QuickFullPress;
				handleButtonChange(softIndex, false); // Remove soft press
				handleButtonChange(fullIndex, true);
			}
			else
			{
				if (buttons[int(softIndex)].sendEvent(GetDuration{ _context->time_now }).out_duration >= _context->getSetting(SettingID::TRIGGER_SKIP_DELAY))
				{
					if (mode == TriggerMode::MUST_SKIP_R)
					{
						trigger_rumble.start = offset + (position + 0.05) * range;
					}
					triggerState[idxState] = DstState::SoftPress;
				}
				handleButtonChange(softIndex, true);
			}
			break;
		case DstState::QuickSoftTap:
			// Soft trigger is already released. Send release now!
			// don't change trigger rumble : keep whatever was set at no press
			triggerState[idxState] = DstState::NoPress;
			handleButtonChange(softIndex, false);
			break;
		case DstState::QuickFullPress:
			trigger_rumble.mode = 2;
			trigger_rumble.strength = UINT16_MAX;
			trigger_rumble.start = offset + 0.89 * range;
			trigger_rumble.end = offset + 0.99 * range;
			if (position < 1.0f)
			{
				// Full press is being release
				triggerState[idxState] = DstState::QuickFullRelease;
				handleButtonChange(fullIndex, false);
			}
			else
			{
				// Full press is being held
				handleButtonChange(fullIndex, true);
			}
			break;
		case DstState::QuickFullRelease:
			trigger_rumble.mode = 2;
			trigger_rumble.strength = UINT16_MAX;
			trigger_rumble.start = offset + 0.89 * range;
			trigger_rumble.end = offset + 0.99 * range;
			if (!isSoftPullPressed(idxState, position))
			{
				triggerState[idxState] = DstState::NoPress;
			}
			else if (position == 1.0f)
			{
				// Trigger is being full pressed again
				triggerState[idxState] = DstState::QuickFullPress;
				handleButtonChange(fullIndex, true);
			}
			// else wait for the the trigger to be fully released
			break;
		case DstState::SoftPress:
			if (!isSoftPullPressed(idxState, position))
			{
				// Soft press is being released
				handleButtonChange(softIndex, false);
				triggerState[idxState] = DstState::NoPress;
			}
			else // Soft Press is being held
			{
				if (mode == TriggerMode::NO_SKIP || mode == TriggerMode::MAY_SKIP || mode == TriggerMode::MAY_SKIP_R)
				{
					trigger_rumble.strength = min(int(UINT16_MAX), trigger_rumble.strength + int(1/30.f * tick_time  * UINT16_MAX));
					trigger_rumble.start = min(offset + 0.89* range, trigger_rumble.start + 1/150. * tick_time * range);
					trigger_rumble.end = trigger_rumble.start + 0.1 * range;
					handleButtonChange(softIndex, true);
					if (position == 1.0)
					{
						// Full press is allowed in addition to soft press
						triggerState[idxState] = DstState::DelayFullPress;
						handleButtonChange(fullIndex, true);
					}
				}
				else if (mode == TriggerMode::NO_SKIP_EXCLUSIVE)
				{
					trigger_rumble.strength = min(int(UINT16_MAX), trigger_rumble.strength + int(1/30.f * tick_time * UINT16_MAX));
					trigger_rumble.start = min(offset + 0.89* range, trigger_rumble.start + 1/150. * tick_time * range);
					trigger_rumble.end = trigger_rumble.start + 0.1 * range;
					handleButtonChange(softIndex, false);
					if (position == 1.0)
					{
						triggerState[idxState] = DstState::ExclFullPress;
						handleButtonChange(fullIndex, true);
					}
				}
				else // NO_FULL, MUST_SKIP and MUST_SKIP_R
				{
					trigger_rumble.mode = 1;
					trigger_rumble.strength = min(int(UINT16_MAX), trigger_rumble.strength + int(1/30.f * tick_time  * UINT16_MAX));
					// keep old trigger_rumble.start
					handleButtonChange(softIndex, true);
				}
			}
			break;
		case DstState::DelayFullPress:
			trigger_rumble.mode = 2;
			trigger_rumble.strength = UINT16_MAX;
			trigger_rumble.start = offset + 0.8 * range;
			trigger_rumble.end = offset + 0.99 * range;
			if (position < 1.0)
			{
				// Full Press is being released
				triggerState[idxState] = DstState::SoftPress;
				handleButtonChange(fullIndex, false);
			}
			else // Full press is being held
			{
				handleButtonChange(fullIndex, true);
			}
			// Soft press is always held regardless
			handleButtonChange(softIndex, true);
			break;
		case DstState::ExclFullPress:
			trigger_rumble.mode = 2;
			trigger_rumble.strength = UINT16_MAX;
			trigger_rumble.start = offset + 0.89 * range;
			trigger_rumble.end = offset + 0.99 * range;
			if (position < 1.0f)
			{
				// Full press is being release
				triggerState[idxState] = DstState::SoftPress;
				handleButtonChange(fullIndex, false);
				handleButtonChange(softIndex, true);
			}
			else
			{
				// Full press is being held
				handleButtonChange(fullIndex, true);
			}
			break;
		default:
			CERR << "Trigger " << softIndex << " has invalid state " << triggerState[idxState] << ". Reset to NoPress." << endl;
			triggerState[idxState] = DstState::NoPress;
			break;
		}

		return;
	}

	bool IsPressed(ButtonID btn)
	{
		// Use chord stack to know if a button is pressed, because the state from the callback
		// only holds half the information when it comes to a joycon pair.
		// Also, NONE is always part of the stack (for chord handling) but NONE is never pressed.
		return btn != ButtonID::NONE && find(_context->chordStack.begin(), _context->chordStack.end(), btn) != _context->chordStack.end();
	}

	void updateGridSize(size_t noTouchBtns)
	{
		int noGridBtns = max(size_t(0), min(touch_buttons.size(), noTouchBtns) - 5); // Don't include touch stick buttons

		while (gridButtons.size() > noGridBtns)
			gridButtons.pop_back();

		for (int i = gridButtons.size(); i < noGridBtns; ++i)
		{
			JSMButton &map(touch_buttons[i + 5]);
			gridButtons.push_back(DigitalButton(_context, map));
		}
	}
};

function<void(JoyShock *, ButtonID, int, bool)> ScrollAxis::_handleButtonChange = [](JoyShock *js, ButtonID id, int tid, bool pressed) {
	js->handleButtonChange(id, pressed, tid);
};

static void resetAllMappings()
{
	for_each(mappings.begin(), mappings.end(), [](auto &map) { map.Reset(); });
	// Question: Why is this a default mapping? Shouldn't it be empty? It's always possible to calibrate with RESET_GYRO_CALIBRATION
	min_gyro_sens.Reset();
	max_gyro_sens.Reset();
	min_gyro_threshold.Reset();
	max_gyro_threshold.Reset();
	stick_power.Reset();
	stick_sens.Reset();
	real_world_calibration.Reset();
	in_game_sens.Reset();
	left_stick_mode.Reset();
	right_stick_mode.Reset();
	motion_stick_mode.Reset();
	left_ring_mode.Reset();
	right_ring_mode.Reset();
	motion_ring_mode.Reset();
	mouse_x_from_gyro.Reset();
	mouse_y_from_gyro.Reset();
	joycon_gyro_mask.Reset();
	joycon_motion_mask.Reset();
	zlMode.Reset();
	zrMode.Reset();
	trigger_threshold.Reset();
	gyro_settings.Reset();
	aim_y_sign.Reset();
	aim_x_sign.Reset();
	gyro_y_sign.Reset();
	gyro_x_sign.Reset();
	flick_time.Reset();
	flick_time_exponent.Reset();
	gyro_smooth_time.Reset();
	gyro_smooth_threshold.Reset();
	gyro_cutoff_speed.Reset();
	gyro_cutoff_recovery.Reset();
	stick_acceleration_rate.Reset();
	stick_acceleration_cap.Reset();
	left_stick_deadzone_inner.Reset();
	left_stick_deadzone_outer.Reset();
	flick_deadzone_angle.Reset();
	right_stick_deadzone_inner.Reset();
	right_stick_deadzone_outer.Reset();
	motion_deadzone_inner.Reset();
	motion_deadzone_outer.Reset();
	lean_threshold.Reset();
	controller_orientation.Reset();
	screen_resolution_x.Reset();
	screen_resolution_y.Reset();
	mouse_ring_radius.Reset();
	trackball_decay.Reset();
	rotate_smooth_override.Reset();
	flick_snap_strength.Reset();
	flick_snap_mode.Reset();
	trigger_skip_delay.Reset();
	turbo_period.Reset();
	hold_press_time.Reset();
	sim_press_window.Reset();
	dbl_press_window.Reset();
	grid_size.Reset();
	touchpad_mode.Reset();
	touch_stick_mode.Reset();
	touch_stick_radius.Reset();
	touch_deadzone_inner.Reset();
	touch_ring_mode.Reset();
	touchpad_sens.Reset();
	tick_time.Reset();
	light_bar.Reset();
	scroll_sens.Reset();
	rumble_enable.Reset();
	adaptive_trigger.Reset();
	left_trigger_offset.Reset();
	left_trigger_range.Reset();
	right_trigger_offset.Reset();
	right_trigger_range.Reset();
	touch_ds_mode.Reset();
	for_each(touch_buttons.begin(), touch_buttons.end(), [](auto &map) { map.Reset(); });

	os_mouse_speed = 1.0f;
	last_flick_and_rotation = 0.0f;
}

void connectDevices(bool mergeJoycons = true)
{
	handle_to_joyshock.clear();
	int numConnected = jsl->ConnectDevices();
	vector<int> deviceHandles(numConnected, 0);
	if (numConnected > 0)
	{
		jsl->GetConnectedDeviceHandles(&deviceHandles[0], numConnected);

		for (auto handle : deviceHandles) // Don't use foreach!
		{
			auto type = jsl->GetControllerSplitType(handle);
			auto otherJoyCon = find_if(handle_to_joyshock.begin(), handle_to_joyshock.end(),
			  [type](auto &pair) {
				  return type == JS_SPLIT_TYPE_LEFT && pair.second->controller_split_type == JS_SPLIT_TYPE_RIGHT ||
				    type == JS_SPLIT_TYPE_RIGHT && pair.second->controller_split_type == JS_SPLIT_TYPE_LEFT;
			  });
			shared_ptr<JoyShock> js = nullptr;
			if (mergeJoycons && otherJoyCon != handle_to_joyshock.end())
			{
				// The second JC points to the same common buttons as the other one.
				js.reset(new JoyShock(handle, type, otherJoyCon->second->_context));
			}
			else
			{
				js.reset(new JoyShock(handle, type));
			}
			handle_to_joyshock[handle] = js;
		}
	}

	if (numConnected == 1)
	{
		COUT << "1 device connected" << endl;
	}
	else if (numConnected == 0)
	{
		CERR << numConnected << " devices connected" << endl;
	}
	else
	{
		COUT << numConnected << " devices connected" << endl;
	}
	//if (!IsVisible())
	//{
	//	tray->SendNotification(wstring(msg.begin(), msg.end()));
	//}

	//if (numConnected != 0) {
	//	COUT << "All devices have started continuous gyro calibration" << endl;
	//}
}

void SimPressCrossUpdate(ButtonID sim, ButtonID origin, const Mapping &newVal)
{
	mappings[int(sim)].AtSimPress(origin)->operator=(newVal);
}

bool do_NO_GYRO_BUTTON()
{
	// TODO: handle chords
	gyro_settings.Reset();
	return true;
}

bool do_RESET_MAPPINGS(CmdRegistry *registry)
{
	COUT << "Resetting all mappings to defaults" << endl;
	resetAllMappings();
	if (registry)
	{
		if (!registry->loadConfigFile("OnReset.txt"))
		{
			COUT << "There is no ";
			COUT_INFO << "OnReset.txt";
			COUT << " file to load." << endl;
		}
	}
	return true;
}

bool do_RECONNECT_CONTROLLERS(in_string arguments)
{
	bool mergeJoycons = arguments.empty() || (arguments.compare("MERGE") == 0);
	if (mergeJoycons || arguments.rfind("SPLIT", 0) == 0)
	{
		COUT << "Reconnecting controllers: " << arguments << endl;
		jsl->DisconnectAndDisposeAll();
		connectDevices(mergeJoycons);
		jsl->SetCallback(&joyShockPollCallback);
		jsl->SetTouchCallback(&TouchCallback);
		return true;
	}
	return false;
}

bool do_COUNTER_OS_MOUSE_SPEED()
{
	COUT << "Countering OS mouse speed setting" << endl;
	os_mouse_speed = getMouseSpeed();
	return true;
}

bool do_IGNORE_OS_MOUSE_SPEED()
{
	COUT << "Ignoring OS mouse speed setting" << endl;
	os_mouse_speed = 1.0;
	return true;
}

void UpdateThread(PollingThread *thread, const Switch &newValue)
{
	if (thread)
	{
		if (newValue == Switch::ON)
		{
			thread->Start();
		}
		else if (newValue == Switch::OFF)
		{
			thread->Stop();
		}
	}
	else
	{
		COUT << "The thread does not exist" << endl;
	}
}

bool do_CALCULATE_REAL_WORLD_CALIBRATION(in_string argument)
{
	// first, check for a parameter
	float numRotations = 1.0;
	if (argument.length() > 0)
	{
		try
		{
			numRotations = stof(argument);
		}
		catch (invalid_argument ia)
		{
			COUT << "Can't convert \"" << argument << "\" to a number" << endl;
			return false;
		}
	}
	if (numRotations == 0)
	{
		COUT << "Can't calculate calibration from zero rotations" << endl;
	}
	else if (last_flick_and_rotation == 0)
	{
		COUT << "Need to use the flick stick at least once before calculating an appropriate calibration value" << endl;
	}
	else
	{
		COUT << "Recommendation: REAL_WORLD_CALIBRATION = " << setprecision(5) << (*real_world_calibration.get() * last_flick_and_rotation / numRotations) << endl;
	}
	return true;
}

bool do_FINISH_GYRO_CALIBRATION()
{
	COUT << "Finishing continuous calibration for all devices" << endl;
	for (auto iter = handle_to_joyshock.begin(); iter != handle_to_joyshock.end(); ++iter)
	{
		iter->second->motion.PauseContinuousCalibration();
	}
	devicesCalibrating = false;
	return true;
}

bool do_RESTART_GYRO_CALIBRATION()
{
	COUT << "Restarting continuous calibration for all devices" << endl;
	for (auto iter = handle_to_joyshock.begin(); iter != handle_to_joyshock.end(); ++iter)
	{
		iter->second->motion.ResetContinuousCalibration();
		iter->second->motion.StartContinuousCalibration();
	}
	devicesCalibrating = true;
	return true;
}

bool do_SET_MOTION_STICK_NEUTRAL()
{
	COUT << "Setting neutral motion stick orientation..." << endl;
	for (auto iter = handle_to_joyshock.begin(); iter != handle_to_joyshock.end(); ++iter)
	{
		iter->second->set_neutral_quat = true;
	}
	return true;
}

bool do_SLEEP(in_string argument)
{
	// first, check for a parameter
	float sleepTime = 1.0;
	if (argument.length() > 0)
	{
		try
		{
			sleepTime = stof(argument);
		}
		catch (invalid_argument ia)
		{
			COUT << "Can't convert \"" << argument << "\" to a number" << endl;
			return false;
		}
	}

	if (sleepTime <= 0)
	{
		COUT << "Sleep time must be greater than 0 and less than or equal to 10" << endl;
		return false;
	}

	if (sleepTime > 10)
	{
		COUT << "Sleep is capped at 10s per command" << endl;
		sleepTime = 10.f;
	}
	COUT << "Sleeping for " << setprecision(3) << sleepTime << " second(s)..." << endl;
	std::this_thread::sleep_for(std::chrono::milliseconds((int)(sleepTime * 1000)));
	COUT << "Finished sleeping." << endl;

	return true;
}

bool do_README()
{
	auto err = ShowOnlineHelp();
	if (err != 0)
	{
		COUT << "Could not open online help. Error #" << err << endl;
	}
	return true;
}

bool do_WHITELIST_SHOW()
{
	if (whitelister)
	{
		COUT << "Your PID is " << GetCurrentProcessId() << endl;
		whitelister->ShowConsole();
	}
	return true;
}

bool do_WHITELIST_ADD()
{
	string errMsg = "Whitelister is not implemented";
	if (whitelister && whitelister->Add(&errMsg))
	{
		COUT << "JoyShockMapper was successfully whitelisted" << endl;
	}
	else
	{
		CERR << "Whitelist operation failed: " << errMsg << endl;
	}
	return true;
}

bool do_WHITELIST_REMOVE()
{
	string errMsg = "Whitelister is not implemented";
	if (whitelister && whitelister->Remove(&errMsg))
	{
		COUT << "JoyShockMapper removed from whitelist" << endl;
	}
	else
	{
		CERR << "Whitelist operation failed: " << errMsg << endl;
	}
	return true;
}

void DisplayTouchInfo(int id, optional<FloatXY> xy, optional<FloatXY> prevXY = nullopt)
{
	if (xy)
	{
		if (!prevXY)
		{
			cout << "New touch " << id << " at " << *xy << endl;
		}
		else if (fabsf(xy->x() - prevXY->x()) > FLT_EPSILON || fabsf(xy->y() - prevXY->y()) > FLT_EPSILON)
		{
			cout << "Touch " << id << " moved to " << *xy << endl;
		}
	}
	else if (prevXY)
	{
		cout << "Touch " << id << " has been released" << endl;
	}
}

void TouchCallback(int jcHandle, TOUCH_STATE newState, TOUCH_STATE prevState, float delta_time)
{

	//if (current.t0Down || previous.t0Down)
	//{
	//	DisplayTouchInfo(current.t0Down ? current.t0Id : previous.t0Id,
	//		current.t0Down ? optional<FloatXY>({ current.t0X, current.t0Y }) : nullopt,
	//		previous.t0Down ? optional<FloatXY>({ previous.t0X, previous.t0Y }) : nullopt);
	//}

	//if (current.t1Down || previous.t1Down)
	//{
	//	DisplayTouchInfo(current.t1Down ? current.t1Id : previous.t1Id,
	//		current.t1Down ? optional<FloatXY>({ current.t1X, current.t1Y }) : nullopt,
	//		previous.t1Down ? optional<FloatXY>({ previous.t1X, previous.t1Y }) : nullopt);
	//}

	shared_ptr<JoyShock> js = handle_to_joyshock[jcHandle];
	int tpSizeX, tpSizeY;
	if (!js || jsl->GetTouchpadDimension(jcHandle, tpSizeX, tpSizeY) == false)
		return;

	lock_guard guard(js->_context->callback_lock);

	TOUCH_POINT point0, point1;

	point0.posX = newState.t0Down ? newState.t0X : -1.f; // Absolute position in percentage
	point0.posY = newState.t0Down ? newState.t0Y : -1.f;
	point0.movX = js->prevTouchState.t0Down ? (newState.t0X - js->prevTouchState.t0X) * tpSizeX : 0.f; // Relative movement in unit
	point0.movY = js->prevTouchState.t0Down ? (newState.t0Y - js->prevTouchState.t0Y) * tpSizeY : 0.f;
	point1.posX = newState.t1Down ? newState.t1X : -1.f;
	point1.posY = newState.t1Down ? newState.t1Y : -1.f;
	point1.movX = js->prevTouchState.t1Down ? (newState.t1X - js->prevTouchState.t1X) * tpSizeX : 0.f;
	point1.movY = js->prevTouchState.t1Down ? (newState.t1Y - js->prevTouchState.t1Y) * tpSizeY : 0.f;

	auto mode = js->_context->getSetting<TouchpadMode>(SettingID::TOUCHPAD_MODE);
	js->handleButtonChange(ButtonID::TOUCH, point0.isDown() || point1.isDown());
	if (!point0.isDown() && !point1.isDown())
	{

		static const std::function<bool(ButtonID)> IS_TOUCH_BUTTON = [](ButtonID id) {
			return id >= ButtonID::T1;
		};

		for (auto currentlyActive = find_if(js->_context->chordStack.begin(), js->_context->chordStack.end(), IS_TOUCH_BUTTON);
		     currentlyActive != js->_context->chordStack.end();
		     currentlyActive = find_if(js->_context->chordStack.begin(), js->_context->chordStack.end(), IS_TOUCH_BUTTON))
		{
			js->_context->chordStack.erase(currentlyActive);
		}
	}
	if (mode == TouchpadMode::GRID_AND_STICK)
	{
		// Handle grid
		int index0 = -1, index1 = -1;
		if (point0.isDown())
		{
			float row = floorf(point0.posY * grid_size.get().y());
			float col = floorf(point0.posX * grid_size.get().x());
			//cout << "I should be in button " << row << " " << col << endl;
			index0 = int(row * grid_size.get().x() + col) + 5;
		}

		if (point1.isDown())
		{
			float row = floorf(point1.posY * grid_size.get().y());
			float col = floorf(point1.posX * grid_size.get().x());
			//cout << "I should be in button " << row << " " << col << endl;
			index1 = int(row * grid_size.get().x() + col) + 5;
		}

		for (int i = 5; i < touch_buttons.size(); ++i)
		{
			auto optId = magic_enum::enum_cast<ButtonID>(FIRST_TOUCH_BUTTON + i);

			// JSM can get touch button callbacks before the grid buttons are setup at startup. Just skip then.
			if (optId && js->gridButtons.size() == touch_buttons.size() - 5)
				js->handleButtonChange(*optId, i == index0 || i == index1);
		}

		// Handle stick
		js->touchpads[0].handleTouchStickChange(js, point0.isDown(), point0.movX, point0.movY, delta_time, js->controller_split_type);
		js->touchpads[1].handleTouchStickChange(js, point1.isDown(), point1.movX, point1.movY, delta_time, js->controller_split_type);
	}
	else if (mode == TouchpadMode::MOUSE)
	{
			// Disable gestures
		/*if (point0.isDown() && point1.isDown())
		{*/
			//if (js->prevTouchState.t0Down && js->prevTouchState.t1Down)
			//{
			//	float x = fabsf(newState.t0X - newState.t1X);
			//	float y = fabsf(newState.t0Y - newState.t1Y);
			//	float angle = atan2f(y, x) / PI * 360;
			//	float dist = sqrt(x * x + y * y);
			//	x = fabsf(js->prevTouchState.t0X - js->prevTouchState.t1X);
			//	y = fabsf(js->prevTouchState.t0Y - js->prevTouchState.t1Y);
			//	float oldAngle = atan2f(y, x) / PI * 360;
			//	float oldDist = sqrt(x * x + y * y);
			//	if (angle != oldAngle)
			//		DEBUG_LOG << "Angle went from " << oldAngle << " degrees to " << angle << " degress. Diff is " << angle - oldAngle << " degrees. ";
			//	js->touch_scroll_x.ProcessScroll(angle - oldAngle, js->_context->getSetting<FloatXY>(SettingID::SCROLL_SENS).x(), js->_context->time_now);
			//	if (dist != oldDist)
			//		DEBUG_LOG << "Dist went from " << oldDist << " points to " << dist << " points. Diff is " << dist - oldDist << " points. ";
			//	js->touch_scroll_y.ProcessScroll(dist - oldDist, js->_context->getSetting<FloatXY>(SettingID::SCROLL_SENS).y(), js->_context->time_now);
			//}
			//else
			//{
			//	js->touch_scroll_x.Reset(js->_context->time_now);
			//	js->touch_scroll_y.Reset(js->_context->time_now);
			//}
		//}
		//else
		//{
		//	js->touch_scroll_x.Reset(js->_context->time_now);
		//	js->touch_scroll_y.Reset(js->_context->time_now);
		//  if (point0.isDown() ^ point1.isDown()) // XOR
			if (point0.isDown() || point1.isDown())
			{
				TOUCH_POINT *downPoint = point0.isDown() ? &point0 : &point1;
				FloatXY sens = js->_context->getSetting<FloatXY>(SettingID::TOUCHPAD_SENS);
				// if(downPoint->movX || downPoint->movY) cout << "Moving the cursor by " << std::dec << int(downPoint->movX) << " h and " << int(downPoint->movY) << " v" << endl;
				moveMouse(downPoint->movX * sens.x(), downPoint->movY * sens.y());
				// Ignore second touch point in this mode for now until gestures gets handled here
			}
		//}
	}
	js->prevTouchState = newState;
}

void joyShockPollCallback(int jcHandle, JOY_SHOCK_STATE state, JOY_SHOCK_STATE lastState, IMU_STATE imuState, IMU_STATE lastImuState, float deltaTime)
{

	shared_ptr<JoyShock> jc = handle_to_joyshock[jcHandle];
	if (jc == nullptr)
		return;
	jc->_context->callback_lock.lock();

	auto timeNow = chrono::steady_clock::now();
	deltaTime = ((float)chrono::duration_cast<chrono::microseconds>(timeNow - jc->_context->time_now).count()) / 1000000.0f;
	jc->_context->time_now = timeNow;

	if (triggersCalibrating)
	{
		auto rpos = jsl->GetRightTrigger(jcHandle);
		auto lpos = jsl->GetLeftTrigger(jcHandle);
		switch (triggersCalibrating)
		{
		case 1:
			COUT << "Softly press on the right trigger only just until you feel the resistance, then press the dpad DOWN button" << endl;
			tick_time = 100;
			jc->right_effect.mode = 2;
			jc->right_effect.start = 0;
			jc->right_effect.end = 255;
			jc->right_effect.strength = 255;
			triggersCalibrating++;
			break;
		case 2:
			if (jsl->GetButtons(jcHandle) & (1 << JSOFFSET_DOWN))
			{
				triggersCalibrating++;
			}
			break;
		case 3:
			DEBUG_LOG << "trigger pos is at " << int(rpos * 255.f) << " (" << int(rpos * 100.f) << "%) and effect pos is at " << int(jc->right_effect.start) << endl;
			if (int(rpos * 255.f) > 0)
			{
				right_trigger_offset = jc->right_effect.start;
				tick_time = 40;
				triggersCalibrating++;
			}
			++jc->right_effect.start;
			break;
		case 4:
			DEBUG_LOG << "trigger pos is at " << int(rpos * 255.f) << " (" << int(rpos * 100.f) << "%) and effect pos is at " << int(jc->right_effect.start) << endl;
			if (int(rpos * 255.f) > 240)
			{
				tick_time = 100;
				triggersCalibrating++;
			}
			++jc->right_effect.start;
			break;
		case 5:
			DEBUG_LOG << "trigger pos is at " << int(rpos * 255.f) << " (" << int(rpos * 100.f) << "%) and effect pos is at " << int(jc->right_effect.start) << endl;
			if (int(rpos * 255.f) == 255)
			{
				triggersCalibrating++;
				right_trigger_range = int(jc->right_effect.start - right_trigger_offset);
			}
			++jc->right_effect.start;
			break;
		case 6:
			COUT << "Softly press on the left trigger only just until you feel the resistance, then press the SOUTH button" << endl;
			tick_time = 100;
			jc->left_effect.mode = 2;
			jc->left_effect.start = 0;
			jc->left_effect.end = 255;
			jc->left_effect.strength = 255;
			triggersCalibrating++;
			break;
		case 7:
			if (jsl->GetButtons(jcHandle) & (1 << JSOFFSET_S))
			{
				triggersCalibrating++;
			}
			break;
		case 8:
			DEBUG_LOG << "trigger pos is at " << int(lpos * 255.f) << " (" << int(lpos * 100.f) << "%) and effect pos is at " << int(jc->left_effect.start) << endl;
			if (int(lpos * 255.f) > 0)
			{
				left_trigger_offset = jc->left_effect.start;
				tick_time = 40;
				triggersCalibrating++;
			}
			++jc->left_effect.start;
			break;
		case 9:
			DEBUG_LOG << "trigger pos is at " << int(lpos * 255.f) << " (" << int(lpos * 100.f) << "%) and effect pos is at " << int(jc->left_effect.start) << endl;
			if (int(lpos * 255.f) > 240)
			{
				tick_time = 100;
				triggersCalibrating++;
			}
			++jc->left_effect.start;
			break;
		case 10:
			DEBUG_LOG << "trigger pos is at " << int(lpos * 255.f) << " (" << int(lpos * 100.f) << "%) and effect pos is at " << int(jc->left_effect.start) << endl;
			if (int(lpos * 255.f) == 255)
			{
				triggersCalibrating++;
				left_trigger_range = int(jc->left_effect.start - left_trigger_offset);
			}
			++jc->left_effect.start;
			break;
		case 11:
			COUT << "Your triggers have been successfully calibrated. Add the trigger offset and range values in your onreset.txt file to have those values set by default." << endl;
			COUT_INFO << SettingID::RIGHT_TRIGGER_OFFSET << " = " << right_trigger_offset << endl;
			COUT_INFO << SettingID::RIGHT_TRIGGER_RANGE << " = " << right_trigger_range << endl;
			COUT_INFO << SettingID::LEFT_TRIGGER_OFFSET << " = " << left_trigger_offset << endl;
			COUT_INFO << SettingID::LEFT_TRIGGER_RANGE << " = " << left_trigger_range << endl;
			triggersCalibrating = 0;
			tick_time.Reset();
			break;
		}
		jsl->SetRightTriggerEffect(jcHandle, jc->right_effect);
		jsl->SetLeftTriggerEffect(jcHandle, jc->left_effect);
		jc->_context->callback_lock.unlock();
		return;
	}

	GamepadMotion &motion = jc->motion;

	IMU_STATE imu = jsl->GetIMUState(jc->handle);

	motion.ProcessMotion(imu.gyroX, imu.gyroY, imu.gyroZ, imu.accelX, imu.accelY, imu.accelZ, deltaTime);

	float inGyroX, inGyroY, inGyroZ;
	motion.GetCalibratedGyro(inGyroX, inGyroY, inGyroZ);

	float inGravX, inGravY, inGravZ;
	motion.GetGravity(inGravX, inGravY, inGravZ);
	inGravX *= 1.f / 9.8f; // to Gs
	inGravY *= 1.f / 9.8f;
	inGravZ *= 1.f / 9.8f;

	float inQuatW, inQuatX, inQuatY, inQuatZ;
	motion.GetOrientation(inQuatW, inQuatX, inQuatY, inQuatZ);

	//COUT << "DS4 accel: %.4f, %.4f, %.4f\n", imuState.accelX, imuState.accelY, imuState.accelZ);
	//COUT << "\tDS4 gyro: %.4f, %.4f, %.4f\n", imuState.gyroX, imuState.gyroY, imuState.gyroZ);
	//COUT << "\tDS4 quat: %.4f, %.4f, %.4f, %.4f | accel: %.4f, %.4f, %.4f | grav: %.4f, %.4f, %.4f\n",
	//	inQuatW, inQuatX, inQuatY, inQuatZ,
	//	motion.accelX, motion.accelY, motion.accelZ,
	//	inGravvX, inGravY, inGravZ);

	bool blockGyro = false;
	bool lockMouse = false;
	bool leftAny = false;
	bool rightAny = false;
	bool motionAny = false;

	if (jc->set_neutral_quat)
	{
		jc->neutralQuatW = inQuatW;
		jc->neutralQuatX = inQuatX;
		jc->neutralQuatY = inQuatY;
		jc->neutralQuatZ = inQuatZ;
		jc->set_neutral_quat = false;
		COUT << "Neutral orientation for device " << jc->handle << " set..." << endl;
	}

	float gyroX = 0.0;
	float gyroY = 0.0;
	int mouse_x_flag = (int)jc->_context->getSetting<GyroAxisMask>(SettingID::MOUSE_X_FROM_GYRO_AXIS);
	if ((mouse_x_flag & (int)GyroAxisMask::X) > 0)
	{
		gyroX += inGyroX;
	}
	if ((mouse_x_flag & (int)GyroAxisMask::Y) > 0)
	{
		gyroX -= inGyroY;
	}
	if ((mouse_x_flag & (int)GyroAxisMask::Z) > 0)
	{
		gyroX -= inGyroZ;
	}
	int mouse_y_flag = (int)jc->_context->getSetting<GyroAxisMask>(SettingID::MOUSE_Y_FROM_GYRO_AXIS);
	if ((mouse_y_flag & (int)GyroAxisMask::X) > 0)
	{
		gyroY -= inGyroX;
	}
	if ((mouse_y_flag & (int)GyroAxisMask::Y) > 0)
	{
		gyroY += inGyroY;
	}
	if ((mouse_y_flag & (int)GyroAxisMask::Z) > 0)
	{
		gyroY += inGyroZ;
	}
	float gyroLength = sqrt(gyroX * gyroX + gyroY * gyroY);
	// do gyro smoothing
	// convert gyro smooth time to number of samples
	auto numGyroSamples = 1.f / tick_time * jc->_context->getSetting(SettingID::GYRO_SMOOTH_TIME); // samples per second * seconds = samples
	if (numGyroSamples < 1)
		numGyroSamples = 1; // need at least 1 sample
	auto threshold = jc->_context->getSetting(SettingID::GYRO_SMOOTH_THRESHOLD);
	jc->GetSmoothedGyro(gyroX, gyroY, gyroLength, threshold / 2.0f, threshold, int(numGyroSamples), gyroX, gyroY);
	//COUT << "%d Samples for threshold: %0.4f\n", numGyroSamples, gyro_smooth_threshold * maxSmoothingSamples);

	// now, honour gyro_cutoff_speed
	gyroLength = sqrt(gyroX * gyroX + gyroY * gyroY);
	auto speed = jc->_context->getSetting(SettingID::GYRO_CUTOFF_SPEED);
	auto recovery = jc->_context->getSetting(SettingID::GYRO_CUTOFF_RECOVERY);
	if (recovery > speed)
	{
		// we can use gyro_cutoff_speed
		float gyroIgnoreFactor = (gyroLength - speed) / (recovery - speed);
		if (gyroIgnoreFactor < 1.0f)
		{
			if (gyroIgnoreFactor <= 0.0f)
			{
				gyroX = gyroY = gyroLength = 0.0f;
			}
			else
			{
				gyroX *= gyroIgnoreFactor;
				gyroY *= gyroIgnoreFactor;
				gyroLength *= gyroIgnoreFactor;
			}
		}
	}
	else if (speed > 0.0f && gyroLength < speed)
	{
		// gyro_cutoff_recovery is something weird, so we just do a hard threshold
		gyroX = gyroY = gyroLength = 0.0f;
	}

	jc->_context->time_now = std::chrono::steady_clock::now();

	// sticks!
	ControllerOrientation controllerOrientation = jc->_context->getSetting<ControllerOrientation>(SettingID::CONTROLLER_ORIENTATION);
	if (controllerOrientation == ControllerOrientation::JOYCON_SIDEWAYS)
	{
		if (jc->controller_split_type == JS_SPLIT_TYPE_LEFT)
		{
			controllerOrientation = ControllerOrientation::LEFT;
		}
		else if (jc->controller_split_type == JS_SPLIT_TYPE_RIGHT)
		{
			controllerOrientation = ControllerOrientation::RIGHT;
		}
		else
		{
			controllerOrientation = ControllerOrientation::FORWARD;
		}
	}

	float camSpeedX = 0.0f;
	float camSpeedY = 0.0f;
	// account for os mouse speed and convert from radians to degrees because gyro reports in degrees per second
	float mouseCalibrationFactor = 180.0f / PI / os_mouse_speed;
	FloatXY sens(jc->_context->getSetting<FloatXY>(SettingID::STICK_SENS));
	sens.first *= jc->_context->getSetting(SettingID::STICK_AXIS_X);
	sens.second *= jc->_context->getSetting(SettingID::STICK_AXIS_Y);
	if (jc->controller_split_type != JS_SPLIT_TYPE_RIGHT)
	{
		// let's do these sticks... don't want to constantly send input, so we need to compare them to last time
		FloatXY cal = {jsl->GetLeftX(jc->handle), jsl->GetLeftY(jc->handle)};
		leftAny = jc->leftStick.processStick(cal, sens, jc->controller_split_type, mouseCalibrationFactor, deltaTime, lockMouse, camSpeedX, camSpeedY);
	}

	if (jc->controller_split_type != JS_SPLIT_TYPE_LEFT)
	{
		FloatXY cal = {jsl->GetRightX(jc->handle), jsl->GetRightY(jc->handle)};
		rightAny = jc->rightStick.processStick(cal, sens, jc->controller_split_type, mouseCalibrationFactor, deltaTime, lockMouse, camSpeedX, camSpeedY);
	}

	if (jc->controller_split_type == JS_SPLIT_TYPE_FULL ||
	  (jc->controller_split_type & (int)jc->_context->getSetting<JoyconMask>(SettingID::JOYCON_MOTION_MASK)) == 0)
	{
		Quat neutralQuat = Quat(jc->neutralQuatW, jc->neutralQuatX, jc->neutralQuatY, jc->neutralQuatZ);
		Vec grav = Vec(inGravX, inGravY, inGravZ) * neutralQuat;

		// use gravity vector deflection
		FloatXY cal = { grav.x, -grav.z };
		float gravLength2D = sqrtf(grav.x * grav.x + grav.z * grav.z);
		float gravStickDeflection = atan2f(gravLength2D, -grav.y) / PI;
		if (gravLength2D > 0)
		{
			cal.first *= gravStickDeflection / gravLength2D;
			cal.second *= gravStickDeflection / gravLength2D;
		}


		// motion deadzones are divided by 180
		motionAny = jc->motionStick.processStick(cal, sens, jc->controller_split_type, mouseCalibrationFactor, deltaTime, lockMouse, camSpeedX, camSpeedY);

		float gravLength3D = grav.Length();
		if (gravLength3D > 0)
		{
			float gravSideDir;
			switch (controllerOrientation)
			{
			case ControllerOrientation::FORWARD:
				gravSideDir = grav.x;
				break;
			case ControllerOrientation::LEFT:
				gravSideDir = grav.z;
				break;
			case ControllerOrientation::RIGHT:
				gravSideDir = -grav.z;
				break;
			case ControllerOrientation::BACKWARD:
				gravSideDir = -grav.x;
				break;
			}
			float gravDirX = gravSideDir / gravLength3D;
			float sinLeanThreshold = sin(jc->_context->getSetting(SettingID::LEAN_THRESHOLD) * PI / 180.f);
			jc->handleButtonChange(ButtonID::LEAN_LEFT, gravDirX < -sinLeanThreshold);
			jc->handleButtonChange(ButtonID::LEAN_RIGHT, gravDirX > sinLeanThreshold);
		}
	}

	int buttons = jsl->GetButtons(jc->handle);
	// button mappings
	if (jc->controller_split_type != JS_SPLIT_TYPE_RIGHT)
	{
		jc->handleButtonChange(ButtonID::UP, buttons & (1 << JSOFFSET_UP));
		jc->handleButtonChange(ButtonID::DOWN, buttons & (1 << JSOFFSET_DOWN));
		jc->handleButtonChange(ButtonID::LEFT, buttons & (1 << JSOFFSET_LEFT));
		jc->handleButtonChange(ButtonID::RIGHT, buttons & (1 << JSOFFSET_RIGHT));
		jc->handleButtonChange(ButtonID::L, buttons & (1 << JSOFFSET_L));
		jc->handleButtonChange(ButtonID::MINUS, buttons & (1 << JSOFFSET_MINUS));
		// for backwards compatibility, we need need to account for the fact that SDL2 maps the touchpad button differently to SDL
		jc->handleButtonChange(ButtonID::L3, buttons & (1 << JSOFFSET_LCLICK));

		float lTrigger = jsl->GetLeftTrigger(jc->handle);
		jc->handleTriggerChange(ButtonID::ZL, ButtonID::ZLF, jc->_context->getSetting<TriggerMode>(SettingID::ZL_MODE), lTrigger, jc->left_effect);

		bool touch = jsl->GetTouchDown(jc->handle, false) || jsl->GetTouchDown(jc->handle, true);
		switch (jc->platform_controller_type)
		{
		case JS_TYPE_DS:
			// JSL mapps mic button on the SL index
			jc->handleButtonChange(ButtonID::MIC, buttons & (1 << JSOFFSET_MIC));
			// Don't break but continue onto DS4 stuff too
		case JS_TYPE_DS4:
		{
			float triggerpos = buttons & (1 << JSOFFSET_CAPTURE) ? 1.f :
			  touch                                              ? 0.99f :
                                                                   0.f;
			jc->handleTriggerChange(ButtonID::TOUCH, ButtonID::CAPTURE, jc->_context->getSetting<TriggerMode>(SettingID::TOUCHPAD_DUAL_STAGE_MODE), triggerpos, jc->unused_effect);
		}
		break;
		default:
		{
			jc->handleButtonChange(ButtonID::TOUCH, touch);
			jc->handleButtonChange(ButtonID::CAPTURE, buttons & (1 << JSOFFSET_CAPTURE));
			jc->handleButtonChange(ButtonID::LSL, buttons & (1 << JSOFFSET_SL));
		}
		break;
		}

		// SL and SR are mapped to back paddle positions:
		jc->handleButtonChange(ButtonID::LSR, buttons & (1 << JSOFFSET_SR));
	}
	else // split type is RIGHT
	{
		// SL and SR are mapped to back paddle positions:
		jc->handleButtonChange(ButtonID::RSL, buttons & (1 << JSOFFSET_SL));
		jc->handleButtonChange(ButtonID::RSR, buttons & (1 << JSOFFSET_SR));
	}

	if (jc->controller_split_type != JS_SPLIT_TYPE_LEFT)
	{
		jc->handleButtonChange(ButtonID::E, buttons & (1 << JSOFFSET_E));
		jc->handleButtonChange(ButtonID::S, buttons & (1 << JSOFFSET_S));
		jc->handleButtonChange(ButtonID::N, buttons & (1 << JSOFFSET_N));
		jc->handleButtonChange(ButtonID::W, buttons & (1 << JSOFFSET_W));
		jc->handleButtonChange(ButtonID::R, buttons & (1 << JSOFFSET_R));
		jc->handleButtonChange(ButtonID::PLUS, buttons & (1 << JSOFFSET_PLUS));
		jc->handleButtonChange(ButtonID::HOME, buttons & (1 << JSOFFSET_HOME));
		jc->handleButtonChange(ButtonID::R3, buttons & (1 << JSOFFSET_RCLICK));

		float rTrigger = jsl->GetRightTrigger(jc->handle);
		jc->handleTriggerChange(ButtonID::ZR, ButtonID::ZRF, jc->_context->getSetting<TriggerMode>(SettingID::ZR_MODE), rTrigger, jc->right_effect);
	}

	if (jc->_context->getSetting<Switch>(SettingID::ADAPTIVE_TRIGGER) == Switch::ON)
	{
		jsl->SetRightTriggerEffect(jc->handle, jc->right_effect);
		jsl->SetLeftTriggerEffect(jc->handle, jc->left_effect);
	}
	else
	{
		JOY_SHOCK_TRIGGER_EFFECT none;
		jsl->SetRightTriggerEffect(jc->handle, none);
		jsl->SetLeftTriggerEffect(jc->handle, none);
	}

	// Handle buttons before GYRO because some of them may affect the value of blockGyro
	auto gyro = jc->_context->getSetting<GyroSettings>(SettingID::GYRO_ON); // same result as getting GYRO_OFF
	switch (gyro.ignore_mode)
	{
	case GyroIgnoreMode::BUTTON:
		blockGyro = gyro.always_off ^ jc->IsPressed(gyro.button);
		break;
	case GyroIgnoreMode::LEFT_STICK:
		blockGyro = (gyro.always_off ^ leftAny);
		break;
	case GyroIgnoreMode::RIGHT_STICK:
		blockGyro = (gyro.always_off ^ rightAny);
		break;
	}
	float gyro_x_sign_to_use = jc->_context->getSetting(SettingID::GYRO_AXIS_X);
	float gyro_y_sign_to_use = jc->_context->getSetting(SettingID::GYRO_AXIS_Y);

	bool trackball_x_pressed = false;
	bool trackball_y_pressed = false;

	// Apply gyro modifiers in the queue from oldest to newest (thus giving priority to most recent)
	for (auto pair : jc->_context->gyroActionQueue)
	{
		if (pair.second.code == GYRO_ON_BIND)
			blockGyro = false;
		else if (pair.second.code == GYRO_OFF_BIND)
			blockGyro = true;
		else if (pair.second.code == GYRO_INV_X)
			gyro_x_sign_to_use = jc->_context->getSetting(SettingID::GYRO_AXIS_X) * -1; // Intentionally don't support multiple inversions
		else if (pair.second.code == GYRO_INV_Y)
			gyro_y_sign_to_use = jc->_context->getSetting(SettingID::GYRO_AXIS_Y) * -1; // Intentionally don't support multiple inversions
		else if (pair.second.code == GYRO_INVERT)
		{
			// Intentionally don't support multiple inversions
			gyro_x_sign_to_use = jc->_context->getSetting(SettingID::GYRO_AXIS_X) * -1;
			gyro_y_sign_to_use = jc->_context->getSetting(SettingID::GYRO_AXIS_Y) * -1;
		}
		else if (pair.second.code == GYRO_TRACK_X)
			trackball_x_pressed = true;
		else if (pair.second.code == GYRO_TRACK_Y)
			trackball_y_pressed = true;
		else if (pair.second.code == GYRO_TRACKBALL)
		{
			trackball_x_pressed = true;
			trackball_y_pressed = true;
		}
	}

	float decay = exp2f(-deltaTime * jc->_context->getSetting(SettingID::TRACKBALL_DECAY));
	int maxTrackballSamples = max(1, min(jc->numLastGyroSamples, (int)(1.f / deltaTime * 0.125f)));

	if (!trackball_x_pressed && !trackball_y_pressed)
	{
		jc->lastGyroAbsX = abs(gyroX);
		jc->lastGyroAbsY = abs(gyroY);
	}

	if (!trackball_x_pressed)
	{
		int gyroSampleIndex = jc->lastGyroIndexX = (jc->lastGyroIndexX + 1) % maxTrackballSamples;
		jc->lastGyroX[gyroSampleIndex] = gyroX;
	}
	else
	{
		float lastGyroX = 0.f;
		for (int gyroAverageIdx = 0; gyroAverageIdx < maxTrackballSamples; gyroAverageIdx++)
		{
			lastGyroX += jc->lastGyroX[gyroAverageIdx];
			jc->lastGyroX[gyroAverageIdx] *= decay;
		}
		lastGyroX /= maxTrackballSamples;
		float lastGyroAbsX = abs(lastGyroX);
		if (lastGyroAbsX > jc->lastGyroAbsX)
		{
			lastGyroX *= jc->lastGyroAbsX / lastGyroAbsX;
		}
		gyroX = lastGyroX;
	}
	if (!trackball_y_pressed)
	{
		int gyroSampleIndex = jc->lastGyroIndexY = (jc->lastGyroIndexY + 1) % maxTrackballSamples;
		jc->lastGyroY[gyroSampleIndex] = gyroY;
	}
	else
	{
		float lastGyroY = 0.f;
		for (int gyroAverageIdx = 0; gyroAverageIdx < maxTrackballSamples; gyroAverageIdx++)
		{
			lastGyroY += jc->lastGyroY[gyroAverageIdx];
			jc->lastGyroY[gyroAverageIdx] *= decay;
		}
		lastGyroY /= maxTrackballSamples;
		float lastGyroAbsY = abs(lastGyroY);
		if (lastGyroAbsY > jc->lastGyroAbsY)
		{
			lastGyroY *= jc->lastGyroAbsY / lastGyroAbsY;
		}
		gyroY = lastGyroY;
	}

	if (blockGyro)
	{
		gyroX = 0;
		gyroY = 0;
	}
	// optionally ignore the gyro of one of the joycons
	if (!lockMouse &&
	  (jc->controller_split_type == JS_SPLIT_TYPE_FULL ||
	    (jc->controller_split_type & (int)jc->_context->getSetting<JoyconMask>(SettingID::JOYCON_GYRO_MASK)) == 0))
	{
		//COUT << "GX: %0.4f GY: %0.4f GZ: %0.4f\n", imuState.gyroX, imuState.gyroY, imuState.gyroZ);
		float mouseCalibration = jc->_context->getSetting(SettingID::REAL_WORLD_CALIBRATION) / os_mouse_speed / jc->_context->getSetting(SettingID::IN_GAME_SENS);
		shapedSensitivityMoveMouse(gyroX * gyro_x_sign_to_use, gyroY * gyro_y_sign_to_use, jc->_context->getSetting<FloatXY>(SettingID::MIN_GYRO_SENS), jc->_context->getSetting<FloatXY>(SettingID::MAX_GYRO_SENS),
		  jc->_context->getSetting(SettingID::MIN_GYRO_THRESHOLD), jc->_context->getSetting(SettingID::MAX_GYRO_THRESHOLD), deltaTime,
		  camSpeedX * jc->_context->getSetting(SettingID::STICK_AXIS_X), -camSpeedY * jc->_context->getSetting(SettingID::STICK_AXIS_Y), mouseCalibration);
	}
	if (jc->_context->_vigemController)
	{
		jc->_context->_vigemController->update(); // Check for initialized built-in
	}
	auto newColor = jc->_context->getSetting<Color>(SettingID::LIGHT_BAR);
	if (jc->_light_bar != newColor)
	{
		jsl->SetLightColour(jc->handle, newColor.raw);
		jc->_light_bar = newColor;
	}
	jc->_context->callback_lock.unlock();
}

// https://stackoverflow.com/a/25311622/1130520 says this is why filenames obtained by fgets don't work
static void removeNewLine(char *string)
{
	char *p;
	if ((p = strchr(string, '\n')) != NULL)
	{
		*p = '\0'; /* remove newline */
	}
}

// https://stackoverflow.com/a/4119881/1130520 gives us case insensitive equality
static bool iequals(const string &a, const string &b)
{
	return equal(a.begin(), a.end(),
	  b.begin(), b.end(),
	  [](char a, char b) {
		  return tolower(a) == tolower(b);
	  });
}

bool AutoLoadPoll(void *param)
{
	auto registry = reinterpret_cast<CmdRegistry *>(param);
	static string lastModuleName;
	string windowTitle, windowModule;
	tie(windowModule, windowTitle) = GetActiveWindowName();
	if (!windowModule.empty() && windowModule != lastModuleName && windowModule.compare("JoyShockMapper.exe") != 0)
	{
		lastModuleName = windowModule;
		string path(AUTOLOAD_FOLDER());
		auto files = ListDirectory(path);
		auto noextmodule = windowModule.substr(0, windowModule.find_first_of('.'));
		COUT_INFO << "[AUTOLOAD] \"" << windowTitle << "\" in focus: "; // looking for config : " , );
		bool success = false;
		for (auto file : files)
		{
			auto noextconfig = file.substr(0, file.find_first_of('.'));
			if (iequals(noextconfig, noextmodule))
			{
				COUT_INFO << "loading \"AutoLoad\\" << noextconfig << ".txt\"." << endl;
				loading_lock.lock();
				registry->processLine(path + file);
				loading_lock.unlock();
				COUT_INFO << "[AUTOLOAD] Loading completed" << endl;
				success = true;
				break;
			}
		}
		if (!success)
		{
			COUT_INFO << "create ";
			COUT << "AutoLoad\\" << noextmodule << ".txt";
			COUT_INFO << " to autoload for this application." << endl;
		}
	}
	return true;
}

bool MinimizePoll(void *param)
{
	if (isConsoleMinimized())
	{
		HideConsole();
	}
	return true;
}

void beforeShowTrayMenu()
{
	if (!tray || !*tray)
		CERR << "ERROR: Cannot create tray item." << endl;
	else
	{
		tray->ClearMenuMap();
		tray->AddMenuItem(U("Show Console"), &ShowConsole);
		tray->AddMenuItem(U("Reconnect controllers"), []() {
			WriteToConsole("RECONNECT_CONTROLLERS");
		});
		tray->AddMenuItem(
		  U("AutoLoad"), [](bool isChecked) {
			  autoloadSwitch = isChecked ? Switch::ON : Switch::OFF;
		  },
		  bind(&PollingThread::isRunning, autoLoadThread.get()));

		if (whitelister && whitelister->IsAvailable())
		{
			tray->AddMenuItem(
			  U("Whitelist"), [](bool isChecked) {
				  isChecked ?
                    do_WHITELIST_ADD() :
                    do_WHITELIST_REMOVE();
			  },
			  bind(&Whitelister::operator bool, whitelister.get()));
		}
		tray->AddMenuItem(
		  U("Calibrate all devices"), [](bool isChecked) { isChecked ?
                                                             WriteToConsole("RESTART_GYRO_CALIBRATION") :
                                                             WriteToConsole("FINISH_GYRO_CALIBRATION"); }, []() { return devicesCalibrating; });

		string autoloadFolder{ AUTOLOAD_FOLDER() };
		for (auto file : ListDirectory(autoloadFolder.c_str()))
		{
			string fullPathName = ".\\AutoLoad\\" + file;
			auto noext = file.substr(0, file.find_last_of('.'));
			tray->AddMenuItem(U("AutoLoad folder"), UnicodeString(noext.begin(), noext.end()), [fullPathName] {
				WriteToConsole(string(fullPathName.begin(), fullPathName.end()));
				autoLoadThread->Stop();
			});
		}
		std::string gyroConfigsFolder{ GYRO_CONFIGS_FOLDER() };
		for (auto file : ListDirectory(gyroConfigsFolder.c_str()))
		{
			string fullPathName = ".\\GyroConfigs\\" + file;
			auto noext = file.substr(0, file.find_last_of('.'));
			tray->AddMenuItem(U("GyroConfigs folder"), UnicodeString(noext.begin(), noext.end()), [fullPathName] {
				WriteToConsole(string(fullPathName.begin(), fullPathName.end()));
				autoLoadThread->Stop();
			});
		}
		tray->AddMenuItem(U("Calculate RWC"), []() {
			WriteToConsole("CALCULATE_REAL_WORLD_CALIBRATION");
			ShowConsole();
		});
		tray->AddMenuItem(
		  U("Hide when minimized"), [](bool isChecked) {
			  hide_minimized = isChecked ? Switch::ON : Switch::OFF;
			  if (!isChecked)
				  UnhideConsole();
		  },
		  bind(&PollingThread::isRunning, minimizeThread.get()));
		tray->AddMenuItem(U("Quit"), []() {
			WriteToConsole("QUIT");
		});
	}
}

// Perform all cleanup tasks when JSM is exiting
void CleanUp()
{
	if (tray)
	{
		tray->Hide();
	}
	HideConsole();
	jsl->DisconnectAndDisposeAll();
	handle_to_joyshock.clear(); // Destroy Vigem Gamepads
	ReleaseConsole();
}

int filterClampByte(int current, int next)
{
	return max(0, min(0xff, next));
}

float filterClamp01(float current, float next)
{
	return max(0.0f, min(1.0f, next));
}

float filterPositive(float current, float next)
{
	return max(0.0f, next);
}

float filterSign(float current, float next)
{
	return next == -1.0f || next == 0.0f || next == 1.0f ?
      next :
      current;
}

template<typename E, E invalid>
E filterInvalidValue(E current, E next)
{
	return next != invalid ? next : current;
}

float filterFloat(float current, float next)
{
	// Exclude Infinite, NaN and Subnormal
	return fpclassify(next) == FP_NORMAL || fpclassify(next) == FP_ZERO ? next : current;
}

FloatXY filterFloatPair(FloatXY current, FloatXY next)
{
	return (fpclassify(next.x()) == FP_NORMAL || fpclassify(next.x()) == FP_ZERO) &&
	    (fpclassify(next.y()) == FP_NORMAL || fpclassify(next.y()) == FP_ZERO) ?
      next :
      current;
}

float filterHoldPressDelay(float c, float next)
{
	if (next <= sim_press_window)
	{
		CERR << SettingID::HOLD_PRESS_TIME << " can only be set to a value higher than " << SettingID::SIM_PRESS_WINDOW << " which is " << sim_press_window << "ms." << endl;
		return c;
	}
	return next;
}

float filterTickTime(float c, float next)
{
	return max(1.f, min(100.f, round(next)));
}

Mapping filterMapping(Mapping current, Mapping next)
{
	if (next.hasViGEmBtn())
	{
		if (virtual_controller.get() == ControllerScheme::NONE)
		{
			COUT_WARN << "Before using this mapping, you need to set VIRTUAL_CONTROLLER." << endl;
			return current;
		}
		for (auto &js : handle_to_joyshock)
		{
			if (js.second->CheckVigemState() == false)
				return current;
		}
	}
	return next.isValid() ? next : current;
}

TriggerMode filterTriggerMode(TriggerMode current, TriggerMode next)
{
	// With SDL, I'm not sure if we have a reliable way to check if the device has analog or digital triggers. There's a function to query them, but I don't know if it works with the devices with custom readers (Switch, PS)
	/*	for (auto &js : handle_to_joyshock)
	{
		if (jsl->GetControllerType(js.first) != JS_TYPE_DS4 && next != TriggerMode::NO_FULL)
		{
			COUT_WARN << "WARNING: Dual Stage Triggers are only valid on analog triggers. Full pull bindings will be ignored on non DS4 controllers." << endl;
			break;
		}
	}
*/
	if (next == TriggerMode::X_LT || next == TriggerMode::X_RT)
	{
		if (virtual_controller.get() == ControllerScheme::NONE)
		{
			COUT_WARN << "Before using this trigger mode, you need to set VIRTUAL_CONTROLLER." << endl;
			return current;
		}
		for (auto &js : handle_to_joyshock)
		{
			if (js.second->CheckVigemState() == false)
				return current;
		}
	}
	return filterInvalidValue<TriggerMode, TriggerMode::INVALID>(current, next);
}

TriggerMode filterTouchpadDualStageMode(TriggerMode current, TriggerMode next)
{
	if (next == TriggerMode::X_LT || next == TriggerMode::X_RT || next == TriggerMode::INVALID)
	{
		COUT_WARN << SettingID::TOUCHPAD_DUAL_STAGE_MODE << " doesn't support vigem analog modes." << endl;
		return current;
	}
	return next;
}

StickMode filterStickMode(StickMode current, StickMode next)
{
	if (next == StickMode::LEFT_STICK || next == StickMode::RIGHT_STICK)
	{
		if (virtual_controller.get() == ControllerScheme::NONE)
		{
			COUT_WARN << "Before using this stick mode, you need to set VIRTUAL_CONTROLLER." << endl;
			return current;
		}
		for (auto &js : handle_to_joyshock)
		{
			if (js.second->CheckVigemState() == false)
				return current;
		}
	}
	return filterInvalidValue<StickMode, StickMode::INVALID>(current, next);
}

void UpdateRingModeFromStickMode(JSMVariable<RingMode> *stickRingMode, const StickMode &newValue)
{
	if (newValue == StickMode::INNER_RING)
	{
		*stickRingMode = RingMode::INNER;
	}
	else if (newValue == StickMode::OUTER_RING)
	{
		*stickRingMode = RingMode::OUTER;
	}
}

ControllerScheme UpdateVirtualController(ControllerScheme prevScheme, ControllerScheme nextScheme)
{
	bool success = true;
	for (auto &js : handle_to_joyshock)
	{
		if (!js.second->_context->_vigemController ||
		  js.second->_context->_vigemController->getType() != nextScheme)
		{
			if (nextScheme == ControllerScheme::NONE)
			{
				js.second->_context->_vigemController.reset(nullptr);
			}
			else
			{
				js.second->_context->_vigemController.reset(Gamepad::getNew(nextScheme, bind(&JoyShock::handleViGEmNotification, js.second.get(), placeholders::_1, placeholders::_2, placeholders::_3)));
				success &= js.second->_context->_vigemController && js.second->_context->_vigemController->isInitialized();
			}
		}
	}
	return success ? nextScheme : prevScheme;
}

void OnVirtualControllerChange(const ControllerScheme &newScheme)
{
	for (auto &js : handle_to_joyshock)
	{
		// Display an error message if any vigem is no good.
		if (!js.second->CheckVigemState())
		{
			break;
		}
	}
}

void RefreshAutoLoadHelp(JSMAssignment<Switch> *autoloadCmd)
{
	stringstream ss;
	ss << "AUTOLOAD will attempt load a file from the following folder when a window with a matching executable name enters focus:" << endl
	   << AUTOLOAD_FOLDER();
	autoloadCmd->SetHelp(ss.str());
}

void OnNewGridDimensions(CmdRegistry *registry, const FloatXY &newGridDims)
{
	_ASSERT_EXPR(registry, U("You forgot to bind the command registry properly!"));
	auto numberOfButtons = size_t(newGridDims.first * newGridDims.second) + 5; // Add Touch stick buttons

	if (numberOfButtons < touch_buttons.size())
	{
		// Remove all extra touch button commands
		bool successfulRemove = true;
		for (int id = FIRST_TOUCH_BUTTON + numberOfButtons; successfulRemove; ++id)
		{
			string name(magic_enum::enum_name(*magic_enum::enum_cast<ButtonID>(id)));
			successfulRemove = registry->Remove(name);
		}

		// For all joyshocks, remove extra touch DigitalButtons
		for (auto &js : handle_to_joyshock)
		{
			lock_guard guard(js.second->_context->callback_lock);
			js.second->updateGridSize(numberOfButtons);
		}

		// Remove extra touch button variables
		while (touch_buttons.size() > numberOfButtons)
			touch_buttons.pop_back();
	}
	else if (numberOfButtons > touch_buttons.size())
	{
		// Add new touch button variables and commands
		for (int id = FIRST_TOUCH_BUTTON + int(touch_buttons.size()); touch_buttons.size() < numberOfButtons; ++id)
		{
			JSMButton touchButton(*magic_enum::enum_cast<ButtonID>(id), Mapping::NO_MAPPING);
			touchButton.SetFilter(&filterMapping);
			touch_buttons.push_back(touchButton);
			registry->Add(new JSMAssignment<Mapping>(touch_buttons.back()));
		}

		// For all joyshocks, remove extra touch DigitalButtons
		for (auto &js : handle_to_joyshock)
		{
			lock_guard guard(js.second->_context->callback_lock);
			js.second->updateGridSize(numberOfButtons);
		}
	}
	// Else numbers are the same, possibly just reconfigured
}

class GyroSensAssignment : public JSMAssignment<FloatXY>
{
public:
	GyroSensAssignment(SettingID id, JSMSetting<FloatXY> &gyroSens)
	  : JSMAssignment(magic_enum::enum_name(id).data(), string(magic_enum::enum_name(gyroSens._id)), gyroSens)
	{
		// min and max gyro sens already have a listener
		gyroSens.RemoveOnChangeListener(_listenerId);
	}
};

class StickDeadzoneAssignment : public JSMAssignment<float>
{
public:
	StickDeadzoneAssignment(SettingID id, JSMSetting<float> &stickDeadzone)
	  : JSMAssignment(magic_enum::enum_name(id).data(), string(magic_enum::enum_name(stickDeadzone._id)), stickDeadzone)
	{
		// min and max gyro sens already have a listener
		stickDeadzone.RemoveOnChangeListener(_listenerId);
	}
};

class GyroButtonAssignment : public JSMAssignment<GyroSettings>
{
protected:
	const bool _always_off;
	const ButtonID _chordButton;

	virtual void DisplayCurrentValue() override
	{
		GyroSettings value(_var);
		if (_chordButton > ButtonID::NONE)
		{
			COUT << _chordButton << ',';
		}
		COUT << (value.always_off ? string("GYRO_ON") : string("GYRO_OFF")) << " = " << value << endl;
	}

	virtual GyroSettings ReadValue(stringstream &in) override
	{
		GyroSettings value;
		value.always_off = _always_off; // Added line from DefaultParser
		in >> value;
		return value;
	}

	virtual void DisplayNewValue(const GyroSettings &value) override
	{
		if (_chordButton > ButtonID::NONE)
		{
			COUT << _chordButton << ',';
		}
		COUT << (value.always_off ? string("GYRO_ON") : string("GYRO_OFF")) << " has been set to " << value << endl;
	}

public:
	GyroButtonAssignment(in_string name, in_string displayName, JSMVariable<GyroSettings> &setting, bool always_off, ButtonID chord = ButtonID::NONE)
	  : JSMAssignment(name, name, setting, true)
	  , _always_off(always_off)
	  , _chordButton(chord)
	{
	}

	GyroButtonAssignment(SettingID id, bool always_off)
	  : GyroButtonAssignment(magic_enum::enum_name(id).data(), magic_enum::enum_name(id).data(), gyro_settings, always_off)
	{
	}

	GyroButtonAssignment *SetListener()
	{
		_listenerId = _var.AddOnChangeListener(bind(&GyroButtonAssignment::DisplayNewValue, this, placeholders::_1));
		return this;
	}

	virtual unique_ptr<JSMCommand> GetModifiedCmd(char op, in_string chord) override
	{
		auto optBtn = magic_enum::enum_cast<ButtonID>(chord);
		auto settingVar = dynamic_cast<JSMSetting<GyroSettings> *>(&_var);
		if (optBtn > ButtonID::NONE && op == ',' && settingVar)
		{
			//Create Modeshift
			string name = chord + op + _displayName;
			unique_ptr<JSMCommand> chordAssignment((new GyroButtonAssignment(_name, name, *settingVar->AtChord(*optBtn), _always_off, *optBtn))->SetListener());
			chordAssignment->SetHelp(_help)->SetParser(bind(&GyroButtonAssignment::ModeshiftParser, *optBtn, settingVar, &_parse, placeholders::_1, placeholders::_2))->SetTaskOnDestruction(bind(&JSMSetting<GyroSettings>::ProcessModeshiftRemoval, settingVar, *optBtn));
			return chordAssignment;
		}
		return JSMCommand::GetModifiedCmd(op, chord);
	}

	virtual ~GyroButtonAssignment()
	{
	}
};

class HelpCmd : public JSMMacro
{
private:
	// HELP runs the macro for each argument given to it.
	string arg; // parsed argument

	bool Parser(in_string arguments)
	{
		stringstream ss(arguments);
		ss >> arg;
		do
		{ // Run at least once with an empty arg string if there's no argument.
			_macro(this, arguments);
			ss >> arg;
		} while (!ss.fail());
		arg.clear();
		return true;
	}

	// The run function is nothing like the delegate. See how I use the bind function
	// below to hard-code the pointer parameter and the instance pointer 'this'.
	bool RunHelp(CmdRegistry *registry)
	{
		if (arg.empty())
		{
			// Show all commands
			COUT << "Here's the list of all commands." << endl;
			vector<string> list;
			registry->GetCommandList(list);
			for (auto cmd : list)
			{
				COUT_INFO << "    " << cmd << endl;
			}
			COUT << "Enter HELP [cmd1] [cmd2] ... for details on specific commands." << endl;
		}
		else if (registry->hasCommand(arg))
		{
			auto help = registry->GetHelp(arg);
			if (!help.empty())
			{
				COUT << arg << " :" << endl
				     << "    " << help << endl;
			}
			else
			{
				COUT << arg << " is not a recognized command" << endl;
			}
		}
		else
		{
			// Show all commands that include ARG
			COUT << "\"" << arg << "\" is not a command, but the following are:" << endl;
			vector<string> list;
			registry->GetCommandList(list);
			for (auto cmd : list)
			{
				auto pos = cmd.find(arg);
				if (pos != string::npos)
					COUT_INFO << "    " << cmd << endl;
			}
			COUT << "Enter HELP [cmd1] [cmd2] ... for details on specific commands." << endl;
		}
		return true;
	}

public:
	HelpCmd(CmdRegistry &reg)
	  : JSMMacro("HELP")
	{
		// Bind allows me to use instance function by hardcoding the invisible "this" parameter, and the registry pointer
		SetMacro(bind(&HelpCmd::RunHelp, this, &reg));

		// The placeholder parameter says to pass 2nd parameter of call to _parse to the 1st argument of the call to HelpCmd::Parser.
		// The first parameter is the command pointer which is not required because Parser is an instance function rather than a static one.
		SetParser(bind(&HelpCmd::Parser, this, ::placeholders::_2));
	}
};

#ifdef _WIN32
int __stdcall wWinMain(HINSTANCE hInstance, HINSTANCE prevInstance, LPWSTR cmdLine, int cmdShow)
{
	auto trayIconData = hInstance;
	int argc = 0;
	wchar_t **argv = CommandLineToArgvW(cmdLine, &argc);
	unsigned long length = 256;
	wstring wmodule(length, '\0');
	auto handle = GetCurrentProcess();
	QueryFullProcessImageNameW(handle, 0, &wmodule[0], &length);
	string module(wmodule.begin(), wmodule.begin() + length);

#else
int main(int argc, char *argv[])
{
	static_cast<void>(argc);
	static_cast<void>(argv);
	void *trayIconData = nullptr;
#endif // _WIN32
	jsl.reset(JslWrapper::getNew());
	whitelister.reset(Whitelister::getNew(false));
	currentWorkingDir = GetCWD();

	touch_buttons.reserve(int(ButtonID::T25) - FIRST_TOUCH_BUTTON); // This makes sure the items will never get copied and cause crashes
	mappings.reserve(MAPPING_SIZE);
	for (int id = 0; id < MAPPING_SIZE; ++id)
	{
		JSMButton newButton(ButtonID(id), Mapping::NO_MAPPING);
		newButton.SetFilter(&filterMapping);
		mappings.push_back(newButton);
	}
	// console
	initConsole();
	COUT_BOLD << "Welcome to JoyShockMapper version " << version << '!' << endl;
	//if (whitelister) COUT << "JoyShockMapper was successfully whitelisted!" << endl;
	// Threads need to be created before listeners
	CmdRegistry commandRegistry;
	minimizeThread.reset(new PollingThread("Minimize thread", &MinimizePoll, nullptr, 1000, hide_minimized.get() == Switch::ON));          // Start by default
	autoLoadThread.reset(new PollingThread("AutoLoad thread", &AutoLoadPoll, &commandRegistry, 1000, autoloadSwitch.get() == Switch::ON)); // Start by default

	left_stick_mode.SetFilter(&filterStickMode)->AddOnChangeListener(bind(&UpdateRingModeFromStickMode, &left_ring_mode, ::placeholders::_1));
	right_stick_mode.SetFilter(&filterStickMode)->AddOnChangeListener(bind(&UpdateRingModeFromStickMode, &right_ring_mode, ::placeholders::_1));
	motion_stick_mode.SetFilter(&filterStickMode)->AddOnChangeListener(bind(&UpdateRingModeFromStickMode, &motion_ring_mode, ::placeholders::_1));
	left_ring_mode.SetFilter(&filterInvalidValue<RingMode, RingMode::INVALID>);
	right_ring_mode.SetFilter(&filterInvalidValue<RingMode, RingMode::INVALID>);
	motion_ring_mode.SetFilter(&filterInvalidValue<RingMode, RingMode::INVALID>);
	mouse_x_from_gyro.SetFilter(&filterInvalidValue<GyroAxisMask, GyroAxisMask::INVALID>);
	mouse_y_from_gyro.SetFilter(&filterInvalidValue<GyroAxisMask, GyroAxisMask::INVALID>);
	gyro_settings.SetFilter([](GyroSettings current, GyroSettings next) {
		return next.ignore_mode != GyroIgnoreMode::INVALID ? next : current;
	});
	joycon_gyro_mask.SetFilter(&filterInvalidValue<JoyconMask, JoyconMask::INVALID>);
	joycon_motion_mask.SetFilter(&filterInvalidValue<JoyconMask, JoyconMask::INVALID>);
	controller_orientation.SetFilter(&filterInvalidValue<ControllerOrientation, ControllerOrientation::INVALID>);
	zlMode.SetFilter(&filterTriggerMode);
	zrMode.SetFilter(&filterTriggerMode);
	flick_snap_mode.SetFilter(&filterInvalidValue<FlickSnapMode, FlickSnapMode::INVALID>);
	min_gyro_sens.SetFilter(&filterFloatPair);
	max_gyro_sens.SetFilter(&filterFloatPair);
	min_gyro_threshold.SetFilter(&filterFloat);
	max_gyro_threshold.SetFilter(&filterFloat);
	stick_power.SetFilter(&filterFloat);
	real_world_calibration.SetFilter(&filterFloat);
	in_game_sens.SetFilter(bind(&fmaxf, 0.0001f, ::placeholders::_2));
	trigger_threshold.SetFilter(&filterFloat);
	aim_x_sign.SetFilter(&filterInvalidValue<AxisMode, AxisMode::INVALID>);
	aim_y_sign.SetFilter(&filterInvalidValue<AxisMode, AxisMode::INVALID>);
	gyro_x_sign.SetFilter(&filterInvalidValue<AxisMode, AxisMode::INVALID>);
	gyro_y_sign.SetFilter(&filterInvalidValue<AxisMode, AxisMode::INVALID>);
	flick_time.SetFilter(bind(&fmaxf, 0.0001f, ::placeholders::_2));
	flick_time_exponent.SetFilter(&filterFloat);
	gyro_smooth_time.SetFilter(bind(&fmaxf, 0.0001f, ::placeholders::_2));
	gyro_smooth_threshold.SetFilter(&filterPositive);
	gyro_cutoff_speed.SetFilter(&filterPositive);
	gyro_cutoff_recovery.SetFilter(&filterPositive);
	stick_acceleration_rate.SetFilter(&filterPositive);
	stick_acceleration_cap.SetFilter(bind(&fmaxf, 1.0f, ::placeholders::_2));
	left_stick_deadzone_inner.SetFilter(&filterClamp01);
	left_stick_deadzone_outer.SetFilter(&filterClamp01);
	flick_deadzone_angle.SetFilter(&filterPositive);
	right_stick_deadzone_inner.SetFilter(&filterClamp01);
	right_stick_deadzone_outer.SetFilter(&filterClamp01);
	motion_deadzone_inner.SetFilter(&filterPositive);
	motion_deadzone_outer.SetFilter(&filterPositive);
	lean_threshold.SetFilter(&filterPositive);
	mouse_ring_radius.SetFilter([](float c, float n) { return n <= screen_resolution_y ? floorf(n) : c; });
	trackball_decay.SetFilter(&filterPositive);
	screen_resolution_x.SetFilter(&filterPositive);
	screen_resolution_y.SetFilter(&filterPositive);
	// no filtering for rotate_smooth_override
	flick_snap_strength.SetFilter(&filterClamp01);
	trigger_skip_delay.SetFilter(&filterPositive);
	turbo_period.SetFilter(&filterPositive);
	sim_press_window.SetFilter(&filterPositive);
	dbl_press_window.SetFilter(&filterPositive);
	hold_press_time.SetFilter(&filterHoldPressDelay);
	tick_time.SetFilter(&filterTickTime);
	currentWorkingDir.SetFilter([](PathString current, PathString next) {
		return SetCWD(string(next)) ? next : current;
	});
	autoloadSwitch.SetFilter(&filterInvalidValue<Switch, Switch::INVALID>)->AddOnChangeListener(bind(&UpdateThread, autoLoadThread.get(), placeholders::_1));
	hide_minimized.SetFilter(&filterInvalidValue<Switch, Switch::INVALID>)->AddOnChangeListener(bind(&UpdateThread, minimizeThread.get(), placeholders::_1));
	grid_size.SetFilter([](auto current, auto next) {
		float floorX = floorf(next.x());
		float floorY = floorf(next.y());
		return floorX * floorY >= 1 && floorX * floorY <= 25 ? FloatXY{ floorX, floorY } : current;
	});
	grid_size.AddOnChangeListener(bind(&OnNewGridDimensions, &commandRegistry, placeholders::_1), true); // Call the listener now
	touchpad_mode.SetFilter(&filterInvalidValue<TouchpadMode, TouchpadMode::INVALID>);
	touch_stick_mode.SetFilter(&filterInvalidValue<StickMode, StickMode::INVALID>)->AddOnChangeListener(bind(&UpdateRingModeFromStickMode, &touch_ring_mode, ::placeholders::_1));
	touch_deadzone_inner.SetFilter(&filterPositive);
	touch_ring_mode.SetFilter(&filterInvalidValue<RingMode, RingMode::INVALID>);
	touchpad_sens.SetFilter(filterFloatPair);
	touch_stick_radius.SetFilter([](auto current, auto next) {
		return filterPositive(current, floorf(next));
	});
	virtual_controller.SetFilter(&UpdateVirtualController)->AddOnChangeListener(&OnVirtualControllerChange);
	rumble_enable.SetFilter(&filterInvalidValue<Switch, Switch::INVALID>);
	adaptive_trigger.SetFilter(&filterInvalidValue<Switch, Switch::INVALID>);
	scroll_sens.SetFilter(&filterFloatPair);
	touch_ds_mode.SetFilter(&filterTouchpadDualStageMode);
	right_trigger_offset.SetFilter(&filterClampByte);
	left_trigger_offset.SetFilter(&filterClampByte);
	right_trigger_range.SetFilter(&filterClampByte);
	left_trigger_range.SetFilter(&filterClampByte);

	// light_bar needs no filter or listener. The callback polls and updates the color.
	for (int i = argc - 1; i >= 0; --i)
	{
#if _WIN32
		string arg(&argv[i][0], &argv[i][wcslen(argv[i])]);
#else
		string arg = string(argv[0]);
#endif
		if (filesystem::is_directory(filesystem::status(arg)) &&
		  (currentWorkingDir = arg).compare(arg) == 0)
		{
			break;
		}
	}

	if (autoLoadThread && autoLoadThread->isRunning())
	{
		COUT << "AUTOLOAD is available. Files in ";
		COUT_INFO << AUTOLOAD_FOLDER();
		COUT << " folder will get loaded automatically when a matching application is in focus." << endl;
	}
	else
	{
		CERR << "AutoLoad is unavailable" << endl;
	}

	assert(MAPPING_SIZE == buttonHelpMap.size() && "Please update the button help map in ButtonHelp.cpp");
	for (auto &mapping : mappings) // Add all button mappings as commands
	{
		commandRegistry.Add((new JSMAssignment<Mapping>(mapping.getName(), mapping))->SetHelp(buttonHelpMap.at(mapping._id)));
	}
	// SL and SR are shorthand for two different mappings
	commandRegistry.Add(new JSMAssignment<Mapping>("SL", "", mappings[(int)ButtonID::LSL], true));
	commandRegistry.Add(new JSMAssignment<Mapping>("SL", "", mappings[(int)ButtonID::RSL], true));
	commandRegistry.Add(new JSMAssignment<Mapping>("SR", "", mappings[(int)ButtonID::LSR], true));
	commandRegistry.Add(new JSMAssignment<Mapping>("SR", "", mappings[(int)ButtonID::RSR], true));

	commandRegistry.Add((new JSMAssignment<FloatXY>(min_gyro_sens))
	                      ->SetHelp("Minimum gyro sensitivity when turning controller at or below MIN_GYRO_THRESHOLD.\nYou can assign a second value as a different vertical sensitivity."));
	commandRegistry.Add((new JSMAssignment<FloatXY>(max_gyro_sens))
	                      ->SetHelp("Maximum gyro sensitivity when turning controller at or above MAX_GYRO_THRESHOLD.\nYou can assign a second value as a different vertical sensitivity."));
	commandRegistry.Add((new JSMAssignment<float>(min_gyro_threshold))
	                      ->SetHelp("Degrees per second at and below which to apply minimum gyro sensitivity."));
	commandRegistry.Add((new JSMAssignment<float>(max_gyro_threshold))
	                      ->SetHelp("Degrees per second at and above which to apply maximum gyro sensitivity."));
	commandRegistry.Add((new JSMAssignment<float>(stick_power))
	                      ->SetHelp("Power curve for stick input when in AIM mode. 1 for linear, 0 for no curve (full strength once out of deadzone). Higher numbers make more of the stick's range appear like a very slight tilt."));
	commandRegistry.Add((new JSMAssignment<FloatXY>(stick_sens))
	                      ->SetHelp("Stick sensitivity when using classic AIM mode."));
	commandRegistry.Add((new JSMAssignment<float>(real_world_calibration))
	                      ->SetHelp("Calibration value mapping mouse values to in game degrees. This value is used for FLICK mode, and to make GYRO and stick AIM sensitivities use real world values."));
	commandRegistry.Add((new JSMAssignment<float>(in_game_sens))
	                      ->SetHelp("Set this value to the sensitivity you use in game. It is used by stick FLICK and AIM modes as well as GYRO aiming."));
	commandRegistry.Add((new JSMAssignment<float>(trigger_threshold))
	                      ->SetHelp("Set this to a value between 0 and 1. This is the threshold at which a soft press binding is triggered. Or set the value to -1 to use hair trigger mode"));
	commandRegistry.Add((new JSMMacro("RESET_MAPPINGS"))->SetMacro(bind(&do_RESET_MAPPINGS, &commandRegistry))->SetHelp("Delete all custom bindings and reset to default.\nHOME and CAPTURE are set to CALIBRATE on both tap and hold by default."));
	commandRegistry.Add((new JSMMacro("NO_GYRO_BUTTON"))->SetMacro(bind(&do_NO_GYRO_BUTTON))->SetHelp("Enable gyro at all times, without any GYRO_OFF binding."));
	commandRegistry.Add((new JSMAssignment<StickMode>(left_stick_mode))
	                      ->SetHelp("Set a mouse mode for the left stick. Valid values are the following:\nNO_MOUSE, AIM, FLICK, FLICK_ONLY, ROTATE_ONLY, MOUSE_RING, MOUSE_AREA, OUTER_RING, INNER_RING, SCROLL_WHEEL, LEFT_STICK, RIGHT_STICK"));
	commandRegistry.Add((new JSMAssignment<StickMode>(right_stick_mode))
	                      ->SetHelp("Set a mouse mode for the right stick. Valid values are the following:\nNO_MOUSE, AIM, FLICK, FLICK_ONLY, ROTATE_ONLY, MOUSE_RING, MOUSE_AREA, OUTER_RING, INNER_RING LEFT_STICK, RIGHT_STICK"));
	commandRegistry.Add((new JSMAssignment<StickMode>(motion_stick_mode))
	                      ->SetHelp("Set a mouse mode for the motion-stick -- the whole controller is treated as a stick. Valid values are the following:\nNO_MOUSE, AIM, FLICK, FLICK_ONLY, ROTATE_ONLY, MOUSE_RING, MOUSE_AREA, OUTER_RING, INNER_RING LEFT_STICK, RIGHT_STICK"));
	commandRegistry.Add((new GyroButtonAssignment(SettingID::GYRO_OFF, false))
	                      ->SetHelp("Assign a controller button to disable the gyro when pressed."));
	commandRegistry.Add((new GyroButtonAssignment(SettingID::GYRO_ON, true))->SetListener() // Set only one listener
	                      ->SetHelp("Assign a controller button to enable the gyro when pressed."));
	commandRegistry.Add((new JSMAssignment<AxisMode>(aim_x_sign))
	                      ->SetHelp("When in AIM mode, set stick X axis inversion. Valid values are the following:\nSTANDARD or 1, and INVERTED or -1"));
	commandRegistry.Add((new JSMAssignment<AxisMode>(aim_y_sign))
	                      ->SetHelp("When in AIM mode, set stick Y axis inversion. Valid values are the following:\nSTANDARD or 1, and INVERTED or -1"));
	commandRegistry.Add((new JSMAssignment<AxisMode>(gyro_x_sign))
	                      ->SetHelp("Set gyro X axis inversion. Valid values are the following:\nSTANDARD or 1, and INVERTED or -1"));
	commandRegistry.Add((new JSMAssignment<AxisMode>(gyro_y_sign))
	                      ->SetHelp("Set gyro Y axis inversion. Valid values are the following:\nSTANDARD or 1, and INVERTED or -1"));
	commandRegistry.Add((new JSMMacro("RECONNECT_CONTROLLERS"))->SetMacro(bind(&do_RECONNECT_CONTROLLERS, placeholders::_2))->SetHelp("Look for newly connected controllers. Specify MERGE (default) or SPLIT whether you want to consider joycons as a single or separate controllers."));
	commandRegistry.Add((new JSMMacro("COUNTER_OS_MOUSE_SPEED"))->SetMacro(bind(do_COUNTER_OS_MOUSE_SPEED))->SetHelp("JoyShockMapper will load the user's OS mouse sensitivity value to consider it in its calculations."));
	commandRegistry.Add((new JSMMacro("IGNORE_OS_MOUSE_SPEED"))->SetMacro(bind(do_IGNORE_OS_MOUSE_SPEED))->SetHelp("Disable JoyShockMapper's consideration of the the user's OS mouse sensitivity value."));
	commandRegistry.Add((new JSMAssignment<JoyconMask>(joycon_gyro_mask))
	                      ->SetHelp("When using two Joycons, select which one will be used for gyro. Valid values are the following:\nUSE_BOTH, IGNORE_LEFT, IGNORE_RIGHT, IGNORE_BOTH"));
	commandRegistry.Add((new JSMAssignment<JoyconMask>(joycon_motion_mask))
	                      ->SetHelp("When using two Joycons, select which one will be used for non-gyro motion. Valid values are the following:\nUSE_BOTH, IGNORE_LEFT, IGNORE_RIGHT, IGNORE_BOTH"));
	commandRegistry.Add((new GyroSensAssignment(SettingID::GYRO_SENS, min_gyro_sens))
	                      ->SetHelp("Sets a gyro sensitivity to use. This sets both MIN_GYRO_SENS and MAX_GYRO_SENS to the same values. You can assign a second value as a different vertical sensitivity."));
	commandRegistry.Add((new GyroSensAssignment(SettingID::GYRO_SENS, max_gyro_sens))->SetHelp(""));
	commandRegistry.Add((new JSMAssignment<float>(flick_time))
	                      ->SetHelp("Sets how long a flick takes in seconds. This value is used by stick FLICK mode."));
	commandRegistry.Add((new JSMAssignment<float>(flick_time_exponent))
	                      ->SetHelp("Applies a delta exponent to flick_time, effectively making flick speed depend on the flick angle: use 0 for no effect and 1 for linear. This value is used by stick FLICK mode."));
	commandRegistry.Add((new JSMAssignment<float>(gyro_smooth_threshold))
	                      ->SetHelp("When the controller's angular velocity is below this threshold (in degrees per second), smoothing will be applied."));
	commandRegistry.Add((new JSMAssignment<float>(gyro_smooth_time))
	                      ->SetHelp("This length of the smoothing window in seconds. Smoothing is only applied below the GYRO_SMOOTH_THRESHOLD, with a smooth transition to full smoothing."));
	commandRegistry.Add((new JSMAssignment<float>(gyro_cutoff_speed))
	                      ->SetHelp("Gyro deadzone. Gyro input will be ignored when below this angular velocity (in degrees per second). This should be a last-resort stability option."));
	commandRegistry.Add((new JSMAssignment<float>(gyro_cutoff_recovery))
	                      ->SetHelp("Below this threshold (in degrees per second), gyro sensitivity is pushed down towards zero. This can tighten and steady aim without a deadzone."));
	commandRegistry.Add((new JSMAssignment<float>(stick_acceleration_rate))
	                      ->SetHelp("When in AIM mode and the stick is fully tilted, stick sensitivity increases over time. This is a multiplier starting at 1x and increasing this by this value per second."));
	commandRegistry.Add((new JSMAssignment<float>(stick_acceleration_cap))
	                      ->SetHelp("When in AIM mode and the stick is fully tilted, stick sensitivity increases over time. This value is the maximum sensitivity multiplier."));
	commandRegistry.Add((new JSMAssignment<float>(left_stick_deadzone_inner))
	                      ->SetHelp("Defines a radius of the left stick within which all values will be ignored. This value can only be between 0 and 1 but it should be small. Stick input out of this radius will be adjusted."));
	commandRegistry.Add((new JSMAssignment<float>(left_stick_deadzone_outer))
	                      ->SetHelp("Defines a distance from the left stick's outer edge for which the stick will be considered fully tilted. This value can only be between 0 and 1 but it should be small. Stick input out of this deadzone will be adjusted."));
	commandRegistry.Add((new JSMAssignment<float>(flick_deadzone_angle))
	                      ->SetHelp("Defines a minimum angle (in degrees) for the flick to be considered a flick. Helps ignore unintentional turns when tilting the stick straight forward."));
	commandRegistry.Add((new JSMAssignment<float>(right_stick_deadzone_inner))
	                      ->SetHelp("Defines a radius of the right stick within which all values will be ignored. This value can only be between 0 and 1 but it should be small. Stick input out of this radius will be adjusted."));
	commandRegistry.Add((new JSMAssignment<float>(right_stick_deadzone_outer))
	                      ->SetHelp("Defines a distance from the right stick's outer edge for which the stick will be considered fully tilted. This value can only be between 0 and 1 but it should be small. Stick input out of this deadzone will be adjusted."));
	commandRegistry.Add((new StickDeadzoneAssignment(SettingID::STICK_DEADZONE_INNER, left_stick_deadzone_inner))
	                      ->SetHelp("Defines a radius of the both left and right sticks within which all values will be ignored. This value can only be between 0 and 1 but it should be small. Stick input out of this radius will be adjusted."));
	commandRegistry.Add((new StickDeadzoneAssignment(SettingID::STICK_DEADZONE_INNER, right_stick_deadzone_inner))->SetHelp(""));
	commandRegistry.Add((new StickDeadzoneAssignment(SettingID::STICK_DEADZONE_OUTER, left_stick_deadzone_outer))
	                      ->SetHelp("Defines a distance from both sticks' outer edge for which the stick will be considered fully tilted. This value can only be between 0 and 1 but it should be small. Stick input out of this deadzone will be adjusted."));
	commandRegistry.Add((new StickDeadzoneAssignment(SettingID::STICK_DEADZONE_OUTER, right_stick_deadzone_outer))->SetHelp(""));
	commandRegistry.Add((new JSMAssignment<float>(motion_deadzone_inner))
	                      ->SetHelp("Defines a radius of the motion-stick within which all values will be ignored. This value can only be between 0 and 1 but it should be small. Stick input out of this radius will be adjusted."));
	commandRegistry.Add((new JSMAssignment<float>(motion_deadzone_outer))
	                      ->SetHelp("Defines a distance from the motion-stick's outer edge for which the stick will be considered fully tilted. Stick input out of this deadzone will be adjusted."));
	commandRegistry.Add((new JSMAssignment<float>(lean_threshold))
	                      ->SetHelp("How far the controller must be leaned left or right to trigger a LEAN_LEFT or LEAN_RIGHT binding."));
	commandRegistry.Add((new JSMMacro("CALCULATE_REAL_WORLD_CALIBRATION"))->SetMacro(bind(&do_CALCULATE_REAL_WORLD_CALIBRATION, placeholders::_2))->SetHelp("Get JoyShockMapper to recommend you a REAL_WORLD_CALIBRATION value after performing the calibration sequence. Visit GyroWiki for details:\nhttp://gyrowiki.jibbsmart.com/blog:joyshockmapper-guide#calibrating"));
	commandRegistry.Add((new JSMMacro("SLEEP"))->SetMacro(bind(&do_SLEEP, placeholders::_2))->SetHelp("Sleep for the given number of seconds, or one second if no number is given. Can't sleep more than 10 seconds per command."));
	commandRegistry.Add((new JSMMacro("FINISH_GYRO_CALIBRATION"))->SetMacro(bind(&do_FINISH_GYRO_CALIBRATION))->SetHelp("Finish calibrating the gyro in all controllers."));
	commandRegistry.Add((new JSMMacro("RESTART_GYRO_CALIBRATION"))->SetMacro(bind(&do_RESTART_GYRO_CALIBRATION))->SetHelp("Start calibrating the gyro in all controllers."));
	commandRegistry.Add((new JSMMacro("SET_MOTION_STICK_NEUTRAL"))->SetMacro(bind(&do_SET_MOTION_STICK_NEUTRAL))->SetHelp("Set the neutral orientation for motion stick to whatever the orientation of the controller is."));
	commandRegistry.Add((new JSMAssignment<GyroAxisMask>(mouse_x_from_gyro))
	                      ->SetHelp("Pick a gyro axis to operate on the mouse's X axis. Valid values are the following: X, Y and Z."));
	commandRegistry.Add((new JSMAssignment<GyroAxisMask>(mouse_y_from_gyro))
	                      ->SetHelp("Pick a gyro axis to operate on the mouse's Y axis. Valid values are the following: X, Y and Z."));
	commandRegistry.Add((new JSMAssignment<ControllerOrientation>(controller_orientation))
	                      ->SetHelp("Let the stick modes account for how you're holding the controller:\nFORWARD, LEFT, RIGHT, BACKWARD"));
	commandRegistry.Add((new JSMAssignment<TriggerMode>(zlMode))
	                      ->SetHelp("Controllers with a right analog trigger can use one of the following dual stage trigger modes:\nNO_FULL, NO_SKIP, MAY_SKIP, MUST_SKIP, MAY_SKIP_R, MUST_SKIP_R, NO_SKIP_EXCLUSIVE, X_LT, X_RT, PS_L2, PS_R2"));
	commandRegistry.Add((new JSMAssignment<TriggerMode>(zrMode))
	                      ->SetHelp("Controllers with a left analog trigger can use one of the following dual stage trigger modes:\nNO_FULL, NO_SKIP, MAY_SKIP, MUST_SKIP, MAY_SKIP_R, MUST_SKIP_R, NO_SKIP_EXCLUSIVE, X_LT, X_RT, PS_L2, PS_R2"));
	auto *autoloadCmd = new JSMAssignment<Switch>("AUTOLOAD", autoloadSwitch);
	commandRegistry.Add(autoloadCmd);
	currentWorkingDir.AddOnChangeListener(bind(&RefreshAutoLoadHelp, autoloadCmd), true);
	commandRegistry.Add((new JSMMacro("README"))->SetMacro(bind(&do_README))->SetHelp("Open the latest JoyShockMapper README in your browser."));
	commandRegistry.Add((new JSMMacro("WHITELIST_SHOW"))->SetMacro(bind(&do_WHITELIST_SHOW))->SetHelp("Open HIDCerberus configuration page in your browser."));
	commandRegistry.Add((new JSMMacro("WHITELIST_ADD"))->SetMacro(bind(&do_WHITELIST_ADD))->SetHelp("Add JoyShockMapper to HIDGuardian whitelisted applications."));
	commandRegistry.Add((new JSMMacro("WHITELIST_REMOVE"))->SetMacro(bind(&do_WHITELIST_REMOVE))->SetHelp("Remove JoyShockMapper from HIDGuardian whitelisted applications."));
	commandRegistry.Add((new JSMAssignment<RingMode>(left_ring_mode))
	                      ->SetHelp("Pick a ring where to apply the LEFT_RING binding. Valid values are the following: INNER and OUTER."));
	commandRegistry.Add((new JSMAssignment<RingMode>(right_ring_mode))
	                      ->SetHelp("Pick a ring where to apply the RIGHT_RING binding. Valid values are the following: INNER and OUTER."));
	commandRegistry.Add((new JSMAssignment<RingMode>(motion_ring_mode))
	                      ->SetHelp("Pick a ring where to apply the MOTION_RING binding. Valid values are the following: INNER and OUTER."));
	commandRegistry.Add((new JSMAssignment<float>(mouse_ring_radius))
	                      ->SetHelp("Pick a radius on which the cursor will be allowed to move. This value is used for stick mode MOUSE_RING and MOUSE_AREA."));
	commandRegistry.Add((new JSMAssignment<float>(trackball_decay))
	                      ->SetHelp("Choose the rate at which trackball gyro slows down. 0 means no decay, 1 means it'll halve each second, 2 to halve each 1/2 seconds, etc."));
	commandRegistry.Add((new JSMAssignment<float>(screen_resolution_x))
	                      ->SetHelp("Indicate your monitor's horizontal resolution when using the stick mode MOUSE_RING."));
	commandRegistry.Add((new JSMAssignment<float>(screen_resolution_y))
	                      ->SetHelp("Indicate your monitor's vertical resolution when using the stick mode MOUSE_RING."));
	commandRegistry.Add((new JSMAssignment<float>(rotate_smooth_override))
	                      ->SetHelp("Some smoothing is applied to flick stick rotations to account for the controller's stick resolution. This value overrides the smoothing threshold."));
	commandRegistry.Add((new JSMAssignment<FlickSnapMode>(flick_snap_mode))
	                      ->SetHelp("Snap flicks to cardinal directions. Valid values are the following: NONE or 0, FOUR or 4 and EIGHT or 8."));
	commandRegistry.Add((new JSMAssignment<float>(flick_snap_strength))
	                      ->SetHelp("If FLICK_SNAP_MODE is set to something other than NONE, this sets the degree of snapping -- 0 for none, 1 for full snapping to the nearest direction, and values in between will bias you towards the nearest direction instead of snapping."));
	commandRegistry.Add((new JSMAssignment<float>(trigger_skip_delay))
	                      ->SetHelp("Sets the amount of time in milliseconds within which the user needs to reach the full press to skip the soft pull binding of the trigger."));
	commandRegistry.Add((new JSMAssignment<float>(turbo_period))
	                      ->SetHelp("Sets the time in milliseconds to wait between each turbo activation."));
	commandRegistry.Add((new JSMAssignment<float>(hold_press_time))
	                      ->SetHelp("Sets the amount of time in milliseconds to hold a button before the hold press is enabled. Releasing the button before this time will trigger the tap press. Turbo press only starts after this delay."));
	commandRegistry.Add((new JSMAssignment<float>("SIM_PRESS_WINDOW", sim_press_window))
	                      ->SetHelp("Sets the amount of time in milliseconds within which both buttons of a simultaneous press needs to be pressed before enabling the sim press mappings. This setting does not support modeshift."));
	commandRegistry.Add((new JSMAssignment<float>("DBL_PRESS_WINDOW", dbl_press_window))
	                      ->SetHelp("Sets the amount of time in milliseconds within which the user needs to press a button twice before enabling the double press mappings. This setting does not support modeshift."));
	commandRegistry.Add((new JSMAssignment<float>("TICK_TIME", tick_time))
	                      ->SetHelp("Sets the time in milliseconds that JoyShockMaper waits before reading from each controller again."));
	commandRegistry.Add((new JSMAssignment<PathString>("JSM_DIRECTORY", currentWorkingDir))
	                      ->SetHelp("If AUTOLOAD doesn't work properly, set this value to the path to the directory holding the JoyShockMapper.exe file. Make sure a folder named \"AutoLoad\" exists there."));
	commandRegistry.Add((new JSMAssignment<TouchpadMode>("TOUCHPAD_MODE", touchpad_mode))
	                      ->SetHelp("Assign a mode to the touchpad. Valid values are GRID_AND_STICK or MOUSE."));
	commandRegistry.Add((new JSMAssignment<FloatXY>("GRID_SIZE", grid_size))
	                      ->SetHelp("When TOUCHPAD_MODE is set to GRID_AND_STICK, this variable sets the number of rows and columns in the grid. The product of the two numbers need to be between 1 and 25."));
	commandRegistry.Add((new JSMAssignment<StickMode>(touch_stick_mode))
	                      ->SetHelp("Set a mouse mode for the touchpad stick. Valid values are the following:\nNO_MOUSE, AIM, FLICK, FLICK_ONLY, ROTATE_ONLY, MOUSE_RING, MOUSE_AREA, OUTER_RING, INNER_RING"));
	commandRegistry.Add((new JSMAssignment<float>(touch_stick_radius))
	                      ->SetHelp("Set the radius of the touchpad stick. The center of the stick is always the first point of contact. Use a very large value (ex: 800) to use it as swipe gesture."));
	commandRegistry.Add((new JSMAssignment<Color>(light_bar))
	                      ->SetHelp("Changes the color bar of the DS4. Either enter as a hex code (xRRGGBB), as three decimal values between 0 and 255 (RRR GGG BBB), or as a common color name in all caps and underscores."));
	commandRegistry.Add((new JSMAssignment<FloatXY>(touchpad_sens))
	                      ->SetHelp("Changes the sensitivity of the touchpad when set as a mouse. Enter a second value for a different vertical sensitivity."));
	commandRegistry.Add(new HelpCmd(commandRegistry));
	commandRegistry.Add((new JSMAssignment<ControllerScheme>(magic_enum::enum_name(SettingID::VIRTUAL_CONTROLLER).data(), virtual_controller))
	                      ->SetHelp("Sets the vigem virtual controller type. Can be NONE (default), XBOX (360) or DS4 (PS4)."));
	commandRegistry.Add((new JSMAssignment<Switch>("HIDE_MINIMIZED", hide_minimized))
	                      ->SetHelp("JSM will be hidden in the notification area when minimized if this setting is ON. Otherwise it stays in the taskbar."));
	commandRegistry.Add((new JSMAssignment<FloatXY>(scroll_sens))
	                      ->SetHelp("Scrolling sensitivity for sticks."));
	commandRegistry.Add((new JSMAssignment<Switch>(rumble_enable))
	                      ->SetHelp("Disable the rumbling feature from vigem. Valid values are ON and OFF."));
	commandRegistry.Add((new JSMAssignment<Switch>(adaptive_trigger))
						  ->SetHelp("Control the adaptive trigger feature of the DualSense. Valid values are ON and OFF."));
	commandRegistry.Add((new JSMAssignment<TriggerMode>(touch_ds_mode))
	                      ->SetHelp("Dual stage mode for the touchpad TOUCH and CAPTURE (i.e. click) bindings."));
	commandRegistry.Add((new JSMMacro("CLEAR"))->SetMacro(bind(&ClearConsole))->SetHelp("Removes all text in the console screen"));
	commandRegistry.Add((new JSMMacro("CALIBRATE_TRIGGERS"))->SetMacro([](JSMMacro*, in_string) 
		{
			triggersCalibrating = 1;
			return true;
		})->SetHelp("Starts the trigger calibration procedure for the dualsense triggers."));
	commandRegistry.Add((new JSMAssignment<int>(magic_enum::enum_name(SettingID::LEFT_TRIGGER_OFFSET).data(), left_trigger_offset)));
	commandRegistry.Add((new JSMAssignment<int>(magic_enum::enum_name(SettingID::RIGHT_TRIGGER_OFFSET).data(), right_trigger_offset)));
	commandRegistry.Add((new JSMAssignment<int>(magic_enum::enum_name(SettingID::LEFT_TRIGGER_RANGE).data(), left_trigger_range)));
	commandRegistry.Add((new JSMAssignment<int>(magic_enum::enum_name(SettingID::RIGHT_TRIGGER_RANGE).data(), right_trigger_range)));

	bool quit = false;
	commandRegistry.Add((new JSMMacro("QUIT"))
	                      ->SetMacro([&quit](JSMMacro *, in_string) {
		                      quit = true;
		                      WriteToConsole(""); // If ran from autoload thread, you need to send RETURN to resume the main loop and check the quit flag.
		                      return true;
	                      })
	                      ->SetHelp("Close the application."));

	Mapping::_isCommandValid = bind(&CmdRegistry::isCommandValid, &commandRegistry, placeholders::_1);

	connectDevices();
	jsl->SetCallback(&joyShockPollCallback);
	jsl->SetTouchCallback(&TouchCallback);
	tray.reset(TrayIcon::getNew(trayIconData, &beforeShowTrayMenu));
	if (tray)
	{
		tray->Show();
	}

	do_RESET_MAPPINGS(&commandRegistry); // OnReset.txt
	if (commandRegistry.loadConfigFile("OnStartup.txt"))
	{
		COUT << "Finished executing startup file." << endl;
	}
	else
	{
		COUT << "There is no ";
		COUT_INFO << "OnStartup.txt";
		COUT << " file to load." << endl;
	}

	for (int i = 0; i < argc; ++i)
	{
#if _WIN32
		string arg(&argv[i][0], &argv[i][wcslen(argv[i])]);
#else
		string arg = string(argv[0]);
#endif
		if (filesystem::is_regular_file(filesystem::status(arg)) &&
		  arg != module)
		{
			commandRegistry.loadConfigFile(arg);
			autoloadSwitch = Switch::OFF;
		}
	}

	// The main loop is simple and reads like pseudocode
	string enteredCommand;
	while (!quit)
	{
		getline(cin, enteredCommand);
		loading_lock.lock();
		commandRegistry.processLine(enteredCommand);
		loading_lock.unlock();
	}
	LocalFree(argv);
	CleanUp();
	return 0;
}
