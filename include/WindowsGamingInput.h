#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <variant>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NO_MIN_MAX
#define NO_MIN_MAX
#endif

#include <Windows.h>

#ifndef DLLEXPORT
#define DLLEXPORT 
#endif

namespace WindowsGamingInput
{
	// == ABI::Windows::Gaming::Input::GamepadButtons
	enum class GamepadButtons : unsigned int
	{
		None = 0,
		Menu = 0x1,
		View = 0x2,
		A = 0x4,
		B = 0x8,
		X = 0x10,
		Y = 0x20,
		DPadUp = 0x40,
		DPadDown = 0x80,
		DPadLeft = 0x100,
		DPadRight = 0x200,
		LeftShoulder = 0x400,
		RightShoulder = 0x800,
		LeftThumbstick = 0x1000,
		RightThumbstick = 0x2000,
		Paddle1 = 0x4000,
		Paddle2 = 0x8000,
		Paddle3 = 0x10000,
		Paddle4 = 0x20000,
	};

	DEFINE_ENUM_FLAG_OPERATORS(GamepadButtons)

	// == ABI::Windows::Gaming::Input::GamepadReading
	struct GamepadState
	{
		uint64_t Timestamp;
		GamepadButtons Buttons;
		double LeftTrigger;
		double RightTrigger;
		double LeftThumbstickX;
		double LeftThumbstickY;
		double RightThumbstickX;
		double RightThumbstickY;
	};

	// == ABI::Windows::Gaming::Input::GamepadVibration
	struct Vibration
	{
		double LeftMotor = 0;
		double RightMotor = 0;
		double LeftTrigger = 0;
		double RightTrigger = 0;
	};

	// == ABI::Windows::System::Power::BatteryStatus
	enum class BatteryStatus
	{
		NotPresent = 0,
		Discharging = 1,
		Idle = 2,
		Charging = 3,
	};

	// == ABI::Windows::Gaming::Input::GameControllerSwitchPosition
	enum class SwitchPosition
	{
		Center = 0,
		Up = 1,
		UpRight = 2,
		Right = 3,
		DownRight = 4,
		Down = 5,
		DownLeft = 6,
		Left = 7,
		UpLeft = 8,
	};

	enum class ButtonLabel : int // == ABI::Windows::Gaming::Input::GameControllerButtonLabel
	{
		None = 0,
		XboxBack = 1,
		XboxStart = 2,
		XboxMenu = 3,
		XboxView = 4,
		XboxUp = 5,
		XboxDown = 6,
		XboxLeft = 7,
		XboxRight = 8,
		XboxA = 9,
		XboxB = 10,
		XboxX = 11,
		XboxY = 12,
		XboxLeftBumper = 13,
		XboxLeftTrigger = 14,
		XboxLeftStickButton = 15,
		XboxRightBumper = 16,
		XboxRightTrigger = 17,
		XboxRightStickButton = 18,
		XboxPaddle1 = 19,
		XboxPaddle2 = 20,
		XboxPaddle3 = 21,
		XboxPaddle4 = 22,
		Mode = 23,
		Select = 24,
		Menu = 25,
		View = 26,
		Back = 27,
		Start = 28,
		Options = 29,
		Share = 30,
		Up = 31,
		Down = 32,
		Left = 33,
		Right = 34,
		LetterA = 35,
		LetterB = 36,
		LetterC = 37,
		LetterL = 38,
		LetterR = 39,
		LetterX = 40,
		LetterY = 41,
		LetterZ = 42,
		Cross = 43,
		Circle = 44,
		Square = 45,
		Triangle = 46,
		LeftBumper = 47,
		LeftTrigger = 48,
		LeftStickButton = 49,
		Left1 = 50,
		Left2 = 51,
		Left3 = 52,
		RightBumper = 53,
		RightTrigger = 54,
		RightStickButton = 55,
		Right1 = 56,
		Right2 = 57,
		Right3 = 58,
		Paddle1 = 59,
		Paddle2 = 60,
		Paddle3 = 61,
		Paddle4 = 62,
		Plus = 63,
		Minus = 64,
		DownLeftArrow = 65,
		DialLeft = 66,
		DialRight = 67,
		Suspension = 68,
	};

	enum class ControllerType
	{
		RawController,
		Gamepad,
	};

	enum class EventType
	{
		ControllerAdded,
		ControllerRemoved,
	};

	using ControllerChanged_t = void (*)(EventType type, ControllerType controller, std::variant<size_t, std::wstring_view> uid);
	DLLEXPORT void AddControllerChanged(ControllerChanged_t cb);
	DLLEXPORT void RemoveControllerChanged(ControllerChanged_t cb);

	namespace Gamepad
	{
		DLLEXPORT size_t GetCount();
		DLLEXPORT bool IsConnected(size_t index);
		DLLEXPORT bool GetState(size_t index, GamepadState& state);

		DLLEXPORT bool SetVibration(size_t index, const Vibration& vibration);
		DLLEXPORT bool GetVibration(size_t index, Vibration& vibration);

		DLLEXPORT bool IsWireless(size_t index, bool& wireless);
		DLLEXPORT bool GetBatteryStatus(size_t index, BatteryStatus& status, double& battery);
	}
	
	namespace RawController
	{
		struct Description
		{
			wchar_t uid[256];
			wchar_t display_name[256];

			size_t button_count;
			size_t switches_count;
			size_t axis_count;
		};
		// <uid, display_name>
		DLLEXPORT size_t GetCount();
		DLLEXPORT size_t GetControllers(Description* controllers, size_t count);
		DLLEXPORT bool GetController(std::wstring_view uid, Description& description);
		DLLEXPORT bool GetButtonLabel(std::wstring_view uid, size_t button, ButtonLabel& label);
		
		DLLEXPORT bool IsConnected(std::wstring_view uid);
		DLLEXPORT bool GetState(std::wstring_view uid, bool* buttons, size_t button_count, SwitchPosition* switches, size_t switch_count, double* axis, size_t axis_count, uint64_t& timestamp);

		DLLEXPORT bool SetVibration(std::wstring_view uid, double vibration);
		DLLEXPORT bool IsVibrating(std::wstring_view uid);
		DLLEXPORT bool HasVibration(std::wstring_view uid);

		DLLEXPORT bool IsWireless(std::wstring_view uid, bool& wireless);
		DLLEXPORT bool GetBatteryStatus(std::wstring_view uid, BatteryStatus& status, double& battery);
	}
}

