#pragma once

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"

#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <optional>
#include <string>
#include <vector>
#include <windows.h>

namespace vstdemon {

struct HostResult
{
	bool ok {false};
	std::string error;
};

// The host's edit-notification handler. Installed unconditionally on the controller before the
// view is created. The save-requesting callbacks (endEdit / restartComponent / setDirty) post
// WM_VSTDEMON_SAVE to the editor window rather than saving inline, so the actual save runs on the
// single message-loop code path shared with the 1s dirty-poll timer. All callbacks arrive on the
// UI thread; no locking. Lifetime is owned by PluginHost, so the FUnknown refcount is a stub.
class ComponentHandler : public Steinberg::Vst::IComponentHandler,
                         public Steinberg::Vst::IComponentHandler2
{
public:
	// The window to post save requests to. Set after the editor window is created; until then
	// callbacks are dropped (no window to catch the message yet, and startup load fires no edits).
	void setWindow (HWND window) { hwnd = window; }

	// IComponentHandler
	Steinberg::tresult PLUGIN_API beginEdit (Steinberg::Vst::ParamID id) override;
	Steinberg::tresult PLUGIN_API performEdit (Steinberg::Vst::ParamID id,
	                                           Steinberg::Vst::ParamValue valueNormalized) override;
	Steinberg::tresult PLUGIN_API endEdit (Steinberg::Vst::ParamID id) override;
	Steinberg::tresult PLUGIN_API restartComponent (Steinberg::int32 flags) override;

	// IComponentHandler2
	Steinberg::tresult PLUGIN_API setDirty (Steinberg::TBool state) override;
	Steinberg::tresult PLUGIN_API requestOpenEditor (Steinberg::FIDString name) override;
	Steinberg::tresult PLUGIN_API startGroupEdit () override;
	Steinberg::tresult PLUGIN_API finishGroupEdit () override;

	// FUnknown — stub refcount; lifetime is owned by PluginHost.
	Steinberg::tresult PLUGIN_API queryInterface (const Steinberg::TUID iid, void** obj) override;
	Steinberg::uint32 PLUGIN_API addRef () override { return 1000; }
	Steinberg::uint32 PLUGIN_API release () override { return 1000; }

private:
	void requestSave ();

	HWND hwnd {nullptr};
};

class PluginHost
{
public:
	PluginHost ();
	~PluginHost ();

	PluginHost (const PluginHost&) = delete;
	PluginHost& operator= (const PluginHost&) = delete;

	// Load the module only; no provider is initialized. Used for --list.
	HostResult loadModule (const std::string& pluginPath);

	// Class names of every kVstAudioEffectClass class in the loaded module.
	std::vector<std::string> effectClassNames () const;

	// The error shown when a module's factory exposes no audio-effect class
	// (a Waves shell with an inactive license, most commonly).
	static const char* emptyFactoryError ();

	// Load the module, select a class, and bring up component/controller/view.
	// pluginName selects a class by ClassInfo::name(); empty picks the sole/first effect class.
	HostResult open (const std::string& pluginPath, const std::string& pluginName);

	Steinberg::Vst::IComponent* getComponent () const;
	Steinberg::Vst::IEditController* getController () const;
	bool getComponentUID (Steinberg::FUID& uid) const;
	Steinberg::IPtr<Steinberg::IPlugView> createView () const;

	// The installed edit-notification handler. Wire its target window after the editor is created.
	ComponentHandler& componentHandler () { return handler; }

	const std::string& selectedClassName () const { return selectedName; }

private:
	std::optional<Steinberg::Vst::PlugProvider::ClassInfo> selectClass (const std::string& pluginName,
	                                                                     std::string& error) const;

	Steinberg::Vst::HostApplication pluginContext;
	VST3::Hosting::Module::Ptr module {nullptr};
	Steinberg::IPtr<Steinberg::Vst::PlugProvider> plugProvider {nullptr};
	ComponentHandler handler;
	std::string selectedName;
	bool componentActivated {false};
};

} // namespace vstdemon
