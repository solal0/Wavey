#include "Physics.h"
#include <algorithm>
#include <vector>
#include <cmath>

RigidBody& Physics::GetBody(Instance* part) {
    return bodies[part];
}

bool Physics::HasBody(Instance* part) const {
    return bodies.count(part) > 0;
}

void Physics::Reset() {
    bodies.clear();
}

void Physics::CollectParts(Instance* node, std::vector<Instance*>& out) {
    if (!node || node->IsDestroyed()) return;
    if (node->IsA("Part")) out.push_back(node);
    for (auto* child : node->GetChildren()) {
        if (!child->IsDestroyed())
            CollectParts(child, out);
    }
}

static bool IsAnchored(Instance* part) {
    if (part->HasProperty("Anchored")) {
        auto av = part->GetProperty("Anchored");
        if (std::holds_alternative<bool>(av))
            return std::get<bool>(av);
    }
    return false;
}

static bool IsCanCollide(Instance* part) {
    if (part->HasProperty("CanCollide")) {
        auto cv = part->GetProperty("CanCollide");
        if (std::holds_alternative<bool>(cv))
            return std::get<bool>(cv);
    }
    return true; // Default to true
}

static Vector3 GetPos(Instance* part) {
    if (part->HasProperty("Position")) {
        auto pv = part->GetProperty("Position");
        if (std::holds_alternative<Vector3>(pv))
            return std::get<Vector3>(pv);
    }
    return {0, 0, 0};
}

static void SetPos(Instance* part, Vector3 pos) {
    part->SetProperty("Position", pos);
}

static Vector3 GetOrient(Instance* part) {
    if (part->HasProperty("Orientation")) {
        auto ov = part->GetProperty("Orientation");
        if (std::holds_alternative<Vector3>(ov))
            return std::get<Vector3>(ov);
    }
    return {0, 0, 0};
}

static void SetOrient(Instance* part, Vector3 orient) {
    part->SetProperty("Orientation", orient);
}

static Vector3 GetSize(Instance* part) {
    if (part->HasProperty("Size")) {
        auto sv = part->GetProperty("Size");
        if (std::holds_alternative<Vector3>(sv))
            return std::get<Vector3>(sv);
    }
    return {4, 1, 2};
}

bool Physics::GetPartAABB(Instance* part, Vector3& bMin, Vector3& bMax) {
    Vector3 pos = GetPos(part);
    Vector3 size = GetSize(part);
    bMin = {pos.X - size.X / 2, pos.Y - size.Y / 2, pos.Z - size.Z / 2};
    bMax = {pos.X + size.X / 2, pos.Y + size.Y / 2, pos.Z + size.Z / 2};
    return true;
}

void Physics::Step(Instance* root, float dt) {
    if (dt <= 0.0f || dt > 0.1f) return;

    std::vector<Instance*> parts;
    CollectParts(root, parts);

    // ── Phase 1: Gravity + Linear + Angular Integration ──
    for (auto* part : parts) {
        if (IsAnchored(part)) continue;
        if (part->Name == "HumanoidRootPart") continue; // Handled by character controller

        RigidBody& body = bodies[part];

        // Linear gravity
        body.velocity.Y += GRAVITY * dt;

        // Integrate linear position
        Vector3 pos = GetPos(part);
        pos.X += body.velocity.X * dt;
        pos.Y += body.velocity.Y * dt;
        pos.Z += body.velocity.Z * dt;
        SetPos(part, pos);

        // Integrate angular velocity → Orientation (degrees)
        Vector3 orient = GetOrient(part);
        orient.X += body.angularVelocity.X * dt;
        orient.Y += body.angularVelocity.Y * dt;
        orient.Z += body.angularVelocity.Z * dt;

        // Normalize to -180..180 to prevent float overflow over time
        auto normalizeAngle = [](float a) -> float {
            while (a > 180.0f) a -= 360.0f;
            while (a < -180.0f) a += 360.0f;
            return a;
        };
        orient.X = normalizeAngle(orient.X);
        orient.Y = normalizeAngle(orient.Y);
        orient.Z = normalizeAngle(orient.Z);
        SetOrient(part, orient);

        // Apply angular damping
        body.angularVelocity.X *= body.angularDamping;
        body.angularVelocity.Y *= body.angularDamping;
        body.angularVelocity.Z *= body.angularDamping;

        // Kill tiny angular velocities to let things settle
        if (fabsf(body.angularVelocity.X) < 0.5f) body.angularVelocity.X = 0;
        if (fabsf(body.angularVelocity.Y) < 0.5f) body.angularVelocity.Y = 0;
        if (fabsf(body.angularVelocity.Z) < 0.5f) body.angularVelocity.Z = 0;
    }

    // ── Phase 2: Ground Collision with Torque ──
    for (auto* part : parts) {
        if (IsAnchored(part)) continue;
        if (part->Name == "HumanoidRootPart") continue;

        Vector3 pos = GetPos(part);
        Vector3 size = GetSize(part);
        RigidBody& body = bodies[part];

        // Compute the actual bottom of the part's AABB
        // For a non-rotated part, bottom = pos.Y - size.Y/2
        // For rotated parts, we use the AABB extent (conservative)
        float maxExtent = (std::max)({size.X, size.Y, size.Z}) * 0.5f;
        float bottomY = pos.Y - maxExtent;

        if (bottomY < GROUND_Y) {
            // Push part up so its bottom rests on the ground
            float penetration = GROUND_Y - bottomY;
            pos.Y += penetration;
            SetPos(part, pos);

            // Kill downward velocity
            if (body.velocity.Y < 0) {
                body.velocity.Y *= -RESTITUTION;
                if (fabsf(body.velocity.Y) < 2.0f) body.velocity.Y = 0;
            }

            // Ground friction on linear velocity
            body.velocity.X *= FRICTION;
            body.velocity.Z *= FRICTION;

            // ── Ground contact torque ──
            // If the part is tilted, the ground pushes up on the lowest corner,
            // creating a torque that tends to flatten or tumble the part.
            float tiltX = pos.X - GetOrient(part).X; // Simplified tilt measure
            float tiltZ = pos.Z - GetOrient(part).Z;

            // Use the actual orientation to compute a righting torque
            Vector3 orient = GetOrient(part);
            float radX = orient.X * 3.14159265f / 180.0f;
            float radZ = orient.Z * 3.14159265f / 180.0f;

            // Torque proportional to tilt angle (tries to flatten the part)
            float torqueStrength = penetration * 50.0f; // Stronger when deeper penetration
            body.angularVelocity.X += -sinf(radX) * torqueStrength * dt;
            body.angularVelocity.Z += -sinf(radZ) * torqueStrength * dt;

            // Add slight random perturbation for natural-looking tumbling
            // (only when the part first hits, i.e., has significant downward velocity)
            if (penetration > 0.1f) {
                body.angularVelocity.X += ((rand() % 100) - 50) * 0.1f * dt;
                body.angularVelocity.Z += ((rand() % 100) - 50) * 0.1f * dt;
            }

            // Angular friction from ground contact
            body.angularVelocity.X *= 0.95f;
            body.angularVelocity.Z *= 0.95f;
        }
    }

    // ── Phase 3: Part-Part AABB Collision with Angular Impulse ──
    for (int iter = 0; iter < 4; iter++) {
        for (size_t i = 0; i < parts.size(); i++) {
            for (size_t j = i + 1; j < parts.size(); j++) {
                Instance* a = parts[i];
                Instance* b = parts[j];

                if (!IsCanCollide(a) || !IsCanCollide(b)) continue;

                bool aAnc = IsAnchored(a);
                bool bAnc = IsAnchored(b);
                if (aAnc && bAnc) continue;

                Vector3 aPos = GetPos(a);
                Vector3 bPos = GetPos(b);
                Vector3 aSize = GetSize(a);
                Vector3 bSize = GetSize(b);

                float overlapX = (aSize.X + bSize.X) / 2.0f - fabsf(aPos.X - bPos.X);
                float overlapY = (aSize.Y + bSize.Y) / 2.0f - fabsf(aPos.Y - bPos.Y);
                float overlapZ = (aSize.Z + bSize.Z) / 2.0f - fabsf(aPos.Z - bPos.Z);

                if (overlapX <= 0 || overlapY <= 0 || overlapZ <= 0) continue;

                // Push apart along minimum overlap axis
                if (overlapX <= overlapY && overlapX <= overlapZ) {
                    float pushA = aAnc ? 0.0f : (bAnc ? overlapX : overlapX / 2.0f);
                    float pushB = bAnc ? 0.0f : (aAnc ? overlapX : overlapX / 2.0f);

                    if (aPos.X < bPos.X) { aPos.X -= pushA; bPos.X += pushB; }
                    else { aPos.X += pushA; bPos.X -= pushB; }

                    if (!aAnc) { bodies[a].velocity.X = 0; SetPos(a, aPos); }
                    if (!bAnc) { bodies[b].velocity.X = 0; SetPos(b, bPos); }

                    // Angular impulse from off-center X collision
                    float contactOffsetY = (aPos.Y - bPos.Y) * 0.5f;
                    float impulse = overlapX * 10.0f;
                    if (!aAnc) bodies[a].angularVelocity.Z += contactOffsetY * impulse * dt;
                    if (!bAnc) bodies[b].angularVelocity.Z -= contactOffsetY * impulse * dt;
                }
                else if (overlapY <= overlapX && overlapY <= overlapZ) {
                    float pushA = aAnc ? 0.0f : (bAnc ? overlapY : overlapY / 2.0f);
                    float pushB = bAnc ? 0.0f : (aAnc ? overlapY : overlapY / 2.0f);

                    if (aPos.Y < bPos.Y) { aPos.Y -= pushA; bPos.Y += pushB; }
                    else { aPos.Y += pushA; bPos.Y -= pushB; }

                    if (!aAnc) {
                        if (bodies[a].velocity.Y < 0) bodies[a].velocity.Y *= -RESTITUTION;
                        else bodies[a].velocity.Y = 0;
                        if (fabsf(bodies[a].velocity.Y) < 1.5f) bodies[a].velocity.Y = 0;
                        SetPos(a, aPos);
                    }
                    if (!bAnc) {
                        if (bodies[b].velocity.Y < 0) bodies[b].velocity.Y *= -RESTITUTION;
                        else bodies[b].velocity.Y = 0;
                        if (fabsf(bodies[b].velocity.Y) < 1.5f) bodies[b].velocity.Y = 0;
                        SetPos(b, bPos);
                    }
                }
                else {
                    float pushA = aAnc ? 0.0f : (bAnc ? overlapZ : overlapZ / 2.0f);
                    float pushB = bAnc ? 0.0f : (aAnc ? overlapZ : overlapZ / 2.0f);

                    if (aPos.Z < bPos.Z) { aPos.Z -= pushA; bPos.Z += pushB; }
                    else { aPos.Z += pushA; bPos.Z -= pushB; }

                    if (!aAnc) { bodies[a].velocity.Z = 0; SetPos(a, aPos); }
                    if (!bAnc) { bodies[b].velocity.Z = 0; SetPos(b, bPos); }

                    // Angular impulse from off-center Z collision
                    float contactOffsetY = (aPos.Y - bPos.Y) * 0.5f;
                    float impulse = overlapZ * 10.0f;
                    if (!aAnc) bodies[a].angularVelocity.X -= contactOffsetY * impulse * dt;
                    if (!bAnc) bodies[b].angularVelocity.X += contactOffsetY * impulse * dt;
                }
            }
        }
    }
}