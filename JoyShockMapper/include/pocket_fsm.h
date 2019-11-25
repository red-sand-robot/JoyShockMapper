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

	virtual ~StateIF() 
	{
		if (_onTransition) 
			_onTransition();
	}

	virtual void onEntry() = 0;
	virtual void onExit() = 0;

	inline StateIF *GetNextState() 
	{
		return _nextState;
	}

	const char *name; // Stringified name of the concrete class
protected:
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
	virtual ~FiniteStateMachine() 
	{
		_currentState->onExit(); // Cleanup
	}

	// Call this method with an event that S can react to!
	template<class E>
	inline void sendEvent(E &evt)
	{
		static_assert(std::is_base_of<Event, E>::value, "Parameter of StateMachine::SendEvent needs to be a descendant of EventData");
		lock();
		_currentState->react(&evt);
		setCurrentstate(_currentState->GetNextState());
		unlock();
	}

	inline const char * getCurrentStateName() const
	{
		return _currentState->_name;
	}

protected:
	// Descendants call this in their constructor to set the initial state 
	void initialize(S *initialState) 
	{
		_currentState.reset(initialState);
		_currentState->onEntry();
	}

	// Override these functions using your favorite lock mecanism 
	// to secure State Machine cross thread operation
	virtual void lock() {};
	virtual void unlock() {};

private:
	// This function runs the basic state machine logic event calls
	void setCurrentstate(StateIF *nextState)
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

// Call this macro in your base state derving from StateIF
#define BASE_STATE(BASENAME) \
protected: \
	template<class S> \
	void changeState(std::function<void()> onTransit = nullptr) { \
		static_assert(std::is_base_of<BASENAME, S>::value, "Parameter of changeState needs to be a descendant of " #BASENAME); \
		_onTransition = onTransit; \
		if(_nextState) delete _nextState; \
		_nextState = new S(*this); \
	}

// Call this macro in a concrete state where NAME is the name of your State Class
// derived from BASE, and BASE is derived from StateIF. This sets up the chain.
#define STATE_OF(NAME, BASE) \
public: \
	NAME(BASE &chain) : BASE(chain) { name=#NAME; } 


// Use this macro in the custom constructor of your initial state to initialize
#define INITIAL_STATE_CTOR(NAME) \
	name = #NAME

}