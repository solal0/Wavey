#pragma once
// ═══════════════════════════════════════════════════════
// Wavey Studio - Math Types
// These mirror Roblox's Vector3, Color3, CFrame, etc.
// ═══════════════════════════════════════════════════════

#include <string>
#include <cmath>

// ── Vector3 ── (Position, Size, etc.)
struct Vector3 {
    float X = 0, Y = 0, Z = 0;

    Vector3() = default;
    Vector3(float x, float y, float z) : X(x), Y(y), Z(z) {}

    Vector3 operator+(const Vector3& o) const { return {X + o.X, Y + o.Y, Z + o.Z}; }
    Vector3 operator-(const Vector3& o) const { return {X - o.X, Y - o.Y, Z - o.Z}; }
    Vector3 operator*(float s) const { return {X * s, Y * s, Z * s}; }

    float Magnitude() const { return std::sqrt(X * X + Y * Y + Z * Z); }

    static Vector3 zero() { return {0, 0, 0}; }
    static Vector3 one() { return {1, 1, 1}; }
    static Vector3 up() { return {0, 1, 0}; }

    std::string ToString() const {
        return std::to_string(X) + ", " + std::to_string(Y) + ", " + std::to_string(Z);
    }
};

// ── Color3 ── (BrickColor, Part colors, etc.)
struct Color3 {
    float R = 0, G = 0, B = 0; // 0.0 to 1.0

    Color3() = default;
    Color3(float r, float g, float b) : R(r), G(g), B(b) {}

    // Like Color3.fromRGB(255, 0, 0)
    static Color3 FromRGB(int r, int g, int b) {
        return {r / 255.f, g / 255.f, b / 255.f};
    }

    std::string ToString() const {
        return std::to_string(R) + ", " + std::to_string(G) + ", " + std::to_string(B);
    }
};

// ── CFrame ── (Simplified: position + euler rotation)
struct CFrame {
    Vector3 Position;
    float RX = 0, RY = 0, RZ = 0; // Rotation in radians

    CFrame() = default;
    CFrame(float x, float y, float z) : Position(x, y, z) {}
    CFrame(Vector3 pos) : Position(pos) {}

    std::string ToString() const { return Position.ToString(); }
};

// ── UDim ── (Single axis layout unit: Scale + Offset)
struct UDim {
    float Scale = 0.0f;
    int Offset = 0;

    UDim() = default;
    UDim(float s, int o) : Scale(s), Offset(o) {}

    std::string ToString() const {
        return "{" + std::to_string(Scale) + ", " + std::to_string(Offset) + "}";
    }
};

// ── UDim2 ── (Two-dimensional layout unit: X and Y axes)
struct UDim2 {
    UDim X;
    UDim Y;

    UDim2() = default;
    UDim2(float xs, int xo, float ys, int yo) : X(xs, xo), Y(ys, yo) {}
    UDim2(UDim x, UDim y) : X(x), Y(y) {}

    std::string ToString() const {
        return "{" + X.ToString() + ", " + Y.ToString() + "}";
    }
};