// Where the EE thread's time goes, for the UWP bench log (pcsx2-uwp/src/Bench.cpp).
// Copyright (c) 2026 Skitzelles, https://github.com/skitzellez - written for Game Vault.
// Free software under the GNU General Public License version 3 or later, like the rest of this program;
// this notice stays in every copy.
#pragma once

#include "common/Pcsx2Defs.h"
#include "common/Timer.h"

namespace BenchProbe
{
	enum Id : u32
	{
		IPU,
		IPU_DMA,
		SPU2,
		IOP,
		CDVD,
		GS_WAIT,
		GS_STALL,
		VU1_WAIT,
		VSYNC_POLL,
		THROTTLE,
		COUNT
	};

	// log names, in Id order
	extern const char* const Names[COUNT];

	// Common::Timer values; a probe's own time is ticks - child_ticks
	struct Totals
	{
		u64 ticks;
		u64 child_ticks;
		u64 calls;
	};

	// Scopes count only on a thread that called this (the EE thread in bench mode); elsewhere they are a
	// load and a branch, which keeps the VU thread's WaitGS out of gs_wait.
	void EnableThisThread();

	// any thread; relaxed reads, so a scope still open on the EE thread is not in it yet
	void Snapshot(Totals out[COUNT]);

	extern thread_local bool t_enabled;
	extern thread_local Id t_current;
	void Close(Id id, u64 start, Id parent);

	struct Scope
	{
		Id id;
		u64 start;
		Id parent;

		explicit Scope(Id id_)
		{
			if (!t_enabled)
			{
				id = COUNT;
				return;
			}
			id = id_;
			start = Common::Timer::GetCurrentValue();
			parent = t_current;
			t_current = id_;
		}

		~Scope()
		{
			if (id != COUNT)
				Close(id, start, parent);
		}

		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;
	};
} // namespace BenchProbe
