#pragma once

#include <string>
#include <windows.h>

namespace vstdemon {

// UTF-8 <-> UTF-16 conversion for the Win32 wide APIs. The process runs as UTF-8
// (src/vst-demon.manifest activeCodePage), so narrow strings — argv, preset paths — are UTF-8;
// these bridge to the wide Win32 calls (GetFileAttributesW / MoveFileExW / SetWindowTextW / ...).
inline std::wstring widen (const std::string& s)
{
	if (s.empty ())
		return {};
	int len = MultiByteToWideChar (CP_UTF8, 0, s.data (), static_cast<int> (s.size ()), nullptr, 0);
	std::wstring out (static_cast<size_t> (len), L'\0');
	MultiByteToWideChar (CP_UTF8, 0, s.data (), static_cast<int> (s.size ()), out.data (), len);
	return out;
}

inline std::string narrow (const wchar_t* s)
{
	if (!s || !*s)
		return {};
	int len = WideCharToMultiByte (CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 0)
		return {};
	std::string out (static_cast<size_t> (len - 1), '\0');
	WideCharToMultiByte (CP_UTF8, 0, s, -1, out.data (), len, nullptr, nullptr);
	return out;
}

} // namespace vstdemon
