#pragma once
#include "../core/Instance.h"
#include "../core/Types.h"
#include "../engine/Camera.h"
#include <string>
#include <vector>

class Editor {
public:
    Instance* Selected = nullptr;              // Last-selected (primary) — for Properties panel
    std::vector<Instance*> SelectedList;        // All selected instances

    // ── Viewport Raycasting ──
    // Returns the closest Part under the mouse cursor, or nullptr
    Instance* Raycast(float mouseX, float mouseY,
                      float vpX, float vpY, float vpW, float vpH,
                      const Camera& cam, Instance* root);

    // ── Actions ──
    void Select(Instance* inst);                 // Single-select (clears list)
    void AddToSelection(Instance* inst);         // Ctrl+click: toggle add/remove
    void SelectRange(Instance* from, Instance* to, Instance* root); // Shift+click: range
    void ClearSelection();
    bool IsSelected(Instance* inst) const;
    void DeleteSelected();
    Instance* InsertObject(const std::string& className, Instance* parent);

    // ── Save / Load ──
    bool SavePlace(const std::string& path, Instance* root);
    bool LoadPlace(const std::string& path, Instance* root);

    // ── UI State ──
    bool showInsertDialog = false;
    char insertClassName[128] = "Part";
    char renameBuffer[256] = "";
    bool showRenameDialog = false;

private:
    // Math helpers
    bool InvertMatrix4(const float* m, float* out);
    bool RayAABB(Vector3 origin, Vector3 dir,
                 Vector3 boxMin, Vector3 boxMax, float& t);
    void RayFromMouse(float mx, float my, float vpW, float vpH,
                      const Camera& cam, Vector3& origin, Vector3& dir);

    // Save helpers
    void WriteNode(std::string& out, Instance* inst, int depth);
    Instance* ParseNode(const std::vector<std::string>& lines,
                        int& idx, Instance* parent);
};