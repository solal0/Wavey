#include "Instance.h"
#include <algorithm>
#include <iostream>
#include <random>

// ═══════════════════════════════════════════════════════
// Core Class Registration Blueprint
// ═══════════════════════════════════════════════════════
static void RegisterAllCoreClasses() {
    Instance::RegisterClass("Part", [](Instance* p) {
        p->SetProperty("Size", Vector3(4, 1, 2));
        p->SetProperty("Position", Vector3(0, 0, 0));
        p->SetProperty("Orientation", Vector3(0, 0, 0));
        p->SetProperty("Color", Color3(0.63f, 0.65f, 0.64f));
        p->SetProperty("Anchored", false);
        p->SetProperty("CanCollide", true);
        p->SetProperty("Transparency", 0.0);
        p->SetProperty("Material", std::string("Plastic"));
        p->SetProperty("Shape", std::string("Block"));
    });

    Instance::RegisterClass("Folder", [](Instance*) {});

    Instance::RegisterClass("Model", [](Instance*) {});

    Instance::RegisterClass("Script", [](Instance* s) {
        s->SetProperty("Source", std::string("print(\"Hello World!\")"));
        s->SetProperty("Disabled", false);
    });

    Instance::RegisterClass("LocalScript", [](Instance* s) {
        s->SetProperty("Source", std::string("print(\"Hello World!\")"));
        s->SetProperty("Disabled", false);
    });

    Instance::RegisterClass("ModuleScript", [](Instance* s) {
        s->SetProperty("Source", std::string("local module = {}\n\nreturn module"));
    });

    Instance::RegisterClass("Camera", [](Instance* c) {
        c->SetProperty("FieldOfView", 70.0);
        c->SetProperty("CFrame", CFrame(0, 10, 10));
    });

    Instance::RegisterClass("StringValue", [](Instance* v) {
        v->SetProperty("Value", std::string(""));
    });

    Instance::RegisterClass("IntValue", [](Instance* v) {
        v->SetProperty("Value", 0);
    });

    Instance::RegisterClass("BoolValue", [](Instance* v) {
        v->SetProperty("Value", false);
    });

    Instance::RegisterClass("NumberValue", [](Instance* v) {
        v->SetProperty("Value", 0.0);
    });

    Instance::RegisterClass("PointLight", [](Instance* l) {
        l->SetProperty("Brightness", 1.0);
        l->SetProperty("Range", 8.0);
        l->SetProperty("Color", Color3(1, 1, 1));
    });

    Instance::RegisterClass("Sound", [](Instance* s) {
        s->SetProperty("SoundId", std::string(""));
        s->SetProperty("Volume", 0.5);
        s->SetProperty("Loop", false);
        s->SetProperty("Playing", false);
    });

    Instance::RegisterClass("ObjectValue", [](Instance* v) {
        v->SetProperty("Value", (Instance*)nullptr);
    });

    Instance::RegisterClass("Player", [](Instance* p) {
        p->SetProperty("Character", (Instance*)nullptr);
    });

    // ── ScreenGui ──
    Instance::RegisterClass("ScreenGui", [](Instance* g) {
        g->SetProperty("Enabled", true);
        g->SetProperty("ResetOnSpawn", true);
        g->SetProperty("DisplayOrder", 0);
    });

    // ── Frame ──
    Instance::RegisterClass("Frame", [](Instance* f) {
        f->SetProperty("Position", UDim2(0, 0, 0, 0));
        f->SetProperty("Size", UDim2(0, 100, 0, 100));
        f->SetProperty("BackgroundColor3", Color3(0.15f, 0.15f, 0.15f));
        f->SetProperty("BackgroundTransparency", 0.0);
        f->SetProperty("BorderSizePixel", 1);
        f->SetProperty("BorderColor3", Color3(0, 0, 0));
        f->SetProperty("Visible", true);
        f->SetProperty("Active", true);
        f->SetProperty("ZIndex", 1);
    });

    // ── TextLabel ──
    Instance::RegisterClass("TextLabel", [](Instance* l) {
        l->SetProperty("Position", UDim2(0, 0, 0, 0));
        l->SetProperty("Size", UDim2(0, 200, 0, 50));
        l->SetProperty("BackgroundColor3", Color3(0.15f, 0.15f, 0.15f));
        l->SetProperty("BackgroundTransparency", 0.0);  // Visible by default
        l->SetProperty("BorderSizePixel", 1);
        l->SetProperty("BorderColor3", Color3(0, 0, 0));
        l->SetProperty("Visible", true);
        l->SetProperty("Active", true);
        l->SetProperty("ZIndex", 1);
        l->SetProperty("Text", std::string("Label"));
        l->SetProperty("TextColor3", Color3(1, 1, 1));
        l->SetProperty("TextSize", 14);
        l->SetProperty("TextTransparency", 0.0);
        l->SetProperty("TextScaled", false);
        l->SetProperty("TextXAlignment", std::string("Center"));
        l->SetProperty("TextYAlignment", std::string("Center"));
        l->SetProperty("Font", std::string("Roboto"));
    });

    // ── TextButton ──
    Instance::RegisterClass("TextButton", [](Instance* b) {
        b->SetProperty("Position", UDim2(0, 0, 0, 0));
        b->SetProperty("Size", UDim2(0, 200, 0, 50));
        b->SetProperty("BackgroundColor3", Color3(0.25f, 0.25f, 0.25f));
        b->SetProperty("BackgroundTransparency", 0.0);
        b->SetProperty("BorderSizePixel", 1);
        b->SetProperty("BorderColor3", Color3(0, 0, 0));
        b->SetProperty("Visible", true);
        b->SetProperty("Active", true);
        b->SetProperty("ZIndex", 1);
        b->SetProperty("Text", std::string("Button"));
        b->SetProperty("TextColor3", Color3(1, 1, 1));
        b->SetProperty("TextSize", 14);
        b->SetProperty("TextTransparency", 0.0);
        b->SetProperty("TextScaled", false);
        b->SetProperty("TextXAlignment", std::string("Center"));
        b->SetProperty("TextYAlignment", std::string("Center"));
        b->SetProperty("Font", std::string("Roboto"));
    });

    // ── ImageLabel ──
    Instance::RegisterClass("ImageLabel", [](Instance* i) {
        i->SetProperty("Position", UDim2(0, 0, 0, 0));
        i->SetProperty("Size", UDim2(0, 100, 0, 100));
        i->SetProperty("BackgroundColor3", Color3(1, 1, 1));
        i->SetProperty("BackgroundTransparency", 1.0);
        i->SetProperty("BorderSizePixel", 0);
        i->SetProperty("Visible", true);
        i->SetProperty("ZIndex", 1);
        i->SetProperty("Image", std::string(""));
        i->SetProperty("ImageColor3", Color3(1, 1, 1));
        i->SetProperty("ImageTransparency", 0.0);
    });

    Instance::RegisterClass("Humanoid", [](Instance* h) {
        h->SetProperty("Health", 100.0);
        h->SetProperty("MaxHealth", 100.0);
        h->SetProperty("WalkSpeed", 16.0);
        h->SetProperty("JumpPower", 50.0);
        h->SetProperty("Jumping", false);
    });
}

// ═══════════════════════════════════════════════════════
// Class Registry (singleton)
// ═══════════════════════════════════════════════════════
std::unordered_map<std::string, std::function<void(Instance*)>>& Instance::Registry() {
    static std::unordered_map<std::string, std::function<void(Instance*)>> registry;
    return registry;
}

void Instance::RegisterClass(const std::string& className,
                              std::function<void(Instance*)> defaults) {
    Registry()[className] = defaults;
}

bool Instance::ClassExists(const std::string& className) {
    // Auto-run core registration on first check if empty
    static bool coreClassesRegistered = false;
    if (!coreClassesRegistered) {
        coreClassesRegistered = true;
        RegisterAllCoreClasses();
    }
    return Registry().count(className) > 0;
}

std::vector<std::string> Instance::GetRegisteredClasses() {
    // Auto-run core registration on first retrieval
    static bool coreClassesRegistered = false;
    if (!coreClassesRegistered) {
        coreClassesRegistered = true;
        RegisterAllCoreClasses();
    }

    std::vector<std::string> classes;
    for (auto& [name, _] : Registry()) {
        classes.push_back(name);
    }
    std::sort(classes.begin(), classes.end());
    return classes;
}

// Instance.new("Part") — the factory
Instance* Instance::Create(const std::string& className) {
    // Auto-run core registration on first creation
    static bool coreClassesRegistered = false;
    if (!coreClassesRegistered) {
        coreClassesRegistered = true;
        RegisterAllCoreClasses();
    }

    Instance* inst = new Instance(className);

    auto it = Registry().find(className);
    if (it != Registry().end()) {
        it->second(inst);
    }

    return inst;
}

// ═══════════════════════════════════════════════════════
// Constructor / Destructor / Methods (remains the same)
// ═══════════════════════════════════════════════════════
static uint64_t GenerateUID() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    return gen();
}

Instance::Instance(const std::string& className): Name(className), ClassName(className) {
    UID = GenerateUID();
}

Instance* Instance::FindByUID(uint64_t uid) {
    if (this->UID == uid) return this;
    for (auto* child : children) {
        if (!child->IsDestroyed()) {
            if (auto* found = child->FindByUID(uid)) return found;
        }
    }
    return nullptr;
}

Instance::~Instance() {
    for (auto* child : children) {
        child->parent = nullptr;
        delete child;
    }
    children.clear();
}

Instance* Instance::GetParent() const { return parent; }

void Instance::SetParent(Instance* newParent) {
    if (destroyed) return;
    if (parent == newParent) return;

    if (parent) {
        parent->RemoveChild(this);
        parent->ChildRemoved.Fire(this);
    }

    parent = newParent;

    if (parent) {
        parent->AddChild(this);
        parent->ChildAdded.Fire(this);
    }

    AncestryChanged.Fire();
}

void Instance::AddChild(Instance* child) {
    children.push_back(child);
}

void Instance::RemoveChild(Instance* child) {
    children.erase(
        std::remove(children.begin(), children.end(), child),
        children.end()
    );
}

std::vector<Instance*> Instance::GetChildren() const {
    return children;
}

Instance* Instance::FindFirstChild(const std::string& name) const {
    for (auto* child : children) {
        if (child->Name == name) return child;
    }
    return nullptr;
}

std::vector<Instance*> Instance::GetDescendants() const {
    std::vector<Instance*> result;
    for (auto* child : children) {
        result.push_back(child);
        auto childDesc = child->GetDescendants();
        result.insert(result.end(), childDesc.begin(), childDesc.end());
    }
    return result;
}

void Instance::SetProperty(const std::string& name, PropertyValue value) {
    properties[name] = value;
    PropertyChanged.Fire(name);
}

Instance::PropertyValue Instance::GetProperty(const std::string& name) const {
    auto it = properties.find(name);
    if (it != properties.end()) return it->second;
    return std::monostate{};
}

bool Instance::HasProperty(const std::string& name) const {
    return properties.count(name) > 0;
}

std::vector<std::string> Instance::GetPropertyNames() const {
    std::vector<std::string> names;
    for (auto& [key, val] : properties)
        names.push_back(key);
    return names;
}

void Instance::Destroy() {
    if (destroyed) return;
    destroyed = true;

    auto childrenCopy = children;
    for (auto* child : childrenCopy) {
        child->Destroy();
    }

    if (parent) {
        parent->RemoveChild(this);
        parent->ChildRemoved.Fire(this);
        parent = nullptr;
    }

    AncestryChanged.Fire();
}

bool Instance::IsDestroyed() const { return destroyed; }

Instance* Instance::Clone() const {
    Instance* copy = new Instance(ClassName);
    copy->Name = Name;
    copy->properties = properties;

    for (auto* child : children) {
        Instance* childCopy = child->Clone();
        childCopy->SetParent(copy);
    }
    return copy;
}

bool Instance::IsA(const std::string& className) const {
    return ClassName == className;
}