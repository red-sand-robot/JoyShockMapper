#pragma once

#include "JoyShockMapper.h"
#include "gmock/gmock.h"
#include <iostream>

class MockLog : public Log
{
	Level _lvl;

public:
	MockLog(Level lvl)
	  : _lvl(lvl)
	{
	}

	~MockLog()
	{
		if (!str().empty())
		{
			std::cout << str();
			if (_lvl != Level::UT)
			{
				_logger->print(_lvl, str());
			}
		}
	}

	struct Inst
	{
		MOCK_METHOD(void, print, (int, in_string));
	};

	static unique_ptr<Inst> _logger;
};

unique_ptr<MockLog::Inst> MockLog::_logger = nullptr;

unique_ptr<Log> Log::getLog(Level lvl)
{
	unique_ptr<Log> ptr(new MockLog(lvl));
	return std::move(ptr);
	//return std::move(make_unique<MockLog>(lvl));
}
