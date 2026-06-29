cmake_minimum_required(VERSION 3.15)
project(ClashMonitorPlugin VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build as DLL
add_library(ClashMonitorPlugin SHARED
    PluginInterface.h
    pch.h
    pch.cpp
    framework.h
    dllmain.cpp
    ClashPlugin.h
    ClashPlugin.cpp
    ClashNodeItem.h
    ClashNodeItem.cpp
    HttpHelper.h
    HttpHelper.cpp
    JsonParser.h
    JsonParser.cpp
    Settings.h
    Settings.cpp
    SystemProxy.h
    SystemProxy.cpp
)

target_include_directories(ClashMonitorPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Use static CRT (no VC++ redist dependency)
set_property(TARGET ClashMonitorPlugin PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_link_libraries(ClashMonitorPlugin PRIVATE
    winhttp
    wininet
    gdi32
    user32
)

# Precompiled header
target_precompile_headers(ClashMonitorPlugin PRIVATE pch.h)

# Output DLL to build/ directory
set_target_properties(ClashMonitorPlugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
)
