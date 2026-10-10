#pragma once
#include "lua.h"
#include "../core/Instance.h"
#include "../core/Types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include <iostream>

// Forward declare SDL_Event to avoid pulling SDL into every file
union SDL_Event;

// ═══════════════════════════════════════════════════════
// UserInputService
// ═══════════════════════════════════════════════════════
class UserInputService {
public:
    void ProcessEvent(const SDL_Event& event);
    void FireCallbacks(lua_State* L);
    bool IsKeyDown(int keyCode) const;
    float MouseX = 0, MouseY = 0;

    void UserInputService::AddBeganCallback(int ref) { 
        beganRefs.push_back(ref); 
        //std::cout << "[UIS C++] Registered Began Callback. Total: " << beganRefs.size() << std::endl;
    }
    void UserInputService::AddEndedCallback(int ref) { 
        endedRefs.push_back(ref); 
        //std::cout << "[UIS C++] Registered Ended Callback. Total: " << endedRefs.size() << std::endl;
    }
    void ClearCallbacks();

    // Queued events to fire next frame
    struct InputEvent {
        int keyCode;
        bool isBegin; // true = began, false = ended
        std::string inputType; // "Keyboard" or "MouseButton"
    };

private:
    std::unordered_map<int, bool> keyStates;
    std::vector<int> beganRefs;
    std::vector<int> endedRefs;
    std::vector<InputEvent> pendingEvents;
};

// ═══════════════════════════════════════════════════════
// TweenService
// ═══════════════════════════════════════════════════════
struct TweenGoal {
    std::string property;
    Instance::PropertyValue startVal;
    Instance::PropertyValue endVal;
};

struct ActiveTween {
    Instance* target = nullptr;
    float duration = 1.0f;
    float elapsed = 0.0f;
    float delay = 0.0f;
    float delayElapsed = 0.0f;
    int easingStyle = 0;   // 0=Linear,1=Sine,2=Quad,3=Cubic
    int easingDir = 0;     // 0=Out,1=In,2=InOut
    int repeatCount = 0;
    int repeatsLeft = 0;
    bool reverses = false;
    bool playing = false;
    bool reversed = false;
    std::vector<TweenGoal> goals;
};

class TweenService {
public:
    int Create(Instance* target, float duration, int easingStyle, int easingDir,
               int repeatCount, bool reverses, float delay,
               std::vector<TweenGoal> goals);
    void Play(int id);
    void Pause(int id);
    void Cancel(int id);
    void Update(float dt);
    void Clear();

private:
    std::unordered_map<int, ActiveTween> tweens;
    int nextId = 1;

    float Ease(int style, int dir, float t);
    Instance::PropertyValue Lerp(const Instance::PropertyValue& a,
                                  const Instance::PropertyValue& b, float t);
};

// ═══════════════════════════════════════════════════════
// Globals & Registration
// ═══════════════════════════════════════════════════════
extern UserInputService g_UIS;
extern TweenService g_TweenService;

// Call once during LuauBridge::Initialize to register all service APIs
void RegisterGameServices(lua_State* L);