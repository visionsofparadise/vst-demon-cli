cmake_minimum_required(VERSION 3.25)

project(vst-demon
	VERSION 0.2.1
	LANGUAGES CXX
)

# On macOS the sources include Objective-C++ (.mm), and building the integration-test fixture pulls
# in VSTGUI's Cocoa backend, whose CMake needs OBJCXX formally enabled (CMAKE_OBJCXX_COMPILE_OBJECT).
# Declared here rather than in project(LANGUAGES) so the Windows/Linux legs never probe an ObjC++
# compiler they don't have.
if(APPLE)
	enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The SDK knobs are FORCE'd so a stray cache value can never flip them; the one sanctioned way to
# deviate is VSTDEMON_BUILD_TEST_FIXTURE, which builds the SDK's `again` example plugin (and the
# VSTGUI it needs) as the integration-test fixture — see tests/integration.sh.
option(VSTDEMON_BUILD_TEST_FIXTURE "Build the SDK 'again' example plugin as the integration-test fixture" OFF)
if(VSTDEMON_BUILD_TEST_FIXTURE)
	set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES ON CACHE BOOL "" FORCE)
	set(SMTG_ENABLE_VSTGUI_SUPPORT ON CACHE BOOL "" FORCE)
	set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
	# Never symlink the fixture plugin into the machine's real VST3 folder (also needs privileges CI
	# and plain user shells don't have).
	set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
else()
	set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
endif()
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)

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
		src/win32/vst-demon.rc
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
