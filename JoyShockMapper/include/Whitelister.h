#pragma once

#include <string>

using namespace std;

// A whitelister object adds its PID to a running HIDCerberus service and removes it on destruciton
// as per the RAII design pattern: https://en.cppreference.com/w/cpp/language/raii
class Whitelister
{
protected:
	Whitelister(bool add = false)
	  : _whitelisted(false)
	{
	}

public:
	static bool ShowHIDCerberus();
	static bool IsHIDCerberusRunning();
	static Whitelister *getNew(bool add = false);

	virtual ~Whitelister()
	{
	}

	virtual bool Add(string *optErrMsg = nullptr) = 0;
	virtual bool Remove(string *optErrMsg = nullptr) = 0;

	// Check whether you're whitelisted or not.
	// Could be replaced with an socket query, if one exists.
	inline operator bool()
	{
		return _whitelisted;
	}

protected:
	bool _whitelisted;
};
