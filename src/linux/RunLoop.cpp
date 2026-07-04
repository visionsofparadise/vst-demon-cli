#include "RunLoop.h"

#include <algorithm>

#include <sys/select.h>

namespace vstdemon {

//------------------------------------------------------------------------
RunLoop& RunLoop::instance ()
{
	static RunLoop gInstance;
	return gInstance;
}

//------------------------------------------------------------------------
void RunLoop::setDisplay (Display* display)
{
	this->display = display;
}

//------------------------------------------------------------------------
void RunLoop::registerWindow (XID window, const EventCallback& callback)
{
	windowMap.emplace (window, callback);
}

//------------------------------------------------------------------------
void RunLoop::unregisterWindow (XID window)
{
	windowMap.erase (window);
}

//------------------------------------------------------------------------
void RunLoop::registerFileDescriptor (int fd, const FileDescriptorCallback& callback)
{
	fileDescriptors.emplace (fd, callback);
}

//------------------------------------------------------------------------
void RunLoop::unregisterFileDescriptor (int fd)
{
	fileDescriptors.erase (fd);
}

//------------------------------------------------------------------------
void RunLoop::doSelect (timeval* timeout)
{
	int nfds = 0;
	fd_set readFDs;
	FD_ZERO (&readFDs);

	// Read-readiness only. Registering the fds for write/except readiness too would make select()
	// return immediately on every iteration (a socket is almost always writable), busy-spinning a
	// whole core while the editor sits idle. IEventHandler wants "data to read" (incoming X events),
	// which is read-readiness.
	for (auto& e : fileDescriptors)
	{
		int fd = e.first;
		FD_SET (fd, &readFDs);
		nfds = std::max (nfds, fd);
	}

	int result = ::select (nfds + 1, &readFDs, nullptr, nullptr, timeout);
	if (result <= 0)
		return;

	// Copy fds first: a callback may unregister descriptors (dialog scope, teardown), which would
	// invalidate iterators over the live map.
	std::vector<int> ready;
	for (auto& e : fileDescriptors)
	{
		if (FD_ISSET (e.first, &readFDs))
			ready.push_back (e.first);
	}
	for (int fd : ready)
	{
		auto it = fileDescriptors.find (fd);
		if (it != fileDescriptors.end ())
			it->second (fd);
	}
}

//------------------------------------------------------------------------
bool RunLoop::handleEvents ()
{
	auto count = XPending (display);
	if (count == 0)
		return false;
	for (int i = 0; i < count; ++i)
	{
		XEvent event {};
		XNextEvent (display, &event);
		auto it = windowMap.find (event.xany.window);
		if (it != windowMap.end ())
		{
			it->second (event);
			if (event.type == DestroyNotify)
				windowMap.erase (it);
		}
		else
		{
			XPutBackEvent (display, &event);
			break;
		}
	}
	return true;
}

//------------------------------------------------------------------------
TimerId RunLoop::registerTimer (TimerIntervalMs interval, const TimerCallback& callback)
{
	return timerProcessor.registerTimer (interval, callback);
}

//------------------------------------------------------------------------
void RunLoop::unregisterTimer (TimerId id)
{
	timerProcessor.unregisterTimer (id);
}

//------------------------------------------------------------------------
void RunLoop::start ()
{
	running = true;

	int fd = XConnectionNumber (display);
	registerFileDescriptor (fd, [this] (int) { handleEvents (); });

	XSync (display, False);
	handleEvents ();

	while (running)
	{
		// Fire due timers and get the wait BEFORE selecting. Ordering matters: a plugin editor may
		// produce no X events on our connection after attach (VSTGUI editors render on their own
		// connection; only JUCE-style editors reliably wake ours), so a select() that ran first with
		// no timeout would block forever and no timer — dirty poll or the close-after hook — would
		// ever fire. Computing the timeout from the timer queue first bounds the wait to the next
		// timer.
		auto nextFireTime = timerProcessor.handleTimersAndReturnNextFireTimeInMs ();
		if (!running)
			break; // a timer callback (e.g. close) may have stopped the loop

		timeval selectTimeout {};
		timeval* timeout = nullptr;
		if (nextFireTime != TimerProcessor::noTimers)
		{
			selectTimeout.tv_sec = static_cast<time_t> (nextFireTime / 1000);
			selectTimeout.tv_usec = static_cast<suseconds_t> ((nextFireTime % 1000) * 1000);
			timeout = &selectTimeout;
		}
		doSelect (timeout);
	}

	unregisterFileDescriptor (fd);
}

//------------------------------------------------------------------------
void RunLoop::stop ()
{
	running = false;
}

//------------------------------------------------------------------------
uint64_t TimerProcessor::handleTimersAndReturnNextFireTimeInMs ()
{
	using std::chrono::time_point_cast;

	if (timers.empty ())
		return noTimers;

	auto current = time_point_cast<Millisecond> (Clock::now ());

	std::vector<TimerId> timersToFire;
	for (auto& timer : timers)
	{
		if (timer.nextFireTime > current)
			break;
		timersToFire.push_back (timer.id);
		updateTimerNextFireTime (timer, current);
	}

	// Defer unregisters across the whole dispatch: a callback (e.g. a one-shot close/save hook) may
	// unregister itself, and erasing the executing std::function would free its captures mid-call.
	firing = true;
	for (auto id : timersToFire)
	{
		for (auto& timer : timers)
		{
			if (timer.id == id)
			{
				timer.callback (timer.id);
				break;
			}
		}
	}
	firing = false;
	for (auto id : pendingUnregister)
		eraseTimer (id);
	pendingUnregister.clear ();

	// A fired callback may have unregistered the last timer — re-check before touching front().
	if (timers.empty ())
		return noTimers;

	// Fired timers got new next-fire times; re-sort so front() is the soonest. Always return the
	// delay to that soonest timer (0 if already due) — NOT noTimers when nothing was due this pass,
	// which would make the caller block select() indefinitely and stall the 1 s dirty poll while idle.
	if (!timersToFire.empty ())
		sortTimers ();

	auto nextFireTime = timers.front ().nextFireTime;
	current = now ();
	if (nextFireTime <= current)
		return 0;
	return static_cast<uint64_t> ((nextFireTime - current).count ());
}

//------------------------------------------------------------------------
void TimerProcessor::updateTimerNextFireTime (Timer& timer, TimePoint current)
{
	timer.nextFireTime = current + Millisecond (timer.interval);
}

//------------------------------------------------------------------------
void TimerProcessor::sortTimers ()
{
	std::sort (timers.begin (), timers.end (),
	           [] (const Timer& t1, const Timer& t2) { return t1.nextFireTime < t2.nextFireTime; });
}

//------------------------------------------------------------------------
auto TimerProcessor::now () -> TimePoint
{
	return std::chrono::time_point_cast<Millisecond> (Clock::now ());
}

//------------------------------------------------------------------------
TimerId TimerProcessor::registerTimer (TimerIntervalMs interval, const TimerCallback& callback)
{
	auto timerId = ++timerIdCounter;
	Timer timer;
	timer.id = timerId;
	timer.callback = callback;
	timer.interval = interval;
	updateTimerNextFireTime (timer, now ());

	timers.emplace_back (std::move (timer));
	sortTimers ();

	return timerId;
}

//------------------------------------------------------------------------
void TimerProcessor::unregisterTimer (TimerId id)
{
	// During dispatch, defer: erasing now could free the executing callback (see handleTimers).
	if (firing)
	{
		pendingUnregister.push_back (id);
		return;
	}
	eraseTimer (id);
}

//------------------------------------------------------------------------
void TimerProcessor::eraseTimer (TimerId id)
{
	for (auto it = timers.begin (), end = timers.end (); it != end; ++it)
	{
		if (it->id == id)
		{
			timers.erase (it);
			break;
		}
	}
}

} // namespace vstdemon
