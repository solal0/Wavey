#include <SDL.h>
#include <GL/glew.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>

#include "lua.h"
#include "lualib.h"
#include "Luau/Compiler.h"

#include "core/Instance.h"
#include "core/Types.h"
#include "scripting/LuauBridge.h"
#include "engine/Camera.h"
#include "engine/Renderer.h"
#include "editor/Editor.h"
#include "engine/Physics.h"
#include "editor/UndoStack.h"
#include "editor/IconManager.h"
#include "editor/ScriptEditor.h"
#include "engine/GameServices.h"
#include "scripting/Scheduler.h"
#include "editor/Autocomplete.h"
#include "engine/JoltPhysics.h"
#include "ImGuizmo.h"

#define NOMINMAX  // Prevents Windows.h from defining min and max macros
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h> // For SHBrowseForFolderA and SHGetPathFromIDListA
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

#include <unordered_map>
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <string>
#include <vector>
#include <cfloat>
#include <ctime>
#include <set>

// ── Global State ──
static bool standaloneMode = false;
static Instance* g_GameRoot = nullptr;
static lua_State* g_LuaState = nullptr;

// ── Forward Declarations of Helpers ──
static void DoSaveProper();
static void DoSaveAs();
static void DoLoad();
static void DoBuild();
static void DoAutosave(bool isCrash);
static void RenderUIHierarchy(Instance* inst, float parentX, float parentY, float parentW, float parentH);

// ═══════════════════════════════════════════════════════
// Globals
// ═══════════════════════════════════════════════════════
std::vector<std::string> g_OutputLog;
static bool showExplorer = true;
static bool showProperties = true;
static bool showOutput = true;
static bool showViewport = true;
static bool showTools = true;
static Camera g_Camera;
static Renderer g_Renderer;
static Editor g_Editor;
static Instance* g_SelectedInstance = nullptr;
static ImVec2 g_ViewportSize = ImVec2(800, 600);
static ImVec2 g_ViewportScreenPos = ImVec2(0, 0);
static bool g_ViewportHovered = false;

// ── File & Dirty State ──
static std::string g_CurrentFilePath = "";
static bool g_Dirty = false;
static bool g_FocusRename = false;
static Instance* g_InsertTarget = nullptr;         // Which instance to parent to
static char g_InsertSearch[128] = "";              // Search filter
static bool g_InsertFocusSearch = false;           // Focus search box on open
static ImVec2 g_InsertPopupAnchor = ImVec2(0, 0);
static std::set<Instance*> g_AutoExpandNodes;
static Instance* g_HoverExpandCandidate = nullptr;
static constexpr float HOVER_EXPAND_DELAY = 1.0f; // seconds
static float g_HoverExpandStartTime = 0.0f;
static SDL_Window* g_Window = nullptr;

// ── Play/Stop State ──
bool g_IsRunning = false;
static Physics g_Physics;
static std::unordered_map<Instance*, Vector3> g_SavedPositions;
static std::unordered_map<Instance*, Vector3> g_SavedOrientations; // Added Phase 13b

// ── Character Controller State ──
Instance* g_DevCharacter = nullptr;   // Dev's HumanoidRootPart
Instance* g_DevHumanoid = nullptr;    // Dev's Humanoid
Instance* g_DevCharacterModel = nullptr; // The Model containing everything
Vector3 g_CharVelocity = {0, 0, 0};
bool g_CharGrounded = false;
float g_CharYaw = 0.0f;
float g_WalkCycleTime = 0.0f;         // For procedural animation
bool g_CharacterDead = false;
float g_RespawnTimer = 0.0f;
constexpr float RESPAWN_DELAY = 5.0f; // Moved to top
Vector3 g_SpawnPoint = {0, 3, 0}; // Height accounts for HipHeight(2) + HRP half-height(1) + ground(0) + some drop buffer

// ── Camera Save/Restore ──
static Vector3 g_SavedCameraTarget = {0, 5, 0};
static float g_SavedCameraDistance = 30.0f;
static float g_SavedCameraYaw = -45.0f;
static float g_SavedCameraPitch = 30.0f;

static constexpr float SCRIPT_AUTOSAVE_INTERVAL = 30.0f;
static constexpr float PLACE_AUTOSAVE_INTERVAL = 600.0f; // 10 minutes
static constexpr float DM_SNAPSHOT_INTERVAL = 1.0f;
static float g_LastScriptAutoSave = 0.0f;
static float g_LastPlaceAutoSave = 0.0f;
static bool g_ShowClosePrompt = false;
static bool g_ConfirmedClose = false;
static float g_LastDMSnapshot = 0.0f;
float g_LastFrameTime = 0.0f;

// ── Snap settings for Gizmo (Phase 11d) ──
static bool g_SnapMoveEnabled = true;
static bool g_SnapRotateEnabled = true;
static float g_SnapMoveValue = 1.0f;    // Studs
static float g_SnapRotateValue = 15.0f; // Degrees

// ── Console & Debug HUD (Phase 10d) ──
static bool g_ShowDebugHUD = false;
static bool g_ShowConsole = false;
static int g_ConsoleMaxLines = 50;     // How many lines the in-game console shows
bool g_ConsoleEnabled = true;   // StarterGui can disable
bool g_DebugEnabled = true;     // StarterGui can disable
static float g_FPS = 0.0f;
static int g_FrameCount = 0;
static float g_FPSTimer = 0.0f;

// ── Clipboard (Phase 13b) ──
static std::vector<Instance*> g_Clipboard; // Stores cloned instances for paste

// ── Tool & Toolbar State ──
enum class EditorTool {
    Mouse,
    Move,
    Rotate,
    Scale
};
static EditorTool g_CurrentTool = EditorTool::Mouse;
static bool g_ShowPartDropdown = false;

static const char* g_PartShapes[] = { "Block", "Ball", "Cylinder", "Wedge" };
static const int g_PartShapesCount = 4;

// ── Object Picker State ──
static Instance* g_PickingHostInstance = nullptr;
static std::string g_PickingPropertyName = "";

// ── UI Hit Testing State (Phase 11c) ──
static Instance* g_HoveredUI = nullptr;
static Instance* g_PressedUI = nullptr;

// ── Column-Major 4x4 Matrix Mathematics (Phase 13b) ──
static void BuildMatrix(const Vector3& pos, const Vector3& orient, const Vector3& size, float* matrix) {
    float rx = orient.X * 3.14159265f / 180.0f;
    float ry = orient.Y * 3.14159265f / 180.0f;
    float rz = orient.Z * 3.14159265f / 180.0f;

    float cx = cosf(rx), sx = sinf(rx);
    float cy = cosf(ry), sy = sinf(ry);
    float cz = cosf(rz), sz = sinf(rz);

    float r00 = cy * cz, r01 = cy * sz, r02 = -sy;
    float r10 = sx * sy * cz - cx * sz, r11 = sx * sy * sz + cx * cz, r12 = sx * cy;
    float r20 = cx * sy * cz + sx * sz, r21 = cx * sy * sz - sx * cz, r22 = cx * cy;

    matrix[0] = r00 * size.X; matrix[4] = r10 * size.Y; matrix[8]  = r20 * size.Z; matrix[12] = pos.X;
    matrix[1] = r01 * size.X; matrix[5] = r11 * size.Y; matrix[9]  = r21 * size.Z; matrix[13] = pos.Y;
    matrix[2] = r02 * size.X; matrix[6] = r12 * size.Y; matrix[10] = r22 * size.Z; matrix[14] = pos.Z;
    matrix[3] = 0.0f;         matrix[7] = 0.0f;         matrix[11] = 0.0f;         matrix[15] = 1.0f;
}

static void MatrixMultiply(const float* A, const float* B, float* R) {
    float T[16];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            T[r + c * 4] = 0.0f;
            for (int k = 0; k < 4; ++k) {
                T[r + c * 4] += A[r + k * 4] * B[k + c * 4];
            }
        }
    }
    memcpy(R, T, sizeof(T));
}

static bool MatrixInverse(const float* m, float* out) {
    float inv[16];
    inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
    inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
    inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
    inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
    inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
    inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];

    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (fabsf(det) < 1e-10f) return false;
    det = 1.0f / det;
    for (int i = 0; i < 16; i++) out[i] = inv[i] * det;
    return true;
}

// ── Helpers ──
template<typename T>
static T GetProp(Instance* inst, const std::string& name, T def) {
    if (inst->HasProperty(name)) {
        auto v = inst->GetProperty(name);
        if (std::holds_alternative<T>(v)) return std::get<T>(v);
    }
    return def;
}

static void UpdateWindowTitle() {
    std::string title = "Wavey Studio";
    if (!standaloneMode) {
        if (g_IsRunning) {
            title += " Running";
        }
        if (!g_CurrentFilePath.empty()) {
            title += " - " + g_CurrentFilePath;
            if (g_Dirty) {
                title += " *";
            }
        }
    }
    else {
        title = "Wavey Player";
    }
    if (g_Window) SDL_SetWindowTitle(g_Window, title.c_str());
}

static void MarkDirty() {
    if (!g_Dirty) {
        g_Dirty = true;
        UpdateWindowTitle();
    }
}

// ── Windows File Dialogs ──
static std::string OpenFileDialog() {
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "Wavey Place (*.wavey)\0*.wavey\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = "wavey";
    if (GetOpenFileNameA(&ofn)) return std::string(filename);
    return "";
}

static std::string SaveFileDialog() {
    char filename[MAX_PATH] = "my_place.wavey";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "Wavey Place (*.wavey)\0*.wavey\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = "wavey";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameA(&ofn)) return std::string(filename);
    return "";
}

namespace fs = std::filesystem;

static std::string ChooseFolderDialog() {
    char path[MAX_PATH] = "";
    BROWSEINFOA bi = { 0 };
    bi.lpszTitle = "Select Build Output Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl != nullptr) {
        if (SHGetPathFromIDListA(pidl, path)) {
            CoTaskMemFree(pidl);
            return std::string(path);
        }
        CoTaskMemFree(pidl);
    }
    return "";
}

static void DoBuild() {
    std::string targetFolder = ChooseFolderDialog();
    if (targetFolder.empty()) return; // An empty return is correct for void

    try {
        fs::path targetDir(targetFolder);
        fs::create_directories(targetDir);

        char exePath[MAX_PATH];
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        fs::path sourceDir = fs::path(exePath).parent_path();

        fs::path outputExe = targetDir / "WaveyPlayer.exe";
        fs::path outputGame = targetDir / "game.wavey";

        // Save all open scripts before building
        g_ScriptEditor.SaveAll();

        g_Editor.SavePlace(outputGame.string(), g_GameRoot);

        if (fs::exists(outputExe)) {
            fs::remove(outputExe);
        }
        fs::copy_file(exePath, outputExe);

        for (const auto& entry : fs::directory_iterator(sourceDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".dll") {
                fs::path targetDll = targetDir / entry.path().filename();
                if (fs::exists(targetDll)) {
                    fs::remove(targetDll);
                }
                fs::copy_file(entry.path(), targetDll);
                g_OutputLog.push_back("[Build] Copied dependency: " + entry.path().filename().string());
            }
        }

        // 6. Copy wavey/fonts/ folder to the built game
        fs::path projectRoot = sourceDir.parent_path().parent_path();
        fs::path fontsSrc = projectRoot / "wavey" / "fonts";
        fs::path fontsDst = targetDir / "wavey" / "fonts";
        if (fs::exists(fontsSrc)) {
            fs::create_directories(fontsDst);
            for (const auto& f : fs::directory_iterator(fontsSrc)) {
                if (f.is_regular_file()) {
                    fs::path targetFile = fontsDst / f.path().filename();
                    if (fs::exists(targetFile)) fs::remove(targetFile);
                    fs::copy_file(f.path(), targetFile);
                }
            }
            g_OutputLog.push_back("[Build] Copied fonts folder");
        } else {
            g_OutputLog.push_back("[Build] WARNING: fonts folder not found at " + fontsSrc.string());
        }

        g_OutputLog.push_back("[Build] Successfully built at: " + targetFolder);
    }
    catch (const std::exception& e) {
        g_OutputLog.push_back(std::string("[Build] ERROR: ") + e.what());
    }
}

static void DoSave() {
    if (g_CurrentFilePath.empty()) {
        std::string path = SaveFileDialog();
        if (path.empty()) return;
        g_CurrentFilePath = path;
    }
    if (g_Editor.SavePlace(g_CurrentFilePath, g_Editor.Selected ? g_Editor.Selected : nullptr)) {
        // SavePlace needs the root, fix below
    }
}

static void RenderInsertPopup();

static std::string GetAutosaveFolder() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();
    fs::path autosaveDir = exeDir / "autosaves";
    fs::create_directories(autosaveDir);
    return autosaveDir.string();
}

static std::string MakeAutosaveFilename(bool isCrash = false) {
    time_t now = time(nullptr);
    tm localTime;
    localtime_s(&localTime, &now);

    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", &localTime);

    std::string prefix = isCrash ? "autosave-crash-" : "autosave-";
    return GetAutosaveFolder() + "\\" + prefix + timestamp + ".wavey";
}

static void DoAutosave(bool isCrash = false) {
    if (!g_GameRoot) return;

    // Save all scripts before autosaving the place
    g_ScriptEditor.SaveAll();

    std::string path = MakeAutosaveFilename(isCrash);
    if (g_Editor.SavePlace(path, g_GameRoot)) {
        g_OutputLog.push_back("[Autosave] Created at " + path);
    } else {
        g_OutputLog.push_back("[Autosave] ERROR: Failed to write " + path);
    }
}

static void DoSaveProper() {
    if (g_CurrentFilePath.empty()) {
        std::string path = SaveFileDialog();
        if (path.empty()) return;
        g_CurrentFilePath = path;
    }
    if (g_Editor.SavePlace(g_CurrentFilePath, g_GameRoot)) {
        g_Dirty = false;
        g_OutputLog.push_back("[Save] Saved to " + g_CurrentFilePath);
        UpdateWindowTitle();
    } else {
        g_OutputLog.push_back("[Save] ERROR: Failed to save");
    }
}

static void DoSaveAs() {
    std::string path = SaveFileDialog();
    if (path.empty()) return;
    g_CurrentFilePath = path;
    if (g_Editor.SavePlace(g_CurrentFilePath, g_GameRoot)) {
        g_Dirty = false;
        g_OutputLog.push_back("[Save] Saved to " + g_CurrentFilePath);
        UpdateWindowTitle();
    }
}

// Forward decl — character creation will be split out of DoPlay later; for now re-run inline
void RespawnCharacter() {
    Instance* playersSvc = g_GameRoot->FindFirstChild("Players");
    Instance* workspaceSvc = g_GameRoot->FindFirstChild("Workspace");
    if (!playersSvc || !workspaceSvc) return;

    // Clean up remnants
    if (auto* oldPlayer = playersSvc->FindFirstChild("Dev")) oldPlayer->Destroy();
    if (auto* oldChar = workspaceSvc->FindFirstChild("Dev")) oldChar->Destroy();

    Instance* devPlayer = Instance::Create("Player");
    devPlayer->Name = "Dev";
    devPlayer->SetParent(playersSvc);

    Instance* devChar = Instance::Create("Model");
    devChar->Name = "Dev";
    devChar->SetParent(workspaceSvc);

    Vector3 spawn = g_SpawnPoint;

    Color3 torsoColor(
        0.3f + (rand() % 70) / 100.0f,
        0.3f + (rand() % 70) / 100.0f,
        0.3f + (rand() % 70) / 100.0f
    );
    Color3 skinColor(0.95f, 0.87f, 0.65f);
    Color3 legColor(0.25f, 0.25f, 0.28f);

    auto makePart = [&](const std::string& name, Vector3 size, Vector3 relPos, Color3 color, double transparency, bool canCollide) {
        Instance* p = Instance::Create("Part");
        p->Name = name;
        p->SetProperty("Size", size);
        p->SetProperty("Position", Vector3(spawn.X + relPos.X, spawn.Y + relPos.Y, spawn.Z + relPos.Z));
        p->SetProperty("Color", color);
        p->SetProperty("Transparency", transparency);
        p->SetProperty("Anchored", name == "HumanoidRootPart" ? false : true);
        p->SetProperty("CanCollide", canCollide);
        p->SetProperty("Shape", std::string("Block"));
        p->SetParent(devChar);
        return p;
    };

    Instance* hrp     = makePart("HumanoidRootPart", Vector3(2, 2, 1), Vector3(0, 0, 0), Color3(0.5f, 0.5f, 1.0f), 1.0, true);
    makePart("Head",      Vector3(2, 1, 1), Vector3(0, 1.5f, 0), skinColor, 0.0, false);
    makePart("Torso",     Vector3(2, 2, 1), Vector3(0, 0, 0),    torsoColor, 0.0, false);
    makePart("Left Arm",  Vector3(1, 2, 1), Vector3(-1.5f, 0, 0), skinColor, 0.0, false);
    makePart("Right Arm", Vector3(1, 2, 1), Vector3(1.5f, 0, 0),  skinColor, 0.0, false);
    makePart("Left Leg",  Vector3(1, 2, 1), Vector3(-0.5f, -2.0f, 0), legColor, 0.0, false);
    makePart("Right Leg", Vector3(1, 2, 1), Vector3(0.5f, -2.0f, 0), legColor, 0.0, false);

    Instance* humanoid = Instance::Create("Humanoid");
    humanoid->Name = "Humanoid";
    humanoid->SetParent(devChar);

    devPlayer->SetProperty("Character", devChar);

    g_DevCharacter = hrp;
    g_DevHumanoid = humanoid;
    g_DevCharacterModel = devChar;
    g_CharVelocity = {0, 0, 0};
    g_CharYaw = g_Camera.Yaw;
    g_WalkCycleTime = 0.0f;
    g_CharacterDead = false;

    g_OutputLog.push_back("[Game] Dev respawned.");
}

static void UpdateCharacterController(float dt) {
    if (!g_DevCharacterModel || g_DevCharacterModel->IsDestroyed()) return;

    // Handle death
    if (g_CharacterDead) {
        g_RespawnTimer -= dt;
        if (g_RespawnTimer <= 0.0f) {
            if (g_DevCharacterModel && !g_DevCharacterModel->IsDestroyed()) {
                g_DevCharacterModel->Destroy();
            }
            g_DevCharacterModel = nullptr;
            g_DevCharacter = nullptr;
            g_DevHumanoid = nullptr;
            g_CharacterDead = false;
            RespawnCharacter();
        }
        return;
    }

    if (!g_DevCharacter || g_DevCharacter->IsDestroyed()) return;
    if (!g_DevHumanoid || g_DevHumanoid->IsDestroyed()) return;

    double walkSpeed = GetProp<double>(g_DevHumanoid, "WalkSpeed", 16.0);
    double jumpPower = GetProp<double>(g_DevHumanoid, "JumpPower", 50.0);
    double health = GetProp<double>(g_DevHumanoid, "Health", 100.0);
    bool platformStand = GetProp<bool>(g_DevHumanoid, "PlatformStand", false);

    if (health <= 0.0 && !g_CharacterDead) {
        g_CharacterDead = true;
        g_RespawnTimer = RESPAWN_DELAY;

        // Detach visible limbs and unregister HRP
        g_JoltPhysics.UnregisterPart(g_DevCharacter);
        for (auto* child : g_DevCharacterModel->GetChildren()) {
            if (!child->IsA("Part")) continue;
            if (child->Name == "HumanoidRootPart") {
                child->SetProperty("Anchored", true);
                child->SetProperty("CanCollide", false);
                continue;
            }
            child->SetProperty("Anchored", false);
            child->SetProperty("CanCollide", true);
            g_JoltPhysics.RegisterPart(child); // Let limbs fall with Jolt!
        }
        g_OutputLog.push_back("[Game] Dev died. Respawning in 5 seconds...");
        return;
    }

    // Read keyboard
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    float forward = 0.0f, right = 0.0f;
    if (!platformStand) {
        if (keys[SDL_SCANCODE_W]) forward += 1.0f;
        if (keys[SDL_SCANCODE_S]) forward -= 1.0f;
        if (keys[SDL_SCANCODE_D]) right += 1.0f;
        if (keys[SDL_SCANCODE_A]) right -= 1.0f;
    }

    float mag = sqrtf(forward * forward + right * right);
    if (mag > 0.0f) { forward /= mag; right /= mag; }

    // Camera-relative movement vectors (FIXED: negated forward direction)
    float yawRad = g_Camera.Yaw * 3.14159265f / 180.0f;
    float fwdX = -sinf(yawRad), fwdZ = -cosf(yawRad);
    float rgtX = cosf(yawRad), rgtZ = -sinf(yawRad);

    float moveX = fwdX * forward + rgtX * right;
    float moveZ = fwdZ * forward + rgtZ * right;

    // Read current Jolt velocity
    Vector3 velocity = g_JoltPhysics.GetLinearVelocity(g_DevCharacter);
    
    velocity.X = moveX * (float)walkSpeed;
    velocity.Z = moveZ * (float)walkSpeed;

    g_DevHumanoid->SetProperty("MoveDirection", Vector3(moveX, 0, moveZ));

    // Face the movement direction (not camera direction)
    if (mag > 0.01f) {
        float moveAngle = atan2f(moveX, moveZ) * 180.0f / 3.14159265f;
        g_CharYaw = moveAngle;
    }

    // Jolt-driven Ground Check
    Vector3 pos = GetProp<Vector3>(g_DevCharacter, "Position", Vector3(0, 5, 0));
    double hipHeight = GetProp<double>(g_DevHumanoid, "HipHeight", 2.0);
    
    // Standing height: center of HRP is at: groundY + 1 (HRP half-height) + HipHeight
    float standingHeight = 1.0f + (float)hipHeight;

    float raycastGroundY = 0.0f;
    Vector3 dummyNormal;
    
    // Cast ray from HRP center straight down, ignoring our own body!
    bool hitGround = g_JoltPhysics.Raycast(
        pos, 
        Vector3(0, -1, 0), 
        standingHeight + 0.15f, 
        raycastGroundY, 
        dummyNormal,
        g_DevCharacter // Ignored during raycast!
    );

    // Grounded only if the ground surface is directly under our feet
    g_CharGrounded = hitGround && (pos.Y - raycastGroundY) <= (standingHeight + 0.15f);

    // Read current linear velocity from Jolt (reusing existing variable)
    velocity = g_JoltPhysics.GetLinearVelocity(g_DevCharacter);

    // Set horizontal velocities (WASD)
    velocity.X = moveX * (float)walkSpeed;
    velocity.Z = moveZ * (float)walkSpeed;

    // ── Jump Trigger (Space) ──
    if (keys[SDL_SCANCODE_SPACE] && g_CharGrounded && !platformStand) {
        velocity.Y = (float)jumpPower; // Apply upward impulse
        g_CharGrounded = false;
        g_DevHumanoid->SetProperty("Jumping", true);
    } else {
        // PRESERVE Jolt's vertical velocity! This lets gravity run at full speed.
        g_DevHumanoid->SetProperty("Jumping", !g_CharGrounded);
    }

    // Pass velocities back to Jolt
    g_JoltPhysics.SetLinearVelocity(g_DevCharacter, velocity);

    // Also force-set Jolt body position so it doesn't drift
    if (g_CharGrounded) {
        g_JoltPhysics.SetLinearVelocity(g_DevCharacter, Vector3(velocity.X, 0, velocity.Z));
    }

    // Update orientation
    g_DevCharacter->SetProperty("Orientation", Vector3(0, g_CharYaw, 0));

    // ── Procedural animation ──
    bool isMoving = (mag > 0.01f);
    bool isAirborne = !g_CharGrounded;

    if (isMoving && !isAirborne) {
        g_WalkCycleTime += dt * (float)walkSpeed * 0.4f;
    } else {
        if (g_WalkCycleTime > 0.001f) g_WalkCycleTime *= 0.85f;
        else g_WalkCycleTime = 0.0f;
    }

    float swing = sinf(g_WalkCycleTime) * 30.0f;

    float yawR = g_CharYaw * 3.14159265f / 180.0f;
    float cs = cosf(yawR), sn = sinf(yawR);
    auto rotateXZ = [&](float x, float z) -> Vector3 {
        return Vector3(x * cs + z * sn, 0, -x * sn + z * cs);
    };

    // Keep cosmetic limbs welded to HRP
    for (auto* child : g_DevCharacterModel->GetChildren()) {
        if (!child->IsA("Part") || child->Name == "HumanoidRootPart") continue;

        Vector3 localOffset(0, 0, 0);
        Vector3 extraRot(0, g_CharYaw, 0);

        if (child->Name == "Head") {
            localOffset = Vector3(0, 1.5f, 0);
        } else if (child->Name == "Torso") {
            localOffset = Vector3(0, 0, 0);
        } else if (child->Name == "Left Arm") {
            Vector3 r = rotateXZ(-1.5f, 0);
            localOffset = Vector3(r.X, 0, r.Z);
            if (!isAirborne) extraRot.X = -swing;
            else extraRot.X = -20.0f;
        } else if (child->Name == "Right Arm") {
            Vector3 r = rotateXZ(1.5f, 0);
            localOffset = Vector3(r.X, 0, r.Z);
            if (!isAirborne) extraRot.X = swing;
            else extraRot.X = -20.0f;
        } else if (child->Name == "Left Leg") {
            Vector3 r = rotateXZ(-0.5f, 0);
            localOffset = Vector3(r.X, -2.0f, r.Z);
            if (!isAirborne) extraRot.X = swing;
            else extraRot.X = 0.0f;
        } else if (child->Name == "Right Leg") {
            Vector3 r = rotateXZ(0.5f, 0);
            localOffset = Vector3(r.X, -2.0f, r.Z);
            if (!isAirborne) extraRot.X = -swing;
            else extraRot.X = 0.0f;
        }

        child->SetProperty("Position", pos + localOffset);
        child->SetProperty("Orientation", extraRot);
    }

    g_Camera.Target = Vector3(pos.X, pos.Y + 1.0f, pos.Z);
}

static void DoPlay() {
    if (g_IsRunning) return;
    g_IsRunning = true;

    // Force focus to Scene tab for one frame
    g_ScriptEditor.forceFocusTab = 0;

    // Clear output on Play (Phase 10d)
    g_OutputLog.clear();
    g_OutputLog.push_back("[Play] Entering Play mode (F5)");

    // Save all Part positions and orientations so we can restore on Stop
    g_SavedPositions.clear();
    g_SavedOrientations.clear();
    auto descendants = g_GameRoot->GetDescendants();
    for (auto* inst : descendants) {
        if (inst->IsA("Part") && !inst->IsDestroyed()) {
            if (inst->HasProperty("Position")) {
                auto pv = inst->GetProperty("Position");
                if (std::holds_alternative<Vector3>(pv))
                    g_SavedPositions[inst] = std::get<Vector3>(pv);
            }
            if (inst->HasProperty("Orientation")) {
                auto ov = inst->GetProperty("Orientation");
                if (std::holds_alternative<Vector3>(ov))
                    g_SavedOrientations[inst] = std::get<Vector3>(ov);
            }
        }
    }

    g_Physics.Reset();
    g_LastFrameTime = (float)SDL_GetTicks() / 1000.0f;
    g_ScriptEditor.SaveAll();

    // ── Save camera state before Play ──
    g_SavedCameraTarget = g_Camera.Target;
    g_SavedCameraDistance = g_Camera.Distance;
    g_SavedCameraYaw = g_Camera.Yaw;
    g_SavedCameraPitch = g_Camera.Pitch;

    // ── Create Dev Player & Full 6-Part Character (Phase 13a) ──
    Instance* playersSvc = g_GameRoot->FindFirstChild("Players");
    Instance* workspaceSvc = g_GameRoot->FindFirstChild("Workspace");
    if (playersSvc && workspaceSvc) {
        if (auto* oldPlayer = playersSvc->FindFirstChild("Dev")) oldPlayer->Destroy();
        if (auto* oldChar = workspaceSvc->FindFirstChild("Dev")) oldChar->Destroy();

        // Player instance
        Instance* devPlayer = Instance::Create("Player");
        devPlayer->Name = "Dev";
        devPlayer->SetParent(playersSvc);

        // Character Model
        Instance* devChar = Instance::Create("Model");
        devChar->Name = "Dev";
        devChar->SetParent(workspaceSvc);

        Vector3 spawn = g_SpawnPoint;

        // Random torso color (chosen once per spawn)
        Color3 torsoColor(
            0.3f + (rand() % 70) / 100.0f,
            0.3f + (rand() % 70) / 100.0f,
            0.3f + (rand() % 70) / 100.0f
        );

        // Skin tone (pale yellow)
        Color3 skinColor(0.95f, 0.87f, 0.65f);
        // Dark grey for legs
        Color3 legColor(0.25f, 0.25f, 0.28f);

        // HumanoidRootPart (invisible driver, 2x2x1)
        Instance* hrp = Instance::Create("Part");
        hrp->Name = "HumanoidRootPart";
        hrp->SetProperty("Size", Vector3(2, 2, 1));
        hrp->SetProperty("Position", spawn);
        hrp->SetProperty("Color", Color3(0.5f, 0.5f, 1.0f));
        hrp->SetProperty("Transparency", 1.0); // Invisible
        hrp->SetProperty("Anchored", false);
        hrp->SetProperty("CanCollide", true);
        hrp->SetProperty("Shape", std::string("Block"));
        hrp->SetParent(devChar);

        auto makePart = [&](const std::string& name, Vector3 size, Vector3 relPos, Color3 color, double transparency, bool canCollide, bool anchored) {
            Instance* p = Instance::Create("Part");
            p->Name = name;
            p->SetProperty("Size", size);
            p->SetProperty("Position", Vector3(spawn.X + relPos.X, spawn.Y + relPos.Y, spawn.Z + relPos.Z));
            p->SetProperty("Color", color);
            p->SetProperty("Transparency", transparency);
            p->SetProperty("Anchored", anchored);
            p->SetProperty("CanCollide", canCollide);
            p->SetProperty("Shape", std::string("Block"));
            p->SetParent(devChar);
            return p;
        };

        makePart("Head",      Vector3(2, 1, 1), Vector3(0, 1.5f, 0), skinColor, 0.0, false, true);
        makePart("Torso",     Vector3(2, 2, 1), Vector3(0, 0, 0),    torsoColor, 0.0, false, true);
        makePart("Left Arm",  Vector3(1, 2, 1), Vector3(-1.5f, 0, 0), skinColor, 0.0, false, true);
        makePart("Right Arm", Vector3(1, 2, 1), Vector3(1.5f, 0, 0),  skinColor, 0.0, false, true);
        makePart("Left Leg",  Vector3(1, 2, 1), Vector3(-0.5f, -2.0f, 0), legColor, 0.0, false, true);
        makePart("Right Leg", Vector3(1, 2, 1), Vector3(0.5f, -2.0f, 0), legColor, 0.0, false, true);

        // Humanoid
        Instance* humanoid = Instance::Create("Humanoid");
        humanoid->Name = "Humanoid";
        humanoid->SetParent(devChar);

        // Link player → character
        devPlayer->SetProperty("Character", devChar);

        // Store refs
        g_DevCharacter = hrp;
        g_DevHumanoid = humanoid;
        g_DevCharacterModel = devChar;
        g_CharVelocity = {0, 0, 0};
        g_CharYaw = 0.0f;
        g_WalkCycleTime = 0.0f;
        g_CharacterDead = false;
        g_RespawnTimer = 0.0f;

        // Position camera behind character (Custom mode)
        g_Camera.Target = Vector3(spawn.X, spawn.Y + 1.0f, spawn.Z);
        g_Camera.Distance = 15.0f;
        g_Camera.Pitch = 15.0f;
        g_Camera.Yaw = 0.0f;
    }

    g_Scheduler.Init(g_LuaState);

    // ── Crawl the DataModel and execute any Scripts! ──
    auto allDescendants = g_GameRoot->GetDescendants();
    for (auto* inst : allDescendants) {
        if (inst->ClassName == "Script" || inst->ClassName == "LocalScript") {
            // Check if disabled
            bool disabled = GetProp<bool>(inst, "Disabled", false);
            if (disabled) continue;

            std::string source = GetProp<std::string>(inst, "Source", "");
            if (!source.empty()) {
                g_Scheduler.SpawnScript(inst->Name, source, inst);
            }
        }
    }

    g_OutputLog.push_back("[Play] Entering Play mode (F5)");
    UpdateWindowTitle();
}

static void DoStop() {
    if (!g_IsRunning) return;
    g_IsRunning = false;
    g_ShowConsole = false;
    g_ShowDebugHUD = false;

    g_JoltPhysics.ClearAll();

    // ── Clean up Dev Player & Character ──
    Instance* playersSvc = g_GameRoot->FindFirstChild("Players");
    if (playersSvc) {
        Instance* devPlayer = playersSvc->FindFirstChild("Dev");
        if (devPlayer) devPlayer->Destroy();
    }
    Instance* workspaceSvc = g_GameRoot->FindFirstChild("Workspace");
    if (workspaceSvc) {
        Instance* devChar = workspaceSvc->FindFirstChild("Dev");
        if (devChar) devChar->Destroy();
    }
    g_DevCharacter = nullptr;
    g_DevHumanoid = nullptr;
    g_DevCharacterModel = nullptr;
    g_CharVelocity = {0, 0, 0};
    g_WalkCycleTime = 0.0f;
    g_CharacterDead = false;
    g_RespawnTimer = 0.0f;

    // ── Restore camera state ──
    g_Camera.Target = g_SavedCameraTarget;
    g_Camera.Distance = g_SavedCameraDistance;
    g_Camera.Yaw = g_SavedCameraYaw;
    g_Camera.Pitch = g_SavedCameraPitch;

    // Restore all saved positions and orientations
    for (auto& [inst, pos] : g_SavedPositions) {
        if (inst && !inst->IsDestroyed()) {
            inst->SetProperty("Position", pos);
        }
    }
    for (auto& [inst, orient] : g_SavedOrientations) {
        if (inst && !inst->IsDestroyed()) {
            inst->SetProperty("Orientation", orient);
        }
    }
    g_SavedPositions.clear();
    g_SavedOrientations.clear();
    g_Physics.Reset();
    LuauBridge::ClearCallbacks();
    g_Scheduler.Clear();

    g_OutputLog.push_back("[Stop] Exiting Play mode (Shift+F5)");
    UpdateWindowTitle();
}

static void DoLoad() {
    std::string path = OpenFileDialog();
    if (path.empty()) return;
    if (g_Editor.LoadPlace(path, g_GameRoot)) {
        g_CurrentFilePath = path;
        g_Dirty = false;
        g_SelectedInstance = nullptr;
        g_Editor.Selected = nullptr;
        g_OutputLog.push_back("[Load] Loaded from " + g_CurrentFilePath);
        UpdateWindowTitle();
    } else {
        g_OutputLog.push_back("[Load] ERROR: Failed to load file");
    }
}

// ── Luau Print ──
static int luaPrint(lua_State* L) {
    int n = lua_gettop(L);
    std::string line;
    for (int i = 1; i <= n; i++) {
        if (i > 1) line += "  ";
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        if (s) line += std::string(s, len);
        lua_pop(L, 1);
    }
    g_OutputLog.push_back(line);
    std::cout << "[Wavey] " << line << std::endl;
    return 0;
}

// ── Render the floating Insert Object popup ──
static void RenderInsertPopup() {
    if (!g_InsertTarget || g_InsertTarget->IsDestroyed()) {
        g_InsertTarget = nullptr;
        return;
    }

    Instance* parent = g_InsertTarget;

    // Position anchored right below the clicked instance in the tree
    ImGui::SetNextWindowPos(g_InsertPopupAnchor, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(240, 220)); // Compact fixed size

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
                           | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoDocking
                           | ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoSavedSettings;

    bool openFlag = true;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35f, 0.55f, 0.90f, 0.70f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);

    if (ImGui::Begin("##InsertPopup", &openFlag, flags)) {

        // Search bar (full width)
        if (g_InsertFocusSearch) {
            ImGui::SetKeyboardFocusHere();
            g_InsertFocusSearch = false;
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##search", "Search...", g_InsertSearch, sizeof(g_InsertSearch));

        ImGui::Separator();

        // Filter and display list
        std::string filter = g_InsertSearch;
        std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
        auto allClasses = Instance::GetRegisteredClasses();

        ImGui::BeginChild("InsertList", ImVec2(0, 0), false);
        bool clicked = false;
        std::string chosen;

        for (const auto& c : allClasses) {
            std::string lc = c;
            std::transform(lc.begin(), lc.end(), lc.begin(), ::tolower);
            if (!filter.empty() && lc.find(filter) == std::string::npos) continue;

            bool hasIcon = g_Icons.HasIcon(c);
            float rowHeight = ImGui::GetFrameHeight();

            // 1. Selectable claims the row (including scroll height)
            std::string selectId = "##sel_" + c;
            if (ImGui::Selectable(selectId.c_str(), false, 0, ImVec2(0, rowHeight))) {
                chosen = c;
                clicked = true;
            }

            // 2. Overlay icon + text without moving the cursor at all
            if (ImGui::IsItemVisible()) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 rMin = ImGui::GetItemRectMin();
                ImVec2 rMax = ImGui::GetItemRectMax();
                float padX = ImGui::GetStyle().FramePadding.x;
                float spacing = 2.0f;

                float iconX = rMin.x + padX;
                float iconY = rMin.y + (rMax.y - rMin.y - 16.0f) * 0.5f;

                if (hasIcon) {
                    dl->AddImage(
                        (ImTextureID)(intptr_t)g_Icons.GetIcon(c),
                        ImVec2(iconX, iconY),
                        ImVec2(iconX + 16.0f, iconY + 16.0f)
                    );
                }

                float textX = iconX + (hasIcon ? 16.0f + spacing : 0.0f);
                float textY = rMin.y + (rMax.y - rMin.y - ImGui::GetTextLineHeight()) * 0.5f;
                dl->AddText(ImVec2(textX, textY), ImGui::GetColorU32(ImGuiCol_Text), c.c_str());
            }
        }
        ImGui::EndChild();

        // Perform insertion
        if (clicked) {
            Instance* newInst = g_Editor.InsertObject(chosen, parent);
            if (newInst) {
                g_Editor.Select(newInst);
                g_SelectedInstance = newInst;
                g_Undo.PushInsert(newInst); // Register for undo
            }
            if (parent) {
                g_AutoExpandNodes.insert(parent);
            }

            // Auto-open script editor tab if the created instance is a script
            if (newInst && (chosen == "Script" || chosen == "LocalScript" || chosen == "ModuleScript")) {
                g_ScriptEditor.OpenScript(newInst);
                showViewport = true;
                ImGui::SetWindowFocus("Viewport");
            }

            MarkDirty();
            g_InsertTarget = nullptr;
        }

        // Close if user clicks physically outside
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            ImVec2 pos = ImGui::GetWindowPos();
            ImVec2 size = ImGui::GetWindowSize();
            ImVec2 mouse = ImGui::GetIO().MousePos;

            if (mouse.x < pos.x || mouse.x > pos.x + size.x ||
                mouse.y < pos.y || mouse.y > pos.y + size.y) {
                g_InsertTarget = nullptr;
            }
        }
    }
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    if (!openFlag) {
        g_InsertTarget = nullptr;
    }
}

static void RenderInstanceTree(Instance* inst) {
    if (!inst || inst->IsDestroyed()) return;

    ImGui::PushID(inst);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    auto children = inst->GetChildren();

    int liveChildren = 0;
    for (auto* c : children) if (!c->IsDestroyed()) liveChildren++;
    if (liveChildren == 0)
        flags |= ImGuiTreeNodeFlags_Leaf;
    if (g_Editor.IsSelected(inst))
        flags |= ImGuiTreeNodeFlags_Selected;

    // Auto-expand if flagged
    if (g_AutoExpandNodes.count(inst) > 0) {
        ImGui::SetNextItemOpen(true);
        g_AutoExpandNodes.erase(inst);
    }

    // 1. Draw empty tree node (only arrow + full row select box)
    bool open = ImGui::TreeNodeEx("##node", flags);

    // ═══════════════════════════════════════════════════════
    // Row Interaction Queries (Must run IMMEDIATELY after TreeNodeEx)
    // ═══════════════════════════════════════════════════════
    
    // ── Viewport Click-to-Select (Only in Editor Mode, and only when 3D Viewport is active) ──
    if (!standaloneMode && g_ScriptEditor.activeTab == 0 && g_ViewportHovered 
        && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered() && !ImGuizmo::IsOver()
        && !g_HoveredUI) {
        
        ImGuiIO& sio = ImGui::GetIO();
        Instance* workspace = g_GameRoot->FindFirstChild("Workspace");
        
        // 1. Cast Ray to find clicked Part
        Instance* hit = g_Editor.Raycast(
            sio.MousePos.x, sio.MousePos.y,
            g_ViewportScreenPos.x, g_ViewportScreenPos.y,
            g_ViewportSize.x, g_ViewportSize.y,
            g_Camera, workspace ? workspace : g_GameRoot
        );

        // 2. Process Selection (Shift / Ctrl Aware)
        if (hit) {
            if (sio.KeyShift || sio.KeyCtrl) {
                g_Editor.AddToSelection(hit);
            } else {
                g_Editor.Select(hit);
            }
        } else {
            // Clicking empty space clears selection (unless Shift/Ctrl is held)
            if (!sio.KeyShift && !sio.KeyCtrl) {
                g_Editor.ClearSelection();
            }
        }
        g_SelectedInstance = g_Editor.Selected;
    }

    // B. Double Click to Open Script
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (inst->ClassName == "Script" || inst->ClassName == "LocalScript" || inst->ClassName == "ModuleScript") {
            g_ScriptEditor.OpenScript(inst);
            // Force focus Viewport panel when script is double-clicked
            showViewport = true;
            ImGui::SetWindowFocus("Viewport");
        }
    }

    // C. Drag Source
    bool isService = inst->GetParent() && inst->GetParent()->ClassName == "DataModel";
    bool isProtected = (inst->ClassName == "DataModel") || isService;

    if (!isProtected && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload("WAVEY_INSTANCE", &inst, sizeof(Instance*));
        ImGui::Text("Move %s", inst->Name.c_str());
        ImGui::EndDragDropSource();
    }

    // D. Drag Target
    if (ImGui::BeginDragDropTarget()) {
        if (liveChildren > 0 && !open) {
            float now = (float)SDL_GetTicks() / 1000.0f;
            if (g_HoverExpandCandidate != inst) {
                g_HoverExpandCandidate = inst;
                g_HoverExpandStartTime = now;
            } else if (now - g_HoverExpandStartTime > HOVER_EXPAND_DELAY) {
                g_AutoExpandNodes.insert(inst);
                g_HoverExpandCandidate = nullptr;
            }
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("WAVEY_INSTANCE")) {
            Instance* draggedInst = *(Instance**)payload->Data;
            bool isDescendant = false;
            Instance* check = inst;
            while (check) {
                if (check == draggedInst) { isDescendant = true; break; }
                check = check->GetParent();
            }

            if (draggedInst && draggedInst != inst && !isDescendant) {
                Instance* oldParent = draggedInst->GetParent();
                g_Undo.PushReparent(draggedInst, oldParent, inst);
                draggedInst->SetParent(inst);
                g_AutoExpandNodes.insert(inst);
                g_SelectedInstance = draggedInst;
                g_Editor.Select(draggedInst);
                MarkDirty();
            }
        }
        ImGui::EndDragDropTarget();
    }

    // E. Context Menu
    if (ImGui::BeginPopupContextItem("##ctx")) {
        g_SelectedInstance = inst;
        g_Editor.Select(inst);

        if (ImGui::MenuItem("Insert Object...")) {
            g_InsertTarget = inst;
            g_InsertSearch[0] = '\0';
            g_InsertFocusSearch = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Rename")) {
            g_FocusRename = true;
            if (!showProperties) showProperties = true;
        }
        if (ImGui::MenuItem("Copy", "Ctrl+C")) {
            for (auto* c : g_Clipboard) if (c) delete c;
            g_Clipboard.clear();
            for (Instance* s : g_Editor.SelectedList) {
                if (s && !s->IsDestroyed()) g_Clipboard.push_back(s->Clone());
            }
        }
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, !g_Clipboard.empty())) {
            for (Instance* c : g_Clipboard) {
                if (c) {
                    Instance* pasted = c->Clone();
                    pasted->SetParent(inst);
                    g_Editor.Select(pasted);
                    g_SelectedInstance = pasted;
                    g_Undo.PushInsert(pasted);
                }
            }
            MarkDirty();
        }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
            Instance* clone = inst->Clone();
            clone->Name = inst->Name;
            clone->SetParent(inst->GetParent());
            g_Editor.Select(clone);
            g_SelectedInstance = clone;
            g_Undo.PushInsert(clone);
            MarkDirty();
        }
        ImGui::Separator();
        if (!isProtected) {
            if (ImGui::MenuItem("Delete", "Del")) {
                g_Editor.Select(inst);
                // Push undo BEFORE destroying so deleted instances can be restored
                for (Instance* s : g_Editor.SelectedList) {
                    if (s && !s->IsDestroyed() && s->ClassName != "DataModel") {
                        bool isService = s->GetParent() && s->GetParent()->ClassName == "DataModel";
                        if (!isService) {
                            g_Undo.PushDelete(s, s->GetParent());
                        }
                    }
                }
                g_Editor.DeleteSelected();
                g_SelectedInstance = nullptr;
                MarkDirty();
            }
        } else {
            ImGui::MenuItem("Delete", "Del", false, false);
        }
        ImGui::EndPopup();
    }

    // F. Insert Popup Anchor
    if (g_InsertTarget == inst) {
        g_InsertPopupAnchor = ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y);
    }

    // ═══════════════════════════════════════════════════════
    // Draw Custom Row Content (Icons & Text)
    // ═══════════════════════════════════════════════════════
    bool hasIcon = g_Icons.HasIcon(inst->ClassName);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);

    if (hasIcon) {
        ImGui::Image((ImTextureID)(intptr_t)g_Icons.GetIcon(inst->ClassName), ImVec2(14, 14));
        ImGui::SameLine(0.0f, 2.0f);
    }

    ImGui::TextUnformatted(inst->Name.c_str());

    // ── Render Children ──
    if (open) {
        for (auto* child : children) {
            if (!child->IsDestroyed())
                RenderInstanceTree(child);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

// ── Editable Property Row ──
static void DisplayProperty(Instance* inst, const std::string& name,
                            const Instance::PropertyValue& val) {
    ImGui::PushID(name.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", name.c_str());
    ImGui::SameLine(140);
    ImGui::SetNextItemWidth(-1);

    bool changed = false;

    if (std::holds_alternative<bool>(val)) {
        bool v = std::get<bool>(val);
        if (ImGui::Checkbox("##v", &v)) {
            inst->SetProperty(name, v); changed = true;
        }
    }
    else if (std::holds_alternative<int>(val)) {
        int v = std::get<int>(val);
        if (ImGui::DragInt("##v", &v)) {
            inst->SetProperty(name, v); changed = true;
        }
    }
    else if (std::holds_alternative<double>(val)) {
        float v = (float)std::get<double>(val);
        bool clamped01 = (name == "Transparency" || name == "Reflectance");
        float minV = clamped01 ? 0.0f : -FLT_MAX;
        float maxV = clamped01 ? 1.0f : FLT_MAX;
        if (ImGui::DragFloat("##v", &v, clamped01 ? 0.01f : 0.1f, minV, maxV)) {
            inst->SetProperty(name, (double)v); changed = true;
        }
    }
    else if (std::holds_alternative<std::string>(val)) {
        std::string s = std::get<std::string>(val);

        // Source property shows a preview (first 20 chars) grayed out
        if (name == "Source") {
            std::string preview = s;
            // Replace newlines with spaces for cleaner display
            for (auto& ch : preview) if (ch == '\n' || ch == '\r') ch = ' ';
            if (preview.length() > 20) preview = preview.substr(0, 20) + "...";
            if (preview.empty()) preview = "(empty)";
            ImGui::TextDisabled("%s", preview.c_str());
        } else {
            char buf[512];
            strncpy_s(buf, s.c_str(), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            if (ImGui::InputText("##v", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                inst->SetProperty(name, std::string(buf)); changed = true;
            }
        }
    }
    else if (std::holds_alternative<Vector3>(val)) {
        Vector3 v = std::get<Vector3>(val);
        float arr[3] = { v.X, v.Y, v.Z };
        if (ImGui::DragFloat3("##v", arr, 0.1f)) {
            inst->SetProperty(name, Vector3(arr[0], arr[1], arr[2])); changed = true;
        }
    }
    else if (std::holds_alternative<Color3>(val)) {
        Color3 c = std::get<Color3>(val);
        float arr[3] = { c.R, c.G, c.B };
        if (ImGui::ColorEdit3("##v", arr)) {
            inst->SetProperty(name, Color3(arr[0], arr[1], arr[2])); changed = true;
        }
    }
    else if (std::holds_alternative<Instance*>(val)) {
        auto* ref = std::get<Instance*>(val);
        ImGui::TextDisabled("%s", ref ? ref->Name.c_str() : "nil");
    }
    else {
        ImGui::TextDisabled("nil");
    }

    if (changed) MarkDirty();
    ImGui::PopID();
}

// ── Build the DataModel ──
static Instance* CreateDataModel() {
    Instance* game = new Instance("DataModel");
    game->Name = "game";

    const char* services[] = {
        "Workspace", "Lighting", "Players",
        "ReplicatedStorage", "ServerStorage",
        "ServerScriptService", "StarterGui",
        "StarterPack", "SoundService", "RunService"
    };

    for (const char* svc : services) {
        Instance* service = new Instance(svc);
        service->Name = svc;
        service->SetParent(game);
    }

    Instance* camera = Instance::Create("Camera");
    camera->Name = "Camera";
    camera->SetParent(game->FindFirstChild("Workspace"));

    Instance* baseplate = Instance::Create("Part");
    baseplate->Name = "Baseplate";
    baseplate->SetProperty("Size", Vector3(512, 1, 512));
    baseplate->SetProperty("Position", Vector3(0, -0.5f, 0));
    baseplate->SetProperty("Color", Color3(0.388f, 0.372f, 0.384f));
    baseplate->SetProperty("Anchored", true);
    baseplate->SetProperty("Material", std::string("Plastic"));
    baseplate->SetParent(game->FindFirstChild("Workspace"));

    return game;
}

// ── Compute absolute rect from UDim2 (shared by renderer and hit tester) ──
static void ComputeUIRect(Instance* inst, float parentX, float parentY, float parentW, float parentH,
                          float& outX, float& outY, float& outW, float& outH) {
    UDim2 pos = GetProp<UDim2>(inst, "Position", UDim2(0, 0, 0, 0));
    UDim2 size = GetProp<UDim2>(inst, "Size", UDim2(0, 100, 0, 100));
    outW = size.X.Scale * parentW + size.X.Offset;
    outH = size.Y.Scale * parentH + size.Y.Offset;
    outX = parentX + pos.X.Scale * parentW + pos.X.Offset;
    outY = parentY + pos.Y.Scale * parentH + pos.Y.Offset;
}

// ── Recursive UI Hit Test (reverse order = topmost first) ──
static Instance* HitTestUI(Instance* inst, float parentX, float parentY, float parentW, float parentH,
                           float mouseX, float mouseY) {
    if (!inst || inst->IsDestroyed()) return nullptr;
    if (!GetProp<bool>(inst, "Visible", true)) return nullptr;
    if (!GetProp<bool>(inst, "Active", true)) return nullptr;

    float absX, absY, absW, absH;

    // ScreenGui takes full viewport
    if (inst->ClassName == "ScreenGui") {
        absX = parentX; absY = parentY; absW = parentW; absH = parentH;
    } else {
        ComputeUIRect(inst, parentX, parentY, parentW, parentH, absX, absY, absW, absH);
    }

    // Check children in REVERSE order (last child = topmost Z)
    auto children = inst->GetChildren();
    for (int i = (int)children.size() - 1; i >= 0; i--) {
        Instance* hit = HitTestUI(children[i], absX, absY, absW, absH, mouseX, mouseY);
        if (hit) return hit;
    }

    // Check self (only for interactive classes)
    if (inst->ClassName == "TextButton" || inst->ClassName == "ImageButton" ||
        inst->ClassName == "Frame" || inst->ClassName == "TextLabel" || inst->ClassName == "ImageLabel") {
        if (mouseX >= absX && mouseX <= absX + absW && mouseY >= absY && mouseY <= absY + absH) {
            return inst;
        }
    }

    return nullptr;
}

// ── Recursive 2D UI Drawing Engine (Phase 11b) ──
static void RenderUIHierarchy(Instance* inst, float parentX, float parentY, float parentW, float parentH) {
    if (!inst || inst->IsDestroyed()) return;

    bool visible = GetProp<bool>(inst, "Visible", true);
    if (!visible) return;

    float absX = parentX;
    float absY = parentY;
    float absW = parentW;
    float absH = parentH;

    if (inst->ClassName == "Frame" || inst->ClassName == "TextLabel" ||
        inst->ClassName == "TextButton" || inst->ClassName == "ImageLabel") {

        UDim2 pos = GetProp<UDim2>(inst, "Position", UDim2(0, 0, 0, 0));
        UDim2 size = GetProp<UDim2>(inst, "Size", UDim2(0, 100, 0, 100));

        absW = size.X.Scale * parentW + size.X.Offset;
        absH = size.Y.Scale * parentH + size.Y.Offset;
        absX = parentX + pos.X.Scale * parentW + pos.X.Offset;
        absY = parentY + pos.Y.Scale * parentH + pos.Y.Offset;

        double transp = GetProp<double>(inst, "BackgroundTransparency", 0.0);
        if (transp < 1.0) {
            Color3 bgCol = GetProp<Color3>(inst, "BackgroundColor3", Color3(0.15f, 0.15f, 0.15f));
            g_Renderer.DrawRect2D(absX, absY, absW, absH, bgCol, (float)transp);
        }

        int borderSize = GetProp<int>(inst, "BorderSizePixel", 1);
        if (borderSize < 0) borderSize = 0;
        if (borderSize > 10) borderSize = 10;
        if (borderSize > 0) {
            Color3 borderCol = GetProp<Color3>(inst, "BorderColor3", Color3(0, 0, 0));
            g_Renderer.DrawRect2D(absX, absY, absW, (float)borderSize, borderCol, 0.0f);
            g_Renderer.DrawRect2D(absX, absY + absH - borderSize, absW, (float)borderSize, borderCol, 0.0f);
            g_Renderer.DrawRect2D(absX, absY, (float)borderSize, absH, borderCol, 0.0f);
            g_Renderer.DrawRect2D(absX + absW - borderSize, absY, (float)borderSize, absH, borderCol, 0.0f);
        }
    }

    if (inst->ClassName == "TextLabel" || inst->ClassName == "TextButton") {
        std::string text = GetProp<std::string>(inst, "Text", "Label");
        if (!text.empty()) {
            Color3 textCol = GetProp<Color3>(inst, "TextColor3", Color3(1, 1, 1));
            int textSize = GetProp<int>(inst, "TextSize", 14);
            double textTransp = GetProp<double>(inst, "TextTransparency", 0.0);
            std::string fontName = GetProp<std::string>(inst, "Font", "Roboto");
            std::string xAlign = GetProp<std::string>(inst, "TextXAlignment", "Center");
            std::string yAlign = GetProp<std::string>(inst, "TextYAlignment", "Center");
            bool scaled = GetProp<bool>(inst, "TextScaled", false);

            float renderSize = (float)textSize;

            // TextScaled: binary search to find max size that fits
            if (scaled) {
                float lo = 2.0f, hi = absH;
                for (int iter = 0; iter < 10; iter++) {
                    float mid = (lo + hi) * 0.5f;
                    float w = g_Renderer.MeasureTextWidth(text, mid, fontName);
                    if (w <= absW && mid <= absH) lo = mid;
                    else hi = mid;
                }
                renderSize = lo;
            }

            float textW = g_Renderer.MeasureTextWidth(text, renderSize, fontName);

            float tx = absX;
            if (xAlign == "Center") tx = absX + (absW - textW) * 0.5f;
            else if (xAlign == "Right") tx = absX + absW - textW - 4.0f;
            else tx = absX + 4.0f; // Left (default)

            float ty = absY;
            if (yAlign == "Center") ty = absY + (absH - renderSize) * 0.5f;
            else if (yAlign == "Bottom") ty = absY + absH - renderSize - 2.0f;
            else ty = absY + 2.0f; // Top

            g_Renderer.DrawText2D(text, tx, ty, renderSize, textCol, (float)textTransp, fontName);
        }
    }
    else if (inst->ClassName == "ImageLabel") {
        std::string imgAsset = GetProp<std::string>(inst, "Image", "");
        if (!imgAsset.empty()) {
            Color3 imgCol = GetProp<Color3>(inst, "ImageColor3", Color3(1, 1, 1));
            double imgTransp = GetProp<double>(inst, "ImageTransparency", 0.0);
            GLuint tex = g_Icons.GetIcon(imgAsset);
            if (tex != 0) {
                g_Renderer.DrawImage2D(tex, absX, absY, absW, absH, imgCol, (float)imgTransp);
            }
        }
    }

    // Sort children by ZIndex (stable) before rendering
    auto children = inst->GetChildren();
    std::vector<Instance*> sorted;
    for (auto* c : children) {
        if (!c->IsDestroyed()) sorted.push_back(c);
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](Instance* a, Instance* b) {
        int za = 1, zb = 1;
        if (a->HasProperty("ZIndex")) {
            auto v = a->GetProperty("ZIndex");
            if (std::holds_alternative<int>(v)) za = std::get<int>(v);
        }
        if (b->HasProperty("ZIndex")) {
            auto v = b->GetProperty("ZIndex");
            if (std::holds_alternative<int>(v)) zb = std::get<int>(v);
        }
        return za < zb;
    });

    for (auto* child : sorted) {
        RenderUIHierarchy(child, absX, absY, absW, absH);
    }
}

// ═══════════════════════════════════════════════════════
// MAIN
// ═══════════════════════════════════════════════════════
int main(int argc, char* argv[]) {
    try {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 1;

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

        g_Window = SDL_CreateWindow(
            "Wavey Studio", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!g_Window) return 1;

        SDL_GLContext glContext = SDL_GL_CreateContext(g_Window);
        SDL_GL_MakeCurrent(g_Window, glContext);
        SDL_GL_SetSwapInterval(1);

        glewExperimental = GL_TRUE;
        glewInit();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 4.0f;
        style.FrameRounding = 2.0f;
        style.Colors[ImGuiCol_WindowBg]       = ImVec4(0.13f, 0.13f, 0.15f, 1.00f);
        style.Colors[ImGuiCol_TitleBg]        = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);
        style.Colors[ImGuiCol_TitleBgActive]  = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
        style.Colors[ImGuiCol_MenuBarBg]      = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
        style.Colors[ImGuiCol_Header]         = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
        style.Colors[ImGuiCol_HeaderHovered]  = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
        style.Colors[ImGuiCol_HeaderActive]   = ImVec4(0.30f, 0.30f, 0.36f, 1.00f);
        style.Colors[ImGuiCol_DockingPreview] = ImVec4(0.35f, 0.55f, 0.90f, 0.70f);

        g_Renderer.Init();
        g_JoltPhysics.Init();

        char exePath[MAX_PATH];
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        fs::path exeDir = fs::path(exePath).parent_path();
        fs::path projectRoot = fs::path(exePath).parent_path().parent_path().parent_path();

        std::string iconPath = (exeDir / "wavey" / "ui" / "icons").string();
        fs::path fontsPath = exeDir / "wavey" / "fonts";

        g_Renderer.InitUI(fontsPath.string());
        g_Icons.LoadIcons(iconPath);
        
        Autocomplete::InitBuiltins();
        Autocomplete::SnapshotEnums();
        Autocomplete::SnapshotDataModel(g_GameRoot);

        // ── Check if we should run in Standalone Player Mode ──
        if (fs::exists("game.wavey")) {
            standaloneMode = true;
            g_IsRunning = true; // Auto-play physics and scripts
            SDL_SetWindowFullscreen(g_Window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        }

        // Only init ImGui if we are NOT in standalone mode
        if (!standaloneMode) {
            ImGui_ImplSDL2_InitForOpenGL(g_Window, glContext);
            ImGui_ImplOpenGL3_Init("#version 330");
        }

        // ── Luau VM ──
        lua_State* L = luaL_newstate();
        luaL_openlibs(L);
        lua_pushcfunction(L, luaPrint, "print");
        lua_setglobal(L, "print");
        
        g_GameRoot = CreateDataModel();
        LuauBridge::Initialize(L, g_GameRoot);
        g_LuaState = L;

        // ── Test Script ──
        const char* baseScript = R"LUAU(
            print("Running Wavey Studio v0.11")
        )LUAU";

        // ── Load Script/Level ──
        if (standaloneMode) {
            if (g_Editor.LoadPlace("game.wavey", g_GameRoot)) {
                // DoPlay() handles everything: scripts, player, character, scheduler, autosave positions
                g_IsRunning = false; // Force reset so DoPlay runs cleanly
                DoPlay();
            }
        } else {
            // Run editor startup test script
            std::string bytecode = Luau::compile(baseScript);
            if (luau_load(g_LuaState, "=WaveyStartup", bytecode.data(), bytecode.size(), 0) == 0) {
                lua_resume(g_LuaState, nullptr, 0);
            } else {
                const char* err = lua_tostring(L, -1);
                g_OutputLog.push_back(std::string("ERROR: ") + (err ? err : "unknown"));
            }
        }
        
        // ═══════════════════════════════════════════════════════
        // MAIN LOOP
        // ═══════════════════════════════════════════════════════
        bool running = true;
        SDL_Event event;

        // Clear hover-expand candidate when drag ends
        if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            g_HoverExpandCandidate = nullptr;
        }

        while (running) {
            while (SDL_PollEvent(&event)) {
                // Feed to UserInputService FIRST during Play mode
                if (g_IsRunning) {
                    g_UIS.ProcessEvent(event);
                    // ── UI Mouse Events (Phase 11c) ──
                    if (event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
                        // Get mouse position relative to the viewport FBO
                        int vpW = (int)g_ViewportSize.x;
                        int vpH = (int)g_ViewportSize.y;
                        float localMouseX = io.MousePos.x - g_ViewportScreenPos.x;
                        float localMouseY = io.MousePos.y - g_ViewportScreenPos.y;

                        // Only test if mouse is inside the viewport area
                        Instance* hitUI = nullptr;
                        if (localMouseX >= 0 && localMouseX <= vpW && localMouseY >= 0 && localMouseY <= vpH) {
                            Instance* starterGui = g_GameRoot->FindFirstChild("StarterGui");
                            if (starterGui) {
                                auto guiChildren = starterGui->GetChildren();
                                // Test ScreenGuis in reverse order
                                for (int i = (int)guiChildren.size() - 1; i >= 0; i--) {
                                    Instance* sg = guiChildren[i];
                                    if (sg->ClassName == "ScreenGui" && GetProp<bool>(sg, "Enabled", true)) {
                                        hitUI = HitTestUI(sg, 0, 0, (float)vpW, (float)vpH, localMouseX, localMouseY);
                                        if (hitUI) break;
                                    }
                                }
                            }
                        }

                        // MouseEnter / MouseLeave
                        if (hitUI != g_HoveredUI) {
                            if (g_HoveredUI) LuauBridge::FireUISignal(g_HoveredUI, "MouseLeave");
                            g_HoveredUI = hitUI;
                            if (g_HoveredUI) LuauBridge::FireUISignal(g_HoveredUI, "MouseEnter");
                        }

                        // MouseButton1Down, MouseButton1Up, and MouseButton1Click
                        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                            g_PressedUI = hitUI;
                            if (hitUI) {
                                LuauBridge::FireUISignal(hitUI, "MouseButton1Down");
                            }
                        }
                        if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                            if (hitUI) {
                                LuauBridge::FireUISignal(hitUI, "MouseButton1Up");
                            }
                            if (g_PressedUI && g_PressedUI == hitUI) {
                                LuauBridge::FireUISignal(hitUI, "MouseButton1Click");
                            }
                            g_PressedUI = nullptr;
                        }
                    }
                }

                // Feed scroll events to camera in Standalone Mode
                if (standaloneMode && event.type == SDL_MOUSEWHEEL) {
                    g_Camera.Distance -= event.wheel.y * 3.0f;
                    g_Camera.Distance = (std::max)(1.0f, (std::min)(g_Camera.Distance, 500.0f));
                }

                if (!standaloneMode) {
                    ImGui_ImplSDL2_ProcessEvent(&event);
                }

                if (event.type == SDL_QUIT) {
                    if (g_Dirty && !g_ConfirmedClose) {
                        g_ShowClosePrompt = true;
                    } else {
                        running = false;
                    }
                }

                if (event.type == SDL_WINDOWEVENT &&
                    event.window.event == SDL_WINDOWEVENT_CLOSE &&
                    event.window.windowID == SDL_GetWindowID(g_Window)) {
                    if (g_Dirty && !g_ConfirmedClose) {
                        g_ShowClosePrompt = true;
                    } else {
                        running = false;
                    }
                }
            }

            // ── Keyboard Shortcuts (Only in Editor Mode) ──
            if (!standaloneMode) {
                const Uint8* keys = SDL_GetKeyboardState(nullptr);
                if (keys) {
                    bool ctrl = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];

                    // ── Gizmo Tool Shortcuts (1=Select, 2=Move, 3=Scale, 4=Rotate) ──
                    if (!ctrl) {
                        if (keys[SDL_SCANCODE_1]) g_CurrentTool = EditorTool::Mouse;
                        if (keys[SDL_SCANCODE_2]) g_CurrentTool = EditorTool::Move;
                        if (keys[SDL_SCANCODE_3]) g_CurrentTool = EditorTool::Scale;
                        if (keys[SDL_SCANCODE_4]) g_CurrentTool = EditorTool::Rotate;
                    }

                    // Ctrl+Z = Undo, Ctrl+Y = Redo
                    bool shift = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
                    ImGuiIO& undoIO = ImGui::GetIO();
                    bool ctrlModifier = undoIO.KeyCtrl;

                    if (ctrlModifier && ImGui::IsKeyPressed(ImGuiKey_Z, false) && !shift) {
                        bool ok = g_Undo.Undo();
                        g_OutputLog.push_back(ok ? "[Edit] Undo OK" : "[Edit] Undo: stack empty");
                        if (ok) MarkDirty();
                    }
                    if (ctrlModifier && (ImGui::IsKeyPressed(ImGuiKey_Y, false) || (ImGui::IsKeyPressed(ImGuiKey_Z, false) && shift))) {
                        bool ok = g_Undo.Redo();
                        g_OutputLog.push_back(ok ? "[Edit] Redo OK" : "[Edit] Redo: stack empty");
                        if (ok) MarkDirty();
                    }

                    // Ctrl+C = Copy, Ctrl+V = Paste, Ctrl+D = Duplicate
                    if (ctrlModifier && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
                        // Copy: clone selection into clipboard
                        for (auto* c : g_Clipboard) if (c) delete c;
                        g_Clipboard.clear();
                        for (Instance* s : g_Editor.SelectedList) {
                            if (s && !s->IsDestroyed()) {
                                g_Clipboard.push_back(s->Clone());
                            }
                        }
                        if (!g_Clipboard.empty()) {
                            g_OutputLog.push_back("[Edit] Copied " + std::to_string(g_Clipboard.size()) + " instance(s)");
                        }
                    }
                    if (ctrlModifier && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
                        // Paste: parent cloned clipboard into same parent as original
                        if (!g_Clipboard.empty()) {
                            g_Editor.ClearSelection();
                            for (Instance* c : g_Clipboard) {
                                if (c) {
                                    Instance* pasted = c->Clone();
                                    pasted->Name = c->Name;
                                    // Find a reasonable parent (workspace or first selected's parent)
                                    Instance* parent = g_GameRoot->FindFirstChild("Workspace");
                                    pasted->SetParent(parent);
                                    g_Editor.AddToSelection(pasted);
                                    g_Undo.PushInsert(pasted);
                                }
                            }
                            g_SelectedInstance = g_Editor.Selected;
                            MarkDirty();
                            g_OutputLog.push_back("[Edit] Pasted " + std::to_string(g_Clipboard.size()) + " instance(s)");
                        }
                    }
                    if (ctrlModifier && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
                        // Duplicate: instant clone of selection into same parent
                        std::vector<Instance*> dupes;
                        for (Instance* s : g_Editor.SelectedList) {
                            if (s && !s->IsDestroyed()) {
                                Instance* dupe = s->Clone();
                                dupe->Name = s->Name;
                                dupe->SetParent(s->GetParent());
                                dupes.push_back(dupe);
                                g_Undo.PushInsert(dupe);
                            }
                        }
                        g_Editor.ClearSelection();
                        for (Instance* d : dupes) g_Editor.AddToSelection(d);
                        g_SelectedInstance = g_Editor.Selected;
                        MarkDirty();
                        if (!dupes.empty()) {
                            g_OutputLog.push_back("[Edit] Duplicated " + std::to_string(dupes.size()) + " instance(s)");
                        }
                    }

                    // Cancel object picking on Escape
                    if (keys[SDL_SCANCODE_ESCAPE]) {
                        g_PickingHostInstance = nullptr;
                        g_PickingPropertyName = "";
                    }

                    // F9 = Toggle Console, F3 = Toggle Debug HUD (Phase 10d)
                    if (ImGui::IsKeyPressed(ImGuiKey_F9, false) && g_ConsoleEnabled && g_IsRunning) {
                        g_ShowConsole = !g_ShowConsole;
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_F3, false) && g_DebugEnabled && g_IsRunning) {
                        g_ShowDebugHUD = !g_ShowDebugHUD;
                    }

                    // F5 = Play, Shift+F5 = Stop
                    if (keys[SDL_SCANCODE_F5]) {
                        bool shift = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
                        if (shift) DoStop();
                        else DoPlay();
                        SDL_Delay(200);
                    }

                    if (keys[SDL_SCANCODE_F7]) {
                        DoBuild();
                        SDL_Delay(200);
                    }

                    if (keys[SDL_SCANCODE_DELETE] && !g_Editor.SelectedList.empty()) {
                        for (Instance* s : g_Editor.SelectedList) {
                            if (s && !s->IsDestroyed() && s->ClassName != "DataModel") {
                                bool isService = s->GetParent() && s->GetParent()->ClassName == "DataModel";
                                if (!isService) {
                                    g_Undo.PushDelete(s, s->GetParent());
                                }
                            }
                        }
                        g_Editor.DeleteSelected();
                        g_SelectedInstance = nullptr;
                        MarkDirty();
                    }
                    if (ctrl && keys[SDL_SCANCODE_S] && !keys[SDL_SCANCODE_LSHIFT]) {
                        DoSaveProper();
                    }
                    if (ctrl && keys[SDL_SCANCODE_O]) {
                        DoLoad();
                    }
                }
            }

            // ── Global Camera Look in Standalone Mode (No ImGui) ──
            if (standaloneMode) {
                int rx, ry;
                Uint32 mouseState = SDL_GetRelativeMouseState(&rx, &ry);
                
                // Right-click drag to rotate
                if (mouseState & SDL_BUTTON(SDL_BUTTON_RIGHT)) {
                    g_Camera.Yaw -= rx * 0.3f;
                    g_Camera.Pitch += ry * 0.3f;
                    g_Camera.Pitch = (std::max)(-89.0f, (std::min)(g_Camera.Pitch, 89.0f));
                }
                // Middle-click drag to pan
                if (mouseState & SDL_BUTTON(SDL_BUTTON_MIDDLE)) {
                    float right[3], up[3];
                    g_Camera.GetRightVector(right);
                    g_Camera.GetUpVector(up);
                    float speed = g_Camera.Distance * 0.003f;
                    g_Camera.Target.X -= (right[0] * rx - up[0] * ry) * speed;
                    g_Camera.Target.Y -= (right[1] * rx - up[1] * ry) * speed;
                    g_Camera.Target.Z -= (right[2] * rx - up[2] * ry) * speed;
                }
            }

            if (!standaloneMode && g_ScriptEditor.activeTab == 0 && g_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered() && !ImGuizmo::IsOver() && !g_HoveredUI) {
                Instance* workspace = g_GameRoot->FindFirstChild("Workspace");
                Instance* hit = g_Editor.Raycast(
                    io.MousePos.x, io.MousePos.y,
                    g_ViewportScreenPos.x, g_ViewportScreenPos.y,
                    g_ViewportSize.x, g_ViewportSize.y,
                    g_Camera, workspace ? workspace : g_GameRoot
                );

                // Respect modifier keys for multi-selection (same as Explorer tree)
                ImGuiIO& sio = ImGui::GetIO();
                if (sio.KeyShift && g_Editor.Selected && hit) {
                    g_Editor.SelectRange(g_Editor.Selected, hit, g_GameRoot);
                } else if (sio.KeyCtrl && hit) {
                    g_Editor.AddToSelection(hit);
                } else {
                    g_Editor.Select(hit);
                }
                g_SelectedInstance = g_Editor.Selected;
            }

            // ── Script Auto-Save (every 30 seconds) ──
            {
                float now = (float)SDL_GetTicks() / 1000.0f;
                if (now - g_LastScriptAutoSave > SCRIPT_AUTOSAVE_INTERVAL) {
                    g_ScriptEditor.SaveAll();
                    g_LastScriptAutoSave = now;
                }
            }

            // ── DataModel snapshot for autocomplete (every 1s) ──
            {
                float now = (float)SDL_GetTicks() / 1000.0f;
                if (now - g_LastDMSnapshot > DM_SNAPSHOT_INTERVAL) {
                    Autocomplete::SnapshotDataModel(g_GameRoot);
                    g_LastDMSnapshot = now;
                }
            }

            // ── Place Auto-Save (every 10 minutes) ──
            {
                float now = (float)SDL_GetTicks() / 1000.0f;
                if (g_LastPlaceAutoSave == 0.0f) g_LastPlaceAutoSave = now; // Initialize on first frame
                if (now - g_LastPlaceAutoSave > PLACE_AUTOSAVE_INTERVAL) {
                    DoAutosave(false);
                    g_LastPlaceAutoSave = now;
                }
            }

            if (g_IsRunning) {
                float now = (float)SDL_GetTicks() / 1000.0f;
                float dt = now - g_LastFrameTime;
                g_LastFrameTime = now;

                // FPS counter (Phase 10d)
                g_FrameCount++;
                g_FPSTimer += dt;
                if (g_FPSTimer >= 1.0f) {
                    g_FPS = (float)g_FrameCount / g_FPSTimer;
                    g_FrameCount = 0;
                    g_FPSTimer = 0.0f;
                }

                if (dt > 0.05f) dt = 0.05f;

                // Register workspace parts with Jolt (excluding player character parts)
                {
                    Instance* ws = g_GameRoot->FindFirstChild("Workspace");
                    if (ws) {
                        for (auto* desc : ws->GetDescendants()) {
                            if (desc->IsA("Part") && !desc->IsDestroyed()) {
                                // Check if this part is a cosmetic character limb
                                bool isCosmeticLimb = false;
                                Instance* p = desc->GetParent();
                                while (p) {
                                    if (p->ClassName == "Model" && p->Name == "Dev") {
                                        if (desc->Name != "HumanoidRootPart") {
                                            isCosmeticLimb = true;
                                        }
                                        break;
                                    }
                                    p = p->GetParent();
                                }
                                if (isCosmeticLimb) continue; // Skip cosmetic limbs, but register HRP!

                                g_JoltPhysics.RegisterPart(desc);
                            }
                        }
                    }
                }
                g_JoltPhysics.Step(dt);
                UpdateCharacterController(dt);
                LuauBridge::FireHeartbeat(dt);
                g_Scheduler.Update(now);
                g_UIS.FireCallbacks(g_LuaState);
                g_TweenService.Update(dt);
            }

            // ── Begin ImGui Frame (Only in Editor Mode) ──
            if (!standaloneMode) {
                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplSDL2_NewFrame();
                ImGui::NewFrame();
                ImGuizmo::BeginFrame();
                ImGui::DockSpaceOverViewport(ImGui::GetMainViewport()->ID, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
            }

            // ── Render ImGui Panels (Only in Editor Mode) ──
            if (!standaloneMode) {
                // ── Menu Bar ──
                if (ImGui::BeginMainMenuBar()) {
                    if (ImGui::BeginMenu("File")) {
                        ImGui::MenuItem("New Place", "Ctrl+N");
                        if (ImGui::MenuItem("Open Place...", "Ctrl+O")) DoLoad();
                        ImGui::Separator();
                        if (ImGui::MenuItem("Save", "Ctrl+S")) DoSaveProper();
                        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) DoSaveAs();
                        ImGui::Separator();
                        if (ImGui::MenuItem("Build", "F7")) DoBuild();
                        ImGui::Separator();
                        if (ImGui::MenuItem("Exit", "Alt+F4")) running = false;
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Edit")) {
                        ImGui::MenuItem("Undo", "Ctrl+Z");
                        ImGui::MenuItem("Redo", "Ctrl+Y");
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("View")) {
                        ImGui::MenuItem("Explorer", nullptr, &showExplorer);
                        ImGui::MenuItem("Properties", nullptr, &showProperties);
                        ImGui::MenuItem("Output", nullptr, &showOutput);
                        ImGui::MenuItem("Viewport", nullptr, &showViewport);
                        ImGui::MenuItem("Tools", nullptr, &showTools);
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Play")) {
                        if (!g_IsRunning) {
                            if (ImGui::MenuItem("Run", "F5")) DoPlay();
                        } else {
                            if (ImGui::MenuItem("Stop", "Shift+F5")) DoStop();
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Help")) {
                        ImGui::MenuItem("About Wavey Studio");
                        ImGui::EndMenu();
                    }
                    ImGui::EndMainMenuBar();
                }

                // ── Explorer ──
                if (showExplorer) {
                    ImGui::Begin("Explorer", nullptr);
                    // Render each service as a root node (hiding the "game" DataModel wrapper)
                    for (auto* service : g_GameRoot->GetChildren()) {
                        if (!service->IsDestroyed()) {
                            RenderInstanceTree(service);
                        }
                    }
                    ImGui::End();
                }

                // ── Properties ──
                if (showProperties) {
                    ImGui::Begin("Properties", nullptr);

                    // Sync selection
                    if (g_Editor.Selected && g_Editor.Selected != g_SelectedInstance) {
                        g_SelectedInstance = g_Editor.Selected;
                    }

                    if (!g_Editor.SelectedList.empty()) {
                        // Show selection count if multi-select
                        int count = (int)g_Editor.SelectedList.size();
                        if (count > 1) {
                            ImGui::TextColored(ImVec4(0.75f, 0.5f, 1.0f, 1.0f),
                                "%d instances selected", count);
                            ImGui::Separator();
                        }

                        // Primary instance (last clicked)
                        Instance* primary = g_Editor.Selected;
                        if (!primary || primary->IsDestroyed()) {
                            ImGui::TextDisabled("Invalid selection.");
                        } else {
                            // ── Search bar ──
                            static char propertySearch[128] = "";
                            ImGui::SetNextItemWidth(-1);
                            ImGui::InputTextWithHint("##propsearch", "Search properties...",
                                propertySearch, sizeof(propertySearch));
                            std::string searchFilter = propertySearch;
                            std::transform(searchFilter.begin(), searchFilter.end(),
                                searchFilter.begin(), ::tolower);

                            ImGui::Separator();

                            auto matchesFilter = [&](const std::string& propName) -> bool {
                                if (searchFilter.empty()) return true;
                                std::string lc = propName;
                                std::transform(lc.begin(), lc.end(), lc.begin(), ::tolower);
                                return lc.find(searchFilter) != std::string::npos;
                            };

                            // Name (always first)
                            if (matchesFilter("Name")) {
                                char nameBuf[256];
                                // Show primary's name; if all selected share same name, keep it, else empty
                                bool nameMatches = true;
                                for (Instance* s : g_Editor.SelectedList) {
                                    if (s->Name != primary->Name) { nameMatches = false; break; }
                                }
                                strncpy_s(nameBuf, nameMatches ? primary->Name.c_str() : "",
                                    sizeof(nameBuf) - 1);
                                nameBuf[sizeof(nameBuf) - 1] = '\0';

                                ImGui::AlignTextToFramePadding();
                                ImGui::Text("Name"); ImGui::SameLine(140);
                                ImGui::SetNextItemWidth(-1);

                                if (g_FocusRename) {
                                    ImGui::SetKeyboardFocusHere();
                                    g_FocusRename = false;
                                }

                                if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf),
                                    ImGuiInputTextFlags_EnterReturnsTrue)) {
                                    for (Instance* s : g_Editor.SelectedList) {
                                        std::string oldName = s->Name;
                                        s->Name = nameBuf;
                                        if (oldName != s->Name) {
                                            g_Undo.PushRename(s, oldName, s->Name);
                                        }
                                    }
                                    MarkDirty();
                                }
                            }

                            // ClassName (always, read-only)
                            if (matchesFilter("ClassName")) {
                                bool classMatches = true;
                                for (Instance* s : g_Editor.SelectedList) {
                                    if (s->ClassName != primary->ClassName) { classMatches = false; break; }
                                }
                                ImGui::Text("ClassName: %s",
                                    classMatches ? primary->ClassName.c_str() : "<mixed>");
                            }

                            // Parent (always, read-only text)
                            if (matchesFilter("Parent")) {
                                bool parentMatches = true;
                                Instance* parent0 = primary->GetParent();
                                for (Instance* s : g_Editor.SelectedList) {
                                    if (s->GetParent() != parent0) { parentMatches = false; break; }
                                }
                                if (parentMatches) {
                                    ImGui::Text("Parent: %s", parent0 ? parent0->Name.c_str() : "nil");
                                } else {
                                    ImGui::Text("Parent: <mixed>");
                                }
                            }

                            ImGui::Separator();

                            // Collect union of all property names across all selected
                            std::set<std::string> allPropNames;
                            for (Instance* s : g_Editor.SelectedList) {
                                for (auto& n : s->GetPropertyNames()) allPropNames.insert(n);
                            }

                            for (auto& propName : allPropNames) {
                                if (!matchesFilter(propName)) continue;

                                // Find which selected instances own this property
                                std::vector<Instance*> owners;
                                for (Instance* s : g_Editor.SelectedList) {
                                    if (s->HasProperty(propName)) owners.push_back(s);
                                }
                                if (owners.empty()) continue;

                                // Use first owner as primary for display + editing
                                Instance* valPrimary = owners[0];
                                auto primaryVal = valPrimary->GetProperty(propName);

                                // Check if all OWNERS share the same value
                                bool allMatch = true;
                                for (Instance* s : owners) {
                                    if (!s->HasProperty(propName)) { allMatch = false; break; }
                                    auto sv = s->GetProperty(propName);
                                    if (sv.index() != primaryVal.index()) { allMatch = false; break; }
                                    
                                    if (std::holds_alternative<bool>(primaryVal)) {
                                        if (std::get<bool>(sv) != std::get<bool>(primaryVal)) { allMatch = false; break; }
                                    } else if (std::holds_alternative<int>(primaryVal)) {
                                        if (std::get<int>(sv) != std::get<int>(primaryVal)) { allMatch = false; break; }
                                    } else if (std::holds_alternative<double>(primaryVal)) {
                                        if (std::get<double>(sv) != std::get<double>(primaryVal)) { allMatch = false; break; }
                                    } else if (std::holds_alternative<std::string>(primaryVal)) {
                                        if (std::get<std::string>(sv) != std::get<std::string>(primaryVal)) { allMatch = false; break; }
                                    } else if (std::holds_alternative<Vector3>(primaryVal)) {
                                        Vector3 a = std::get<Vector3>(sv), b = std::get<Vector3>(primaryVal);
                                        if (a.X != b.X || a.Y != b.Y || a.Z != b.Z) { allMatch = false; break; }
                                    } else if (std::holds_alternative<Color3>(primaryVal)) {
                                        Color3 a = std::get<Color3>(sv), b = std::get<Color3>(primaryVal);
                                        if (a.R != b.R || a.G != b.G || a.B != b.B) { allMatch = false; break; }
                                    }
                                }

                                if (allMatch) {
                                    // Show the shared value with normal editor
                                    // The DisplayProperty applies changes to all selected
                                    ImGui::PushID(propName.c_str());
                                    ImGui::AlignTextToFramePadding();
                                    ImGui::Text("%s", propName.c_str());
                                    ImGui::SameLine(140);
                                    ImGui::SetNextItemWidth(-1);

                                    // Render editor and capture change
                                    bool changed = false;
                                    Instance::PropertyValue newVal;

                                    if (std::holds_alternative<bool>(primaryVal)) {
                                        bool v = std::get<bool>(primaryVal);
                                        if (ImGui::Checkbox("##v", &v)) { newVal = v; changed = true; }
                                    } else if (std::holds_alternative<int>(primaryVal)) {
                                        int v = std::get<int>(primaryVal);
                                        if (ImGui::DragInt("##v", &v)) { newVal = v; changed = true; }
                                    } else if (std::holds_alternative<double>(primaryVal)) {
                                        float v = (float)std::get<double>(primaryVal);
                                        bool is01 = (propName == "Transparency" || propName == "Reflectance" ||
                                                      propName == "BackgroundTransparency" || propName == "ImageTransparency");
                                        bool isTextTransp = (propName == "TextTransparency");
                                        float minV = -FLT_MAX, maxV = FLT_MAX;
                                        float speed = 0.1f;
                                        if (is01) { minV = 0.0f; maxV = 1.0f; speed = 0.01f; }
                                        else if (isTextTransp) { minV = -10.0f; maxV = 1.0f; speed = 0.05f; }
                                        if (ImGui::DragFloat("##v", &v, speed, minV, maxV)) {
                                            newVal = (double)v; changed = true;
                                        }
                                    } else if (std::holds_alternative<std::string>(primaryVal)) {
                                        std::string s = std::get<std::string>(primaryVal);
                                        if (propName == "Source") {
                                            std::string preview = s;
                                            for (auto& ch : preview) if (ch == '\n' || ch == '\r') ch = ' ';
                                            if (preview.length() > 20) preview = preview.substr(0, 20) + "...";
                                            if (preview.empty()) preview = "(empty)";
                                            ImGui::TextDisabled("%s", preview.c_str());
                                        } else if (propName == "Shape") {
                                            // Dropdown for Part shapes
                                            static const char* shapes[] = { "Block", "Ball", "Cylinder", "Wedge" };
                                            int current = 0;
                                            for (int i = 0; i < 4; i++) {
                                                if (s == shapes[i]) { current = i; break; }
                                            }
                                            if (ImGui::Combo("##shape", &current, shapes, 4)) {
                                                newVal = std::string(shapes[current]); changed = true;
                                            }
                                        } else if (propName == "Material") {
                                            static const char* materials[] = {
                                                "Plastic", "Wood", "Metal", "Concrete", "Grass",
                                                "Brick", "Ice", "Glass", "Fabric", "Neon"
                                            };
                                            int current = 0;
                                            for (int i = 0; i < 10; i++) {
                                                if (s == materials[i]) { current = i; break; }
                                            }
                                            if (ImGui::Combo("##material", &current, materials, 10)) {
                                                newVal = std::string(materials[current]); changed = true;
                                            }
                                        } else if (propName == "Font") {
                                            static const char* fonts[] = { "Roboto", "RobotoBold", "OpenSans", "JetBrainsMono" };
                                            int current = 0;
                                            for (int i = 0; i < 4; i++) {
                                                if (s == fonts[i]) { current = i; break; }
                                            }
                                            if (ImGui::Combo("##font", &current, fonts, 4)) {
                                                newVal = std::string(fonts[current]); changed = true;
                                            }
                                        } else if (propName == "CameraType") {
                                            static const char* camTypes[] = { "Fixed", "Custom", "Scriptable", "Classic" };
                                            int current = 1; // Default = Custom
                                            for (int i = 0; i < 4; i++) {
                                                if (s == camTypes[i]) { current = i; break; }
                                            }
                                            if (ImGui::Combo("##camtype", &current, camTypes, 4)) {
                                                newVal = std::string(camTypes[current]); changed = true;
                                            }
                                        } else if (propName == "TextXAlignment") {
                                            static const char* aligns[] = { "Left", "Center", "Right" };
                                            int current = 1;
                                            for (int i = 0; i < 3; i++) {
                                                if (s == aligns[i]) { current = i; break; }
                                            }
                                            if (ImGui::Combo("##xalign", &current, aligns, 3)) {
                                                newVal = std::string(aligns[current]); changed = true;
                                            }
                                        } else if (propName == "TextYAlignment") {
                                            static const char* aligns[] = { "Top", "Center", "Bottom" };
                                            int current = 1;
                                            for (int i = 0; i < 3; i++) {
                                                if (s == aligns[i]) { current = i; break; }
                                            }
                                            if (ImGui::Combo("##yalign", &current, aligns, 3)) {
                                                newVal = std::string(aligns[current]); changed = true;
                                            }
                                        } else {
                                            char buf[512];
                                            strncpy_s(buf, s.c_str(), sizeof(buf) - 1);
                                            buf[sizeof(buf) - 1] = '\0';
                                            if (ImGui::InputText("##v", buf, sizeof(buf),
                                                ImGuiInputTextFlags_EnterReturnsTrue)) {
                                                newVal = std::string(buf); changed = true;
                                            }
                                        }
                                    } else if (std::holds_alternative<Vector3>(primaryVal)) {
                                        Vector3 v = std::get<Vector3>(primaryVal);
                                        float arr[3] = { v.X, v.Y, v.Z };
                                        if (ImGui::DragFloat3("##v", arr, 0.1f)) {
                                            newVal = Vector3(arr[0], arr[1], arr[2]); changed = true;
                                        }
                                    } else if (std::holds_alternative<Color3>(primaryVal)) {
                                        Color3 c = std::get<Color3>(primaryVal);
                                        float arr[3] = { c.R, c.G, c.B };
                                        if (ImGui::ColorEdit3("##v", arr)) {
                                            newVal = Color3(arr[0], arr[1], arr[2]); changed = true;
                                        }
                                    } else if (std::holds_alternative<CFrame>(primaryVal)) {
                                        CFrame cf = std::get<CFrame>(primaryVal);
                                        float arr[3] = { cf.Position.X, cf.Position.Y, cf.Position.Z };
                                        if (ImGui::DragFloat3("##v", arr, 0.1f)) {
                                            cf.Position = Vector3(arr[0], arr[1], arr[2]);
                                            newVal = cf; changed = true;
                                        }
                                    } else if (std::holds_alternative<UDim>(primaryVal)) {
                                        UDim u = std::get<UDim>(primaryVal);
                                        float scale = u.Scale;
                                        int offset = u.Offset;
                                        ImGui::Text("Scale"); ImGui::SameLine();
                                        ImGui::SetNextItemWidth(60);
                                        if (ImGui::DragFloat("##sc", &scale, 0.01f)) { u.Scale = scale; newVal = u; changed = true; }
                                        ImGui::SameLine();
                                        ImGui::Text("Offset"); ImGui::SameLine();
                                        ImGui::SetNextItemWidth(60);
                                        if (ImGui::DragInt("##off", &offset, 1)) { u.Offset = offset; newVal = u; changed = true; }
                                    } else if (std::holds_alternative<UDim2>(primaryVal)) {
                                        UDim2 u2 = std::get<UDim2>(primaryVal);
                                        float xs = u2.X.Scale, ys = u2.Y.Scale;
                                        int xo = u2.X.Offset, yo = u2.Y.Offset;

                                        // Render `{XScale, XOffset}, {YScale, YOffset}` in a compact side-by-side grid
                                        ImGui::Text("X:"); ImGui::SameLine();
                                        ImGui::SetNextItemWidth(50);
                                        if (ImGui::DragFloat("##xs", &xs, 0.01f)) { u2.X.Scale = xs; newVal = u2; changed = true; }
                                        ImGui::SameLine();
                                        ImGui::SetNextItemWidth(50);
                                        if (ImGui::DragInt("##xo", &xo, 1)) { u2.X.Offset = xo; newVal = u2; changed = true; }

                                        ImGui::SameLine();
                                        ImGui::Text("Y:"); ImGui::SameLine();
                                        ImGui::SetNextItemWidth(50);
                                        if (ImGui::DragFloat("##ys", &ys, 0.01f)) { u2.Y.Scale = ys; newVal = u2; changed = true; }
                                        ImGui::SameLine();
                                        ImGui::SetNextItemWidth(50);
                                        if (ImGui::DragInt("##yo", &yo, 1)) { u2.Y.Offset = yo; newVal = u2; changed = true; }
                                    } else if (std::holds_alternative<Instance*>(primaryVal)) {
                                        Instance* ref = std::get<Instance*>(primaryVal);
                                        
                                        bool isThisPicking = (g_PickingHostInstance == primary && g_PickingPropertyName == propName);

                                        if (isThisPicking) {
                                            ImGui::Button("...", ImVec2(ImGui::GetContentRegionAvail().x, 0));
                                        } else {
                                            // Display target name
                                            std::string btnLabel = ref ? ref->Name : "nil";
                                            btnLabel += "##picker";
                                            
                                            if (ImGui::Button(btnLabel.c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 24.0f, 0))) {
                                                // Left-click: Enter pick mode
                                                g_PickingHostInstance = primary;
                                                g_PickingPropertyName = propName;
                                            }
                                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to pick instance in Explorer\nRight-click for more options");

                                            // Right-click context menu on the main button
                                            if (ImGui::BeginPopupContextItem("##refCtx")) {
                                                if (ImGui::MenuItem("GoTo Instance", nullptr, false, ref != nullptr)) {
                                                    if (ref) {
                                                        g_Editor.Select(ref);
                                                        g_SelectedInstance = ref;
                                                    }
                                                }
                                                ImGui::Separator();
                                                if (ImGui::MenuItem("Clear")) {
                                                    for (Instance* s : owners) {
                                                        if (s->HasProperty(propName)) {
                                                            auto oldV = s->GetProperty(propName);
                                                            s->SetProperty(propName, (Instance*)nullptr);
                                                            g_Undo.PushPropertyChange(s, propName, oldV, Instance::PropertyValue{(Instance*)nullptr});
                                                        }
                                                    }
                                                    MarkDirty();
                                                }
                                                ImGui::EndPopup();
                                            }
                                        }
                                    } else {
                                        ImGui::TextDisabled("<unsupported>");
                                    }

                                    // Apply the live change immediately but defer undo-push until drag ends
                                    static std::unordered_map<Instance*, std::unordered_map<std::string, Instance::PropertyValue>> dragStartValues;

                                    // Capture start values when the user first activates this widget
                                    if (ImGui::IsItemActivated()) {
                                        for (Instance* s : owners) {
                                            if (s->HasProperty(propName)) {
                                                dragStartValues[s][propName] = s->GetProperty(propName);
                                            }
                                        }
                                    }

                                    if (changed) {
                                        for (Instance* s : owners) {
                                            s->SetProperty(propName, newVal);
                                        }
                                        MarkDirty();
                                    }

                                    // On release, push ONE undo entry per instance using the captured start value
                                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                                        for (Instance* s : owners) {
                                            auto it = dragStartValues.find(s);
                                            if (it != dragStartValues.end()) {
                                                auto propIt = it->second.find(propName);
                                                if (propIt != it->second.end()) {
                                                    Instance::PropertyValue currentVal = s->GetProperty(propName);
                                                    g_Undo.PushPropertyChange(s, propName, propIt->second, currentVal);
                                                }
                                            }
                                        }
                                        dragStartValues.clear();
                                    }

                                    ImGui::PopID();
                                } else {
                                    // Mixed values: show property name with empty/placeholder
                                    ImGui::PushID(propName.c_str());
                                    ImGui::AlignTextToFramePadding();
                                    ImGui::Text("%s", propName.c_str());
                                    ImGui::SameLine(140);
                                    ImGui::SetNextItemWidth(-1);

                                    // Click-to-reveal: show the primary's value if user clicks
                                    char placeholder[64];
                                    snprintf(placeholder, sizeof(placeholder), "<mixed - click to edit as %s>", primary->Name.c_str());
                                    char empty[1] = { '\0' };
                                    if (ImGui::InputTextWithHint("##mixed", placeholder, empty, 0,
                                        ImGuiInputTextFlags_EnterReturnsTrue)) {
                                        // User committed an empty edit — do nothing
                                    }
                                    // If user clicks the mixed field, show primary's value in a tooltip
                                    if (ImGui::IsItemHovered()) {
                                        ImGui::SetTooltip("Mixed values. Click and edit any single instance to unify.");
                                    }

                                    ImGui::PopID();
                                }
                            }
                        }
                    } else {
                        //ImGui::TextDisabled("Select an instance in the Explorer or Scene.");
                    }
                    ImGui::End();
                }

                // ── Output ──
                if (showOutput) {
                    ImGui::Begin("Output", nullptr);
                    if (ImGui::Button("Clear")) g_OutputLog.clear();
                    ImGui::SameLine();
                    if (ImGui::Button("Copy")) {
                        std::string fullLog;
                        for (const auto& line : g_OutputLog) { fullLog += line; fullLog += '\n'; }
                        ImGui::SetClipboardText(fullLog.c_str());
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%zu lines)", g_OutputLog.size());
                    ImGui::Separator();
                    ImGui::BeginChild("OutputScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

                    if (ImGui::BeginPopupContextWindow("OutputCtx")) {
                        if (ImGui::MenuItem("Copy All")) {
                            std::string fullLog;
                            for (const auto& line : g_OutputLog) { fullLog += line; fullLog += '\n'; }
                            ImGui::SetClipboardText(fullLog.c_str());
                        }
                        ImGui::EndPopup();
                    }

                    for (const auto& line : g_OutputLog) {
                        if (line.find("ERROR") != std::string::npos)
                            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", line.c_str());
                        else if (line.find("[Build]") != std::string::npos)
                            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1, 1), "%s", line.c_str());
                        else if (line.find("[Autosave]") != std::string::npos)
                            ImGui::TextColored(ImVec4(0.75f, 0.5f, 1.0f, 1), "%s", line.c_str());
                        else if (line.find("[Save]") != std::string::npos || line.find("[Load]") != std::string::npos)
                            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1), "%s", line.c_str());
                        else
                            ImGui::TextUnformatted(line.c_str());
                    }
                    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                        ImGui::SetScrollHereY(1.0f);
                    ImGui::EndChild();
                    ImGui::End();
                }

            // ── Viewport + Script Tabs (Guaranteed, Debug-Equipped Version) ──
            if (showViewport) {
                ImGui::Begin("Viewport", nullptr);

                if (ImGui::BeginTabBar("ViewportTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs)) {

                    // ── Tab 0: 3D Viewport ──
                    bool viewportOpen = true;
                    ImGuiTabItemFlags sceneTabFlags = 0;
                    if (g_ScriptEditor.forceFocusTab == 0) {
                        sceneTabFlags |= ImGuiTabItemFlags_SetSelected;
                        g_ScriptEditor.forceFocusTab = -1;
                    }

                    if (ImGui::BeginTabItem("Scene", &viewportOpen, sceneTabFlags)) {
                        if (g_ScriptEditor.lastActiveTab != 0) {
                            g_ScriptEditor.SaveAll();
                            g_ScriptEditor.lastActiveTab = 0;
                        }
                        g_ScriptEditor.activeTab = 0;
                        g_ViewportHovered = ImGui::IsWindowHovered();
                        g_ViewportSize = ImGui::GetContentRegionAvail();
                        g_ViewportScreenPos = ImGui::GetCursorScreenPos();

                        // Camera controls
                        if (g_ViewportHovered) {
                            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                                g_Camera.Yaw -= io.MouseDelta.x * 0.3f;
                                g_Camera.Pitch += io.MouseDelta.y * 0.3f;
                                g_Camera.Pitch = (std::max)(-89.0f, (std::min)(g_Camera.Pitch, 89.0f));
                            }
                            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                                float right[3], up[3];
                                g_Camera.GetRightVector(right);
                                g_Camera.GetUpVector(up);
                                float speed = g_Camera.Distance * 0.003f;
                                g_Camera.Target.X -= (right[0] * io.MouseDelta.x - up[0] * io.MouseDelta.y) * speed;
                                g_Camera.Target.Y -= (right[1] * io.MouseDelta.x - up[1] * io.MouseDelta.y) * speed;
                                g_Camera.Target.Z -= (right[2] * io.MouseDelta.x - up[2] * io.MouseDelta.y) * speed;
                            }
                            if (io.MouseWheel != 0) {
                                g_Camera.Distance -= io.MouseWheel * 3.0f;
                                g_Camera.Distance = (std::max)(1.0f, (std::min)(g_Camera.Distance, 500.0f));
                            }
                        }

                        if (g_ViewportSize.x > 10 && g_ViewportSize.y > 10) {
                            // Render 3D Scene FBO
                            ImGui::Image((ImTextureID)(intptr_t)g_Renderer.GetSceneTexture(), g_ViewportSize, ImVec2(0, 1), ImVec2(1, 0));

                            // ── 3D Gizmo Manipulation ──
                            if (g_SelectedInstance && g_SelectedInstance->IsA("Part") && !g_SelectedInstance->IsDestroyed() && g_CurrentTool != EditorTool::Mouse) {
                                Instance* part = g_SelectedInstance;

                                ImGuizmo::SetDrawlist();
                                ImGuizmo::SetRect(g_ViewportScreenPos.x, g_ViewportScreenPos.y, g_ViewportSize.x, g_ViewportSize.y);

                                float view[16], proj[16];
                                g_Camera.GetViewMatrix(view);
                                g_Camera.GetProjectionMatrix(g_ViewportSize.x / g_ViewportSize.y, proj);

                                ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
                                if (g_CurrentTool == EditorTool::Move) op = ImGuizmo::TRANSLATE;
                                else if (g_CurrentTool == EditorTool::Rotate) op = ImGuizmo::ROTATE;
                                else if (g_CurrentTool == EditorTool::Scale) op = ImGuizmo::SCALE;

                                // ── Static Caching of Drag Start State ──
                                static bool wasUsingGizmo = false;
                                static float gizmoStart[16];
                                static float gizmoCurrent[16];
                                static Vector3 startPos;
                                static Vector3 startOrient;
                                static Vector3 startSize;

                                Vector3 pos = GetProp<Vector3>(part, "Position", Vector3(0, 0, 0));
                                Vector3 orient = GetProp<Vector3>(part, "Orientation", Vector3(0, 0, 0));
                                Vector3 size = GetProp<Vector3>(part, "Size", Vector3(4, 1, 2));

                                bool isUsingNow = ImGuizmo::IsUsing();

                                if (!wasUsingGizmo && isUsingNow) {
                                    // DRAG STARTED! Capture frozen start state
                                    startPos = pos;
                                    startOrient = orient;
                                    startSize = size;

                                    BuildMatrix(pos, orient, size, gizmoStart);
                                    memcpy(gizmoCurrent, gizmoStart, sizeof(gizmoStart));
                                    wasUsingGizmo = true;

                                    std::cout << "[Gizmo Debug] Drag Start! Part: " << part->Name 
                                              << " | Size: (" << size.X << ", " << size.Y << ", " << size.Z << ")" << std::endl;
                                }

                                if (!isUsingNow) {
                                    // IDLE: Continuously sync the matrix from the part's properties
                                    BuildMatrix(pos, orient, size, gizmoCurrent);
                                    wasUsingGizmo = false;
                                }

                                // Apply Snapping (Phase 11d)
                                float snapValues[3] = { 0, 0, 0 };
                                float* snapPtr = nullptr;
                                if (op == ImGuizmo::TRANSLATE && g_SnapMoveEnabled) {
                                    snapValues[0] = snapValues[1] = snapValues[2] = g_SnapMoveValue;
                                    snapPtr = snapValues;
                                } else if (op == ImGuizmo::ROTATE && g_SnapRotateEnabled) {
                                    float radSnap = g_SnapRotateValue * 3.14159265f / 180.0f;
                                    snapValues[0] = snapValues[1] = snapValues[2] = radSnap;
                                    snapPtr = snapValues;
                                } else if (op == ImGuizmo::SCALE && g_SnapMoveEnabled) {
                                    // Scale snap must be divided by the starting size to prevent giant jumps!
                                    snapValues[0] = g_SnapMoveValue / (size.X > 0.001f ? size.X : 1.0f);
                                    snapValues[1] = g_SnapMoveValue / (size.Y > 0.001f ? size.Y : 1.0f);
                                    snapValues[2] = g_SnapMoveValue / (size.Z > 0.001f ? size.Z : 1.0f);
                                    snapPtr = snapValues;
                                }

                                // Let the user manipulate the gizmo
                                ImGuizmo::Manipulate(view, proj, op, ImGuizmo::LOCAL, gizmoCurrent, nullptr, snapPtr);

                                if (isUsingNow) {
                                    float outPos[3], outOrient[3], outSize[3];
                                    ImGuizmo::DecomposeMatrixToComponents(gizmoCurrent, outPos, outOrient, outSize);

                                    if (op == ImGuizmo::SCALE) {
                                        // Recover the true sign in case user scaled past zero into negatives
                                        float signX = (gizmoCurrent[0]*gizmoStart[0] + gizmoCurrent[1]*gizmoStart[1] + gizmoCurrent[2]*gizmoStart[2]) < 0.0f ? -1.0f : 1.0f;
                                        float signY = (gizmoCurrent[4]*gizmoStart[4] + gizmoCurrent[5]*gizmoStart[5] + gizmoCurrent[6]*gizmoStart[6]) < 0.0f ? -1.0f : 1.0f;
                                        float signZ = (gizmoCurrent[8]*gizmoStart[8] + gizmoCurrent[9]*gizmoStart[9] + gizmoCurrent[10]*gizmoStart[10]) < 0.0f ? -1.0f : 1.0f;

                                        float finalSizeX = outSize[0] * signX;
                                        float finalSizeY = outSize[1] * signY;
                                        float finalSizeZ = outSize[2] * signZ;

                                        part->SetProperty("Size", Vector3(finalSizeX, finalSizeY, finalSizeZ));
                                        
                                        // Keep position centered during scaling (two-sided scaling)
                                        part->SetProperty("Position", startPos);

                                        std::cout << "[Gizmo Debug] Scaling Live! outSize: (" 
                                                  << outSize[0] << ", " << outSize[1] << ", " << outSize[2] 
                                                  << ") | Signs: (" << signX << ", " << signY << ", " << signZ 
                                                  << ") | Final: (" << finalSizeX << ", " << finalSizeY << ", " << finalSizeZ << ")" << std::endl;
                                    } else {
                                        part->SetProperty("Position", Vector3(outPos[0], outPos[1], outPos[2]));
                                        part->SetProperty("Orientation", Vector3(outOrient[0], outOrient[1], outOrient[2]));
                                    }
                                    MarkDirty();
                                } else if (wasUsingGizmo) {
                                    // Drag ended: push undo states for actually-changed properties
                                    Vector3 endPos = GetProp<Vector3>(part, "Position", Vector3());
                                    Vector3 endOrient = GetProp<Vector3>(part, "Orientation", Vector3());
                                    Vector3 endSize = GetProp<Vector3>(part, "Size", Vector3(4,1,2));

                                    auto vec3Diff = [](const Vector3& a, const Vector3& b) {
                                        return fabsf(a.X - b.X) > 0.001f || fabsf(a.Y - b.Y) > 0.001f || fabsf(a.Z - b.Z) > 0.001f;
                                    };

                                    if (vec3Diff(startPos, endPos))
                                        g_Undo.PushPropertyChange(part, "Position", startPos, endPos);
                                    if (vec3Diff(startOrient, endOrient))
                                        g_Undo.PushPropertyChange(part, "Orientation", startOrient, endOrient);
                                    if (vec3Diff(startSize, endSize))
                                        g_Undo.PushPropertyChange(part, "Size", startSize, endSize);

                                    std::cout << "[Gizmo Debug] Drag End! Final Size: (" 
                                              << endSize.X << ", " << endSize.Y << ", " << endSize.Z << ")" << std::endl;
                                    wasUsingGizmo = false;
                                }
                            }
                        }
                        ImGui::EndTabItem(); // End Scene Tab
                    }

                    // ── Tabs 1+: Script Editors ──
                    for (int i = 0; i < (int)g_ScriptEditor.tabs.size(); i++) {
                        ScriptTab& tab = g_ScriptEditor.tabs[i];
                        if (!tab.script || tab.script->IsDestroyed()) {
                            g_ScriptEditor.CloseTab(i + 1);
                            i--;
                            continue;
                        }

                        std::string tabLabel = tab.script->Name + "##tab" + std::to_string(i);

                        bool tabOpen = true;
                        ImGuiTabItemFlags tabFlags = 0;
                        if (g_ScriptEditor.forceFocusTab == i + 1) {
                            tabFlags |= ImGuiTabItemFlags_SetSelected;
                            g_ScriptEditor.forceFocusTab = -1;
                        }

                        if (ImGui::BeginTabItem(tabLabel.c_str(), &tabOpen, tabFlags)) {
                            int thisTabIndex = i + 1;

                            if (g_ScriptEditor.lastActiveTab != thisTabIndex) {
                                g_ScriptEditor.SaveAll();
                                g_ScriptEditor.lastActiveTab = thisTabIndex;
                            }
                            g_ScriptEditor.activeTab = thisTabIndex;

                            if (tab.editor) {
                                // ── Define Shared Autocomplete Accept Logic ──
                                auto acceptSuggestion = [&](int idx) {
                                    if (idx < 0 || idx >= (int)tab.autocomplete.suggestions.size()) return;
                                    auto& entry = tab.autocomplete.suggestions[idx];
                                    auto cursor = tab.editor->GetCursorPosition();
                                    int prefixLen = (int)tab.autocomplete.currentPrefix.length();

                                    // 1. Delete the typed prefix
                                    if (prefixLen > 0 && cursor.mColumn >= prefixLen) {
                                        TextEditor::Coordinates selStart(cursor.mLine, cursor.mColumn - prefixLen);
                                        TextEditor::Coordinates selEnd(cursor.mLine, cursor.mColumn);
                                        tab.editor->SetSelection(selStart, selEnd);
                                        tab.editor->Delete();
                                    }

                                    // 2. Figure out what to insert and where cursor should go
                                    std::string insertion;
                                    int cursorOffset = 0;

                                        if (!entry.snippet.empty()) {
                                        insertion = entry.snippet;
                                        cursorOffset = entry.cursorOffset;
                                    } else if (entry.isFunction) {
                                        insertion = entry.name + "()";
                                        cursorOffset = 1;
                                    } else {
                                        insertion = entry.name;
                                        cursorOffset = 0;
                                    }

                                    // 3. Insert the text
                                    tab.editor->InsertText(insertion.c_str());

                                    // 4. Move cursor back by offset
                                    if (cursorOffset > 0) {
                                        auto nc = tab.editor->GetCursorPosition();
                                        tab.editor->SetCursorPosition(
                                            TextEditor::Coordinates(nc.mLine, nc.mColumn - cursorOffset)
                                        );
                                    }

                                    tab.autocomplete.Reset();
                                };

                                // Autocomplete Key Handling
                                bool blockEditorKeysForFrame = false;

                                if (tab.autocomplete.visible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
                                    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                                        tab.autocomplete.selectedIndex++;
                                        if (tab.autocomplete.selectedIndex >= (int)tab.autocomplete.suggestions.size())
                                            tab.autocomplete.selectedIndex = 0;
                                        blockEditorKeysForFrame = true;
                                    }
                                    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                                        tab.autocomplete.selectedIndex--;
                                        if (tab.autocomplete.selectedIndex < 0)
                                            tab.autocomplete.selectedIndex = (int)tab.autocomplete.suggestions.size() - 1;
                                        blockEditorKeysForFrame = true;
                                    }
                                    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                                        tab.autocomplete.Reset();
                                        blockEditorKeysForFrame = true;
                                    }

                                    if (ImGui::IsKeyPressed(ImGuiKey_Tab) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                                        acceptSuggestion(tab.autocomplete.selectedIndex);
                                        blockEditorKeysForFrame = true;
                                        ImGui::GetIO().InputQueueCharacters.clear();
                                    }
                                }

                                tab.editor->SetHandleKeyboardInputs(!blockEditorKeysForFrame);

                                // Auto-pair brackets and quotes
                                if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
                                    ImGuiIO& eio = ImGui::GetIO();
                                    for (ImWchar c : eio.InputQueueCharacters) {
                                        const char* pair = nullptr;
                                        switch (c) {
                                            case '(': pair = ")"; break;
                                            case '[': pair = "]"; break;
                                            case '{': pair = "}"; break;
                                            case '"': pair = "\""; break;
                                            case '\'': pair = "'"; break;
                                        }
                                        if (pair) {
                                            auto cursor = tab.editor->GetCursorPosition();
                                            tab.editor->InsertText(pair);
                                            tab.editor->SetCursorPosition(cursor);
                                        }
                                    }
                                }

                                ImVec2 editorSize = ImGui::GetContentRegionAvail();
                                std::string editorId = "ScriptEditor##" + std::to_string(i);
                                tab.editor->Render(editorId.c_str(), editorSize, true);

                                // Autocomplete Popup
                                if (tab.editor->IsTextChanged()) {
                                    tab.autocomplete.RebuildLearnedWords(tab.editor->GetText());
                                }

                                {
                                    auto cursor = tab.editor->GetCursorPosition();
                                    std::string fullText = tab.editor->GetText();
                                    std::string prefix = "";
                                    std::string chainContext = "";

                                    std::vector<std::string> lines;
                                    std::string line;
                                    for (char c : fullText) {
                                        if (c == '\n') { lines.push_back(line); line.clear(); }
                                        else line += c;
                                    }
                                    lines.push_back(line);

                                    if (cursor.mLine < (int)lines.size()) {
                                        std::string& curLine = lines[cursor.mLine];
                                        int col = (std::min)(cursor.mColumn, (int)curLine.length());
                                        int start = col;
                                        while (start > 0 && (isalnum(curLine[start - 1]) || curLine[start - 1] == '_' || curLine[start - 1] == '.')) {
                                            start--;
                                        }
                                        if (start < col) {
                                            std::string fullChain = curLine.substr(start, col - start);
                                            size_t lastDot = fullChain.rfind('.');
                                            if (lastDot != std::string::npos) {
                                                chainContext = fullChain.substr(0, lastDot);
                                                prefix = fullChain.substr(lastDot + 1);
                                            } else {
                                                prefix = fullChain;
                                            }
                                        }
                                    }

                                    tab.autocomplete.Update(prefix, chainContext);
                                }

                                if (tab.autocomplete.visible) {
                                    ImVec2 editorMin = ImGui::GetItemRectMin();
                                    float charW = ImGui::CalcTextSize("M").x;
                                    float lineH = ImGui::GetTextLineHeightWithSpacing();
                                    auto cursor = tab.editor->GetCursorPosition();

                                    float popupX = editorMin.x + 60 + cursor.mColumn * charW;
                                    float popupY = editorMin.y + (cursor.mLine + 1) * lineH + 4;

                                    ImGui::SetNextWindowPos(ImVec2(popupX, popupY));
                                    ImGui::SetNextWindowSize(ImVec2(250, 0));
                                    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.15f, 0.97f));
                                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35f, 0.55f, 0.90f, 0.70f));
                                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));

                                    ImGuiWindowFlags popupFlags =
                                        ImGuiWindowFlags_NoTitleBar |
                                        ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoDocking |
                                        ImGuiWindowFlags_NoScrollbar |
                                        ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoNav;

                                    if (ImGui::Begin("##ACPopup", nullptr, popupFlags)) {
                                        for (int j = 0; j < (int)tab.autocomplete.suggestions.size(); j++) {
                                            auto& entry = tab.autocomplete.suggestions[j];
                                            bool isSelected = (j == tab.autocomplete.selectedIndex);

                                            if (isSelected) {
                                                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.45f, 0.80f, 1.0f));
                                            }

                                            std::string label = entry.name + (entry.isFunction ? "()" : "");
                                            if (ImGui::Selectable(label.c_str(), isSelected)) {
                                                acceptSuggestion(j);
                                            }

                                            if (isSelected) {
                                                ImGui::PopStyleColor();
                                                if (ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
                                            }
                                        }
                                    }
                                    ImGui::End();
                                    ImGui::PopStyleVar();
                                    ImGui::PopStyleColor(2);
                                }
                            }

                            ImGui::EndTabItem(); // End Script Tab
                        }

                        if (!tabOpen) {
                            g_ScriptEditor.CloseTab(i + 1);
                            i--;
                        }
                    }

                    ImGui::EndTabBar(); // End Tab Bar
                }

                ImGui::End(); // End Viewport Window
            }

                // ── Tools Panel (dockable like any other panel) ──
                if (showTools) {
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
                    if (ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {

                        // Helper lambda to draw a tool button (icon or text fallback)
                        auto drawToolButton = [&](const char* iconName, const char* fallbackLabel, bool active, bool enabled = true) -> bool {
                            bool clicked = false;
                            ImVec2 btnSize(32, 32);

                            if (active) {
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.45f, 0.80f, 1.00f));
                                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.55f, 0.90f, 1.00f));
                            }
                            if (!enabled) ImGui::BeginDisabled();

                            if (g_Icons.HasIcon(iconName)) {
                                ImGui::PushID(iconName);
                                clicked = ImGui::ImageButton("##b",
                                    (ImTextureID)(intptr_t)g_Icons.GetIcon(iconName),
                                    ImVec2(20, 20),
                                    ImVec2(0, 0), ImVec2(1, 1),
                                    ImVec4(0, 0, 0, 0),
                                    active ? ImVec4(1, 1, 1, 1) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
                                ImGui::PopID();
                            } else {
                                clicked = ImGui::Button(fallbackLabel, btnSize);
                            }

                            if (!enabled) ImGui::EndDisabled();
                            if (active) ImGui::PopStyleColor(2);
                            return clicked;
                        };

                        // ── 1. Play / Stop Simulation ──
                        if (!g_IsRunning) {
                            if (drawToolButton("Tool_Play", "P", false)) DoPlay();
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play Simulation (F5)");
                        } else {
                            if (drawToolButton("Tool_Stop", "S", true)) DoStop();
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop Simulation (Shift+F5)");
                        }

                        ImGui::SameLine();
                        ImGui::TextDisabled("|");
                        ImGui::SameLine();

                        // ── 2. Tool Selection ──
                        if (drawToolButton("Tool_Mouse", "Sel", g_CurrentTool == EditorTool::Mouse)) {
                            g_CurrentTool = EditorTool::Mouse;
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select Tool (1)");
                        ImGui::SameLine();

                        if (drawToolButton("Tool_Move", "Mov", g_CurrentTool == EditorTool::Move)) {
                            g_CurrentTool = EditorTool::Move;
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move Tool (2)");
                        ImGui::SameLine();

                        if (drawToolButton("Tool_Scale", "Scl", g_CurrentTool == EditorTool::Scale)) {
                            g_CurrentTool = EditorTool::Scale;
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale Tool (3)");
                        ImGui::SameLine();

                        if (drawToolButton("Tool_Rotate", "Rot", g_CurrentTool == EditorTool::Rotate)) {
                            g_CurrentTool = EditorTool::Rotate;
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate Tool (4)");

                        ImGui::SameLine();
                        ImGui::TextDisabled("|");
                        ImGui::SameLine();

                        // ── Snap controls ──
                        ImGui::Checkbox("##snapmove", &g_SnapMoveEnabled);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable Move snap");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(60);
                        ImGui::DragFloat("##movevalue", &g_SnapMoveValue, 0.1f, 0.0f, 1000.0f, "%.2f st");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move snap distance (studs)");

                        ImGui::SameLine();
                        ImGui::Checkbox("##snaprot", &g_SnapRotateEnabled);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable Rotate snap");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(60);
                        ImGui::DragFloat("##rotvalue", &g_SnapRotateValue, 0.5f, 0.0f, 360.0f, "%.1f deg");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate snap angle (degrees)");

                        ImGui::SameLine();
                        ImGui::TextDisabled("|");
                        ImGui::SameLine();

                        // ── 3. Part Quick-Spawner ──
                        if (drawToolButton("Tool_Part", "Part", false)) {
                            Instance* workspace = g_GameRoot->FindFirstChild("Workspace");
                            if (workspace) {
                                Instance* part = g_Editor.InsertObject("Part", workspace);
                                if (part) {
                                    part->SetProperty("Shape", std::string("Block"));
                                    g_Editor.Select(part);
                                    g_SelectedInstance = part;
                                    g_Undo.PushInsert(part);
                                    MarkDirty();
                                }
                            }
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Insert Default Part (Block)");

                        ImGui::SameLine(0, 2);
                        if (ImGui::ArrowButton("##partArrow", ImGuiDir_Down)) {
                            g_ShowPartDropdown = true;
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Choose Part Shape");

                        if (g_ShowPartDropdown) {
                            ImGui::OpenPopup("PartShapeMenu");
                            g_ShowPartDropdown = false;
                        }
                        if (ImGui::BeginPopup("PartShapeMenu")) {
                            for (int i = 0; i < g_PartShapesCount; i++) {
                                if (ImGui::MenuItem(g_PartShapes[i])) {
                                    Instance* workspace = g_GameRoot->FindFirstChild("Workspace");
                                    if (workspace) {
                                        Instance* part = g_Editor.InsertObject("Part", workspace);
                                        if (part) {
                                            part->SetProperty("Shape", std::string(g_PartShapes[i]));
                                            part->Name = g_PartShapes[i];
                                            g_Editor.Select(part);
                                            g_SelectedInstance = part;
                                            g_Undo.PushInsert(part);
                                            MarkDirty();
                                        }
                                    }
                                }
                            }
                            ImGui::EndPopup();
                        }
                    }
                    ImGui::End();
                    ImGui::PopStyleVar();
                }

                // ── Insert Object floating popup ──
                if (g_InsertTarget) {
                    RenderInsertPopup();
                }

                // ── Close Confirmation Prompt ──
                if (g_ShowClosePrompt) {
                    ImGui::OpenPopup("Save changes?");
                    g_ShowClosePrompt = false;
                }

                ImGui::SetNextWindowSize(ImVec2(350, 0));
                if (ImGui::BeginPopupModal("Save changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextWrapped("You have unsaved changes. Save before closing?");
                    ImGui::Spacing();

                    if (ImGui::Button("Yes", ImVec2(100, 0))) {
                        DoSaveProper();
                        if (!g_Dirty) { // Save succeeded
                            g_ConfirmedClose = true;
                            running = false;
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("No", ImVec2(100, 0))) {
                        g_ConfirmedClose = true;
                        running = false;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            }

            // ── 3D Render Pass (Shared between Editor and Player) ──
            {
                int vpW, vpH;
                if (standaloneMode) {
                    SDL_GL_GetDrawableSize(g_Window, &vpW, &vpH);
                } else {
                    vpW = (int)g_ViewportSize.x;
                    vpH = (int)g_ViewportSize.y;
                }

                if (vpW > 10 && vpH > 10) {
                    g_Renderer.ResizeFBO(vpW, vpH);
                    g_Renderer.BeginScene();

                    // Sync Camera properties bidirectionally
                    Instance* workspace = g_GameRoot->FindFirstChild("Workspace");
                    if (workspace) {
                        Instance* cam = workspace->FindFirstChild("Camera");
                        if (cam) {
                            // FieldOfView: Instance → Renderer
                            if (cam->HasProperty("FieldOfView")) {
                                auto fv = cam->GetProperty("FieldOfView");
                                if (std::holds_alternative<double>(fv))
                                    g_Camera.FOV = (float)std::get<double>(fv);
                                else if (std::holds_alternative<int>(fv))
                                    g_Camera.FOV = (float)std::get<int>(fv);
                            }
                            // Live camera tracking: Position + Orientation
                            Vector3 camPos = g_Camera.GetPosition();
                            cam->SetProperty("Position", camPos);
                            cam->SetProperty("Orientation", Vector3(g_Camera.Pitch, g_Camera.Yaw, 0));
                        }
                    }
                    
                    g_Renderer.SetCamera(g_Camera, (float)vpW / (float)vpH);

                    // Sync Camera properties bidirectionally
                    if (workspace) {
                        Instance* cam = workspace->FindFirstChild("Camera");
                        if (cam) {
                            // FieldOfView: Instance → Renderer
                            if (cam->HasProperty("FieldOfView")) {
                                auto fv = cam->GetProperty("FieldOfView");
                                if (std::holds_alternative<double>(fv))
                                    g_Camera.FOV = (float)std::get<double>(fv);
                                else if (std::holds_alternative<int>(fv))
                                    g_Camera.FOV = (float)std::get<int>(fv);
                            }
                            // Live camera tracking: Position + Orientation
                            Vector3 camPos = g_Camera.GetPosition();
                            cam->SetProperty("Position", camPos);
                            cam->SetProperty("Orientation", Vector3(g_Camera.Pitch, g_Camera.Yaw, 0));
                        }
                    }

                    // Collect only Workspace descendants for rendering
                    std::vector<Instance*> descendants;
                    if (workspace) {
                        descendants = workspace->GetDescendants();
                    } else {
                        descendants = g_GameRoot->GetDescendants();
                    }

                    // 4. Draw the parts (only 3D physical classes, skip 2D UI)
                    for (auto* inst : descendants) {
                        if (!inst->IsDestroyed() && inst->IsA("Part")) {
                            Vector3 pos = GetProp<Vector3>(inst, "Position", Vector3(0, 0, 0));
                            Vector3 size = GetProp<Vector3>(inst, "Size", Vector3(4, 1, 2));
                            Color3 color = GetProp<Color3>(inst, "Color", Color3(0.63f, 0.65f, 0.64f));
                            float transp = 0.0f;
                            if (inst->HasProperty("Transparency")) {
                                auto tv = inst->GetProperty("Transparency");
                                if (std::holds_alternative<double>(tv)) transp = (float)std::get<double>(tv);
                                else if (std::holds_alternative<int>(tv)) transp = (float)std::get<int>(tv);
                            }
                            
                            // If 100% transparent (like HRP), skip rendering completely to avoid depth-buffer glitches!
                            if (transp >= 1.0f) continue;

                            Vector3 orient = GetProp<Vector3>(inst, "Orientation", Vector3(0, 0, 0));
                            std::string shape = GetProp<std::string>(inst, "Shape", std::string("Block"));
                            g_Renderer.DrawPart(pos, size, orient, color, transp, shape);
                        }
                    }

                    // Draw wireframes for ALL selected parts (multi-selection support)
                    if (!standaloneMode) {
                        for (Instance* sel : g_Editor.SelectedList) {
                            if (!sel || sel->IsDestroyed() || !sel->IsA("Part")) continue;
                            Vector3 sPos = GetProp<Vector3>(sel, "Position", Vector3());
                            Vector3 sSize = GetProp<Vector3>(sel, "Size", Vector3(4,1,2));
                            Vector3 sOrient = GetProp<Vector3>(sel, "Orientation", Vector3(0, 0, 0));
                            g_Renderer.DrawWireframe(sPos, sSize, sOrient, Color3(0.2f, 0.6f, 1.0f));
                        }
                    }

                    // ── 2D UI Render Pass ──
                    // Renders ScreenGuis directly over the 3D scene in the viewport
                    g_Renderer.Begin2D(vpW, vpH);

                    Instance* starterGui = g_GameRoot->FindFirstChild("StarterGui");
                    if (starterGui) {
                        for (auto* child : starterGui->GetChildren()) {
                            if (child->ClassName == "ScreenGui" && !child->IsDestroyed()) {
                                bool enabled = GetProp<bool>(child, "Enabled", true);
                                if (enabled) {
                                    // ScreenGui roots take the full viewport resolution as parent coordinates
                                    RenderUIHierarchy(child, 0.0f, 0.0f, (float)vpW, (float)vpH);
                                }
                            }
                        }
                    }

                    // ── In-Game Console Overlay (F9) ──
                        if (g_ShowConsole && g_IsRunning) {
                        float consoleH = (float)vpH * 0.35f;
                        float consoleY = (float)vpH - consoleH;

                        // Semi-transparent black background
                        g_Renderer.DrawRect2D(0, consoleY, (float)vpW, consoleH, Color3(0, 0, 0), 0.3f);

                        // Title bar
                        g_Renderer.DrawRect2D(0, consoleY, (float)vpW, 28, Color3(0.1f, 0.1f, 0.15f), 0.1f);
                        g_Renderer.DrawText2D("Console", 10, consoleY + 5, 16, Color3(0.8f, 0.8f, 0.8f), 0.0f);

                        // Print recent log lines (bottom-up, newest at bottom)
                        int startIdx = (std::max)(0, (int)g_OutputLog.size() - g_ConsoleMaxLines);
                        float lineH = 18.0f;
                        float textY = consoleY + 32.0f;

                        for (int i = startIdx; i < (int)g_OutputLog.size(); i++) {
                            if (textY + lineH > (float)vpH) break;

                            Color3 lineColor(0.85f, 0.85f, 0.85f);
                            const auto& line = g_OutputLog[i];
                            if (line.find("ERROR") != std::string::npos)
                                lineColor = Color3(1.0f, 0.3f, 0.3f);
                            else if (line.find("[Play]") != std::string::npos || line.find("[Stop]") != std::string::npos)
                                lineColor = Color3(0.3f, 0.8f, 1.0f);
                            else if (line.find("[Autosave]") != std::string::npos)
                                lineColor = Color3(0.75f, 0.5f, 1.0f);
                            else if (line.find("[Save]") != std::string::npos || line.find("[Load]") != std::string::npos)
                                lineColor = Color3(0.4f, 1.0f, 0.4f);

                            g_Renderer.DrawText2D(line, 10.0f, textY, 15, lineColor, 0.0f);
                            textY += lineH;
                        }
                    }

                    // ── Debug HUD Overlay (F3, Play mode only) ──
                    if (g_ShowDebugHUD && g_IsRunning) {
                        float hudX = 10.0f;
                        float hudY = 10.0f;
                        float lineH = 22.0f;
                        float textSize = 16.0f;
                        Color3 hudColor(1.0f, 1.0f, 1.0f);
                        Color3 hudAccent(0.6f, 1.0f, 0.6f);
                        Color3 hudBg(0, 0, 0);

                        char buf[256];
                        float frameMs = (g_FPS > 0) ? (1000.0f / g_FPS) : 0.0f;

                        // Collect all stats first so we can size the background correctly
                        std::vector<std::pair<std::string, Color3>> lines;

                        snprintf(buf, sizeof(buf), "Wavey Studio v0.11");
                        lines.push_back({buf, hudAccent});

                        snprintf(buf, sizeof(buf), "FPS: %.1f  (%.2f ms/frame)", g_FPS, frameMs);
                        lines.push_back({buf, hudColor});

                        snprintf(buf, sizeof(buf), "Window: %d x %d", vpW, vpH);
                        lines.push_back({buf, hudColor});

                        lines.push_back({"", hudColor}); // Spacer

                        snprintf(buf, sizeof(buf), "Camera Pos: %.1f, %.1f, %.1f",
                            g_Camera.GetPosition().X, g_Camera.GetPosition().Y, g_Camera.GetPosition().Z);
                        lines.push_back({buf, hudColor});

                        snprintf(buf, sizeof(buf), "Camera Target: %.1f, %.1f, %.1f",
                            g_Camera.Target.X, g_Camera.Target.Y, g_Camera.Target.Z);
                        lines.push_back({buf, hudColor});

                        snprintf(buf, sizeof(buf), "Camera FOV: %.0f  |  Yaw: %.0f  Pitch: %.0f",
                            g_Camera.FOV, g_Camera.Yaw, g_Camera.Pitch);
                        lines.push_back({buf, hudColor});

                        lines.push_back({"", hudColor}); // Spacer

                        if (g_DevCharacter && !g_DevCharacter->IsDestroyed()) {
                            Vector3 pPos = GetProp<Vector3>(g_DevCharacter, "Position", Vector3());
                            snprintf(buf, sizeof(buf), "Player Pos: %.1f, %.1f, %.1f", pPos.X, pPos.Y, pPos.Z);
                            lines.push_back({buf, hudColor});

                            snprintf(buf, sizeof(buf), "Player Vel: %.1f, %.1f, %.1f",
                                g_CharVelocity.X, g_CharVelocity.Y, g_CharVelocity.Z);
                            lines.push_back({buf, hudColor});

                            snprintf(buf, sizeof(buf), "Grounded: %s", g_CharGrounded ? "YES" : "NO");
                            lines.push_back({buf, hudColor});
                        } else {
                            lines.push_back({"Player: N/A", Color3(1.0f, 0.5f, 0.5f)});
                        }

                        lines.push_back({"", hudColor}); // Spacer

                        // Engine stats
                        Instance* ws = g_GameRoot->FindFirstChild("Workspace");
                        int partCount = 0, totalInstances = 0;
                        if (ws) {
                            auto desc = ws->GetDescendants();
                            for (auto* d : desc) if (d->IsA("Part")) partCount++;
                        }
                        auto allDesc = g_GameRoot->GetDescendants();
                        totalInstances = (int)allDesc.size();

                        snprintf(buf, sizeof(buf), "Parts in Workspace: %d", partCount);
                        lines.push_back({buf, hudColor});

                        snprintf(buf, sizeof(buf), "Total Instances: %d", totalInstances);
                        lines.push_back({buf, hudColor});

                        lines.push_back({"", hudColor}); // Spacer

                        // Memory (Windows-specific, no external deps)
                        #ifdef _WIN32
                        PROCESS_MEMORY_COUNTERS pmc;
                        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
                            double memMB = (double)pmc.WorkingSetSize / (1024.0 * 1024.0);
                            snprintf(buf, sizeof(buf), "Memory: %.1f MB", memMB);
                            lines.push_back({buf, hudColor});
                        }
                        #endif

                        // Draw background sized for the content
                        float bgW = 360.0f;
                        float bgH = lines.size() * lineH + 10.0f;
                        g_Renderer.DrawRect2D(hudX - 6, hudY - 6, bgW, bgH, hudBg, 0.35f);

                        // Draw all lines
                        for (auto& [text, color] : lines) {
                            if (!text.empty()) {
                                g_Renderer.DrawText2D(text, hudX, hudY, textSize, color, 0.0f);
                            }
                            hudY += lineH;
                        }
                    }

                    g_Renderer.End2D();

                    // Blit active scene straight to window in standalone mode
                    if (standaloneMode) {
                        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                        glBlitFramebuffer(0, 0, vpW, vpH, 0, 0, vpW, vpH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                    }

                    g_Renderer.EndScene();
                }
            }

            // ── Render Presentation ──
            if (!standaloneMode) {
                ImGui::Render();
                int displayW, displayH;
                SDL_GL_GetDrawableSize(g_Window, &displayW, &displayH);
                glViewport(0, 0, displayW, displayH);
                glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                SDL_GL_SwapWindow(g_Window);
            } else {
                SDL_GL_SwapWindow(g_Window);
            }
        }

        // ── Cleanup ──
        lua_close(L);
        g_ScriptEditor.ClearAll();
        g_JoltPhysics.Shutdown();
        g_Renderer.Shutdown();
        g_Icons.Shutdown();
        g_Undo.Clear();
        delete g_GameRoot;
        if (!standaloneMode) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplSDL2_Shutdown();
            ImGui::DestroyContext();
        }
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(g_Window);
        SDL_Quit();
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << std::endl;
        std::cerr << "Fatal Error! Crash detected. Details: " << e.what() << std::endl;
        try {
            DoAutosave(true);
            std::cerr << "Emergency autosave written to autosaves folder." << std::endl;
        } catch (...) {
            std::cerr << "Failed to write emergency autosave." << std::endl;
        }
        std::cerr << "Press ENTER to close the window..." << std::endl;
        std::cin.get();
        return 1;
    }
    catch (...) {
        std::cerr << std::endl;
        std::cerr << "Fatal Error! Crash detected. Details: Unknown non-standard C++ exception" << std::endl;
        try {
            DoAutosave(true);
            std::cerr << "Emergency autosave written to autosaves folder." << std::endl;
        } catch (...) {
            std::cerr << "Failed to write emergency autosave." << std::endl;
        }
        std::cerr << "Press ENTER to close the window..." << std::endl;
        std::cin.get();
        return 1;
    }
}