#include "../Platform.h"
#include "IRunLoopImpl.h"
#include "RunLoop.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include <X11/Xlib.h>

namespace vstdemon {
namespace platform {

namespace {
Display* gDisplay {nullptr};

// Xlib's default error handler calls exit() on any protocol error, and XSetErrorHandler is
// process-global — it also catches errors on the plugin's own X connection (JUCE-based editors open
// their own Display and emit benign async errors, e.g. polling a not-yet-present property). A host
// embedding a third-party plugin must neither die nor spam on those, so errors from any connection
// other than ours are swallowed silently, and our own connection's errors are logged and continued.
// Fatal (display-lost) errors still terminate via the IO error handler, which Xlib cannot make
// non-fatal.
int xErrorHandler (Display* display, XErrorEvent* event)
{
	if (display != gDisplay)
		return 0;
	char buffer[256];
	XGetErrorText (display, event->error_code, buffer, sizeof (buffer));
	std::fprintf (stderr, "X error: %s (request %d.%d, resource 0x%lx)\n", buffer,
	              event->request_code, event->minor_code, event->resourceid);
	return 0;
}
} // namespace

//------------------------------------------------------------------------
void initialize ()
{
	XInitThreads ();
	XSetErrorHandler (xErrorHandler);

	std::string displayName;
	if (const char* env = std::getenv ("DISPLAY"))
		displayName = env;
	if (displayName.empty ())
		displayName = ":0";

	gDisplay = XOpenDisplay (displayName.c_str ());
	if (!gDisplay)
	{
		std::fprintf (stderr, "Could not open X display '%s'.\n", displayName.c_str ());
		return;
	}
	RunLoop::instance ().setDisplay (gDisplay);
}

//------------------------------------------------------------------------
void terminate ()
{
	if (gDisplay)
	{
		XCloseDisplay (gDisplay);
		gDisplay = nullptr;
	}
}

//------------------------------------------------------------------------
void runEventLoop ()
{
	if (!gDisplay)
		return;
	RunLoop::instance ().start ();
}

//------------------------------------------------------------------------
void quitEventLoop ()
{
	RunLoop::instance ().stop ();
}

//------------------------------------------------------------------------
Steinberg::FUnknown* getPluginFactoryContext ()
{
	return &RunLoopImpl::instance ();
}

} // namespace platform
} // namespace vstdemon
