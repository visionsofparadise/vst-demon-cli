#include "PresetManager.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"

#include <cstdio>
#include <filesystem>
#include <windows.h>

using Steinberg::FUID;
using Steinberg::IBStream;
using Steinberg::IPtr;
using Steinberg::MemoryStream;
using Steinberg::owned;
using Steinberg::Vst::FileStream;
using Steinberg::Vst::PresetFile;

namespace vstdemon {

namespace {

std::wstring widen (const std::string& s)
{
	if (s.empty ())
		return {};
	int len = MultiByteToWideChar (CP_UTF8, 0, s.data (), static_cast<int> (s.size ()), nullptr, 0);
	std::wstring out (static_cast<size_t> (len), L'\0');
	MultiByteToWideChar (CP_UTF8, 0, s.data (), static_cast<int> (s.size ()), out.data (), len);
	return out;
}

bool fileExists (const std::string& path)
{
	DWORD attrs = GetFileAttributesW (widen (path).c_str ());
	return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::string parentDirectory (const std::string& path)
{
	size_t pos = path.find_last_of ("/\\");
	return pos == std::string::npos ? std::string () : path.substr (0, pos);
}

// Create the parent directory of `path` (recursively) if it does not already exist. Succeeds when
// the directory exists or `path` has no directory component. On failure sets `error`.
bool ensureParentDir (const std::string& path, std::string& error)
{
	std::string dir = parentDirectory (path);
	if (dir.empty ())
		return true;

	std::wstring wdir = widen (dir);
	DWORD attrs = GetFileAttributesW (wdir.c_str ());
	if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
		return true;

	std::error_code ec;
	std::filesystem::create_directories (std::filesystem::path (wdir), ec);
	if (ec)
	{
		error = "Could not create directory '" + dir + "' for preset '" + path + "'.";
		return false;
	}
	return true;
}

// Read a MemoryStream's full contents into a byte vector.
std::vector<char> drain (MemoryStream& stream)
{
	Steinberg::int64 size = 0;
	stream.seek (0, Steinberg::IBStream::kIBSeekEnd, &size);
	std::vector<char> out (static_cast<size_t> (size));
	stream.seek (0, Steinberg::IBStream::kIBSeekSet, nullptr);
	if (!out.empty ())
	{
		Steinberg::int32 read = 0;
		stream.read (out.data (), static_cast<Steinberg::int32> (out.size ()), &read);
		out.resize (static_cast<size_t> (read));
	}
	return out;
}

} // namespace

//------------------------------------------------------------------------
PresetManager::PresetManager (Steinberg::Vst::IComponent* component,
                              Steinberg::Vst::IEditController* controller, const FUID& componentUID)
    : component (component), controller (controller), componentUID (componentUID)
{
}

//------------------------------------------------------------------------
void PresetManager::setTarget (const std::string& path)
{
	if (path == target)
		return;
	target = path;
	if (onRetarget)
		onRetarget (target);
}

//------------------------------------------------------------------------
void PresetManager::announceTarget ()
{
	if (!target.empty () && onRetarget)
		onRetarget (target);
}

//------------------------------------------------------------------------
PresetResult PresetManager::prepareTarget ()
{
	if (target.empty ())
		return {true, {}};

	std::string error;
	if (!ensureParentDir (target, error))
		return {false, error};
	return {true, {}};
}

//------------------------------------------------------------------------
PresetResult PresetManager::loadFile (const std::string& path)
{
	IPtr<IBStream> stream = owned (FileStream::open (path.c_str (), "rb"));
	if (!stream)
		return {false, "Could not open preset file '" + path + "' for reading."};

	if (!PresetFile::loadPreset (stream, componentUID, component, controller))
		return {false,
		        "Failed to load preset '" + path +
		            "': the file is unreadable or was authored for a different plugin class."};

	std::vector<char> componentBytes, controllerBytes;
	if (captureState (componentBytes, controllerBytes))
	{
		lastComponentState = std::move (componentBytes);
		lastControllerState = std::move (controllerBytes);
		haveLastWritten = true;
	}

	return {true, {}};
}

//------------------------------------------------------------------------
PresetResult PresetManager::load ()
{
	if (target.empty () || !fileExists (target))
		return {true, {}};

	return loadFile (target);
}

//------------------------------------------------------------------------
PresetResult PresetManager::openPreset (const std::string& path)
{
	auto result = loadFile (path);
	if (!result.ok)
		return result;

	setTarget (path);
	return {true, {}};
}

//------------------------------------------------------------------------
bool PresetManager::captureState (std::vector<char>& componentBytes,
                                  std::vector<char>& controllerBytes) const
{
	if (!component)
		return false;

	MemoryStream componentStream;
	if (component->getState (&componentStream) != Steinberg::kResultOk)
		return false;
	componentBytes = drain (componentStream);

	controllerBytes.clear ();
	if (controller)
	{
		MemoryStream controllerStream;
		if (controller->getState (&controllerStream) == Steinberg::kResultOk)
			controllerBytes = drain (controllerStream);
	}

	return true;
}

//------------------------------------------------------------------------
bool PresetManager::writePreset (const std::string& path)
{
	std::string dirError;
	if (!ensureParentDir (path, dirError))
	{
		std::fprintf (stderr, "%s\n", dirError.c_str ());
		return false;
	}

	std::string tmp = path + ".tmp";

	{
		IPtr<IBStream> stream = owned (FileStream::open (tmp.c_str (), "wb"));
		if (!stream)
		{
			std::fprintf (stderr, "Could not open '%s' for writing.\n", tmp.c_str ());
			return false;
		}
		if (!PresetFile::savePreset (stream, componentUID, component, controller))
		{
			std::fprintf (stderr, "Failed to serialize preset state for '%s'.\n", path.c_str ());
			stream = nullptr;
			DeleteFileW (widen (tmp).c_str ());
			return false;
		}
	}

	if (!MoveFileExW (widen (tmp).c_str (), widen (path).c_str (), MOVEFILE_REPLACE_EXISTING))
	{
		std::fprintf (stderr, "Failed to move '%s' over '%s' (error %lu).\n", tmp.c_str (),
		              path.c_str (), GetLastError ());
		DeleteFileW (widen (tmp).c_str ());
		return false;
	}

	std::vector<char> componentBytes, controllerBytes;
	if (captureState (componentBytes, controllerBytes))
	{
		lastComponentState = std::move (componentBytes);
		lastControllerState = std::move (controllerBytes);
		haveLastWritten = true;
	}

	return true;
}

//------------------------------------------------------------------------
bool PresetManager::save ()
{
	if (target.empty ())
		return false;
	if (!writePreset (target))
		return false;
	if (onSaved)
		onSaved (target);
	return true;
}

//------------------------------------------------------------------------
bool PresetManager::saveAs (const std::string& path)
{
	// Write first, retarget only on success — symmetric with openPreset (a failed Save-As must not
	// switch the auto-save target or announce a preset-path for a file that never got written).
	if (!writePreset (path))
		return false;
	setTarget (path);   // fires onRetarget -> preset-path (before saved, per the event contract)
	if (onSaved)
		onSaved (path);
	return true;
}

//------------------------------------------------------------------------
bool PresetManager::saveIfDirty ()
{
	if (target.empty ())
		return false;

	std::vector<char> componentBytes, controllerBytes;
	if (!captureState (componentBytes, controllerBytes))
		return false;

	if (haveLastWritten && componentBytes == lastComponentState &&
	    controllerBytes == lastControllerState)
		return false;

	if (!writePreset (target))
		return false;
	if (onSaved)
		onSaved (target);
	return true;
}

} // namespace vstdemon
