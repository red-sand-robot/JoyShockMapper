#include "DigitalButton.h"
#include "main.h"

// Step #5: Forward Declare your concrete States
class NoPress;
class BtnPress;

// Step #6: For each concrete state:
//  - Declare and use macro to define chaining constructor
//  - Implement all abstract function and override those desired: onEntry/Exit and react to events
//  - Call changeState when appropriate, transition action is optional and occurs between exit and entry.
//  - State transition occurs after returning from the react function
class NoPress : public ButtonState
{
	STATE_OF(NoPress, ButtonState)

	// 6.2 Define a constructor for your initial state, calling macro and initializing custom fields.
	NoPress(JoyShockIF *p_jc, int i)
	{
		INITIAL_STATE_CTOR(NoPress);
		jc = p_jc;
		index = i;
	}

	void react(PressEvent *evt) override
	{
		// You can use lambda for transition function
		changeState<BtnPress>([this]() { printf("(%s) press true\n", _name); });
	}
};

class BtnPress : public ButtonState
{
	STATE_OF(BtnPress, ButtonState)

	void onEntry() override
	{
		jc->ApplyBtnPress(index);
	}

	void react(ReleaseEvent *evt) override
	{
		// Or you can use std::bind with a member function
		changeState<NoPress>(std::bind(&BtnPress::ReleaseTransition, this));
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
//  - Optionally override lock/unlock functions to secure cross thread operation
DigitalButton::DigitalButton(JoyShockIF &jc, int index)
	: FiniteStateMachine()
{;
	Initialize(new NoPress(&jc, index));
}
