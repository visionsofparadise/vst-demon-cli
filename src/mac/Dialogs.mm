#import "Dialogs.h"

#import <Cocoa/Cocoa.h>

#if !__has_feature(objc_arc)
#error this file needs to be compiled with automatic reference counting enabled
#endif

namespace vstdemon {

//------------------------------------------------------------------------
std::optional<std::string> runOpenPresetDialog (const std::string& /*suggestedName*/)
{
	NSOpenPanel* panel = [NSOpenPanel openPanel];
	panel.title = @"Open Preset";
	panel.allowsMultipleSelection = NO;
	panel.canChooseDirectories = NO;
	panel.canChooseFiles = YES;
	// allowedFileTypes is deprecated in favour of UTType's allowedContentTypes (macOS 11+), but the
	// deployment target is the SDK's 10.13 — the extension-string API is the one that compiles there.
	panel.allowedFileTypes = @[ @"vstpreset" ];

	if ([panel runModal] != NSModalResponseOK)
		return std::nullopt;
	NSURL* url = panel.URLs.firstObject;
	if (!url)
		return std::nullopt;
	return std::string (url.path.UTF8String);
}

//------------------------------------------------------------------------
std::optional<std::string> runSavePresetDialog (const std::string& suggestedName)
{
	NSSavePanel* panel = [NSSavePanel savePanel];
	panel.title = @"Save Preset As";
	panel.nameFieldStringValue = [NSString stringWithUTF8String:suggestedName.c_str ()];
	panel.allowedFileTypes = @[ @"vstpreset" ];

	if ([panel runModal] != NSModalResponseOK)
		return std::nullopt;
	NSURL* url = panel.URL;
	if (!url)
		return std::nullopt;
	return std::string (url.path.UTF8String);
}

} // namespace vstdemon
