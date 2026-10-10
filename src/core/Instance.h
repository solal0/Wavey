#pragma once
// ═══════════════════════════════════════════════════════
// Wavey Studio - Instance Class
// The base of everything, just like Roblox.
// Every Part, Folder, Script, Camera is an Instance.
// ═══════════════════════════════════════════════════════

#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <functional>
#include "Types.h"
#include "Signal.h"

class Instance {
public:
    uint64_t UID; // Unique Identifier for persistent object references
    Instance* FindByUID(uint64_t uid);
    // A property can be any of these types (like Roblox's property system)
    using PropertyValue = std::variant<
        std::monostate,   // nil
        bool,
        int,
        double,
        std::string,
        Vector3,
        Color3,
        CFrame,
        UDim,
        UDim2,
        Instance*         // Object reference (like ObjectValue)
    >;

    // ── Construction ──
    explicit Instance(const std::string& className);
    ~Instance();

    // ── Core Identity ──
    std::string Name;
    const std::string ClassName;  // Read-only after creation

    // ── Parenting (like Roblox's .Parent) ──
    Instance* GetParent() const;
    void SetParent(Instance* newParent);
    std::vector<Instance*> GetChildren() const;
    Instance* FindFirstChild(const std::string& name) const;
    std::vector<Instance*> GetDescendants() const;

    // ── Properties (like Roblox's property system) ──
    void SetProperty(const std::string& name, PropertyValue value);
    PropertyValue GetProperty(const std::string& name) const;
    bool HasProperty(const std::string& name) const;
    std::vector<std::string> GetPropertyNames() const;

    // ── Lifecycle ──
    void Destroy();
    bool IsDestroyed() const;
    Instance* Clone() const;
    bool IsA(const std::string& className) const;

    // ── Signals (like Roblox's events) ──
    Signal<Instance*> ChildAdded;
    Signal<Instance*> ChildRemoved;
    Signal<std::string> PropertyChanged;  // Fires with property name
    Signal<> DescendantAdded;
    Signal<> AncestryChanged;

    // ── Class Registry (like Roblox's class database) ──
    // RegisterClass("Part", [](Instance* p) { p->SetProperty("Size", Vector3(4,1,2)); })
    static void RegisterClass(const std::string& className, std::function<void(Instance*)> defaults);
    static Instance* Create(const std::string& className);
    static bool ClassExists(const std::string& className);
    static std::vector<std::string> GetRegisteredClasses();

private:
    std::unordered_map<std::string, PropertyValue> properties;
    std::vector<Instance*> children;
    Instance* parent = nullptr;
    bool destroyed = false;

    // Internal: add/remove child without firing signals (used during construction)
    void AddChild(Instance* child);
    void RemoveChild(Instance* child);

    // Class registry singleton
    static std::unordered_map<std::string, std::function<void(Instance*)>>& Registry();
};