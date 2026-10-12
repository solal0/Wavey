Here is the complete project resume. Save this as a file (e.g., `PROJECT_CONTEXT.md`) in your `C:\Wavey\` root and feed it to Cline.

---

```markdown
# Wavey Studio — Project Context for AI Assistants

## What This Is
Wavey Studio is a custom, standalone 3D game engine and editor inspired by Roblox Studio. It is written in C++17 with Luau scripting, built on Windows using CMake + MSVC + vcpkg. The editor outputs standalone `.exe` games that run without the editor.

## Tech Stack
- **Language:** C++17 (MSVC 19.x, Visual Studio 2026)
- **Scripting:** Luau (Roblox's Lua dialect, fetched via CMake FetchContent from github.com/luau-lang/luau)
- **Windowing/Input:** SDL2 (via vcpkg)
- **Rendering:** OpenGL 3.3 via GLEW (via vcpkg)
- **Editor UI:** Dear ImGui (docking branch, fetched via FetchContent)
- **3D Gizmos:** ImGuizmo (fetched via FetchContent)
- **Physics:** Jolt Physics v5.2.0 (fetched via FetchContent, compiled from source)
- **Fonts:** stb_truetype.h (single-header, in src/)
- **Images:** stb_image.h (single-header, in src/)
- **Text Editor:** ImGuiColorTextEdit (TextEditor.h/cpp in src/)
- **Build System:** CMake 3.20+ with vcpkg toolchain
- **OS:** Windows x64 only

## Folder Structure
```
C:\Wavey\
├── CMakeLists.txt              # Main build file
├── .gitignore
├── PROJECT_CONTEXT.md          # This file
├── frontend/
│   ├── fonts/                  # TTF fonts (Roboto, OpenSans, JetBrainsMono, default.ttf)
│   └── ui/icons/               # PNG icons for Explorer tree and toolbar
├── src/
│   ├── main.cpp                # Entry point, editor UI, game loop, character controller (~2800 lines)
│   ├── stb_image.h             # Image loading (single-header lib)
│   ├── stb_truetype.h          # Font rasterization (single-header lib)
│   ├── TextEditor.h/cpp        # ImGuiColorTextEdit (syntax-highlighting code editor)
│   ├── ImGuizmo.h/cpp          # 3D transform gizmos
│   ├── core/
│   │   ├── Types.h             # Vector3, Color3, CFrame, UDim, UDim2 structs
│   │   ├── Signal.h            # RBXScriptSignal template
│   │   ├── Instance.h          # Instance class declaration (DataModel node)
│   │   └── Instance.cpp        # Instance implementation + class registry
│   ├── engine/
│   │   ├── Camera.h            # Orbital camera (yaw/pitch/distance/target)
│   │   ├── Renderer.h/cpp      # OpenGL 3D + 2D UI renderer (FBO, shaders, font atlas)
│   │   ├── Physics.h/cpp       # Legacy AABB physics (still used for character controller)
│   │   ├── JoltPhysics.h/cpp   # Jolt Physics wrapper (rigid bodies, raycasting)
│   │   └── GameServices.h/cpp  # Luau enum registration (KeyCode, EasingStyle, etc.)
│   ├── editor/
│   │   ├── Editor.h/cpp        # Selection, drag-drop, raycasting, delete
│   │   ├── ScriptEditor.h/cpp  # Tabbed script editor with TextEditor instances
│   │   ├── AutoComplete.h/cpp  # Per-tab autocomplete with DataModel snapshot
│   │   ├── IconManager.h/cpp   # PNG icon loading for Explorer tree
│   │   └── UndoStack.h/cpp     # 30-slot undo/redo for property changes
│   └── scripting/
│       ├── LuauBridge.h/cpp    # Luau ↔ C++ bindings (Instance API, properties, signals)
│       └── Scheduler.h/cpp     # Coroutine-based task scheduler (task.wait)
└── build/                      # CMake build output (gitignored)
    ├── Release/
    │   ├── WaveyStudio.exe
    │   ├── SDL2.dll
    │   └── glew32.dll
    └── _deps/                  # FetchContent downloads (luau, imgui, imguizmo, jolt)
```

## Build Instructions
```powershell
cd C:\Wavey
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
.\build\Release\WaveyStudio.exe
```
First build takes 5-10 minutes (Jolt is large). Subsequent builds are fast.

## Architecture Overview

### DataModel (Instance Tree)
- Every object is an `Instance` with: `Name`, `ClassName`, `Parent`, `Children`, `UID`, `Properties`
- Properties are stored in a `std::variant` supporting: bool, int, double, string, Vector3, Color3, CFrame, UDim, UDim2, Instance*
- Classes are registered via `Instance::RegisterClass("Part", [](Instance* p) { p->SetProperty(...); })`
- Registered classes: Part, Folder, Model, Script, LocalScript, ModuleScript, Camera, Humanoid, Player, ScreenGui, Frame, TextLabel, TextButton, ImageLabel, ObjectValue, StringValue, IntValue, BoolValue, NumberValue, PointLight, Sound

### Rendering Pipeline
1. 3D scene renders to an FBO (offscreen framebuffer)
2. 2D UI (ScreenGui elements) renders on top of the FBO
3. FBO texture is displayed in the ImGui Viewport panel via `ImGui::Image()`
4. In standalone mode, FBO blits directly to screen

### Physics (Dual System)
- **Jolt Physics** (`JoltPhysics.h/cpp`): Handles all Part rigid bodies, gravity (-196.2 studs/s²), collisions, raycasting. Parts are registered as Jolt bodies on Play.
- **Legacy AABB** (`Physics.h/cpp`): Still used for character controller ground detection. Will be fully replaced by Jolt.
- Character HRP uses a tall BoxShape (2 wide × 4 tall × 1 deep) with locked rotation (translation only).
- Cosmetic limbs (Head, Torso, Arms, Legs) are excluded from Jolt; their positions are computed relative to HRP each frame.

### Scripting
- Luau VM runs in coroutines via `Scheduler.h/cpp`
- `task.wait(seconds)` yields the coroutine and resumes after the delay
- Scripts access the Instance tree via globals: `game`, `workspace`, `script`
- `Instance.new("ClassName")`, `part.Position`, `part:FindFirstChild()`, etc.
- Signals: `RunService.Heartbeat:Connect(fn)`, `MouseButton1Click`, `MouseEnter`, `MouseLeave`
- Autocomplete snapshots the DataModel tree every 1 second for dot-completion

### Editor Features
- Explorer tree with drag-drop reparenting
- Properties panel with multi-select, search bar, type-specific editors (drag floats, color pickers, combos, object pickers)
- 3D gizmos via ImGuizmo (Move/Rotate/Scale) with snap settings
- Undo/redo (30 slots) for property changes, inserts, deletes, reparents
- Script editor tabs with syntax highlighting, bracket auto-pairing, autocomplete
- Toolbar with Play/Stop, tool selection, Part spawner
- Save/Load `.wavey` place files (text format with UIDs for object references)
- Build to standalone `.exe` (copies exe + DLLs + fonts + game.wavey)

### Play Mode
- F5 starts, Shift+F5 stops
- On Play: creates Dev Player + 6-part character (HRP, Head, Torso, Arms, Legs, Humanoid)
- Camera switches to follow mode
- Jolt registers all workspace Parts as rigid bodies
- On Stop: destroys character, restores all Part positions/orientations, clears Jolt bodies

## Key Patterns and Conventions

### Property Access
```cpp
// Reading (with default fallback)
Vector3 pos = GetProp<Vector3>(inst, "Position", Vector3(0, 0, 0));

// Writing
inst->SetProperty("Position", Vector3(1, 2, 3));
```

### Luau Binding Pattern
All Luau C functions are standalone `static int lua_Foo(lua_State* L)` functions (NOT lambdas inside macros — lambdas in `lua_pushcfunction` macros cause MSVC preprocessor crashes).

### Jolt Body Creation
```cpp
BodyCreationSettings settings(shape, position, rotation, motionType, layer);
settings.mAllowDynamicOrKinematic = true; // CRITICAL: allows Static→Dynamic transitions
settings.mRestitution = 0.1f;
settings.mFriction = 0.6f;
```

### Gizmo Matrix Math
- `BuildMatrix(pos, orient, size, outMatrix)` builds a column-major 4x4 with XYZ Euler rotation order
- ImGuizmo's `DecomposeMatrixToComponents` also uses XYZ order
- Scale snap values must be divided by part size: `snap / partSize` for 1-stud absolute steps
- Multi-select gizmo uses local-space matrix caching: `partLocal = Inverse(gizmoStart) * partWorld`

## Known Issues (As of Current Build)
1. **Scale gizmo**: The ImGuizmo on-screen overlay always shows relative scale (starts at 1.00), not absolute studs. This is hardcoded in ImGuizmo and cannot be changed. The actual Part Size property updates correctly.
2. **Scale snapping**: When snap is enabled, scale increments may jump by `partSize` instead of 1 stud. The fix is to divide snap values by part size, but this needs testing.
3. **Character legs**: May clip slightly into the ground depending on HipHeight. The HRP collision box extends downward but Jolt's resting position can drift.
4. **Ragdoll physics**: Dead character limbs fall with Jolt but don't tumble realistically (no angular impulse on death yet).
5. **OBB collision**: Jolt uses AABB for collision detection. Rotated parts collide as if axis-aligned.
6. **Jolt warnings**: MSVC 19.x generates C4865 warnings from Windows SDK headers when compiling Jolt. These are harmless but noisy.

## Important Warnings for AI Assistants
1. **Never use C++ lambdas inside `lua_pushcfunction()` or `lua_pushcclosure()` macros.** MSVC's preprocessor chokes on commas inside lambda bodies within macro arguments. Always define Luau C functions as standalone `static int` functions.
2. **Never use `static` keyword on global variables in `main.cpp` that need to be accessed from other `.cpp` files.** Use `extern` declarations in headers instead.
3. **Jolt's `BodyCreationSettings` must have `mAllowDynamicOrKinematic = true`** if you plan to switch bodies between Static and Dynamic at runtime.
4. **`luaL_getmetatable` does not exist in Luau.** Use `lua_getfield(L, LUA_REGISTRYINDEX, "metatableName")` instead.
5. **`luaL_ref` and `luaL_unref` do not exist in Luau.** Use `lua_ref(L, -1)` and `lua_unref(L, ref)` instead.
6. **`luaL_testudata` does not exist in Luau.** Use a custom metatable comparison function.
7. **The project uses `#define NOMINMAX` before `<windows.h>`** to prevent conflicts with `std::min`/`std::max`.
8. **ImGuizmo scale snap values are relative**, not absolute. Divide by part size for world-space snapping.
9. **`std::filesystem` requires C++17** and `<filesystem>` include. Use `namespace fs = std::filesystem;`.
10. **Jolt's `IgnoreSingleBodyFilter` already exists** in `<Jolt/Physics/Body/BodyFilter.h>`. Do not redefine it.
```

---

Save this as `C:\Wavey\PROJECT_CONTEXT.md` and give it to Cline. It covers everything the AI needs to understand the codebase, build it, and make correct edits without breaking things.