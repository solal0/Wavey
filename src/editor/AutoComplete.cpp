#include "Autocomplete.h"
#include <algorithm>
#include <cctype>
#include <sstream>

// ── Shared Built-in Word List ──
static std::vector<ACEntry> g_Builtins;
static bool g_BuiltinsInitialized = false;

// ── DataModel snapshot ──
// Maps "chain path" → children names
// e.g. "game" → {"Workspace", "Players", ...}
// e.g. "game.Workspace" → {"Baseplate", "RedBlock", ...}
static std::unordered_map<std::string, std::vector<std::string>> g_DMSnapshot;

// Maps enum name → values
// e.g. "KeyCode" → {"A", "B", ..., "Space"}
static std::unordered_map<std::string, std::vector<std::string>> g_EnumSnapshot;

void Autocomplete::InitBuiltins() {
    if (g_BuiltinsInitialized) return;
    g_BuiltinsInitialized = true;

    const char* keywords[] = {
        "local", "function", "end", "if", "then", "else", "elseif",
        "for", "while", "do", "repeat", "until", "return", "break",
        "continue", "in", "and", "or", "not", "true", "false", "nil",
        "type", "typeof", "export", "self"
    };
    for (auto& k : keywords) g_Builtins.push_back({k, false});

    const char* funcs[] = {
        "print", "tostring", "tonumber", "pairs", "ipairs", "next", "select",
        "pcall", "xpcall", "error", "assert", "rawget", "rawset", "rawequal",
        "setmetatable", "getmetatable", "unpack", "tick", "wait", "spawn",
        "delay", "loadstring", "newproxy", "gcinfo", "collectgarbage"
    };
    for (auto& f : funcs) g_Builtins.push_back({f, true});

    const char* strFuncs[] = {
        "string.format", "string.find", "string.match", "string.sub",
        "string.len", "string.rep", "string.lower", "string.upper",
        "string.gsub", "string.byte", "string.char", "string.split"
    };
    for (auto& f : strFuncs) g_Builtins.push_back({f, true});

    const char* tblFuncs[] = {
        "table.insert", "table.remove", "table.sort", "table.concat",
        "table.find", "table.create", "table.move", "table.pack",
        "table.unpack", "table.clear", "table.freeze", "table.clone"
    };
    for (auto& f : tblFuncs) g_Builtins.push_back({f, true});

    const char* mathFuncs[] = {
        "math.floor", "math.ceil", "math.abs", "math.max", "math.min",
        "math.sqrt", "math.random", "math.randomseed", "math.clamp",
        "math.sign", "math.round", "math.sin", "math.cos", "math.tan",
        "math.pi", "math.huge", "math.rad", "math.deg"
    };
    for (auto& f : mathFuncs) g_Builtins.push_back({f, true});

    const char* globals[] = {
        "game", "workspace", "script", "Instance", "Vector3", "Color3",
        "CFrame", "UDim2", "UDim", "task", "Enum"
    };
    for (auto& g : globals) g_Builtins.push_back({g, false});

    const char* methods[] = {
        "FindFirstChild", "WaitForChild", "GetChildren", "GetDescendants",
        "Clone", "Destroy", "IsA", "GetService", "GetPropertyChangedSignal",
        "IsDescendantOf", "GetFullName", "Connect"
    };
    for (auto& m : methods) g_Builtins.push_back({m, true});

    const char* taskFuncs[] = {
        "task.wait", "task.spawn", "task.delay", "task.defer", "task.cancel"
    };
    for (auto& f : taskFuncs) g_Builtins.push_back({f, true});

    // Snippet overrides
    for (auto& entry : g_Builtins) {
        if (entry.name == "print") { entry.snippet = "print(\"\")"; entry.cursorOffset = 2; }
        else if (entry.name == "Connect") { entry.snippet = "Connect(function()\n\nend)"; entry.cursorOffset = 5; }
        else if (entry.name == "function") { entry.snippet = "function()\n\nend"; entry.cursorOffset = 4; entry.isFunction = false; }
        else if (entry.name == "if") { entry.snippet = "if  then\n\nend"; entry.cursorOffset = 9; }
        else if (entry.name == "for") { entry.snippet = "for  do\n\nend"; entry.cursorOffset = 8; }
        else if (entry.name == "while") { entry.snippet = "while  do\n\nend"; entry.cursorOffset = 8; }
        else if (entry.name == "task.wait") { entry.snippet = "task.wait()"; entry.cursorOffset = 1; }
    }
}

std::vector<ACEntry> Autocomplete::GetBuiltins() {
    InitBuiltins();
    return g_Builtins;
}

// ── Snapshot the DataModel tree into a flat chain map ──
static void SnapshotRecursive(Instance* inst, const std::string& chainPath) {
    if (!inst || inst->IsDestroyed()) return;
    std::vector<std::string> childNames;
    for (auto* child : inst->GetChildren()) {
        if (!child->IsDestroyed()) {
            childNames.push_back(child->Name);
            SnapshotRecursive(child, chainPath + "." + child->Name);
        }
    }
    g_DMSnapshot[chainPath] = childNames;
}

void Autocomplete::SnapshotDataModel(Instance* root) {
    if (!root) return;
    g_DMSnapshot.clear();

    // Build map keyed by: "game", "game.Workspace", "game.Workspace.RedBlock", etc.
    std::vector<std::string> gameChildren;
    for (auto* child : root->GetChildren()) {
        if (!child->IsDestroyed()) {
            gameChildren.push_back(child->Name);
            SnapshotRecursive(child, "game." + child->Name);
            // Also register under "workspace" alias
            if (child->Name == "Workspace") {
                SnapshotRecursive(child, "workspace");
            }
        }
    }
    g_DMSnapshot["game"] = gameChildren;
}

void Autocomplete::SnapshotEnums() {
    g_EnumSnapshot.clear();

    // KeyCode
    g_EnumSnapshot["KeyCode"] = {
        "A","B","C","D","E","F","G","H","I","J","K","L","M",
        "N","O","P","Q","R","S","T","U","V","W","X","Y","Z",
        "0","1","2","3","4","5","6","7","8","9",
        "Space","Return","Escape","Backspace","Tab",
        "LeftShift","RightShift","LeftControl","RightControl","LeftAlt",
        "Up","Down","Left","Right","Button1","Button2","Button3"
    };
    g_EnumSnapshot["Enum.KeyCode"] = g_EnumSnapshot["KeyCode"];

    g_EnumSnapshot["EasingStyle"] = {"Linear","Sine","Quad","Cubic"};
    g_EnumSnapshot["Enum.EasingStyle"] = g_EnumSnapshot["EasingStyle"];

    g_EnumSnapshot["EasingDirection"] = {"In","Out","InOut"};
    g_EnumSnapshot["Enum.EasingDirection"] = g_EnumSnapshot["EasingDirection"];

    g_EnumSnapshot["Enum.Font"] = {"Roboto","RobotoBold","OpenSans","JetBrainsMono"};
    g_EnumSnapshot["Enum.TextXAlignment"] = {"Left","Center","Right"};
    g_EnumSnapshot["Enum.TextYAlignment"] = {"Top","Center","Bottom"};

    // Enum root
    g_EnumSnapshot["Enum"] = {"KeyCode","EasingStyle","EasingDirection","Font","TextXAlignment","TextYAlignment"};
}

std::vector<std::string> Autocomplete::ResolveChain(const std::string& chain) {
    // Check DataModel first
    auto itDM = g_DMSnapshot.find(chain);
    if (itDM != g_DMSnapshot.end()) return itDM->second;

    // Check enum
    auto itE = g_EnumSnapshot.find(chain);
    if (itE != g_EnumSnapshot.end()) return itE->second;

    return {};
}

// ── Learn identifiers from script text ──
void Autocomplete::RebuildLearnedWords(const std::string& text) {
    learnedWords.clear();
    learnedFunctions.clear();

    size_t len = text.size();
    for (size_t i = 0; i < len; i++) {
        if (i + 6 < len && text.substr(i, 6) == "local " && (i == 0 || !isalnum(text[i - 1]))) {
            size_t start = i + 6;
            size_t end = start;
            while (end < len && (isalnum(text[end]) || text[end] == '_')) end++;
            if (end > start) {
                std::string word = text.substr(start, end - start);
                if (word.length() >= 2) learnedWords.insert(word);
            }
        }

        if (isalpha(text[i]) || text[i] == '_') {
            size_t start = i;
            while (i < len && (isalnum(text[i]) || text[i] == '_' || text[i] == '.')) i++;
            std::string word = text.substr(start, i - start);
            if (i < len && text[i] == '(' && word.length() >= 2) {
                size_t dot = word.rfind('.');
                std::string funcName = (dot != std::string::npos) ? word.substr(dot + 1) : word;
                if (funcName.length() >= 2) {
                    learnedFunctions.insert(funcName);
                    learnedWords.insert(funcName);
                }
            }
            i--;
        }
    }
}

void Autocomplete::Update(const std::string& prefix, const std::string& chainContext) {
    // Only reset selection if the prefix or context actually changed
    static std::string lastPrefix;
    static std::string lastChain;
    bool contextChanged = (prefix != lastPrefix || chainContext != lastChain);
    lastPrefix = prefix;
    lastChain = chainContext;

    currentPrefix = prefix;
    suggestions.clear();
    if (contextChanged) selectedIndex = 0;

    std::string lcPrefix = prefix;
    std::transform(lcPrefix.begin(), lcPrefix.end(), lcPrefix.begin(), ::tolower);

    auto matches = [&](const std::string& name) -> bool {
        if (lcPrefix.empty()) return true;
        std::string lc = name;
        std::transform(lc.begin(), lc.end(), lc.begin(), ::tolower);
        return lc.find(lcPrefix) == 0;
    };

    if (!chainContext.empty()) {
        // Dotted context: ONLY show resolved children/enum values, no learned/built-ins
        auto resolved = ResolveChain(chainContext);
        for (auto& name : resolved) {
            if (matches(name)) {
                suggestions.push_back({name, false, "", 0});
            }
        }
    } else {
        // No dot context: show learned + built-ins
        if (prefix.length() < 2) {
            visible = false;
            return;
        }

        for (auto& w : learnedWords) {
            if (matches(w)) {
                bool isFunc = learnedFunctions.count(w) > 0;
                suggestions.push_back({w, isFunc});
            }
        }

        for (auto& b : GetBuiltins()) {
            if (matches(b.name)) {
                bool dup = false;
                for (auto& s : suggestions) if (s.name == b.name) { dup = true; break; }
                if (!dup) suggestions.push_back(b);
            }
        }
    }

    std::sort(suggestions.begin(), suggestions.end(), [](const ACEntry& a, const ACEntry& b) { return a.name < b.name; });

    if (suggestions.size() > 15) suggestions.resize(15);

    // Clamp selectedIndex to valid range
    if (selectedIndex >= (int)suggestions.size()) selectedIndex = (int)suggestions.size() - 1;
    if (selectedIndex < 0) selectedIndex = 0;

    visible = !suggestions.empty();
}

void Autocomplete::Reset() {
    visible = false;
    suggestions.clear();
    currentPrefix.clear();
    selectedIndex = 0;
}