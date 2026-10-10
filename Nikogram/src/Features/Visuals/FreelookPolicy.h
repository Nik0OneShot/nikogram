#pragma once
#include <algorithm>
#include <cmath>

// Camera state only: no packet, animation, movement or saved engine-angle writes.
namespace Freelook
{
    struct Angles { float pitch = 0.f, yaw = 0.f; };
    struct Settings
    {
        bool limited = false, smooth = false;
        float horizontal = 90.f, vertical = 60.f, duration = 0.2f;
    };
    inline float Wrap(float angle)
    {
        if (!std::isfinite(angle)) return 0.f;
        return std::remainder(angle, 360.f);
    }
    inline float Bound(float value, float low, float high, float fallback)
    {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    }
    struct State
    {
        bool held = false, returning = false;
        Angles anchor{}, offset{}, returnStart{};
        float elapsed = 0.f;

        void Reset() { *this = {}; }
        bool Active() const { return held || returning; }
        Angles View(Angles base) const
        {
            if (held) base = anchor;
            return { Bound(base.pitch + offset.pitch, -89.f, 89.f, 0.f), Wrap(base.yaw + offset.yaw) };
        }
        void Limit(const Settings& settings)
        {
            if (settings.limited)
            {
                const float horizontal = Bound(settings.horizontal, 1.f, 180.f, 90.f);
                const float vertical = Bound(settings.vertical, 1.f, 89.f, 60.f);
                offset.yaw = std::clamp(offset.yaw, -horizontal, horizontal);
                offset.pitch = std::clamp(offset.pitch, -vertical, vertical);
            }
            else offset.yaw = Wrap(offset.yaw);
            offset.pitch = Bound(anchor.pitch + offset.pitch, -89.f, 89.f, anchor.pitch) - anchor.pitch;
        }
        void Sync(bool requested, bool eligible, Angles base, const Settings& settings)
        {
            if (!eligible || !std::isfinite(base.pitch) || !std::isfinite(base.yaw)) { Reset(); return; }
            if (requested && !held)
            {
                // Re-hold during return captures the visible camera without a jump.
                const Angles visible = View(base);
                anchor = { Bound(base.pitch, -89.f, 89.f, 0.f), Wrap(base.yaw) };
                offset = { visible.pitch - anchor.pitch, Wrap(visible.yaw - anchor.yaw) };
                held = true; returning = false; elapsed = 0.f;
            }
            else if (!requested && held)
            {
                held = false;
                // Translate the absolute held camera into an offset from live aim.
                offset.pitch += anchor.pitch - base.pitch;
                offset.yaw = Wrap(offset.yaw + anchor.yaw - base.yaw);
                if (settings.smooth)
                {
                    returning = true; returnStart = offset; elapsed = 0.f;
                }
                else Reset();
            }
            if (returning && !settings.smooth) Reset();
            if (held) Limit(settings);
        }
        void SetCamera(Angles camera, const Settings& settings)
        {
            if (!held || !std::isfinite(camera.pitch) || !std::isfinite(camera.yaw)) return;
            offset = { camera.pitch - anchor.pitch, Wrap(camera.yaw - anchor.yaw) };
            Limit(settings);
        }
        void AddMouse(float pitch, float yaw, const Settings& settings)
        {
            if (!held || !std::isfinite(pitch) || !std::isfinite(yaw)) return;
            offset.pitch += pitch; offset.yaw += yaw;
            Limit(settings);
        }
        void Advance(float dt, const Settings& settings)
        {
            if (!returning) return;
            const float duration = Bound(settings.duration, 0.05f, 1.f, 0.2f);
            elapsed += Bound(dt, 0.f, 1.f, 0.f);
            const float t = std::clamp(elapsed / duration, 0.f, 1.f);
            const float remaining = 1.f - t * t * (3.f - 2.f * t);
            offset = { returnStart.pitch * remaining, returnStart.yaw * remaining };
            if (t >= 1.f) Reset();
        }
    };

    // Input has already passed through native filtering/sensitivity/acceleration.
    // Consume the final deltas directly, avoiding ApplyMouse's gameplay, strafe
    // and thirdperson-platformer side effects while the camera is detached.
    template<class View, class Command>
    bool RedirectMouse(State& state, const Settings& settings, View& view, Command* command,
        float mouseX, float mouseY, float yawScale, float pitchScale)
    {
        if (!state.held || !command) return false;
        state.AddMouse(mouseY * pitchScale, -mouseX * yawScale, settings);
        view.x = state.anchor.pitch; view.y = state.anchor.yaw;
        command->viewangles = view;
        command->mousedx = command->mousedy = 0;
        return true;
    }
}
