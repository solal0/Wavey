#pragma once
#include "../core/Instance.h"
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>

struct ACEntry {
    std::string name;
    bool isFunction = false;
    std::string snippet;
    int cursorOffset = 0;
};

class Autocomplete {
public:
    int selectedIndex = 0;
    bool visible = false;
    std::string currentPrefix;
    std::vector<ACEntry> suggestions;

    void RebuildLearnedWords(const std::string& fullText);
    void Update(const std::string& prefix, const std::string& chainContext);
    void Reset();

    static void InitBuiltins();
    static std::vector<ACEntry> GetBuiltins();

    // ── Live DataModel snapshot (Phase 11d) ──
    static void SnapshotDataModel(Instance* root);
    static void SnapshotEnums();
    static std::vector<std::string> ResolveChain(const std::string& chain); // Returns child names for e.g. "game.Workspace"

private:
    std::unordered_set<std::string> learnedWords;
    std::unordered_set<std::string> learnedFunctions;
};