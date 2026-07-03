cmake_minimum_required(VERSION 3.25)

project(vst-demon
	VERSION 0.1.0
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
	src/EditorWindow.cpp
	src/PresetManager.cpp
	vst3sdk/public.sdk/source/vst/hosting/plugprovider.cpp
	vst3sdk/public.sdk/source/vst/hosting/module_win32.cpp
	vst3sdk/public.sdk/source/common/memorystream.cpp
)

target_link_libraries(vst-demon PRIVATE ole32)

target_include_directories(vst-demon PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/vst3sdk"
)

target_link_libraries(vst-demon PRIVATE sdk_hosting)
