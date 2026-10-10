#include "Physics.h"
#include <cmath>
#include <algorithm>
#include <vector>

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

    // ── Phase 1: Gravity + Integration ──
    for (auto* part : parts) {
        if (IsAnchored(part)) continue;
        if (part->Name == "HumanoidRootPart") continue; // Handled by character controller

        RigidBody& body = bodies[part];
        body.velocity.Y += GRAVITY * dt;

        Vector3 pos = GetPos(part);
        pos.X += body.velocity.X * dt;
        pos.Y += body.velocity.Y * dt;
        pos.Z += body.velocity.Z * dt;
        SetPos(part, pos);
    }

    // ── Phase 2: Ground collision ──
    for (auto* part : parts) {
        if (IsAnchored(part) || !IsCanCollide(part)) continue;

        Vector3 pos = GetPos(part);
        Vector3 size = GetSize(part);
        float bottomY = pos.Y - size.Y / 2.0f;

        if (bottomY < GROUND_Y) {
            pos.Y = GROUND_Y + size.Y / 2.0f;
            SetPos(part, pos);

            RigidBody& body = bodies[part];
            if (body.velocity.Y < 0) {
                body.velocity.Y *= -RESTITUTION;
                if (fabsf(body.velocity.Y) < 1.5f)
                    body.velocity.Y = 0;
            }
            body.velocity.X *= FRICTION;
            body.velocity.Z *= FRICTION;
        }
    }

    // ── Phase 3: Part-part AABB collision ──
    // Multiple iterations for stability (prevents sinking through stacks)
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

                // AABB overlap test
                float overlapX = (aSize.X + bSize.X) / 2.0f - fabsf(aPos.X - bPos.X);
                float overlapY = (aSize.Y + bSize.Y) / 2.0f - fabsf(aPos.Y - bPos.Y);
                float overlapZ = (aSize.Z + bSize.Z) / 2.0f - fabsf(aPos.Z - bPos.Z);

                if (overlapX <= 0 || overlapY <= 0 || overlapZ <= 0) continue;

                // Push apart along minimum overlap axis
                // KEY FIX: anchored parts don't move, so the non-anchored
                // part must absorb the FULL overlap, not half.
                if (overlapX <= overlapY && overlapX <= overlapZ) {
                    float pushA = aAnc ? 0.0f : (bAnc ? overlapX : overlapX / 2.0f);
                    float pushB = bAnc ? 0.0f : (aAnc ? overlapX : overlapX / 2.0f);

                    if (aPos.X < bPos.X) {
                        aPos.X -= pushA;
                        bPos.X += pushB;
                    } else {
                        aPos.X += pushA;
                        bPos.X -= pushB;
                    }

                    if (!aAnc) { bodies[a].velocity.X = 0; SetPos(a, aPos); }
                    if (!bAnc) { bodies[b].velocity.X = 0; SetPos(b, bPos); }
                }
                else if (overlapY <= overlapX && overlapY <= overlapZ) {
                    float pushA = aAnc ? 0.0f : (bAnc ? overlapY : overlapY / 2.0f);
                    float pushB = bAnc ? 0.0f : (aAnc ? overlapY : overlapY / 2.0f);

                    if (aPos.Y < bPos.Y) {
                        aPos.Y -= pushA;
                        bPos.Y += pushB;
                    } else {
                        aPos.Y += pushA;
                        bPos.Y -= pushB;
                    }

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

                    if (aPos.Z < bPos.Z) {
                        aPos.Z -= pushA;
                        bPos.Z += pushB;
                    } else {
                        aPos.Z += pushA;
                        bPos.Z -= pushB;
                    }

                    if (!aAnc) { bodies[a].velocity.Z = 0; SetPos(a, aPos); }
                    if (!bAnc) { bodies[b].velocity.Z = 0; SetPos(b, bPos); }
                }
            }
        }
    }
}