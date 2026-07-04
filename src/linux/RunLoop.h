#pragma once

#include <X11/Xlib.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
#include <vector>

namespace vstdemon {

using TimerId = uint64_t;
using TimerIntervalMs = uint64_t;
using TimerCallback = std::function<void (TimerId)>;

// Sorted steady-clock timer queue. Timers fire when their next-fire time is reached; the run loop
// asks for the next fire delay to bound its select() wait. Adapted from the SDK editorhost sample's
// TimerProcessor.
class TimerProcessor
{
public:
	static constexpr uint64_t noTimers = std::numeric_limits<uint64_t>::max ();

	TimerId registerTimer (TimerIntervalMs interval, const TimerCallback& callback);
	void unregisterTimer (TimerId id);

	// Fire every due timer and return the ms until the next one is due (0 if already due, noTimers if
	// no timers remain).
	uint64_t handleTimersAndReturnNextFireTimeInMs ();

private:
	using Clock = std::chrono::steady_clock;
	using Millisecond = std::chrono::milliseconds;
	using TimePoint = std::chrono::time_point<Clock, Millisecond>;

	struct Timer
	{
		TimerId id;
		TimerIntervalMs interval;
		TimerCallback callback;
		TimePoint nextFireTime;
	};

	void updateTimerNextFireTime (Timer& timer, TimePoint current);
	void sortTimers ();
	TimePoint now ();

	std::vector<Timer> timers;
	TimerId timerIdCounter {0};
};

// The Linux event loop: select() over registered file descriptors (the X connection fd is one of
// them), per-XID X-event dispatch, and the sorted timer queue. A singleton because both the X11
// window and the IRunLoop bridge register against the same loop. Adapted from the SDK editorhost
// sample's RunLoop; kept in the vstdemon namespace and our style rather than included from the
// samples tree.
class RunLoop
{
public:
	using EventCallback = std::function<bool (const XEvent& event)>;
	using FileDescriptorCallback = std::function<void (int fd)>;

	static RunLoop& instance ();

	void setDisplay (Display* display);
	Display* getDisplay () const { return display; }

	void registerWindow (XID window, const EventCallback& callback);
	void unregisterWindow (XID window);

	void registerFileDescriptor (int fd, const FileDescriptorCallback& callback);
	void unregisterFileDescriptor (int fd);

	TimerId registerTimer (TimerIntervalMs interval, const TimerCallback& callback);
	void unregisterTimer (TimerId id);

	// Run until stop() is called (or no windows remain). Registers the X connection fd on entry.
	void start ();
	void stop ();

private:
	void doSelect (timeval* timeout);
	bool handleEvents ();

	std::unordered_map<XID, EventCallback> windowMap;
	std::unordered_map<int, FileDescriptorCallback> fileDescriptors;
	TimerProcessor timerProcessor;

	Display* display {nullptr};
	bool running {false};
};

} // namespace vstdemon
