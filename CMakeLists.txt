cmake_minimum_required(VERSION 3.20)
project(WaveyStudio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies from vcpkg ──
find_package(SDL2 CONFIG REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)

include(FetchContent)

# ── 1. Luau from GitHub ──
FetchContent_Declare(
    luau
    GIT_REPOSITORY https://github.com/luau-lang/luau.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(luau)

# ── 2. Dear ImGui (Docking) from GitHub ──
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_docking STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_docking PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_docking PUBLIC
    SDL2::SDL2 GLEW::GLEW OpenGL::GL
)

# ── 3. Jolt Physics Direct Source Build (100% Reliable) ──
FetchContent_Declare(
    joltphysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.2.0
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(joltphysics)
if(NOT joltphysics_POPULATED)
    FetchContent_Populate(joltphysics)
endif()

file(GLOB_RECURSE JOLT_SOURCES "${joltphysics_SOURCE_DIR}/Jolt/*.cpp")

add_library(Jolt STATIC ${JOLT_SOURCES})
target_include_directories(Jolt PUBLIC ${joltphysics_SOURCE_DIR})
target_compile_options(Jolt PRIVATE /WX- /wd4865)

# ── 4. Wavey Studio Executable ──
add_executable(WaveyStudio
    src/scripting/LuauBridge.cpp
    src/scripting/Scheduler.cpp
    src/engine/GameServices.cpp
    src/editor/ScriptEditor.cpp
    src/editor/AutoComplete.cpp
    src/editor/IconManager.cpp
    src/engine/JoltPhysics.cpp
    src/editor/UndoStack.cpp
    src/engine/Renderer.cpp
    src/engine/Physics.cpp
    src/editor/Editor.cpp
    src/core/Instance.cpp
    src/TextEditor.cpp
    src/ImGuizmo.cpp
    src/main.cpp
)

target_include_directories(WaveyStudio PRIVATE
    ${luau_SOURCE_DIR}/VM/include
    ${luau_SOURCE_DIR}/Compiler/include
    ${luau_SOURCE_DIR}/Common/include
    ${joltphysics_SOURCE_DIR}
)

target_link_libraries(WaveyStudio PRIVATE
    Jolt
    imgui_docking
    SDL2::SDL2
    SDL2::SDL2main
    GLEW::GLEW
    OpenGL::GL
    Luau.Compiler
    Luau.VM
    comdlg32
    psapi
)