#include "whitelister.h"
#include "gmock/gmock.h"

class MockWhitelister : public Whitelister
{
public:
	MockWhitelister()
	  : Whitelister()
	{
	}

	virtual ~MockWhitelister(){};

	MOCK_METHOD(bool, ShowHIDCerberus, (), (override));
	MOCK_METHOD(bool, IsHIDCerberusRunning, (), (override));
	MOCK_METHOD(bool, Add, (string * optErrMsg), (override));
	MOCK_METHOD(bool, Remove, (string * optErrMsg), (override));
};

Whitelister* Whitelister::getNew(bool add)
{
	new MockWhitelister();
}
