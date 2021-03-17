#include "gmock/gmock.h"
#include "InputHelpers.h"

class MockInputHelpers
{
public:
	MOCK_METHOD(float, getMouseSpeed, ());
	MOCK_METHOD(int, pressMouse, (KeyCode vkKey, bool isPressed));
	MOCK_METHOD(int, pressKey, (KeyCode vkKey, bool pressed));
	MOCK_METHOD(void, moveMouse, (float x, float y));
	MOCK_METHOD(void, setMouseNorm, (float x, float y));
	MOCK_METHOD(BOOL, WriteToConsole, (in_string command));
	MOCK_METHOD(BOOL, ConsoleCtrlHandler, (DWORD dwCtrlType));
	MOCK_METHOD(void, initConsole, ());
	MOCK_METHOD((std::tuple<std::string, std::string>), GetActiveWindowName, ());
	MOCK_METHOD(std::vector<std::string>, ListDirectory, (std::string directory));
	MOCK_METHOD(std::string, GetCWD, ());
	MOCK_METHOD(bool, SetCWD, (in_string newCWD));
	MOCK_METHOD(DWORD, ShowOnlineHelp, ());
	MOCK_METHOD(void, HideConsole, ());
	MOCK_METHOD(void, UnhideConsole, ());
	MOCK_METHOD(void, ShowConsole, ());
	MOCK_METHOD(void, ReleaseConsole, ());
	MOCK_METHOD(bool, IsVisible, ());
	MOCK_METHOD(bool, isConsoleMinimized, ());
};

unique_ptr<MockInputHelpers> mockInput;

float getMouseSpeed()
{
	return mockInput->getMouseSpeed();
}

// send mouse button
int pressMouse(KeyCode vkKey, bool isPressed)
{
	return mockInput->pressMouse(vkKey, pressKey);
}

// send key press
int pressKey(KeyCode vkKey, bool pressed)
{
	return mockInput->pressKey(vkKey, pressed);
}

void moveMouse(float x, float y)
{
	return mockInput->moveMouse(x, y);
}

void setMouseNorm(float x, float y)
{
	return mockInput->setMouseNorm(x, y);
}

BOOL WriteToConsole(in_string command)
{
	return mockInput->WriteToConsole(command);
}

BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType)
{
	return mockInput->ConsoleCtrlHandler(dwCtrlType);
}

// just setting up the console with standard stuff
void initConsole()
{
	return mockInput->initConsole();
}

std::tuple<std::string, std::string> GetActiveWindowName()
{
	return mockInput->GetActiveWindowName();
}

std::vector<std::string> ListDirectory(std::string directory)
{
	return mockInput->ListDirectory(directory);
}

std::string GetCWD()
{
	return mockInput->GetCWD();
}

bool SetCWD(in_string newCWD)
{
	return mockInput->SetCWD(newCWD);
}

DWORD ShowOnlineHelp()
{
	return mockInput->ShowOnlineHelp();
}

void HideConsole()
{
	return mockInput->HideConsole();
}

void UnhideConsole()
{
	return mockInput->UnhideConsole();
}

void ShowConsole()
{
	return mockInput->ShowConsole();
}

void ReleaseConsole()
{
	return mockInput->ReleaseConsole();
}

bool IsVisible()
{
	return mockInput->IsVisible();
}

bool isConsoleMinimized()
{
	return mockInput->isConsoleMinimized();
}
