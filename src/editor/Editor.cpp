#include "Editor.h"
#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>

// Global lookup vector for unresolved references
struct UnresolvedRef {
    Instance* host = nullptr;
    std::string propName;
    uint64_t targetUID = 0;
};
static std::vector<UnresolvedRef> g_UnresolvedRefs;

// ═══════════════════════════════════════════════════════
// Ray-AABB Intersection (slab method)
// ═══════════════════════════════════════════════════════
bool Editor::RayAABB(Vector3 origin, Vector3 dir, Vector3 bMin, Vector3 bMax, float& t) {
    float tmin = -1e30f, tmax = 1e30f;
    float o[3] = {origin.X, origin.Y, origin.Z};
    float d[3] = {dir.X, dir.Y, dir.Z};
    float mn[3] = {bMin.X, bMin.Y, bMin.Z};
    float mx[3] = {bMax.X, bMax.Y, bMax.Z};

    for (int i = 0; i < 3; i++) {
        if (fabsf(d[i]) < 1e-8f) {
            if (o[i] < mn[i] || o[i] > mx[i]) return false;
        } else {
            float t1 = (mn[i] - o[i]) / d[i];
            float t2 = (mx[i] - o[i]) / d[i];
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }
    t = tmin > 0 ? tmin : tmax;
    return t > 0;
}

// ═══════════════════════════════════════════════════════
// Mouse → World Ray (camera-based, no matrix inverse)
// This is much more reliable than the VP unproject method.
// ═══════════════════════════════════════════════════════
void Editor::RayFromMouse(float mx, float my, float vpW, float vpH, const Camera& cam, Vector3& origin, Vector3& dir) {
    // NDC: [-1, 1]
    float ndcX = (2.0f * mx / vpW) - 1.0f;
    float ndcY = 1.0f - (2.0f * my / vpH);

    // Camera basis vectors
    Vector3 pos = cam.GetPosition();
    Vector3 forward = {cam.Target.X - pos.X, cam.Target.Y - pos.Y, cam.Target.Z - pos.Z};
    float fLen = forward.Magnitude();
    if (fLen < 1e-6f) fLen = 1e-6f;
    forward = {forward.X / fLen, forward.Y / fLen, forward.Z / fLen};

    Vector3 worldUp = {0, 1, 0};
    Vector3 right = {
        forward.Y * worldUp.Z - forward.Z * worldUp.Y,
        forward.Z * worldUp.X - forward.X * worldUp.Z,
        forward.X * worldUp.Y - forward.Y * worldUp.X
    };
    float rLen = right.Magnitude();
    if (rLen < 1e-6f) rLen = 1e-6f;
    right = {right.X / rLen, right.Y / rLen, right.Z / rLen};

    Vector3 up = {
        right.Y * forward.Z - right.Z * forward.Y,
        right.Z * forward.X - right.X * forward.Z,
        right.X * forward.Y - right.Y * forward.X
    };

    float tanHalfFov = tanf(cam.FOV * 3.14159265f / 360.0f);
    float aspect = vpW / vpH;

    dir = {
        forward.X + right.X * ndcX * tanHalfFov * aspect + up.X * ndcY * tanHalfFov,
        forward.Y + right.Y * ndcX * tanHalfFov * aspect + up.Y * ndcY * tanHalfFov,
        forward.Z + right.Z * ndcX * tanHalfFov * aspect + up.Z * ndcY * tanHalfFov
    };
    float dLen = dir.Magnitude();
    if (dLen > 0) dir = {dir.X / dLen, dir.Y / dLen, dir.Z / dLen};
    origin = pos;
}

// ═══════════════════════════════════════════════════════
// Raycast: find closest Part under mouse
// ═══════════════════════════════════════════════════════
Instance* Editor::Raycast(float mouseX, float mouseY,
                          float vpX, float vpY, float vpW, float vpH,
                          const Camera& cam, Instance* root) {
    float localX = mouseX - vpX;
    float localY = mouseY - vpY;
    if (localX < 0 || localY < 0 || localX > vpW || localY > vpH)
        return nullptr;

    Vector3 origin, dir;
    RayFromMouse(localX, localY, vpW, vpH, cam, origin, dir);

    Instance* closest = nullptr;
    float closestT = 1e30f;

    auto descendants = root->GetDescendants();
    for (auto* inst : descendants) {
        if (!inst->IsA("Part") || inst->IsDestroyed()) continue;

        Vector3 pos = {0, 0, 0}, size = {4, 1, 2};
        if (inst->HasProperty("Position")) {
            auto v = inst->GetProperty("Position");
            if (std::holds_alternative<Vector3>(v)) pos = std::get<Vector3>(v);
        }
        if (inst->HasProperty("Size")) {
            auto v = inst->GetProperty("Size");
            if (std::holds_alternative<Vector3>(v)) size = std::get<Vector3>(v);
        }

        Vector3 bMin = {pos.X - size.X / 2, pos.Y - size.Y / 2, pos.Z - size.Z / 2};
        Vector3 bMax = {pos.X + size.X / 2, pos.Y + size.Y / 2, pos.Z + size.Z / 2};

        float t;
        if (RayAABB(origin, dir, bMin, bMax, t) && t < closestT) {
            closestT = t;
            closest = inst;
        }
    }
    return closest;
}

// ═══════════════════════════════════════════════════════
// Selection & Actions
// ═══════════════════════════════════════════════════════
void Editor::Select(Instance* inst) {
    SelectedList.clear();
    if (inst) SelectedList.push_back(inst);
    Selected = inst;
}

void Editor::AddToSelection(Instance* inst) {
    if (!inst) return;
    auto it = std::find(SelectedList.begin(), SelectedList.end(), inst);
    if (it != SelectedList.end()) {
        // Already selected → deselect
        SelectedList.erase(it);
        Selected = SelectedList.empty() ? nullptr : SelectedList.back();
    } else {
        SelectedList.push_back(inst);
        Selected = inst;
    }
}

void Editor::ClearSelection() {
    SelectedList.clear();
    Selected = nullptr;
}

bool Editor::IsSelected(Instance* inst) const {
    return std::find(SelectedList.begin(), SelectedList.end(), inst) != SelectedList.end();
}

// Helper for range selection: flatten tree depth-first
static void CollectDepthFirst(Instance* node, std::vector<Instance*>& out) {
    if (!node || node->IsDestroyed()) return;
    out.push_back(node);
    for (auto* child : node->GetChildren()) {
        if (!child->IsDestroyed())
            CollectDepthFirst(child, out);
    }
}

void Editor::SelectRange(Instance* from, Instance* to, Instance* root) {
    if (!from || !to || !root) return;

    std::vector<Instance*> flat;
    CollectDepthFirst(root, flat);

    auto itFrom = std::find(flat.begin(), flat.end(), from);
    auto itTo = std::find(flat.begin(), flat.end(), to);
    if (itFrom == flat.end() || itTo == flat.end()) return;

    if (itFrom > itTo) std::swap(itFrom, itTo);

    SelectedList.clear();
    for (auto it = itFrom; it <= itTo; ++it) {
        SelectedList.push_back(*it);
    }
    Selected = to;
}

void Editor::DeleteSelected() {
    for (Instance* inst : SelectedList) {
        if (!inst || inst->ClassName == "DataModel") continue;
        bool isService = inst->GetParent() && inst->GetParent()->ClassName == "DataModel";
        if (isService) continue;
        inst->Destroy();
    }
    SelectedList.clear();
    Selected = nullptr;
}

Instance* Editor::InsertObject(const std::string& className, Instance* parent) {
    if (!parent) return nullptr;
    Instance* inst = Instance::Create(className);
    inst->Name = className;
    inst->SetParent(parent);
    Selected = inst;
    std::cout << "[Wavey] Inserted: " << className << " into " << parent->Name << std::endl;
    return inst;
}

// ═══════════════════════════════════════════════════════
// Save Place (.wavey format)
// ═══════════════════════════════════════════════════════
void Editor::WriteNode(std::string& out, Instance* inst, int depth) {
    if (inst->IsDestroyed()) return;
    std::string indent(depth * 2, ' ');

    out += indent + "node " + inst->ClassName + " \"" + inst->Name + "\" " + std::to_string(inst->UID) + "\n";

    for (auto& name : inst->GetPropertyNames()) {
        auto val = inst->GetProperty(name);
        std::string propLine = indent + "  prop ";

        if (std::holds_alternative<bool>(val))
            propLine += "b " + name + " " + (std::get<bool>(val) ? "1" : "0");
        else if (std::holds_alternative<int>(val))
            propLine += "i " + name + " " + std::to_string(std::get<int>(val));
        else if (std::holds_alternative<double>(val))
            propLine += "d " + name + " " + std::to_string(std::get<double>(val));
        else if (std::holds_alternative<std::string>(val))
            propLine += "s " + name + " \"" + std::get<std::string>(val) + "\"";
        else if (std::holds_alternative<Vector3>(val)) {
            auto v = std::get<Vector3>(val);
            propLine += "v " + name + " " + std::to_string(v.X) + " " + std::to_string(v.Y) + " " + std::to_string(v.Z);
        }
        else if (std::holds_alternative<Color3>(val)) {
            auto c = std::get<Color3>(val);
            propLine += "c " + name + " " + std::to_string(c.R) + " " + std::to_string(c.G) + " " + std::to_string(c.B);
        }
        else if (std::holds_alternative<Instance*>(val)) {
            auto* ref = std::get<Instance*>(val);
            if (ref && !ref->IsDestroyed())
                propLine += "o " + name + " " + std::to_string(ref->UID);
            else
                propLine += "o " + name + " 0";
        }
        else if (std::holds_alternative<UDim>(val)) {
            auto u = std::get<UDim>(val);
            propLine += "m " + name + " " + std::to_string(u.Scale) + " " + std::to_string(u.Offset);
        }
        else if (std::holds_alternative<UDim2>(val)) {
            auto u2 = std::get<UDim2>(val);
            propLine += "u " + name + " " + std::to_string(u2.X.Scale) + " " + std::to_string(u2.X.Offset) + " " + std::to_string(u2.Y.Scale) + " " + std::to_string(u2.Y.Offset);
        }
        else continue;

        out += propLine + "\n";
    }

    for (auto* child : inst->GetChildren()) {
        WriteNode(out, child, depth + 1);
    }

    out += indent + "end\n";
}

bool Editor::SavePlace(const std::string& path, Instance* root) {
    std::string data = "WAVEY_PLACE_V1\n";
    for (auto* child : root->GetChildren()) {
        if (!child->IsDestroyed())
            WriteNode(data, child, 0);
    }

    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << data;
    file.close();
    std::cout << "[Wavey] Saved to: " << path << std::endl;
    return true;
}

// ═══════════════════════════════════════════════════════
// Load Place (.wavey format)
// ═══════════════════════════════════════════════════════
static std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

static std::vector<std::string> SplitTokens(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    bool inQuotes = false;
    for (char c : line) {
        if (c == '"') { inQuotes = !inQuotes; continue; }
        if ((c == ' ' || c == '\t') && !inQuotes) {
            if (!current.empty()) { tokens.push_back(current); current.clear(); }
        } else {
            current += c;
        }
    }
    if (!current.empty()) tokens.push_back(current);
    return tokens;
}

Instance* Editor::ParseNode(const std::vector<std::string>& lines, int& idx, Instance* parent) {
    if (idx >= (int)lines.size()) return nullptr;

    auto tokens = SplitTokens(lines[idx]);
    if (tokens.size() < 3 || tokens[0] != "node") return nullptr;

    std::string className = tokens[1];
    std::string name = tokens[2];
    
    uint64_t uid = 0;
    if (tokens.size() >= 4) {
        uid = std::stoull(tokens[3]);
    }
    
    idx++;

    Instance* inst = Instance::Create(className);
    inst->Name = name;
    if (uid != 0) inst->UID = uid; // Preserve saved UID
    if (parent) inst->SetParent(parent);

    while (idx < (int)lines.size()) {
        auto t = SplitTokens(lines[idx]);
        if (t.empty()) { idx++; continue; }

        if (t[0] == "end") {
            idx++;
            break;
        }
        else if (t[0] == "node") {
            ParseNode(lines, idx, inst);
        }
        else if (t[0] == "prop" && t.size() >= 4) {
            std::string type = t[1];
            std::string propName = t[2];

            if (type == "b")
                inst->SetProperty(propName, t[3] == "1");
            else if (type == "i")
                inst->SetProperty(propName, std::stoi(t[3]));
            else if (type == "d")
                inst->SetProperty(propName, std::stod(t[3]));
            else if (type == "s")
                inst->SetProperty(propName, t[3]);
            else if (type == "v" && t.size() >= 6)
                inst->SetProperty(propName, Vector3(std::stof(t[3]), std::stof(t[4]), std::stof(t[5])));
            else if (type == "c" && t.size() >= 6)
                inst->SetProperty(propName, Color3(std::stof(t[3]), std::stof(t[4]), std::stof(t[5])));
            else if (type == "f" && t.size() >= 6)
                inst->SetProperty(propName, CFrame(std::stof(t[3]), std::stof(t[4]), std::stof(t[5])));
            else if (type == "o" && t.size() >= 4) {
                uint64_t targetUID = std::stoull(t[3]);
                if (targetUID != 0) {
                    g_UnresolvedRefs.push_back({inst, propName, targetUID});
                } else {
                    inst->SetProperty(propName, (Instance*)nullptr);
                }
            }
            else if (type == "m" && t.size() >= 5) {
                inst->SetProperty(propName, UDim(std::stof(t[3]), std::stoi(t[4])));
            }
            else if (type == "u" && t.size() >= 7) {
                inst->SetProperty(propName, UDim2(
                    std::stof(t[3]), std::stoi(t[4]),
                    std::stof(t[5]), std::stoi(t[6])
                ));
            }

            idx++;
        }
        else {
            idx++;
        }
    }

    return inst;
}

bool Editor::LoadPlace(const std::string& path, Instance* root) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    // Properly clear ALL existing children (iterate a copy, destroy each)
    auto existingChildren = root->GetChildren();
    for (auto* child : existingChildren) {
        child->SetParent(nullptr);  // Remove from tree first
        child->Destroy();
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (!line.empty()) lines.push_back(line);
    }

    if (lines.empty() || lines[0] != "WAVEY_PLACE_V1") {
        std::cerr << "[Wavey] Invalid place file" << std::endl;
        return false;
    }

    g_UnresolvedRefs.clear();

    int idx = 1;
    while (idx < (int)lines.size()) {
        auto tokens = SplitTokens(lines[idx]);
        if (!tokens.empty() && tokens[0] == "node") {
            ParseNode(lines, idx, root);
        } else {
            idx++;
        }
    }

    // ── Second Pass: Resolve Object References ──
    int resolvedCount = 0;
    for (const auto& ref : g_UnresolvedRefs) {
        Instance* target = root->FindByUID(ref.targetUID);
        if (target) {
            ref.host->SetProperty(ref.propName, target);
            resolvedCount++;
        } else {
            ref.host->SetProperty(ref.propName, (Instance*)nullptr);
        }
    }
    g_UnresolvedRefs.clear();

    std::cout << "[Wavey] Loaded from: " << path << " (Resolved " << resolvedCount << " object references)" << std::endl;
    return true;
}