// Where the EE thread's time goes, for the UWP bench log (pcsx2-uwp/src/Bench.cpp).
// Copyright (c) 2026 Skitzelles, https://github.com/skitzellez - written for Game Vault.
// Free software under the GNU General Public License version 3 or later, like the rest of this program;
// this notice stays in every copy.
#include "BenchProbe.h"

#include <atomic>

namespace BenchProbe
{
	const char* const Names[COUNT] = {"ipu", "ipu_dma", "spu2", "iop", "cdvd", "gs_wait", "gs_stall", "vu1_wait", "poll", "throttle"};

	thread_local bool t_enabled = false;
	thread_local Id t_current = COUNT;

	struct Counter
	{
		std::atomic<u64> ticks{0};
		std::atomic<u64> child_ticks{0};
		std::atomic<u64> calls{0};
	};

	static Counter s_counters[COUNT];

	void EnableThisThread()
	{
		t_enabled = true;
	}

	void Snapshot(Totals out[COUNT])
	{
		for (u32 i = 0; i < COUNT; i++)
		{
			out[i].ticks = s_counters[i].ticks.load(std::memory_order_relaxed);
			out[i].child_ticks = s_counters[i].child_ticks.load(std::memory_order_relaxed);
			out[i].calls = s_counters[i].calls.load(std::memory_order_relaxed);
		}
	}

	void Close(Id id, u64 start, Id parent)
	{
		const u64 elapsed = Common::Timer::GetCurrentValue() - start;
		s_counters[id].ticks.fetch_add(elapsed, std::memory_order_relaxed);
		s_counters[id].calls.fetch_add(1, std::memory_order_relaxed);
		if (parent != COUNT)
			s_counters[parent].child_ticks.fetch_add(elapsed, std::memory_order_relaxed);
		t_current = parent;
	}
} // namespace BenchProbe
