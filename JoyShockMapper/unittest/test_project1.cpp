#include "JoyShockMapper.h"
#include "JslWrapper.h"
#include "Whitelister.h"
#include <iostream>
#include "gtest/gtest.h"
#include "gmock/gmock.h"

class SomeJsl
{
public:
	virtual ~SomeJsl(){};
	virtual int JslConnectDevices()
	{
		return 1;
	}
};

class MockJslWrapper : public JslWrapper
{
public:
	MOCK_METHOD(int, ConnectDevices, (), (override));
	MOCK_METHOD(int, GetConnectedDeviceHandles, (int* deviceHandleArray, int size), (override));
	MOCK_METHOD(void, DisconnectAndDisposeAll, (), (override));
	MOCK_METHOD(JOY_SHOCK_STATE, GetSimpleState, (int deviceId), (override));
	MOCK_METHOD(IMU_STATE, GetIMUState, (int deviceId), (override));
	MOCK_METHOD(MOTION_STATE, GetMotionState, (int deviceId), (override));
	MOCK_METHOD(TOUCH_STATE, GetTouchState, (int deviceId, bool previous), (override));
	MOCK_METHOD(bool, GetTouchpadDimension, (int deviceId, int& sizeX, int& sizeY), (override));
	MOCK_METHOD(int, GetButtons, (int deviceId), (override));
	MOCK_METHOD(float, GetLeftX, (int deviceId), (override));
	MOCK_METHOD(float, GetLeftY, (int deviceId), (override));
	MOCK_METHOD(float, GetRightX, (int deviceId), (override));
	MOCK_METHOD(float, GetRightY, (int deviceId), (override));
	MOCK_METHOD(float, GetLeftTrigger, (int deviceId), (override));
	MOCK_METHOD(float, GetRightTrigger, (int deviceId), (override));
	MOCK_METHOD(float, GetGyroX, (int deviceId), (override));
	MOCK_METHOD(float, GetGyroY, (int deviceId), (override));
	MOCK_METHOD(float, GetGyroZ, (int deviceId), (override));
	MOCK_METHOD(float, GetAccelX, (int deviceId), (override));
	MOCK_METHOD(float, GetAccelY, (int deviceId), (override));
	MOCK_METHOD(float, GetAccelZ, (int deviceId), (override));
	MOCK_METHOD(int, GetTouchId, (int deviceId, bool secondTouch), (override));
	MOCK_METHOD(bool, GetTouchDown, (int deviceId, bool secondTouch), (override));
	MOCK_METHOD(float, GetTouchX, (int deviceId, bool secondTouch), (override));
	MOCK_METHOD(float, GetTouchY, (int deviceId, bool secondTouch), (override));
	MOCK_METHOD(float, GetStickStep, (int deviceId), (override));
	MOCK_METHOD(float, GetTriggerStep, (int deviceId), (override));
	MOCK_METHOD(float, GetPollRate, (int deviceId), (override));
	MOCK_METHOD(void, ResetContinuousCalibration, (int deviceId), (override));
	MOCK_METHOD(void, StartContinuousCalibration, (int deviceId), (override));
	MOCK_METHOD(void, PauseContinuousCalibration, (int deviceId), (override));
	MOCK_METHOD(void, GetCalibrationOffset, (int deviceId, float& xOffset, float& yOffset, float& zOffset), (override));
	MOCK_METHOD(void, SetCalibrationOffset, (int deviceId, float xOffset, float yOffset, float zOffset), (override));
	MOCK_METHOD(void, SetCallback, (void (*callback)(int, JOY_SHOCK_STATE, JOY_SHOCK_STATE, IMU_STATE, IMU_STATE, float)), (override));
	MOCK_METHOD(void, SetTouchCallback, (void (*callback)(int, TOUCH_STATE, TOUCH_STATE, float)), (override));
	MOCK_METHOD(int, GetControllerType, (int deviceId), (override));
	MOCK_METHOD(int, GetControllerSplitType, (int deviceId), (override));
	MOCK_METHOD(int, GetControllerColour, (int deviceId), (override));
	MOCK_METHOD(void, SetLightColour, (int deviceId, int colour), (override));
	MOCK_METHOD(void, SetRumble, (int deviceId, int smallRumble, int bigRumble), (override));
	MOCK_METHOD(void, SetPlayerNumber, (int deviceId, int number), (override));
};

JslWrapper* JslWrapper::getNew()
{
	return new MockJslWrapper();
}

class MockWhitelister : public Whitelister
{
public:
	MockWhitelister()
	  : Whitelister()
	{
	}

	MOCK_METHOD(bool, Add, (string * optErrMsg), (override));
	MOCK_METHOD(bool, Remove, (string * optErrMsg), (override));
};

Whitelister* Whitelister::getNew(bool add)
{
	return new MockWhitelister();
}

bool Whitelister::ShowHIDCerberus()
{
	return false;
}

bool Whitelister::IsHIDCerberusRunning()
{
	return false;
}

extern unique_ptr<JslWrapper> jsl;
extern unordered_map<int, shared_ptr<JoyShock>> handle_to_joyshock;

// IndependentMethod is a test case - here, we have 2 tests for this 1 test case
TEST(Commands, RECONNECT_CONTROLLERS)
{
	using namespace ::testing;
	auto mjsl = dynamic_cast<MockJslWrapper*>(jsl.get());
	EXPECT_CALL(*mjsl, ConnectDevices).WillOnce(Return(1));

	bool result = do_RECONNECT_CONTROLLERS("");

	EXPECT_TRUE(result);
	EXPECT_EQ(handle_to_joyshock.size(), 0) << "There should have been no device";
}
