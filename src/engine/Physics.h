#pragma once
#include "../core/Instance.h"
#include "../core/Types.h"
#include <unordered_map>

struct RigidBody {
    Vector3 velocity = {0, 0, 0};
    Vector3 angularVelocity = {0, 0, 0}; // Degrees per second
    float angularDamping = 0.97f;        // Per-frame damping (1.0 = no damping)
};

class Physics {
public:
    static constexpr float GRAVITY = -196.2f; // Roblox default (studs/s^2)
    static constexpr float GROUND_Y = 0.0f;   // Top of baseplate
    static constexpr float RESTITUTION = 0.3f; // Bounciness
    static constexpr float FRICTION = 0.95f;

    void Step(Instance* root, float dt);
    void Reset();

    RigidBody& GetBody(Instance* part);
    bool HasBody(Instance* part) const;

private:
    std::unordered_map<Instance*, RigidBody> bodies;

    void CollectParts(Instance* node, std::vector<Instance*>& out);
    bool GetPartAABB(Instance* part, Vector3& bMin, Vector3& bMax);
};