#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "JoyShockMapper.h"
#include "Whitelister.h"
#include "TrayIcon.h"
#include "mockGamepad.hpp"
#include "mockJslWrapper.hpp"
#include "mockInputHelpers.hpp"

class JoyShock;

extern unique_ptr<JslWrapper> jsl;
extern unordered_map<int, shared_ptr<JoyShock>> handle_to_joyshock;

using namespace ::testing;

class Whitelister;
Whitelister* Whitelister::getNew(bool add)
{
	return nullptr; // Disable whitelisting
}

TrayIcon* TrayIcon::getNew(TrayIconData applicationName, std::function<void()>&& beforeShow)
{
	return nullptr; // Disable tray icon
}

class Regression : public Test
{
protected:
	MockJslWrapper* _mock_jsl;
	void SetUp() override
	{
		jsl.reset(JslWrapper::getNew());
		_mock_jsl = dynamic_cast<MockJslWrapper*>(jsl.get());
	}

	void TearDown() override
	{
		jsl.reset();
	}
};

TEST_F(Regression, RECONNECT_CONTROLLERS_None)
{
	EXPECT_CALL(*_mock_jsl, ConnectDevices).WillOnce(Return(0));

	bool result = do_RECONNECT_CONTROLLERS("");

	EXPECT_TRUE(result);
	EXPECT_EQ(handle_to_joyshock.size(), 0) << "There should have been no device";
}

TEST_F(Regression, RECONNECT_CONTROLLERS_3_full)
{
	array<int, 3> devices = { 1, 2, 3 };
	EXPECT_CALL(*_mock_jsl, ConnectDevices).WillOnce(Return(devices.size()));
	EXPECT_CALL(*_mock_jsl, GetControllerSplitType).Times(devices.size()).WillRepeatedly(Return(JS_SPLIT_TYPE_FULL));
	EXPECT_CALL(*_mock_jsl, GetConnectedDeviceHandles).WillOnce([&devices](int* deviceHandleArray, int size) {
		memcpy(deviceHandleArray, devices.data(), sizeof(devices));
		return devices.size();
	});

	EXPECT_TRUE(do_RECONNECT_CONTROLLERS(""));

	ASSERT_EQ(handle_to_joyshock.size(), devices.size()) << "There should have been 3 devices";
	int i = 0;
	for (auto pair : handle_to_joyshock)
	{
		EXPECT_EQ(pair.first, devices[i++]) << "handle for index " << i << " is wrong";
		//		EXPECT_EQ(pair.second->controller_split_type, JS_SPLIT_TYPE_FULL) << "Split Type for index " << i << " is wrong";
	}
}
