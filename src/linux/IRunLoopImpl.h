#pragma once

#include "RunLoop.h"

#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/gui/iplugview.h"

#include <algorithm>
#include <unordered_map>

namespace vstdemon {

// The Steinberg::Linux::IRunLoop the plugin editor requires: Linux VST3 GUIs register their own file
// descriptors and timers here (X socket wakeups, animation timers) and the host must service them.
// Bridges each registration onto the single vstdemon::RunLoop. Served two ways (both required by
// real plugins): as the plugin-factory host context (platform::getPluginFactoryContext) and via the
// X11 window's queryInterface (the IPlugFrame fallback path). A non-destroyable singleton — the run
// loop outlives every plugin that queries it. Adapted from the SDK editorhost sample's RunLoopImpl.
class RunLoopImpl : public Steinberg::U::ImplementsNonDestroyable<
                        Steinberg::U::Directly<Steinberg::Linux::IRunLoop>>
{
public:
	static Steinberg::Linux::IRunLoop& instance ()
	{
		static RunLoopImpl impl;
		return impl;
	}

	Steinberg::tresult PLUGIN_API registerEventHandler (Steinberg::Linux::IEventHandler* handler,
	                                                    Steinberg::Linux::FileDescriptor fd) override;
	Steinberg::tresult PLUGIN_API
	unregisterEventHandler (Steinberg::Linux::IEventHandler* handler) override;
	Steinberg::tresult PLUGIN_API registerTimer (Steinberg::Linux::ITimerHandler* handler,
	                                             Steinberg::Linux::TimerInterval milliseconds) override;
	Steinberg::tresult PLUGIN_API
	unregisterTimer (Steinberg::Linux::ITimerHandler* handler) override;

private:
	// Non-owning by deliberate divergence from the SDK sample (which stores IPtr). This is a
	// function-local static singleton, so it is destroyed at atexit — AFTER PluginHost has dlclose'd
	// the plugin module. Owning IPtrs would call release() on the plugin's handler objects from the
	// map destructor, i.e. a virtual call into unmapped .so memory (a guaranteed segfault on close
	// with JUCE-based plugins, which leave a handler registered). The IRunLoop contract already makes
	// the plugin own its handlers and unregister them before they die, so a raw pointer is correct:
	// it is only used for the reverse lookup in unregister*(); the dispatch lambdas capture it too.
	using EventHandler = Steinberg::Linux::IEventHandler*;
	using TimerHandler = Steinberg::Linux::ITimerHandler*;

	std::unordered_map<Steinberg::Linux::FileDescriptor, EventHandler> eventHandlers;
	std::unordered_map<TimerId, TimerHandler> timerHandlers;
};

//------------------------------------------------------------------------
inline Steinberg::tresult PLUGIN_API
RunLoopImpl::registerEventHandler (Steinberg::Linux::IEventHandler* handler,
                                   Steinberg::Linux::FileDescriptor fd)
{
	if (!handler || eventHandlers.find (fd) != eventHandlers.end ())
		return Steinberg::kInvalidArgument;

	RunLoop::instance ().registerFileDescriptor (
	    fd, [handler] (int descriptor) { handler->onFDIsSet (descriptor); });
	eventHandlers.emplace (fd, handler);
	return Steinberg::kResultTrue;
}

//------------------------------------------------------------------------
inline Steinberg::tresult PLUGIN_API
RunLoopImpl::unregisterEventHandler (Steinberg::Linux::IEventHandler* handler)
{
	if (!handler)
		return Steinberg::kInvalidArgument;

	auto it = std::find_if (eventHandlers.begin (), eventHandlers.end (),
	                        [&] (const auto& elem) { return elem.second == handler; });
	if (it == eventHandlers.end ())
		return Steinberg::kResultFalse;

	RunLoop::instance ().unregisterFileDescriptor (it->first);
	eventHandlers.erase (it);
	return Steinberg::kResultTrue;
}

//------------------------------------------------------------------------
inline Steinberg::tresult PLUGIN_API
RunLoopImpl::registerTimer (Steinberg::Linux::ITimerHandler* handler,
                            Steinberg::Linux::TimerInterval milliseconds)
{
	if (!handler || milliseconds == 0)
		return Steinberg::kInvalidArgument;

	auto id = RunLoop::instance ().registerTimer (milliseconds, [handler] (TimerId) { handler->onTimer (); });
	timerHandlers.emplace (id, handler);
	return Steinberg::kResultTrue;
}

//------------------------------------------------------------------------
inline Steinberg::tresult PLUGIN_API
RunLoopImpl::unregisterTimer (Steinberg::Linux::ITimerHandler* handler)
{
	if (!handler)
		return Steinberg::kInvalidArgument;

	auto it = std::find_if (timerHandlers.begin (), timerHandlers.end (),
	                        [&] (const auto& elem) { return elem.second == handler; });
	if (it == timerHandlers.end ())
		return Steinberg::kResultFalse;

	RunLoop::instance ().unregisterTimer (it->first);
	timerHandlers.erase (it);
	return Steinberg::kResultTrue;
}

} // namespace vstdemon
