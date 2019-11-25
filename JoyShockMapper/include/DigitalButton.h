#pragma once
#include "pocket_fsm.h"

// Step 0 (optional): Define an interface to the owning object to access funcitons and data members
class JoyShockIF {
public:
	virtual void ApplyBtnPress(int index, bool tap = false) = 0;
	
	virtual void ApplyBtnRelease(int index, bool tap = false) = 0;
};


// Step #1: List events that the state machine reacts to (i.e.: state machine inputs)
// Include any data that may be relevant for the state to handle
struct PressEvent : public pocket_fsm::Event {};
struct ReleaseEvent : public pocket_fsm::Event {};
struct SimultaneousEvent : public pocket_fsm::Event { int orig; };

// Step #2.1: Declare a base state and use provided macro, adding interface of step 0 as member or other State Machine outputs
// Be aware that these fields are copied to other states by value. Minimize data fields to improve performance.
class ButtonState : public pocket_fsm::StateIF {
	BASE_STATE(ButtonState)

	JoyShockIF *jc;
	int index;
public:

	// Step #2.2: Declare a react function for each event in Step 1. Implment default behaviour or leave abstract.
	virtual void react(PressEvent *evt) {};
	virtual void react(ReleaseEvent *evt) {};
	
	// Step #2.3: Optionally define a default behaviour for onEntry/Exit.
	void onEntry() override {};
	void onExit() override {};
};

// Step #4: Define a State Machine object for the base state of Step #2.1
// Optionally decalre a lock object field that will secure cross thread operation.
class DigitalButton : public pocket_fsm::FiniteStateMachine<ButtonState>
{
public:
	DigitalButton(JoyShockIF &jc, int index); // Take parameters to instantiate initial State
};

