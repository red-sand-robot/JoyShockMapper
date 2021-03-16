#include <iostream>
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <Windows.h>
#include <shellapi.h>

//int __stdcall wWinMain(HINSTANCE hInstance, HINSTANCE prevInstance, LPWSTR cmdLine, int cmdShow)
//    int argc = 0;
//    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
int main(int argc, char** argv)
{
	// Since Google Mock depends on Google Test, InitGoogleMock() is
	// also responsible for initializing Google Test.  Therefore there's
	// no need for calling testing::InitGoogleTest() separately.
	testing::InitGoogleMock(&argc, argv);
	return RUN_ALL_TESTS();
}
