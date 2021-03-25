#pragma once

#include "JoyShockMapper.h"
#include "gmock/gmock.h"
#include <iostream>

class MockLog : public stringbuf
{
	~MockLog()
	{
		std::cout << str();
		if (MockLog::_logger->_lvl > Log::UT)
		{
			MockLog::_logger->print(MockLog::_logger->_lvl, str());
		}
	}

public:
	struct Inst
	{
		Log::Level _lvl;
		MOCK_METHOD(void, print, (int, in_string));
	};

	static unique_ptr<Inst> _logger;
};

streambuf *Log::makeBuffer(Level level)
{
	MockLog::_logger->_lvl = level;
	return new MockLog();
}

unique_ptr<MockLog::Inst> MockLog::_logger = nullptr;
