#pragma once
// ═══════════════════════════════════════════════════════
// Wavey Studio - Luau Bridge
// Connects the Luau VM to the Instance/DataModel system.
// This is what makes Instance.new("Part") work in scripts.
// ═══════════════════════════════════════════════════════

#include "lua.h"
#include "lualib.h"
#include "../core/Instance.h"

namespace LuauBridge {
    void Initialize(lua_State* L, Instance* gameRoot);
    void RegisterClasses();

    // Called every frame during Play mode
    void FireHeartbeat(float dt);
    void ClearCallbacks();

    // ── UI Signal System (Phase 11c) ──
    void AddUISignal(Instance* inst, const std::string& event, int luaRef);
    void FireUISignal(Instance* inst, const std::string& event);
    void ClearUISignals();
}