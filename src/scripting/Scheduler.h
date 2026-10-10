#pragma once
#include "lua.h"
#include <vector>
#include <string>

class Instance;

struct ThreadInfo {
    lua_State* co = nullptr;
    float wakeTime = 0.0f;
    int threadRef = -1;
};

class Scheduler {
public:
    void Init(lua_State* L);
    void Update(float currentTime);
    void SpawnScript(const std::string& name, const std::string& source, Instance* scriptInst);
    void DelayThread(lua_State* L, float seconds);
    void Clear();

private:
    lua_State* mainState = nullptr;
    std::vector<ThreadInfo> suspendedThreads;
};

extern Scheduler g_Scheduler;