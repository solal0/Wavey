#pragma once
// ═══════════════════════════════════════════════════════
// Wavey Studio - Signal System
// Mirrors Roblox's RBXScriptSignal / RBXScriptConnection
// Usage: Signal<int> mySignal; mySignal.Fire(42);
// ═══════════════════════════════════════════════════════

#include <functional>
#include <vector>
#include <algorithm>

template<typename... Args>
class Signal {
public:
    using Callback = std::function<void(Args...)>;

    // Like :Connect(function) in Roblox
    size_t Connect(Callback cb) {
        size_t id = nextId++;
        entries.push_back({id, cb, true});
        return id;
    }

    // Like :Disconnect() — we mark it inactive
    void Disconnect(size_t id) {
        for (auto& e : entries)
            if (e.id == id) { e.active = false; break; }
    }

    // Like :Fire() — calls all connected callbacks
    void Fire(Args... args) {
        for (auto& e : entries)
            if (e.active) e.callback(args...);
        // Clean up disconnected entries
        entries.erase(
            std::remove_if(entries.begin(), entries.end(),
                [](const Entry& e) { return !e.active; }),
            entries.end()
        );
    }

    size_t ConnectionCount() const {
        size_t count = 0;
        for (auto& e : entries) if (e.active) count++;
        return count;
    }

private:
    struct Entry {
        size_t id;
        Callback callback;
        bool active;
    };
    std::vector<Entry> entries;
    size_t nextId = 0;
};