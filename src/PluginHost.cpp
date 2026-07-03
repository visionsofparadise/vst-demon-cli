#include "PluginHost.h"

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <sstream>

using Steinberg::FUID;
using Steinberg::IPlugView;
using Steinberg::IPtr;
using Steinberg::owned;
using Steinberg::Vst::IComponent;
using Steinberg::Vst::IEditController;
using Steinberg::Vst::PlugProvider;
using Steinberg::Vst::PluginContextFactory;
using Steinberg::Vst::ViewType::kEditor;

namespace vstdemon {

namespace {

std::string joinClassNames (const std::vector<std::string>& names)
{
	std::ostringstream os;
	for (size_t i = 0; i < names.size (); ++i)
	{
		if (i != 0)
			os << ", ";
		os << names[i];
	}
	return os.str ();
}

const char* kEmptyFactoryError =
    "The plugin's factory is empty (no audio-effect classes). For a Waves WaveShell this means the "
    "Waves license is inactive — activate it in Waves Central and retry.";

} // namespace

//------------------------------------------------------------------------
PluginHost::PluginHost ()
{
	PluginContextFactory::instance ().setPluginContext (&pluginContext);
}

//------------------------------------------------------------------------
PluginHost::~PluginHost ()
{
	if (componentActivated && plugProvider)
	{
		if (auto* component = plugProvider->getComponentPtr ().get ())
			component->setActive (false);
		componentActivated = false;
	}
	plugProvider.reset ();
	module.reset ();
	PluginContextFactory::instance ().setPluginContext (nullptr);
}

//------------------------------------------------------------------------
HostResult PluginHost::loadModule (const std::string& pluginPath)
{
	if (module)
		return {true, {}};

	std::string error;
	module = VST3::Hosting::Module::create (pluginPath, error);
	if (!module)
	{
		std::string reason = "Could not load module '" + pluginPath + "': " + error;
		return {false, reason};
	}
	return {true, {}};
}

//------------------------------------------------------------------------
std::vector<std::string> PluginHost::effectClassNames () const
{
	std::vector<std::string> names;
	if (!module)
		return names;
	for (auto& classInfo : module->getFactory ().classInfos ())
	{
		if (classInfo.category () == kVstAudioEffectClass)
			names.push_back (classInfo.name ());
	}
	return names;
}

//------------------------------------------------------------------------
std::optional<PlugProvider::ClassInfo> PluginHost::selectClass (const std::string& pluginName,
                                                                std::string& error) const
{
	std::vector<PlugProvider::ClassInfo> effectClasses;
	for (auto& classInfo : module->getFactory ().classInfos ())
	{
		if (classInfo.category () == kVstAudioEffectClass)
			effectClasses.push_back (classInfo);
	}

	if (effectClasses.empty ())
	{
		error = kEmptyFactoryError;
		return std::nullopt;
	}

	std::vector<std::string> names;
	for (auto& ci : effectClasses)
		names.push_back (ci.name ());

	if (!pluginName.empty ())
	{
		for (auto& ci : effectClasses)
		{
			if (ci.name () == pluginName)
				return ci;
		}
		error = "No audio-effect class named '" + pluginName + "'. Available classes: " +
		        joinClassNames (names) + ".";
		return std::nullopt;
	}

	if (effectClasses.size () > 1)
	{
		error = "Module contains multiple audio-effect classes; select one with --plugin-name. "
		        "Available classes: " +
		        joinClassNames (names) + ".";
		return std::nullopt;
	}

	return effectClasses.front ();
}

//------------------------------------------------------------------------
HostResult PluginHost::open (const std::string& pluginPath, const std::string& pluginName)
{
	auto loaded = loadModule (pluginPath);
	if (!loaded.ok)
		return loaded;

	std::string error;
	auto classInfo = selectClass (pluginName, error);
	if (!classInfo)
		return {false, error};

	plugProvider = owned (new PlugProvider (module->getFactory (), *classInfo, true));
	if (!plugProvider->initialize ())
	{
		plugProvider = nullptr;
		return {false, "Failed to initialize plugin class '" + classInfo->name () + "'."};
	}

	if (!plugProvider->getControllerPtr ())
	{
		return {false,
		        "Plugin class '" + classInfo->name () + "' provides no edit controller (no editor)."};
	}

	if (auto* component = plugProvider->getComponentPtr ().get ())
	{
		if (component->setActive (true) == Steinberg::kResultOk)
			componentActivated = true;
	}

	selectedName = classInfo->name ();
	return {true, {}};
}

//------------------------------------------------------------------------
IComponent* PluginHost::getComponent () const
{
	return plugProvider ? plugProvider->getComponentPtr ().get () : nullptr;
}

//------------------------------------------------------------------------
IEditController* PluginHost::getController () const
{
	return plugProvider ? plugProvider->getControllerPtr ().get () : nullptr;
}

//------------------------------------------------------------------------
bool PluginHost::getComponentUID (FUID& uid) const
{
	if (!plugProvider)
		return false;
	return plugProvider->getComponentUID (uid) == Steinberg::kResultTrue;
}

//------------------------------------------------------------------------
IPtr<IPlugView> PluginHost::createView () const
{
	auto* controller = getController ();
	if (!controller)
		return nullptr;
	return owned (controller->createView (kEditor));
}

} // namespace vstdemon
