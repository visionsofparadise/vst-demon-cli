#include "EditorWindow.h"

#include "../PresetManager.h"
#include "../Utf8.h"
#include "WindowMessages.h"

#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "public.sdk/source/vst/utility/optional.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include <algorithm>
#include <cstdio>
#include <shobjidl.h>
#include <string>

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
#ifndef WM_GETDPISCALEDSIZE
#define WM_GETDPISCALEDSIZE 0x02E4
#endif

using Steinberg::kInternalError;
using Steinberg::kInvalidArgument;
using Steinberg::kNoInterface;
using Steinberg::kPlatformTypeHWND;
using Steinberg::kResultFalse;
using Steinberg::kResultTrue;
using Steinberg::tresult;

namespace vstdemon {

namespace {

const WCHAR* kWindowClassName = L"VSTDemon WindowClass";

// 1-second dirty poll — the catch-all trigger for vendors whose UI edits bypass the parameter
// system (so no endEdit fires). See design-cli's auto-save trigger set.
constexpr UINT_PTR kDirtyPollTimerId = 1;
constexpr UINT kDirtyPollIntervalMs = 1000;

// File-menu command ids. design-cli's menu is exactly Open Preset..., Save Preset As..., Close —
// no New/Recent/Save (auto-save makes plain Save meaningless).
constexpr UINT kMenuOpenPreset = 0x1001;
constexpr UINT kMenuSavePresetAs = 0x1002;
constexpr UINT kMenuClose = 0x1003;

const COMDLG_FILTERSPEC kPresetFilter[] = {{L"VST3 Preset (*.vstpreset)", L"*.vstpreset"}};

// The trailing path component (filename) of a UTF-8 path, for the title bar.
std::string fileNameOf (const std::string& path)
{
	auto pos = path.find_last_of ("\\/");
	return pos == std::string::npos ? path : path.substr (pos + 1);
}

struct DynamicLibrary
{
	explicit DynamicLibrary (const char* name) { module = LoadLibraryA (name); }
	~DynamicLibrary () { if (module) FreeLibrary (module); }

	template <typename T>
	T getProcAddress (const char* name)
	{
		return module ? reinterpret_cast<T> (GetProcAddress (module, name)) : nullptr;
	}

private:
	HMODULE module {nullptr};
};

#ifndef _DPI_AWARENESS_CONTEXTS_
DECLARE_HANDLE (DPI_AWARENESS_CONTEXT);
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

struct User32Library : DynamicLibrary
{
	static User32Library& instance ()
	{
		static User32Library gInstance;
		return gInstance;
	}

	bool setProcessDpiAwarenessContext (DPI_AWARENESS_CONTEXT context) const
	{
		if (!setProcessDpiAwarenessContextProc)
			return false;
		return setProcessDpiAwarenessContextProc (context);
	}

	bool adjustWindowRectExForDpi (LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle,
	                               UINT dpi) const
	{
		if (!adjustWindowRectExForDpiProc)
			return false;
		return adjustWindowRectExForDpiProc (lpRect, dwStyle, bMenu, dwExStyle, dpi);
	}

private:
	using SetProcessDpiAwarenessContextProc = BOOL (WINAPI*) (DPI_AWARENESS_CONTEXT);
	using AdjustWindowRectExForDpiProc = BOOL (WINAPI*) (LPRECT, DWORD, BOOL, DWORD, UINT);

	User32Library () : DynamicLibrary ("User32.dll")
	{
		setProcessDpiAwarenessContextProc =
		    getProcAddress<SetProcessDpiAwarenessContextProc> ("SetProcessDpiAwarenessContext");
		adjustWindowRectExForDpiProc =
		    getProcAddress<AdjustWindowRectExForDpiProc> ("AdjustWindowRectExForDpi");
	}

	SetProcessDpiAwarenessContextProc setProcessDpiAwarenessContextProc {nullptr};
	AdjustWindowRectExForDpiProc adjustWindowRectExForDpiProc {nullptr};
};

struct ShcoreLibrary : DynamicLibrary
{
	static ShcoreLibrary& instance ()
	{
		static ShcoreLibrary gInstance;
		return gInstance;
	}

	struct DPI
	{
		UINT x;
		UINT y;
	};

	VST3::Optional<DPI> getDpiForWindow (HWND window) const
	{
		if (!getDpiForMonitorProc)
			return {};
		auto monitor = MonitorFromWindow (window, MONITOR_DEFAULTTONEAREST);
		UINT x, y;
		getDpiForMonitorProc (monitor, MDT_EFFECTIVE_DPI, &x, &y);
		return DPI {x, y};
	}

private:
	enum MONITOR_DPI_TYPE
	{
		MDT_EFFECTIVE_DPI = 0
	};

	using GetDpiForMonitorProc = HRESULT (WINAPI*) (HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*);

	ShcoreLibrary () : DynamicLibrary ("Shcore.dll")
	{
		getDpiForMonitorProc = getProcAddress<GetDpiForMonitorProc> ("GetDpiForMonitor");
	}

	GetDpiForMonitorProc getDpiForMonitorProc {nullptr};
};

DWORD computeStyle (bool resizeable)
{
	DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
	if (resizeable)
		style |= WS_SIZEBOX;
	return style;
}

} // namespace

//------------------------------------------------------------------------
std::shared_ptr<EditorWindow> EditorWindow::make (const std::string& title,
                                                  const IPtr<IPlugView>& view,
                                                  PresetManager* presetManager)
{
	auto window = std::make_shared<EditorWindow> ();
	window->presetManager = presetManager;
	window->className = title;
	if (window->init (title, view))
		return window;
	return nullptr;
}

//------------------------------------------------------------------------
EditorWindow::~EditorWindow () noexcept
{
	closePlugView ();
	if (hwnd)
	{
		SetWindowLongPtr (hwnd, GWLP_USERDATA, (LONG_PTR) nullptr);
		DestroyWindow (hwnd);
		hwnd = nullptr;
	}
}

//------------------------------------------------------------------------
void EditorWindow::registerWindowClass (HINSTANCE instance)
{
	static bool once = false;
	if (once)
		return;
	once = true;

	WNDCLASSEX wcex {};
	wcex.cbSize = sizeof (WNDCLASSEX);
	wcex.style = CS_DBLCLKS;
	wcex.lpfnWndProc = wndProc;
	wcex.hInstance = instance;
	wcex.hCursor = LoadCursor (instance, IDC_ARROW);
	wcex.hbrBackground = nullptr;
	wcex.lpszClassName = kWindowClassName;
	RegisterClassEx (&wcex);
}

//------------------------------------------------------------------------
bool EditorWindow::init (const std::string& title, const IPtr<IPlugView>& view)
{
	plugView = view;

	ViewRect plugViewSize {};
	if (plugView->getSize (&plugViewSize) != kResultTrue)
		return false;

	const bool resizeable = plugView->canResize () == kResultTrue;
	const int width = plugViewSize.right - plugViewSize.left;
	const int height = plugViewSize.bottom - plugViewSize.top;

	HINSTANCE instance = GetModuleHandle (nullptr);
	registerWindowClass (instance);

	DWORD exStyle = WS_EX_APPWINDOW;
	DWORD dwStyle = computeStyle (resizeable);
	auto windowTitle = Steinberg::Vst::StringConvert::convert (title);

	HMENU menu = CreateMenu ();
	HMENU fileMenu = CreatePopupMenu ();
	AppendMenuW (fileMenu, MF_STRING, kMenuOpenPreset, L"Open Preset...");
	AppendMenuW (fileMenu, MF_STRING, kMenuSavePresetAs, L"Save Preset As...");
	AppendMenuW (fileMenu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW (fileMenu, MF_STRING, kMenuClose, L"Close");
	AppendMenuW (menu, MF_POPUP, reinterpret_cast<UINT_PTR> (fileMenu), L"File");

	// bMenu=TRUE so the menu bar's height is added to the adjusted rect — the client area then still
	// exactly fits the plugin view (no clipping/letterboxing). SetMenu happens after CreateWindowEx.
	RECT rect {0, 0, width, height};
	AdjustWindowRectEx (&rect, dwStyle, TRUE, exStyle);

	hwnd = CreateWindowEx (exStyle, kWindowClassName,
	                       reinterpret_cast<const TCHAR*> (windowTitle.data ()), dwStyle, CW_USEDEFAULT,
	                       CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr,
	                       menu, instance, nullptr);
	if (!hwnd)
	{
		DestroyMenu (menu);
		return false;
	}

	SetWindowLongPtr (hwnd, GWLP_USERDATA, (LONG_PTR) this);
	return true;
}

//------------------------------------------------------------------------
LRESULT CALLBACK EditorWindow::wndProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	auto* window = reinterpret_cast<EditorWindow*> ((LONG_PTR)GetWindowLongPtr (hWnd, GWLP_USERDATA));
	if (window)
		return window->proc (message, wParam, lParam);
	return DefWindowProc (hWnd, message, wParam, lParam);
}

//------------------------------------------------------------------------
LRESULT EditorWindow::proc (UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_ERASEBKGND:
			return TRUE;
		case WM_PAINT:
		{
			PAINTSTRUCT ps {};
			BeginPaint (hwnd, &ps);
			EndPaint (hwnd, &ps);
			return FALSE;
		}
		case WM_SIZE:
		{
			RECT r;
			GetClientRect (hwnd, &r);
			onResize (r.right - r.left, r.bottom - r.top);
			break;
		}
		case WM_SIZING:
		{
			constrainSizing (reinterpret_cast<RECT*> (lParam));
			return TRUE;
		}
		case WM_TIMER:
			// dialogOpen gates the poll even though the timer is also killed around dialogs — a
			// belt-and-braces guard shared with the posted-save path below.
			if (wParam == kDirtyPollTimerId && presetManager && !dialogOpen)
				presetManager->saveIfDirty ();
			return 0;
		case WM_VSTDEMON_SAVE:
			// An endEdit-posted save can land inside IFileDialog::Show's nested modal loop; dialogOpen
			// blocks it so pre-dialog state is never written to the target the user is leaving.
			if (presetManager && !dialogOpen)
				presetManager->saveIfDirty ();
			return 0;
		case WM_COMMAND:
			// lParam==0 marks a menu (or accelerator) command; a nonzero lParam is a child-control
			// notification (the plugin editor is a child HWND, so its controls' WM_COMMANDs arrive
			// here too and must not be mistaken for our menu ids).
			if (lParam == 0)
			{
				switch (LOWORD (wParam))
				{
					case kMenuOpenPreset:
						onOpenPreset ();
						return 0;
					case kMenuSavePresetAs:
						onSavePresetAs ();
						return 0;
					case kMenuClose:
						requestClose ();
						return 0;
				}
			}
			break;
		case WM_CLOSE:
			requestClose ();
			return TRUE;
		case WM_GETDPISCALEDSIZE:
		{
			inDpiChangeState = true;
			auto* proposedSize = reinterpret_cast<SIZE*> (lParam);
			auto newScaleFactor =
			    static_cast<float> (wParam) / static_cast<float> (USER_DEFAULT_SCREEN_DPI);
			onContentScaleFactorChanged (newScaleFactor);
			if (dpiChangedWidth != 0 && dpiChangedHeight != 0)
			{
				WINDOWINFO windowInfo {0};
				GetWindowInfo (hwnd, &windowInfo);
				RECT clientRect {};
				clientRect.right = dpiChangedWidth;
				clientRect.bottom = dpiChangedHeight;
				User32Library::instance ().adjustWindowRectExForDpi (
				    &clientRect, windowInfo.dwStyle, TRUE, windowInfo.dwExStyle,
				    static_cast<UINT> (wParam));
				proposedSize->cx = clientRect.right - clientRect.left;
				proposedSize->cy = clientRect.bottom - clientRect.top;
				return TRUE;
			}
			return FALSE;
		}
		case WM_DPICHANGED:
		{
			if (inDpiChangeState)
			{
				auto* rect = reinterpret_cast<RECT*> (lParam);
				inDpiChangeState = false;
				dpiChangedWidth = 0;
				dpiChangedHeight = 0;
				SetWindowPos (hwnd, nullptr, rect->left, rect->top, rect->right - rect->left,
				              rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
				return 0;
			}
			onContentScaleFactorChanged (getContentScaleFactor ());
			return 0;
		}
	}
	return DefWindowProc (hwnd, message, wParam, lParam);
}

//------------------------------------------------------------------------
void EditorWindow::onResize (int width, int height)
{
	if (!plugView)
		return;
	ViewRect r {};
	r.right = width;
	r.bottom = height;
	ViewRect current {};
	if (plugView->getSize (&current) == kResultTrue &&
	    (current.right - current.left != width || current.bottom - current.top != height))
		plugView->onSize (&r);
}

//------------------------------------------------------------------------
void EditorWindow::constrainSizing (RECT* newSize)
{
	if (!plugView)
		return;

	RECT oldSize;
	GetWindowRect (hwnd, &oldSize);
	RECT clientSize;
	GetClientRect (hwnd, &clientSize);

	auto diffX = (newSize->right - newSize->left) - (oldSize.right - oldSize.left);
	auto diffY = (newSize->bottom - newSize->top) - (oldSize.bottom - oldSize.top);

	int newClientWidth = (clientSize.right - clientSize.left) + static_cast<int> (diffX);
	int newClientHeight = (clientSize.bottom - clientSize.top) + static_cast<int> (diffY);

	ViewRect r {};
	r.right = newClientWidth;
	r.bottom = newClientHeight;
	if (plugView->checkSizeConstraint (&r) == kResultTrue)
	{
		int constrainedWidth = r.right - r.left;
		int constrainedHeight = r.bottom - r.top;
		if (constrainedWidth != newClientWidth || constrainedHeight != newClientHeight)
		{
			auto diffX2 = (oldSize.right - oldSize.left) - (clientSize.right - clientSize.left);
			auto diffY2 = (oldSize.bottom - oldSize.top) - (clientSize.bottom - clientSize.top);
			newSize->right = newSize->left + constrainedWidth + static_cast<LONG> (diffX2);
			newSize->bottom = newSize->top + constrainedHeight + static_cast<LONG> (diffY2);
		}
	}
}

//------------------------------------------------------------------------
void EditorWindow::onContentScaleFactorChanged (float scaleFactor)
{
	if (auto css = Steinberg::U::cast<Steinberg::IPlugViewContentScaleSupport> (plugView))
		css->setContentScaleFactor (scaleFactor);
}

//------------------------------------------------------------------------
float EditorWindow::getContentScaleFactor () const
{
	if (auto dpi = ShcoreLibrary::instance ().getDpiForWindow (hwnd))
		return static_cast<float> (dpi->x) / static_cast<float> (USER_DEFAULT_SCREEN_DPI);
	return 1.f;
}

//------------------------------------------------------------------------
void EditorWindow::resizeContent (int width, int height)
{
	if (inDpiChangeState)
	{
		dpiChangedWidth = width;
		dpiChangedHeight = height;
		return;
	}
	RECT current;
	GetClientRect (hwnd, &current);
	if (current.right - current.left == width && current.bottom - current.top == height)
		return;
	WINDOWINFO windowInfo {0};
	GetWindowInfo (hwnd, &windowInfo);
	RECT clientRect {};
	clientRect.right = width;
	clientRect.bottom = height;
	AdjustWindowRectEx (&clientRect, windowInfo.dwStyle, TRUE, windowInfo.dwExStyle);
	SetWindowPos (hwnd, HWND_TOP, 0, 0, clientRect.right - clientRect.left,
	              clientRect.bottom - clientRect.top, SWP_NOMOVE | SWP_NOCOPYBITS | SWP_NOACTIVATE);
}

//------------------------------------------------------------------------
bool EditorWindow::show ()
{
	if (!plugView || !hwnd)
		return false;

	self = shared_from_this ();

	onContentScaleFactorChanged (getContentScaleFactor ());

	if (plugView->isPlatformTypeSupported (kPlatformTypeHWND) != kResultTrue)
	{
		self = nullptr;
		return false;
	}

	plugView->setFrame (this);

	if (plugView->attached (hwnd, kPlatformTypeHWND) != kResultTrue)
	{
		plugView->setFrame (nullptr);
		self = nullptr;
		return false;
	}
	viewAttached = true;

	SetTimer (hwnd, kDirtyPollTimerId, kDirtyPollIntervalMs, nullptr);
	timerActive = true;

	SetWindowPos (hwnd, HWND_TOP, 0, 0, 0, 0,
	              SWP_NOSIZE | SWP_NOMOVE | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
	return true;
}

//------------------------------------------------------------------------
void EditorWindow::closePlugView ()
{
	if (plugView)
	{
		plugView->setFrame (nullptr);
		if (viewAttached)
		{
			plugView->removed ();
			viewAttached = false;
		}
		plugView = nullptr;
	}
}

//------------------------------------------------------------------------
void EditorWindow::requestClose ()
{
	if (timerActive)
	{
		KillTimer (hwnd, kDirtyPollTimerId);
		timerActive = false;
	}
	// Unconditional final save (design-cli: "final capture and write before exit"). saved = "a write
	// occurred". Dormant (no target) no-ops. Decided: keep save(), not saveIfDirty().
	if (presetManager && presetManager->hasTarget () && !presetManager->save ())
		std::fprintf (stderr,
		              "Warning: failed to save preset on close; state was not persisted.\n");
	closePlugView ();
	SetWindowLongPtr (hwnd, GWLP_USERDATA, (LONG_PTR) nullptr);
	HWND toDestroy = hwnd;
	hwnd = nullptr;
	DestroyWindow (toDestroy);
	platform::quitEventLoop ();
	self = nullptr;
}

//------------------------------------------------------------------------
void EditorWindow::updateTitle ()
{
	std::string title = className;
	if (presetManager && presetManager->hasTarget ())
		title += " — " + fileNameOf (presetManager->targetPath ());
	SetWindowTextW (hwnd, widen (title).c_str ());
}

//------------------------------------------------------------------------
void EditorWindow::postSaveRequest ()
{
	if (hwnd)
		PostMessage (hwnd, WM_VSTDEMON_SAVE, 0, 0);
}

namespace {

// Run a native IFileDialog of the given CLSID (FileOpen/FileSave) configured for .vstpreset, and
// return the chosen UTF-8 filesystem path — empty on cancel or any failure. configure() sets
// mode-specific options (default filename/extension). All diagnostics go to stderr.
template <typename Configure>
std::string runFileDialog (HWND owner, REFCLSID clsid, const wchar_t* dialogTitle,
                           Configure&& configure)
{
	IFileDialog* dialog = nullptr;
	if (CoCreateInstance (clsid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS (&dialog)) != S_OK)
	{
		std::fprintf (stderr, "Could not open the file dialog.\n");
		return {};
	}

	dialog->SetFileTypes (ARRAYSIZE (kPresetFilter), kPresetFilter);
	dialog->SetTitle (dialogTitle);
	configure (dialog);

	std::string path;
	if (dialog->Show (owner) == S_OK)
	{
		IShellItem* item = nullptr;
		if (dialog->GetResult (&item) == S_OK)
		{
			PWSTR widePath = nullptr;
			if (item->GetDisplayName (SIGDN_FILESYSPATH, &widePath) == S_OK)
			{
				path = narrow (widePath);
				CoTaskMemFree (widePath);
			}
			item->Release ();
		}
	}

	dialog->Release ();
	return path;
}

} // namespace

//------------------------------------------------------------------------
void EditorWindow::pauseDirtyPoll ()
{
	// IFileDialog::Show runs a nested modal message loop; suspend the 1s poll across it so a WM_TIMER
	// can't auto-save mid-dialog (e.g. persisting pre-Open state to the old target the user is about
	// to leave). Re-armed by resumeDirtyPoll after the dialog returns.
	if (timerActive)
	{
		KillTimer (hwnd, kDirtyPollTimerId);
		timerActive = false;
	}
}

//------------------------------------------------------------------------
void EditorWindow::resumeDirtyPoll ()
{
	if (!timerActive && hwnd)
	{
		SetTimer (hwnd, kDirtyPollTimerId, kDirtyPollIntervalMs, nullptr);
		timerActive = true;
	}
}

//------------------------------------------------------------------------
void EditorWindow::onOpenPreset ()
{
	if (!presetManager)
		return;

	std::string path;
	{
		DialogScope scope (*this);
		path = runFileDialog (hwnd, CLSID_FileOpenDialog, L"Open Preset", [] (IFileDialog*) {});
	}
	if (path.empty ())
		return; // cancelled or failed (diagnostics already on stderr)

	auto result = presetManager->openPreset (path);
	if (!result.ok)
		std::fprintf (stderr, "%s\n", result.error.c_str ()); // keep the current session and target
}

//------------------------------------------------------------------------
void EditorWindow::onSavePresetAs ()
{
	if (!presetManager)
		return;

	std::string suggested = presetManager->hasTarget ()
	                            ? fileNameOf (presetManager->targetPath ())
	                            : className + ".vstpreset";

	std::string path;
	{
		DialogScope scope (*this);
		path = runFileDialog (hwnd, CLSID_FileSaveDialog, L"Save Preset As", [&] (IFileDialog* dialog) {
			dialog->SetDefaultExtension (L"vstpreset");
			dialog->SetFileName (widen (suggested).c_str ());
		});
	}
	if (path.empty ())
		return; // cancelled or failed

	if (!presetManager->saveAs (path))
		std::fprintf (stderr, "Warning: failed to save preset to '%s'.\n", path.c_str ());
}

//------------------------------------------------------------------------
tresult PLUGIN_API EditorWindow::resizeView (IPlugView* view, ViewRect* newSize)
{
	if (newSize == nullptr || view == nullptr || view != plugView)
		return kInvalidArgument;
	if (!hwnd)
		return kInternalError;
	if (resizeViewRecursionGuard)
		return kResultFalse;
	ViewRect r;
	if (plugView->getSize (&r) != kResultTrue)
		return kInternalError;
	if (r.left == newSize->left && r.top == newSize->top && r.right == newSize->right &&
	    r.bottom == newSize->bottom)
		return kResultTrue;

	resizeViewRecursionGuard = true;
	resizeContent (newSize->right - newSize->left, newSize->bottom - newSize->top);
	resizeViewRecursionGuard = false;

	if (plugView->getSize (&r) != kResultTrue)
		return kInternalError;
	if (r.left != newSize->left || r.top != newSize->top || r.right != newSize->right ||
	    r.bottom != newSize->bottom)
		plugView->onSize (newSize);
	return kResultTrue;
}

//------------------------------------------------------------------------
tresult PLUGIN_API EditorWindow::queryInterface (const TUID iid, void** obj)
{
	if (!obj)
		return kInvalidArgument;
	if (Steinberg::FUnknownPrivate::iidEqual (iid, IPlugFrame::iid) ||
	    Steinberg::FUnknownPrivate::iidEqual (iid, FUnknown::iid))
	{
		*obj = this;
		addRef ();
		return kResultTrue;
	}
	*obj = nullptr;
	return kNoInterface;
}

//------------------------------------------------------------------------
std::shared_ptr<PlatformWindow> makePlatformWindow (const std::string& title,
                                                    const IPtr<IPlugView>& view,
                                                    PresetManager* presetManager)
{
	return EditorWindow::make (title, view, presetManager);
}

} // namespace vstdemon
