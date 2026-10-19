/* Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
 * GPL-3.0-or-later, like PCSX2 / XBSX2.
 *
 * Bench mode: <LocalState>\bench.ini boots one game, logs performance samples to bench.log and exits. See Bench.cpp.
 */
#pragma once

namespace Bench
{
	// App::Initialize, once the config is up: reads and deletes bench.ini. False when there was none.
	bool Load();

	// true from a successful Load() on; every hook below does nothing while false
	bool Active();

	// UI thread (OnActivate): queues the boot on the CPU thread, once
	void QueueBoot();

	// CPU thread
	void OnVMStarted();
	void OnVMDestroyed();
	void OnCPUThreadVSync();

	// GS thread
	void OnMetricsUpdated();
} // namespace Bench
