#include "GameServices.h"
#include <SDL.h>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <cstring>

UserInputService g_UIS;
TweenService g_TweenService;

// ═══════════════════════════════════════════════════════
// UserInputService Implementation
// ═══════════════════════════════════════════════════════

// Map SDL scancodes to Roblox-like KeyCode integers
static int SDLToKeyCode(SDL_Scancode sc) {
    // We use the SDL scancode value directly as the KeyCode enum value
    // This keeps it simple and consistent
    return (int)sc;
}

void UserInputService::ProcessEvent(const SDL_Event& event) {
    if (event.type == SDL_KEYDOWN && !event.key.repeat) {
        int kc = SDLToKeyCode(event.key.keysym.scancode);
        keyStates[kc] = true;
        pendingEvents.push_back({kc, true, "Keyboard"});
    }
    else if (event.type == SDL_KEYUP) {
        int kc = SDLToKeyCode(event.key.keysym.scancode);
        keyStates[kc] = false;
        pendingEvents.push_back({kc, false, "Keyboard"});
    }
    else if (event.type == SDL_MOUSEBUTTONDOWN) {
        int kc = 256 + event.button.button;
        keyStates[kc] = true;
        pendingEvents.push_back({kc, true, "MouseButton"});
    }
    else if (event.type == SDL_MOUSEBUTTONUP) {
        int kc = 256 + event.button.button;
        keyStates[kc] = false;
        pendingEvents.push_back({kc, false, "MouseButton"});
    }
    else if (event.type == SDL_MOUSEMOTION) {
        MouseX = (float)event.motion.x;
        MouseY = (float)event.motion.y;
    }
}

void UserInputService::FireCallbacks(lua_State* L) {
    if (!L) return;
    
    if (!pendingEvents.empty()) {
        //std::cout << "[UIS C++] Processing " << pendingEvents.size() << " events. beganRefs=" << beganRefs.size() << ", endedRefs=" << endedRefs.size() << std::endl;
    }

    for (auto& ev : pendingEvents) {
        auto& refs = ev.isBegin ? beganRefs : endedRefs;
        for (int ref : refs) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);

            // Create input object table
            lua_newtable(L);
            lua_pushinteger(L, ev.keyCode);
            lua_setfield(L, -2, "KeyCode");
            lua_pushstring(L, ev.inputType.c_str());
            lua_setfield(L, -2, "UserInputType");

            // gameProcessedEvent = false
            lua_pushboolean(L, false);

            //std::cout << "[UIS C++] Calling Luau callback for key " << ev.keyCode << std::endl;

            if (lua_pcall(L, 2, 0, 0) != 0) {
                const char* err = lua_tostring(L, -1);
                std::cerr << "[UIS C++ Error] " << (err ? err : "unknown") << std::endl;
                
                // Also write to output panel
                extern std::vector<std::string> g_OutputLog;
                g_OutputLog.push_back(std::string("ERROR: ") + (err ? err : "unknown"));
                lua_pop(L, 1);
            }
        }
    }
    pendingEvents.clear();
}

bool UserInputService::IsKeyDown(int keyCode) const {
    auto it = keyStates.find(keyCode);
    return it != keyStates.end() && it->second;
}

void UserInputService::ClearCallbacks() {
    beganRefs.clear();
    endedRefs.clear();
    pendingEvents.clear();
    keyStates.clear();
}

// ═══════════════════════════════════════════════════════
// TweenService Implementation
// ═══════════════════════════════════════════════════════

float TweenService::Ease(int style, int dir, float t) {
    // Clamp
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;

    float result = t; // Default linear

    switch (style) {
        case 0: // Linear
            result = t;
            break;
        case 1: // Sine
            result = 1.0f - cosf(t * 3.14159265f / 2.0f);
            break;
        case 2: // Quad
            result = t * t;
            break;
        case 3: // Cubic
            result = t * t * t;
            break;
    }

    // Apply direction
    if (dir == 1) { // In
        // result is already "In" for Quad/Cubic, need to invert for Sine
        if (style == 1) result = 1.0f - cosf(t * 3.14159265f / 2.0f);
    }
    else if (dir == 0) { // Out
        float inv = 1.0f - t;
        switch (style) {
            case 1: result = sinf(t * 3.14159265f / 2.0f); break;
            case 2: result = 1.0f - inv * inv; break;
            case 3: result = 1.0f - inv * inv * inv; break;
        }
    }
    else if (dir == 2) { // InOut
        if (t < 0.5f) {
            float t2 = t * 2.0f;
            switch (style) {
                case 2: result = t2 * t2 / 2.0f; break;
                case 3: result = t2 * t2 * t2 / 2.0f; break;
                default: result = t; break;
            }
        } else {
            float t2 = (1.0f - t) * 2.0f;
            switch (style) {
                case 2: result = 1.0f - t2 * t2 / 2.0f; break;
                case 3: result = 1.0f - t2 * t2 * t2 / 2.0f; break;
                default: result = t; break;
            }
        }
    }

    return result;
}

Instance::PropertyValue TweenService::Lerp(const Instance::PropertyValue& a,
                                            const Instance::PropertyValue& b, float t) {
    if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
        double va = std::get<double>(a);
        double vb = std::get<double>(b);
        return va + (vb - va) * t;
    }
    if (std::holds_alternative<int>(a) && std::holds_alternative<int>(b)) {
        double va = (double)std::get<int>(a);
        double vb = (double)std::get<int>(b);
        return (int)(va + (vb - va) * t);
    }
    if (std::holds_alternative<Vector3>(a) && std::holds_alternative<Vector3>(b)) {
        Vector3 va = std::get<Vector3>(a);
        Vector3 vb = std::get<Vector3>(b);
        return Vector3(
            va.X + (vb.X - va.X) * t,
            va.Y + (vb.Y - va.Y) * t,
            va.Z + (vb.Z - va.Z) * t
        );
    }
    if (std::holds_alternative<Color3>(a) && std::holds_alternative<Color3>(b)) {
        Color3 ca = std::get<Color3>(a);
        Color3 cb = std::get<Color3>(b);
        return Color3(
            ca.R + (cb.R - ca.R) * t,
            ca.G + (cb.G - ca.G) * t,
            ca.B + (cb.B - ca.B) * t
        );
    }
    // Can't lerp this type, just snap to end
    return b;
}

int TweenService::Create(Instance* target, float duration, int easingStyle, int easingDir, int repeatCount, bool reverses, float delay, std::vector<TweenGoal> goals) {
    int id = nextId++;
    ActiveTween tw;
    tw.target = target;
    tw.duration = duration;
    tw.easingStyle = easingStyle;
    tw.easingDir = easingDir;
    tw.repeatCount = repeatCount;
    tw.repeatsLeft = repeatCount;
    tw.reverses = reverses;
    tw.delay = delay;
    tw.goals = goals;
    tw.playing = false;
    tweens[id] = tw;
    return id;
}

void TweenService::Play(int id) {
    auto it = tweens.find(id);
    if (it != tweens.end()) {
        it->second.playing = true;
        it->second.elapsed = 0;
        it->second.delayElapsed = 0;
        it->second.reversed = false;
        it->second.repeatsLeft = it->second.repeatCount;
    }
}

void TweenService::Pause(int id) {
    auto it = tweens.find(id);
    if (it != tweens.end()) it->second.playing = false;
}

void TweenService::Cancel(int id) {
    auto it = tweens.find(id);
    if (it != tweens.end()) {
        it->second.playing = false;
        it->second.elapsed = 0;
    }
}

void TweenService::Update(float dt) {
    for (auto& [id, tw] : tweens) {
        if (!tw.playing || !tw.target || tw.target->IsDestroyed()) continue;

        // Handle delay
        if (tw.delayElapsed < tw.delay) {
            tw.delayElapsed += dt;
            continue;
        }

        tw.elapsed += dt;
        float t = tw.elapsed / tw.duration;
        if (t > 1.0f) t = 1.0f;

        float eased = Ease(tw.easingStyle, tw.easingDir, t);
        if (tw.reversed) eased = 1.0f - eased;

        // Apply interpolated values
        for (auto& goal : tw.goals) {
            auto lerped = Lerp(goal.startVal, goal.endVal, eased);
            tw.target->SetProperty(goal.property, lerped);
        }

        // Check completion
        if (t >= 1.0f) {
            if (tw.reverses && !tw.reversed) {
                tw.reversed = true;
                tw.elapsed = 0;
            } else if (tw.repeatsLeft > 0) {
                tw.repeatsLeft--;
                tw.elapsed = 0;
                tw.reversed = false;
            } else {
                tw.playing = false;
            }
        }
    }
}

void TweenService::Clear() {
    tweens.clear();
}

// ═══════════════════════════════════════════════════════
// Luau Bindings
// ═══════════════════════════════════════════════════════

// Helper to get Instance* from userdata (duplicated from LuauBridge to avoid circular deps)
static Instance* svc_checkInstance(lua_State* L, int idx) {
    Instance** ud = (Instance**)lua_touserdata(L, idx);
    if (!ud || !*ud) return nullptr;
    return *ud;
}

void RegisterGameServices(lua_State* L) {

    // ═══════════════════════════════════════════════════════
    // enum.KeyCode (maps SDL scancodes to names)
    // ═══════════════════════════════════════════════════════
    lua_newtable(L);

    // Letters
    const char* letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int i = 0; i < 26; i++) {
        char name[2] = {letters[i], 0};
        lua_pushinteger(L, SDL_SCANCODE_A + i);
        lua_setfield(L, -2, name);
    }

    // Numbers
    for (int i = 0; i < 10; i++) {
        char name[2] = {'0' + (char)i, 0};
        lua_pushinteger(L, SDL_SCANCODE_1 + ((i + 9) % 10));
        lua_setfield(L, -2, name);
    }

    // Special keys
    lua_pushinteger(L, SDL_SCANCODE_SPACE);    lua_setfield(L, -2, "Space");
    lua_pushinteger(L, SDL_SCANCODE_RETURN);   lua_setfield(L, -2, "Return");
    lua_pushinteger(L, SDL_SCANCODE_ESCAPE);   lua_setfield(L, -2, "Escape");
    lua_pushinteger(L, SDL_SCANCODE_BACKSPACE);lua_setfield(L, -2, "Backspace");
    lua_pushinteger(L, SDL_SCANCODE_TAB);      lua_setfield(L, -2, "Tab");
    lua_pushinteger(L, SDL_SCANCODE_LSHIFT);   lua_setfield(L, -2, "LeftShift");
    lua_pushinteger(L, SDL_SCANCODE_RSHIFT);   lua_setfield(L, -2, "RightShift");
    lua_pushinteger(L, SDL_SCANCODE_LCTRL);    lua_setfield(L, -2, "LeftControl");
    lua_pushinteger(L, SDL_SCANCODE_RCTRL);    lua_setfield(L, -2, "RightControl");
    lua_pushinteger(L, SDL_SCANCODE_LALT);     lua_setfield(L, -2, "LeftAlt");
    lua_pushinteger(L, SDL_SCANCODE_UP);       lua_setfield(L, -2, "Up");
    lua_pushinteger(L, SDL_SCANCODE_DOWN);     lua_setfield(L, -2, "Down");
    lua_pushinteger(L, SDL_SCANCODE_LEFT);     lua_setfield(L, -2, "Left");
    lua_pushinteger(L, SDL_SCANCODE_RIGHT);    lua_setfield(L, -2, "Right");
    lua_pushinteger(L, 257);                   lua_setfield(L, -2, "Button1"); // Left mouse
    lua_pushinteger(L, 259);                   lua_setfield(L, -2, "Button2"); // Right mouse
    lua_pushinteger(L, 258);                   lua_setfield(L, -2, "Button3"); // Middle mouse

    lua_setglobal(L, "KeyCode");

    // ═══════════════════════════════════════════════════════
    // enum.EasingStyle
    // ═══════════════════════════════════════════════════════
    lua_newtable(L);
    lua_pushinteger(L, 0); lua_setfield(L, -2, "Linear");
    lua_pushinteger(L, 1); lua_setfield(L, -2, "Sine");
    lua_pushinteger(L, 2); lua_setfield(L, -2, "Quad");
    lua_pushinteger(L, 3); lua_setfield(L, -2, "Cubic");
    lua_setglobal(L, "EasingStyle");

    // ═══════════════════════════════════════════════════════
    // enum.EasingDirection
    // ═══════════════════════════════════════════════════════
    lua_newtable(L);
    lua_pushinteger(L, 1); lua_setfield(L, -2, "In");
    lua_pushinteger(L, 0); lua_setfield(L, -2, "Out");
    lua_pushinteger(L, 2); lua_setfield(L, -2, "InOut");
    lua_setglobal(L, "EasingDirection");

    // ── Enum.X.Y nested access (Roblox-canonical form) ──
    // Build Enum table that contains sub-tables for each enum type
    lua_newtable(L); // Enum

    // Enum.KeyCode (copy from global KeyCode)
    lua_getglobal(L, "KeyCode");
    lua_setfield(L, -2, "KeyCode");

    // Enum.EasingStyle
    lua_getglobal(L, "EasingStyle");
    lua_setfield(L, -2, "EasingStyle");

    // Enum.EasingDirection
    lua_getglobal(L, "EasingDirection");
    lua_setfield(L, -2, "EasingDirection");

    // Enum.Font
    lua_newtable(L);
    lua_pushstring(L, "Roboto"); lua_setfield(L, -2, "Roboto");
    lua_pushstring(L, "RobotoBold"); lua_setfield(L, -2, "RobotoBold");
    lua_pushstring(L, "OpenSans"); lua_setfield(L, -2, "OpenSans");
    lua_pushstring(L, "JetBrainsMono"); lua_setfield(L, -2, "JetBrainsMono");
    lua_setfield(L, -2, "Font");

    // Enum.TextXAlignment
    lua_newtable(L);
    lua_pushstring(L, "Left"); lua_setfield(L, -2, "Left");
    lua_pushstring(L, "Center"); lua_setfield(L, -2, "Center");
    lua_pushstring(L, "Right"); lua_setfield(L, -2, "Right");
    lua_setfield(L, -2, "TextXAlignment");

    // Enum.TextYAlignment
    lua_newtable(L);
    lua_pushstring(L, "Top"); lua_setfield(L, -2, "Top");
    lua_pushstring(L, "Center"); lua_setfield(L, -2, "Center");
    lua_pushstring(L, "Bottom"); lua_setfield(L, -2, "Bottom");
    lua_setfield(L, -2, "TextYAlignment");

    // CoreGuiType flat global
    lua_newtable(L);
    lua_pushinteger(L, 0); lua_setfield(L, -2, "Console");
    lua_pushinteger(L, 1); lua_setfield(L, -2, "Debug");
    lua_setglobal(L, "CoreGuiType");

    // Enum.CoreGuiType (Phase 10d)
    lua_newtable(L);
    lua_pushinteger(L, 0); lua_setfield(L, -2, "Console");
    lua_pushinteger(L, 1); lua_setfield(L, -2, "Debug");
    lua_setfield(L, -2, "CoreGuiType");

    // Enum.CameraType (Phase 13a)
    lua_newtable(L);
    lua_pushinteger(L, 0); lua_setfield(L, -2, "Fixed");
    lua_pushinteger(L, 1); lua_setfield(L, -2, "Custom");
    lua_pushinteger(L, 2); lua_setfield(L, -2, "Scriptable");
    lua_pushinteger(L, 3); lua_setfield(L, -2, "Classic");
    lua_setfield(L, -2, "CameraType");

    lua_newtable(L);
    lua_pushinteger(L, 0); lua_setfield(L, -2, "Fixed");
    lua_pushinteger(L, 1); lua_setfield(L, -2, "Custom");
    lua_pushinteger(L, 2); lua_setfield(L, -2, "Scriptable");
    lua_pushinteger(L, 3); lua_setfield(L, -2, "Classic");
    lua_setglobal(L, "CameraType");

    lua_setglobal(L, "Enum");
}