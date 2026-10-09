#pragma once
#include <charconv>
#include <cmath>
#include <string_view>

// Local diagnostic transport only. No aim, animation or packet scheduling policy.
namespace PreviewFeedPolicy
{
    constexpr float SendInterval = 0.075f; // <14 string commands/sec, no forced flush
    constexpr float MaxBuildAge = 0.20f;
    constexpr float MaxHeadDistance = 192.f;

    inline bool ParseToken(std::string_view text, int& token)
    {
        const auto result = std::from_chars(text.data(), text.data() + text.size(), token);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size() && token > 0;
    }

    inline bool LocalLabAddress(std::string_view address)
    {
        // Do not treat LAN addresses or a public server called "itemtest" as local.
        if (address == "loopback") return true;
        constexpr std::string_view prefix = "127.0.0.1:";
        if (!address.starts_with(prefix)) return false;
        int port = 0;
        return ParseToken(address.substr(prefix.size()), port) && port <= 65535;
    }

    inline bool Itemtest(std::string_view level)
    {
        return level == "maps/itemtest.bsp" || level == "maps\\itemtest.bsp" || level == "itemtest";
    }

    inline bool Fresh(float now, float built)
    {
        return std::isfinite(now) && std::isfinite(built) && now >= built && now - built <= MaxBuildAge;
    }

    inline bool NearOrigin(const float point[3], const float origin[3])
    {
        float distanceSquared = 0.f;
        for (int k = 0; k < 3; ++k)
        {
            if (!std::isfinite(point[k]) || !std::isfinite(origin[k])
                || std::abs(point[k]) > 65536.f || std::abs(origin[k]) > 65536.f) return false;
            const float delta = point[k] - origin[k];
            distanceSquared += delta * delta;
        }
        return distanceSquared <= MaxHeadDistance * MaxHeadDistance;
    }
}
