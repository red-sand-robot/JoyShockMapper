#pragma once
#include "JoyShockLibrary.h"
#include "DigitalButton.h"

#include <deque>
#include <vector>
#include <windows.h>


#define MAPPING_ERROR -2 // Represents an error in user input
#define MAPPING_NONE -1 // Represents no button when explicitely stated by the user. Not to be confused with NO_HOLD_MAPPED which is no action bound.
#define MAPPING_UP 0
#define MAPPING_DOWN 1
#define MAPPING_LEFT 2
#define MAPPING_RIGHT 3
#define MAPPING_L 4
#define MAPPING_ZL 5
#define MAPPING_MINUS 6
#define MAPPING_CAPTURE 7
#define MAPPING_E 8
#define MAPPING_S 9
#define MAPPING_N 10
#define MAPPING_W 11
#define MAPPING_R 12
#define MAPPING_ZR 13
#define MAPPING_PLUS 14
#define MAPPING_HOME 15
#define MAPPING_SL 16
#define MAPPING_SR 17
#define MAPPING_L3 18
#define MAPPING_R3 19
#define MAPPING_LUP 20
#define MAPPING_LDOWN 21
#define MAPPING_LLEFT 22
#define MAPPING_LRIGHT 23
#define MAPPING_RUP 24
#define MAPPING_RDOWN 25
#define MAPPING_RLEFT 26
#define MAPPING_RRIGHT 27
#define MAPPING_ZLF 28 // FIRST
// insert more analog triggers here
#define MAPPING_ZRF 29 // LAST
#define MAPPING_SIZE 30

#define FIRST_ANALOG_TRIGGER MAPPING_ZLF
#define LAST_ANALOG_TRIGGER MAPPING_ZRF

#define MIN_GYRO_SENS 31
#define MAX_GYRO_SENS 32
#define MIN_GYRO_THRESHOLD 33
#define MAX_GYRO_THRESHOLD 34
#define STICK_POWER 35
#define STICK_SENS 36
#define REAL_WORLD_CALIBRATION 37
#define IN_GAME_SENS 38
#define TRIGGER_THRESHOLD 39
#define RESET_MAPPINGS 40
#define NO_GYRO_BUTTON 41
#define LEFT_STICK_MODE 42
#define RIGHT_STICK_MODE 43
#define GYRO_OFF 44
#define GYRO_ON 45
#define STICK_AXIS_X 46
#define STICK_AXIS_Y 47
#define GYRO_AXIS_X 48
#define GYRO_AXIS_Y 49
#define RECONNECT_CONTROLLERS 50
#define COUNTER_OS_MOUSE_SPEED 51
#define IGNORE_OS_MOUSE_SPEED 52
#define JOYCON_GYRO_MASK 53
#define GYRO_SENS 54
#define FLICK_TIME 55
#define GYRO_SMOOTH_THRESHOLD 56
#define GYRO_SMOOTH_TIME 57
#define GYRO_CUTOFF_SPEED 58
#define GYRO_CUTOFF_RECOVERY 59
#define STICK_ACCELERATION_RATE 60
#define STICK_ACCELERATION_CAP 61
#define STICK_DEADZONE_INNER 62
#define STICK_DEADZONE_OUTER 63
#define CALCULATE_REAL_WORLD_CALIBRATION 64
#define FINISH_GYRO_CALIBRATION 65
#define RESTART_GYRO_CALIBRATION 66
#define MOUSE_X_FROM_GYRO_AXIS 67
#define MOUSE_Y_FROM_GYRO_AXIS 68
#define ZR_DUAL_STAGE_MODE 69
#define ZL_DUAL_STAGE_MODE 70
#define AUTOLOAD 71
#define HELP 72
#define WHITELISTER 73

#define MAGIC_DST_DELAY 150.0f // in milliseconds
// Tap duration only applies to GYRO_OFF tap and coded a horrible exception. 
#define MAGIC_TAP_DURATION 500.0f // in milliseconds
#define MAGIC_HOLD_TIME 150.0f // in milliseconds
#define MAGIC_SIM_DELAY 50.0f // in milliseconds
static_assert(MAGIC_SIM_DELAY < MAGIC_HOLD_TIME, "Simultaneous press delay has to be smaller than hold delay!");


enum class DstState { NoPress = 0, PressStart, QuickSoftTap, QuickFullPress, QuickFullRelease, SoftPress, DelayFullPress, PressStartResp, invalid };

class DigitalButton;

// Mapping for a press combination, whether chorded or simultaneous
struct ComboMap
{
	std::string name;
	int btn;
	WORD pressBind = 0;
	WORD holdBind = 0;
};


typedef struct GyroSample {
	float x;
	float y;
} GyroSample;

class JoyShock : public JoyShockIF{
private:
	float _weightsRemaining[16];
	float _flickSamples[16];
	int _frontSample = 0;

	GyroSample _gyroSamples[64];
	int _frontGyroSample = 0;

public:
	JoyShock(int uniqueHandle, float pollRate, int controllerSplitType, float stickStepSize);

	const int MaxGyroSamples = 64;
	const int NumSamples = 16;
	int intHandle;

	
	//BtnState btnState[MAPPING_SIZE];
	std::vector<DigitalButton> btnState;
	std::deque<int> chordStack; // Represents the remapping layers active. Each item needs to have an entry in chord_mappings.
	std::deque<std::pair<int, WORD>> gyroActionQueue; // Queue of gyro control actions currently in effect
	std::chrono::steady_clock::time_point started_flick;
	// tap_release_queue has been replaced with button states *TapRelease. The hold time of the tap is the polling period of the device.
	float delta_flick = 0.0;
	float flick_percent_done = 0.0;
	float flick_rotation_counter = 0.0;
	bool toggleContinuous = false;

	float poll_rate;
	int controller_type = 0;
	float stick_step_size;

	float left_acceleration = 1.0;
	float right_acceleration = 1.0;
	std::vector<DstState> triggerState; // State of analog triggers when skip mode is active

	DigitalButton *GetButton(int index) override;

	WORD GetPressMapping(int index);

	WORD GetHoldMapping(int index);

	void ApplyBtnPress(int index, bool tap = false) override;

	void ApplyBtnHold(int index);

	void ApplyBtnRelease(int index, bool tap = false) override;

	void ApplyBtnPress(const ComboMap &simPress, int index, bool tap = false);

	void ApplyBtnHold(const ComboMap &simPress, int index);

	void ApplyBtnRelease(const ComboMap &simPress, int index, bool tap = false);

	// Pretty wrapper
	inline float GetPressDurationMS(int index) override
	{
		return static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(time_now - press_times[index]).count());
	}

	// Indicate if the button is currently sending an assigned mapping.
	bool IsActive(int mappingIndex);

	inline bool HasSimMappings(int index);

	const ComboMap* GetMatchingSimMap(int index) override;

	void ResetSmoothSample();

	float GetSmoothedStickRotation(float value, float bottomThreshold, float topThreshold, int maxSamples);

	void GetSmoothedGyro(float x, float y, float length, float bottomThreshold, float topThreshold, int maxSamples, float& outX, float& outY);

	~JoyShock();
};
