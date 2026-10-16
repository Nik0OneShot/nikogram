#pragma once
#include <algorithm>
#include <cmath>

// Camera-only optical magnification. No saved FOV, player netvars or cvars.
namespace OptifineZoom
{
    struct WheelButton
    {
        bool captured = false;
        bool Event(bool down, bool eligible)
        {
            if (down) return captured = eligible;
            const bool consume = captured;
            captured = false;
            return consume;
        }
    };
    struct Wheel
    {
        float configured = -1.f, value = 4.f;
        float Effective(float setting)
        {
            setting = std::isfinite(setting) ? std::clamp(setting, 1.f, 10.f) : 4.f;
            if (configured != setting) configured = value = setting;
            return value;
        }
        void Adjust(float setting, int steps)
        { value = std::clamp(Effective(setting) + float(steps) * .5f, 1.f, 10.f); }
    };
    struct Level
    {
        float scale = 1.f;
        bool initialized = false;
        void Reset() { scale = 1.f; initialized = false; }
        void Update(float magnification, bool smooth, float duration, float dt, bool visible)
        {
            magnification = std::isfinite(magnification) ? std::clamp(magnification, 1.f, 10.f) : 4.f;
            const float target = 1.f / magnification;
            // Preserve the existing activation ramp. A hidden zoom has no level
            // transition to carry into the next activation.
            if (!initialized || !visible || !smooth || !std::isfinite(scale))
                scale = target;
            else
            {
                duration = std::isfinite(duration) ? std::clamp(duration, 0.05f, 0.5f) : 0.18f;
                dt = std::isfinite(dt) && dt > 0.f ? dt : 0.f;
                // Exact exponential decay in optical scale: 99% of the change
                // within the configured transition time, independent of FPS.
                // Retarget from the displayed value, with no queued wheel steps.
                const float blend = -std::expm1(-4.605170186f * dt / duration);
                scale += (target - scale) * blend;
            }
            scale = std::clamp(scale, 0.1f, 1.f);
            initialized = true;
        }
        float Magnification() const { return 1.f / scale; }
    };
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
