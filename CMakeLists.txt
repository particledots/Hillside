cmake_minimum_required(VERSION 3.22)

# macOS build settings. These must be set before project().
# Universal binary (Apple Silicon + Intel) so the same build runs on the M4 Mac mini
# and on the Intel Mac. Deployment target 11.0 = macOS Big Sur or newer.
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build for Apple Silicon and Intel")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

# The version is the plugin's version in Logic. Raise it with every milestone: macOS and Logic cache a plugin's
# details and only refresh them when the installed version goes up.
project(Hillside VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(HILLSIDE_BUILD_PLUGIN "Build the AU/VST3 plugin (needs JUCE)" ON)
option(HILLSIDE_BUILD_TESTS  "Build the engine self-tests and the mapper test" ON)
option(HILLSIDE_BUILD_CHECK  "Build HillsideCheck, the host-less test of the plugin layer (needs JUCE)" OFF)

if(HILLSIDE_BUILD_TESTS)
    enable_testing()
endif()

# 1) The engine: plain C++, no plugin framework. The envelope, the gate and note tracking and the CC stream.
#    Reusable for later plugins (cnoc).
add_subdirectory(engine)

# 2) The plugin wrapper, controls and panel (JUCE).
if(HILLSIDE_BUILD_PLUGIN OR HILLSIDE_BUILD_CHECK)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.17-security
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
    if(HILLSIDE_BUILD_PLUGIN)
        add_subdirectory(plugin)
    endif()
    if(HILLSIDE_BUILD_CHECK)
        add_subdirectory(check)
    endif()
endif()
