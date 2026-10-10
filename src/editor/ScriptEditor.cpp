#include "ScriptEditor.h"
#include <iostream>

ScriptEditor g_ScriptEditor;

// ═══════════════════════════════════════════════════════
// Luau Language Definition
// ═══════════════════════════════════════════════════════
TextEditor::LanguageDefinition ScriptEditor::BuildLuauLanguage() {
    TextEditor::LanguageDefinition langDef;

    // Keywords (Luau + Lua 5.1)
    static const char* const keywords[] = {
        "and", "break", "do", "else", "elseif", "end", "false", "for",
        "function", "if", "in", "local", "nil", "not", "or", "repeat",
        "return", "then", "true", "until", "while",
        // Luau additions
        "continue", "export", "type", "typeof", "self"
    };
    for (auto& k : keywords) langDef.mKeywords.insert(k);

    // Built-in identifiers (Roblox/Wavey globals and standard library)
    static const char* const identifiers[] = {
        // Lua stdlib
        "print", "tostring", "tonumber", "pairs", "ipairs", "next", "select",
        "pcall", "xpcall", "error", "assert", "type", "rawget", "rawset",
        "rawequal", "setmetatable", "getmetatable", "unpack",
        "string", "table", "math", "os", "io", "coroutine", "debug", "bit32",
        // Wavey globals
        "game", "workspace", "Instance", "Vector3", "Color3", "CFrame",
        "task", "wait", "UDim2", "UDim", "spawn", "delay",
        // Enums
        "KeyCode", "EasingStyle", "EasingDirection", "UserInputType", "Material"
    };
    for (auto& i : identifiers) {
        TextEditor::Identifier id;
        id.mDeclaration = "Built-in";
        langDef.mIdentifiers.insert(std::make_pair(std::string(i), id));
    }

    // Token regexes (order matters - more specific first)
    langDef.mTokenRegexStrings.clear();
    langDef.mTokenRegexStrings.push_back(std::make_pair("--\\[\\[.*?\\]\\]",
        TextEditor::PaletteIndex::Comment));
    langDef.mTokenRegexStrings.push_back(std::make_pair("--.*",
        TextEditor::PaletteIndex::Comment));
    langDef.mTokenRegexStrings.push_back(std::make_pair("\"[^\"]*\"",
        TextEditor::PaletteIndex::String));
    langDef.mTokenRegexStrings.push_back(std::make_pair("\'[^\']*\'",
        TextEditor::PaletteIndex::String));
    langDef.mTokenRegexStrings.push_back(std::make_pair("\\[\\[.*?\\]\\]",
        TextEditor::PaletteIndex::String));
    langDef.mTokenRegexStrings.push_back(std::make_pair(
        "[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?",
        TextEditor::PaletteIndex::Number));
    langDef.mTokenRegexStrings.push_back(std::make_pair("0[xX][0-9a-fA-F]+",
        TextEditor::PaletteIndex::Number));
    langDef.mTokenRegexStrings.push_back(std::make_pair("[a-zA-Z_][a-zA-Z0-9_]*",
        TextEditor::PaletteIndex::Identifier));
    langDef.mTokenRegexStrings.push_back(std::make_pair(
        "[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]",
        TextEditor::PaletteIndex::Punctuation));

    langDef.mCommentStart = "--[[";
    langDef.mCommentEnd = "]]";
    langDef.mSingleLineComment = "--";

    langDef.mCaseSensitive = true;
    langDef.mAutoIndentation = true;
    langDef.mName = "Luau";

    return langDef;
}

// ═══════════════════════════════════════════════════════
// Custom Dark Palette (Wavey-themed)
// ═══════════════════════════════════════════════════════
void ScriptEditor::ApplyDarkTheme(TextEditor& editor) {
    TextEditor::Palette palette = TextEditor::GetDarkPalette();

    // Deep black background for that Studio feel
    palette[(int)TextEditor::PaletteIndex::Background]           = 0xff262121; // Near-black
    palette[(int)TextEditor::PaletteIndex::CurrentLineFill]      = 0x40ffffff; // Subtle highlight
    palette[(int)TextEditor::PaletteIndex::CurrentLineFillInactive] = 0x20ffffff;
    palette[(int)TextEditor::PaletteIndex::CurrentLineEdge]      = 0x40ffffff;

    // Vibrant syntax colors (ARGB little-endian: 0xAABBGGRR)
    palette[(int)TextEditor::PaletteIndex::Default]              = 0xffd4d4d4; // Normal text
    palette[(int)TextEditor::PaletteIndex::Keyword]              = 0xffd49069; // Soft blue-orange (if/then/end)
    palette[(int)TextEditor::PaletteIndex::Number]               = 0xffb5cea8; // Light green
    palette[(int)TextEditor::PaletteIndex::String]               = 0xff6ec3ce; // Teal
    palette[(int)TextEditor::PaletteIndex::CharLiteral]          = 0xff6ec3ce;
    palette[(int)TextEditor::PaletteIndex::Punctuation]          = 0xffd4d4d4;
    palette[(int)TextEditor::PaletteIndex::Preprocessor]         = 0xffbd63c5;
    palette[(int)TextEditor::PaletteIndex::Identifier]           = 0xffd4d4d4;
    palette[(int)TextEditor::PaletteIndex::KnownIdentifier]      = 0xffdcdcaa; // Yellow (print, Vector3)
    palette[(int)TextEditor::PaletteIndex::PreprocIdentifier]    = 0xffbd63c5;
    palette[(int)TextEditor::PaletteIndex::Comment]              = 0xff6a9955; // Green comments
    palette[(int)TextEditor::PaletteIndex::MultiLineComment]     = 0xff6a9955;

    // Line number gutter
    //palette[(int)TextEditor::PaletteIndex::LineNumber]           = 0xff858585; // Grey numbers
    palette[(int)TextEditor::PaletteIndex::Selection]            = 0x80264f78; // Blue selection
    palette[(int)TextEditor::PaletteIndex::Cursor]               = 0xffffffff; // Bright white cursor
    palette[(int)TextEditor::PaletteIndex::LineNumber]           = 0xff5a5a5a; // Subtle grey line numbers

    editor.SetPalette(palette);
}

// ═══════════════════════════════════════════════════════
// Script Editor Methods
// ═══════════════════════════════════════════════════════
int ScriptEditor::FindTab(Instance* script) {
    for (int i = 0; i < (int)tabs.size(); i++) {
        if (tabs[i].script == script) return i + 1;
    }
    return -1;
}

void ScriptEditor::OpenScript(Instance* script) {
    if (!script) return;

    int existing = FindTab(script);
    if (existing >= 0) {
        activeTab = existing;
        forceFocusTab = existing; // Request tab bar to re-select this tab next frame
        return;
    }

    ScriptTab tab;
    tab.script = script;
    tab.editor = std::make_unique<TextEditor>();

    // Configure editor
    tab.editor->SetLanguageDefinition(BuildLuauLanguage());
    ApplyDarkTheme(*tab.editor);
    tab.editor->SetShowWhitespaces(false);
    tab.editor->SetTabSize(4);
    tab.editor->SetHandleMouseInputs(true);
    tab.editor->SetHandleKeyboardInputs(true);

    // Load source from Instance
    std::string source = "";
    if (script->HasProperty("Source")) {
        auto val = script->GetProperty("Source");
        if (std::holds_alternative<std::string>(val))
            source = std::get<std::string>(val);
    }
    tab.editor->SetText(source);

    tabs.push_back(std::move(tab));
    activeTab = (int)tabs.size();
    forceFocusTab = activeTab;
}

void ScriptEditor::CloseTab(int index) {
    if (index < 1 || index > (int)tabs.size()) return;
    int vecIdx = index - 1;
    SaveTab(index);
    tabs.erase(tabs.begin() + vecIdx);

    if (activeTab >= (int)tabs.size() + 1) {
        activeTab = (int)tabs.size();
    }
    if (activeTab < 0) activeTab = 0;
}

void ScriptEditor::SaveTab(int index) {
    if (index < 1 || index > (int)tabs.size()) return;
    int vecIdx = index - 1;
    if (tabs[vecIdx].script && !tabs[vecIdx].script->IsDestroyed() && tabs[vecIdx].editor) {
        tabs[vecIdx].script->SetProperty("Source", tabs[vecIdx].editor->GetText());
    }
}

void ScriptEditor::SaveAll() {
    for (int i = 0; i < (int)tabs.size(); i++) {
        SaveTab(i + 1);
    }
}

void ScriptEditor::ClearAll() {
    SaveAll();
    tabs.clear();
    activeTab = 0;
    lastActiveTab = 0;
}