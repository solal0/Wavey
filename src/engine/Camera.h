#pragma once
#include "../core/Types.h"
#include <cmath>
#include <algorithm>

class Camera {
public:
    Vector3 Target = {0, 5, 0};
    float Distance = 30.0f;
    float Yaw = -45.0f;
    float Pitch = 30.0f;
    float FOV = 70.0f;
    float NearClip = 0.1f;
    float FarClip = 1000.0f;

    Vector3 GetPosition() const {
        float yR = Yaw * 3.14159265f / 180.0f;
        float pR = Pitch * 3.14159265f / 180.0f;
        return {
            Target.X + Distance * cosf(pR) * sinf(yR),
            Target.Y + Distance * sinf(pR),
            Target.Z + Distance * cosf(pR) * cosf(yR)
        };
    }

    void GetViewMatrix(float* m) const {
        Vector3 pos = GetPosition();
        Vector3 f = {Target.X - pos.X, Target.Y - pos.Y, Target.Z - pos.Z};
        float len = f.Magnitude();
        f = {f.X / len, f.Y / len, f.Z / len};
        Vector3 up = {0, 1, 0};
        Vector3 r = {f.Y * up.Z - f.Z * up.Y, f.Z * up.X - f.X * up.Z, f.X * up.Y - f.Y * up.X};
        len = r.Magnitude();
        r = {r.X / len, r.Y / len, r.Z / len};
        up = {r.Y * f.Z - r.Z * f.Y, r.Z * f.X - r.X * f.Z, r.X * f.Y - r.Y * f.X};

        m[0] = r.X;   m[4] = r.Y;   m[8]  = r.Z;   m[12] = -(r.X * pos.X + r.Y * pos.Y + r.Z * pos.Z);
        m[1] = up.X;  m[5] = up.Y;  m[9]  = up.Z;  m[13] = -(up.X * pos.X + up.Y * pos.Y + up.Z * pos.Z);
        m[2] = -f.X;  m[6] = -f.Y;  m[10] = -f.Z;  m[14] = (f.X * pos.X + f.Y * pos.Y + f.Z * pos.Z);
        m[3] = 0;     m[7] = 0;     m[11] = 0;     m[15] = 1;
    }

    void GetProjectionMatrix(float aspect, float* m) const {
        float t = tanf(FOV * 3.14159265f / 360.0f);
        m[0] = 1.0f / (aspect * t); m[4] = 0;            m[8]  = 0;                                      m[12] = 0;
        m[1] = 0;                    m[5] = 1.0f / t;    m[9]  = 0;                                      m[13] = 0;
        m[2] = 0;                    m[6] = 0;            m[10] = -(FarClip + NearClip) / (FarClip - NearClip); m[14] = -(2 * FarClip * NearClip) / (FarClip - NearClip);
        m[3] = 0;                    m[7] = 0;            m[11] = -1;                                     m[15] = 0;
    }

    void GetRightVector(float* out) const {
        float yR = Yaw * 3.14159265f / 180.0f;
        out[0] = cosf(yR); out[1] = 0; out[2] = -sinf(yR);
    }

    void GetUpVector(float* out) const {
        out[0] = 0; out[1] = 1; out[2] = 0;
    }
};