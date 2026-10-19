/* Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
 * GPL-3.0-or-later, like PCSX2 / XBSX2.
 *
 * Racing wheels on the Xbox (Windows.Gaming.Input.RacingWheel). See GameVaultWheel.cpp.
 */
#pragma once

#include <memory>
#include <string_view>

namespace usb_pad
{
	struct FFDevice;
}

namespace GameVaultWheel
{
	// starts watching for wheels (any thread, once)
	void Initialize();

	// CPU thread, with the other input sources: copies the first wheel into USB port 1
	void Poll();

	// the force feedback name written into USB1 Pad_FFDevice for an Xbox wheel
	inline constexpr std::string_view kFFDeviceName = "GameVaultWheel";

	// force feedback for the first wheel with a motor, or null
	std::unique_ptr<usb_pad::FFDevice> CreateFFDevice();
} // namespace GameVaultWheel
