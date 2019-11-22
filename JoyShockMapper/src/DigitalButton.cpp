#include "DigitalButton.h"
#include "main.h"

// Step #5: Forward Declare your concrete State
class NoPress;
class BtnPress;

// Step #6: For each concrete state:
//  - Declare using Macro
//  - Do things onEntry/Exit
//  - React to all events that the state supports
//  - Call transit when you want to change state, transition action is optional
//  - Transition occurs after returning from the react function
//  - Define a constructor for your initial state, calling INIT_BASE macro and initializing custom fields.
STATE(NoPress, ButtonState)
	NoPress(JoyShock *p_jc, int i)
	{
		jc = p_jc;
		index = i;
		INIT_BASE(NoPress);
	}

	void react(PressEvent *evt) override
	{
		// You can use lambda for transition function
		transitTo<BtnPress>([this]() { printf("(%s) press true\n", _name); });
	}
};

STATE(BtnPress, ButtonState)
	void onEntry() override
	{
		jc->ApplyBtnPress(index);
	}

	void react(ReleaseEvent *evt) override
	{
		// Or you can use std::bind with a member function
		transitTo<NoPress>(std::bind(&BtnPress::ReleaseTransition, this));
	}

	void ReleaseTransition()
	{
		printf("(%s) press false\n", _name);
	}

	void onExit() override
	{
		jc->ApplyBtnRelease(index);
	}
};

// Step #7: Define the state machine functions
//  - Constructor calls initialize() with an instance of your initial state. StateMachine takes ownership of pointer.
//  - Event functions create event data and sends it to the current state
DigitalButton::DigitalButton(JoyShock &jc, int index)
	: FiniteStateMachine()
{
	Initialize(new NoPress(&jc, index));
}
