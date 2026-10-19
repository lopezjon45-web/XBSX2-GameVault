/* Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
 * GPL-3.0-or-later, like PCSX2 / XBSX2.
 *
 * Bench mode. When <LocalState>\bench.ini exists at startup, XBSX2 boots the game it names, waits WarmupSeconds,
 * logs one line per performance update to <LocalState>\bench.log for SampleSeconds, writes a summary line and
 * exits. The file is deleted as soon as it is read, so a stray one can never take over a later launch from
 * Game Vault. Without the file nothing here runs.
 *
 *   [Bench]
 *   Game=D:\games\ps2\Gran Turismo 4.iso    (or disc: for the console's own drive)
 *   State=D:\states\gt4 lap.p2s             (optional: a save state file)
 *   Slot=1                                  (optional: a save state slot instead)
 *   WarmupSeconds=25
 *   SampleSeconds=60
 *   Press=20:start,24:cross                 (optional: pad-1 presses at seconds after the VM started)
 */
#include "pcsx2/PrecompiledHeader.h"

#include "Bench.h"

#include <windows.h>
#include <winrt/Windows.ApplicationModel.h>

#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/Timer.h"

#include "pcsx2/BenchProbe.h"
#include "pcsx2/CDVD/CDVDcommon.h"
#include "pcsx2/Config.h"
#include "pcsx2/Host.h"
#include "pcsx2/ImGui/FullscreenUI.h"
#include "pcsx2/INISettingsInterface.h"
#include "pcsx2/Input/InputManager.h"
#include "pcsx2/MTGS.h"
#include "pcsx2/PerformanceMetrics.h"
#include "pcsx2/SIO/Pad/Pad.h"
#include "pcsx2/SIO/Pad/PadDualshock2.h"
#include "pcsx2/VMManager.h"

#include "fmt/format.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	// one scripted pad-1 button press, held for a quarter second, at a time after the VM started (bench mode only)
	struct Press
	{
		float at;
		u32 button;
		const char* name;
		bool down = false;
		bool done = false;
	};

	struct Config
	{
		std::string game;
		std::string save_state;
		std::optional<s32> state_slot;
		float warmup_seconds = 25.0f;
		float sample_seconds = 60.0f;
		std::vector<Press> presses;
		bool probes = false; // the EE-thread time probes (BenchProbe): a profile run, not a metric run
	};

	constexpr std::pair<const char*, u32> kButtons[] = {
		{"up", PadDualshock2::PAD_UP}, {"right", PadDualshock2::PAD_RIGHT}, {"down", PadDualshock2::PAD_DOWN},
		{"left", PadDualshock2::PAD_LEFT}, {"triangle", PadDualshock2::PAD_TRIANGLE}, {"circle", PadDualshock2::PAD_CIRCLE},
		{"cross", PadDualshock2::PAD_CROSS}, {"square", PadDualshock2::PAD_SQUARE}, {"select", PadDualshock2::PAD_SELECT},
		{"start", PadDualshock2::PAD_START}, {"l1", PadDualshock2::PAD_L1}, {"l2", PadDualshock2::PAD_L2},
		{"r1", PadDualshock2::PAD_R1}, {"r2", PadDualshock2::PAD_R2}};

	// "20:start,24:cross": seconds after the VM started, then the button; unknown entries are skipped
	std::vector<Press> ParsePresses(const std::string& text)
	{
		std::vector<Press> out;
		size_t pos = 0;
		while (pos <= text.size())
		{
			const size_t comma = text.find(',', pos);
			const std::string item = text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
			pos = (comma == std::string::npos) ? text.size() + 1 : comma + 1;
			const size_t colon = item.find(':');
			if (colon == std::string::npos)
				continue;
			const std::string name = item.substr(colon + 1);
			for (const auto& [button_name, button] : kButtons)
			{
				if (name == button_name)
					out.push_back(Press{static_cast<float>(std::atof(item.c_str())), button, button_name});
			}
		}
		return out;
	}

	enum class Phase
	{
		Off,
		Booting,
		WarmingUp,
		Sampling,
		Done
	};

	// one entry per performance update inside the sample window
	struct Samples
	{
		std::vector<float> speed, fps, ee_ms, gs_ms;
		std::vector<float> probe_ms[BenchProbe::COUNT]; // each probe's own ms per vsync
		u64 first_frame = 0, last_frame = 0;
		float first_t = 0.0f, last_t = 0.0f;
	};
} // namespace

static Config s_config;
static std::atomic<Phase> s_phase{Phase::Off};
static std::atomic<u64> s_clock_start{0}; // Common::Timer value at OnVMStarted
static std::atomic<u32> s_ee_cpu{0}; // the core the EE thread last ran on

// the log, the samples and the error count: written on the GS thread (samples) and the CPU thread (boot, shutdown)
static std::mutex s_lock;
static std::FILE* s_log = nullptr;
static Samples s_samples;
static u32 s_errors = 0;
static bool s_summary_written = false;
static BenchProbe::Totals s_probe_prev[BenchProbe::COUNT] = {}; // at the previous update (OnVMStarted for the first)
static u64 s_frame_prev = 0;

static void WriteLine(const std::string& line)
{
	if (!s_log)
		return;
	std::fputs(line.c_str(), s_log);
	std::fputc('\n', s_log);
	std::fflush(s_log);
}

static float Median(std::vector<float> v)
{
	if (v.empty())
		return 0.0f;
	std::sort(v.begin(), v.end());
	const size_t n = v.size();
	return (n % 2) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) * 0.5f;
}

// the waits and the decoders get a calls-per-vsync column too; the rest run once per vsync
static bool LogsCalls(u32 id)
{
	// qualified: the global CDVD (the drive API pointer) makes a bare CDVD ambiguous here
	return id == BenchProbe::IPU || id == BenchProbe::SPU2 || id == BenchProbe::IOP || id == BenchProbe::CDVD ||
		   id == BenchProbe::GS_WAIT || id == BenchProbe::VU1_WAIT;
}

// once, whichever way the run ends: the window ran out, the boot failed, or the VM stopped early
static void WriteSummary()
{
	if (s_summary_written)
		return;
	s_summary_written = true;

	const Samples& s = s_samples;
	// a plain loop: std::min_element on floats links the STL's AVX2 copy, which the ISA gate refuses
	float min_speed = 0.0f, max_speed = 0.0f;
	for (size_t i = 0; i < s.speed.size(); i++)
	{
		min_speed = (i == 0) ? s.speed[i] : std::min(min_speed, s.speed[i]);
		max_speed = (i == 0) ? s.speed[i] : std::max(max_speed, s.speed[i]);
	}
	std::string line = fmt::format("# summary samples={} errors={} frames={} seconds={:.1f} median_speed={:.1f} median_fps={:.2f} "
					"median_ee_ms={:.2f} median_gs_ms={:.2f} min_speed={:.1f} max_speed={:.1f}",
		s.speed.size(), s_errors, s.last_frame - s.first_frame, s.last_t - s.first_t, Median(s.speed), Median(s.fps),
		Median(s.ee_ms), Median(s.gs_ms), min_speed, max_speed);
	for (u32 i = 0; i < BenchProbe::COUNT; i++)
		line += fmt::format(" median_{}={:.2f}", BenchProbe::Names[i], Median(s.probe_ms[i]));
	WriteLine(line);
}

// Every line is flushed, so nothing is lost if the clean exit never comes: ten seconds after it was asked
// for, the run ends the process itself and says so in the log (the emulog is closed first so its tail survives).
static void ExitWithWatchdog()
{
	Host::RequestExitApplication(false);
	std::thread([]() {
		std::this_thread::sleep_for(std::chrono::seconds(10));
		{
			std::lock_guard<std::mutex> lock(s_lock);
			WriteLine("# exit forced: the app had not exited 10 s after the request");
			if (s_log)
				std::fclose(s_log);
			s_log = nullptr;
		}
		Log::SetFileOutputLevel(LOGLEVEL_NONE, std::string());
		TerminateProcess(GetCurrentProcess(), 0);
	}).detach();
}

bool Bench::Load()
{
	const std::string ini_path = Path::Combine(EmuFolders::DataRoot, "bench.ini");
	if (!FileSystem::FileExists(ini_path.c_str()))
		return false;

	INISettingsInterface ini(ini_path);
	const bool loaded = ini.Load();
	// one-shot: gone before anything else happens, so it can never take over a later launch from Game Vault
	FileSystem::DeleteFilePath(ini_path.c_str());
	if (!loaded)
	{
		Console.Error("Bench: %s could not be read", ini_path.c_str());
		return false;
	}

	s_config.game = ini.GetStringValue("Bench", "Game");
	s_config.save_state = ini.GetStringValue("Bench", "State");
	const int slot = ini.GetIntValue("Bench", "Slot", -1);
	if (slot >= 0)
		s_config.state_slot = slot;
	s_config.warmup_seconds = ini.GetFloatValue("Bench", "WarmupSeconds", 25.0f);
	s_config.sample_seconds = ini.GetFloatValue("Bench", "SampleSeconds", 60.0f);
	s_config.presses = ParsePresses(ini.GetStringValue("Bench", "Press"));
	s_config.probes = ini.GetBoolValue("Bench", "Probes", false);

	const std::string log_path = Path::Combine(EmuFolders::DataRoot, "bench.log");
	s_log = FileSystem::OpenCFile(log_path.c_str(), "wb");
	if (!s_log)
	{
		Console.Error("Bench: %s could not be opened", log_path.c_str());
		return false;
	}

	const auto version = winrt::Windows::ApplicationModel::Package::Current().Id().Version();
	DWORD_PTR process_mask = 0, system_mask = 0;
	GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask);
	WriteLine(fmt::format("# xbsx2 bench build={}.{}.{}.{} game={} state={} slot={} warmup={:.1f} sample={:.1f} presses={} probes={} affinity=0x{:x} system_affinity=0x{:x}",
		version.Major, version.Minor, version.Build, version.Revision, s_config.game,
		s_config.save_state.empty() ? std::string("none") : s_config.save_state,
		s_config.state_slot ? std::to_string(*s_config.state_slot) : std::string("none"),
		s_config.warmup_seconds, s_config.sample_seconds, s_config.presses.size(), s_config.probes ? 1 : 0, static_cast<u64>(process_mask), static_cast<u64>(system_mask)));

	s_phase.store(Phase::Booting);
	Console.WriteLn("Bench: %s, %.0f s warm-up then %.0f s of samples, log in %s", s_config.game.c_str(),
		s_config.warmup_seconds, s_config.sample_seconds, log_path.c_str());
	return true;
}

bool Bench::Active()
{
	return s_phase.load() != Phase::Off;
}

void Bench::QueueBoot()
{
	static bool queued = false;
	if (queued)
		return;
	queued = true;

	Host::RunOnCPUThread([]() {
		if (VMManager::HasValidVM())
			return;

		VMBootParameters params;
		params.filename = s_config.game;
		params.source_type = s_config.game.rfind("disc:", 0) == 0 ? CDVD_SourceType::Disc : CDVD_SourceType::Iso;
		params.save_state = s_config.save_state;
		params.state_index = s_config.state_slot;
		if (s_config.game.empty() || VMManager::Initialize(params) != VMBootResult::StartupSuccess)
		{
			std::lock_guard<std::mutex> lock(s_lock);
			s_errors++;
			WriteLine("# error boot failed");
			WriteSummary();
			ExitWithWatchdog();
			return;
		}

		VMManager::SetState(VMState::Running);
		MTGS::WaitForOpen();
		InputManager::ReloadDevices();
	}, false);
}

void Bench::OnVMStarted()
{
	if (s_phase.load() != Phase::Booting)
		return;
	s_clock_start.store(Common::Timer::GetCurrentValue());
	BenchProbe::Snapshot(s_probe_prev);
	s_frame_prev = PerformanceMetrics::GetFrameNumber();
	s_phase.store(Phase::WarmingUp);
}

void Bench::OnMetricsUpdated()
{
	const Phase phase = s_phase.load();
	if (phase != Phase::WarmingUp && phase != Phase::Sampling)
		return;

	const float t = static_cast<float>(Common::Timer::ConvertValueToSeconds(Common::Timer::GetCurrentValue() - s_clock_start.load()));
	std::lock_guard<std::mutex> lock(s_lock);
	if (t >= s_config.warmup_seconds + s_config.sample_seconds)
	{
		WriteSummary();
		s_phase.store(Phase::Done);
		Host::RunOnCPUThread([]() { Host::RequestVMShutdown(false, false, false); }, false);
		return;
	}

	const bool sampling = t >= s_config.warmup_seconds;
	s_phase.store(sampling ? Phase::Sampling : Phase::WarmingUp);

	const float speed = PerformanceMetrics::GetSpeed();
	const float fps = PerformanceMetrics::GetFPS();
	const float ee_ms = static_cast<float>(PerformanceMetrics::GetCPUThreadAverageTime());
	const float gs_ms = PerformanceMetrics::GetGSThreadAverageTime();
	const u64 frame = PerformanceMetrics::GetFrameNumber();
	std::string line = fmt::format("t={:.1f} phase={} speed={:.1f} fps={:.2f} ifps={:.2f} ee_ms={:.2f} ee_pct={:.1f} gs_ms={:.2f} gs_pct={:.1f} "
					"vu_ms={:.2f} frame={} ee_cpu={} gs_cpu={}",
		t, sampling ? "sample" : "warmup", speed, fps, PerformanceMetrics::GetInternalFPS(), ee_ms,
		PerformanceMetrics::GetCPUThreadUsage(), gs_ms, PerformanceMetrics::GetGSThreadUsage(),
		PerformanceMetrics::GetVUThreadAverageTime(), frame, s_ee_cpu.load(), GetCurrentProcessorNumber());

	BenchProbe::Totals probes[BenchProbe::COUNT];
	BenchProbe::Snapshot(probes);
	const u64 vsyncs = frame - s_frame_prev;
	s_frame_prev = frame;
	float probe_ms[BenchProbe::COUNT];
	for (u32 i = 0; i < BenchProbe::COUNT; i++)
	{
		const BenchProbe::Totals& now = probes[i];
		const BenchProbe::Totals& prev = s_probe_prev[i];
		// signed: a child closed inside a still-open parent has been charged to the parent's child_ticks but not
		// yet to its ticks, so a snapshot taken then can briefly show the parent below zero
		const double own_ms = Common::Timer::ConvertValueToMilliseconds(now.ticks - prev.ticks) -
			Common::Timer::ConvertValueToMilliseconds(now.child_ticks - prev.child_ticks);
		probe_ms[i] = (vsyncs == 0) ? 0.0f : static_cast<float>(own_ms / vsyncs);
		line += fmt::format(" {}={:.2f}", BenchProbe::Names[i], probe_ms[i]);
		if (LogsCalls(i))
			line += fmt::format(" {}_n={}", BenchProbe::Names[i], (vsyncs == 0) ? 0 : (now.calls - prev.calls) / vsyncs);
		s_probe_prev[i] = now;
	}
	WriteLine(line);
	if (!sampling)
		return;

	if (fps == 0.0f)
		s_errors++;
	if (s_samples.speed.empty())
	{
		s_samples.first_frame = frame;
		s_samples.first_t = t;
	}
	s_samples.speed.push_back(speed);
	s_samples.fps.push_back(fps);
	s_samples.ee_ms.push_back(ee_ms);
	s_samples.gs_ms.push_back(gs_ms);
	for (u32 i = 0; i < BenchProbe::COUNT; i++)
		s_samples.probe_ms[i].push_back(probe_ms[i]);
	s_samples.last_frame = frame;
	s_samples.last_t = t;
}

void Bench::OnVMDestroyed()
{
	switch (s_phase.load())
	{
		case Phase::Off:
			return;

		// VMManager::Initialize reports a failed boot through here too; the boot in QueueBoot writes that one
		case Phase::Booting:
			return;

		case Phase::WarmingUp:
		case Phase::Sampling:
		{
			std::lock_guard<std::mutex> lock(s_lock);
			s_errors++;
			WriteLine("# error vm stopped before the sample window ended");
			WriteSummary();
			break;
		}

		case Phase::Done:
			break;
	}
	{
		std::lock_guard<std::mutex> lock(s_lock);
		WriteLine("# exit requested");
	}
	ExitWithWatchdog();
}

void Bench::OnCPUThreadVSync()
{
	static bool probes_enabled = false;
	if (s_config.probes && !probes_enabled)
	{
		probes_enabled = true;
		BenchProbe::EnableThisThread();
	}
	s_ee_cpu.store(GetCurrentProcessorNumber());

	const Phase phase = s_phase.load();
	if (phase != Phase::WarmingUp && phase != Phase::Sampling)
		return;
	const float t = static_cast<float>(Common::Timer::ConvertValueToSeconds(Common::Timer::GetCurrentValue() - s_clock_start.load()));
	for (Press& press : s_config.presses)
	{
		if (press.done || t < press.at)
			continue;
		if (!press.down)
		{
			Pad::SetControllerState(0, press.button, 1.0f);
			press.down = true;
			std::lock_guard<std::mutex> lock(s_lock);
			WriteLine(fmt::format("# press {} at t={:.1f}", press.name, t));
		}
		else if (t >= press.at + 0.25f)
		{
			Pad::SetControllerState(0, press.button, 0.0f);
			press.down = false;
			press.done = true;
		}
	}
}
