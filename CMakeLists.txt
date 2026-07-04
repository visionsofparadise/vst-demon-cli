cmake_minimum_required(VERSION 3.25)

project(vst-demon
	VERSION 0.2.0
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)

add_subdirectory(vst3sdk)

add_executable(vst-demon
	src/main.cpp
	src/PluginHost.cpp
	src/PresetManager.cpp
	vst3sdk/public.sdk/source/vst/hosting/plugprovider.cpp
	vst3sdk/public.sdk/source/common/memorystream.cpp
)

if(WIN32)
	target_sources(vst-demon PRIVATE
		src/win32/EditorWindow.cpp
		src/win32/PlatformWin32.cpp
		src/vst-demon.manifest
		vst3sdk/public.sdk/source/vst/hosting/module_win32.cpp
	)
	target_link_libraries(vst-demon PRIVATE ole32)
elseif(APPLE)
	set(VSTDEMON_MAC_SOURCES
		src/mac/AppMac.mm
		src/mac/EditorWindowMac.mm
		src/mac/Dialogs.mm
		vst3sdk/public.sdk/source/vst/hosting/module_mac.mm
	)
	target_sources(vst-demon PRIVATE ${VSTDEMON_MAC_SOURCES})
	# ARC is mandatory on every Objective-C++ TU, including the SDK's module_mac.mm.
	set_source_files_properties(${VSTDEMON_MAC_SOURCES} PROPERTIES COMPILE_OPTIONS -fobjc-arc)
	target_link_libraries(vst-demon PRIVATE "-framework Cocoa")
elseif(UNIX AND NOT APPLE)
	find_package(X11 REQUIRED)
	target_sources(vst-demon PRIVATE
		src/linux/PlatformLinux.cpp
		src/linux/RunLoop.cpp
		src/linux/EditorWindowX11.cpp
		src/linux/Dialogs.cpp
		vst3sdk/public.sdk/source/vst/hosting/module_linux.cpp
	)
	target_link_libraries(vst-demon PRIVATE ${X11_LIBRARIES} ${CMAKE_DL_LIBS})
endif()

target_include_directories(vst-demon PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/vst3sdk"
)

target_link_libraries(vst-demon PRIVATE sdk_hosting)
