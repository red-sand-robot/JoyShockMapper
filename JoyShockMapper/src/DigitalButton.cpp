#include "DigitalButton.h"
#include "main.h"

class NoPress;
class BtnPress;
class WaitSim;
class WaitHold;
class SimPress;
class HoldPress;
class WaitSimHold;
class SimHold;
class SimRelease;
class SimTapRelease;
class TapRelease;

void ButtonState::react(SimultaneousEvent *evt)
{
	printf("Error: lost track of matching sim press for %s! Resetting to NoPress.\n", name);
	changeState<SimRelease>();
}

class NoPress : public ButtonState
{
	STATE_OF(NoPress, ButtonState)

	// 6.2 Define a constructor for your initial state, calling macro and initializing custom fields.
	NoPress(JoyShockIF *p_jc, int i, std::unordered_map<int, std::vector<ComboMap>> &sm)
		: ButtonState(sm)
	{
		INITIAL_STATE_CTOR(NoPress);
		jc = p_jc;
		index = i;
	}

	void react(PressEvent *evt) override
	{
		if (sim_mappings.find(index) != sim_mappings.end() && sim_mappings[index].size() > 0)
		{
			changeState<WaitSim>();
		}
		else if (jc->GetHoldMapping(index))
		{
			changeState<WaitHold>();
		}
		else
		{
			changeState<BtnPress>();
		}
	}
};

class BtnPress : public ButtonState
{
	STATE_OF(BtnPress, ButtonState)

	void onEntry() override
	{
		jc->ApplyBtnPress(index);
		printf("%s: true\n", name);
	}

	void react(ReleaseEvent *evt) override
	{
		// Or you can use std::bind with a member function
		changeState<NoPress>();
	}


	void onExit() override
	{
		jc->ApplyBtnRelease(index);
		printf("%s: false\n", name);
	}
};

class WaitSim : public ButtonState
{
	STATE_OF(WaitSim, ButtonState)

	void onEntry() override {
		jc->press_times[index] = jc->time_now;
	};

	void react(PressEvent *evt) override {
		// Is there a sim mapping on this button where the other button is in WaitSim state too?
		auto simMap = jc->GetMatchingSimMap(index);
		if (simMap)
		{
			// We have a simultaneous press!
			if (simMap->holdBind)
			{
				changeState<WaitSimHold>();
			}
			else
			{
				changeState<SimPress>();
				jc->ApplyBtnPress(*simMap, index);
				printf("%s: true\n", simMap->name.c_str());
			}
			SimultaneousEvent evt;
			evt.orig = simMap;
			jc->GetButton(simMap->btn)->sendEvent(evt);
		}
		else if (jc->GetPressDurationMS(index) > MAGIC_SIM_DELAY)
		{
			// Sim delay expired!
			if (jc->GetHoldMapping(index))
			{
				changeState<WaitHold>();
				// Don't reset time
			}
			else
			{
				changeState<BtnPress>();
			}
		}
		// Else let time flow, stay in this state, no output.
	};

	void react(ReleaseEvent *evt) override {
		changeState<TapRelease>();
		jc->press_times[index] = jc->time_now;
	};

	void react(SimultaneousEvent *evt) override {
		const ComboMap *simMap = evt->orig;
		if (simMap->holdBind)
		{
			changeState<WaitSimHold>();
		}
		else
		{
			changeState<SimPress>();
		}
	};

};
class WaitHold : public ButtonState
{
	STATE_OF(WaitHold, ButtonState)

	void onEntry() override {
		jc->press_times[index] = jc->time_now;
	};

	void react(PressEvent *evt) override {
		if (jc->GetPressDurationMS(index) > MAGIC_HOLD_TIME)
		{
			changeState<HoldPress>();
		}
	};

	void react(ReleaseEvent *evt) override {
		changeState<TapRelease>();
	};
};
class SimPress : public ButtonState
{
	STATE_OF(SimPress, ButtonState)

	void react(ReleaseEvent *evt) override {
		// Which is the sim mapping where the other button is in SimPress state too?
		auto simMap = jc->GetMatchingSimMap(index);
		if (!simMap)
		{
			// Should never happen but added for robustness.
			printf("Error: lost track of matching sim press for %s! Resetting to NoPress.\n", name);
			changeState<NoPress>();
		}
		else
		{
			changeState<SimRelease>();
			jc->GetButton(simMap->btn)->sendEvent(SimultaneousEvent());
			jc->ApplyBtnRelease(*simMap, index);
			printf("%s: false\n", simMap->name.c_str());
		}
	};
	void react(SimultaneousEvent *evt) override {
		changeState<SimRelease>();
	};

};
class HoldPress : public ButtonState
{
	STATE_OF(HoldPress, ButtonState)

	void onEntry() override {
		jc->ApplyBtnHold(index);
		printf("%s: held\n", name);
	}

	void react(ReleaseEvent *evt) override {
		changeState<NoPress>();
	};

	void onExit() override
	{
		jc->ApplyBtnRelease(index);
		printf("%s: hold released\n", name);
	}
};
class WaitSimHold : public ButtonState
{
	STATE_OF(WaitSimHold, ButtonState)
		
	void onEntry() override {
		jc->press_times[index] = jc->time_now;
	};

	void react(PressEvent *evt) override {
		// Which is the sim mapping where the other button is in WaitSimHold state too?
		auto simMap = jc->GetMatchingSimMap(index);
		if (!simMap)
		{
			// Should never happen but added for robustness.
			printf("Error: lost track of matching sim press for %s! Resetting to NoPress.\n", name);
			changeState<NoPress>();
		}
		else if (jc->GetPressDurationMS(index) > MAGIC_HOLD_TIME)
		{
			changeState<SimHold>();
			SimultaneousEvent evt;
			evt.pressed = true;
			jc->GetButton(simMap->btn)->sendEvent(evt);
			jc->ApplyBtnHold(*simMap, index);
			printf("%s: held\n", simMap->name.c_str());
			// Else let time flow, stay in this state, no output.
		}
	};

	void react(ReleaseEvent *evt) override {
		// Which is the sim mapping where the other button is in WaitSimHold state too?
		auto simMap = jc->GetMatchingSimMap(index);
		if (!simMap)
		{
			// Should never happen but added for robustness.
			printf("Error: lost track of matching sim press for %s! Resetting to NoPress.\n", name);
			changeState<NoPress>();
		}
		else
		{
			changeState<SimTapRelease>();
			SimultaneousEvent evt;
			evt.pressed = false;
			jc->GetButton(simMap->btn)->sendEvent(evt);
			jc->ApplyBtnPress(*simMap, index, true);
			printf("%s: tapped\n", simMap->name.c_str());
		}
	};

	void react(SimultaneousEvent *evt) override {
		if (evt->pressed)
		{
			changeState<SimHold>();
		}
		else
		{
			changeState<SimTapRelease>();
		}
	};
};
class SimHold : public ButtonState
{
	STATE_OF(SimHold, ButtonState)

	void react(ReleaseEvent *evt) override {
		// Which is the sim mapping where the other button is in SimHold state too?
		auto simMap = jc->GetMatchingSimMap(index);
		if (!simMap)
		{
			// Should never happen but added for robustness.
			printf("Error: lost track of matching sim press for %s! Resetting to NoPress.\n", name);
			changeState<NoPress>();
		}
		else
		{
			changeState<SimRelease>();
			jc->GetButton(simMap->btn)->sendEvent(SimultaneousEvent());
			jc->ApplyBtnRelease(*simMap, index);
			printf("%s: hold released\n", simMap->name.c_str());
		}
	};

	void react(SimultaneousEvent *evt) override
	{
		changeState<SimRelease>();
	}
};
class SimRelease : public ButtonState
{
	STATE_OF(SimRelease, ButtonState)

	void react(ReleaseEvent *evt) override {
		changeState<NoPress>();
	};
};
class SimTapRelease : public ButtonState
{
	STATE_OF(SimTapRelease, ButtonState)

	void onEntry() override
	{
		jc->press_times[index] = jc->time_now;
	}

	void react(PressEvent *evt) override {
		auto simMap = jc->GetMatchingSimMap(index);
		jc->ApplyBtnRelease(*simMap, index, true);
		changeState<SimRelease>();
		jc->GetButton(simMap->btn)->sendEvent(SimultaneousEvent());
	};

	void react(ReleaseEvent *evt) override {
		// Which is the sim mapping where the other button is in SimTapRelease state too?
		auto simMap = jc->GetMatchingSimMap(index);
		if (!simMap)
		{
			// Should never happen but added for robustness.
			printf("Error: lost track of matching sim press for %s! Resetting to NoPress.\n", name);
			changeState<SimRelease>();
		}
		// I hate making an exception for GYRO_OFF -,-
		else if (jc->keyToRelease[index] != GYRO_OFF_BIND || jc->GetPressDurationMS(index) > MAGIC_TAP_DURATION)
		{
			jc->ApplyBtnRelease(*simMap, index, true);
			changeState<SimRelease>();
			jc->GetButton(simMap->btn)->sendEvent(SimultaneousEvent());
		}
	};

	void react(SimultaneousEvent *evt) override
	{
		changeState<SimRelease>();
	}
};
class TapRelease : public ButtonState
{
	STATE_OF(TapRelease, ButtonState)

	void react(PressEvent *evt) override {
		changeState<NoPress>();
	};

	void onEntry() override
	{
		jc->ApplyBtnPress(index, true);
		printf("%s: tapped\n", name);
		jc->press_times[index] = jc->time_now;
	}

	void react(ReleaseEvent *evt) override {
		// I hate making an exception for GYRO_OFF -,-
		if (jc->keyToRelease[index] != GYRO_OFF_BIND || jc->GetPressDurationMS(index) > MAGIC_TAP_DURATION)
		{
			changeState<NoPress>();
		}
	};

	void onExit() override
	{
		jc->ApplyBtnRelease(index, true);
	}
};

DigitalButton::DigitalButton(JoyShockIF &jc, int index, std::unordered_map<int, std::vector<ComboMap>> &sim_mappings)
	: FiniteStateMachine()
{;
	initialize(new NoPress(&jc, index, sim_mappings));
}
