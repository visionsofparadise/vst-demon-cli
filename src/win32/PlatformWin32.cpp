#include "../Platform.h"

#include <windows.h>

namespace vstdemon {
namespace platform {

//------------------------------------------------------------------------
void initialize ()
{
	OleInitialize (nullptr);
}

//------------------------------------------------------------------------
void terminate ()
{
	OleUninitialize ();
}

//------------------------------------------------------------------------
void runEventLoop ()
{
	MSG msg;
	while (GetMessage (&msg, nullptr, 0, 0))
	{
		TranslateMessage (&msg);
		DispatchMessage (&msg);
	}
}

//------------------------------------------------------------------------
void quitEventLoop ()
{
	PostQuitMessage (0);
}

//------------------------------------------------------------------------
Steinberg::FUnknown* getPluginFactoryContext ()
{
	return nullptr;
}

} // namespace platform
} // namespace vstdemon
