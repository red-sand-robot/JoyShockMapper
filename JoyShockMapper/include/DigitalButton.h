#pragma once
#include "pocket_fsm.h"

class JoyShock;

// Step #1: List events that the state machine reacts to (i.e.: state machine inputs)
// Include any data that may be relevant for the state to handle
struct PressEvent : public pocket_fsm::Event {};
struct ReleaseEvent : public pocket_fsm::Event {};
struct SimultaneousEvent : public pocket_fsm::Event { int orig; };

// Step #2.1: Declare a base state using provided macro, adding whatever the state machine needs to execute (i.e.: state machine outputs)
// Be aware that these fields are copied to other states by value. Minimize data fields to improve performance.
BASE_STATE(ButtonState)
	JoyShock *jc;
	int index;
public:

	// Step #2.2: Declare a react function for each event in Step 1. Implment default behaviour 
	virtual void react(PressEvent *evt) {};
	virtual void react(ReleaseEvent *evt) {};
	
	// Step #2.3: Optionally define a default behaviour for onEntry/Exit
	void onEntry() override {};
	void onExit() override {};
};

// Step #4: Define a State Machine Object for the base state of Step #2.1
class DigitalButton : public pocket_fsm::FiniteStateMachine<ButtonState>
{
public:
	DigitalButton(JoyShock &jc, int index); // Take parameters to instantiate initial State
};

