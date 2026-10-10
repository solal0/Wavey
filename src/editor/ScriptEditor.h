#pragma once
#include "../core/Instance.h"
#include "../TextEditor.h"
#include "Autocomplete.h"
#include <string>
#include <vector>
#include <memory>

struct ScriptTab {
    Instance* script = nullptr;
    std::unique_ptr<TextEditor> editor;
    Autocomplete autocomplete;
};

class ScriptEditor {
public:
    std::vector<ScriptTab> tabs;
    int activeTab = 0;
    int lastActiveTab = 0;
    int forceFocusTab = -1;

    void OpenScript(Instance* script);
    void CloseTab(int index);
    void SaveTab(int index);
    void SaveAll();
    int FindTab(Instance* script);
    void ClearAll();

    static TextEditor::LanguageDefinition BuildLuauLanguage();
    static void ApplyDarkTheme(TextEditor& editor);
};

extern ScriptEditor g_ScriptEditor;