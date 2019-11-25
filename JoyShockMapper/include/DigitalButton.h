#pragma once
#include "pocket_fsm.h"
#include <chrono>

#define MAPPING_SIZE 30 // BAD
#define GYRO_OFF_BIND 0x0E

struct ComboMap;
class DigitalButton;

class JoyShockIF {
public:
	std::chrono::steady_clock::time_point press_times[MAPPING_SIZE];
	std::chrono::steady_clock::time_point time_now;
	uint16_t keyToRelease[MAPPING_SIZE]; // At key press, remember what to release

	virtual uint16_t GetHoldMapping(int index) = 0;

	virtual void ApplyBtnPress(int index, bool tap = false) = 0;
	
	virtual void ApplyBtnRelease(int index, bool tap = false) = 0;

	virtual void ApplyBtnHold(int index) = 0;

	virtual void ApplyBtnPress(const ComboMap &simPress, int index, bool tap = false) = 0;

	virtual void ApplyBtnHold(const ComboMap &simPress, int index) = 0;

	virtual void ApplyBtnRelease(const ComboMap &simPress, int index, bool tap = false) = 0;

	virtual const ComboMap* GetMatchingSimMap(int index) = 0;

	virtual inline float GetPressDurationMS(int index) = 0;

	virtual DigitalButton *GetButton(int index) = 0;
};

struct PressEvent : public pocket_fsm::Event {};
struct ReleaseEvent : public pocket_fsm::Event {};
struct SimultaneousEvent : public pocket_fsm::Event 
{
	const ComboMap *orig; 
	bool pressed = true;
};

class ButtonState : public pocket_fsm::StateIF {
	BASE_STATE(ButtonState)

	ButtonState(std::unordered_map<int, std::vector<ComboMap>> &sm) : sim_mappings(sm) { };

	JoyShockIF *jc;
	int index;
	std::unordered_map<int, std::vector<ComboMap>> &sim_mappings;
public:

	// Step #2.2: Declare a react function for each event in Step 1. Implment default behaviour or leave abstract.
	virtual void react(PressEvent *evt) {};
	virtual void react(ReleaseEvent *evt) {};
	virtual void react(SimultaneousEvent *evt);
	
	// Step #2.3: Optionally define a default behaviour for onEntry/Exit.
	void onEntry() override {};
	void onExit() override {};
};

// Step #4: Define a State Machine object for the base state of Step #2.1
// Optionally decalre a lock object field that will secure cross thread operation.
class DigitalButton : public pocket_fsm::FiniteStateMachine<ButtonState>
{
public:
	DigitalButton(JoyShockIF &jc, int index, std::unordered_map<int, std::vector<ComboMap>> &sim_mappings); // Take parameters to instantiate initial State
};

