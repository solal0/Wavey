#pragma once
#include "../core/Instance.h"
#include "../core/Types.h"
#include <unordered_map>
#include <string>

namespace JPH {
    class PhysicsSystem;
    class BodyInterface;
    class TempAllocator;
    class JobSystemThreadPool;
    class BodyID;
}

class JoltPhysics {
public:
    void Init();
    void Shutdown();
    void Step(float dt);
    void ClearAll(); // Clear all bodies on Stop

    // Sync a Part instance to/from Jolt
    void RegisterPart(Instance* part);
    void UnregisterPart(Instance* part);
    void SyncAllFromJolt();
    void SyncAllToJolt();

    // Velocity access (for Luau API)
    Vector3 GetLinearVelocity(Instance* part);
    void SetLinearVelocity(Instance* part, const Vector3& vel);
    Vector3 GetAngularVelocity(Instance* part);
    void SetAngularVelocity(Instance* part, const Vector3& vel);

    // ── High-Performance Raycasting (Phase 13b) ──
    bool Raycast(const Vector3& origin, const Vector3& direction, float maxDistance, float& hitY, Vector3& hitNormal, Instance* ignorePart = nullptr);

private:
    JPH::PhysicsSystem* physicsSystem = nullptr;
    JPH::TempAllocator* tempAllocator = nullptr;
    JPH::JobSystemThreadPool* jobSystem = nullptr;
    JPH::BodyInterface* bodyInterface = nullptr;

    std::unordered_map<Instance*, uint32_t> partToBodyID;
    std::unordered_map<uint32_t, Instance*> bodyIDToPart;

    uint32_t CreateJoltBody(Instance* part);
    void RemoveJoltBody(Instance* part);
};

extern JoltPhysics g_JoltPhysics;