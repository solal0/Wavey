#include "Scheduler.h"
#include "Luau/Compiler.h"
#include <iostream>

Scheduler g_Scheduler;

void Scheduler::Init(lua_State* L) {
    mainState = L;
}

void Scheduler::Clear() {
    for (auto& t : suspendedThreads) {
        lua_unref(mainState, t.threadRef);
    }
    suspendedThreads.clear();
}

void Scheduler::DelayThread(lua_State* L, float seconds) {
    // 1. Get current time
    extern float g_LastFrameTime;
    float wake = g_LastFrameTime + seconds;

    // 2. Reference the thread so it doesn't get GC'd
    lua_pushthread(L);
    int ref = lua_ref(L, -1);
    lua_pop(L, 1);

    suspendedThreads.push_back({L, wake, ref});
}

void Scheduler::Update(float currentTime) {
    for (auto it = suspendedThreads.begin(); it != suspendedThreads.end();) {
        if (currentTime >= it->wakeTime) {
            lua_State* co = it->co;
            int ref = it->threadRef;

            // Remove from list BEFORE resuming in case it yields again immediately
            it = suspendedThreads.erase(it);

            // Resume the coroutine thread
            lua_State* dummy;
            int status = lua_resume(co, nullptr, 0);

            if (status != 0 && status != LUA_YIELD) {
                // Runtime compilation or execution error
                const char* err = lua_tostring(co, -1);
                std::cerr << "[Luau Runtime Error] " << (err ? err : "unknown") << std::endl;
                
                // Print to Wavey Output
                extern std::vector<std::string> g_OutputLog;
                g_OutputLog.push_back(std::string("ERROR: ") + (err ? err : "unknown"));
            }

            // Release GC reference
            lua_unref(mainState, ref);
        } else {
            ++it;
        }
    }
}

void Scheduler::SpawnScript(const std::string& name, const std::string& source, Instance* scriptInst) {
    if (!mainState) return;

    lua_State* co = lua_newthread(mainState);
    int threadRef = lua_ref(mainState, -1);
    lua_pop(mainState, 1);

    // Set `script` global in this coroutine thread to point at the script Instance
    if (scriptInst) {
        Instance** ud = (Instance**)lua_newuserdata(co, sizeof(Instance*));
        *ud = scriptInst;
        lua_getfield(co, LUA_REGISTRYINDEX, "WaveyInstance"); // Safely gets the metatable directly from registry
        lua_setmetatable(co, -2);
        lua_setglobal(co, "script");
    }

    std::string bytecode = Luau::compile(source);

    if (luau_load(co, ("=" + name).c_str(), bytecode.data(), bytecode.size(), 0) == 0) {
        int status = lua_resume(co, nullptr, 0);
        if (status == LUA_YIELD) {
        } else if (status != 0) {
            const char* err = lua_tostring(co, -1);
            extern std::vector<std::string> g_OutputLog;
            g_OutputLog.push_back("ERROR in " + name + ": " + (err ? err : "unknown"));
        }
    } else {
        const char* err = lua_tostring(co, -1);
        extern std::vector<std::string> g_OutputLog;
        g_OutputLog.push_back("ERROR Compiling " + name + ": " + (err ? err : "unknown"));
    }
}