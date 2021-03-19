#define DLLEXPORT __declspec(dllexport)

#include "../include/WindowsGamingInput.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <shared_mutex>
#include <string>

#include <unordered_map>
#include <vector>
#include <queue>

#include <roapi.h>
#include <wrl.h>
#include <windows.gaming.input.h>

using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::Gaming::Input;
using namespace ABI::Windows::Devices::Haptics;
using namespace Microsoft::WRL;
using namespace Wrappers;

RoInitializeWrapper g_ro{RO_INIT_MULTITHREADED};
std::mutex g_cb_mutex;
std::vector<WindowsGamingInput::ControllerChanged_t> g_callbacks;

#pragma region Gamepad
IGamepadStatics* g_gamepad_statics = nullptr;
using GamepadPtr = ComPtr<IGamepad>;
std::vector<GamepadPtr> g_gamepads;
std::shared_mutex g_gamepad_mutex;

void ScanGamepads()
{
	ComPtr<IVectorView<Gamepad*>> gamepads;
	auto hr = g_gamepad_statics->get_Gamepads(&gamepads);
	assert(SUCCEEDED(hr));

	uint32_t count = 0;
	hr = gamepads->get_Size(&count);
	assert(SUCCEEDED(hr));

#ifdef _DEBUG
	std::cout << count << " gamepads are connected" << std::endl;
#endif

	for (uint32_t i = 0; i < count; ++i)
	{
		ComPtr<IGamepad> gamepad;
		hr = gamepads->GetAt(i, &gamepad);
		assert(SUCCEEDED(hr));

		std::unique_lock lock(g_gamepad_mutex);
		const auto it = std::ranges::find(std::as_const(g_gamepads), gamepad);
		if (it == g_gamepads.cend())
		{
			g_gamepads.emplace_back(gamepad);
#ifdef _DEBUG
			std::cout << "inserted new gamepad" << std::endl;
#endif
			lock.unlock();

			std::scoped_lock cb_lock(g_cb_mutex);
			for (const auto& cb : g_callbacks)
			{
				cb(WindowsGamingInput::EventType::ControllerAdded, WindowsGamingInput::ControllerType::Gamepad, g_gamepads.size() - 1);
			}
		}
	}
}

EventRegistrationToken g_add_gamepad_token;
HRESULT OnGamepadAdded(IInspectable* sender, IGamepad* gamepad)
{
#ifdef _DEBUG
	std::cout << "OnGamepadAdded" << std::endl;
#endif

	const ComPtr<IGamepad> ptr{ gamepad };
	
	std::unique_lock lock(g_gamepad_mutex);
	const auto it = std::ranges::find(std::as_const(g_gamepads), ptr);
	if (it != g_gamepads.cend())
		return S_OK;
	
	size_t index = (size_t)-1;
	// check if we still got a free index in our internal list
	for(size_t i = 0; i < g_gamepads.size(); ++i)
	{
		if (!g_gamepads[i])
		{
			g_gamepads[i] = ptr;
#ifdef _DEBUG
			std::cout << "OnGamepadAdded:inserted new gamepad at empty index" << std::endl;
#endif
			index = i;
		}
	}
	
	// no free index
	if (index == (size_t)-1)
	{
		g_gamepads.emplace_back(gamepad);
		index = g_gamepads.size() - 1;
#ifdef _DEBUG
		std::cout << "inserted new gamepad at the end" << std::endl;
#endif
	}

	lock.unlock();
	
	std::scoped_lock cb_lock(g_cb_mutex);
	for (const auto& cb : g_callbacks)
	{
		cb(WindowsGamingInput::EventType::ControllerAdded, WindowsGamingInput::ControllerType::Gamepad, index);
	}

	return S_OK;
}

EventRegistrationToken g_remove_gamepad_token;
HRESULT OnGamepadRemoved(IInspectable* sender, IGamepad* gamepad)
{
#ifdef _DEBUG
	std::cout << "OnGamepadRemoved" << std::endl;
#endif

	std::unique_lock lock(g_gamepad_mutex);
	for (size_t i = 0; i < g_gamepads.size(); ++i)
	{
		if(g_gamepads[i].Get() == gamepad)
		{
			g_gamepads[i].Reset();
#ifdef _DEBUG
			std::cout << "OnGamepadRemoved: removed known gamepad from internal list" << std::endl;
#endif
			lock.unlock();

			std::scoped_lock cb_lock(g_cb_mutex);
			for (const auto& cb : g_callbacks)
			{
				cb(WindowsGamingInput::EventType::ControllerRemoved, WindowsGamingInput::ControllerType::Gamepad, i);
			}
			
			break;
		}
	}
	
	return S_OK;
}
#pragma endregion

#pragma region RawGameController
IRawGameControllerStatics* g_rcontroller_statics = nullptr;
using RControllerPtr = ComPtr<IRawGameController>;

struct wstring_hash
{
	using is_transparent = void;
	using key_equal = std::equal_to<>;
	using hash_type = std::hash<std::wstring_view>;
	size_t operator()(std::wstring_view str) const { return hash_type{}(str); }
	size_t operator()(const std::wstring& str) const { return hash_type{}(str); }
	size_t operator()(const wchar_t* str) const { return hash_type{}(str); }
};

std::unordered_map<std::wstring, RControllerPtr, wstring_hash, wstring_hash::key_equal> g_rcontrollers;
std::shared_mutex g_rcontroller_mutex;

// https://docs.microsoft.com/en-us/uwp/api/windows.devices.haptics.knownsimplehapticscontrollerwaveforms
// ABI::Windows::Devices::Haptics::IKnownSimpleHapticsControllerWaveformsStatics::get_RumbleContinuous()
constexpr uint16_t kRumbleContinuous = 0x1005;

void ScanRawGameControllers()
{
	ComPtr<IVectorView<RawGameController*>> controllers;
	auto hr = g_rcontroller_statics->get_RawGameControllers(&controllers);
	assert(SUCCEEDED(hr));

	uint32_t count;
	hr = controllers->get_Size(&count);
	assert(SUCCEEDED(hr));

#ifdef _DEBUG
	std::cout << count << " controllers are connected" << std::endl;
#endif

	// check for all connected controllers
	for (uint32_t i = 0; i < count; ++i)
	{
		ComPtr<IRawGameController> controller;
		hr = controllers->GetAt(i, &controller);
		assert(SUCCEEDED(hr));

		ComPtr<IRawGameController2> controller2;
		hr = controller.As(&controller2);
		if (FAILED(hr)) // I guess shouldn't fail, idk (?)
			continue;

		HSTRING tmp_name;
		hr = controller2->get_NonRoamableId(&tmp_name);
		if (FAILED(hr))
			continue;

		std::wstring name = WindowsGetStringRawBuffer(tmp_name, nullptr);
		if (name.empty())
			continue;

		std::unique_lock lock(g_rcontroller_mutex);
		if (!g_rcontrollers.contains(name))
		{
			g_rcontrollers.emplace(name, controller);
			lock.unlock();
#ifdef _DEBUG
			std::wcout << L"inserted new controller with uid: " << name << std::endl;
#endif

			std::scoped_lock cb_lock(g_cb_mutex);
			for (const auto& cb : g_callbacks)
			{
				cb(WindowsGamingInput::EventType::ControllerAdded, WindowsGamingInput::ControllerType::RawController, name);
			}
		}
	}
}

EventRegistrationToken g_add_rcontroller_token;

HRESULT OnRawGameControllerAdded(IInspectable* sender, IRawGameController* controller)
{
#ifdef _DEBUG
	std::cout << "OnRawGameControllerAdded" << std::endl;
#endif

	ComPtr<IRawGameController2> controller2;
	HRESULT hr = controller->QueryInterface(__uuidof(IRawGameController2), &controller2);
	if (SUCCEEDED(hr))
	{
		HSTRING tmp_name;
		hr = controller2->get_NonRoamableId(&tmp_name);
		if (SUCCEEDED(hr))
		{
			const std::wstring name = WindowsGetStringRawBuffer(tmp_name, nullptr);
			std::unique_lock lock(g_rcontroller_mutex);
			if (!g_rcontrollers.contains(name))
			{
				g_rcontrollers.emplace(name, controller);
#ifdef _DEBUG
				std::wcout << L"OnRawGameControllerAdded: added new controller with uid: " << name << std::endl;
#endif
				lock.unlock();
				
				std::scoped_lock cb_lock(g_cb_mutex);
				for (const auto& cb : g_callbacks)
				{
					cb(WindowsGamingInput::EventType::ControllerAdded, WindowsGamingInput::ControllerType::RawController, name);
				}
			}
		}
	}
	
	return S_OK;
}

EventRegistrationToken g_remove_rcontroller_token;

HRESULT OnRawGameControllerRemoved(IInspectable* sender, IRawGameController* controller)
{
#ifdef _DEBUG
	std::cout << "OnRawGameControllerRemoved" << std::endl;
#endif

	ComPtr<IRawGameController2> controller2;
	HRESULT hr = controller->QueryInterface(IID_PPV_ARGS(&controller2));
	if (SUCCEEDED(hr))
	{
		HSTRING tmp_name;
		hr = controller2->get_NonRoamableId(&tmp_name);
		if (SUCCEEDED(hr))
		{
			const std::wstring name = WindowsGetStringRawBuffer(tmp_name, nullptr);

			std::unique_lock lock(g_rcontroller_mutex);
			const auto erased = g_rcontrollers.erase(name) == 1;
			lock.unlock();
#ifdef _DEBUG
			std::cout << "OnRawGameControllerRemoved: removed known controller: " << erased << std::endl;
#endif
			
			std::scoped_lock cb_lock(g_cb_mutex);
			for (const auto& cb : g_callbacks)
			{
				cb(WindowsGamingInput::EventType::ControllerRemoved, WindowsGamingInput::ControllerType::RawController, name);
			}
		}
	}
	
	return S_OK;
}
#pragma endregion

BOOL WINAPI DllMain(HINSTANCE hinstance, DWORD reason, LPVOID reserved)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		std::thread([]()
		{
			if (!g_gamepad_statics)
			{
				auto hr = RoGetActivationFactory(HStringReference(L"Windows.Gaming.Input.Gamepad").Get(),
				                                 __uuidof(IGamepadStatics), (void**)&g_gamepad_statics);
				assert(SUCCEEDED(hr));

				hr = g_gamepad_statics->add_GamepadAdded(
					Callback<__FIEventHandler_1_Windows__CGaming__CInput__CGamepad>(OnGamepadAdded).Get(),
					&g_add_gamepad_token);
				assert(SUCCEEDED(hr));

				hr = g_gamepad_statics->add_GamepadRemoved(
					Callback<__FIEventHandler_1_Windows__CGaming__CInput__CGamepad>(OnGamepadRemoved).Get(),
					&g_remove_gamepad_token);
				assert(SUCCEEDED(hr));

#ifdef _DEBUG
				std::cout << "Windows.Gaming.Input.Gamepad initialized" << std::endl;
#endif
			}
			
			ScanGamepads();
			
			if (!g_rcontroller_statics)
			{
				auto hr = RoGetActivationFactory(HStringReference(L"Windows.Gaming.Input.RawGameController").Get(),
				                                 __uuidof(IRawGameControllerStatics), (void**)&g_rcontroller_statics);
				assert(SUCCEEDED(hr));

				hr = g_rcontroller_statics->add_RawGameControllerAdded(
					Callback<__FIEventHandler_1_Windows__CGaming__CInput__CRawGameController>(OnRawGameControllerAdded).
					Get(),
					&g_add_rcontroller_token);
				assert(SUCCEEDED(hr));

				hr = g_rcontroller_statics->add_RawGameControllerRemoved(
					Callback<__FIEventHandler_1_Windows__CGaming__CInput__CRawGameController>(
						OnRawGameControllerRemoved).Get(),
					&g_remove_rcontroller_token);
				assert(SUCCEEDED(hr));

#ifdef _DEBUG
				std::cout << "Windows.Gaming.Input.RawGameController initialized" << std::endl;
#endif
			}
			ScanRawGameControllers();
		}).detach();
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
		// callbacks detach
		{
			std::scoped_lock lock(g_cb_mutex);
			g_callbacks.clear();
		}
				
		// gamepad detach
		{
			std::scoped_lock lock(g_gamepad_mutex);
			g_gamepads.clear();
			if (g_gamepad_statics)
			{
				g_gamepad_statics->remove_GamepadAdded(g_add_rcontroller_token);
				g_gamepad_statics->remove_GamepadRemoved(g_remove_rcontroller_token);
			}
		}
		

		// raw game controller detach
		{
			std::scoped_lock lock(g_rcontroller_mutex);
			g_rcontrollers.clear();
			if (g_rcontroller_statics)
			{
				g_rcontroller_statics->remove_RawGameControllerAdded(g_add_rcontroller_token);
				g_rcontroller_statics->remove_RawGameControllerRemoved(g_remove_rcontroller_token);
			}
		}
	}

	return TRUE;
}

namespace WindowsGamingInput
{
	void AddControllerChanged(ControllerChanged_t cb)
	{
		std::scoped_lock lock(g_cb_mutex);
		if(std::ranges::find(std::as_const(g_callbacks), cb) == g_callbacks.cend())
			g_callbacks.emplace_back(cb);
	}

	void RemoveControllerChanged(ControllerChanged_t cb)
	{
		std::scoped_lock lock(g_cb_mutex);
		const auto rm = std::ranges::remove(g_callbacks, cb);
		g_callbacks.erase(rm.begin(), rm.end());
	}
	
	bool GetBatteryInfo(ComPtr<IGameControllerBatteryInfo> battery_info, BatteryStatus& status, double& battery)
	{
		ComPtr<ABI::Windows::Devices::Power::IBatteryReport> report;
		HRESULT hr = battery_info->TryGetBatteryReport(&report);
		if (FAILED(hr) || !report)
			return false;

		static_assert(sizeof(BatteryStatus) == sizeof(ABI::Windows::System::Power::BatteryStatus));
		hr = report->get_Status((ABI::Windows::System::Power::BatteryStatus*)&status);
		assert(SUCCEEDED(hr));

		ComPtr<__FIReference_1_int> remaining_ptr, full_ptr;
		report->get_RemainingCapacityInMilliwattHours(&remaining_ptr);
		report->get_FullChargeCapacityInMilliwattHours(&full_ptr);

		int remaining = 0, full = 0;
		if (remaining_ptr)
		{
			hr = remaining_ptr->get_Value(&remaining);
			assert(SUCCEEDED(hr));
		}
		
		if (full_ptr)
		{
			hr = full_ptr->get_Value(&full);
			assert(SUCCEEDED(hr));
		}

		// remaining is always 100 when connected and status discharching (?!) -> check for IsWireless before
		battery = full <= 0 ? 0 : static_cast<double>(remaining) / static_cast<double>(full);
		return true;
	}
	
	namespace Gamepad
	{
		size_t GetCount()
		{
			std::shared_lock lock(g_gamepad_mutex);
			return g_gamepads.size();
		}

		bool IsConnected(size_t index)
		{
			GamepadState tmp;
			return GetState(index, tmp);
		}

		bool GetState(size_t index, GamepadState& state)
		{
			std::shared_lock lock(g_gamepad_mutex);
			if (index >= g_gamepads.size())
				return false;

			const auto gamepad = g_gamepads[index];
			lock.unlock();

			if (!gamepad)
				return false;

			return SUCCEEDED(gamepad->GetCurrentReading((GamepadReading*)&state));
		}

		bool SetVibration(size_t index, const Vibration& vibration)
		{
			std::shared_lock lock(g_gamepad_mutex);
			if (index >= g_gamepads.size())
				return false;

			const auto gamepad = g_gamepads[index];
			lock.unlock();

			if (!gamepad)
				return false;

			static_assert(sizeof(Vibration) == sizeof(GamepadVibration));
			GamepadVibration tmp;
			memcpy(&tmp, &vibration, sizeof(GamepadVibration));
			return SUCCEEDED(gamepad->put_Vibration(tmp));
		}

		bool GetVibration(size_t index, Vibration& vibration)
		{
			std::shared_lock lock(g_gamepad_mutex);
			if (index >= g_gamepads.size())
				return false;

			const auto gamepad = g_gamepads[index];
			lock.unlock();

			if (!gamepad)
				return false;

			static_assert(sizeof(Vibration) == sizeof(GamepadVibration));
			return SUCCEEDED(gamepad->get_Vibration((GamepadVibration*)&vibration));
		}

		bool IsWireless(size_t index, bool& wireless)
		{
			std::shared_lock lock(g_gamepad_mutex);
			if (index >= g_gamepads.size())
				return false;

			const auto gamepad = g_gamepads[index];
			lock.unlock();

			if (!gamepad)
				return false;

			ComPtr<IGameController> controller;
			auto hr = gamepad.As(&controller);
			assert(SUCCEEDED(hr));

			static_assert(sizeof(bool) == sizeof(boolean));
			return SUCCEEDED(controller->get_IsWireless((boolean*)&wireless));
		}

		bool GetBatteryStatus(size_t index, BatteryStatus& status, double& battery)
		{
			std::shared_lock lock(g_gamepad_mutex);
			if (index >= g_gamepads.size())
				return false;

			const auto gamepad = g_gamepads[index];
			lock.unlock();

			if (!gamepad)
				return false;

			ComPtr<IGameControllerBatteryInfo> battery_info;
			auto hr = gamepad.As(&battery_info);
			assert(SUCCEEDED(hr));

			return GetBatteryInfo(battery_info, status, battery);
		}
	}

	namespace RawGameController
	{
		size_t GetCount()
		{
			std::shared_lock lock(g_rcontroller_mutex);
			return g_rcontrollers.size();
		}

		size_t GetControllers(RawController::Description* controllers, size_t count)
		{
			std::shared_lock lock(g_rcontroller_mutex);
			if (controllers == nullptr)
				return g_rcontrollers.size(); // return size if no buffer have been given

			size_t result = 0;
			for (const auto& kv : g_rcontrollers)
			{
				if (result >= count)
					break;

				ComPtr<IRawGameController2> controller2;
				kv.second.As(&controller2);

				HSTRING tmp_name;
				controller2->get_DisplayName(&tmp_name);

				const std::wstring name = WindowsGetStringRawBuffer(tmp_name, nullptr);

				wcscpy_s(controllers[result].uid, kv.first.c_str());
				wcscpy_s(controllers[result].display_name, name.c_str());

				controllers[result].axis_count = 0;
				kv.second->get_AxisCount((int*)&controllers[result].axis_count);

				controllers[result].button_count = 0;
				kv.second->get_ButtonCount((int*)&controllers[result].button_count);

				controllers[result].switches_count = 0;
				kv.second->get_SwitchCount((int*)&controllers[result].switches_count);

				++result;
			}

			return result;
		}

		bool GetController(std::wstring_view uid, RawController::Description& description)
		{
			std::shared_lock lock(g_rcontroller_mutex);
			const auto it = g_rcontrollers.find(uid);
			if (it == g_rcontrollers.cend())
				return false;

			auto controller = it->second;
			lock.unlock();

			description.axis_count = 0;
			controller->get_AxisCount((int*)&description.axis_count);

			description.button_count = 0;
			controller->get_ButtonCount((int*)&description.button_count);

			description.switches_count = 0;
			controller->get_SwitchCount((int*)&description.switches_count);
			return true;
		}

		bool IsConnected(std::wstring_view uid)
		{
			std::shared_lock lock(g_rcontroller_mutex);
			const auto it = g_rcontrollers.find(uid);
			return it != g_rcontrollers.cend();
		}

		bool GetState(std::wstring_view uid, bool* buttons, size_t button_count, SwitchPosition* switches, size_t switch_count, double* axis, size_t axis_count, uint64_t& timestamp)
		{
			std::shared_lock lock(g_rcontroller_mutex);
			const auto it = g_rcontrollers.find(uid);
			if (it == g_rcontrollers.cend())
				return false;

			ComPtr<IRawGameController> controller;
			auto hr = it->second.As(&controller);
			assert(SUCCEEDED(hr));
			lock.unlock();

			static_assert(sizeof(bool) == sizeof(boolean));
			hr = controller->GetCurrentReading((uint32_t)button_count, (boolean*)buttons, (uint32_t)switch_count, (GameControllerSwitchPosition*)switches, (uint32_t)axis_count, (double*)axis, &timestamp);
			return SUCCEEDED(hr);
		}

		bool HasVibration(std::wstring_view uid)
		{
			std::shared_lock lock(g_rcontroller_mutex);
			const auto it = g_rcontrollers.find(uid);
			if (it == g_rcontrollers.cend())
				return false;

			ComPtr<IRawGameController2> controller;
			auto hr = it->second.As(&controller);
			assert(SUCCEEDED(hr));
			lock.unlock();

			ComPtr<IVectorView<SimpleHapticsController*>> haptics;
			hr = controller->get_SimpleHapticsControllers(&haptics);
			if (FAILED(hr))
				return false;

			uint32_t count = 0;
			haptics->get_Size(&count); // motor_count (?)

			for (uint32_t i = 0; i < count; ++i)
			{
				ISimpleHapticsController* haptic;
				haptics->GetAt(i, &haptic);

				ComPtr<IVectorView<SimpleHapticsControllerFeedback*>> feedbacks;
				hr = haptic->get_SupportedFeedback(&feedbacks);
				if (FAILED(hr))
					return false;

				uint32_t feedback_count = 0;
				feedbacks->get_Size(&feedback_count);
				for (uint32_t j = 0; j < feedback_count; ++j)
				{
					ISimpleHapticsControllerFeedback* feedback;
					feedbacks->GetAt(j, &feedback);

					uint16_t waveform = 0;
					feedback->get_Waveform(&waveform);
					if (waveform == kRumbleContinuous)
						return true;
				}
			}

			return false;
		}

		bool SetVibration(std::wstring_view uid, double vibration)
		{
			std::shared_lock lock(g_rcontroller_mutex);
			const auto it = g_rcontrollers.find(uid);
			if (it == g_rcontrollers.cend())
				return false;

			ComPtr<IRawGameController2> controller;
			auto hr = it->second.As(&controller);
			assert(SUCCEEDED(hr));
			lock.unlock();
			
			ComPtr<IVectorView<SimpleHapticsController*>> haptics;
			hr = controller->get_SimpleHapticsControllers(&haptics);
			assert(SUCCEEDED(hr));

			bool result = false;
			uint32_t count = 0;
			haptics->get_Size(&count);
			for (uint32_t i = 0; i < count; ++i)
			{
				ISimpleHapticsController* haptic;
				haptics->GetAt(i, &haptic);

				if (vibration <= 0.000001)
				{
					haptic->StopFeedback();
					continue;
				}

				ComPtr<IVectorView<SimpleHapticsControllerFeedback*>> feedbacks;
				hr = haptic->get_SupportedFeedback(&feedbacks);
				if (FAILED(hr))
					return false;

				uint32_t feedback_count = 0;
				feedbacks->get_Size(&feedback_count);
				for (uint32_t j = 0; j < feedback_count; ++j)
				{
					ISimpleHapticsControllerFeedback* feedback;
					feedbacks->GetAt(j, &feedback);

					uint16_t waveform = 0;
					feedback->get_Waveform(&waveform);
					if (waveform == kRumbleContinuous)
					{
						haptic->SendHapticFeedbackWithIntensity(feedback, vibration);
						result = true;
						break;
					}
				}
			}
			return result;
		}

		bool IsVibrating(std::wstring_view uid)
		{
			std::shared_lock lock(g_rcontroller_mutex);
			const auto it = g_rcontrollers.find(uid);
			if (it == g_rcontrollers.cend())
				return false;

			ComPtr<IRawGameController2> controller;
			auto hr = it->second.As(&controller);
			assert(SUCCEEDED(hr));
			lock.unlock();

			ComPtr<IVectorView<SimpleHapticsController*>> haptics;
			hr = controller->get_SimpleHapticsControllers(&haptics);
			assert(SUCCEEDED(hr));

			uint32_t count = 0;
			haptics->get_Size(&count);
			for (uint32_t i = 0; i < count; ++i)
			{
				ISimpleHapticsController* haptic;
				haptics->GetAt(i, &haptic);

				ComPtr<IVectorView<SimpleHapticsControllerFeedback*>> feedbacks;
				hr = haptic->get_SupportedFeedback(&feedbacks);
				if (FAILED(hr))
					return false;

				uint32_t feedback_count = 0;
				feedbacks->get_Size(&feedback_count);
				for (uint32_t j = 0; j < feedback_count; ++j)
				{
					ISimpleHapticsControllerFeedback* feedback;
					feedbacks->GetAt(j, &feedback);

					uint16_t waveform = 0;
					feedback->get_Waveform(&waveform);
					if (waveform == kRumbleContinuous)
					{
						ABI::Windows::Foundation::TimeSpan ts{};
						feedback->get_Duration(&ts);
						if (ts.Duration != 0)
							return true;
					}
				}
			}
			return false;
		}

		bool IsWireless(std::wstring_view uid, bool& wireless)
		{
			std::shared_lock lock(g_rcontroller_mutex);
			const auto it = g_rcontrollers.find(uid);
			if (it == g_rcontrollers.cend())
				return false;

			ComPtr<IGameController> controller;
			auto hr = it->second.As(&controller);
			assert(SUCCEEDED(hr));
			lock.unlock();

			static_assert(sizeof(bool) == sizeof(boolean));
			return SUCCEEDED(controller->get_IsWireless((boolean*)&wireless));
		}

		bool GetBatteryStatus(std::wstring_view uid, BatteryStatus& status, double& battery)
		{
			std::shared_lock lock(g_rcontroller_mutex);
			const auto it = g_rcontrollers.find(uid);
			if (it == g_rcontrollers.cend())
				return false;

			ComPtr<IGameController> controller;
			auto hr = it->second.As(&controller);
			assert(SUCCEEDED(hr));
			lock.unlock();

			ComPtr<IGameControllerBatteryInfo> battery_info;
			hr = controller.As(&battery_info);
			if(FAILED(hr) || !battery_info)
				return false;
			
			return GetBatteryInfo(battery_info, status, battery);
		}

		bool GetButtonLabel(std::wstring_view uid, size_t button, ButtonLabel& label)
		{
			std::shared_lock lock(g_rcontroller_mutex);
			const auto it = g_rcontrollers.find(uid);
			if (it == g_rcontrollers.cend())
				return false;

			auto controller = it->second;
			lock.unlock();

			int max_count = 0;
			controller->get_ButtonCount(&max_count);
			if ((int)button >= max_count)
				return false;

			static_assert(sizeof(ButtonLabel) == sizeof(GameControllerButtonLabel));
			return SUCCEEDED(controller->GetButtonLabel((int)button, (GameControllerButtonLabel*)&label));
		}
	}
}
