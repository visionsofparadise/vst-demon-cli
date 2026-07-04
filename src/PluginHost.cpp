#include "PluginHost.h"

#include "WindowMessages.h"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <sstream>

using Steinberg::FUID;
using Steinberg::FUnknown;
using Steinberg::IPlugView;
using Steinberg::IPtr;
using Steinberg::kInvalidArgument;
using Steinberg::kNoInterface;
using Steinberg::kResultOk;
using Steinberg::kResultTrue;
using Steinberg::owned;
using Steinberg::tresult;
using Steinberg::TUID;
using Steinberg::uint32;
using Steinberg::Vst::IComponent;
using Steinberg::Vst::IComponentHandler;
using Steinberg::Vst::IComponentHandler2;
using Steinberg::Vst::IEditController;
using Steinberg::Vst::ParamID;
using Steinberg::Vst::ParamValue;
using Steinberg::Vst::PlugProvider;
using Steinberg::Vst::PluginContextFactory;
using Steinberg::Vst::ViewType::kEditor;

namespace vstdemon {

//------------------------------------------------------------------------
void ComponentHandler::requestSave ()
{
	if (hwnd)
		PostMessage (hwnd, WM_VSTDEMON_SAVE, 0, 0);
}

tresult PLUGIN_API ComponentHandler::beginEdit (ParamID)
{
	return kResultOk;
}

tresult PLUGIN_API ComponentHandler::performEdit (ParamID, ParamValue)
{
	return kResultOk;
}

tresult PLUGIN_API ComponentHandler::endEdit (ParamID)
{
	requestSave ();
	return kResultOk;
}

tresult PLUGIN_API ComponentHandler::restartComponent (Steinberg::int32)
{
	requestSave ();
	return kResultOk;
}

tresult PLUGIN_API ComponentHandler::setDirty (Steinberg::TBool state)
{
	if (state)
		requestSave ();
	return kResultOk;
}

tresult PLUGIN_API ComponentHandler::requestOpenEditor (Steinberg::FIDString)
{
	return kResultOk;
}

tresult PLUGIN_API ComponentHandler::startGroupEdit ()
{
	return kResultOk;
}

tresult PLUGIN_API ComponentHandler::finishGroupEdit ()
{
	return kResultOk;
}

tresult PLUGIN_API ComponentHandler::queryInterface (const TUID iid, void** obj)
{
	if (!obj)
		return kInvalidArgument;
	if (Steinberg::FUnknownPrivate::iidEqual (iid, IComponentHandler::iid) ||
	    Steinberg::FUnknownPrivate::iidEqual (iid, FUnknown::iid))
	{
		*obj = static_cast<IComponentHandler*> (this);
		addRef ();
		return kResultTrue;
	}
	if (Steinberg::FUnknownPrivate::iidEqual (iid, IComponentHandler2::iid))
	{
		*obj = static_cast<IComponentHandler2*> (this);
		addRef ();
		return kResultTrue;
	}
	*obj = nullptr;
	return kNoInterface;
}

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

} // namespace

//------------------------------------------------------------------------
const char* PluginHost::emptyFactoryError ()
{
	return "The plugin's factory is empty (no audio-effect classes). For a Waves WaveShell this "
	       "means the Waves license is inactive — activate it in Waves Central and retry.";
}

//------------------------------------------------------------------------
PluginHost::PluginHost ()
{
	PluginContextFactory::instance ().setPluginContext (&pluginContext);
}

//------------------------------------------------------------------------
PluginHost::~PluginHost ()
{
	// Uninstall the edit-notification handler before teardown: the editor window it posts to is
	// already destroyed by this point (main.cpp destroys the window before the host), and
	// deactivation/release below can drive controller callbacks. Deregistering here — and dropping
	// the handler's stale HWND — keeps any late callback from reaching a dead window.
	if (plugProvider)
	{
		if (auto* controller = plugProvider->getControllerPtr ().get ())
			controller->setComponentHandler (nullptr);
	}
	handler.setWindow (nullptr);

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
		error = emptyFactoryError ();
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

	auto controller = plugProvider->getControllerPtr ();
	if (!controller)
	{
		return {false,
		        "Plugin class '" + classInfo->name () + "' provides no edit controller (no editor)."};
	}

	controller->setComponentHandler (&handler);

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
