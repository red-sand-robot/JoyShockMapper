#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "DigitalButton.h"
#include "JSMVariable.hpp" // JSMButton
#include "mockGamepad.hpp"
#include "mockInputHelpers.hpp"
#include "MockLog.hpp"

using namespace testing;

JSMVariable<ControllerScheme> virtual_controller(ControllerScheme::NONE);
JSMVariable<float> sim_press_window(50.f);
JSMVariable<float> dbl_press_window(200.f);
JSMSetting<float> turbo_period = JSMSetting<float>(SettingID::TURBO_PERIOD, 80.0f);
JSMSetting<float> hold_press_time = JSMSetting<float>(SettingID::HOLD_PRESS_TIME, 150.0f);
JSMSetting<JoyconMask> joycon_gyro_mask(SettingID::JOYCON_GYRO_MASK, JoyconMask::IGNORE_LEFT);
JSMSetting<JoyconMask> joycon_motion_mask(SettingID::JOYCON_MOTION_MASK, JoyconMask::IGNORE_RIGHT);
function<bool(in_string)> Mapping::_isCommandValid = function<bool(in_string)>();
const Mapping Mapping::NO_MAPPING = Mapping("NONE");

WORD nameToKey(const std::string &name)
{
	return 0;
}

struct TestParam
{
	int buttonCount;
};

std::ostream &operator<<(std::ostream &out, const TestParam &param)
{
	out << "buttonCount: " << param.buttonCount;
	return out;
}

class TestDigitalButton : public TestWithParam<TestParam>
{
protected:
	vector<JSMButton> mappings;
	vector<DigitalButton> buttons;
	shared_ptr<DigitalButton::Common> btnCommon;
	Pressed pressed;
	Released released;

	void SetUp() override
	{
		MockLog::_logger.reset(new MockLog::Inst());
		ON_CALL(*MockLog::_logger, print(Log::UT, _)).WillByDefault(Return());
		pressed.holdTime = *hold_press_time.get();
		pressed.turboTime = *turbo_period.get();
		released.holdTime = *hold_press_time.get();
		released.turboTime = *turbo_period.get();
		mockInput.reset(new NiceMock<MockInputHelpers>());
		btnCommon.reset(new DigitalButton::Common(nullptr, nullptr));
		for (int i = 0; i < GetParam().buttonCount; ++i)
		{
			ButtonID id = *magic_enum::enum_cast<ButtonID>(i);
			mappings.push_back(JSMButton(id, Mapping::NO_MAPPING));
		}
		for (int i = 0; i < GetParam().buttonCount; ++i)
		{
			buttons.push_back(DigitalButton(btnCommon, mappings[i]));
		}
	}

	void TearDown() override
	{
		buttons.clear();
		mappings.clear();
		btnCommon.reset();
		mockInput.reset();
		MockLog::_logger.reset();
	}

	Pressed &DoPress()
	{
		pressed.time_now = chrono::steady_clock::now();
		return pressed;
	}

	Released &DoRelease()
	{
		released.time_now = chrono::steady_clock::now();
		return released;
	}
};

TEST_P(TestDigitalButton, SimpleBindTap)
{
	KeyCode a("A");
	Mapping newMapping;
	newMapping.AddMapping(a, Mapping::EventModifier::StartPress);
	mappings[0] = newMapping;

	InSequence orderMatters;
	EXPECT_CALL(*MockLog::_logger, print(Log::BASE, HasSubstr("true"))).Times(1);
	EXPECT_CALL(*mockInput, pressKey(a, true)).Times(1);
	EXPECT_CALL(*MockLog::_logger, print(Log::BASE, HasSubstr("false"))).Times(1);
	EXPECT_CALL(*mockInput, pressKey(a, false)).Times(1);
	//EXPECT_CALL(*MockLog::_logger, print(1, HasSubstr("false"))).Times(1);

	// Start at no press
	ASSERT_EQ(BtnState::NoPress, buttons[0].getState());

	// Check pressEvent => press state
	buttons[0].sendEvent(DoPress());
	EXPECT_EQ(BtnState::BtnPress, buttons[0].getState());

	// Quick Tap
	this_thread::sleep_for(chrono::milliseconds(int(*hold_press_time.get() - 1)));

	// Check Release => Tap
	buttons[0].sendEvent(DoRelease());
	EXPECT_EQ(BtnState::TapRelease, buttons[0].getState());

	// Quick wait tap duration
	this_thread::sleep_for(chrono::milliseconds(int(MAGIC_TAP_DURATION - 10)));

	// Still Tap Release
	buttons[0].sendEvent(DoRelease());
	EXPECT_EQ(BtnState::TapRelease, buttons[0].getState());

	this_thread::sleep_for(chrono::milliseconds(10));

	// Done Taping
	buttons[0].sendEvent(DoRelease());
	EXPECT_EQ(BtnState::NoPress, buttons[0].getState());
}

TEST_P(TestDigitalButton, SimpleBindHold)
{
	KeyCode a("A");
	Mapping newMapping;
	newMapping.AddMapping(a, Mapping::EventModifier::StartPress);
	mappings[0] = newMapping;

	InSequence orderMatters;
	EXPECT_CALL(*MockLog::_logger, print(Log::BASE, HasSubstr("true"))).Times(1);
	EXPECT_CALL(*mockInput.get(), pressKey(a, true)).Times(1);
	EXPECT_CALL(*MockLog::_logger, print(Log::BASE, HasSubstr("false"))).Times(1);
	EXPECT_CALL(*mockInput.get(), pressKey(a, false)).Times(1);

	// Start at no press
	ASSERT_EQ(BtnState::NoPress, buttons[0].getState());

	// Check pressEvent => press state
	buttons[0].sendEvent(DoPress());
	EXPECT_EQ(BtnState::BtnPress, buttons[0].getState());

	// Quick Tap
	this_thread::sleep_for(chrono::milliseconds(int(*hold_press_time.get() - 1)));

	// Still pressed
	buttons[0].sendEvent(DoPress());
	EXPECT_EQ(BtnState::BtnPress, buttons[0].getState());

	// Wait some more
	this_thread::sleep_for(chrono::milliseconds(2));

	buttons[0].sendEvent(DoPress());
	EXPECT_EQ(BtnState::BtnPress, buttons[0].getState());

	this_thread::sleep_for(chrono::milliseconds(1));

	// Release Event => no press
	buttons[0].sendEvent(DoRelease());
	EXPECT_EQ(BtnState::NoPress, buttons[0].getState());
}

TEST_P(TestDigitalButton, DoubleBindTap)
{
	KeyCode a("A"), b("B");
	Mapping newMapping;
	newMapping.AddMapping(a, Mapping::EventModifier::TapPress);
	newMapping.AddMapping(b, Mapping::EventModifier::HoldPress);
	mappings[0] = newMapping;

	InSequence orderMatters;
	EXPECT_CALL(*MockLog::_logger, print(Log::BASE, HasSubstr("tapped"))).Times(1);
	EXPECT_CALL(*mockInput.get(), pressKey(a, true)).Times(1);
	EXPECT_CALL(*mockInput.get(), pressKey(a, false)).Times(1);
	EXPECT_CALL(*mockInput.get(), pressKey(b, _)).Times(0);

	// Start at no press
	ASSERT_EQ(BtnState::NoPress, buttons[0].getState());

	// Check pressEvent => press state
	buttons[0].sendEvent(DoPress());
	EXPECT_EQ(BtnState::BtnPress, buttons[0].getState());

	// Quick Tap
	this_thread::sleep_for(chrono::milliseconds(int(*hold_press_time.get() - 1)));

	// Check Release => Tap
	buttons[0].sendEvent(DoRelease());
	EXPECT_EQ(BtnState::TapRelease, buttons[0].getState());

	// Quick wait tap duration
	this_thread::sleep_for(chrono::milliseconds(int(MAGIC_TAP_DURATION - 10)));

	// Still taping
	buttons[0].sendEvent(DoRelease());
	EXPECT_EQ(BtnState::TapRelease, buttons[0].getState());

	this_thread::sleep_for(chrono::milliseconds(11));

	// Done Taping
	buttons[0].sendEvent(DoRelease());
	EXPECT_EQ(BtnState::NoPress, buttons[0].getState());
}

TEST_P(TestDigitalButton, DoubleBindHold)
{
	KeyCode a("A"), b("B");
	Mapping newMapping;
	newMapping.AddMapping(a, Mapping::EventModifier::TapPress);
	newMapping.AddMapping(b, Mapping::EventModifier::HoldPress);
	mappings[0] = newMapping;

	InSequence orderMatters;
	EXPECT_CALL(*mockInput.get(), pressKey(a, _)).Times(0);
	EXPECT_CALL(*MockLog::_logger, print(Log::BASE, HasSubstr("held"))).Times(1);
	EXPECT_CALL(*mockInput.get(), pressKey(b, true)).Times(1);
	EXPECT_CALL(*MockLog::_logger, print(Log::BASE, HasSubstr("false"))).Times(1);
	EXPECT_CALL(*mockInput.get(), pressKey(b, false)).Times(1);
	//EXPECT_CALL(*MockLog::_logger, print(LogLevel::BASE, string("patate frite"))).Times(1);

	// Start at no press
	ASSERT_EQ(BtnState::NoPress, buttons[0].getState());

	// Check pressEvent => press state
	buttons[0].sendEvent(DoPress());
	EXPECT_EQ(BtnState::BtnPress, buttons[0].getState());

	// Quick Tap
	this_thread::sleep_for(chrono::milliseconds(int(*hold_press_time.get() - 1)));

	// Still pressed
	buttons[0].sendEvent(DoPress());
	EXPECT_EQ(BtnState::BtnPress, buttons[0].getState());

	// Wait some more
	this_thread::sleep_for(chrono::milliseconds(2));

	buttons[0].sendEvent(DoPress());
	EXPECT_EQ(BtnState::BtnPress, buttons[0].getState());

	this_thread::sleep_for(chrono::milliseconds(1));

	// Release Event => no press
	Released &r(DoRelease());
	buttons[0].sendEvent(r);
	EXPECT_EQ(BtnState::NoPress, buttons[0].getState());
}

TestParam values[] = { { 1 } };

INSTANTIATE_TEST_SUITE_P(SingleButtonTests, TestDigitalButton, testing::ValuesIn(values));
