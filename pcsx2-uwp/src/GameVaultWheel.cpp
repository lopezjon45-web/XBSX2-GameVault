/* Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
 * GPL-3.0-or-later, like PCSX2 / XBSX2.
 *
 * Racing wheels on the Xbox. A wheel (Logitech G920/G923 Xbox, Thrustmaster, Fanatec, MOZA R3/ESX ...) is not a
 * gamepad, so SDL's gamepad bindings never see it, and PCSX2's own wheel force feedback goes through SDL haptics,
 * which do not exist here. Windows.Gaming.Input.RacingWheel gives the wheel, pedals and buttons in fixed units on
 * every Xbox wheel, so it is read here directly:
 * - when a wheel shows up and USB port 1 is empty (or was set by us), port 1 becomes PCSX2's emulated Logitech
 *   Driving Force Pro, the wheel Gran Turismo 3/4, Enthusia, Tourist Trophy and others support natively;
 * - every input poll copies the wheel into that device (no binding setup for the player);
 * - the game's force feedback goes to the wheel's motor where it has one.
 * When the last wheel leaves, a port 1 we set goes back to empty. A player's own USB setup is never touched.
 */
#include "pcsx2/PrecompiledHeader.h"

#include "GameVaultWheel.h"

#include "pcsx2/Host.h"
#include "pcsx2/VMManager.h"
#include "pcsx2/USB/USB.h"
#include "pcsx2/USB/usb-pad/usb-pad.h"
#include "common/Console.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.Gaming.Input.ForceFeedback.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace winrt::Windows::Gaming::Input;
using namespace winrt::Windows::Gaming::Input::ForceFeedback;
namespace wgi = winrt::Windows::Gaming::Input;

namespace
{
	constexpr const char* kSection = "USB1";   // USB port 1, the one Gran Turismo looks at
	constexpr const char* kDevice = "Pad";     // PCSX2's "Wheel Device"
	constexpr unsigned kDrivingForcePro = 1;   // usb_pad::WT_DRIVING_FORCE_PRO
	constexpr const char* kAutoSection = "GameVault";
	constexpr const char* kAutoKey = "WheelAuto";   // port 1 was set by us, so it may be cleared by us

	std::mutex s_lock;
	std::vector<RacingWheel> s_wheels;   // in the order they arrived
	std::atomic<bool> s_started{false};
	std::atomic<bool> s_port_is_ours{false};   // port 1 is our Driving Force Pro right now
	bool s_logged_first_input = false;

	std::string WheelName(const RacingWheel& w)
	{
		try
		{
			const RawGameController raw = RawGameController::FromGameController(w);
			if (raw)
			{
				std::string name = winrt::to_string(raw.DisplayName());
				char ids[32];
				snprintf(ids, sizeof(ids), " (%04X:%04X)", raw.HardwareVendorId(), raw.HardwareProductId());
				return (name.empty() ? std::string("racing wheel") : name) + ids;
			}
		}
		catch (...)
		{
		}
		return "racing wheel";
	}

	// CPU thread: port 1 follows the wheels (only when it is empty or ours)
	void UpdatePort()
	{
		bool have_wheel;
		{
			std::lock_guard<std::mutex> lk(s_lock);
			have_wheel = !s_wheels.empty();
		}
		const std::string type = Host::GetBaseStringSettingValue(kSection, "Type", "None");
		const bool ours = Host::GetBaseBoolSettingValue(kAutoSection, kAutoKey, false);
		bool changed = false;
		if (have_wheel)
		{
			if (type == "None" || type.empty() || ours)
			{
				if (type != kDevice || Host::GetBaseUIntSettingValue(kSection, "Pad_subtype", 0) != kDrivingForcePro ||
					Host::GetBaseStringSettingValue(kSection, "Pad_FFDevice") != GameVaultWheel::kFFDeviceName)
				{
					Host::SetBaseStringSettingValue(kSection, "Type", kDevice);
					Host::SetBaseUIntSettingValue(kSection, "Pad_subtype", kDrivingForcePro);
					Host::SetBaseStringSettingValue(kSection, "Pad_FFDevice", std::string(GameVaultWheel::kFFDeviceName).c_str());
					Host::SetBaseBoolSettingValue(kAutoSection, kAutoKey, true);
					changed = true;
					Console.WriteLn("Game Vault: wheel: USB port 1 set to Driving Force Pro");
				}
				s_port_is_ours = true;
			}
			else
			{
				Console.WriteLn("Game Vault: wheel: USB port 1 already holds '%s' (player's own), left alone", type.c_str());
				s_port_is_ours = false;
			}
		}
		else
		{
			if (ours && type == kDevice)
			{
				Host::SetBaseStringSettingValue(kSection, "Type", "None");
				Host::RemoveBaseSettingValue(kSection, "Pad_FFDevice");
				Host::SetBaseBoolSettingValue(kAutoSection, kAutoKey, false);
				changed = true;
				Console.WriteLn("Game Vault: wheel: no wheel any more, USB port 1 emptied");
			}
			s_port_is_ours = false;
		}
		if (changed)
		{
			Host::CommitBaseSettingChanges();
			if (VMManager::HasValidVM())
				VMManager::ApplySettings();
		}
	}

	void OnAdded(const RacingWheel& w)
	{
		{
			std::lock_guard<std::mutex> lk(s_lock);
			if (std::find(s_wheels.begin(), s_wheels.end(), w) != s_wheels.end())
				return;
			s_wheels.push_back(w);
		}
		std::string extra;
		try
		{
			extra = " max angle " + std::to_string(static_cast<int>(w.MaxWheelAngle())) + ", motor " +
					(w.WheelMotor() ? "yes" : "no") + ", clutch " + (w.HasClutch() ? "yes" : "no") + ", handbrake " +
					(w.HasHandbrake() ? "yes" : "no") + ", shifter " + (w.HasPatternShifter() ? "yes" : "no");
		}
		catch (...)
		{
		}
		Console.WriteLn("Game Vault: wheel connected: %s,%s", WheelName(w).c_str(), extra.c_str());
		s_logged_first_input = false;
		Host::RunOnCPUThread(UpdatePort);
	}

	void OnRemoved(const RacingWheel& w)
	{
		{
			std::lock_guard<std::mutex> lk(s_lock);
			const auto it = std::find(s_wheels.begin(), s_wheels.end(), w);
			if (it == s_wheels.end())
				return;
			s_wheels.erase(it);
		}
		Console.WriteLn("Game Vault: wheel disconnected");
		Host::RunOnCPUThread(UpdatePort);
	}

	float Pressed(bool b) { return b ? 1.0f : 0.0f; }

	// --- force feedback -----------------------------------------------------------------------------------------
	// PCSX2 hands over SDL haptic units: levels and coefficients -32768..32767, saturation and dead band 0..65535.
	class WheelFF final : public usb_pad::FFDevice
	{
	public:
		explicit WheelFF(ForceFeedbackMotor motor)
			: m_motor(std::move(motor))
		{
			// loading an effect is asynchronous; the CPU thread must not wait for the wheel
			m_loader = std::thread([this]() {
				try
				{
					winrt::init_apartment(winrt::apartment_type::multi_threaded);
				}
				catch (...)
				{
				}
				Load(m_constant);
				Load(m_spring);
				Load(m_damper);
				Load(m_friction);
				m_ready = true;
				Console.WriteLn("Game Vault: wheel force feedback ready (%d of 4 effects loaded)", m_loaded.load());
			});
		}

		~WheelFF() override
		{
			if (m_loader.joinable())
				m_loader.join();
			try
			{
				m_motor.StopAllEffects();
			}
			catch (...)
			{
			}
		}

		void SetConstantForce(int level) override
		{
			if (!m_ready)
				return;
			const float x = std::clamp(level / 32767.0f, -1.0f, 1.0f);
			try
			{
				m_constant.SetParameters({x, 0.0f, 0.0f}, kForever);
				if (!m_constant_on)
				{
					m_constant.Start();
					m_constant_on = true;
				}
			}
			catch (...)
			{
			}
		}

		void SetSpringForce(const usb_pad::parsed_ff_data& ff) override { SetCondition(m_spring, m_spring_on, ff); }
		void SetDamperForce(const usb_pad::parsed_ff_data& ff) override { SetCondition(m_damper, m_damper_on, ff); }
		void SetFrictionForce(const usb_pad::parsed_ff_data& ff) override { SetCondition(m_friction, m_friction_on, ff); }

		// the wheel's own centring is left to the wheel; the game's spring effect does the rest
		void SetAutoCenter(int) override {}

		void DisableForce(usb_pad::EffectID force) override
		{
			if (!m_ready)
				return;
			try
			{
				switch (force)
				{
					case usb_pad::EFF_CONSTANT: m_constant.Stop(); m_constant_on = false; break;
					case usb_pad::EFF_SPRING: m_spring.Stop(); m_spring_on = false; break;
					case usb_pad::EFF_DAMPER: m_damper.Stop(); m_damper_on = false; break;
					case usb_pad::EFF_FRICTION: m_friction.Stop(); m_friction_on = false; break;
					default: break;
				}
			}
			catch (...)
			{
			}
		}

	private:
		static constexpr winrt::Windows::Foundation::TimeSpan kForever{std::chrono::hours(24)};

		template <typename Effect>
		void Load(Effect& e)
		{
			try
			{
				if (m_motor.LoadEffectAsync(e).get() == ForceFeedbackLoadEffectResult::Succeeded)
					m_loaded++;
			}
			catch (...)
			{
			}
		}

		void SetCondition(ConditionForceEffect& e, bool& on, const usb_pad::parsed_ff_data& ff)
		{
			if (!m_ready)
				return;
			const auto& c = ff.u.condition;
			try
			{
				e.SetParameters({1.0f, 0.0f, 0.0f}, std::clamp(c.right_coeff / 32767.0f, -1.0f, 1.0f),
					std::clamp(c.left_coeff / 32767.0f, -1.0f, 1.0f), std::clamp(c.right_saturation / 65535.0f, 0.0f, 1.0f),
					std::clamp(c.left_saturation / 65535.0f, 0.0f, 1.0f), std::clamp(c.deadband / 65535.0f, 0.0f, 1.0f),
					std::clamp(c.center / 32767.0f, -1.0f, 1.0f));
				if (!on)
				{
					e.Start();
					on = true;
				}
			}
			catch (...)
			{
			}
		}

		ForceFeedbackMotor m_motor;
		ConstantForceEffect m_constant;
		ConditionForceEffect m_spring{ConditionForceEffectKind::Spring};
		ConditionForceEffect m_damper{ConditionForceEffectKind::Damper};
		ConditionForceEffect m_friction{ConditionForceEffectKind::Friction};
		bool m_constant_on = false, m_spring_on = false, m_damper_on = false, m_friction_on = false;
		std::atomic<bool> m_ready{false};
		std::atomic<int> m_loaded{0};
		std::thread m_loader;
	};
} // namespace

void GameVaultWheel::Initialize()
{
	if (s_started.exchange(true))
		return;
	try
	{
		RacingWheel::RacingWheelAdded([](const auto&, const RacingWheel& w) { OnAdded(w); });
		RacingWheel::RacingWheelRemoved([](const auto&, const RacingWheel& w) { OnRemoved(w); });
		for (const RacingWheel& w : RacingWheel::RacingWheels())   // usually empty this early; Added follows
			OnAdded(w);
	}
	catch (...)
	{
		Console.Warning("Game Vault: wheel: racing wheel support could not start");
	}
	// a port 1 we set at an earlier start, with no wheel now: emptied (the Added event puts it back)
	Host::RunOnCPUThread(UpdatePort);
}

void GameVaultWheel::Poll()
{
	if (!s_started)
		Initialize();
	if (!s_port_is_ours)
		return;

	RacingWheel w{nullptr};
	{
		std::lock_guard<std::mutex> lk(s_lock);
		if (s_wheels.empty())
			return;
		w = s_wheels.front();
	}

	RacingWheelReading r;
	wgi::UINavigationReading nav{};
	bool have_nav = false;
	try
	{
		r = w.GetCurrentReading();
		if (const wgi::UINavigationController ui = wgi::UINavigationController::FromGameController(w))
		{
			nav = ui.GetCurrentReading();
			have_nav = true;
		}
	}
	catch (...)
	{
		return;
	}

	const double wheel = std::clamp(r.Wheel, -1.0, 1.0);
	USB::SetDeviceBindValue(0, usb_pad::CID_STEERING_L, static_cast<float>(wheel < 0 ? -wheel : 0.0));
	USB::SetDeviceBindValue(0, usb_pad::CID_STEERING_R, static_cast<float>(wheel > 0 ? wheel : 0.0));
	USB::SetDeviceBindValue(0, usb_pad::CID_THROTTLE, static_cast<float>(std::clamp(r.Throttle, 0.0, 1.0)));
	USB::SetDeviceBindValue(0, usb_pad::CID_BRAKE, static_cast<float>(std::clamp(r.Brake, 0.0, 1.0)));

	const auto has = [&r](RacingWheelButtons b) { return (r.Buttons & b) == b; };
	const auto nav_has = [&](wgi::RequiredUINavigationButtons b) { return have_nav && (nav.RequiredButtons & b) == b; };
	const auto nav_opt = [&](wgi::OptionalUINavigationButtons b) { return have_nav && (nav.OptionalButtons & b) == b; };

	USB::SetDeviceBindValue(0, usb_pad::CID_DPAD_UP, Pressed(has(RacingWheelButtons::DPadUp) || nav_has(wgi::RequiredUINavigationButtons::Up)));
	USB::SetDeviceBindValue(0, usb_pad::CID_DPAD_DOWN, Pressed(has(RacingWheelButtons::DPadDown) || nav_has(wgi::RequiredUINavigationButtons::Down)));
	USB::SetDeviceBindValue(0, usb_pad::CID_DPAD_LEFT, Pressed(has(RacingWheelButtons::DPadLeft) || nav_has(wgi::RequiredUINavigationButtons::Left)));
	USB::SetDeviceBindValue(0, usb_pad::CID_DPAD_RIGHT, Pressed(has(RacingWheelButtons::DPadRight) || nav_has(wgi::RequiredUINavigationButtons::Right)));
	// A/B/X/Y and Menu/View: the same on every Xbox wheel through UINavigation; paddles are the gear buttons
	USB::SetDeviceBindValue(0, usb_pad::CID_BUTTON0, Pressed(nav_has(wgi::RequiredUINavigationButtons::Accept)));          // A = Cross
	USB::SetDeviceBindValue(0, usb_pad::CID_BUTTON2, Pressed(nav_has(wgi::RequiredUINavigationButtons::Cancel)));          // B = Circle
	USB::SetDeviceBindValue(0, usb_pad::CID_BUTTON1, Pressed(nav_opt(wgi::OptionalUINavigationButtons::Context1))); // X = Square
	USB::SetDeviceBindValue(0, usb_pad::CID_BUTTON3, Pressed(nav_opt(wgi::OptionalUINavigationButtons::Context2))); // Y = Triangle
	USB::SetDeviceBindValue(0, usb_pad::CID_BUTTON9, Pressed(nav_has(wgi::RequiredUINavigationButtons::Menu)));            // Start
	USB::SetDeviceBindValue(0, usb_pad::CID_BUTTON8, Pressed(nav_has(wgi::RequiredUINavigationButtons::View)));            // Select
	USB::SetDeviceBindValue(0, usb_pad::CID_BUTTON4, Pressed(has(RacingWheelButtons::NextGear) ||
												 nav_opt(wgi::OptionalUINavigationButtons::PageRight)));  // R1 shift up
	USB::SetDeviceBindValue(0, usb_pad::CID_BUTTON5, Pressed(has(RacingWheelButtons::PreviousGear) ||
												 nav_opt(wgi::OptionalUINavigationButtons::PageLeft)));   // L1 shift down
	// LSB/RSB style extra buttons on most rims
	USB::SetDeviceBindValue(0, usb_pad::CID_BUTTON11, Pressed(has(RacingWheelButtons::Button1)));  // L3
	USB::SetDeviceBindValue(0, usb_pad::CID_BUTTON10, Pressed(has(RacingWheelButtons::Button2)));  // R3

	// reports: the first time anything is pressed, what the wheel sends (for wheels whose buttons land elsewhere)
	if (!s_logged_first_input && (r.Buttons != RacingWheelButtons::None || r.Throttle > 0.5 || (have_nav && nav.RequiredButtons != wgi::RequiredUINavigationButtons::None)))
	{
		s_logged_first_input = true;
		Console.WriteLn("Game Vault: wheel first input: wheel %.2f throttle %.2f brake %.2f clutch %.2f buttons 0x%X nav 0x%X/0x%X",
			r.Wheel, r.Throttle, r.Brake, r.Clutch, static_cast<unsigned>(r.Buttons),
			have_nav ? static_cast<unsigned>(nav.RequiredButtons) : 0u, have_nav ? static_cast<unsigned>(nav.OptionalButtons) : 0u);
	}
}

std::unique_ptr<usb_pad::FFDevice> GameVaultWheel::CreateFFDevice()
{
	std::vector<RacingWheel> wheels;
	{
		std::lock_guard<std::mutex> lk(s_lock);
		wheels = s_wheels;
	}
	for (const RacingWheel& w : wheels)
	{
		try
		{
			if (ForceFeedbackMotor motor = w.WheelMotor())
			{
				Console.WriteLn("Game Vault: wheel force feedback on %s", WheelName(w).c_str());
				return std::make_unique<WheelFF>(std::move(motor));
			}
		}
		catch (...)
		{
		}
	}
	Console.WriteLn("Game Vault: wheel force feedback: no wheel with a motor");
	return nullptr;
}
