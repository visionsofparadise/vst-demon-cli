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

	// Set the title bar to "<class name> — <preset filename>" when a target is set (filename only),
	// or "<class name>" alone when dormant. Called on every retarget (main.cpp's onRetarget).
	void updateTitle ();

	HWND getHwnd () const { return hwnd; }

	// IPlugFrame
	Steinberg::tresult PLUGIN_API resizeView (IPlugView* view, ViewRect* newSize) override;

private:
	bool init (const std::string& title, const IPtr<IPlugView>& view);

	static LRESULT CALLBACK wndProc (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT proc (UINT message, WPARAM wParam, LPARAM lParam);

	// File menu handlers. Open/Save-As drive a native IFileDialog; Close shares the window-X path.
	void onOpenPreset ();
	void onSavePresetAs ();
	void requestClose ();

	// RAII scope guard around a modal file dialog: suspends the 1s dirty poll and sets dialogOpen for
	// the dialog's lifetime, restoring both when it returns (on OK and Cancel alike). dialogOpen gates
	// BOTH saveIfDirty entry points — the WM_TIMER poll AND the posted WM_VSTDEMON_SAVE — so neither
	// writes pre-dialog state to the old target while IFileDialog::Show pumps its nested modal loop.
	struct DialogScope
	{
		explicit DialogScope (EditorWindow& owner) : window (owner)
		{
			window.dialogOpen = true;
			window.pauseDirtyPoll ();
		}
		~DialogScope ()
		{
			window.resumeDirtyPoll ();
			window.dialogOpen = false;
		}
		DialogScope (const DialogScope&) = delete;
		DialogScope& operator= (const DialogScope&) = delete;

	private:
		EditorWindow& window;
	};

	// Suspend/re-arm the 1s dirty poll around a modal file dialog (its nested loop keeps pumping
	// WM_TIMER otherwise). Driven by DialogScope.
	void pauseDirtyPoll ();
	void resumeDirtyPoll ();

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
	std::string className;
	HWND hwnd {nullptr};
	std::shared_ptr<EditorWindow> self;

	bool viewAttached {false};
	bool timerActive {false};
	bool dialogOpen {false};

	bool resizeViewRecursionGuard {false};
	bool inDpiChangeState {false};
	int dpiChangedWidth {0};
	int dpiChangedHeight {0};
};

} // namespace vstdemon
