cmake_minimum_required (VERSION 3.1)

project ("WinGamingInput" LANGUAGES CXX)

# require c++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

add_library (WinGamingInput SHARED "src/WindowsGamingInput.cpp" "include/WindowsGamingInput.h" "exports.def")

# use static runtime lib for msvc
set_target_properties(WinGamingInput PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_link_libraries(WinGamingInput PRIVATE runtimeobject)
