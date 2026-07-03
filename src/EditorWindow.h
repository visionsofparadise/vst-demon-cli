#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"

#include <memory>
#include <string>
#include <windows.h>

namespace vstdemon {

class PresetManager;

using Steinberg::FUnknown;
using Steinberg::IPlugFrame;
using Steinberg::IPlugView;
using Steinberg::IPtr;
using Steinberg::TUID;
using Steinberg::ViewRect;

class EditorWindow : public IPlugFrame, public std::enable_shared_from_this<EditorWindow>
{
public:
	static std::shared_ptr<EditorWindow> make (const std::string& title, const IPtr<IPlugView>& view,
	                                            PresetManager* presetManager);

	~EditorWindow () noexcept;

	bool show ();
	void closePlugView ();

	HWND getHwnd () const { return hwnd; }

	// IPlugFrame
	Steinberg::tresult PLUGIN_API resizeView (IPlugView* view, ViewRect* newSize) override;

private:
	bool init (const std::string& title, const IPtr<IPlugView>& view);

	static LRESULT CALLBACK wndProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT proc (UINT message, WPARAM wParam, LPARAM lParam);

	void onResize (int width, int height);
	void constrainSizing (RECT* newSize);
	void onContentScaleFactorChanged (float scaleFactor);
	float getContentScaleFactor () const;
	void resizeContent (int width, int height);

	static void registerWindowClass (HINSTANCE instance);

	// FUnknown
	Steinberg::tresult PLUGIN_API queryInterface (const TUID iid, void** obj) override;
	Steinberg::uint32 PLUGIN_API addRef () override { return 1000; }
	Steinberg::uint32 PLUGIN_API release () override { return 1000; }

	IPtr<IPlugView> plugView;
	PresetManager* presetManager {nullptr};
	HWND hwnd {nullptr};
	std::shared_ptr<EditorWindow> self;

	bool viewAttached {false};
	bool timerActive {false};

	bool resizeViewRecursionGuard {false};
	bool inDpiChangeState {false};
	int dpiChangedWidth {0};
	int dpiChangedHeight {0};
};

} // namespace vstdemon
