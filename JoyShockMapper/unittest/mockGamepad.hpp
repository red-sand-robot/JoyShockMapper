#include <gmock/gmock.h>
#include "Gamepad.h"

class MockGamepad : public Gamepad
{
public:
	MockGamepad(ControllerScheme scheme, Callback notification)
	{
	}
	virtual ~MockGamepad()
	{
	}
	MOCK_METHOD(bool, isInitialized, (std::string * errorMsg), (override));
	MOCK_METHOD(void, setButton, (KeyCode btn, bool pressed), (override));
	MOCK_METHOD(void, setLeftStick, (float x, float y), (override));
	MOCK_METHOD(void, setRightStick, (float x, float y), (override));
	MOCK_METHOD(void, setLeftTrigger, (float), (override));
	MOCK_METHOD(void, setRightTrigger, (float), (override));
	MOCK_METHOD(void, update, (), (override));
	MOCK_METHOD(ControllerScheme, getType, (), const, (override));
}

Gamepad*
Gamepad::getNew(ControllerScheme scheme, Callback notification)
{
	return new MockGamepad(scheme, notification);
}
