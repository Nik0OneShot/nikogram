#pragma once
#include <algorithm>
#include <cmath>

// Camera-only optical magnification. No saved FOV, player netvars or cvars.
namespace OptifineZoom
{
    struct State
    {
        float amount = 0.f;
        void Reset() { amount = 0.f; }
        void Update(bool held, bool eligible, bool smooth, float duration, float dt)
        {
            if (!eligible) { Reset(); return; }
            if (!std::isfinite(amount)) Reset();
            if (!smooth) { amount = held ? 1.f : 0.f; return; }
            duration = std::isfinite(duration) ? std::clamp(duration, 0.05f, 0.5f) : 0.18f;
            dt = std::isfinite(dt) ? std::clamp(dt, 0.f, 0.1f) : 0.f;
            amount = std::clamp(amount + (held ? 1.f : -1.f) * dt / duration, 0.f, 1.f);
        }
        float Scale(float magnification) const
        {
            magnification = std::isfinite(magnification) ? std::clamp(magnification, 1.f, 10.f) : 4.f;
            const float t = std::isfinite(amount) ? std::clamp(amount, 0.f, 1.f) : 0.f;
            const float eased = t * t * (3.f - 2.f * t);
            return 1.f + (1.f / magnification - 1.f) * eased;
        }
        float Fov(float base, float magnification) const
        {
            // Return the current base exactly when released, including scope transitions.
            if (!std::isfinite(base) || base <= 0.f || base >= 179.f || amount <= 0.f) return base;
            constexpr float radians = 0.017453292519943295f;
            return 2.f * std::atan(std::tan(base * radians * 0.5f) * Scale(magnification)) / radians;
        }
    };
}
