#pragma once

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"

#include <optional>
#include <string>
#include <vector>

namespace vstdemon {

struct HostResult
{
	bool ok {false};
	std::string error;
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

	const std::string& selectedClassName () const { return selectedName; }

private:
	std::optional<Steinberg::Vst::PlugProvider::ClassInfo> selectClass (const std::string& pluginName,
	                                                                     std::string& error) const;

	Steinberg::Vst::HostApplication pluginContext;
	VST3::Hosting::Module::Ptr module {nullptr};
	Steinberg::IPtr<Steinberg::Vst::PlugProvider> plugProvider {nullptr};
	std::string selectedName;
	bool componentActivated {false};
};

} // namespace vstdemon
