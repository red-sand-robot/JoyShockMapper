#pragma once

#include <functional>

namespace pocket_fsm
{

// Base structure for events to be handled by the States of the Finite State Machine
struct Event
{
	virtual ~Event() {};
};

// Basic State interface and transition call logic
class StateIF
{
public:
	// Signature for a function to be used during transition
	using TransitionFunc = std::function<void()>;

	// Do not copy StateIF data when chaining constructors
	StateIF(StateIF &s)
		: _onTransition(nullptr)
		, _nextState(nullptr)
	{}

	StateIF() = default;

	virtual ~StateIF() {
		if (_onTransition) 
			_onTransition();
	}

	virtual void onEntry() = 0;
	virtual void onExit() = 0;

	const char *_name;
protected:
	// The BASE_STATE macro defines this function and allows a state to transition to another
	template<class S>
	void transitTo(std::function<void()> onTransit = nullptr) = 0;

	// The next state that the state machine must transition to
	StateIF *_nextState = nullptr;

	// Action to perform on changing of state after exit before entry
	TransitionFunc _onTransition = nullptr;
};


// The StateMachine handles sending events to the current state and manages state transitions. Derive from this class
// with your state base class as parameter and set up a constructor with your initial state.
template<class S>
class FiniteStateMachine
{
public:
	static_assert(std::is_base_of<StateIF, S>::value, "Parameter of StateMachine needs to be a descendant of StateIF");

	// Basic constructor / destructor
	FiniteStateMachine() : _currentState(nullptr) {}
	virtual ~FiniteStateMachine() {}

	// Call this method with an event that S can react to!
	template<class E>
	inline void SendEvent(E &evt)
	{
		static_assert(std::is_base_of<Event, E>::value, "Parameter of StateMachine::SendEvent needs to be a descendant of EventData");

		_currentState->react(&evt);
		SetCurrentstate(_currentState->_nextState);
	}

	inline const char * GetCurrentStateName() const
	{
		return _currentState->_name;
	}

protected:
	// Descendants call this in their constructor to set the initial state 
	void Initialize(S *initialState) 
	{
		_currentState.reset(initialState);
		_currentState->onEntry();
	}

private:
	// This function runs the basic state machine logic event calls
	void SetCurrentstate(StateIF *nextState)
	{
		if (nextState)
		{
			_currentState->onExit();
			_currentState.reset(static_cast<S*>(nextState)); // call _onTransition()
			_currentState->onEntry();
		}
	}

	// The current state machine state.
	std::unique_ptr<S> _currentState;

};

// Declare your base state class using this macro
// A StateMachine for that bas class will initialize the hook through friendship privilege
#define BASE_STATE(BASENAME) \
class BASENAME : public pocket_fsm::StateIF { \
	friend class pocket_fsm::FiniteStateMachine<BASENAME>; \
protected: \
	template<class S> \
	void transitTo(std::function<void()> onTransit = nullptr) { \
		static_assert(std::is_base_of<BASENAME, S>::value, "Parameter of transitTo needs to be a descendant of " #BASENAME); \
		_onTransition = onTransit; \
		if(_nextState) delete _nextState; \
		_nextState = new S(*this); \
	}

// Use this macro to define a specific state where NAME is the name of your State Class
// derived from BASE, and BASE is derived from StateIF
#define STATE(NAME, BASE) \
class NAME : public BASE { \
public: \
	NAME(BASE &chain) : BASE(chain) { _name=#NAME; } 


// Use this macro in the custom constructor of your initial state to initialize
#define INIT_BASE(NAME) \
	_name = #NAME

}