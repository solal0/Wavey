#include "LuauBridge.h"
#include "Scheduler.h"
#include "../engine/GameServices.h"
#include "../engine/JoltPhysics.h"
#include <unordered_map>
#include <iostream>
#include <cstring>
#include <string>
#include <vector>

// ── Globals ──
lua_State* g_LuauState = nullptr;
static std::vector<int> g_HeartbeatRefs;

// ── UI Signal Storage (Phase 11c) ──
// Maps Instance* + event name → list of Lua callback refs
static std::unordered_map<Instance*, std::unordered_map<std::string, std::vector<int>>> g_UISignals;

// ── Forward Declarations for Custom Types ──
static bool isUserdataOfType(lua_State* L, int idx, const char* metaName);
static Vector3* checkVector3(lua_State* L, int idx);
static void pushVector3(lua_State* L, const Vector3& v);
static Color3* checkColor3(lua_State* L, int idx);
static void pushColor3(lua_State* L, const Color3& c);
static CFrame* checkCFrame(lua_State* L, int idx);
static void pushCFrame(lua_State* L, const CFrame& cf);
static UDim* checkUDim(lua_State* L, int idx);
static void pushUDim(lua_State* L, const UDim& u);
static UDim2* checkUDim2(lua_State* L, int idx);
static void pushUDim2(lua_State* L, const UDim2& u2);

// ── Forward Declarations for Tweens ──
static int lua_TweenPlay(lua_State* L);
static int lua_TweenPause(lua_State* L);
static int lua_TweenCancel(lua_State* L);
static int lua_TweenServiceCreate(lua_State* L);

// ═══════════════════════════════════════════════════════
// Helper: get the Instance* from a Luau userdata
// ═══════════════════════════════════════════════════════
static Instance* checkInstance(lua_State* L, int idx) {
    Instance** ud = (Instance**)lua_touserdata(L, idx);
    if (!ud || !*ud) {
        luaL_error(L, "Invalid Instance (destroyed or nil)");
        return nullptr;
    }
    return *ud;
}

static void pushInstance(lua_State* L, Instance* inst) {
    if (!inst) {
        lua_pushnil(L);
        return;
    }
    Instance** ud = (Instance**)lua_newuserdata(L, sizeof(Instance*));
    *ud = inst;
    luaL_getmetatable(L, "WaveyInstance");
    lua_setmetatable(L, -2);
}

// ═══════════════════════════════════════════════════════
// Standalone Method Implementations (No macros, no preprocessor bugs)
// ═══════════════════════════════════════════════════════
static int lua_InstanceDestroy(lua_State* L) {
    checkInstance(L, 1)->Destroy();
    return 0;
}

static int lua_InstanceClone(lua_State* L) {
    pushInstance(L, checkInstance(L, 1)->Clone());
    return 1;
}

static int lua_InstanceFindFirstChild(lua_State* L) {
    Instance* inst = checkInstance(L, 1);
    const char* name = luaL_checkstring(L, 2);
    pushInstance(L, inst->FindFirstChild(name));
    return 1;
}

static int lua_InstanceGetChildren(lua_State* L) {
    auto children = checkInstance(L, 1)->GetChildren();
    lua_createtable(L, (int)children.size(), 0);
    for (int i = 0; i < (int)children.size(); i++) {
        pushInstance(L, children[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int lua_InstanceGetDescendants(lua_State* L) {
    auto desc = checkInstance(L, 1)->GetDescendants();
    lua_createtable(L, (int)desc.size(), 0);
    for (int i = 0; i < (int)desc.size(); i++) {
        pushInstance(L, desc[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int lua_InstanceIsA(lua_State* L) {
    lua_pushboolean(L, checkInstance(L, 1)->IsA(luaL_checkstring(L, 2)));
    return 1;
}

static int lua_InstanceWaitForChild(lua_State* L) {
    pushInstance(L, checkInstance(L, 1)->FindFirstChild(luaL_checkstring(L, 2)));
    return 1;
}

static int lua_InstanceGetService(lua_State* L) {
    Instance* inst = checkInstance(L, 1);
    const char* serviceName = luaL_checkstring(L, 2);
    Instance* service = inst->FindFirstChild(serviceName);
    if (!service) {
        service = new Instance(serviceName);
        service->Name = serviceName;
        service->SetParent(inst);
    }
    pushInstance(L, service);
    return 1;
}

static int lua_HumanoidTakeDamage(lua_State* L) {
    Instance* h = checkInstance(L, 1);
    double amount = luaL_checknumber(L, 2);
    if (h && h->HasProperty("Health")) {
        auto hv = h->GetProperty("Health");
        double cur = std::holds_alternative<double>(hv) ? std::get<double>(hv) : 100.0;
        cur -= amount;
        if (cur < 0) cur = 0;
        h->SetProperty("Health", cur);
    }
    return 0;
}

static int lua_HumanoidMove(lua_State* L) {
    Instance* h = checkInstance(L, 1);
    // Second arg: Vector3 direction (userdata)
    if (lua_isuserdata(L, 2)) {
        Vector3* v = (Vector3*)lua_touserdata(L, 2);
        if (v) h->SetProperty("MoveDirection", *v);
    }
    return 0;
}

static int lua_HumanoidJump(lua_State* L) {
    Instance* h = checkInstance(L, 1);
    h->SetProperty("Jumping", true);
    // Actual jump impulse happens in UpdateCharacterController; this just sets the flag.
    // For now, trigger by setting Jumping=true and letting the controller consume it.
    extern bool g_CharGrounded;
    extern Vector3 g_CharVelocity;
    extern Instance* g_DevHumanoid;
    if (h == g_DevHumanoid && g_CharGrounded) {
        double jumpPower = 50.0;
        if (h->HasProperty("JumpPower")) {
            auto jv = h->GetProperty("JumpPower");
            if (std::holds_alternative<double>(jv)) jumpPower = std::get<double>(jv);
            else if (std::holds_alternative<int>(jv)) jumpPower = (double)std::get<int>(jv);
        }
        g_CharVelocity.Y = (float)jumpPower;
        g_CharGrounded = false;
    }
    return 0;
}

static int lua_StarterGuiSetCoreGuiEnabled(lua_State* L) {
    // arg 1 is the StarterGui instance, arg 2 is CoreGuiType, arg 3 is enabled bool
    int guiType = (int)luaL_checkinteger(L, 2);
    bool enabled = lua_toboolean(L, 3) != 0;

    extern bool g_ConsoleEnabled;
    extern bool g_DebugEnabled;

    if (guiType == 0) g_ConsoleEnabled = enabled;      // Console
    else if (guiType == 1) g_DebugEnabled = enabled;    // Debug

    return 0;
}

static int lua_RunServiceHeartbeatConnect(lua_State* L) {
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    int ref = lua_ref(L, -1);
    lua_pop(L, 1);
    g_HeartbeatRefs.push_back(ref);
    lua_newtable(L);
    return 1;
}

static int lua_RunServiceIsRunning(lua_State* L) {
    extern bool g_IsRunning;
    lua_pushboolean(L, g_IsRunning);
    return 1;
}

static int lua_UISInputBeganConnect(lua_State* L) {
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    int ref = lua_ref(L, -1);
    lua_pop(L, 1);
    g_UIS.AddBeganCallback(ref);
    lua_newtable(L);
    return 1;
}

static int lua_UISInputEndedConnect(lua_State* L) {
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    int ref = lua_ref(L, -1);
    lua_pop(L, 1);
    g_UIS.AddEndedCallback(ref);
    lua_newtable(L);
    return 1;
}

static int lua_UISIsKeyDown(lua_State* L) {
    int kc = (int)luaL_checkinteger(L, 2);
    lua_pushboolean(L, g_UIS.IsKeyDown(kc));
    return 1;
}

static int lua_UISGetMouseLocation(lua_State* L) {
    lua_createtable(L, 0, 2);
    lua_pushnumber(L, g_UIS.MouseX); lua_setfield(L, -2, "X");
    lua_pushnumber(L, g_UIS.MouseY); lua_setfield(L, -2, "Y");
    return 1;
}

static int lua_InstanceNew(lua_State* L) {
    const char* className = luaL_checkstring(L, 1);
    Instance* inst = Instance::Create(className);
    pushInstance(L, inst);
    return 1;
}

// ── InstanceMetatable __index ──
static int lua_InstanceIndex(lua_State* L) {
    Instance* inst = checkInstance(L, 1);
    const char* key = luaL_checkstring(L, 2);
    std::string k(key);

    // Methods
    if (k == "Destroy") { lua_pushcclosure(L, lua_InstanceDestroy, "Destroy", 0); return 1; }
    if (k == "Clone") { lua_pushcclosure(L, lua_InstanceClone, "Clone", 0); return 1; }
    if (k == "FindFirstChild") { lua_pushcclosure(L, lua_InstanceFindFirstChild, "FindFirstChild", 0); return 1; }
    if (k == "GetChildren") { lua_pushcclosure(L, lua_InstanceGetChildren, "GetChildren", 0); return 1; }
    if (k == "GetDescendants") { lua_pushcclosure(L, lua_InstanceGetDescendants, "GetDescendants", 0); return 1; }
    if (k == "IsA") { lua_pushcclosure(L, lua_InstanceIsA, "IsA", 0); return 1; }
    if (k == "WaitForChild") { lua_pushcclosure(L, lua_InstanceWaitForChild, "WaitForChild", 0); return 1; }
    if (k == "GetService") { lua_pushcclosure(L, lua_InstanceGetService, "GetService", 0); return 1; }
    if (inst->ClassName == "StarterGui" && k == "SetCoreGuiEnabled") { lua_pushcclosure(L, lua_StarterGuiSetCoreGuiEnabled, "SetCoreGuiEnabled", 0); return 1; }
    // Humanoid methods (Phase 13a)
    if (inst->ClassName == "Humanoid") {
        if (k == "TakeDamage") { lua_pushcclosure(L, lua_HumanoidTakeDamage, "TakeDamage", 0); return 1; }
        if (k == "Move") { lua_pushcclosure(L, lua_HumanoidMove, "Move", 0); return 1; }
        if (k == "Jump") { lua_pushcclosure(L, lua_HumanoidJump, "Jump", 0); return 1; }
    }

    // RunService
    if (inst->ClassName == "RunService") {
        if (k == "Heartbeat") {
            lua_newtable(L);
            lua_pushcclosure(L, lua_RunServiceHeartbeatConnect, "Connect", 0);
            lua_setfield(L, -2, "Connect");
            return 1;
        }
        if (k == "IsRunning") { lua_pushcclosure(L, lua_RunServiceIsRunning, "IsRunning", 0); return 1; }
    }

    // UserInputService
    if (inst->ClassName == "UserInputService") {
        if (k == "InputBegan") {
            lua_newtable(L);
            lua_pushcclosure(L, lua_UISInputBeganConnect, "Connect", 0);
            lua_setfield(L, -2, "Connect");
            return 1;
        }
        if (k == "InputEnded") {
            lua_newtable(L);
            lua_pushcclosure(L, lua_UISInputEndedConnect, "Connect", 0);
            lua_setfield(L, -2, "Connect");
            return 1;
        }
        if (k == "IsKeyDown") { lua_pushcclosure(L, lua_UISIsKeyDown, "IsKeyDown", 0); return 1; }
        if (k == "GetMouseLocation") { lua_pushcclosure(L, lua_UISGetMouseLocation, "GetMouseLocation", 0); return 1; }
    }

    // TweenService
    if (inst->ClassName == "TweenService" && k == "Create") {
        lua_pushcclosure(L, lua_TweenServiceCreate, "Create", 0);
        return 1;
    }

    // Core Properties
    if (k == "Name") { lua_pushstring(L, inst->Name.c_str()); return 1; }
    if (k == "ClassName") { lua_pushstring(L, inst->ClassName.c_str()); return 1; }
    if (k == "Parent") { pushInstance(L, inst->GetParent()); return 1; }

    // ── Virtual Properties: Jolt Velocity (Phase 13b) ──
    if (k == "AssemblyLinearVelocity") {
        pushVector3(L, g_JoltPhysics.GetLinearVelocity(inst));
        return 1;
    }
    if (k == "AssemblyAngularVelocity") {
        pushVector3(L, g_JoltPhysics.GetAngularVelocity(inst));
        return 1;
    }

    // DataModel Properties
    if (inst->HasProperty(k)) {
        auto val = inst->GetProperty(k);
        if (std::holds_alternative<bool>(val))
            lua_pushboolean(L, std::get<bool>(val));
        else if (std::holds_alternative<int>(val))
            lua_pushinteger(L, std::get<int>(val));
        else if (std::holds_alternative<double>(val))
            lua_pushnumber(L, std::get<double>(val));
        else if (std::holds_alternative<std::string>(val))
            lua_pushstring(L, std::get<std::string>(val).c_str());
        else if (std::holds_alternative<Vector3>(val))
            pushVector3(L, std::get<Vector3>(val));
        else if (std::holds_alternative<Color3>(val))
            pushColor3(L, std::get<Color3>(val));
        else if (std::holds_alternative<CFrame>(val))
            pushCFrame(L, std::get<CFrame>(val));
        else if (std::holds_alternative<UDim>(val))
            pushUDim(L, std::get<UDim>(val));
        else if (std::holds_alternative<UDim2>(val))
            pushUDim2(L, std::get<UDim2>(val));
        else if (std::holds_alternative<Instance*>(val))
            pushInstance(L, std::get<Instance*>(val));
        else
            lua_pushnil(L);
        return 1;
    }

    // ── UI Signals (Phase 11c) ──
    if (k == "MouseButton1Click" || k == "MouseButton1Down" || k == "MouseButton1Up" || k == "MouseEnter" || k == "MouseLeave") {
        lua_newtable(L);
        // Capture instance pointer and event name for the Connect closure
        lua_pushlightuserdata(L, inst);
        lua_pushstring(L, k.c_str());
        lua_pushcclosure(L, [](lua_State* L) -> int {
            Instance* signalInst = (Instance*)lua_touserdata(L, lua_upvalueindex(1));
            const char* eventName = lua_tostring(L, lua_upvalueindex(2));
            luaL_checktype(L, 2, LUA_TFUNCTION);
            lua_pushvalue(L, 2);
            int ref = lua_ref(L, -1);
            lua_pop(L, 1);
            LuauBridge::AddUISignal(signalInst, eventName, ref);
            lua_newtable(L); // Return connection stub
            return 1;
        }, "Connect", 2);
        lua_setfield(L, -2, "Connect");
        return 1;
    }

    // ── Child Lookup (like Roblox: workspace.Camera finds child named "Camera") ──
    Instance* child = inst->FindFirstChild(k);
    if (child) {
        pushInstance(L, child);
        return 1;
    }

    lua_pushnil(L);
    return 1;
}

// ═══════════════════════════════════════════════════════
// __newindex: set properties
// ═══════════════════════════════════════════════════════
static int lua_InstanceNewIndex(lua_State* L) {
    Instance* inst = checkInstance(L, 1);
    const char* key = luaL_checkstring(L, 2);
    std::string k(key);

    if (k == "Name") { inst->Name = luaL_checkstring(L, 3); return 0; }
    if (k == "Parent") {
        Instance* np = lua_isnil(L, 3) ? nullptr : checkInstance(L, 3);
        inst->SetParent(np);
        return 0;
    }
    if (k == "ClassName") { luaL_error(L, "ClassName is read-only"); return 0; }

    int type = lua_type(L, 3);
    if (type == LUA_TBOOLEAN)
        inst->SetProperty(k, (bool)lua_toboolean(L, 3));
    else if (type == LUA_TNUMBER) {
        double val = lua_tonumber(L, 3);
        if (val == (int)val) inst->SetProperty(k, (int)val);
        else inst->SetProperty(k, val);
    }
    else if (type == LUA_TSTRING)
        inst->SetProperty(k, std::string(lua_tostring(L, 3)));
    else if (k == "AssemblyLinearVelocity" && type == LUA_TUSERDATA) {
        if (isUserdataOfType(L, 3, "WaveyVector3")) {
            extern JoltPhysics g_JoltPhysics;
            g_JoltPhysics.SetLinearVelocity(inst, *checkVector3(L, 3));
        }
        return 0;
    }
    else if (k == "AssemblyAngularVelocity" && type == LUA_TUSERDATA) {
        if (isUserdataOfType(L, 3, "WaveyVector3")) {
            extern JoltPhysics g_JoltPhysics;
            g_JoltPhysics.SetAngularVelocity(inst, *checkVector3(L, 3));
        }
        return 0;
    }
    else if (type == LUA_TUSERDATA) {
        if (isUserdataOfType(L, 3, "WaveyVector3"))
            inst->SetProperty(k, *checkVector3(L, 3));
        else if (isUserdataOfType(L, 3, "WaveyColor3"))
            inst->SetProperty(k, *checkColor3(L, 3));
        else if (isUserdataOfType(L, 3, "WaveyCFrame"))
            inst->SetProperty(k, *checkCFrame(L, 3));
        else if (isUserdataOfType(L, 3, "WaveyUDim"))
            inst->SetProperty(k, *checkUDim(L, 3));
        else if (isUserdataOfType(L, 3, "WaveyUDim2"))
            inst->SetProperty(k, *checkUDim2(L, 3));
        else
            inst->SetProperty(k, checkInstance(L, 3));
    }
    else if (type == LUA_TTABLE) {
        lua_getfield(L, 3, "X");
        lua_getfield(L, 3, "Y");
        lua_getfield(L, 3, "Z");
        if (lua_isnumber(L, -3) && lua_isnumber(L, -2) && lua_isnumber(L, -1)) {
            inst->SetProperty(k, Vector3((float)lua_tonumber(L,-3),(float)lua_tonumber(L,-2),(float)lua_tonumber(L,-1)));
        } else {
            lua_pop(L, 3);
            lua_getfield(L, 3, "R");
            lua_getfield(L, 3, "G");
            lua_getfield(L, 3, "B");
            if (lua_isnumber(L, -3) && lua_isnumber(L, -2) && lua_isnumber(L, -1))
                inst->SetProperty(k, Color3((float)lua_tonumber(L,-3),(float)lua_tonumber(L,-2),(float)lua_tonumber(L,-1)));
        }
        lua_pop(L, 3);
    }
    return 0;
}

static int lua_InstanceToString(lua_State* L) {
    lua_pushstring(L, checkInstance(L, 1)->Name.c_str());
    return 1;
}

// ═══════════════════════════════════════════════════════
// Vector3 & Color3 Userdata
// ═══════════════════════════════════════════════════════
static bool isUserdataOfType(lua_State* L, int idx, const char* metaName) {
    if (lua_type(L, idx) != LUA_TUSERDATA) return false;
    if (!lua_getmetatable(L, idx)) return false;
    luaL_getmetatable(L, metaName);
    bool match = lua_rawequal(L, -1, -2) != 0;
    lua_pop(L, 2);
    return match;
}

static Vector3* checkVector3(lua_State* L, int idx) {
    return (Vector3*)luaL_checkudata(L, idx, "WaveyVector3");
}

static void pushVector3(lua_State* L, const Vector3& v) {
    Vector3* ud = (Vector3*)lua_newuserdata(L, sizeof(Vector3));
    *ud = v;
    luaL_getmetatable(L, "WaveyVector3");
    lua_setmetatable(L, -2);
}

static int lua_Vector3Add(lua_State* L) {
    Vector3* a = checkVector3(L, 1);
    Vector3* b = checkVector3(L, 2);
    pushVector3(L, {a->X+b->X, a->Y+b->Y, a->Z+b->Z});
    return 1;
}

static int lua_Vector3Sub(lua_State* L) {
    Vector3* a = checkVector3(L, 1);
    Vector3* b = checkVector3(L, 2);
    pushVector3(L, {a->X-b->X, a->Y-b->Y, a->Z-b->Z});
    return 1;
}

static int lua_Vector3Mul(lua_State* L) {
    if (lua_isnumber(L, 2)) {
        Vector3* a = checkVector3(L, 1);
        float s = (float)lua_tonumber(L, 2);
        pushVector3(L, {a->X*s, a->Y*s, a->Z*s});
    } else if (lua_isnumber(L, 1)) {
        float s = (float)lua_tonumber(L, 1);
        Vector3* b = checkVector3(L, 2);
        pushVector3(L, {b->X*s, b->Y*s, b->Z*s});
    } else {
        Vector3* a = checkVector3(L, 1);
        Vector3* b = checkVector3(L, 2);
        pushVector3(L, {a->X*b->X, a->Y*b->Y, a->Z*b->Z});
    }
    return 1;
}

static int lua_Vector3Index(lua_State* L) {
    Vector3* v = checkVector3(L, 1);
    const char* k = luaL_checkstring(L, 2);
    if (strcmp(k,"X")==0) lua_pushnumber(L, v->X);
    else if (strcmp(k,"Y")==0) lua_pushnumber(L, v->Y);
    else if (strcmp(k,"Z")==0) lua_pushnumber(L, v->Z);
    else if (strcmp(k,"Magnitude")==0) lua_pushnumber(L, v->Magnitude());
    else lua_pushnil(L);
    return 1;
}

static int lua_Vector3ToString(lua_State* L) {
    lua_pushstring(L, checkVector3(L, 1)->ToString().c_str());
    return 1;
}

static int lua_Vector3New(lua_State* L) {
    pushVector3(L, {(float)luaL_optnumber(L,1,0),(float)luaL_optnumber(L,2,0),(float)luaL_optnumber(L,3,0)});
    return 1;
}

// ── CFrame ──
static CFrame* checkCFrame(lua_State* L, int idx) {
    return (CFrame*)luaL_checkudata(L, idx, "WaveyCFrame");
}

static void pushCFrame(lua_State* L, const CFrame& cf) {
    CFrame* ud = (CFrame*)lua_newuserdata(L, sizeof(CFrame));
    *ud = cf;
    luaL_getmetatable(L, "WaveyCFrame");
    lua_setmetatable(L, -2);
}

static int lua_CFrameIndex(lua_State* L) {
    CFrame* cf = checkCFrame(L, 1);
    const char* k = luaL_checkstring(L, 2);
    if (strcmp(k, "X") == 0) lua_pushnumber(L, cf->Position.X);
    else if (strcmp(k, "Y") == 0) lua_pushnumber(L, cf->Position.Y);
    else if (strcmp(k, "Z") == 0) lua_pushnumber(L, cf->Position.Z);
    else if (strcmp(k, "p") == 0 || strcmp(k, "Position") == 0) {
        pushVector3(L, cf->Position);
    } else lua_pushnil(L);
    return 1;
}

static int lua_CFrameToString(lua_State* L) {
    CFrame* cf = checkCFrame(L, 1);
    lua_pushstring(L, cf->ToString().c_str());
    return 1;
}

static int lua_CFrameNew(lua_State* L) {
    int n = lua_gettop(L);
    if (n == 0) {
        pushCFrame(L, CFrame(0, 0, 0));
    } else if (n == 1 && isUserdataOfType(L, 1, "WaveyVector3")) {
        Vector3* v = checkVector3(L, 1);
        pushCFrame(L, CFrame(*v));
    } else if (n >= 3) {
        float x = (float)luaL_checknumber(L, 1);
        float y = (float)luaL_checknumber(L, 2);
        float z = (float)luaL_checknumber(L, 3);
        pushCFrame(L, CFrame(x, y, z));
    } else {
        pushCFrame(L, CFrame(0, 0, 0));
    }
    return 1;
}

// ── UDim ──
static UDim* checkUDim(lua_State* L, int idx) {
    return (UDim*)luaL_checkudata(L, idx, "WaveyUDim");
}

static void pushUDim(lua_State* L, const UDim& u) {
    UDim* ud = (UDim*)lua_newuserdata(L, sizeof(UDim));
    *ud = u;
    luaL_getmetatable(L, "WaveyUDim");
    lua_setmetatable(L, -2);
}

static int lua_UDimIndex(lua_State* L) {
    UDim* u = checkUDim(L, 1);
    const char* k = luaL_checkstring(L, 2);
    if (strcmp(k, "Scale") == 0) lua_pushnumber(L, u->Scale);
    else if (strcmp(k, "Offset") == 0) lua_pushinteger(L, u->Offset);
    else lua_pushnil(L);
    return 1;
}

static int lua_UDimToString(lua_State* L) {
    lua_pushstring(L, checkUDim(L, 1)->ToString().c_str());
    return 1;
}

static int lua_UDimNew(lua_State* L) {
    float s = (float)luaL_optnumber(L, 1, 0.0f);
    int o = (int)luaL_optinteger(L, 2, 0);
    pushUDim(L, UDim(s, o));
    return 1;
}

// ── UDim2 ──
static UDim2* checkUDim2(lua_State* L, int idx) {
    return (UDim2*)luaL_checkudata(L, idx, "WaveyUDim2");
}

static void pushUDim2(lua_State* L, const UDim2& u2) {
    UDim2* ud = (UDim2*)lua_newuserdata(L, sizeof(UDim2));
    *ud = u2;
    luaL_getmetatable(L, "WaveyUDim2");
    lua_setmetatable(L, -2);
}

static int lua_UDim2Index(lua_State* L) {
    UDim2* u2 = checkUDim2(L, 1);
    const char* k = luaL_checkstring(L, 2);
    if (strcmp(k, "X") == 0) pushUDim(L, u2->X);
    else if (strcmp(k, "Y") == 0) pushUDim(L, u2->Y);
    else lua_pushnil(L);
    return 1;
}

static int lua_UDim2ToString(lua_State* L) {
    lua_pushstring(L, checkUDim2(L, 1)->ToString().c_str());
    return 1;
}

static int lua_UDim2New(lua_State* L) {
    int n = lua_gettop(L);
    if (n == 4) {
        float xs = (float)luaL_checknumber(L, 1);
        int xo = (int)luaL_checkinteger(L, 2);
        float ys = (float)luaL_checknumber(L, 3);
        int yo = (int)luaL_checkinteger(L, 4);
        pushUDim2(L, UDim2(xs, xo, ys, yo));
    } else if (n == 2 && isUserdataOfType(L, 1, "WaveyUDim") && isUserdataOfType(L, 2, "WaveyUDim")) {
        UDim* x = checkUDim(L, 1);
        UDim* y = checkUDim(L, 2);
        pushUDim2(L, UDim2(*x, *y));
    } else {
        pushUDim2(L, UDim2(0, 0, 0, 0));
    }
    return 1;
}

static Color3* checkColor3(lua_State* L, int idx) {
    return (Color3*)luaL_checkudata(L, idx, "WaveyColor3");
}

static void pushColor3(lua_State* L, const Color3& c) {
    Color3* ud = (Color3*)lua_newuserdata(L, sizeof(Color3));
    *ud = c;
    luaL_getmetatable(L, "WaveyColor3");
    lua_setmetatable(L, -2);
}

static int lua_Color3Index(lua_State* L) {
    Color3* c = checkColor3(L, 1);
    const char* k = luaL_checkstring(L, 2);
    if (strcmp(k,"R")==0) lua_pushnumber(L, c->R);
    else if (strcmp(k,"G")==0) lua_pushnumber(L, c->G);
    else if (strcmp(k,"B")==0) lua_pushnumber(L, c->B);
    else lua_pushnil(L);
    return 1;
}

static int lua_Color3ToString(lua_State* L) {
    lua_pushstring(L, checkColor3(L, 1)->ToString().c_str());
    return 1;
}

static int lua_Color3New(lua_State* L) {
    pushColor3(L, {(float)luaL_optnumber(L,1,0),(float)luaL_optnumber(L,2,0),(float)luaL_optnumber(L,3,0)});
    return 1;
}

static int lua_Color3FromRGB(lua_State* L) {
    pushColor3(L, {(float)luaL_checknumber(L,1)/255.f,(float)luaL_checknumber(L,2)/255.f,(float)luaL_checknumber(L,3)/255.f});
    return 1;
}

// ── Tween Methods ──
static int lua_TweenPlay(lua_State* L) {
    lua_getfield(L, 1, "_id");
    int tid = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    g_TweenService.Play(tid);
    return 0;
}

static int lua_TweenPause(lua_State* L) {
    lua_getfield(L, 1, "_id");
    int tid = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    g_TweenService.Pause(tid);
    return 0;
}

static int lua_TweenCancel(lua_State* L) {
    lua_getfield(L, 1, "_id");
    int tid = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    g_TweenService.Cancel(tid);
    return 0;
}

static int lua_TweenServiceCreate(lua_State* L) {
    Instance* target = checkInstance(L, 2);
    if (!target) {
        luaL_error(L, "Invalid target instance");
        return 0;
    }

    float duration = 1.0f;
    int easingStyle = 0;
    int easingDir = 0;
    int repeatCount = 0;
    bool reverses = false;
    float delay = 0.0f;

    if (lua_istable(L, 3)) {
        lua_getfield(L, 3, "Time");
        if (lua_isnumber(L, -1)) duration = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 3, "EasingStyle");
        if (lua_isnumber(L, -1)) easingStyle = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 3, "EasingDirection");
        if (lua_isnumber(L, -1)) easingDir = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 3, "RepeatCount");
        if (lua_isnumber(L, -1)) repeatCount = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 3, "Reverses");
        if (lua_isboolean(L, -1)) reverses = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);

        lua_getfield(L, 3, "DelayTime");
        if (lua_isnumber(L, -1)) delay = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }

    std::vector<TweenGoal> goals;
    if (lua_istable(L, 4) && target) {
        lua_pushnil(L);
        while (lua_next(L, 4) != 0) {
            const char* propName = lua_tostring(L, -2);
            if (propName && target->HasProperty(propName)) {
                TweenGoal goal;
                goal.property = propName;
                goal.startVal = target->GetProperty(propName);

                if (lua_isnumber(L, -1)) {
                    goal.endVal = (double)lua_tonumber(L, -1);
                } else if (lua_istable(L, -1)) {
                    lua_getfield(L, -1, "X");
                    lua_getfield(L, -2, "Y");
                    lua_getfield(L, -3, "Z");
                    if (lua_isnumber(L, -3) && lua_isnumber(L, -2) && lua_isnumber(L, -1)) {
                        if (std::holds_alternative<Vector3>(goal.startVal)) {
                            goal.endVal = Vector3(
                                (float)lua_tonumber(L, -3),
                                (float)lua_tonumber(L, -2),
                                (float)lua_tonumber(L, -1)
                            );
                        } else {
                            goal.endVal = Color3(
                                (float)lua_tonumber(L, -3),
                                (float)lua_tonumber(L, -2),
                                (float)lua_tonumber(L, -1)
                            );
                        }
                    }
                    lua_pop(L, 3);
                } else if (lua_isuserdata(L, -1)) {
                    if (isUserdataOfType(L, -1, "WaveyVector3")) {
                        goal.endVal = *(Vector3*)lua_touserdata(L, -1);
                    } else if (isUserdataOfType(L, -1, "WaveyColor3")) {
                        goal.endVal = *(Color3*)lua_touserdata(L, -1);
                    }
                }
                goals.push_back(goal);
            }
            lua_pop(L, 1);
        }
    }

    int id = g_TweenService.Create(target, duration, easingStyle, easingDir,
                                    repeatCount, reverses, delay, goals);

    lua_newtable(L);

    lua_pushinteger(L, id);
    lua_setfield(L, -2, "_id");

    lua_pushcclosure(L, lua_TweenPlay, "Play", 0);
    lua_setfield(L, -2, "Play");

    lua_pushcclosure(L, lua_TweenPause, "Pause", 0);
    lua_setfield(L, -2, "Pause");

    lua_pushcclosure(L, lua_TweenCancel, "Cancel", 0);
    lua_setfield(L, -2, "Cancel");

    return 1;
}

// ═══════════════════════════════════════════════════════
// RegisterClasses (dynamic)
// ═══════════════════════════════════════════════════════
void LuauBridge::RegisterClasses() {}

// ═══════════════════════════════════════════════════════
// Initialize: wire everything into the Luau VM
// ═══════════════════════════════════════════════════════
void LuauBridge::Initialize(lua_State* L, Instance* gameRoot) {
    g_LuauState = L;

    // Instance metatable
    luaL_newmetatable(L, "WaveyInstance");
    lua_pushcclosure(L, lua_InstanceIndex, "__index", 0);
    lua_setfield(L, -2, "__index");
    lua_pushcclosure(L, lua_InstanceNewIndex, "__newindex", 0);
    lua_setfield(L, -2, "__newindex");
    lua_pushcclosure(L, lua_InstanceToString, "__tostring", 0);
    lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);

    // Global: Instance.new
    lua_newtable(L);
    lua_pushcclosure(L, lua_InstanceNew, "InstanceNew", 0);
    lua_setfield(L, -2, "new");
    lua_setglobal(L, "Instance");

    // Global: Vector3.new
    lua_newtable(L);
    lua_pushcclosure(L, lua_Vector3New, "Vector3New", 0);
    lua_setfield(L, -2, "new");
    lua_setglobal(L, "Vector3");

    // Global: Color3
    lua_newtable(L);
    lua_pushcclosure(L, lua_Color3New, "Color3New", 0);
    lua_setfield(L, -2, "new");
    lua_pushcclosure(L, lua_Color3FromRGB, "Color3FromRGB", 0);
    lua_setfield(L, -2, "fromRGB");
    lua_setglobal(L, "Color3");

    // Global: game & workspace
    pushInstance(L, gameRoot);
    lua_setglobal(L, "game");

    pushInstance(L, gameRoot->FindFirstChild("Workspace"));
    lua_setglobal(L, "workspace");

    // Global: task.wait
    lua_newtable(L);
    lua_pushcclosure(L, [](lua_State* L) -> int {
        float seconds = (float)luaL_optnumber(L, 1, 0.03f);
        g_Scheduler.DelayThread(L, seconds);
        return lua_yield(L, 0);
    }, "wait", 0);
    lua_setfield(L, -2, "wait");
    lua_setglobal(L, "task");

    // WaveyVector3 metatable
    luaL_newmetatable(L, "WaveyVector3");
    lua_pushcclosure(L, lua_Vector3Index, "__index", 0); lua_setfield(L, -2, "__index");
    lua_pushcclosure(L, lua_Vector3Add, "__add", 0); lua_setfield(L, -2, "__add");
    lua_pushcclosure(L, lua_Vector3Sub, "__sub", 0); lua_setfield(L, -2, "__sub");
    lua_pushcclosure(L, lua_Vector3Mul, "__mul", 0); lua_setfield(L, -2, "__mul");
    lua_pushcclosure(L, lua_Vector3ToString, "__tostring", 0); lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);

    // WaveyColor3 metatable
    luaL_newmetatable(L, "WaveyColor3");
    lua_pushcclosure(L, lua_Color3Index, "__index", 0); lua_setfield(L, -2, "__index");
    lua_pushcclosure(L, lua_Color3ToString, "__tostring", 0); lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);

    // CFrame metatable
    luaL_newmetatable(L, "WaveyCFrame");
    lua_pushcclosure(L, lua_CFrameIndex, "__index", 0); lua_setfield(L, -2, "__index");
    lua_pushcclosure(L, lua_CFrameToString, "__tostring", 0); lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);

    // CFrame global
    lua_newtable(L);
    lua_pushcclosure(L, lua_CFrameNew, "CFrameNew", 0);
    lua_setfield(L, -2, "new");
    lua_setglobal(L, "CFrame");

    // UDim metatable
    luaL_newmetatable(L, "WaveyUDim");
    lua_pushcclosure(L, lua_UDimIndex, "__index", 0); lua_setfield(L, -2, "__index");
    lua_pushcclosure(L, lua_UDimToString, "__tostring", 0); lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);

    // UDim global
    lua_newtable(L);
    lua_pushcclosure(L, lua_UDimNew, "UDimNew", 0);
    lua_setfield(L, -2, "new");
    lua_setglobal(L, "UDim");

    // UDim2 metatable
    luaL_newmetatable(L, "WaveyUDim2");
    lua_pushcclosure(L, lua_UDim2Index, "__index", 0); lua_setfield(L, -2, "__index");
    lua_pushcclosure(L, lua_UDim2ToString, "__tostring", 0); lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);

    // UDim2 global
    lua_newtable(L);
    lua_pushcclosure(L, lua_UDim2New, "UDim2New", 0);
    lua_setfield(L, -2, "new");
    lua_setglobal(L, "UDim2");

    // Game services enums
    RegisterGameServices(L);
}

// ═══════════════════════════════════════════════════════
// Heartbeat: called every frame from the main loop
// ═══════════════════════════════════════════════════════
void LuauBridge::FireHeartbeat(float dt) {
    if (!g_LuauState) return;
    for (int ref : g_HeartbeatRefs) {
        lua_rawgeti(g_LuauState, LUA_REGISTRYINDEX, ref);
        lua_pushnumber(g_LuauState, dt);
        if (lua_pcall(g_LuauState, 1, 0, 0) != 0) {
            const char* err = lua_tostring(g_LuauState, -1);
            std::cerr << "[Wavey] Heartbeat error: " << (err ? err : "unknown") << std::endl;
            lua_pop(g_LuauState, 1);
        }
    }
}

// ═══════════════════════════════════════════════════════
// UI Signal System (Phase 11c)
// ═══════════════════════════════════════════════════════
void LuauBridge::AddUISignal(Instance* inst, const std::string& event, int luaRef) {
    g_UISignals[inst][event].push_back(luaRef);
}

void LuauBridge::FireUISignal(Instance* inst, const std::string& event) {
    if (!g_LuauState) return;
    auto itInst = g_UISignals.find(inst);
    if (itInst == g_UISignals.end()) return;
    auto itEvent = itInst->second.find(event);
    if (itEvent == itInst->second.end()) return;

    for (int ref : itEvent->second) {
        lua_rawgeti(g_LuauState, LUA_REGISTRYINDEX, ref);
        if (lua_pcall(g_LuauState, 0, 0, 0) != 0) {
            const char* err = lua_tostring(g_LuauState, -1);
            //extern std::vector<std::string> g_OutputLog;
            //g_OutputLog.push_back(std::string("ERROR [UI]: ") + (err ? err : "unknown"));
            lua_pop(g_LuauState, 1);
        }
    }
}

void LuauBridge::ClearUISignals() {
    if (!g_LuauState) return;
    for (auto& [inst, events] : g_UISignals) {
        for (auto& [event, refs] : events) {
            for (int ref : refs) {
                lua_unref(g_LuauState, ref);
            }
        }
    }
    g_UISignals.clear();
}

void LuauBridge::ClearCallbacks() {
    if (!g_LuauState) return;
    for (int ref : g_HeartbeatRefs) {
        lua_unref(g_LuauState, ref);
    }
    g_HeartbeatRefs.clear();
    g_UIS.ClearCallbacks();
    g_TweenService.Clear();
    ClearUISignals();
}