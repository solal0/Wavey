#include "UndoStack.h"
#include <iostream>

UndoStack g_Undo;

static void ApplyAction(const UndoAction& a, bool reverse) {
    switch (a.type) {
        case UndoActionType::PropertyChange:
            if (a.target && !a.target->IsDestroyed()) {
                a.target->SetProperty(a.propertyName, reverse ? a.oldValue : a.newValue);
            }
            break;
        case UndoActionType::Rename:
            if (a.target && !a.target->IsDestroyed()) {
                a.target->Name = reverse ? a.oldName : a.newName;
            }
            break;
        case UndoActionType::Reparent:
            if (a.target && !a.target->IsDestroyed()) {
                a.target->SetParent(reverse ? a.oldParent : a.newParent);
            }
            break;
        case UndoActionType::Insert:
            if (reverse) {
                // Undo insert = destroy the instance
                if (a.target && !a.target->IsDestroyed()) a.target->Destroy();
            }
            // Redo insert currently not supported (would need to resurrect)
            break;
        case UndoActionType::Delete:
            if (reverse) {
                // Undo delete = clone the saved snapshot and parent it back
                if (a.savedClone && a.deletedParent) {
                    Instance* restored = a.savedClone->Clone();
                    restored->SetParent(a.deletedParent);
                }
            } else {
                // Redo delete: the original target was already destroyed; nothing to do
                // (user can delete again if they want)
            }
            break;
    }
}

void UndoStack::ClearRedo() {
    redoStack.clear();
}

void UndoStack::PushPropertyChange(Instance* inst, const std::string& prop, const Instance::PropertyValue& oldV, const Instance::PropertyValue& newV) {
    UndoAction a;
    a.type = UndoActionType::PropertyChange;
    a.target = inst;
    a.propertyName = prop;
    a.oldValue = oldV;
    a.newValue = newV;
    undoStack.push_back(a);
    while ((int)undoStack.size() > MAX_UNDO) undoStack.pop_front();
    ClearRedo();
}

void UndoStack::PushRename(Instance* inst, const std::string& oldN, const std::string& newN) {
    UndoAction a;
    a.type = UndoActionType::Rename;
    a.target = inst;
    a.oldName = oldN;
    a.newName = newN;
    undoStack.push_back(a);
    while ((int)undoStack.size() > MAX_UNDO) undoStack.pop_front();
    ClearRedo();
}

void UndoStack::PushReparent(Instance* inst, Instance* oldP, Instance* newP) {
    UndoAction a;
    a.type = UndoActionType::Reparent;
    a.target = inst;
    a.oldParent = oldP;
    a.newParent = newP;
    undoStack.push_back(a);
    while ((int)undoStack.size() > MAX_UNDO) undoStack.pop_front();
    ClearRedo();
}

void UndoStack::PushInsert(Instance* inst) {
    UndoAction a;
    a.type = UndoActionType::Insert;
    a.target = inst;
    undoStack.push_back(a);
    while ((int)undoStack.size() > MAX_UNDO) undoStack.pop_front();
    ClearRedo();
}

void UndoStack::PushDelete(Instance* inst, Instance* oldParent) {
    UndoAction a;
    a.type = UndoActionType::Delete;
    a.target = inst;
    a.deletedParent = oldParent;
    // Clone the subtree BEFORE it gets destroyed so we can resurrect it later
    if (inst && !inst->IsDestroyed()) {
        a.savedClone = inst->Clone();
    }
    undoStack.push_back(a);
    while ((int)undoStack.size() > MAX_UNDO) {
        auto& old = undoStack.front();
        if (old.type == UndoActionType::Delete && old.savedClone) delete old.savedClone;
        undoStack.pop_front();
    }
    ClearRedo();
}

bool UndoStack::Undo() {
    if (undoStack.empty()) return false;
    UndoAction a = undoStack.back();
    undoStack.pop_back();
    ApplyAction(a, true); // reverse = true (undo)
    redoStack.push_back(a);
    while ((int)redoStack.size() > MAX_UNDO) redoStack.pop_front();
    return true;
}

bool UndoStack::Redo() {
    if (redoStack.empty()) return false;
    UndoAction a = redoStack.back();
    redoStack.pop_back();
    ApplyAction(a, false); // reverse = false (redo)
    undoStack.push_back(a);
    while ((int)undoStack.size() > MAX_UNDO) undoStack.pop_front();
    return true;
}

void UndoStack::Clear() {
    for (auto& a : undoStack) {
        if (a.type == UndoActionType::Delete && a.savedClone) delete a.savedClone;
    }
    for (auto& a : redoStack) {
        if (a.type == UndoActionType::Delete && a.savedClone) delete a.savedClone;
    }
    undoStack.clear();
    redoStack.clear();
}
