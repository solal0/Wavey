#include "JoltPhysics.h"
#include <iostream>
#include <cstring>
#include <cmath>
#include <vector>
#include <filesystem>
#include <cstdarg>

// Jolt headers
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h> // Added for Raycast

using namespace JPH;

JoltPhysics g_JoltPhysics;

namespace Layers {
    static constexpr ObjectLayer NON_MOVING = 0;
    static constexpr ObjectLayer MOVING = 1;
    static constexpr ObjectLayer NUM_LAYERS = 2;
}

class ObjectLayerPairFilterImpl : public ObjectLayerPairFilter {
public:
    virtual bool ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override {
        switch (inObject1) {
            case Layers::NON_MOVING: return inObject2 == Layers::MOVING;
            case Layers::MOVING:     return true;
            default:                 return false;
        }
    }
};

namespace BroadPhaseLayers {
    static constexpr BroadPhaseLayer NON_MOVING(0);
    static constexpr BroadPhaseLayer MOVING(1);
    static constexpr uint32 NUM_LAYERS = 2;
}

class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
    }
    virtual uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
    virtual BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override {
        return mObjectToBroadPhase[inLayer];
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    virtual const char* GetBroadPhaseLayerName(BroadPhaseLayer inLayer) const override {
        switch ((BroadPhaseLayer::Type)inLayer) {
            case (BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
            case (BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:     return "MOVING";
            default: return "INVALID";
        }
    }
#endif
private:
    BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl : public ObjectVsBroadPhaseLayerFilter {
public:
    virtual bool ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override {
        switch (inLayer1) {
            case Layers::NON_MOVING: return inLayer2 == BroadPhaseLayers::MOVING;
            case Layers::MOVING:     return true;
            default:                 return false;
        }
    }
};

static BPLayerInterfaceImpl sBroadPhaseLayerInterface;
static ObjectVsBroadPhaseLayerFilterImpl sObjectVsBroadPhaseLayerFilter;
static ObjectLayerPairFilterImpl sObjectPairFilter;

static void TraceImpl(const char* inFMT, ...) {
    va_list list;
    va_start(list, inFMT);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), inFMT, list);
    va_end(list);
}

#ifdef JPH_ENABLE_ASSERTS
static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, uint inLine) {
    return true; 
}
#endif

void JoltPhysics::Init() {
    RegisterDefaultAllocator();
    Trace = TraceImpl;
#ifdef JPH_ENABLE_ASSERTS
    AssertFailed = AssertFailedImpl;
#endif

    Factory::sInstance = new Factory();
    RegisterTypes();

    tempAllocator = new TempAllocatorImpl(10 * 1024 * 1024);
    jobSystem = new JobSystemThreadPool(cMaxPhysicsJobs, cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);

    physicsSystem = new PhysicsSystem();
    physicsSystem->Init(1024, 0, 65536, 10240, sBroadPhaseLayerInterface, sObjectVsBroadPhaseLayerFilter, sObjectPairFilter);

    // Set Roblox-scale gravity vector (Punchy -196.2 studs/s^2 instead of slow-motion Earth gravity)
    physicsSystem->SetGravity(Vec3(0.0f, -196.2f, 0.0f));

    bodyInterface = &physicsSystem->GetBodyInterface();

    // Create default static ground plane at Y=0
    BodyCreationSettings groundSettings(new BoxShape(Vec3(256.0f, 0.5f, 256.0f)),
        RVec3(0, -0.5f, 0), Quat::sIdentity(), EMotionType::Static, Layers::NON_MOVING);
    bodyInterface->CreateAndAddBody(groundSettings, EActivation::DontActivate);

    std::cout << "[Jolt] Physics engine initialized." << std::endl;
}

void JoltPhysics::Shutdown() {
    ClearAll();
    if (physicsSystem) delete physicsSystem;
    if (jobSystem) delete jobSystem;
    if (tempAllocator) delete tempAllocator;
    if (Factory::sInstance) { delete Factory::sInstance; Factory::sInstance = nullptr; }
    UnregisterTypes();
}

void JoltPhysics::ClearAll() {
    if (!bodyInterface) return;
    for (auto& [inst, bodyID] : partToBodyID) {
        BodyID bid(bodyID);
        if (bodyInterface->IsAdded(bid)) {
            bodyInterface->RemoveBody(bid);
            bodyInterface->DestroyBody(bid);
        }
    }
    partToBodyID.clear();
    bodyIDToPart.clear();
    std::cout << "[Jolt] Cleared all physics bodies." << std::endl;
}

uint32_t JoltPhysics::CreateJoltBody(Instance* part) {
    Vector3 pos(0, 0, 0), size(4, 1, 2), orient(0, 0, 0);
    bool anchored = false;

    if (part->HasProperty("Position")) {
        auto pv = part->GetProperty("Position");
        if (std::holds_alternative<Vector3>(pv)) pos = std::get<Vector3>(pv);
    }
    if (part->HasProperty("Orientation")) {
        auto ov = part->GetProperty("Orientation");
        if (std::holds_alternative<Vector3>(ov)) orient = std::get<Vector3>(ov);
    }
    if (part->HasProperty("Size")) {
        auto sv = part->GetProperty("Size");
        if (std::holds_alternative<Vector3>(sv)) size = std::get<Vector3>(sv);
    }
    if (part->HasProperty("Anchored")) {
        auto av = part->GetProperty("Anchored");
        if (std::holds_alternative<bool>(av)) anchored = std::get<bool>(av);
    }

    std::string shape = "Block";
    if (part->HasProperty("Shape")) {
        auto shv = part->GetProperty("Shape");
        if (std::holds_alternative<std::string>(shv)) shape = std::get<std::string>(shv);
    }

    RefConst<Shape> joltShape;
    if (part->Name == "HumanoidRootPart") {
        // Read HipHeight from the sibling Humanoid in the character model (default to 2.0 R6 standard)
        float hipHeight = 2.0f;
        Instance* parentModel = part->GetParent();
        if (parentModel) {
            Instance* humanoid = parentModel->FindFirstChild("Humanoid");
            if (humanoid && humanoid->HasProperty("HipHeight")) {
                auto hhv = humanoid->GetProperty("HipHeight");
                if (std::holds_alternative<double>(hhv)) hipHeight = (float)std::get<double>(hhv);
                else if (std::holds_alternative<int>(hhv)) hipHeight = (float)std::get<int>(hhv);
            }
        }

        // HRP height is 2.0. Total collision height = HRP height (2.0) + HipHeight.
        // This extends the collision box perfectly down to the soles of the feet!
        float totalHeight = 2.0f + hipHeight;
        joltShape = new BoxShape(Vec3(1.0f, totalHeight * 0.5f, 0.5f));
    } else if (shape == "Ball") {
        float radius = (std::max)({fabsf(size.X), fabsf(size.Y), fabsf(size.Z)}) * 0.5f;
        joltShape = new SphereShape(radius);
    } else if (shape == "Cylinder") {
        float halfHeight = fabsf(size.X) * 0.5f;
        float radius = (std::max)(fabsf(size.Y), fabsf(size.Z)) * 0.5f;
        joltShape = new CylinderShape(halfHeight, radius);
    } else {
        joltShape = new BoxShape(Vec3(fabsf(size.X) * 0.5f, fabsf(size.Y) * 0.5f, fabsf(size.Z) * 0.5f));
    }

    EMotionType motionType = anchored ? EMotionType::Static : EMotionType::Dynamic;
    ObjectLayer layer = anchored ? Layers::NON_MOVING : Layers::MOVING;

    float rx = orient.X * 3.14159265f / 360.0f;
    float ry = orient.Y * 3.14159265f / 360.0f;
    float rz = orient.Z * 3.14159265f / 360.0f;
    Quat rotation = Quat::sEulerAngles(Vec3(rx, ry, rz));

    BodyCreationSettings settings(joltShape, RVec3(pos.X, pos.Y, pos.Z), rotation, motionType, layer);
    
    settings.mRestitution = 0.1f;
    settings.mFriction = 0.6f;
    // CRITICAL: force Jolt to allocate motion properties even if the body starts Static.
    // This allows safe conversion from Static → Dynamic at runtime without crashing.
    settings.mAllowDynamicOrKinematic = true;

    // ── Rotation Locking & Inertia calculations ──
    if (part->Name == "HumanoidRootPart") {
        // Player's driver part is dynamic but has locked rotation (translation-only!)
        settings.mAllowedDOFs = EAllowedDOFs::TranslationX | EAllowedDOFs::TranslationY | EAllowedDOFs::TranslationZ;
        settings.mOverrideMassProperties = EOverrideMassProperties::CalculateMassAndInertia;
    }
    else if (!anchored) {
        // Standard dynamic parts need automated mass/inertia computed so they don't clip
        settings.mOverrideMassProperties = EOverrideMassProperties::CalculateMassAndInertia;
    }

    BodyID bodyID = bodyInterface->CreateAndAddBody(settings, EActivation::Activate);
    uint32_t id = bodyID.GetIndexAndSequenceNumber();

    partToBodyID[part] = id;
    bodyIDToPart[id] = part;

    return id;
}

void JoltPhysics::RemoveJoltBody(Instance* part) {
    auto it = partToBodyID.find(part);
    if (it != partToBodyID.end()) {
        BodyID bid(it->second);
        if (bodyInterface->IsAdded(bid)) {
            bodyInterface->RemoveBody(bid);
            bodyInterface->DestroyBody(bid);
        }
        bodyIDToPart.erase(it->second);
        partToBodyID.erase(it);
    }
}

void JoltPhysics::RegisterPart(Instance* part) {
    if (partToBodyID.count(part)) return;

    bool canCollide = true;
    if (part->HasProperty("CanCollide")) {
        auto cv = part->GetProperty("CanCollide");
        if (std::holds_alternative<bool>(cv)) canCollide = std::get<bool>(cv);
    }
    if (!canCollide) return;

    CreateJoltBody(part);
}

void JoltPhysics::UnregisterPart(Instance* part) {
    RemoveJoltBody(part);
}

void JoltPhysics::Step(float dt) {
    if (!physicsSystem) return;
    if (dt <= 0.0f || dt > 0.1f) dt = 1.0f / 60.0f;

    // ── Dynamic Synchronizer (Anchored / CanCollide / Destroyed) ──
    for (auto it = partToBodyID.begin(); it != partToBodyID.end();) {
        Instance* inst = it->first;
        uint32_t bodyID = it->second;

        // 1. Destroyed check
        if (!inst || inst->IsDestroyed()) {
            BodyID bid(bodyID);
            bodyInterface->RemoveBody(bid);
            bodyInterface->DestroyBody(bid);
            bodyIDToPart.erase(bodyID);
            it = partToBodyID.erase(it);
            continue;
        }

        // 2. CanCollide check (remove body if turned off)
        bool canCollide = true;
        if (inst->HasProperty("CanCollide")) {
            auto cv = inst->GetProperty("CanCollide");
            if (std::holds_alternative<bool>(cv)) canCollide = std::get<bool>(cv);
        }
        if (!canCollide) {
            BodyID bid(bodyID);
            bodyInterface->RemoveBody(bid);
            bodyInterface->DestroyBody(bid);
            bodyIDToPart.erase(bodyID);
            it = partToBodyID.erase(it);
            continue;
        }

        // 3. Anchored check (sync static vs dynamic layer)
        bool anchored = false;
        if (inst->HasProperty("Anchored")) {
            auto av = inst->GetProperty("Anchored");
            if (std::holds_alternative<bool>(av)) anchored = std::get<bool>(av);
        }
        BodyID bid(bodyID);
        EMotionType current = bodyInterface->GetMotionType(bid);
        EMotionType desired = anchored ? EMotionType::Static : EMotionType::Dynamic;
        if (current != desired) {
            bodyInterface->SetMotionType(bid, desired, EActivation::Activate);
            bodyInterface->SetObjectLayer(bid, anchored ? Layers::NON_MOVING : Layers::MOVING);
        }

        ++it;
    }

    // Step physics
    physicsSystem->Update(dt, 1, tempAllocator, jobSystem);

    // Sync back positions/orientations
    SyncAllFromJolt();
}

void JoltPhysics::SyncAllFromJolt() {
    for (auto& [inst, bodyID] : partToBodyID) {
        if (!inst || inst->IsDestroyed()) continue;

        BodyID bid(bodyID);
        if (!bodyInterface->IsAdded(bid)) continue;
        if (bodyInterface->GetMotionType(bid) == EMotionType::Static) continue;

        RVec3 jPos = bodyInterface->GetPosition(bid);
        Quat jRot = bodyInterface->GetRotation(bid);

        if (inst->Name == "HumanoidRootPart") {
            // Read HipHeight dynamically to compute the leg offset
            float hipHeight = 2.0f;
            Instance* parentModel = inst->GetParent();
            if (parentModel) {
                Instance* humanoid = parentModel->FindFirstChild("Humanoid");
                if (humanoid && humanoid->HasProperty("HipHeight")) {
                    auto hhv = humanoid->GetProperty("HipHeight");
                    if (std::holds_alternative<double>(hhv)) hipHeight = (float)std::get<double>(hhv);
                    else if (std::holds_alternative<int>(hhv)) hipHeight = (float)std::get<int>(hhv);
                }
            }
            // HRP visual position sits at the hips, which is exactly 0.5 * HipHeight above Jolt's body center
            float offset = 0.5f * hipHeight;
            inst->SetProperty("Position", Vector3((float)jPos.GetX(), (float)jPos.GetY() + offset, (float)jPos.GetZ()));
        } else {
            inst->SetProperty("Position", Vector3((float)jPos.GetX(), (float)jPos.GetY(), (float)jPos.GetZ()));
        }

        // Convert quaternion to Euler angles (degrees) matching our renderer's XYZ convention
        float qx = jRot.GetX(), qy = jRot.GetY(), qz = jRot.GetZ(), qw = jRot.GetW();
        float sinp = 2.0f * (qw * qx + qy * qz);
        float cosp = 1.0f - 2.0f * (qx * qx + qy * qy);
        float pitchX = atan2f(sinp, cosp);
        
        float siny = 2.0f * (qw * qy - qz * qx);
        float yawY = (fabsf(siny) >= 1.0f) ? copysignf(3.14159265f / 2.0f, siny) : asinf(siny);
        
        float sinr = 2.0f * (qw * qz + qx * qy);
        float cosr = 1.0f - 2.0f * (qy * qy + qz * qz);
        float rollZ = atan2f(sinr, cosr);
        
        float deg = 180.0f / 3.14159265f;
        inst->SetProperty("Orientation", Vector3(pitchX * deg, yawY * deg, rollZ * deg));
    }
}

void JoltPhysics::SyncAllToJolt() {
    for (auto& [inst, bodyID] : partToBodyID) {
        if (!inst || inst->IsDestroyed()) continue;

        Vector3 pos(0, 0, 0);
        if (inst->HasProperty("Position")) {
            auto pv = inst->GetProperty("Position");
            if (std::holds_alternative<Vector3>(pv)) pos = std::get<Vector3>(pv);
        }

        BodyID bid(bodyID);
        bodyInterface->SetPosition(bid, RVec3(pos.X, pos.Y, pos.Z), EActivation::Activate);
    }
}

// ═══════════════════════════════════════════════════════
// Velocity Access
// ═══════════════════════════════════════════════════════
Vector3 JoltPhysics::GetLinearVelocity(Instance* part) {
    auto it = partToBodyID.find(part);
    if (it == partToBodyID.end()) return {0, 0, 0};
    Vec3 v = bodyInterface->GetLinearVelocity(BodyID(it->second));
    return {(float)v.GetX(), (float)v.GetY(), (float)v.GetZ()};
}

void JoltPhysics::SetLinearVelocity(Instance* part, const Vector3& vel) {
    auto it = partToBodyID.find(part);
    if (it == partToBodyID.end()) return;
    bodyInterface->SetLinearVelocity(BodyID(it->second), Vec3(vel.X, vel.Y, vel.Z));
}

Vector3 JoltPhysics::GetAngularVelocity(Instance* part) {
    auto it = partToBodyID.find(part);
    if (it == partToBodyID.end()) return {0, 0, 0};
    Vec3 v = bodyInterface->GetAngularVelocity(BodyID(it->second));
    float deg = 180.0f / 3.14159265f;
    return {(float)v.GetX() * deg, (float)v.GetY() * deg, (float)v.GetZ() * deg};
}

void JoltPhysics::SetAngularVelocity(Instance* part, const Vector3& vel) {
    auto it = partToBodyID.find(part);
    if (it == partToBodyID.end()) return;
    float rad = 3.14159265f / 180.0f;
    bodyInterface->SetAngularVelocity(BodyID(it->second), Vec3(vel.X * rad, vel.Y * rad, vel.Z * rad));
}

// ═══════════════════════════════════════════════════════
// Jolt Narrow-Phase Raycaster (Character Standing Math)
// ═══════════════════════════════════════════════════════
// Custom body filter to ignore a specific body (like the player's own HRP)
bool JoltPhysics::Raycast(const Vector3& origin, const Vector3& direction, float maxDistance, float& hitY, Vector3& hitNormal, Instance* ignorePart) {
    if (!physicsSystem) return false;

    RRayCast ray;
    ray.mOrigin = RVec3(origin.X, origin.Y, origin.Z);
    ray.mDirection = Vec3(direction.X, direction.Y, direction.Z) * maxDistance;

    RayCastResult result;
    const NarrowPhaseQuery& query = physicsSystem->GetNarrowPhaseQuery();
    
    if (ignorePart) {
        auto it = partToBodyID.find(ignorePart);
        if (it != partToBodyID.end()) {
            // Use Jolt's native IgnoreSingleBodyFilter (with JPH:: namespace)
            JPH::IgnoreSingleBodyFilter filter(BodyID(it->second));
            // Passing {} for broadphase filters uses Jolt's clean defaults!
            if (query.CastRay(ray, result, {}, {}, filter)) {
                RVec3 hitPos = ray.GetPointOnRay(result.mFraction);
                hitY = (float)hitPos.GetY();
                return true;
            }
            return false;
        }
    }

    if (query.CastRay(ray, result)) {
        RVec3 hitPos = ray.GetPointOnRay(result.mFraction);
        hitY = (float)hitPos.GetY();
        return true;
    }
    return false;
}