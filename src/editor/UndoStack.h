#pragma once
#include "../core/Instance.h"
#include <string>
#include <vector>
#include <deque>

enum class UndoActionType {
    PropertyChange,
    Rename,
    Reparent,
    Insert,
    Delete
};

struct UndoAction {
    UndoActionType type;
    Instance* target = nullptr;
    std::string propertyName;
    Instance::PropertyValue oldValue;
    Instance::PropertyValue newValue;
    std::string oldName, newName;
    Instance* oldParent = nullptr;
    Instance* newParent = nullptr;
    Instance* deletedParent = nullptr;
    Instance* savedClone = nullptr; // For Delete: snapshot taken before destroying
};

class UndoStack {
public:
    static constexpr int MAX_UNDO = 30;

    void PushPropertyChange(Instance* inst, const std::string& prop, const Instance::PropertyValue& oldV, const Instance::PropertyValue& newV);
    void PushRename(Instance* inst, const std::string& oldName, const std::string& newName);
    void PushReparent(Instance* inst, Instance* oldParent, Instance* newParent);
    void PushInsert(Instance* inst);
    void PushDelete(Instance* inst, Instance* oldParent);

    bool Undo();
    bool Redo();
    void Clear();

private:
    std::deque<UndoAction> undoStack;
    std::deque<UndoAction> redoStack;
    void ClearRedo();
};

extern UndoStack g_Undo;